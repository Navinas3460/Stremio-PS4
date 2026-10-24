#include "player_screen.h"

#include "../core/charset.h"
#include "../core/languages.h"
#include "../core/stremio.h"
#include "../net/http.h"
#include "../net/urls.h"
#include "../platform/platform.h"
#include "theme.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace {

using theme::S;

const float W = 1920, H = 1080;
const double SEEK_STEP = 10.0;
const double SEEK_APPLY_DELAY = 0.6; // wait for more presses before seeking
const double OVERLAY_HIDE = 4.0;
const double REPORT_EVERY = 1.0;

// Panel layout (right side).
const float PANEL_W = S(900);
const float PANEL_X = W - PANEL_W;
const float COL_SUB_X = PANEL_X + S(48), COL_SUB_W = S(470);
const float COL_AUDIO_X = COL_SUB_X + COL_SUB_W + S(36), COL_AUDIO_W = W - S(48) - (COL_SUB_X + COL_SUB_W + S(36));
const float LIST_TOP = S(170), ROW_H = S(70), ROW_GAP = S(8);
const float LIST_BOTTOM = H - S(110);
// Language tabs in a box over the subtitles column (like the tab bar's
// daisyUI "tabs-box"); the column starts below it.
const float LANG_TAB_H = S(44), LANG_TAB_GAP = S(6), LANG_BOX_PAD = S(6);
const float LANG_BOX_H = LANG_TAB_H + 2 * LANG_BOX_PAD, LANG_TABS_W = COL_SUB_W - 2 * LANG_BOX_PAD;
const float SUB_LIST_TOP = LIST_TOP + LANG_BOX_H + S(16);

const char *PREF_SUB = "subtitle_language.txt";
const char *PREF_AUDIO = "audio_language.txt";

std::string format_time(double seconds) {
	const int s = (int)std::max(0.0, seconds);
	char buf[32];
	if (s >= 3600) {
		snprintf(buf, sizeof(buf), "%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
	} else {
		snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
	}
	return buf;
}

std::string read_pref(const char *name) {
	std::string v;
	if (!platform::read_file(platform::data_dir() + name, v)) return "";
	while (!v.empty() && (v.back() == '\n' || v.back() == '\r' || v.back() == ' ')) v.pop_back();
	return v;
}

void write_pref(const char *name, const std::string &value) {
	platform::write_file(platform::data_dir() + name, value + "\n");
}

// Two codes for the same language ("en" / "eng" / "English")?
bool same_language(const std::string &a, const std::string &b) {
	if (a.empty() || b.empty()) return false;
	return language_name(a) == language_name(b);
}

// The language tab a subtitle is listed under: unrecognised and untagged
// ones go under "Other".
const char *OTHER_TAB = "Other";
std::string tab_name(const std::string &lang) {
	const char *name = find_language(lang);
	return name && std::string(name) != "Unknown" ? name : OTHER_TAB;
}

} // namespace

PlayerScreen::PlayerScreen(Fonts &fonts, Navigator &nav, const Args &args)
	: fonts_(fonts), nav_(nav), args_(args), player_(VideoPlayer::create()) {
	const int64_t resume_ms = stremio().player_start(args_.meta_id, args_.video_id, args_.has_meta ? &args_.meta : nullptr);
	session_ = true;
	platform::log("player: open %s at %.1f s", urls::for_log(args_.stream.playable_url()).c_str(), resume_ms / 1000.0);
	player_->open(args_.stream.playable_url(), args_.stream.request_headers, resume_ms / 1000.0);
	overlay_timer_ = OVERLAY_HIDE;
	pref_sub_lang_ = read_pref(PREF_SUB);
	pref_audio_lang_ = read_pref(PREF_AUDIO);
	rebuild_subtitle_options();

	// Subtitles from add-ons (OpenSubtitles etc.), matched to this file when
	// the stream says which file it is.
	std::weak_ptr<int> alive = alive_;
	const std::string type = args_.type.empty() ? "movie" : args_.type;
	stremio().request_subtitles(type, args_.video_id, &args_.stream, [this, alive](std::vector<SubtitleItem> &subs) {
		if (alive.expired()) return;
		for (SubtitleItem &s : subs) addon_subs_.push_back(s);
		rebuild_subtitle_options();
		auto_select();
	});
}

PlayerScreen::~PlayerScreen() {
	if (session_) {
		stremio().player_stop();
	}
}

void PlayerScreen::show_overlay() {
	overlay_timer_ = OVERLAY_HIDE;
}

void PlayerScreen::exit() {
	if (session_) {
		session_ = false;
		stremio().player_stop();
	}
	nav_.pop();
}

void PlayerScreen::queue_seek(double step) {
	const double base = pending_seek_ >= 0 ? pending_seek_ : player_->position();
	const double duration = player_->duration();
	pending_seek_ = std::max(0.0, base + step);
	if (duration > 0) pending_seek_ = std::min(pending_seek_, duration);
	seek_timer_ = SEEK_APPLY_DELAY;
	show_overlay();
}

// --- Tracks & subtitles ---------------------------------------------------------------

void PlayerScreen::rebuild_subtitle_options() {
	std::vector<SubtitleOption> options;
	SubtitleOption off;
	off.label = "Off";
	options.push_back(off);
	for (const VideoPlayer::Track &t : player_->subtitle_tracks()) {
		SubtitleOption o;
		o.kind = SubtitleOption::EMBEDDED;
		o.id = t.id;
		o.lang = t.lang;
		o.label = t.label;
		o.detail = "In the video";
		options.push_back(o);
	}
	// Add-on subtitles grouped by language: the preferred language first, then
	// alphabetically; numbered within each language.
	std::vector<int> order(addon_subs_.size());
	for (size_t i = 0; i < order.size(); i++) order[i] = (int)i;
	auto rank = [this](int i) {
		const std::string &lang = addon_subs_[i].lang;
		const int group = same_language(lang, pref_sub_lang_) ? 0 : same_language(lang, "eng") ? 1 : 2;
		return std::make_pair(group, language_name(lang));
	};
	std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return rank(a) < rank(b); });
	std::map<std::string, int> count;
	for (int i : order) {
		const SubtitleItem &s = addon_subs_[i];
		SubtitleOption o;
		o.kind = SubtitleOption::ADDON;
		o.id = i;
		o.lang = s.lang;
		const std::string name = s.lang.empty() ? "Unknown" : language_name(s.lang);
		o.label = name + " " + std::to_string(++count[name]);
		o.detail = s.addon_name + (s.release.empty() ? "" : " \xC2\xB7 " + s.release);
		options.push_back(o);
	}
	// Add-on subtitles can arrive while the panel is open: keep its tab and
	// selected row.
	SubtitleOption selected;
	if (columns_[0].selected < (int)sub_rows_.size()) selected = sub_options_[sub_rows_[columns_[0].selected]];
	const std::string open_tab = menu_open_ && sub_tab_ < (int)sub_tabs_.size() ? sub_tabs_[sub_tab_].name : "";
	sub_options_.swap(options);

	// One tab per language, in the list's order.
	std::map<std::string, int> per_language;
	for (const SubtitleOption &o : sub_options_) {
		if (o.kind != SubtitleOption::OFF) per_language[tab_name(o.lang)]++;
	}
	const std::string preferred = pref_sub_lang_.empty() ? "" : language_name(pref_sub_lang_);
	auto group = [&](const LanguageTab &t) {
		return t.name == OTHER_TAB ? 3 : t.name == preferred ? 0 : t.name == "English" ? 1 : 2;
	};
	std::vector<LanguageTab> tabs;
	for (const auto &[name, n] : per_language) tabs.push_back({ name, name + " (" + std::to_string(n) + ")" });
	std::stable_sort(tabs.begin(), tabs.end(), [&](const LanguageTab &a, const LanguageTab &b) { return group(a) < group(b); });
	float x = 0;
	for (LanguageTab &t : tabs) {
		t.w = ui::chip_width(fonts_.small, t.label);
		t.x = x;
		x += t.w + LANG_TAB_GAP;
	}
	sub_tabs_.swap(tabs);

	int tab = 0;
	for (int i = 0; i < (int)sub_tabs_.size(); i++) {
		if (sub_tabs_[i].name == open_tab) tab = i;
	}
	show_language_tab(tab);
	columns_[0].selected = 0;
	for (int i = 0; i < (int)sub_rows_.size(); i++) {
		const SubtitleOption &o = sub_options_[sub_rows_[i]];
		if (o.kind == selected.kind && o.id == selected.id) columns_[0].selected = i;
	}
}

// Lists "Off" and the subtitles in tab `tab`'s language.
void PlayerScreen::show_language_tab(int tab) {
	sub_tab_ = std::clamp(tab, 0, std::max(0, (int)sub_tabs_.size() - 1));
	// The row shows whole tabs from sub_tabs_first_, moved only as far as it
	// takes to show this one.
	if (sub_tabs_first_ > sub_tab_) sub_tabs_first_ = sub_tab_;
	if (!sub_tabs_.empty()) {
		const LanguageTab &t = sub_tabs_[sub_tab_];
		while (sub_tabs_first_ < sub_tab_ && t.x + t.w - sub_tabs_[sub_tabs_first_].x > LANG_TABS_W) sub_tabs_first_++;
	}
	const std::string name = sub_tabs_.empty() ? "" : sub_tabs_[sub_tab_].name;
	sub_rows_.clear();
	for (int i = 0; i < (int)sub_options_.size(); i++) {
		const SubtitleOption &o = sub_options_[i];
		if (o.kind == SubtitleOption::OFF || tab_name(o.lang) == name) sub_rows_.push_back(i);
	}
}

// Opens the panel on the subtitle (and its language tab) and audio track in use.
void PlayerScreen::open_menu() {
	menu_open_ = true;
	menu_column_ = 0;
	int tab = 0;
	for (const SubtitleOption &o : sub_options_) {
		if (o.kind == SubtitleOption::OFF || o.kind != sub_kind_ || o.id != sub_id_) continue;
		for (int i = 0; i < (int)sub_tabs_.size(); i++) {
			if (sub_tabs_[i].name == tab_name(o.lang)) tab = i;
		}
	}
	show_language_tab(tab);
	columns_[0].selected = 0;
	for (int i = 0; i < (int)sub_rows_.size(); i++) {
		const SubtitleOption &o = sub_options_[sub_rows_[i]];
		if (o.kind == sub_kind_ && (sub_kind_ == SubtitleOption::OFF || o.id == sub_id_)) columns_[0].selected = i;
	}
	for (int i = 0; i < (int)audio_tracks_.size(); i++) {
		if (audio_tracks_[i].id == player_->audio_track()) columns_[1].selected = i;
	}
}

void PlayerScreen::choose_subtitle(const SubtitleOption &option, bool remember) {
	sub_kind_ = option.kind;
	sub_id_ = option.id;
	player_->set_subtitle_track(option.kind == SubtitleOption::EMBEDDED ? option.id : -1);
	if (remember) {
		user_chose_subtitles_ = true;
		pref_sub_lang_ = option.kind == SubtitleOption::OFF ? "off" : option.lang;
		write_pref(PREF_SUB, pref_sub_lang_);
	}
	if (option.kind != SubtitleOption::ADDON || addon_cues_for_ == option.id) return;
	// Download and parse the add-on's file.
	addon_cues_.clear();
	addon_cues_for_ = -1;
	const SubtitleItem item = addon_subs_[option.id];
	const int index = option.id;
	if (remember) toast_.show("Loading subtitles\xE2\x80\xA6");
	std::weak_ptr<int> alive = alive_;
	http::get(item.url, [this, alive, index, lang = item.lang](http::Response &res) {
		if (alive.expired() || sub_kind_ != SubtitleOption::ADDON || sub_id_ != index) return;
		std::vector<SubtitleCue> cues;
		// Files in legacy encodings (Windows-1256 Arabic, GBK Chinese...)
		// are decoded by the subtitle's language.
		if (!res.ok() || !parse_subtitles(to_utf8(res.body, lang), cues)) {
			platform::log("player: subtitles failed (%s, HTTP %d)", res.error.c_str(), res.status);
			toast_.show("Couldn't load these subtitles", true);
			return;
		}
		addon_cues_.swap(cues);
		addon_cues_for_ = index;
		toast_.hide();
		platform::log("player: %zu subtitle cues", addon_cues_.size());
	});
}

void PlayerScreen::choose_audio(int track, bool remember) {
	player_->set_audio_track(track);
	if (!remember) return;
	user_chose_audio_ = true;
	for (const VideoPlayer::Track &t : audio_tracks_) {
		if (t.id == track && !t.lang.empty()) {
			pref_audio_lang_ = t.lang;
			write_pref(PREF_AUDIO, t.lang);
		}
	}
}

// Picks the remembered languages once tracks / add-on subtitles are known.
void PlayerScreen::auto_select() {
	if (!user_chose_audio_ && !audio_auto_done_ && !audio_tracks_.empty()) {
		audio_auto_done_ = true;
		for (const VideoPlayer::Track &t : audio_tracks_) {
			if (same_language(t.lang, pref_audio_lang_)) {
				if (t.id != player_->audio_track()) choose_audio(t.id, false);
				break;
			}
		}
	}
	if (user_chose_subtitles_ || pref_sub_lang_.empty() || pref_sub_lang_ == "off") return;
	if (sub_kind_ == SubtitleOption::EMBEDDED) return; // an embedded track wins over add-ons
	for (const SubtitleOption &o : sub_options_) {
		if (o.kind == SubtitleOption::EMBEDDED && same_language(o.lang, pref_sub_lang_)) {
			choose_subtitle(o, false);
			return;
		}
	}
	if (sub_kind_ == SubtitleOption::ADDON) return;
	for (const SubtitleOption &o : sub_options_) {
		if (o.kind == SubtitleOption::ADDON && same_language(o.lang, pref_sub_lang_)) {
			choose_subtitle(o, false);
			return;
		}
	}
}

void PlayerScreen::update_menu(uint32_t pressed) {
	// L1 / R1: the previous / next language, starting on its first subtitle.
	const int step = (pressed & platform::BUTTON_R1) ? 1 : (pressed & platform::BUTTON_L1) ? -1 : 0;
	if (step != 0 && !sub_tabs_.empty()) {
		const int n = (int)sub_tabs_.size();
		show_language_tab((sub_tab_ + step + n) % n);
		menu_column_ = 0;
		columns_[0].selected = sub_rows_.size() > 1 ? 1 : 0;
	}
	const int count[2] = { (int)sub_rows_.size(), (int)audio_tracks_.size() };
	Column &col = columns_[menu_column_];
	if (pressed & platform::BUTTON_UP) col.selected = std::max(0, col.selected - 1);
	if (pressed & platform::BUTTON_DOWN) col.selected = std::min(count[menu_column_] - 1, col.selected + 1);
	if ((pressed & platform::BUTTON_LEFT) && menu_column_ == 1) menu_column_ = 0;
	if ((pressed & platform::BUTTON_RIGHT) && menu_column_ == 0 && count[1] > 0) menu_column_ = 1;
	if (pressed & platform::BUTTON_ACCEPT) {
		if (menu_column_ == 0 && col.selected < count[0]) {
			choose_subtitle(sub_options_[sub_rows_[col.selected]], true);
		} else if (menu_column_ == 1 && col.selected < count[1]) {
			choose_audio(audio_tracks_[col.selected].id, true);
		}
	}
	if (pressed & (platform::BUTTON_BACK | platform::BUTTON_OPTIONS)) menu_open_ = false;
}

// --- Frame ---------------------------------------------------------------------------

void PlayerScreen::update(double dt, uint32_t pressed) {
	player_->update();
	const VideoPlayer::State state = player_->state();
	if (state != last_state_) {
		platform::log("player: state %d%s%s", (int)state, state == VideoPlayer::FAILED ? " - " : "", player_->error().c_str());
		last_state_ = state;
		if (state == VideoPlayer::PAUSED) show_overlay();
		if (state == VideoPlayer::ENDED) {
			const int64_t d = (int64_t)(player_->duration() * 1000);
			stremio().player_time_changed(d, d);
			exit();
			return;
		}
	}
	// Embedded tracks appear once the stream is open.
	if (state == VideoPlayer::PLAYING || state == VideoPlayer::PAUSED || state == VideoPlayer::BUFFERING) {
		const std::vector<VideoPlayer::Track> audio = player_->audio_tracks();
		const size_t embedded = player_->subtitle_tracks().size();
		size_t known_embedded = 0;
		for (const SubtitleOption &o : sub_options_) known_embedded += o.kind == SubtitleOption::EMBEDDED;
		if (audio.size() != audio_tracks_.size() || embedded != known_embedded) {
			audio_tracks_ = audio;
			rebuild_subtitle_options();
			auto_select();
		}
	}

	if (menu_open_) {
		update_menu(pressed);
	} else {
		if (pressed & platform::BUTTON_BACK) {
			exit();
			return;
		}
		if (state != VideoPlayer::FAILED) {
			if (pressed & platform::BUTTON_ACCEPT) {
				state == VideoPlayer::PAUSED ? player_->play() : player_->pause();
				show_overlay();
			}
			if (pressed & platform::BUTTON_LEFT) queue_seek(-SEEK_STEP);
			if (pressed & platform::BUTTON_RIGHT) queue_seek(SEEK_STEP);
			if (pressed & (platform::BUTTON_UP | platform::BUTTON_OPTIONS)) open_menu();
			if (pressed & platform::BUTTON_DOWN) show_overlay();
		}
	}

	if (pending_seek_ >= 0) {
		seek_timer_ -= dt;
		if (seek_timer_ <= 0) {
			player_->seek(pending_seek_);
			stremio().player_seeked((int64_t)(pending_seek_ * 1000), (int64_t)(player_->duration() * 1000));
			pending_seek_ = -1;
		}
	}
	// Report real playback time only: a seek still being chosen isn't "watched".
	if (state == VideoPlayer::PLAYING && pending_seek_ < 0) {
		report_timer_ -= dt;
		if (report_timer_ <= 0) {
			report_timer_ = REPORT_EVERY;
			stremio().player_time_changed((int64_t)(player_->position() * 1000), (int64_t)(player_->duration() * 1000));
		}
	}

	// Overlay: visible while not playing or recently used; fades out otherwise.
	const bool keep = state != VideoPlayer::PLAYING || pending_seek_ >= 0 || menu_open_;
	if (!keep) overlay_timer_ -= dt;
	const float target = (keep || overlay_timer_ > 0) ? 1.0f : 0.0f;
	overlay_ += (target - overlay_) * std::min(1.0f, (float)dt * 8.0f);
	menu_ += ((menu_open_ ? 1.0f : 0.0f) - menu_) * std::min(1.0f, (float)dt * 10.0f);
	for (int i = 0; i < 2; i++) {
		const float top = i == 0 ? SUB_LIST_TOP : LIST_TOP;
		columns_[i].scroll = ui::follow(columns_[i].scroll, columns_[i].selected * (ROW_H + ROW_GAP), ROW_H, LIST_BOTTOM - top, dt);
	}
	toast_.update(dt);
}

void PlayerScreen::draw_subtitles(Renderer &r, float vx, float vy, float vw, float vh) {
	VideoPlayer::Subtitle sub;
	std::string text;
	if (sub_kind_ == SubtitleOption::EMBEDDED && player_->current_subtitle(sub)) {
		if (sub.image) {
			r.image_cover(*sub.image, vx + sub.x * vw, vy + sub.y * vh, sub.w * vw, sub.h * vh);
			return;
		}
		text = sub.text;
	} else if (sub_kind_ == SubtitleOption::ADDON && addon_cues_for_ == sub_id_) {
		if (const SubtitleCue *cue = cue_at(addon_cues_, player_->position())) text = cue->text;
	}
	if (text.empty()) return;
	Font &f = fonts_.subtitle;
	const std::vector<std::string> lines = f.wrap(text, std::min(vw, W) * 0.8f);
	const float line_h = theme::SIZE_SUBTITLE * 1.3f;
	// Above the controls while they show.
	float bottom = std::min(vy + vh, H) - S(56);
	if (overlay_ > 0.5f && !menu_open_) bottom = std::min(bottom, H - S(250));
	float baseline = bottom - (lines.size() - 1) * line_h;
	for (const std::string &line : lines) {
		const float w = f.measure(line);
		const float x = (W - w) / 2;
		r.rounded_rect(x - S(14), baseline - theme::SIZE_SUBTITLE * 0.95f, w + S(28), line_h, S(8), rgb(0x000000, 0.6f));
		r.text(f, line, x, baseline, rgb(0xFFFFFF));
		baseline += line_h;
	}
}

void PlayerScreen::draw_menu(Renderer &r) {
	r.push(W / 2, H / 2, 1.0f, menu_); // fades in and out as a whole
	r.rect(PANEL_X, 0, PANEL_W, H, theme::BASE_100.with_alpha(0.94f));
	r.text(fonts_.page_header, "Audio & subtitles", COL_SUB_X, S(96), theme::BASE_CONTENT);

	auto column = [&](int index, const char *title, float x, float w, float top,
						  const std::vector<std::pair<std::string, std::string>> &rows, int active) {
		const bool focused = menu_column_ == index;
		r.text(fonts_.section, title, x, LIST_TOP - S(24), focused ? theme::BASE_CONTENT : theme::BASE_CONTENT_MUTED);
		const Column &col = columns_[index];
		const float R = theme::RADIUS_FIELD;
		for (int i = 0; i < (int)rows.size(); i++) {
			const float y = top + i * (ROW_H + ROW_GAP) - col.scroll;
			if (y + ROW_H < top - S(4) || y > LIST_BOTTOM) continue;
			const float sel = (focused && i == col.selected) ? 1.0f : 0.0f;
			ui::begin_focus(r, x, y, w, ROW_H, R, sel, 1.0f);
			r.rounded_rect(x, y, w, ROW_H, R, mix(theme::BASE_200, theme::PRIMARY, 0.3f * sel));
			if (i == active) ui::status(r, x + S(26), y + ROW_H / 2); // the track in use
			const float tx = x + S(46), tw = w - (tx - x) - S(16);
			const bool two_lines = !rows[i].second.empty();
			const float label_y = two_lines ? y + S(31) : y + ROW_H / 2 + S(9);
			r.text(fonts_.card_title, fonts_.card_title.ellipsize(rows[i].first, tw), tx, label_y, theme::BASE_CONTENT);
			if (two_lines) {
				r.text(fonts_.small, fonts_.small.ellipsize(rows[i].second, tw), tx, y + S(56), theme::BASE_CONTENT_MUTED);
			}
			r.pop();
		}
		if (rows.empty()) r.text(fonts_.body, "None in this video", x, top + S(30), theme::BASE_CONTENT_MUTED);
	};

	std::vector<std::pair<std::string, std::string>> subs, audio;
	int active_sub = -1, active_audio = -1; // -1: the one in use is in another tab
	for (int i = 0; i < (int)sub_rows_.size(); i++) {
		const SubtitleOption &o = sub_options_[sub_rows_[i]];
		subs.push_back({ o.label, o.detail });
		if (o.kind == sub_kind_ && (o.kind == SubtitleOption::OFF || o.id == sub_id_)) active_sub = i;
	}
	for (int i = 0; i < (int)audio_tracks_.size(); i++) {
		// "Spanish · Doblaje · 5.1 AC3": codec and channels on the second line.
		const std::string &label = audio_tracks_[i].label;
		const size_t dot = label.rfind(" \xC2\xB7 ");
		if (dot == std::string::npos) {
			audio.push_back({ label, "" });
		} else {
			audio.push_back({ label.substr(0, dot), label.substr(dot + 4) });
		}
		if (audio_tracks_[i].id == player_->audio_track()) active_audio = i;
	}
	column(0, "Subtitles", COL_SUB_X, COL_SUB_W, SUB_LIST_TOP, subs, active_sub);
	column(1, "Audio", COL_AUDIO_X, COL_AUDIO_W, LIST_TOP, audio, active_audio);

	// Language tabs in their box over the subtitles column: the ones that fit,
	// widened to fill it, with "<" / ">" on the side of the others.
	int last = sub_tabs_first_;
	while (last + 1 < (int)sub_tabs_.size() &&
			sub_tabs_[last + 1].x + sub_tabs_[last + 1].w - sub_tabs_[sub_tabs_first_].x <= LANG_TABS_W) {
		last++;
	}
	const bool more_left = sub_tabs_first_ > 0, more_right = last + 1 < (int)sub_tabs_.size();
	if (!sub_tabs_.empty()) {
		r.rounded_rect(COL_SUB_X, LIST_TOP, COL_SUB_W, LANG_BOX_H, theme::RADIUS_SELECTOR, theme::BASE_200);
		const int shown = last - sub_tabs_first_ + 1;
		const float used = sub_tabs_[last].x + sub_tabs_[last].w - sub_tabs_[sub_tabs_first_].x;
		const float extra = std::max(0.0f, LANG_TABS_W - used) / shown;
		float x = COL_SUB_X + LANG_BOX_PAD;
		for (int i = sub_tabs_first_; i <= last; i++) {
			const LanguageTab &t = sub_tabs_[i];
			ui::chip(r, fonts_.small, t.label, x, LIST_TOP + LANG_BOX_PAD, t.w + extra, LANG_TAB_H, i == sub_tab_, 0.0f, theme::BASE_200);
			x += t.w + extra + LANG_TAB_GAP;
		}
	}
	const float arrow_y = LIST_TOP + LANG_BOX_H / 2 + fonts_.body.size() * 0.36f;
	if (more_left) r.text(fonts_.body, "<", COL_SUB_X - S(10) - fonts_.body.measure("<"), arrow_y, theme::BASE_CONTENT_MUTED);
	if (more_right) r.text(fonts_.body, ">", COL_SUB_X + COL_SUB_W + S(10), arrow_y, theme::BASE_CONTENT_MUTED);

	std::vector<ui::Hint> hints = { { "\xC3\x97", "Select" }, { "\xE2\x97\x80 \xE2\x96\xB6", "Column" } };
	if (sub_tabs_.size() > 1) hints.push_back({ "L1 R1", "Language" });
	hints.push_back({ "\xE2\x97\x8B", "Close" });
	ui::hints(r, fonts_.small, hints, COL_SUB_X, H - S(52));
	r.pop();
}

void PlayerScreen::draw(Renderer &r) {
	r.rect(0, 0, W, H, rgb(0x000000));

	// Video, letterboxed to keep its aspect ratio.
	const Texture &frame = player_->frame();
	float vx = 0, vy = 0, vw = W, vh = H;
	if (frame.valid()) {
		const float aspect = (float)frame.picture_width() / frame.height;
		vw = W;
		vh = W / aspect;
		if (vh > H) {
			vh = H;
			vw = H * aspect;
		}
		vx = (W - vw) / 2;
		vy = (H - vh) / 2;
		r.image_cover(frame, vx, vy, vw, vh);
	}

	// Status in the middle.
	const VideoPlayer::State state = player_->state();
	std::string status;
	bool error = false;
	switch (state) {
		case VideoPlayer::OPENING: status = "Loading\xE2\x80\xA6"; break;
		case VideoPlayer::BUFFERING: status = "Buffering\xE2\x80\xA6"; break;
		case VideoPlayer::PAUSED: status = "Paused"; break;
		case VideoPlayer::FAILED:
			status = player_->error() + "  \xC2\xB7  Press \xE2\x97\x8B to choose another stream";
			error = true;
			break;
		default: break;
	}

	if (frame.valid()) draw_subtitles(r, vx, vy, vw, vh);

	if (!status.empty() && menu_ < 0.5f) {
		Font &f = fonts_.card_title;
		const std::string shown = f.ellipsize(status, W - 240);
		r.text(f, shown, (W - f.measure(shown)) / 2, H / 2, error ? theme::ERROR : theme::BASE_CONTENT);
		// Torrents: peers and speed under "Loading…" / "Buffering…".
		const std::string detail = state == VideoPlayer::OPENING || state == VideoPlayer::BUFFERING ? player_->source_status() : "";
		if (!detail.empty()) {
			Font &b = fonts_.body;
			const std::string line = b.ellipsize(detail, W - 240);
			r.text(b, line, (W - b.measure(line)) / 2, H / 2 + S(48), theme::BASE_CONTENT_MUTED);
		}
	}

	// Bottom overlay: title, progress, times, hints.
	if (overlay_ > 0.01f && state != VideoPlayer::FAILED && menu_ < 0.99f) {
		r.push(W / 2, H / 2, 1.0f, overlay_ * (1.0f - menu_));
		r.vertical_fade(0, H - S(330), W, S(330), rgb(0x000000), 0.0f, 0.85f);
		const float x = 120, right = W - 120;
		const float y = H - S(150);
		r.text(fonts_.page_header, fonts_.page_header.ellipsize(args_.title, right - x), x, y, theme::BASE_CONTENT);
		if (!args_.subtitle.empty()) r.text(fonts_.body, args_.subtitle, x, y + S(36), theme::BASE_CONTENT_MUTED);
		const double duration = player_->duration();
		const double position = pending_seek_ >= 0 ? pending_seek_ : player_->position();
		const float bar_y = H - S(72);
		ui::progress(r, x, bar_y, right - x, S(8), duration > 0 ? (float)(position / duration) : 0.0f);
		const float ty = bar_y + S(42);
		r.text(fonts_.body, format_time(position), x, ty, theme::BASE_CONTENT);
		const std::string total = format_time(duration);
		r.text(fonts_.body, total, right - fonts_.body.measure(total), ty, theme::BASE_CONTENT);
		const std::vector<ui::Hint> hints = { { "\xC3\x97", "Play / Pause" }, { "\xE2\x97\x80 \xE2\x96\xB6", "Seek" },
			{ "\xE2\x96\xB2", "Audio & subtitles" }, { "\xE2\x97\x8B", "Back" } };
		ui::hints(r, fonts_.small, hints, (W - ui::hints_width(fonts_.small, hints)) / 2, ty);
		r.pop();
	}

	if (menu_ > 0.01f) draw_menu(r);

	toast_.draw(r, fonts_.body, W / 2, S(40));
}
