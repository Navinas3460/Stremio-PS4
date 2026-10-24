// Title cards (landscape artwork, title, subtitle, progress, badge) and the
// rows of them used by Home and Search, plus opening a card's details.
#pragma once

#include "../core/models.h"
#include "screen.h"
#include "theme.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

struct BoardRow;

struct Card {
	std::string title, subtitle, badge;
	float progress = 0;              // 0..1, 0 = no bar
	std::vector<std::string> images; // artwork URLs, best first
	std::string meta_id, type;       // what the card opens
	std::string poster, release_info;
	float focus = 0;                 // animated 0..1

	static Card from_meta(const MetaItem &m);
};

// Draws `c` at (x, y), w x h, scaled up and ringed by its focus.
void draw_card(Renderer &r, Fonts &fonts, const Card &c, float x, float y, float w, float h);

// Poster card (Library): portrait artwork with the progress bar and badge on
// it, and the title and subtitle in poster_caption_h() below it (over the
// artwork they'd clash with the title printed on most posters).
inline float poster_caption_h() { return theme::S(84); }
void draw_poster(Renderer &r, Fonts &fonts, const Card &c, float x, float y, float w, float h);

// The poster grid of Library and Discover, between the screen margins: 8
// columns at 100% UI scale, fewer and bigger posters at larger scales.
namespace poster_grid {
inline int columns() { return std::clamp((int)std::lround(8 * 100.0 / theme::ui_scale_percent()), 5, 10); }
inline float gap() { return theme::S(28); }
inline float row_gap() { return theme::S(20); }
inline float card_w() { return (1920 - 2 * 120 - (columns() - 1) * gap()) / columns(); }
inline float card_h() { return card_w() * 1.5f; } // posters are 2:3
inline float cell_h() { return card_h() + poster_caption_h(); }
inline float pitch() { return cell_h() + row_gap(); }
} // namespace poster_grid

// A content type as a filter label: "movie" -> "Movies", "anime" -> "Anime".
std::string type_label(const std::string &type);

// Pushes the details screen for `c`.
void open_details(const Card &c, Fonts &fonts, Navigator &nav);

// A titled, horizontally scrolling row of cards; without cards it shows a
// status line instead (loading, error).
struct CardRow {
	std::string title, status;
	bool status_is_error = false;
	std::vector<Card> cards;
	int selected = 0;
	float scroll = 0; // animated x offset

	// Row for an add-on catalog. False if there's nothing to show.
	static bool from_board(const BoardRow &b, const std::string &loading, const std::string &error_prefix, CardRow &out);
};

// Geometry of a row of cards.
struct CardRowLayout {
	float card_w, card_h, gap;
	float cards_offset; // title top -> cards top
	float height;       // with cards
	float status_height; // without cards
};

// Index of the next row with cards after `from` in direction `step`, or -1.
int next_row_with_cards(const std::vector<CardRow> &rows, int from, int step);
// Keeps the selection, scroll and focus animation of rows (by title) that
// existed before a rebuild.
void carry_over(std::vector<CardRow> &rows, const std::vector<CardRow> &old);
float row_height(const CardRow &row, const CardRowLayout &l);
// Animates card focus (`focused` = this row has the focus) and the scroll
// that keeps the selected card in a `view` px wide window.
void update_row(CardRow &row, bool focused, const CardRowLayout &l, float view, double dt);
// Title and cards (or the status line) at (x, y), `view` px wide; the focused
// card is drawn last so it overlaps its neighbours.
void draw_row(Renderer &r, Fonts &fonts, const CardRow &row, const CardRowLayout &l, float x, float y, float view);
