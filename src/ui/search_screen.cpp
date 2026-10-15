#include "search_screen.h"

#include "../core/stremio.h"
#include "../platform/platform.h"
#include "components.h"
#include "theme.h"

#include <algorithm>
#include <cmath>

namespace {

using theme::S;

const float W = 1920;
const float MARGIN_X = 120;
const float TOP = S(150);
const float KEY = S(64), KEY_GAP = S(8);
const int KEY_COLS = 6;
const float KB_W = KEY_COLS * KEY + (KEY_COLS - 1) * KEY_GAP;
const float QUERY_H = S(72);
const float KEYS_TOP = TOP + QUERY_H + S(24);
const float RESULTS_X = MARGIN_X + KB_W + S(64);
const float VIEW_W = W - MARGIN_X - RESULTS_X;
const CardRowLayout LAYOUT = { S(330), S(186), S(22), S(56), S(290), S(100) };
const float SEARCH_DELAY = 0.6f;

} // namespace

SearchScreen::SearchScreen(Fonts &fonts, Navigator &nav) : fonts_(fonts), navigator_(nav) {
	build_keys();
	// Come back to the last search (e.g. after opening a result).
	query_ = stremio().search_query();
	rebuild_rows();
}

void SearchScreen::build_keys() {
	const std::string chars = "abcdefghijklmnopqrstuvwxyz0123456789";
	for (int i = 0; i < (int)chars.size(); i++) {
		const std::string c(1, chars[i]);
		keys_.push_back({ c, c, i % KEY_COLS, i / KEY_COLS, 1 });
	}
	const int last = (int)chars.size() / KEY_COLS;
	keys_.push_back({ "Space", " ", 0, last, 2 });
	keys_.push_back({ "Delete", "", 2, last, 2 });
	keys_.push_back({ "Clear", "", 4, last, 2 });
}

int SearchScreen::key_at(int col, int row) const {
	for (int i = 0; i < (int)keys_.size(); i++) {
		const Key &k = keys_[i];
		if (k.row == row && col >= k.col && col < k.col + k.span) return i;
	}
	return -1;
}

void SearchScreen::press_key(const Key &k) {
	if (k.label == "Delete") {
		if (!query_.empty()) query_.pop_back();
	} else if (k.label == "Clear") {
		query_.clear();
	} else if (query_.size() < 60) {
		query_ += k.text;
	}
	cursor_time_ = 0;
	search_delay_ = SEARCH_DELAY;
}

void SearchScreen::move_keyboard(int dx, int dy) {
	const Key &k = keys_[key_];
	if (dx != 0) {
		const int col = dx > 0 ? k.col + k.span : k.col - 1;
		if (col >= KEY_COLS) { // off the right edge: into the results
			const int first = next_row_with_cards(rows_, -1, 1);
			if (first >= 0) {
				area_ = RESULTS;
				if (rows_[row_].cards.empty()) row_ = first;
			}
			return;
		}
		const int i = key_at(col, k.row);
		if (i >= 0) key_ = i;
		return;
	}
	const int rows = keys_.back().row + 1;
	const int row = k.row + dy;
	if (row >= rows) {
		area_ = BAR;
		return;
	}
	if (row < 0) return;
	const int i = key_at(k.col, row);
	if (i >= 0) key_ = i;
}

void SearchScreen::move_results(int dx, int dy) {
	CardRow &row = rows_[row_];
	if (dx != 0) {
		if (dx < 0 && row.selected == 0) {
			area_ = KEYBOARD; // off the left edge: back to the keyboard
			return;
		}
		row.selected = std::clamp(row.selected + dx, 0, (int)row.cards.size() - 1);
		return;
	}
	const int target = next_row_with_cards(rows_, row_, dy);
	if (target >= 0) {
		row_ = target;
	} else if (dy > 0) {
		area_ = BAR;
	}
}

void SearchScreen::rebuild_rows() {
	Stremio &s = stremio();
	seen_search_rev_ = s.search_rev;
	std::vector<CardRow> rows;
	for (const BoardRow &b : s.search_results()) {
		CardRow row;
		if (CardRow::from_board(b, "Searching\xE2\x80\xA6", "Couldn't search this catalog: ", row)) rows.push_back(row);
	}
	carry_over(rows, rows_); // keep the position in rows that are still there
	rows_.swap(rows);
	// Until the user is in the results, focus (and scrolling) stays on the
	// first row with cards, whichever row's results arrive first.
	const int first = next_row_with_cards(rows_, -1, 1);
	if (area_ != RESULTS || row_ >= (int)rows_.size() || rows_[row_].cards.empty()) row_ = std::max(0, first);
	if (area_ == RESULTS && first < 0) area_ = KEYBOARD;
}

void SearchScreen::update(double dt, uint32_t pressed) {
	if (stremio().search_rev != seen_search_rev_) rebuild_rows();
	if (bar_.handle_shoulders(pressed, fonts_, navigator_)) return;

	if (area_ == BAR) {
		if (bar_.handle_focused(pressed, fonts_, navigator_)) return;
		if (pressed & platform::BUTTON_UP) area_ = KEYBOARD;
	} else if (area_ == KEYBOARD) {
		if (pressed & platform::BUTTON_LEFT) move_keyboard(-1, 0);
		if (pressed & platform::BUTTON_RIGHT) move_keyboard(1, 0);
		if (pressed & platform::BUTTON_UP) move_keyboard(0, -1);
		if (pressed & platform::BUTTON_DOWN) move_keyboard(0, 1);
		if (pressed & platform::BUTTON_ACCEPT) press_key(keys_[key_]);
		// ○ deletes while typing, like a backspace; on an empty query it does nothing.
		if ((pressed & platform::BUTTON_BACK) && !query_.empty()) {
			query_.pop_back();
			search_delay_ = SEARCH_DELAY;
		}
	} else {
		if (pressed & platform::BUTTON_LEFT) move_results(-1, 0);
		if (pressed & platform::BUTTON_RIGHT) move_results(1, 0);
		if (pressed & platform::BUTTON_UP) move_results(0, -1);
		if (pressed & platform::BUTTON_DOWN) move_results(0, 1);
		if (pressed & platform::BUTTON_BACK) area_ = KEYBOARD;
		if ((pressed & platform::BUTTON_ACCEPT) && area_ == RESULTS) {
			const CardRow &row = rows_[row_];
			open_details(row.cards[row.selected], fonts_, navigator_);
		}
	}

	if (search_delay_ > 0) {
		search_delay_ -= (float)dt;
		if (search_delay_ <= 0 && query_ != stremio().search_query()) stremio().search(query_);
	}
	cursor_time_ += (float)dt;

	for (int i = 0; i < (int)keys_.size(); i++) {
		keys_[i].focus = ui::approach(keys_[i].focus, (area_ == KEYBOARD && i == key_) ? 1.0f : 0.0f, dt);
	}
	for (int r = 0; r < (int)rows_.size(); r++) update_row(rows_[r], area_ == RESULTS && r == row_, LAYOUT, VIEW_W, dt);
	// Keep the focused row in view.
	float top = 0;
	for (int i = 0; i < row_ && i < (int)rows_.size(); i++) top += row_height(rows_[i], LAYOUT);
	content_scroll_ = ui::follow(content_scroll_, top, LAYOUT.height, tabs::CONTENT_BOTTOM - TOP, dt);
	bar_.update(dt, area_ == BAR);
}

void SearchScreen::draw(Renderer &r) {
	// Results (drawn first: the header and keyboard bands cover the overflow).
	if (rows_.empty()) {
		const bool searching = search_delay_ > 0 || query_ != stremio().search_query();
		const std::string hint = query_.empty() ? "Search for movies and series with the keyboard"
				: searching ? "" : "No results for \"" + query_ + "\"";
		r.text(fonts_.body, fonts_.body.ellipsize(hint, VIEW_W), RESULTS_X, TOP + S(40), theme::BASE_CONTENT_MUTED);
	}
	float y = TOP - content_scroll_;
	for (const CardRow &row : rows_) {
		const float h = row_height(row, LAYOUT);
		if (y < tabs::CONTENT_BOTTOM && y + h > 0) draw_row(r, fonts_, row, LAYOUT, RESULTS_X, y, VIEW_W);
		y += h;
	}

	// Left band: query + keyboard (covers cards scrolled to the left).
	r.rect(0, TOP - S(10), RESULTS_X - S(24), tabs::CONTENT_BOTTOM - TOP + S(10), theme::BASE_100);
	r.rect(0, 0, W, TOP - S(10), theme::BASE_100);
	r.text(fonts_.page_header, "Search", MARGIN_X, S(96), theme::BASE_CONTENT);

	// The query field (daisyUI input): ringed while typing.
	const float ring = area_ == KEYBOARD ? 1.0f : 0.0f;
	ui::begin_focus(r, MARGIN_X, TOP, KB_W, QUERY_H, theme::RADIUS_FIELD, ring, 1.0f, theme::BASE_300);
	r.rounded_rect(MARGIN_X, TOP, KB_W, QUERY_H, theme::RADIUS_FIELD, theme::BASE_200);
	r.pop();
	const float qx = MARGIN_X + S(20), qy = TOP + QUERY_H / 2 + S(10);
	if (query_.empty()) {
		r.text(fonts_.card_title, "Type a title", qx, qy, theme::BASE_CONTENT_MUTED);
	} else {
		const std::string shown = query_.size() > 26 ? "\xE2\x80\xA6" + query_.substr(query_.size() - 25) : query_;
		r.text(fonts_.card_title, shown, qx, qy, theme::BASE_CONTENT);
		if (std::fmod(cursor_time_, 1.0f) < 0.6f) {
			r.rect(qx + fonts_.card_title.measure(shown) + S(3), TOP + S(18), S(3), QUERY_H - S(36), theme::PRIMARY);
		}
	}

	for (const Key &k : keys_) {
		const float x = MARGIN_X + k.col * (KEY + KEY_GAP);
		const float ky = KEYS_TOP + k.row * (KEY + KEY_GAP);
		const float w = k.span * KEY + (k.span - 1) * KEY_GAP;
		const float f = k.focus;
		ui::begin_focus(r, x, ky, w, KEY, theme::RADIUS_FIELD, f);
		r.rounded_rect(x, ky, w, KEY, theme::RADIUS_FIELD, mix(theme::BASE_200, theme::PRIMARY, 0.35f * f));
		Font &font = k.span > 1 ? fonts_.body : fonts_.card_title;
		r.text(font, k.label, x + (w - font.measure(k.label)) / 2, ky + KEY / 2 + font.size() * 0.36f, theme::BASE_CONTENT);
		r.pop(); // keys stay bright: the whole keyboard is one control
	}
	const float hint_y = KEYS_TOP + (keys_.back().row + 1) * (KEY + KEY_GAP) + S(30);
	ui::hints(r, fonts_.small, { { "\xE2\x97\x8B", "Delete" }, { "\xE2\x96\xB6", "Results" } }, MARGIN_X, hint_y);

	bar_.draw(r, fonts_);
}
