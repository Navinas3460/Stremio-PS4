#include "details_screen.h"

#include "../core/stremio.h"
#include "../net/images.h"
#include "../platform/platform.h"
#include "i18n.h"
#include "player_screen.h"
#include "theme.h"

#include <algorithm>
#include <cctype>

namespace {

using theme::S;

const float W = 1920, H = 1080;
const float MARGIN = 120;
float row_h() { return S(84); }
float row_gap() { return S(10); }
float lists_top() { return S(330); } // "Episodes" / "Streams" titles
float list_y() { return lists_top() + S(30); }
float list_h() { return H - S(90) - list_y(); }
// Add-on filter: a smaller version of the home tab bar, over the streams.
float chip_h() { return S(46); }
float chip_min_w() { return S(110); }
float box_pad() { return S(6); }
float box_h() { return chip_h() + 2 * box_pad(); }
float filter_space() { return box_h() + S(18); } // taken from the top of the streams list
float caps_w() { return S(110); } // the L1 / R1 marks after the box

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

// "56 seeders", coloured by how quickly the torrent is likely to start.
void seeders_badge(int seeders, std::string &text, Color &bg) {
	if (seeders < 0) return;
        text = std::to_string(seeders) + i18n::tr(
                seeders == 1 ? i18n::Text::SEEDER_SUFFIX : i18n::Text::SEEDERS_SUFFIX);
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
	if (!video_id_.empty()) {
		load_streams(video_id_);
	} else {
                streams_status_ = i18n::tr(i18n::Text::LOADING_EPISODES);
	}
}

void DetailsScreen::on_meta(const MetaItem &meta) {
	has_meta_ = true;
	meta_ = meta;
	description_ = clean(meta.description);
	if (!meta.name.empty()) target_.name = meta.name;
	if (!meta.release_info.empty()) target_.release_info = meta.release_info;
	if (!meta.landscape_images().empty()) target_.images = meta.landscape_images();
	episodes_list_.focus.assign(meta_.videos.size(), 0.0f);
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
	// Start the episode list on the current episode.
	for (size_t i = 0; i < meta_.videos.size(); i++) {
		if (meta_.videos[i].id == video_id_) episodes_list_.selected = (int)i;
	}
	if (meta_.videos.size() > 1 && streams_.empty()) column_ = 0;
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
        streams_status_ = i18n::tr(i18n::Text::FINDING_STREAMS);
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
                                if (streams_.empty()) streams_status_ = i18n::tr(i18n::Text::NO_STREAMS_FOUND);
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
        w.push_back(std::max(chip_min_w(), ui::chip_width(fonts_.body, i18n::tr(i18n::Text::FILTER_ALL))));
	for (const std::string &a : addons_) w.push_back(std::max(chip_min_w(), ui::chip_width(fonts_.body, clean(a))));
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
                toast_.show(i18n::tr(i18n::Text::REMOVED_FROM_LIBRARY));
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
        toast_.show(i18n::tr(i18n::Text::ADDED_TO_LIBRARY));
}

void DetailsScreen::update(double dt, uint32_t pressed) {
	if (pressed & platform::BUTTON_BACK) {
		nav_.pop();
		return;
	}
	if (pressed & platform::BUTTON_TRIANGLE) toggle_library();
	const bool has_episodes = has_meta_ && meta_.videos.size() > 1;
	if (!has_episodes) column_ = 1;
	const int filters = (int)addons_.size() + 1;
	if (!has_filter()) filter_focused_ = false;
	// L1 / R1 switch the add-on filter from anywhere, like the tabs on home.
	if (has_filter() && (pressed & (platform::BUTTON_L1 | platform::BUTTON_R1))) {
		set_filter((filter_ + ((pressed & platform::BUTTON_R1) ? 1 : -1) + filters) % filters);
	}
	if (filter_focused_) {
		if (pressed & platform::BUTTON_LEFT) {
			if (filter_ > 0) set_filter(filter_ - 1);
			else if (has_episodes) {
				column_ = 0;
				filter_focused_ = false;
			}
		}
		if ((pressed & platform::BUTTON_RIGHT) && filter_ < filters - 1) set_filter(filter_ + 1);
		if ((pressed & (platform::BUTTON_DOWN | platform::BUTTON_ACCEPT)) && !visible_.empty()) filter_focused_ = false;
	} else {
		ListState &list = column_ == 0 ? episodes_list_ : streams_list_;
		const int count = column_ == 0 ? (int)meta_.videos.size() : (int)visible_.size();
		if (pressed & platform::BUTTON_UP) {
			if (column_ == 1 && list.selected == 0 && has_filter()) filter_focused_ = true;
			list.selected = std::max(0, list.selected - 1);
		}
		if (pressed & platform::BUTTON_DOWN) list.selected = std::max(0, std::min(count - 1, list.selected + 1));
		if ((pressed & platform::BUTTON_LEFT) && has_episodes) column_ = 0;
		if ((pressed & platform::BUTTON_RIGHT) && !streams_.empty()) column_ = 1;
		if (pressed & platform::BUTTON_ACCEPT) {
			if (column_ == 0 && count > 0) {
				const std::string id = meta_.videos[episodes_list_.selected].id;
				if (id != video_id_) load_streams(id);
				column_ = 1;
			} else if (column_ == 1 && count > 0) {
				play(streams_[visible_[streams_list_.selected]]);
			}
		}
	}
	// Animations.
	for (int c = 0; c < 2; c++) {
		ListState &l = c == 0 ? episodes_list_ : streams_list_;
		for (size_t i = 0; i < l.focus.size(); i++) {
			const bool focused = c == column_ && (int)i == l.selected && !(c == 1 && filter_focused_);
			l.focus[i] = ui::approach(l.focus[i], focused ? 1.0f : 0.0f, dt);
		}
		const float view = c == 1 && has_filter() ? list_h() - filter_space() : list_h();
		l.scroll = ui::follow(l.scroll, l.selected * (row_h() + row_gap()), row_h(), view, dt);
	}
	filter_focus_.resize(filters, 0.0f);
	for (int i = 0; i < filters; i++) {
		filter_focus_[i] = ui::approach(filter_focus_[i], (filter_focused_ && i == filter_) ? 1.0f : 0.0f, dt);
	}
	if (has_filter()) { // keep the current chip inside the box when they overflow it
		const std::vector<float> w = filter_widths();
		float start = 0;
		for (int i = 0; i < filter_; i++) start += w[i];
		const float sx = has_episodes ? MARGIN + S(560) + S(48) : MARGIN;
		const float view = W - MARGIN - sx - caps_w() - 2 * box_pad();
		filter_scroll_ = ui::follow(filter_scroll_, start, w[filter_], view, dt);
	}
	toast_.update(dt);
}

void DetailsScreen::draw_list(Renderer &r, ListState &list, const std::vector<ListRow> &rows, int active, float x,
		float y, float w, float h) {
	const float R = theme::radius_field(), pad = S(22);
	for (size_t i = 0; i < rows.size(); i++) {
		const float ry = y + i * (row_h() + row_gap()) - list.scroll;
		if (ry < y - S(6) || ry + row_h() > y + h + S(6)) continue; // only whole rows (the hint line is below)
		const ListRow &row = rows[i];
		const float f = i < list.focus.size() ? list.focus[i] : 0.0f;
		ui::begin_focus(r, x, ry, w, row_h(), R, f, 1.03f);
		r.rounded_rect(x, ry, w, row_h(), R, mix(theme::BASE_200, theme::PRIMARY, 0.35f * f));
		float tx = x + pad;
		if ((int)i == active) { // e.g. the episode whose streams are listed
			ui::status(r, tx + S(6), ry + row_h() / 2);
			tx += S(26);
		}
		float tw = w - (tx - x) - pad;
		const Color title = row.muted ? theme::BASE_CONTENT_MUTED : theme::BASE_CONTENT;
		if (!row.badge.empty()) { // right end of the first line
			const float bw = ui::badge_width(fonts_.small, row.badge), bh = ui::badge_height(fonts_.small);
			const float by = (row.line2.empty() ? ry + row_h() / 2 : ry + S(36) - fonts_.card_title.size() * 0.36f) - bh / 2;
			ui::badge(r, fonts_.small, row.badge, x + w - pad - bw, by, row.badge_bg, theme::BASE_100);
			tw -= bw + S(16);
		}
		if (!row.note.empty()) {
			const float nw = fonts_.body.measure(row.note);
			r.text(fonts_.body, row.note, tx + tw - nw, row.line2.empty() ? ry + row_h() / 2 + S(8) : ry + S(36),
					theme::BASE_CONTENT_MUTED);
			tw -= nw + S(20);
		}
		if (!row.line2.empty()) {
			r.text(fonts_.card_title, fonts_.card_title.ellipsize(row.line1, tw), tx, ry + S(36), title);
			r.text(fonts_.body, fonts_.body.ellipsize(row.line2, w - (tx - x) - pad), tx, ry + S(66), theme::BASE_CONTENT_MUTED);
		} else {
			r.text(fonts_.card_title, fonts_.card_title.ellipsize(row.line1, tw), tx, ry + row_h() / 2 + S(9), title);
		}
		ui::end_focus(r, x, ry, w, row_h(), R, f);
	}
}

void DetailsScreen::draw_filter(Renderer &r, float x, float y, float w) {
	const std::vector<float> widths = filter_widths();
	float total = 0;
	for (float cw : widths) total += cw;
	const float box_w = std::min(total + 2 * box_pad(), w - caps_w());
	const float scroll = total + 2 * box_pad() > box_w ? filter_scroll_ : 0;
	r.rounded_rect(x, y, box_w, box_h(), theme::radius_selector(), theme::BASE_200);
	// The focused chip last, so its ring isn't covered by its neighbours.
	for (int pass = 0; pass < 2; pass++) {
		float cx = x + box_pad() - scroll;
		for (size_t i = 0; i < widths.size(); i++) {
			const float f = i < filter_focus_.size() ? filter_focus_[i] : 0.0f;
			const bool inside = cx >= x + box_pad() - 1 && cx + widths[i] <= x + box_w - box_pad() + 1; // only whole chips
			if (inside && (pass == 0) != (f > 0.5f)) {
                                const std::string label = i == 0 ? i18n::tr(i18n::Text::FILTER_ALL) : clean(addons_[i - 1]);
				ui::chip(r, fonts_.body, label, cx, y + box_pad(), widths[i], chip_h(), (int)i == filter_, f, theme::BASE_200);
			}
			cx += widths[i];
		}
	}
	const float hy = y + box_h() / 2 + fonts_.small.size() * 0.36f;
	const float kx = x + box_w + S(18);
	ui::kbd(r, fonts_.small, "R1", kx + ui::kbd_width(fonts_.small, "L1") + S(8), hy);
	ui::kbd(r, fonts_.small, "L1", kx, hy);
}

void DetailsScreen::draw(Renderer &r) {
	// Backdrop, darkened (solid overlay: alpha on solids is proven on PS4).
	const Texture &art = images::first(target_.images);
	if (art.valid()) {
		r.image_cover(art, 0, 0, W, H);
		r.rect(0, 0, W, H, theme::BASE_100.with_alpha(0.78f));
	}

	float y = S(110);
	r.text(fonts_.page_header, fonts_.page_header.ellipsize(target_.name, W - 2 * MARGIN), MARGIN, y, theme::BASE_CONTENT);
	y += S(44);
	std::string info = target_.type.empty() ? "" : std::string(1, (char)toupper(target_.type[0])) + target_.type.substr(1);
	if (!target_.release_info.empty()) info += "  \xC2\xB7  " + target_.release_info;
	const float info_w = r.text(fonts_.body, info, MARGIN, y, theme::BASE_CONTENT_MUTED);
	if (stremio().in_library(target_.meta_id)) {
		const float bx = MARGIN + info_w + (info.empty() ? 0 : S(20));
		const float by = y - fonts_.body.size() * 0.36f - ui::badge_height(fonts_.small) / 2;
                ui::badge(r, fonts_.small, i18n::tr(i18n::Text::IN_LIBRARY), bx, by, theme::ACCENT, theme::ACCENT_CONTENT);
	}
	y += S(42);
	if (!meta_error_.empty() && !has_meta_) {
                r.text(fonts_.body, std::string(i18n::tr(i18n::Text::DETAILS_LOAD_ERROR)) + meta_error_, MARGIN, y, theme::ERROR);
	}
	for (const std::string &line : fonts_.body.wrap(description_, S(1150), 3)) {
		r.text(fonts_.body, line, MARGIN, y, theme::BASE_CONTENT);
		y += S(30);
	}

	const bool has_episodes = has_meta_ && meta_.videos.size() > 1;
	float streams_x = MARGIN;
	if (has_episodes) {
		const float ew = S(560);
                r.text(fonts_.section, i18n::tr(i18n::Text::EPISODES), MARGIN, lists_top(), theme::BASE_CONTENT);
		std::vector<ListRow> rows;
		int active = -1;
		for (size_t i = 0; i < meta_.videos.size(); i++) {
			rows.push_back({ clean(MetaItem::video_label(meta_.videos[i])) });
			if (meta_.videos[i].id == video_id_) active = (int)i;
		}
		draw_list(r, episodes_list_, rows, active, MARGIN, list_y(), ew, list_h());
		streams_x = MARGIN + ew + S(48);
	}

        std::string title = i18n::tr(i18n::Text::STREAMS);
	if (has_meta_ && target_.type == "series") {
		if (const Video *v = meta_.find_video(video_id_)) title += "  \xC2\xB7  " + clean(MetaItem::video_label(*v));
	}
	const float sw = W - MARGIN - streams_x;
	r.text(fonts_.section, fonts_.section.ellipsize(title, sw), streams_x, lists_top(), theme::BASE_CONTENT);
	if (!streams_status_.empty()) {
		r.text(fonts_.body, streams_status_, streams_x, lists_top() + S(60), theme::BASE_CONTENT_MUTED);
	}
	float streams_y = list_y(), streams_h = list_h();
	if (has_filter()) {
		draw_filter(r, streams_x, list_y(), sw);
		streams_y += filter_space();
		streams_h -= filter_space();
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
	draw_list(r, streams_list_, rows, -1, streams_x, streams_y, sw, streams_h);

	const bool saved = stremio().in_library(target_.meta_id);
	ui::hints(r, fonts_.small,
                        { { "\xC3\x97", i18n::tr(filter_focused_
                                ? i18n::Text::SHOW_STREAMS
                                : column_ == 0 ? i18n::Text::EPISODE : i18n::Text::PLAY) },
                          { "\xE2\x96\xB3", i18n::tr(saved
                                ? i18n::Text::REMOVE_FROM_LIBRARY
                                : i18n::Text::ADD_TO_LIBRARY) },
                          { "\xE2\x97\x8B", i18n::tr(i18n::Text::BACK) } },
			MARGIN, H - S(40));
	toast_.draw(r, fonts_.body, W / 2, H - S(120));
}
