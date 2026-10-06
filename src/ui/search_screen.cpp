#include "search_screen.h"

#include "../core/stremio.h"
#include "../platform/platform.h"
#include "theme.h"

#include <algorithm>
#include <cmath>

namespace {

using theme::S;
using tabs::approach;

const float W = 1920;
const float MARGIN_X = 120;
const float TOP = S(150);
const float KEY = S(64), KEY_GAP = S(8);
const int KEY_COLS = 6;
const float KB_W = KEY_COLS * KEY + (KEY_COLS - 1) * KEY_GAP;
const float QUERY_H = S(72);
const float KEYS_TOP = TOP + QUERY_H + S(24);
const float RESULTS_X = MARGIN_X + KB_W + S(64);
const float CARD_W = S(330), CARD_H = S(186), CARD_GAP = S(22);
const float ROW_HEIGHT = S(290), STATUS_ROW_HEIGHT = S(100), ROW_CARDS_OFFSET = S(56);
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
			if (next_row_with_cards(-1, 1) >= 0) {
				area_ = RESULTS;
				if (rows_[row_].cards.empty()) row_ = next_row_with_cards(-1, 1);
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

int SearchScreen::next_row_with_cards(int from, int step) const {
	for (int i = from + step; i >= 0 && i < (int)rows_.size(); i += step) {
		if (!rows_[i].cards.empty()) return i;
	}
	return -1;
}

void SearchScreen::move_results(int dx, int dy) {
	Row &row = rows_[row_];
	if (dx != 0) {
		if (dx < 0 && row.selected == 0) {
			area_ = KEYBOARD; // off the left edge: back to the keyboard
			return;
		}
		row.selected = std::clamp(row.selected + dx, 0, (int)row.cards.size() - 1);
		return;
	}
	const int target = next_row_with_cards(row_, dy);
	if (target >= 0) {
		row_ = target;
	} else if (dy > 0) {
		area_ = BAR;
	}
}

void SearchScreen::rebuild_rows() {
	Stremio &s = stremio();
	seen_search_rev_ = s.search_rev;
	std::vector<Row> rows;
	for (const BoardRow &b : s.search_results()) {
		Row row;
		row.title = b.title;
		if (b.loading) {
			row.status = "Searching\xE2\x80\xA6";
		} else if (!b.error.empty()) {
			row.status = "Couldn't search this catalog: " + b.error;
			row.status_is_error = true;
		} else if (b.items.empty()) {
			continue;
		}
		for (const MetaItem &m : b.items) row.cards.push_back(Card::from_meta(m));
		// Keep the position in rows that are still there.
		for (const Row &old : rows_) {
			if (old.title == row.title) {
				row.selected = std::min(old.selected, std::max(0, (int)row.cards.size() - 1));
				row.scroll = old.scroll;
			}
		}
		rows.push_back(row);
	}
	rows_.swap(rows);
	// Until the user is in the results, focus (and scrolling) stays on the
	// first row with cards, whichever row's results arrive first.
	if (area_ != RESULTS || row_ >= (int)rows_.size() || rows_[row_].cards.empty()) {
		row_ = std::max(0, next_row_with_cards(-1, 1));
	}
	if (area_ == RESULTS && next_row_with_cards(-1, 1) < 0) area_ = KEYBOARD;
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
			const Row &row = rows_[row_];
			open_details(row.cards[row.selected], fonts_, navigator_);
		}
	}

	if (search_delay_ > 0) {
		search_delay_ -= (float)dt;
		if (search_delay_ <= 0 && query_ != stremio().search_query()) stremio().search(query_);
	}
	cursor_time_ += (float)dt;

	for (int i = 0; i < (int)keys_.size(); i++) {
		keys_[i].focus = approach(keys_[i].focus, (area_ == KEYBOARD && i == key_) ? 1.0f : 0.0f, dt);
	}
	const float visible = W - MARGIN_X - RESULTS_X;
	for (int r = 0; r < (int)rows_.size(); r++) {
		Row &row = rows_[r];
		for (int i = 0; i < (int)row.cards.size(); i++) {
			const bool focused = area_ == RESULTS && r == row_ && i == row.selected;
			row.cards[i].focus = approach(row.cards[i].focus, focused ? 1.0f : 0.0f, dt);
		}
		const float left = row.selected * (CARD_W + CARD_GAP);
		float target = row.scroll;
		if (left - target < 0) target = left;
		if (left + CARD_W - target > visible) target = left + CARD_W - visible;
		row.scroll = approach(row.scroll, target, dt);
	}
	// Keep the focused row in view.
	float top = 0;
	for (int i = 0; i < row_ && i < (int)rows_.size(); i++) top += rows_[i].cards.empty() ? STATUS_ROW_HEIGHT : ROW_HEIGHT;
	const float band = tabs::CONTENT_BOTTOM - TOP;
	float target = content_scroll_;
	if (top - target < 0) target = top;
	if (top + ROW_HEIGHT - target > band) target = top + ROW_HEIGHT - band;
	content_scroll_ = approach(content_scroll_, target, dt);
	bar_.update(dt, area_ == BAR);
}

void SearchScreen::draw(Renderer &r) {
	const Color bg = theme::BRAND_TERTIARY;

	// Results (drawn first: the header and keyboard bands cover the overflow).
	float y = TOP - content_scroll_;
	const float max_w = W - MARGIN_X - RESULTS_X;
	if (rows_.empty()) {
		const std::string hint = query_.empty() ? "Search for movies and series with the keyboard"
				: (search_delay_ > 0 || query_ != stremio().search_query()) ? "" : "No results for \"" + query_ + "\"";
		r.text(fonts_.body, fonts_.body.ellipsize(hint, max_w), RESULTS_X, TOP + S(40), theme::TEXT_MUTED);
	}
	for (int ri = 0; ri < (int)rows_.size(); ri++) {
		Row &row = rows_[ri];
		const float h = row.cards.empty() ? STATUS_ROW_HEIGHT : ROW_HEIGHT;
		if (y < tabs::CONTENT_BOTTOM && y + h > 0) {
			r.text(fonts_.section, fonts_.section.ellipsize(row.title, max_w), RESULTS_X, y + S(34), theme::SECTION_TITLE);
			if (row.cards.empty()) {
				r.text(fonts_.body, fonts_.body.ellipsize(row.status, max_w), RESULTS_X, y + S(80),
						row.status_is_error ? rgb(0xFF8A80) : theme::TEXT_MUTED);
			} else {
				for (int pass = 0; pass < 2; pass++) { // focused card last (on top)
					for (int i = 0; i < (int)row.cards.size(); i++) {
						if ((pass == 0) == (row.cards[i].focus > 0.5f)) continue;
						const float x = RESULTS_X + i * (CARD_W + CARD_GAP) - row.scroll;
						if (x > W + 40 || x + CARD_W < RESULTS_X - 40) continue;
						draw_card(r, fonts_, row.cards[i], x, y + ROW_CARDS_OFFSET, CARD_W, CARD_H);
					}
				}
			}
		}
		y += h;
	}

	// Left band: query + keyboard (covers cards scrolled to the left).
	r.rect(0, TOP - S(10), RESULTS_X - S(24), tabs::CONTENT_BOTTOM - TOP + S(10), bg);
	r.rect(0, 0, W, TOP - S(10), bg);
	r.text(fonts_.page_header, "Search", MARGIN_X, S(96), theme::TEXT);

	r.rounded_rect(MARGIN_X, TOP, KB_W, QUERY_H, theme::RADIUS_CARD, theme::SURFACE);
	const float qx = MARGIN_X + S(20), qy = TOP + QUERY_H / 2 + S(10);
	if (query_.empty()) {
		r.text(fonts_.card_title, "Type a title", qx, qy, theme::TEXT_MUTED);
	} else {
		const std::string shown = query_.size() > 26 ? "\xE2\x80\xA6" + query_.substr(query_.size() - 25) : query_;
		r.text(fonts_.card_title, shown, qx, qy, theme::TEXT);
		if (std::fmod(cursor_time_, 1.0f) < 0.6f) {
			r.rect(qx + fonts_.card_title.measure(shown) + S(3), TOP + S(18), S(3), QUERY_H - S(36), theme::BRAND_PRIMARY);
		}
	}

	for (const Key &k : keys_) {
		const float x = MARGIN_X + k.col * (KEY + KEY_GAP);
		const float ky = KEYS_TOP + k.row * (KEY + KEY_GAP);
		const float w = k.span * KEY + (k.span - 1) * KEY_GAP;
		const float f = k.focus;
		r.push(x + w / 2, ky + KEY / 2, 1.0f + (theme::FOCUS_SCALE - 1.0f) * f, 1.0f);
		if (f > 0.02f) {
			r.rounded_rect(x - S(3), ky - S(3), w + S(6), KEY + S(6), theme::RADIUS_CARD + S(3), theme::BRAND_PRIMARY.with_alpha(f));
		}
		r.rounded_rect(x, ky, w, KEY, theme::RADIUS_CARD, f > 0.5f ? mix(theme::SURFACE, theme::BRAND_PRIMARY, 0.35f) : theme::SURFACE);
		Font &font = k.span > 1 ? fonts_.body : fonts_.card_title;
		const float lw = font.measure(k.label);
		r.text(font, k.label, x + (w - lw) / 2, ky + KEY / 2 + S(9), theme::TEXT);
		r.pop();
	}
	const float hint_y = KEYS_TOP + (keys_.back().row + 1) * (KEY + KEY_GAP) + S(20);
	r.text(fonts_.body, "\xE2\x97\x8B Delete  \xC2\xB7  \xE2\x96\xB6 Results", MARGIN_X, hint_y, theme::TEXT_MUTED);

	bar_.draw(r, fonts_);
}
