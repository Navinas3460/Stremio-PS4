#include "library_screen.h"

#include "../core/stremio.h"
#include "../platform/platform.h"
#include "components.h"
#include "theme.h"

#include <algorithm>
#include <map>

namespace {

using theme::S;

const float W = 1920;
const float MARGIN_X = 120;
const float VIEW_W = W - 2 * MARGIN_X;
const int COLUMNS = 8;
const float GAP = S(28), ROW_GAP = S(20);
const float CARD_W = (VIEW_W - (COLUMNS - 1) * GAP) / COLUMNS;
const float CARD_H = CARD_W * 1.5f; // posters are 2:3
const float CELL_H = CARD_H + POSTER_CAPTION_H;
const float PITCH = CELL_H + ROW_GAP;
const float TITLE_BASELINE = S(96);
const float CHIP_H = S(52), CHIP_GAP = S(12);
const float CHIPS_CY = S(78); // centred on the title
const float GRID_TOP = S(150);
const float VIEW_H = tabs::CONTENT_BOTTOM - GRID_TOP;

const char *SORT_LABELS[] = { "Recent", "A\xE2\x80\x93Z", "Most watched" }; // Stremio::LibrarySort order

// The filters last chosen, kept while the app runs (switching tabs recreates
// the screen).
std::string g_type;
int g_sort = (int)Stremio::LibrarySort::LAST_WATCHED;

std::string type_label(const std::string &type) {
	if (type == "movie") return "Movies";
	if (type == "series") return "Series";
	if (type == "channel") return "Channels";
	if (type == "tv") return "TV";
	std::string label = type;
	if (!label.empty()) label[0] = (char)toupper((unsigned char)label[0]);
	return label;
}

} // namespace

LibraryScreen::LibraryScreen(Fonts &fonts, Navigator &nav) : fonts_(fonts), navigator_(nav) {
	rebuild(false);
}

// --- data -> chips and cards -------------------------------------------------------

void LibraryScreen::rebuild(bool keep_selection) {
	Stremio &s = stremio();
	seen_library_rev_ = s.library_rev;
	seen_notifications_rev_ = s.notifications_rev;

	// Chips: "All", one per type in the library, then the sort orders.
	const std::vector<std::string> types = s.library_types();
	if (std::find(types.begin(), types.end(), g_type) == types.end()) g_type.clear();
	std::vector<Chip> chips;
	chips.push_back({ "All", "" });
	for (const std::string &t : types) chips.push_back({ type_label(t), t });
	for (int i = 0; i < 3; i++) {
		Chip c;
		c.label = SORT_LABELS[i];
		c.sort = i;
		chips.push_back(c);
	}
	const std::string focused_chip = chip_ < (int)chips_.size() ? chips_[chip_].label : "";
	chip_ = 0;
	for (size_t i = 0; i < chips.size(); i++) {
		for (const Chip &old : chips_) {
			if (old.label == chips[i].label) chips[i].focus = old.focus;
		}
		if (chips[i].label == focused_chip) chip_ = (int)i;
	}
	// Types from the left, after the title; sort orders against the right edge.
	float x = MARGIN_X + fonts_.page_header.measure("Library") + S(48);
	for (Chip &c : chips) {
		c.w = ui::chip_width(fonts_.body, c.label);
		if (c.sort >= 0) continue;
		c.x = x;
		x += c.w + CHIP_GAP;
	}
	x = W - MARGIN_X;
	for (auto it = chips.rbegin(); it != chips.rend() && it->sort >= 0; ++it) {
		x -= it->w;
		it->x = x;
		x -= CHIP_GAP;
	}
	chips_.swap(chips);

	// Cards.
	const std::string selected_id = keep_selection && selected_ < (int)cards_.size() ? cards_[selected_].meta_id : "";
	std::map<std::string, float> focus;
	for (const Card &c : cards_) focus[c.meta_id] = c.focus;
	cards_.clear();
	status_.clear();
	status_is_error_ = false;
	if (!s.library_loaded()) {
		status_ = "Loading your library\xE2\x80\xA6";
	} else if (!s.library_error().empty()) {
		status_ = "Couldn't load your library: " + s.library_error();
		status_is_error_ = true;
	} else {
		for (const LibraryItem *item : s.library(g_type, (Stremio::LibrarySort)g_sort)) {
			Card c;
			c.title = item->name;
			c.subtitle = item->episode_label();
			if (item->is_in_continue_watching()) c.progress = item->progress();
			const int n = s.notification_count(item->id);
			if (n > 0) c.badge = std::to_string(n) + " new";
			c.images = item->poster_images();
			c.meta_id = item->id;
			c.type = item->type;
			c.poster = item->poster;
			auto f = focus.find(c.meta_id);
			if (f != focus.end()) c.focus = f->second;
			cards_.push_back(c);
		}
		if (cards_.empty()) status_ = "Your library is empty. Open a title and press \xE2\x96\xB3 to add it.";
	}
	selected_ = 0;
	for (size_t i = 0; i < cards_.size(); i++) {
		if (cards_[i].meta_id == selected_id) selected_ = (int)i;
	}

	// Before the user has moved, focus starts on the grid once it has cards.
	if (!user_moved_) zone_ = cards_.empty() ? FILTERS : GRID;
	if (zone_ == GRID && cards_.empty()) zone_ = FILTERS;
}

bool LibraryScreen::chip_active(const Chip &c) const {
	return c.sort >= 0 ? c.sort == g_sort : c.type == g_type;
}

void LibraryScreen::activate_chip() {
	const Chip &c = chips_[chip_];
	if (chip_active(c)) return;
	if (c.sort >= 0) g_sort = c.sort;
	else g_type = c.type;
	scroll_ = 0;
	rebuild(false);
}

// --- input --------------------------------------------------------------------------

void LibraryScreen::move(int dx, int dy) {
	const int count = (int)cards_.size();
	switch (zone_) {
		case FILTERS:
			chip_ = std::clamp(chip_ + dx, 0, (int)chips_.size() - 1);
			if (dy > 0) zone_ = count > 0 ? GRID : BAR;
			break;
		case GRID: {
			const int row = selected_ / COLUMNS, last_row = (count - 1) / COLUMNS;
			if (dx != 0) { // stays within the row
				const int target = selected_ + dx;
				if (target >= 0 && target < count && target / COLUMNS == row) selected_ = target;
			} else if (dy < 0) {
				if (row > 0) selected_ -= COLUMNS;
				else zone_ = FILTERS;
			} else if (dy > 0) {
				if (row < last_row) selected_ = std::min(selected_ + COLUMNS, count - 1);
				else zone_ = BAR;
			}
			break;
		}
		case BAR: // left/right handled by the bar
			if (dy < 0) zone_ = count > 0 ? GRID : FILTERS;
			break;
	}
}

void LibraryScreen::update(double dt, uint32_t pressed) {
	Stremio &s = stremio();
	if (s.library_rev != seen_library_rev_ || s.notifications_rev != seen_notifications_rev_) rebuild(true);
	if (bar_.handle_shoulders(pressed, fonts_, navigator_)) return;
	if (zone_ == BAR && bar_.handle_focused(pressed, fonts_, navigator_)) return;
	if (zone_ != BAR && (pressed & platform::BUTTON_LEFT)) move(-1, 0);
	if (zone_ != BAR && (pressed & platform::BUTTON_RIGHT)) move(1, 0);
	if (pressed & platform::BUTTON_UP) move(0, -1);
	if (pressed & platform::BUTTON_DOWN) move(0, 1);
	if (pressed & platform::DIRECTIONS) user_moved_ = true;
	if (pressed & platform::BUTTON_ACCEPT) {
		if (zone_ == FILTERS) {
			activate_chip();
		} else if (zone_ == GRID) {
			Card c = cards_[selected_];
			// The details backdrop is landscape artwork, not the poster.
			if (const LibraryItem *item = s.library_item(c.meta_id)) c.images = item->landscape_images();
			open_details(c, fonts_, navigator_);
		}
	}

	for (int i = 0; i < (int)chips_.size(); i++) {
		chips_[i].focus = ui::approach(chips_[i].focus, (zone_ == FILTERS && i == chip_) ? 1.0f : 0.0f, dt);
	}
	for (int i = 0; i < (int)cards_.size(); i++) {
		cards_[i].focus = ui::approach(cards_[i].focus, (zone_ == GRID && i == selected_) ? 1.0f : 0.0f, dt);
	}
	if (zone_ == GRID) scroll_ = ui::follow(scroll_, (selected_ / COLUMNS) * PITCH, CELL_H, VIEW_H, dt);
	bar_.update(dt, zone_ == BAR);
}

// --- drawing --------------------------------------------------------------------------

void LibraryScreen::draw(Renderer &r) {
	if (!status_.empty()) {
		r.text(fonts_.body, fonts_.body.ellipsize(status_, VIEW_W), MARGIN_X, GRID_TOP + S(40),
				status_is_error_ ? theme::ERROR : theme::BASE_CONTENT_MUTED);
	}
	// The focused card last, so it overlaps its neighbours.
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < (int)cards_.size(); i++) {
			if ((pass == 0) == (cards_[i].focus > 0.5f)) continue;
			const float x = MARGIN_X + (i % COLUMNS) * (CARD_W + GAP);
			const float y = GRID_TOP + (i / COLUMNS) * PITCH - scroll_;
			if (y > tabs::CONTENT_BOTTOM || y + CELL_H < 0) continue; // off screen: don't load its art
			draw_poster(r, fonts_, cards_[i], x, y, CARD_W, CARD_H);
		}
	}

	// Header band (covers cards scrolled under it): title and filter chips.
	r.rect(0, 0, W, GRID_TOP - S(24), theme::BASE_100);
	r.text(fonts_.page_header, "Library", MARGIN_X, TITLE_BASELINE, theme::BASE_CONTENT);
	for (int pass = 0; pass < 2; pass++) { // the focused chip last, over its neighbours
		for (const Chip &c : chips_) {
			if ((pass == 0) == (c.focus > 0.5f)) continue;
			ui::chip(r, fonts_.body, c.label, c.x, CHIPS_CY - CHIP_H / 2, c.w, CHIP_H, chip_active(c), c.focus);
		}
	}

	bar_.draw(r, fonts_);
}
