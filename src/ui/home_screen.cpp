#include "home_screen.h"

#include "../core/stremio.h"
#include "../platform/platform.h"
#include "components.h"
#include "theme.h"

#include <algorithm>

namespace {

using theme::S;

const float W = 1920;
const float MARGIN_X = 120;
const float ROWS_TOP = S(150); // first row's title top
const float VIEW_W = W - 2 * MARGIN_X;
const CardRowLayout LAYOUT = { S(520), S(292), S(28), S(62), S(400), S(110) };

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
	float y = 0;
	for (int i = 0; i < index; i++) y += row_height(rows_[i], LAYOUT);
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

	for (int r = 0; r < (int)rows_.size(); r++) update_row(rows_[r], r == focus_row_, LAYOUT, VIEW_W, dt);
	bar_.update(dt, focus_row_ < 0);
	if (focus_row_ >= 0) {
		content_scroll_ = ui::follow(content_scroll_, row_top(focus_row_), row_height(rows_[focus_row_], LAYOUT),
				tabs::CONTENT_BOTTOM - ROWS_TOP, dt);
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

// --- drawing --------------------------------------------------------------------------

void HomeScreen::draw(Renderer &r) {
	float y = ROWS_TOP - content_scroll_;
	for (const CardRow &row : rows_) {
		const float h = row_height(row, LAYOUT);
		if (y < tabs::CONTENT_BOTTOM && y + h > 0) draw_row(r, fonts_, row, LAYOUT, MARGIN_X, y, VIEW_W);
		y += h;
	}

	// Header band: title, and the account (avatar + email) on the right.
	r.rect(0, 0, W, ROWS_TOP - 8, theme::BASE_100);
	r.text(fonts_.page_header, "Stremio", MARGIN_X, S(96), theme::BASE_CONTENT);
	const std::string email = stremio().user_email();
	const float email_x = W - MARGIN_X - fonts_.body.measure(email);
	r.text(fonts_.body, email, email_x, S(90), theme::BASE_CONTENT_MUTED);
	const float avatar = S(52);
	ui::avatar(r, fonts_.card_title, email, email_x - S(16) - avatar, S(82) - avatar / 2, avatar);

	bar_.draw(r, fonts_);

#if defined(STREMIO_DEV)
	char stats[64];
	snprintf(stats, sizeof(stats), "%d fps \xC2\xB7 %d draw calls", fps_, r.draw_calls());
	r.text(fonts_.small, stats, 24, 1080 - 20, theme::BASE_CONTENT_MUTED);
#endif
}
