#include "details_screen.h"

#include "../core/stremio.h"
#include "../net/images.h"
#include "../platform/platform.h"
#include "player_screen.h"
#include "theme.h"

#include <algorithm>

namespace {

using theme::S;

const float W = 1920, H = 1080;
const float MARGIN = 120;
const float ROW_H = S(84), ROW_GAP = S(10);
const float LISTS_TOP = S(330);               // "Episodes" / "Streams" titles
const float LIST_Y = LISTS_TOP + S(30);
const float LIST_H = H - S(90) - LIST_Y;

// Add-ons decorate stream names with emoji; Inter (and the PS4, which has no
// font fallback) can't draw them, so drop those code points.
std::string clean(const std::string &text) {
	std::string out;
	size_t i = 0;
	while (i < text.size()) {
		const size_t start = i;
		const uint32_t c = next_codepoint(text, i);
		if ((c >= 0x2600 && c <= 0x27BF) || c >= 0x1F000 || c == 0xFE0F || c == 0x200D) continue;
		out.append(text, start, i - start);
	}
	// Collapse newlines into separators.
	for (char &ch : out) {
		if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
	}
	return out;
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
		streams_status_ = "Loading episodes\xE2\x80\xA6";
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
	streams_list_ = ListState();
	streams_loading_ = true;
	streams_status_ = "Finding streams\xE2\x80\xA6";
	std::weak_ptr<int> alive = alive_;
	const std::string requested = video_id;
	stremio().request_streams(target_.type, video_id,
			[this, alive, requested](const std::string &, std::vector<StreamItem> &streams, const std::string &) {
				if (alive.expired() || requested != video_id_) return;
				// Playable streams first, keeping add-on order within each group.
				std::stable_partition(streams.begin(), streams.end(), [](const StreamItem &s) { return !s.playable_url().empty(); });
				const size_t playable_before = std::count_if(streams_.begin(), streams_.end(), [](const StreamItem &s) { return !s.playable_url().empty(); });
				size_t insert_at = playable_before;
				for (StreamItem &st : streams) {
					if (!st.playable_url().empty()) streams_.insert(streams_.begin() + insert_at++, st);
					else streams_.push_back(st);
				}
				streams_list_.focus.assign(streams_.size(), 0.0f);
				if (!streams_.empty()) streams_status_.clear();
			},
			[this, alive, requested]() {
				if (alive.expired() || requested != video_id_) return;
				streams_loading_ = false;
				if (streams_.empty()) streams_status_ = "No streams found. Install a streaming add-on in Stremio to watch this.";
			});
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
	if (pressed & platform::BUTTON_BACK) {
		nav_.pop();
		return;
	}
	if (pressed & platform::BUTTON_TRIANGLE) toggle_library();
	const bool has_episodes = has_meta_ && meta_.videos.size() > 1;
	if (!has_episodes) column_ = 1;
	ListState &list = column_ == 0 ? episodes_list_ : streams_list_;
	const int count = column_ == 0 ? (int)meta_.videos.size() : (int)streams_.size();
	if (pressed & platform::BUTTON_UP) list.selected = std::max(0, list.selected - 1);
	if (pressed & platform::BUTTON_DOWN) list.selected = std::max(0, std::min(count - 1, list.selected + 1));
	if ((pressed & platform::BUTTON_LEFT) && has_episodes) column_ = 0;
	if ((pressed & platform::BUTTON_RIGHT) && !streams_.empty()) column_ = 1;
	if (pressed & platform::BUTTON_ACCEPT) {
		if (column_ == 0 && count > 0) {
			const std::string id = meta_.videos[episodes_list_.selected].id;
			if (id != video_id_) load_streams(id);
			column_ = 1;
		} else if (column_ == 1 && count > 0) {
			play(streams_[streams_list_.selected]);
		}
	}
	// Animations.
	for (int c = 0; c < 2; c++) {
		ListState &l = c == 0 ? episodes_list_ : streams_list_;
		for (size_t i = 0; i < l.focus.size(); i++) {
			l.focus[i] = ui::approach(l.focus[i], (c == column_ && (int)i == l.selected) ? 1.0f : 0.0f, dt);
		}
		l.scroll = ui::follow(l.scroll, l.selected * (ROW_H + ROW_GAP), ROW_H, LIST_H, dt);
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
		const float tw = w - (tx - x) - pad;
		const Color title = row.muted ? theme::BASE_CONTENT_MUTED : theme::BASE_CONTENT;
		if (!row.line2.empty()) {
			r.text(fonts_.card_title, fonts_.card_title.ellipsize(row.line1, tw), tx, ry + S(36), title);
			r.text(fonts_.body, fonts_.body.ellipsize(row.line2, tw), tx, ry + S(66), theme::BASE_CONTENT_MUTED);
		} else {
			r.text(fonts_.card_title, fonts_.card_title.ellipsize(row.line1, tw), tx, ry + ROW_H / 2 + S(9), title);
		}
		ui::end_focus(r, x, ry, w, ROW_H, R, f);
	}
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
		ui::badge(r, fonts_.small, "In library", bx, by, theme::ACCENT, theme::ACCENT_CONTENT);
	}
	y += S(42);
	if (!meta_error_.empty() && !has_meta_) {
		r.text(fonts_.body, "Couldn't load details: " + meta_error_, MARGIN, y, theme::ERROR);
	}
	for (const std::string &line : fonts_.body.wrap(description_, S(1150), 3)) {
		r.text(fonts_.body, line, MARGIN, y, theme::BASE_CONTENT);
		y += S(30);
	}

	const bool has_episodes = has_meta_ && meta_.videos.size() > 1;
	float streams_x = MARGIN;
	if (has_episodes) {
		const float ew = S(560);
		r.text(fonts_.section, "Episodes", MARGIN, LISTS_TOP, theme::BASE_CONTENT);
		std::vector<ListRow> rows;
		int active = -1;
		for (size_t i = 0; i < meta_.videos.size(); i++) {
			rows.push_back({ clean(MetaItem::video_label(meta_.videos[i])) });
			if (meta_.videos[i].id == video_id_) active = (int)i;
		}
		draw_list(r, episodes_list_, rows, active, MARGIN, LIST_Y, ew, LIST_H);
		streams_x = MARGIN + ew + S(48);
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
	std::vector<ListRow> rows;
	for (const StreamItem &s : streams_) {
		const bool playable = !s.playable_url().empty();
		rows.push_back({ clean(s.addon_name + (s.name.empty() ? "" : "  \xC2\xB7  " + s.name)),
				clean(playable ? s.description : s.unsupported_reason()), !playable });
	}
	draw_list(r, streams_list_, rows, -1, streams_x, LIST_Y, sw, LIST_H);

	const bool saved = stremio().in_library(target_.meta_id);
	ui::hints(r, fonts_.small,
			{ { "\xC3\x97", column_ == 0 ? "Episode" : "Play" },
					{ "\xE2\x96\xB3", saved ? "Remove from library" : "Add to library" }, { "\xE2\x97\x8B", "Back" } },
			MARGIN, H - S(40));
	toast_.draw(r, fonts_.body, W / 2, H - S(120));
}
