#include "details_screen.h"

#include "../core/stremio.h"
#include "../net/images.h"
#include "../platform/platform.h"
#include "player_screen.h"
#include "tab_bar.h"
#include "theme.h"

#include <algorithm>
#include <cctype>

namespace {

using theme::S;

const float W = 1920, H = 1080;
const float MARGIN = 120;
const theme::ScaledFloat ROW_H{ 84 }, ROW_GAP{ 10 };
const theme::ScaledFloat LISTS_TOP{ 346 };               // "Episodes" / "Streams" titles
const theme::ScaledFloat LIST_Y{ 366 };
float list_h() { return H - S(90) - LIST_Y; }
const tabs::DynamicMetric LIST_H{ list_h };
// Season & Add-on selector bars: smaller versions of the home tab bar.
const theme::ScaledFloat CHIP_H{ 46 }, BOX_PAD{ 6 };
const theme::ScaledFloat BOX_H{ 58 };
const theme::ScaledFloat FILTER_SPACE{ 68 }; // taken from the top of the list when a bar is shown
const theme::ScaledFloat EP_PREVIEW_H{ 168 };
const theme::ScaledFloat EP_PREVIEW_SPACE{ 180 }; // EP_PREVIEW_H + 12 gap
const theme::ScaledFloat EP_COL_W{ 680 };
const theme::ScaledFloat CAPS_W{ 110 };      // the L1 / R1 marks after the box

// Add-ons decorate stream names with emoji; Inter (and the PS4, which has no
// font fallback) can't draw them, so drop those code points. Flags (pairs of
// regional indicators, e.g. audio languages) become their letters ("GB").
// Lines are joined with separators, and lines left with nothing readable
// (e.g. only the slashes between flags) are dropped.
std::string clean(const std::string &text) {
	std::string out, line;
	auto flush = [&]() {
		const bool readable = std::any_of(line.begin(), line.end(), [](char ch) { return isalnum((unsigned char)ch); });
		const size_t a = line.find_first_not_of(' '), b = line.find_last_not_of(' ');
		if (readable) out += (out.empty() ? "" : "  \xC2\xB7  ") + line.substr(a, b - a + 1);
		line.clear();
	};
	size_t i = 0;
	while (i < text.size()) {
		const size_t start = i;
		const uint32_t c = next_codepoint(text, i);
		if (c >= 0x1F1E6 && c <= 0x1F1FF) {
			line += (char)('A' + (c - 0x1F1E6));
			continue;
		}
		if ((c >= 0x2600 && c <= 0x27BF) || c >= 0x1F000 || c == 0xFE0F || c == 0x200D) continue;
		if (c == '\n' || c == '\r') {
			flush();
			continue;
		}
		if (c == '\t') {
			line += ' ';
			continue;
		}
		line.append(text, start, i - start);
	}
	flush();
	return out;
}

std::string join_strings(const std::vector<std::string> &v, size_t max_items = 99) {
	std::string out;
	for (size_t i = 0; i < v.size() && i < max_items; i++) {
		if (i) out += ", ";
		out += v[i];
	}
	return out;
}

// "56 seeders", coloured by how quickly the torrent is likely to start.
void seeders_badge(int seeders, std::string &text, Color &bg) {
	if (seeders < 0) return;
	text = std::to_string(seeders) + (seeders == 1 ? " seeder" : " seeders");
	bg = seeders >= 15 ? theme::ACCENT : seeders >= 5 ? theme::WARNING : theme::ERROR;
}

} // namespace

DetailsScreen::DetailsScreen(Fonts &fonts, Navigator &nav, const Target &target)
	: fonts_(fonts), nav_(nav), target_(target) {
	Stremio &s = stremio();
	if (const LibraryItem *item = s.library_item(target_.meta_id)) {
		video_id_ = item->resume_video_id();
	} else if (target_.type != "series") {
		video_id_ = target_.meta_id; // movies: the meta id is the video id
	}
	std::weak_ptr<int> alive = alive_;
	s.request_meta(target_.type, target_.meta_id, [this, alive](bool ok, const MetaItem &meta, const std::string &error) {
		if (alive.expired()) return;
		if (ok) on_meta(meta);
		else meta_error_ = error;
	});
	s.request_ratings(target_.meta_id, [this, alive](bool ok, const RatingsInfo &info) {
		if (alive.expired() || !ok) return;
		on_ratings(info);
	});
	if (!video_id_.empty()) {
		load_streams(video_id_);
	} else {
		streams_status_ = "Loading episodes\xE2\x80\xA6";
	}
}

void DetailsScreen::on_meta(const MetaItem &meta) {
	has_meta_ = true;
	meta_ = meta;
	if (has_ratings_) meta_.apply_ratings(ratings_);
	description_ = clean(meta_.description);
	if (!meta_.name.empty()) target_.name = meta_.name;
	if (!meta_.release_info.empty() && target_.release_info.empty()) target_.release_info = meta_.release_info;
	if (!meta_.landscape_images().empty()) target_.images = meta_.landscape_images();
	rebuild_people();

	// If the title had a non-tt ID that resolved to a tt imdb_id, fetch ratings for it
	if (!has_ratings_ && !meta_.imdb_id.empty() && meta_.imdb_id != target_.meta_id) {
		std::weak_ptr<int> alive = alive_;
		stremio().request_ratings(meta_.imdb_id, [this, alive](bool ok, const RatingsInfo &info) {
			if (alive.expired() || !ok) return;
			on_ratings(info);
		});
	}

	if (video_id_.empty()) {
		video_id_ = meta_.direct_video_id();
		if (video_id_.empty()) {
			for (const Video &v : meta_.videos) { // first regular-season episode
				if (v.season > 0) {
					video_id_ = v.id;
					break;
				}
			}
			if (video_id_.empty() && !meta_.videos.empty()) video_id_ = meta_.videos[0].id;
		}
		if (!video_id_.empty()) load_streams(video_id_);
	}

	rebuild_seasons();
	if (meta_.videos.size() > 1 && streams_.empty()) column_ = 0;
}

void DetailsScreen::rebuild_seasons() {
	seasons_.clear();
	bool has_specials = false;
	for (const Video &v : meta_.videos) {
		if (v.season <= 0) {
			has_specials = true;
		} else if (std::find(seasons_.begin(), seasons_.end(), v.season) == seasons_.end()) {
			seasons_.push_back(v.season);
		}
	}
	std::sort(seasons_.begin(), seasons_.end());
	if (has_specials) seasons_.push_back(0);
	if (seasons_.empty() && !meta_.videos.empty()) seasons_.push_back(0);

	season_idx_ = 0;
	if (const Video *cur = meta_.find_video(video_id_)) {
		const int s_num = cur->season <= 0 ? 0 : cur->season;
		for (int i = 0; i < (int)seasons_.size(); i++) {
			if (seasons_[i] == s_num) {
				season_idx_ = i;
				break;
			}
		}
	}
	rebuild_season_videos();
}

void DetailsScreen::set_season(int idx) {
	if (seasons_.empty()) return;
	const int n = (int)seasons_.size();
	idx = ((idx % n) + n) % n;
	if (idx == season_idx_) return;
	season_idx_ = idx;
	episodes_list_ = ListState();
	rebuild_season_videos();
}

void DetailsScreen::rebuild_season_videos() {
	season_videos_.clear();
	if (seasons_.empty()) return;
	const int target_season = seasons_[season_idx_];
	for (int i = 0; i < (int)meta_.videos.size(); i++) {
		const int s_num = meta_.videos[i].season <= 0 ? 0 : meta_.videos[i].season;
		if (s_num == target_season) {
			season_videos_.push_back(i);
		}
	}
	episodes_list_.focus.assign(season_videos_.size(), 0.0f);
	episodes_list_.selected = 0;
	for (int i = 0; i < (int)season_videos_.size(); i++) {
		if (meta_.videos[season_videos_[i]].id == video_id_) {
			episodes_list_.selected = i;
			break;
		}
	}
}

std::string DetailsScreen::season_label(int season_num) const {
	if (season_num <= 0) return "Specials";
	return "Season " + std::to_string(season_num);
}

std::vector<float> DetailsScreen::season_widths() {
	std::vector<float> w;
	for (int s : seasons_) {
		w.push_back(std::max(S(116), ui::chip_width(fonts_.body, season_label(s))));
	}
	return w;
}

void DetailsScreen::on_ratings(const RatingsInfo &info) {
	has_ratings_ = true;
	ratings_ = info;
	meta_.apply_ratings(info);
	if (!meta_.description.empty()) description_ = clean(meta_.description);
	rebuild_people();
}

void DetailsScreen::rebuild_people() {
	people_.clear();
	auto add_person = [&](const std::string &name, const std::string &role) {
		const std::string cl = clean(name);
		if (cl.empty()) return;
		for (PersonChip &p : people_) {
			if (p.name == cl) {
				if (p.role.find(role) == std::string::npos) p.role += " / " + role;
				return;
			}
		}
		people_.push_back({ cl, role, 0.0f });
	};
	for (const std::string &d : meta_.directors) add_person(d, "Director");
	for (const std::string &c : meta_.cast) add_person(c, "Cast");
	for (const std::string &w : meta_.writers) add_person(w, "Writer");
	if (person_selected_ >= (int)people_.size()) {
		person_selected_ = std::max(0, (int)people_.size() - 1);
	}
}

void DetailsScreen::load_streams(const std::string &video_id) {
	video_id_ = video_id;
	streams_.clear();
	addons_.clear();
	visible_.clear();
	filter_ = 0;
	filter_focused_ = false;
	filter_scroll_ = 0;
	streams_list_ = ListState();
	streams_loading_ = true;
	streams_status_ = "Finding streams\xE2\x80\xA6";
	std::weak_ptr<int> alive = alive_;
	const std::string requested = video_id;
	stremio().request_streams(target_.type, video_id,
			[this, alive, requested](const std::string &, std::vector<StreamItem> &streams, const std::string &) {
				if (alive.expired() || requested != video_id_) return;
				if (!streams.empty() && std::find(addons_.begin(), addons_.end(), streams[0].addon_name) == addons_.end()) {
					addons_.push_back(streams[0].addon_name);
				}
				// Playable streams first, keeping add-on order within each group.
				std::stable_partition(streams.begin(), streams.end(), [](const StreamItem &s) { return !s.playable_url().empty(); });
				const size_t playable_before = std::count_if(streams_.begin(), streams_.end(), [](const StreamItem &s) { return !s.playable_url().empty(); });
				size_t insert_at = playable_before;
				for (StreamItem &st : streams) {
					if (!st.playable_url().empty()) streams_.insert(streams_.begin() + insert_at++, st);
					else streams_.push_back(st);
				}
				streams_list_.focus.clear();
				refilter();
				if (!streams_.empty()) streams_status_.clear();
			},
			[this, alive, requested]() {
				if (alive.expired() || requested != video_id_) return;
				streams_loading_ = false;
				if (streams_.empty()) streams_status_ = "No streams found. Install a streaming add-on in Addons to watch this.";
			});
}

void DetailsScreen::set_filter(int filter) {
	if (filter == filter_) return;
	filter_ = filter;
	streams_list_ = ListState();
	refilter();
}

void DetailsScreen::refilter() {
	visible_.clear();
	const std::string *addon = filter_ > 0 && filter_ <= (int)addons_.size() ? &addons_[filter_ - 1] : nullptr;
	for (size_t i = 0; i < streams_.size(); i++) {
		if (!addon || streams_[i].addon_name == *addon) visible_.push_back((int)i);
	}
	streams_list_.focus.resize(visible_.size(), 0.0f);
	streams_list_.selected = std::max(0, std::min((int)visible_.size() - 1, streams_list_.selected));
}

std::vector<float> DetailsScreen::filter_widths() {
	std::vector<float> w;
	w.push_back(std::max(S(110), ui::chip_width(fonts_.body, "All")));
	for (const std::string &a : addons_) w.push_back(std::max(S(110), ui::chip_width(fonts_.body, clean(a))));
	return w;
}

void DetailsScreen::play(const StreamItem &stream) {
	if (stream.playable_url().empty()) {
		toast_.show(stream.unsupported_reason(), true);
		return;
	}
	PlayerScreen::Args args;
	args.stream = stream;
	args.meta_id = target_.meta_id;
	args.type = target_.type;
	args.video_id = video_id_;
	args.title = target_.name;
	if (has_meta_) {
		args.has_meta = true;
		args.meta = meta_;
		if (const Video *v = meta_.find_video(video_id_)) {
			if (target_.type == "series") args.subtitle = MetaItem::video_label(*v);
		}
	}
	nav_.push(std::unique_ptr<Screen>(new PlayerScreen(fonts_, nav_, args)));
}

void DetailsScreen::toggle_library() {
	Stremio &s = stremio();
	if (s.in_library(target_.meta_id)) {
		s.remove_from_library(target_.meta_id);
		toast_.show("Removed from your library");
		return;
	}
	MetaItem meta = meta_;
	if (!has_meta_) { // not loaded (yet): what the card showed
		meta.id = target_.meta_id;
		meta.type = target_.type;
		meta.name = target_.name;
		meta.poster = target_.poster;
	}
	s.add_to_library(meta);
	toast_.show("Added to your library");
}

void DetailsScreen::update(double dt, uint32_t pressed) {
	if (more_open_) {
		if (pressed & (platform::BUTTON_BACK | platform::BUTTON_SQUARE)) {
			more_open_ = false;
			return;
		}
		if (!people_.empty()) {
			if (pressed & (platform::BUTTON_LEFT | platform::BUTTON_UP)) {
				person_selected_ = std::max(0, person_selected_ - 1);
			}
			if (pressed & (platform::BUTTON_RIGHT | platform::BUTTON_DOWN)) {
				person_selected_ = std::min((int)people_.size() - 1, person_selected_ + 1);
			}
			if (pressed & platform::BUTTON_ACCEPT) {
				const std::string person = people_[person_selected_].name;
				more_open_ = false;
				stremio().search_person(person);
				tabs::open(tabs::SEARCH, fonts_, nav_);
				return;
			}
		}
		for (int i = 0; i < (int)people_.size(); i++) {
			people_[i].focus = ui::approach(people_[i].focus, i == person_selected_ ? 1.0f : 0.0f, dt);
		}
		toast_.update(dt);
		return;
	}

	if (pressed & platform::BUTTON_BACK) {
		nav_.pop();
		return;
	}
	if (pressed & platform::BUTTON_TRIANGLE) toggle_library();
	if (pressed & platform::BUTTON_SQUARE) {
		more_open_ = true;
		return;
	}

	const bool has_episodes = has_meta_ && meta_.videos.size() > 1;
	if (!has_episodes) {
		column_ = 1;
		season_focused_ = false;
	}
	if (!has_seasons()) season_focused_ = false;
	const int filters = (int)addons_.size() + 1;
	if (!has_filter()) filter_focused_ = false;

	// L1 / R1 switch seasons when focused on the left column (or if there is no stream filter),
	// and switch the stream add-on filter when focused on the right column.
	if (pressed & (platform::BUTTON_L1 | platform::BUTTON_R1)) {
		const int step = (pressed & platform::BUTTON_R1) ? 1 : -1;
		if ((column_ == 0 || season_focused_ || !has_filter()) && has_seasons()) {
			set_season(season_idx_ + step);
		} else if (has_filter()) {
			set_filter((filter_ + step + filters) % filters);
		}
	}

	if (more_focused_) {
		if (pressed & platform::BUTTON_ACCEPT) {
			more_open_ = true;
		} else if (pressed & platform::BUTTON_DOWN) {
			more_focused_ = false;
			if (column_ == 0 && has_seasons()) season_focused_ = true;
			else if (column_ == 1 && has_filter()) filter_focused_ = true;
		}
	} else if (season_focused_) {
		if (pressed & platform::BUTTON_UP) {
			season_focused_ = false;
			more_focused_ = true;
		} else if (pressed & platform::BUTTON_LEFT) {
			set_season(season_idx_ - 1);
		} else if (pressed & platform::BUTTON_RIGHT) {
			if (season_idx_ + 1 < (int)seasons_.size()) {
				set_season(season_idx_ + 1);
			} else if (!streams_.empty()) {
				season_focused_ = false;
				column_ = 1;
				if (has_filter()) filter_focused_ = true;
			} else {
				set_season(0);
			}
		} else if (pressed & platform::BUTTON_ACCEPT) {
			set_season(season_idx_ + 1);
		} else if (pressed & platform::BUTTON_DOWN) {
			season_focused_ = false;
		}
	} else if (filter_focused_) {
		if (pressed & platform::BUTTON_UP) {
			filter_focused_ = false;
			more_focused_ = true;
		} else if (pressed & platform::BUTTON_LEFT) {
			if (filter_ > 0) set_filter(filter_ - 1);
			else if (has_episodes) {
				column_ = 0;
				filter_focused_ = false;
				season_focused_ = has_seasons();
			}
		}
		if ((pressed & platform::BUTTON_RIGHT) && filter_ < filters - 1) set_filter(filter_ + 1);
		if ((pressed & (platform::BUTTON_DOWN | platform::BUTTON_ACCEPT)) && !visible_.empty()) filter_focused_ = false;
	} else {
		ListState &list = column_ == 0 ? episodes_list_ : streams_list_;
		const int count = column_ == 0 ? (int)season_videos_.size() : (int)visible_.size();
		if (pressed & platform::BUTTON_UP) {
			if (list.selected == 0) {
				if (column_ == 0 && has_seasons()) season_focused_ = true;
				else if (column_ == 1 && has_filter()) filter_focused_ = true;
				else more_focused_ = true;
			} else {
				list.selected = std::max(0, list.selected - 1);
			}
		}
		if (pressed & platform::BUTTON_DOWN) list.selected = std::max(0, std::min(count - 1, list.selected + 1));
		if ((pressed & platform::BUTTON_LEFT) && has_episodes) column_ = 0;
		if (pressed & platform::BUTTON_RIGHT) {
			if (column_ == 0 && count > 0) {
				const std::string id = meta_.videos[season_videos_[episodes_list_.selected]].id;
				if (id != video_id_) load_streams(id);
				column_ = 1;
			} else if (!streams_.empty()) {
				column_ = 1;
			}
		}
		if (pressed & platform::BUTTON_ACCEPT) {
			if (column_ == 0 && count > 0) {
				const std::string id = meta_.videos[season_videos_[episodes_list_.selected]].id;
				if (id != video_id_) load_streams(id);
				column_ = 1;
			} else if (column_ == 1 && count > 0) {
				play(streams_[visible_[streams_list_.selected]]);
			}
		}
	}
	// Animations.
	more_btn_focus_ = ui::approach(more_btn_focus_, more_focused_ ? 1.0f : 0.0f, dt);
	for (int c = 0; c < 2; c++) {
		ListState &l = c == 0 ? episodes_list_ : streams_list_;
		for (size_t i = 0; i < l.focus.size(); i++) {
			const bool focused = !more_focused_ && c == column_ && (int)i == l.selected &&
					!(c == 0 && season_focused_) && !(c == 1 && filter_focused_);
			l.focus[i] = ui::approach(l.focus[i], focused ? 1.0f : 0.0f, dt);
		}
		float view = LIST_H;
		if (c == 0 && has_episodes) {
			if (has_seasons()) view -= FILTER_SPACE;
			view -= EP_PREVIEW_SPACE;
		} else if (c == 1 && has_filter()) {
			view -= FILTER_SPACE;
		}
		l.scroll = ui::follow(l.scroll, l.selected * (ROW_H + ROW_GAP), ROW_H, view, dt);
	}

	const int num_seasons = (int)seasons_.size();
	season_focus_.resize(num_seasons, 0.0f);
	for (int i = 0; i < num_seasons; i++) {
		season_focus_[i] = ui::approach(season_focus_[i], (!more_focused_ && season_focused_ && i == season_idx_) ? 1.0f : 0.0f, dt);
	}
	if (has_seasons()) {
		const std::vector<float> w = season_widths();
		float start = 0;
		for (int i = 0; i < season_idx_ && i < (int)w.size(); i++) start += w[i];
		const float view = EP_COL_W - CAPS_W - 2 * BOX_PAD;
		if (season_idx_ < (int)w.size()) {
			season_scroll_ = ui::follow(season_scroll_, start, w[season_idx_], view, dt);
		}
	}

	filter_focus_.resize(filters, 0.0f);
	for (int i = 0; i < filters; i++) {
		filter_focus_[i] = ui::approach(filter_focus_[i], (!more_focused_ && filter_focused_ && i == filter_) ? 1.0f : 0.0f, dt);
	}
	if (has_filter()) { // keep the current chip inside the box when they overflow it
		const std::vector<float> w = filter_widths();
		float start = 0;
		for (int i = 0; i < filter_; i++) start += w[i];
		const float sx = has_episodes ? MARGIN + EP_COL_W + S(44) : MARGIN;
		const float view = W - MARGIN - sx - CAPS_W - 2 * BOX_PAD;
		filter_scroll_ = ui::follow(filter_scroll_, start, w[filter_], view, dt);
	}
	toast_.update(dt);
}

void DetailsScreen::draw_list(Renderer &r, ListState &list, const std::vector<ListRow> &rows, int active, float x,
		float y, float w, float h) {
	const float R = theme::RADIUS_FIELD, pad = S(22);
	for (size_t i = 0; i < rows.size(); i++) {
		const float ry = y + i * (ROW_H + ROW_GAP) - list.scroll;
		if (ry < y - S(6) || ry + ROW_H > y + h + S(6)) continue; // only whole rows (the hint line is below)
		const ListRow &row = rows[i];
		const float f = i < list.focus.size() ? list.focus[i] : 0.0f;
		ui::begin_focus(r, x, ry, w, ROW_H, R, f, 1.03f);
		r.rounded_rect(x, ry, w, ROW_H, R, mix(theme::BASE_200, theme::PRIMARY, 0.35f * f));
		float tx = x + pad;
		if ((int)i == active) { // e.g. the episode whose streams are listed
			ui::status(r, tx + S(6), ry + ROW_H / 2);
			tx += S(26);
		}
		float tw = w - (tx - x) - pad;
		const Color title = row.muted ? theme::BASE_CONTENT_MUTED : theme::BASE_CONTENT;
		if (!row.badge.empty()) { // right end of the first line
			const float bw = ui::badge_width(fonts_.small, row.badge), bh = ui::badge_height(fonts_.small);
			const float by = (row.line2.empty() ? ry + ROW_H / 2 : ry + S(36) - fonts_.card_title.size() * 0.36f) - bh / 2;
			ui::badge(r, fonts_.small, row.badge, x + w - pad - bw, by, row.badge_bg, theme::BASE_100);
			tw -= bw + S(16);
		}
		if (!row.note.empty()) {
			const float nw = fonts_.body.measure(row.note);
			r.text(fonts_.body, row.note, tx + tw - nw, row.line2.empty() ? ry + ROW_H / 2 + S(8) : ry + S(36),
					theme::BASE_CONTENT_MUTED);
			tw -= nw + S(20);
		}
		if (!row.line2.empty()) {
			r.text(fonts_.card_title, fonts_.card_title.ellipsize(row.line1, tw), tx, ry + S(36), title);
			r.text(fonts_.body, fonts_.body.ellipsize(row.line2, w - (tx - x) - pad), tx, ry + S(66), theme::BASE_CONTENT_MUTED);
		} else {
			r.text(fonts_.card_title, fonts_.card_title.ellipsize(row.line1, tw), tx, ry + ROW_H / 2 + S(9), title);
		}
		ui::end_focus(r, x, ry, w, ROW_H, R, f);
	}
}

void DetailsScreen::draw_seasons(Renderer &r, float x, float y, float w) {
	const std::vector<float> widths = season_widths();
	float total = 0;
	for (float cw : widths) total += cw;
	const float box_w = std::min(total + 2 * BOX_PAD, w - CAPS_W);
	const float scroll = total + 2 * BOX_PAD > box_w ? season_scroll_ : 0;
	r.rounded_rect(x, y, box_w, BOX_H, theme::RADIUS_SELECTOR, theme::BASE_200);
	for (int pass = 0; pass < 2; pass++) {
		float cx = x + BOX_PAD - scroll;
		for (size_t i = 0; i < widths.size(); i++) {
			const float f = i < season_focus_.size() ? season_focus_[i] : 0.0f;
			const bool inside = cx >= x + BOX_PAD - 1 && cx + widths[i] <= x + box_w - BOX_PAD + 1;
			if (inside && (pass == 0) != (f > 0.5f)) {
				ui::chip(r, fonts_.body, season_label(seasons_[i]), cx, y + BOX_PAD, widths[i], CHIP_H,
						(int)i == season_idx_, f, theme::BASE_200);
			}
			cx += widths[i];
		}
	}
	const float hy = y + BOX_H / 2 + fonts_.small.size() * 0.36f;
	const float kx = x + box_w + S(16);
	ui::kbd(r, fonts_.small, "R1", kx + ui::kbd_width(fonts_.small, "L1") + S(8), hy);
	ui::kbd(r, fonts_.small, "L1", kx, hy);
}

void DetailsScreen::draw_episode_preview(Renderer &r, const Video &ep, float x, float y, float w, float h) {
	r.rounded_rect(x, y, w, h, theme::RADIUS_BOX, theme::BASE_200);

	// 16:9 episode thumbnail on the left
	const float pad = S(14);
	const float img_x = x + pad, img_y = y + pad;
	const float img_h = h - 2 * pad;
	const float img_w = S(248);
	r.rounded_rect(img_x, img_y, img_w, img_h, theme::RADIUS_FIELD, theme::BASE_300);

	std::vector<std::string> ep_imgs;
	if (!ep.thumbnail.empty()) ep_imgs.push_back(ep.thumbnail);
	for (const std::string &u : target_.images) ep_imgs.push_back(u);
	const Texture &tex = images::first(ep_imgs);
	if (tex.valid()) {
		r.image_cover(tex, img_x, img_y, img_w, img_h);
		r.round_corners(img_x, img_y, img_w, img_h, theme::RADIUS_FIELD, theme::BASE_200);
	}

	const std::string ep_code = ep.season > 0
			? ("S" + std::to_string(ep.season) + " \xC2\xB7 E" + std::to_string(ep.episode))
			: ("Special \xC2\xB7 E" + std::to_string(ep.episode));
	const float bh = ui::badge_height(fonts_.small);
	ui::badge(r, fonts_.small, ep_code, img_x + S(10), img_y + img_h - bh - S(8), theme::PRIMARY, theme::PRIMARY_CONTENT);

	// Episode metadata & synopsis on the right
	const float tx = img_x + img_w + S(18);
	const float tw = x + w - pad - tx;
	const std::string ep_title = ep.title.empty() ? ("Episode " + std::to_string(ep.episode)) : clean(ep.title);
	r.text(fonts_.card_title, fonts_.card_title.ellipsize(ep_title, tw), tx, y + S(38), theme::BASE_CONTENT);

	std::string meta_sub = !ep.released_date.empty() ? ("Aired " + ep.released_date) : ep_code;
	if (ep.id == video_id_) meta_sub += "   \xC2\xB7   Active Stream Target";
	r.text(fonts_.small, fonts_.small.ellipsize(meta_sub, tw), tx, y + S(64),
			ep.id == video_id_ ? theme::ACCENT : theme::BASE_CONTENT_MUTED);

	const std::string synopsis = !ep.overview.empty()
			? clean(ep.overview)
			: "Select this episode with \xC3\x97 to browse available streams.";
	float sy = y + S(92);
	for (const std::string &line : fonts_.small.wrap(synopsis, tw, 3)) {
		r.text(fonts_.small, line, tx, sy, theme::BASE_CONTENT);
		sy += S(24);
	}
}

void DetailsScreen::draw_filter(Renderer &r, float x, float y, float w) {
	const std::vector<float> widths = filter_widths();
	float total = 0;
	for (float cw : widths) total += cw;
	const float box_w = std::min(total + 2 * BOX_PAD, w - CAPS_W);
	const float scroll = total + 2 * BOX_PAD > box_w ? filter_scroll_ : 0;
	r.rounded_rect(x, y, box_w, BOX_H, theme::RADIUS_SELECTOR, theme::BASE_200);
	// The focused chip last, so its ring isn't covered by its neighbours.
	for (int pass = 0; pass < 2; pass++) {
		float cx = x + BOX_PAD - scroll;
		for (size_t i = 0; i < widths.size(); i++) {
			const float f = i < filter_focus_.size() ? filter_focus_[i] : 0.0f;
			const bool inside = cx >= x + BOX_PAD - 1 && cx + widths[i] <= x + box_w - BOX_PAD + 1; // only whole chips
			if (inside && (pass == 0) != (f > 0.5f)) {
				const std::string label = i == 0 ? "All" : clean(addons_[i - 1]);
				ui::chip(r, fonts_.body, label, cx, y + BOX_PAD, widths[i], CHIP_H, (int)i == filter_, f, theme::BASE_200);
			}
			cx += widths[i];
		}
	}
	const float hy = y + BOX_H / 2 + fonts_.small.size() * 0.36f;
	const float kx = x + box_w + S(18);
	ui::kbd(r, fonts_.small, "R1", kx + ui::kbd_width(fonts_.small, "L1") + S(8), hy);
	ui::kbd(r, fonts_.small, "L1", kx, hy);
}

void DetailsScreen::draw_more_modal(Renderer &r) {
	r.rect(0, 0, W, H, theme::BASE_100.with_alpha(0.86f));

	const float mw = S(1560), mh = S(860);
	const float mx = (W - mw) / 2, my = (H - mh) / 2;
	r.rounded_rect(mx, my, mw, mh, theme::RADIUS_BOX, theme::BASE_200);

	const float pad = S(44);
	const float ix = mx + pad;
	const float iw = mw - 2 * pad;
	float y = my + pad + S(28);

	// Title
	r.text(fonts_.page_header, fonts_.page_header.ellipsize(target_.name, iw), ix, y, theme::BASE_CONTENT);
	y += S(40);

	// Subtitle metadata + Rating Badges
	std::string meta_line = target_.type.empty() ? "" : std::string(1, (char)toupper(target_.type[0])) + target_.type.substr(1);
	if (!target_.release_info.empty()) meta_line += "  \xC2\xB7  " + target_.release_info;
	if (!meta_.runtime.empty()) meta_line += "  \xC2\xB7  " + meta_.runtime;
	if (!meta_.genres.empty()) meta_line += "  \xC2\xB7  " + join_strings(meta_.genres, 4);
	float bx = ix + r.text(fonts_.body, meta_line, ix, y, theme::BASE_CONTENT_MUTED) + S(20);
	const float by = y - fonts_.body.size() * 0.36f - ui::badge_height(fonts_.small) / 2;

	if (!meta_.rated.empty()) {
		bx += ui::badge(r, fonts_.small, meta_.rated, bx, by, theme::BASE_300, theme::BASE_CONTENT) + S(10);
	}
	if (!meta_.imdb_rating.empty()) {
		bx += ui::badge(r, fonts_.small, "IMDb " + meta_.imdb_rating, bx, by, Color{ 245, 197, 24, 255 }, theme::BASE_100) + S(10);
	}
	if (!meta_.rotten_tomatoes.empty()) {
		bx += ui::badge(r, fonts_.small, "Rotten Tomatoes " + meta_.rotten_tomatoes, bx, by, Color{ 250, 50, 38, 255 }, theme::BASE_CONTENT) + S(10);
	}
	if (!meta_.metacritic.empty()) {
		bx += ui::badge(r, fonts_.small, "Metacritic " + meta_.metacritic, bx, by, theme::SECONDARY, theme::PRIMARY_CONTENT) + S(10);
	}
	y += S(42);

	// Full Plot
	const std::string plot = !description_.empty() ? description_ : "No synopsis available.";
	for (const std::string &line : fonts_.body.wrap(plot, iw, 5)) {
		r.text(fonts_.body, line, ix, y, theme::BASE_CONTENT);
		y += S(30);
	}
	y += S(10);

	// Awards & Box Office / Country
	std::string extra_meta;
	if (!meta_.awards.empty()) extra_meta += "Awards: " + clean(meta_.awards);
	if (!meta_.box_office.empty()) extra_meta += (extra_meta.empty() ? "" : "   \xC2\xB7   ") + std::string("Box Office: ") + meta_.box_office;
	if (!meta_.country.empty()) extra_meta += (extra_meta.empty() ? "" : "   \xC2\xB7   ") + std::string("Country: ") + clean(meta_.country);
	if (!extra_meta.empty()) {
		r.text(fonts_.small, fonts_.small.ellipsize(extra_meta, iw), ix, y, theme::WARNING);
		y += S(36);
	}

	// Interactive Cast & Director Section
	r.text(fonts_.section, "Cast & Crew", ix, y + S(8), theme::BASE_CONTENT);
	r.text(fonts_.small, "Select any actor, director, or writer with \xC3\x97 to view their movies & series",
			ix + S(210), y + S(6), theme::BASE_CONTENT_MUTED);
	y += S(32);

	if (people_.empty()) {
		r.text(fonts_.body, "Cast and director details are not listed for this title.", ix, y + S(24), theme::BASE_CONTENT_MUTED);
	} else {
		const float chip_h = S(48), gap_x = S(12), gap_y = S(12);
		float cx = ix, cy = y;
		for (int i = 0; i < (int)people_.size(); i++) {
			const PersonChip &p = people_[i];
			const std::string label = p.name + "  (" + p.role + ")";
			const float cw = ui::chip_width(fonts_.body, label);
			if (cx + cw > ix + iw && cx > ix) {
				cx = ix;
				cy += chip_h + gap_y;
			}
			if (cy + chip_h > my + mh - S(70)) break;
			ui::chip(r, fonts_.body, label, cx, cy, cw, chip_h, i == person_selected_, p.focus, theme::BASE_200);
			cx += cw + gap_x;
		}
	}

	ui::hints(r, fonts_.small,
			{ { "\xC3\x97", "Search Actor / Director Filmography" }, { "\xE2\x97\x8B", "Close" } },
			ix, my + mh - S(26));
}

void DetailsScreen::draw(Renderer &r) {
	// Backdrop, darkened (solid overlay: alpha on solids is proven on PS4).
	const Texture &art = images::first(target_.images);
	if (art.valid()) {
		r.image_cover(art, 0, 0, W, H);
		r.rect(0, 0, W, H, theme::BASE_100.with_alpha(0.78f));
	}

	float y = S(98);
	r.text(fonts_.page_header, fonts_.page_header.ellipsize(target_.name, W - 2 * MARGIN), MARGIN, y, theme::BASE_CONTENT);
	y += S(42);
	std::string info = target_.type.empty() ? "" : std::string(1, (char)toupper(target_.type[0])) + target_.type.substr(1);
	if (!target_.release_info.empty()) info += "  \xC2\xB7  " + target_.release_info;
	if (!meta_.runtime.empty()) info += "  \xC2\xB7  " + meta_.runtime;
	if (!meta_.genres.empty()) info += "  \xC2\xB7  " + join_strings(meta_.genres, 3);
	const float info_w = r.text(fonts_.body, info, MARGIN, y, theme::BASE_CONTENT_MUTED);

	// Rating Badges (MPAA Rated, IMDb, Rotten Tomatoes, Metacritic) + In Library badge
	float bx = MARGIN + info_w + (info.empty() ? 0 : S(18));
	const float by = y - fonts_.body.size() * 0.36f - ui::badge_height(fonts_.small) / 2;
	if (!meta_.rated.empty()) {
		bx += ui::badge(r, fonts_.small, meta_.rated, bx, by, theme::BASE_300, theme::BASE_CONTENT) + S(10);
	}
	if (!meta_.imdb_rating.empty()) {
		bx += ui::badge(r, fonts_.small, "IMDb " + meta_.imdb_rating, bx, by, Color{ 245, 197, 24, 255 }, theme::BASE_100) + S(10);
	}
	if (!meta_.rotten_tomatoes.empty()) {
		bx += ui::badge(r, fonts_.small, "RT " + meta_.rotten_tomatoes, bx, by, Color{ 250, 50, 38, 255 }, theme::BASE_CONTENT) + S(10);
	}
	if (!meta_.metacritic.empty()) {
		bx += ui::badge(r, fonts_.small, "MC " + meta_.metacritic, bx, by, theme::SECONDARY, theme::PRIMARY_CONTENT) + S(10);
	}
	if (stremio().in_library(target_.meta_id)) {
		ui::badge(r, fonts_.small, "In library", bx, by, theme::ACCENT, theme::ACCENT_CONTENT);
	}

	y += S(38);
	if (!meta_error_.empty() && !has_meta_) {
		r.text(fonts_.body, "Couldn't load details: " + meta_error_, MARGIN, y, theme::ERROR);
	}
	for (const std::string &line : fonts_.body.wrap(description_, S(1240), 2)) {
		r.text(fonts_.body, line, MARGIN, y, theme::BASE_CONTENT);
		y += S(28);
	}

	// Cast & Director summary line + "See More · Cast & Crew" button under the description
	const float btn_y = S(268);
	const std::string btn_label = "See More \xC2\xB7 Cast & Crew";
	const float btn_w = ui::chip_width(fonts_.small, btn_label);
	ui::chip(r, fonts_.small, btn_label, MARGIN, btn_y, btn_w, S(42), more_focused_, more_btn_focus_);

	std::string credits;
	if (!meta_.directors.empty()) {
		credits += "Director: " + clean(join_strings(meta_.directors, 2));
	}
	if (!meta_.cast.empty()) {
		if (!credits.empty()) credits += "   \xC2\xB7   ";
		credits += "Cast: " + clean(join_strings(meta_.cast, 4));
	}
	if (!credits.empty()) {
		const float cx = MARGIN + btn_w + S(18);
		r.text(fonts_.small, fonts_.small.ellipsize(credits, W - MARGIN - cx), cx, btn_y + S(27), theme::BASE_CONTENT_MUTED);
	}

	const bool has_episodes = has_meta_ && meta_.videos.size() > 1;
	float streams_x = MARGIN;
	if (has_episodes) {
		const float ew = EP_COL_W;
		const float ep_hdr_w = r.text(fonts_.section, "Episodes", MARGIN, LISTS_TOP, theme::BASE_CONTENT);
		if (!seasons_.empty()) {
			const std::string s_info = season_label(seasons_[season_idx_]) + "  \xC2\xB7  " +
					std::to_string(season_videos_.size()) + (season_videos_.size() == 1 ? " episode" : " episodes");
			r.text(fonts_.small, s_info, MARGIN + ep_hdr_w + S(16), LISTS_TOP - S(2), theme::BASE_CONTENT_MUTED);
		}

		float ep_y = LIST_Y, ep_h = LIST_H;
		if (has_seasons()) {
			draw_seasons(r, MARGIN, ep_y, ew);
			ep_y += FILTER_SPACE;
			ep_h -= FILTER_SPACE;
		}
		if (!season_videos_.empty()) {
			const int sel = std::clamp(episodes_list_.selected, 0, (int)season_videos_.size() - 1);
			draw_episode_preview(r, meta_.videos[season_videos_[sel]], MARGIN, ep_y, ew, EP_PREVIEW_H);
			ep_y += EP_PREVIEW_SPACE;
			ep_h -= EP_PREVIEW_SPACE;
		}

		std::vector<ListRow> rows;
		int active = -1;
		for (size_t i = 0; i < season_videos_.size(); i++) {
			const Video &v = meta_.videos[season_videos_[i]];
			const std::string line1 = (v.episode > 0 ? ("E" + std::to_string(v.episode) + "  \xC2\xB7  ") : "") +
					(v.title.empty() ? ("Episode " + std::to_string(v.episode)) : clean(v.title));
			const std::string line2 = !v.overview.empty() ? clean(v.overview) : "";
			ListRow row{ line1, line2, false };
			row.note = v.released_date;
			rows.push_back(row);
			if (v.id == video_id_) active = (int)i;
		}
		draw_list(r, episodes_list_, rows, active, MARGIN, ep_y, ew, ep_h);
		streams_x = MARGIN + ew + S(44);
	}

	std::string title = "Streams";
	if (has_meta_ && target_.type == "series") {
		if (const Video *v = meta_.find_video(video_id_)) title += "  \xC2\xB7  " + clean(MetaItem::video_label(*v));
	}
	const float sw = W - MARGIN - streams_x;
	r.text(fonts_.section, fonts_.section.ellipsize(title, sw), streams_x, LISTS_TOP, theme::BASE_CONTENT);
	if (!streams_status_.empty()) {
		r.text(fonts_.body, streams_status_, streams_x, LISTS_TOP + S(60), theme::BASE_CONTENT_MUTED);
	}
	float list_y = LIST_Y, list_h = LIST_H;
	if (has_filter()) {
		draw_filter(r, streams_x, LIST_Y, sw);
		list_y += FILTER_SPACE;
		list_h -= FILTER_SPACE;
	}
	std::vector<ListRow> rows;
	for (int i : visible_) {
		const StreamItem &s = streams_[i];
		const bool playable = !s.playable_url().empty();
		// Add-ons often start the name with their own name ("Torrentio\n1080p").
		std::string name = s.name;
		if (name.rfind(s.addon_name, 0) == 0) name.erase(0, s.addon_name.size());
		ListRow row{ clean(s.addon_name + "\n" + name), clean(playable ? s.details() : s.unsupported_reason()), !playable };
		if (playable) {
			seeders_badge(s.seeders, row.badge, row.badge_bg);
			row.note = clean(s.size_label);
		}
		rows.push_back(row);
	}
	draw_list(r, streams_list_, rows, -1, streams_x, list_y, sw, list_h);

	const bool saved = stremio().in_library(target_.meta_id);
	const char *accept_label = more_focused_ ? "Cast & Crew"
			: season_focused_ ? "Next Season"
			: filter_focused_ ? "Show Streams"
			: column_ == 0 ? "Load Episodes Streams" : "Play";
	ui::hints(r, fonts_.small,
			{ { "\xC3\x97", accept_label },
					{ "\xE2\x96\xA1", "See More / Cast & Crew" },
					{ "\xE2\x96\xB3", saved ? "Remove from library" : "Add to library" },
					{ "\xE2\x97\x8B", "Back" } },
			MARGIN, H - S(40));

	if (more_open_) {
		draw_more_modal(r);
	}

	toast_.draw(r, fonts_.body, W / 2, H - S(120));
}
