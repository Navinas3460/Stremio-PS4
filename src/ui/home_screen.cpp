#include "home_screen.h"

#include "../core/stremio.h"
#include "../platform/platform.h"
#include "components.h"
#include "theme.h"

#include <algorithm>

namespace {

using theme::S;
using theme::HS;

const float W = 1920;
const float H = 1080;
const float MARGIN_X = 120;
const float VIEW_W = W - 2 * MARGIN_X;

float rows_top() {
	return S(102); // first row's title top
}

// Home screen row layout controlled by the separate Home screen scale slider in Settings.
CardRowLayout row_layout() {
	return { HS(218), HS(122), HS(18), HS(30), HS(166), HS(74) };
}

} // namespace

HomeScreen::HomeScreen(Fonts &fonts, Navigator &nav) : fonts_(fonts), navigator_(nav) {
	rebuild_rows();
}

// --- data -> rows -----------------------------------------------------------------

void HomeScreen::rebuild_rows() {
	Stremio &s = stremio();
	seen_library_rev_ = s.library_rev;
	seen_board_rev_ = s.board_rev;
	seen_notifications_rev_ = s.notifications_rev;
	const std::string focused_title = focus_row_ >= 0 && focus_row_ < (int)rows_.size() ? rows_[focus_row_].title : "";

	std::vector<CardRow> rows;
	CardRow cw;
	cw.title = "Continue watching";
	if (!s.library_loaded()) {
		cw.status = "Loading your library\xE2\x80\xA6";
	} else if (!s.library_error().empty()) {
		cw.status = "Couldn't load your library: " + s.library_error();
		cw.status_is_error = true;
	} else {
		for (const LibraryItem *item : s.continue_watching()) {
			Card c;
			c.title = item->name;
			c.subtitle = item->episode_label();
			const int left = item->minutes_left();
			if (left > 0 && item->progress() > 0) {
				c.subtitle += (c.subtitle.empty() ? "" : "  \xC2\xB7  ") + std::to_string(left) + " min left";
			}
			c.progress = item->progress();
			const int n = s.notification_count(item->id);
			if (n > 0) c.badge = n == 1 ? "1 new episode" : std::to_string(n) + " new episodes";
			c.images = item->landscape_images();
			c.meta_id = item->id;
			c.type = item->type;
			c.poster = item->poster;
			cw.cards.push_back(c);
		}
		if (cw.cards.empty()) cw.status = "Nothing to continue yet. Start something in Stremio and it will show up here.";
	}
	rows.push_back(cw);

	if (!s.addons_error().empty()) {
		CardRow err;
		err.title = "Add-ons";
		err.status = "Couldn't load your add-ons: " + s.addons_error();
		err.status_is_error = true;
		rows.push_back(err);
	}
	for (const BoardRow &b : s.board()) {
		CardRow row;
		if (CardRow::from_board(b, "Loading\xE2\x80\xA6", "Couldn't load this catalog: ", row)) rows.push_back(row);
	}
	carry_over(rows, rows_);
	rows_.swap(rows);

	// Before the user has moved, focus follows the first row with cards as
	// data arrives. Afterwards it stays on the same row (by title) or in the
	// nav bar; if its row disappeared, it goes to the first row with cards.
	const bool was_on_nav = user_moved_ && focus_row_ < 0;
	focus_row_ = -1;
	if (user_moved_) {
		for (int i = 0; i < (int)rows_.size(); i++) {
			if (rows_[i].title == focused_title && !rows_[i].cards.empty()) focus_row_ = i;
		}
	}
	if (focus_row_ < 0 && !was_on_nav) {
		focus_row_ = next_row_with_cards(rows_, -1, 1); // -1 (nav bar) if no row has cards yet
	}
}

// --- layout -------------------------------------------------------------------------

float HomeScreen::row_top(int index) const {
	const CardRowLayout l = row_layout();
	float y = 0;
	for (int i = 0; i < index; i++) y += row_height(rows_[i], l);
	return y;
}

// --- input --------------------------------------------------------------------------

void HomeScreen::move(int dx, int dy) {
	if (focus_row_ < 0) { // tab bar (left/right handled by the bar)
		if (dy < 0) {
			const int last = next_row_with_cards(rows_, (int)rows_.size(), -1);
			if (last >= 0) focus_row_ = last;
		}
		return;
	}
	CardRow &row = rows_[focus_row_];
	if (dx != 0) {
		row.selected = std::clamp(row.selected + dx, 0, (int)row.cards.size() - 1);
		return;
	}
	// Each row keeps its own position (like Stremio): moving up/down doesn't
	// carry the column over.
	const int target = next_row_with_cards(rows_, focus_row_, dy);
	if (target >= 0) {
		focus_row_ = target;
	} else if (dy > 0) {
		focus_row_ = -1; // down from the last row: nav bar
	}
}

void HomeScreen::update(double dt, uint32_t pressed) {
	Stremio &s = stremio();
	if (s.library_rev != seen_library_rev_ || s.board_rev != seen_board_rev_ || s.notifications_rev != seen_notifications_rev_) {
		rebuild_rows();
	}
	if (context_menu_open_) {
		if (pressed & (platform::BUTTON_BACK | platform::BUTTON_OPTIONS)) {
			context_menu_open_ = false;
			return;
		}
		if (pressed & platform::BUTTON_UP) {
			context_menu_selected_ = std::max(0, context_menu_selected_ - 1);
		}
		if (pressed & platform::BUTTON_DOWN) {
			context_menu_selected_ = std::min((int)context_options_.size() - 1, context_menu_selected_ + 1);
		}
		if (pressed & platform::BUTTON_ACCEPT) {
			const int action_id = context_options_[context_menu_selected_].id;
			context_menu_open_ = false;
			if (action_id == 1) { // Delete from currently watched
				s.dismiss_from_continue_watching(context_card_.meta_id);
				bar_.show_toast("Removed \"" + context_card_.title + "\" from Continue watching");
				rebuild_rows();
			} else if (action_id == 2) { // Add / Remove library
				if (s.in_library(context_card_.meta_id)) {
					s.remove_from_library(context_card_.meta_id);
					bar_.show_toast("Removed from Library");
				} else {
					MetaItem meta;
					meta.id = context_card_.meta_id;
					meta.name = context_card_.title;
					meta.type = context_card_.type;
					meta.poster = context_card_.poster;
					s.add_to_library(meta);
					bar_.show_toast("Added to Library");
				}
			} else if (action_id == 3) { // Play / View Details
				open_details(context_card_, fonts_, navigator_);
			}
			return;
		}
		return;
	}

	if (bar_.handle_shoulders(pressed, fonts_, navigator_)) return;
	if (focus_row_ < 0 && bar_.handle_focused(pressed, fonts_, navigator_)) return;
	if (focus_row_ >= 0 && (pressed & platform::BUTTON_LEFT)) move(-1, 0);
	if (focus_row_ >= 0 && (pressed & platform::BUTTON_RIGHT)) move(1, 0);
	if (pressed & platform::BUTTON_UP) move(0, -1);
	if (pressed & platform::BUTTON_DOWN) move(0, 1);
	if (pressed & platform::DIRECTIONS) user_moved_ = true;
	if ((pressed & platform::BUTTON_ACCEPT) && focus_row_ >= 0) {
		open_details(rows_[focus_row_].cards[rows_[focus_row_].selected], fonts_, navigator_);
	}
	if (pressed & platform::BUTTON_TRIANGLE) {
		tabs::open(tabs::SEARCH, fonts_, navigator_);
		return;
	}
	if (focus_row_ >= 0 && focus_row_ < (int)rows_.size() && !rows_[focus_row_].cards.empty()) {
		const bool is_cw = (rows_[focus_row_].title == "Continue watching");
		const Card &card = rows_[focus_row_].cards[rows_[focus_row_].selected];
		if (pressed & platform::BUTTON_OPTIONS) {
			open_context_menu(card, is_cw);
			return;
		}
	}

	const CardRowLayout l = row_layout();
	for (int r = 0; r < (int)rows_.size(); r++) update_row(rows_[r], r == focus_row_, l, VIEW_W, dt);
	bar_.update(dt, focus_row_ < 0);
	if (focus_row_ >= 0) {
		content_scroll_ = ui::follow(content_scroll_, row_top(focus_row_), row_height(rows_[focus_row_], l),
				tabs::CONTENT_BOTTOM - rows_top(), dt);
	}

#if defined(STREMIO_DEV)
	fps_frames_++;
	fps_time_ += dt;
	if (fps_time_ >= 1.0) {
		fps_ = fps_frames_;
		fps_frames_ = 0;
		fps_time_ = 0;
	}
#endif
}

void HomeScreen::open_context_menu(const Card &card, bool is_cw) {
	context_card_ = card;
	context_options_.clear();
	context_menu_selected_ = 0;
	if (is_cw) {
		context_options_.push_back({ "Delete from currently watched", 1 });
	}
	const bool in_lib = stremio().in_library(card.meta_id);
	context_options_.push_back({ in_lib ? "Remove from Library" : "Add to Library", 2 });
	context_options_.push_back({ "Play / View Details", 3 });
	context_options_.push_back({ "Cancel", 4 });
	context_menu_open_ = true;
}

void HomeScreen::draw_context_menu(Renderer &r) {
	if (!context_menu_open_) return;
	// Dark semi-transparent modal backdrop
	r.rect(0, 0, W, H, rgb(0x000000, 0.70f));

	const float menu_w = S(580);
	const float item_h = S(56), item_gap = S(12);
	const float menu_h = S(130) + (float)context_options_.size() * (item_h + item_gap) + S(20);
	const float mx = (W - menu_w) / 2, my = (H - menu_h) / 2;
	const float R = theme::RADIUS_BOX;

	// Glowing outer border and card body
	r.rounded_rect(mx - S(2), my - S(2), menu_w + S(4), menu_h + S(4), R + S(2), theme::PRIMARY.with_alpha(0.5f));
	r.rounded_rect(mx, my, menu_w, menu_h, R, theme::BASE_200);

	// Badge and Title
	ui::badge(r, fonts_.small, "OPTIONS", mx + S(28), my + S(24), theme::PRIMARY, theme::PRIMARY_CONTENT);
	std::string title_line = fonts_.card_title.ellipsize(context_card_.title, menu_w - S(160));
	r.text(fonts_.card_title, title_line, mx + S(28), my + S(78), theme::BASE_CONTENT);

	const float items_top = my + S(110);
	for (int i = 0; i < (int)context_options_.size(); i++) {
		const bool selected = (i == context_menu_selected_);
		const float iy = items_top + i * (item_h + item_gap);
		const float row_w = menu_w - S(56);
		const float row_x = mx + S(28);
		const float row_r = theme::RADIUS_FIELD;

		if (selected) {
			r.rounded_rect(row_x - S(2), iy - S(2), row_w + S(4), item_h + S(4), row_r + S(2), theme::PRIMARY);
			r.rounded_rect(row_x, iy, row_w, item_h, row_r, theme::BASE_300);
		} else {
			r.rounded_rect(row_x, iy, row_w, item_h, row_r, mix(theme::BASE_200, theme::BASE_100, 0.4f));
		}

		Color text_color = selected ? theme::PRIMARY_CONTENT : theme::BASE_CONTENT;
		if (context_options_[i].id == 1 && selected) text_color = theme::ERROR;
		r.text(fonts_.body, context_options_[i].label, row_x + S(20), iy + item_h / 2 + S(6), text_color);
	}

	ui::hints(r, fonts_.small, { { "\xC3\x97", "Select" }, { "\xE2\x97\x8B", "Back" } }, mx + S(28), my + menu_h - S(16));
}

// --- drawing --------------------------------------------------------------------------

void HomeScreen::draw(Renderer &r) {
	const CardRowLayout l = row_layout();
	const float top = rows_top();
	float y = top - content_scroll_;
	for (const CardRow &row : rows_) {
		const float h = row_height(row, l);
		if (y < tabs::CONTENT_BOTTOM && y + h > 0) draw_row(r, fonts_, row, l, MARGIN_X, y, VIEW_W);
		y += h;
	}

	// Header band: title, and the account (avatar + email) on the right.
	r.rect(0, 0, W, top - 6, theme::BASE_100);
	r.text(fonts_.page_header, "Stremio", MARGIN_X, S(74), theme::BASE_CONTENT);
	if (focus_row_ >= 0 && focus_row_ < (int)rows_.size() && !rows_[focus_row_].cards.empty()) {
		ui::hints(r, fonts_.small, { { "\xE2\x96\xB3", "Search" }, { "OPTIONS", "Menu" }, { "\xC3\x97", "Watch" } },
				MARGIN_X + fonts_.page_header.measure("Stremio") + S(36), S(74));
	} else {
		ui::hints(r, fonts_.small, { { "\xE2\x96\xB3", "Search" } },
				MARGIN_X + fonts_.page_header.measure("Stremio") + S(36), S(74));
	}
	const std::string email = stremio().user_email();
	const float email_x = W - MARGIN_X - fonts_.body.measure(email);
	r.text(fonts_.body, email, email_x, S(70), theme::BASE_CONTENT_MUTED);
	const float avatar = S(44);
	ui::avatar(r, fonts_.card_title, email, email_x - S(16) - avatar, S(64) - avatar / 2, avatar);

	bar_.draw(r, fonts_);
	if (context_menu_open_) {
		draw_context_menu(r);
	}

#if defined(STREMIO_DEV)
	char stats[64];
	snprintf(stats, sizeof(stats), "%d fps \xC2\xB7 %d draw calls", fps_, r.draw_calls());
	r.text(fonts_.small, stats, 24, 1080 - 20, theme::BASE_CONTENT_MUTED);
#endif
}
