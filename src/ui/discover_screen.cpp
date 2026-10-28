#include "discover_screen.h"

#include "../core/stremio.h"
#include "../platform/platform.h"
#include "components.h"
#include "i18n.h"
#include "theme.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

namespace {

using theme::S;
using namespace poster_grid;

const float W = 1920;
const float MARGIN_X = 120;
const float VIEW_W = W - 2 * MARGIN_X;
float title_baseline() { return S(96); }
float grid_top() { return S(150); }

// The filter drop-down: a panel under the title with one list per column.
float menu_top() { return S(124); }
float menu_pad() { return S(24); }
float menu_gap() { return S(24); }
float list_top() { return menu_top() + menu_pad() + S(46); } // under the column titles
float menu_row_h() { return S(56); }
float menu_row_gap() { return S(6); }
float column_w(int column) { return S(column == 1 ? 440 : 300); } // catalog names are the longest

// Cinemeta's "New" catalog filters by year with its "genre" extra.
bool all_years(const std::vector<std::string> &options) {
	int years = 0;
	for (const std::string &o : options) {
		if (o.empty()) continue;
		if (o.size() != 4 || !std::all_of(o.begin(), o.end(), [](char c) { return isdigit((unsigned char)c); })) return false;
		years++;
	}
	return years > 0;
}

} // namespace

DiscoverScreen::DiscoverScreen(Fonts &fonts, Navigator &nav) : fonts_(fonts), navigator_(nav) {
	stremio().discover_open();
	rebuild();
}

// --- data -> cards --------------------------------------------------------------------

void DiscoverScreen::rebuild() {
	Stremio &s = stremio();
	const Discover &d = s.discover();
	seen_discover_rev_ = s.discover_rev;
	seen_library_rev_ = s.library_rev;
	seen_notifications_rev_ = s.notifications_rev;

	// Cards, keeping the focused title (by id) when it's still there.
	const std::string selected_id = selected_ < (int)cards_.size() ? cards_[selected_].meta_id : "";
	std::map<std::string, float> focus;
	for (const Card &c : cards_) focus[c.meta_id] = c.focus;
	cards_.clear();
	for (const MetaItem &m : d.items) {
		Card c = Card::from_meta(m);
		c.images = m.poster_images();
		if (const LibraryItem *item = s.library_item(m.id)) {
			if (item->is_in_continue_watching()) c.progress = item->progress();
			const int n = s.notification_count(item->id);
                        if (n > 0) c.badge = std::to_string(n)
                                + i18n::tr(n == 1 ? i18n::Text::NEW_SINGULAR_SUFFIX : i18n::Text::NEW_PLURAL_SUFFIX);
		}
		auto f = focus.find(c.meta_id);
		if (f != focus.end()) c.focus = f->second;
		cards_.push_back(c);
	}
	selected_ = 0;
	for (size_t i = 0; i < cards_.size(); i++) {
		if (cards_[i].meta_id == selected_id) selected_ = (int)i;
	}
	if (cards_.empty()) scroll_ = 0;
	// Before the user has moved, focus starts on the grid once it has cards.
	if (!user_moved_) zone_ = cards_.empty() ? BAR : GRID;
	if (zone_ == GRID && cards_.empty()) zone_ = BAR;

	status_.clear();
	status_is_error_ = false;
	if (cards_.empty()) {
		if (!s.addons_loaded()) {
                        status_ = i18n::tr(i18n::Text::LOADING_ADDONS);
		} else if (d.types.empty()) {
                        status_ = s.addons_error().empty()
                                ? std::string(i18n::tr(i18n::Text::NO_BROWSABLE_CATALOGS))
                                : std::string(i18n::tr(i18n::Text::ADDONS_LOAD_ERROR)) + s.addons_error();
			status_is_error_ = !s.addons_error().empty();
		} else if (d.loading) {
                        status_ = i18n::tr(i18n::Text::LOADING);
		} else if (!d.error.empty()) {
                        status_ = std::string(i18n::tr(i18n::Text::CATALOG_LOAD_ERROR)) + d.error;
			status_is_error_ = true;
		} else {
                        status_ = i18n::tr(i18n::Text::EMPTY_CATALOG);
		}
	}

	// The drop-down's other columns follow what's chosen.
	for (int c = 0; c < COLUMNS; c++) {
		const int n = (int)column_items(c).size();
		if (!menu_open_ || c != menu_column_) columns_[c].selected = column_active(c);
		columns_[c].selected = std::clamp(columns_[c].selected, 0, std::max(0, n - 1));
	}
	if (menu_column_ == FILTER && column_items(FILTER).empty()) menu_column_ = CATALOG;
}

std::vector<std::string> DiscoverScreen::column_items(int column) const {
	const Discover &d = stremio().discover();
	std::vector<std::string> items;
	if (column == TYPE) {
		for (const std::string &t : d.types) items.push_back(type_label(t));
	} else if (column == CATALOG) {
                for (const std::string &name : d.catalogs) items.push_back(i18n::catalog_label(name));
	} else {
                for (const std::string &f : d.filter_options) items.push_back(i18n::label(f.empty() ? "All" : f));
	}
	return items;
}

int DiscoverScreen::column_active(int column) const {
	const Discover &d = stremio().discover();
	if (column == TYPE) return (int)(std::find(d.types.begin(), d.types.end(), d.type) - d.types.begin());
	if (column == CATALOG) return d.catalog;
	return (int)(std::find(d.filter_options.begin(), d.filter_options.end(), d.filter) - d.filter_options.begin());
}

// --- input ----------------------------------------------------------------------------

void DiscoverScreen::open_menu() {
	menu_open_ = true;
	menu_column_ = TYPE;
	for (int c = 0; c < COLUMNS; c++) columns_[c].selected = std::max(0, column_active(c));
}

void DiscoverScreen::update_menu(uint32_t pressed) {
	const int count = (int)column_items(menu_column_).size();
	Column &col = columns_[menu_column_];
	if (pressed & platform::BUTTON_UP) col.selected = std::max(0, col.selected - 1);
	if (pressed & platform::BUTTON_DOWN) col.selected = std::min(count - 1, col.selected + 1);
	if ((pressed & platform::BUTTON_LEFT) && menu_column_ > TYPE) menu_column_--;
	if ((pressed & platform::BUTTON_RIGHT) && menu_column_ + 1 < COLUMNS && !column_items(menu_column_ + 1).empty()) {
		menu_column_++;
	}
	if ((pressed & platform::BUTTON_ACCEPT) && col.selected < count && col.selected != column_active(menu_column_)) {
		// Applies at once; the drop-down stays open for the next choice.
		Stremio &s = stremio();
		const Discover &d = s.discover();
		if (menu_column_ == TYPE) {
			s.discover_select(d.types[col.selected], 0, "");
		} else if (menu_column_ == CATALOG) {
			s.discover_select(d.type, col.selected, "");
		} else {
			s.discover_select(d.type, d.catalog, d.filter_options[col.selected]);
		}
		selected_ = 0;
		scroll_ = 0;
	}
	if (pressed & (platform::BUTTON_BACK | platform::BUTTON_SQUARE)) {
		menu_open_ = false;
		if (!cards_.empty()) zone_ = GRID;
	}
}

void DiscoverScreen::move(int dx, int dy) {
	const int count = (int)cards_.size();
	if (zone_ == GRID) {
		const int cols = columns(), row = selected_ / cols, last_row = (count - 1) / cols;
		if (dx != 0) { // stays within the row
			const int target = selected_ + dx;
			if (target >= 0 && target < count && target / cols == row) selected_ = target;
		} else if (dy < 0) {
			if (row > 0) selected_ -= cols;
		} else if (dy > 0) {
			const Discover &d = stremio().discover();
			if (row < last_row) selected_ = std::min(selected_ + cols, count - 1);
			else if (!d.loading && !d.has_more) zone_ = BAR; // else the next page is on its way
		}
	} else if (dy < 0 && count > 0) { // the bar handles left / right
		zone_ = GRID;
	}
}

void DiscoverScreen::update(double dt, uint32_t pressed) {
	Stremio &s = stremio();
	if (s.discover_rev != seen_discover_rev_ || s.library_rev != seen_library_rev_ ||
			s.notifications_rev != seen_notifications_rev_) {
		rebuild();
	}
	if (menu_open_) {
		update_menu(pressed);
	} else {
		if (bar_.handle_shoulders(pressed, fonts_, navigator_)) return;
		if (zone_ == BAR && bar_.handle_focused(pressed, fonts_, navigator_)) return;
		if (zone_ != BAR && (pressed & platform::BUTTON_LEFT)) move(-1, 0);
		if (zone_ != BAR && (pressed & platform::BUTTON_RIGHT)) move(1, 0);
		if (pressed & platform::BUTTON_UP) move(0, -1);
		if (pressed & platform::BUTTON_DOWN) move(0, 1);
		if (pressed & platform::DIRECTIONS) user_moved_ = true;
		if ((pressed & platform::BUTTON_ACCEPT) && zone_ == GRID && selected_ < (int)cards_.size()) {
			Card c = cards_[selected_];
			// The details backdrop is landscape artwork, not the poster.
			c.images = s.discover().items[selected_].landscape_images();
			open_details(c, fonts_, navigator_);
		}
		if (pressed & platform::BUTTON_SQUARE) open_menu();
	}
	// Near the end of the grid: the next page.
	if (!cards_.empty() && selected_ / columns() >= ((int)cards_.size() - 1) / columns() - 1) s.discover_load_more();

	for (int i = 0; i < (int)cards_.size(); i++) {
		cards_[i].focus = ui::approach(cards_[i].focus, (!menu_open_ && zone_ == GRID && i == selected_) ? 1.0f : 0.0f, dt);
	}
	if (zone_ == GRID) scroll_ = ui::follow(scroll_, (selected_ / columns()) * pitch(), cell_h(), tabs::content_bottom() - grid_top(), dt);
	menu_ += ((menu_open_ ? 1.0f : 0.0f) - menu_) * std::min(1.0f, (float)dt * 10.0f);
	// The lists scroll by whole rows (partly hidden rows aren't drawn).
	const float pitch = menu_row_h() + menu_row_gap();
	const float list_h = std::floor((tabs::content_bottom() - menu_pad() - list_top() + menu_row_gap()) / pitch) * pitch - menu_row_gap();
	for (Column &c : columns_) c.scroll = ui::follow(c.scroll, c.selected * pitch, menu_row_h(), list_h, dt);
	bar_.update(dt, !menu_open_ && zone_ == BAR);
}

// --- drawing --------------------------------------------------------------------------

void DiscoverScreen::draw_menu(Renderer &r) {
	const Discover &d = stremio().discover();
        const std::string titles[COLUMNS] = {
                i18n::label("Type"),
                i18n::label("Catalog"),
                i18n::label(all_years(d.filter_options) ? "Year" : "Genre")
        };
	const bool has_filter = !column_items(FILTER).empty();
	float width = 2 * menu_pad() + column_w(TYPE) + menu_gap() + column_w(CATALOG);
	if (has_filter) width += menu_gap() + column_w(FILTER);
	const float x0 = MARGIN_X - menu_pad(), bottom = tabs::content_bottom();
	r.push(x0, menu_top(), 1.0f, menu_); // fades in and out as a whole
	r.rounded_rect(x0, menu_top(), width, bottom - menu_top(), theme::radius_box(), theme::BASE_200);
	float x = MARGIN_X;
	const float R = theme::radius_field();
	for (int c = 0; c < (has_filter ? COLUMNS : FILTER); c++) {
		const std::vector<std::string> items = column_items(c);
		const float w = column_w(c);
		const bool focused = menu_column_ == c;
		r.text(fonts_.card_title, titles[c], x, list_top() - S(18), focused ? theme::BASE_CONTENT : theme::BASE_CONTENT_MUTED);
		const Column &col = columns_[c];
		const int active = column_active(c);
		for (int i = 0; i < (int)items.size(); i++) {
			const float y = list_top() + i * (menu_row_h() + menu_row_gap()) - col.scroll;
			if (y < list_top() - S(2) || y + menu_row_h() > bottom - menu_pad() + S(2)) continue; // whole rows only
			const float sel = (focused && i == col.selected) ? 1.0f : 0.0f;
			ui::begin_focus(r, x, y, w, menu_row_h(), R, sel, 1.0f);
			r.rounded_rect(x, y, w, menu_row_h(), R, mix(theme::BASE_300, theme::PRIMARY, 0.35f * sel));
			if (i == active) ui::status(r, x + S(24), y + menu_row_h() / 2); // the one chosen
			const float tx = x + S(44);
			r.text(fonts_.body, fonts_.body.ellipsize(items[i], w - (tx - x) - S(14)), tx,
					y + menu_row_h() / 2 + fonts_.body.size() * 0.36f, theme::BASE_CONTENT);
			r.pop();
		}
		x += w + menu_gap();
	}
	r.pop();
}

void DiscoverScreen::draw(Renderer &r) {
	const Discover &d = stremio().discover();
	if (!status_.empty()) {
		r.text(fonts_.body, fonts_.body.ellipsize(status_, VIEW_W), MARGIN_X, grid_top() + S(40),
				status_is_error_ ? theme::ERROR : theme::BASE_CONTENT_MUTED);
	}
	// The focused card last, so it overlaps its neighbours.
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < (int)cards_.size(); i++) {
			if ((pass == 0) == (cards_[i].focus > 0.5f)) continue;
			const float x = MARGIN_X + (i % columns()) * (card_w() + gap());
			const float y = grid_top() + (i / columns()) * pitch() - scroll_;
			if (y > tabs::content_bottom() || y + cell_h() < 0) continue; // off screen: don't load its art
			draw_poster(r, fonts_, cards_[i], x, y, card_w(), card_h());
		}
	}
	if (!cards_.empty() && d.loading) { // the next page
		const float y = grid_top() + ((cards_.size() + columns() - 1) / columns()) * pitch() - scroll_ + S(30);
                if (y < tabs::content_bottom()) r.text(fonts_.body,
                        i18n::tr(i18n::Text::LOADING_MORE), MARGIN_X, y, theme::BASE_CONTENT_MUTED);
	}

	// Header band (covers cards scrolled under it): the title, what's shown
	// and the filters' button.
	r.rect(0, 0, W, grid_top() - S(24), theme::BASE_100);
        const float title_w = r.text(fonts_.page_header, i18n::tr(i18n::Text::DISCOVER), MARGIN_X, title_baseline(), theme::BASE_CONTENT);
	std::string shown;
	if (!d.type.empty()) shown = type_label(d.type);
        if (d.catalog < (int)d.catalogs.size()) shown += " \xC2\xB7 " + i18n::catalog_label(d.catalogs[d.catalog]);
        if (!d.filter.empty()) shown += " \xC2\xB7 " + i18n::label(d.filter);
        const std::vector<ui::Hint> hints = { { "\xE2\x96\xA1", i18n::tr(i18n::Text::FILTERS) } };
	const float hints_w = ui::hints_width(fonts_.small, hints);
	const float shown_x = MARGIN_X + title_w + S(32);
	r.text(fonts_.card_title, fonts_.card_title.ellipsize(shown, W - MARGIN_X - hints_w - S(32) - shown_x), shown_x,
			title_baseline(), theme::BASE_CONTENT_MUTED);
	ui::hints(r, fonts_.small, hints, W - MARGIN_X - hints_w, title_baseline() - S(4));

	if (menu_ > 0.01f) draw_menu(r);
	bar_.draw(r, fonts_);
}
