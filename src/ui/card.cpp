#include "card.h"

#include "../core/stremio.h"
#include "../net/images.h"
#include "components.h"
#include "details_screen.h"

#include <algorithm>
#include <map>

using theme::S;

Card Card::from_meta(const MetaItem &m) {
	Card c;
	c.title = m.name;
	c.subtitle = m.release_info;
	if (!m.imdb_rating.empty()) {
		c.subtitle += (c.subtitle.empty() ? "" : " \xC2\xB7 ") + std::string("IMDb ") + m.imdb_rating;
	}
	c.images = m.landscape_images();
	c.meta_id = m.id;
	c.type = m.type;
	c.poster = m.poster;
	c.release_info = m.release_info;
	return c;
}

namespace {
// Artwork (or the title's initial) in a rounded w x h box.
void draw_artwork(Renderer &r, Fonts &fonts, const Card &c, float x, float y, float w, float h) {
	r.rounded_rect(x, y, w, h, theme::RADIUS_FIELD, theme::BASE_200);
	const Texture &art = images::first(c.images);
	if (art.valid()) {
		r.image_cover(art, x, y, w, h);
	} else {
		size_t first = 0; // whole UTF-8 character (e.g. "Ж", "千")
		if (!c.title.empty()) next_codepoint(c.title, first);
		const std::string initial = c.title.substr(0, first);
		const float iw = fonts.page_header.measure(initial);
		r.text(fonts.page_header, initial, x + (w - iw) / 2, y + h / 2 + S(16), theme::BASE_CONTENT_MUTED);
	}
}

// The artwork is square-cornered: cap its corners in the colour behind it
// (the focus ring while it shows).
void round_artwork(Renderer &r, const Card &c, float x, float y, float w, float h) {
	r.round_corners(x, y, w, h, theme::RADIUS_FIELD,
			c.focus > 0.02f ? mix(theme::BASE_100, theme::PRIMARY, c.focus) : theme::BASE_100);
}

void draw_badge(Renderer &r, Fonts &fonts, const Card &c, float x, float y, float w, float h = 999.0f) {
	if (c.badge.empty()) return;
	Font &f = h <= 190.0f ? fonts.small : fonts.body;
	const float pad = h <= 190.0f ? (h * 0.08f) : S(16);
	const float bw = ui::badge_width(f, c.badge);
	ui::badge(r, f, c.badge, x + w - pad - bw, y + pad, theme::ACCENT, theme::ACCENT_CONTENT);
}
} // namespace

void draw_card(Renderer &r, Fonts &fonts, const Card &c, float x, float y, float w, float h) {
	const float R = theme::RADIUS_FIELD;
	const bool compact = h <= 190.0f;
	const float cs = h / 122.0f; // proportional card scale for compact Home rows
	ui::begin_focus(r, x, y, w, h, R, c.focus);
	draw_artwork(r, fonts, c, x, y, w, h);
	r.vertical_fade(x, y + h * (compact ? 0.25f : 0.35f), w, h * (compact ? 0.75f : 0.65f), theme::BASE_100, 0.0f, 0.95f);
	round_artwork(r, c, x, y, w, h);

	const float pad = compact ? (12.0f * cs) : S(24);
	float baseline = y + h - (compact ? (11.0f * cs) : S(22));
	if (c.progress > 0) {
		const float bar_h = compact ? std::max(3.0f, 4.0f * cs) : S(6);
		const float bar_y = y + h - (compact ? (9.0f * cs) : S(20)) - bar_h;
		ui::progress(r, x + pad, bar_y, w - 2 * pad, bar_h, c.progress);
		baseline = bar_y - (compact ? (8.0f * cs) : S(14));
	}
	Font &sub_font = compact ? fonts.small : fonts.body;
	Font &title_font = compact ? (cs < 0.85f ? fonts.small : fonts.body) : fonts.card_title;
	if (!c.subtitle.empty()) {
		r.text(sub_font, sub_font.ellipsize(c.subtitle, w - 2 * pad), x + pad, baseline, theme::BASE_CONTENT_MUTED);
		baseline -= compact ? std::max(15.0f, 19.0f * cs) : S(32);
	}
	r.text(title_font, title_font.ellipsize(c.title, w - 2 * pad), x + pad, baseline, theme::BASE_CONTENT);
	draw_badge(r, fonts, c, x, y, w, h);
	ui::end_focus(r, x, y, w, h, R, c.focus);
}

void draw_poster(Renderer &r, Fonts &fonts, const Card &c, float x, float y, float w, float h) {
	const float R = theme::RADIUS_FIELD;
	ui::begin_focus(r, x, y, w, h, R, c.focus);
	draw_artwork(r, fonts, c, x, y, w, h);
	if (c.progress > 0) {
		const float pad = S(14), bar_h = S(6);
		r.vertical_fade(x, y + h * 0.75f, w, h * 0.25f, theme::BASE_100, 0.0f, 0.85f);
		ui::progress(r, x + pad, y + h - pad - bar_h, w - 2 * pad, bar_h, c.progress);
	}
	round_artwork(r, c, x, y, w, h);
	draw_badge(r, fonts, c, x, y, w);
	ui::end_focus(r, x, y, w, h, R, c.focus);

	// Caption (not scaled; it brightens with the focus like the poster).
	const float alpha = theme::ALPHA_UNFOCUSED + (1.0f - theme::ALPHA_UNFOCUSED) * c.focus;
	r.text(fonts.card_title, fonts.card_title.ellipsize(c.title, w), x, y + h + S(46), theme::BASE_CONTENT.with_alpha(alpha));
	if (!c.subtitle.empty()) {
		r.text(fonts.body, fonts.body.ellipsize(c.subtitle, w), x, y + h + S(74),
				theme::BASE_CONTENT_MUTED.with_alpha(alpha));
	}
}

void open_details(const Card &c, Fonts &fonts, Navigator &nav) {
	DetailsScreen::Target t;
	t.meta_id = c.meta_id;
	t.type = c.type;
	t.name = c.title;
	t.poster = c.poster;
	t.release_info = c.release_info;
	t.images = c.images;
	nav.push(std::unique_ptr<Screen>(new DetailsScreen(fonts, nav, t)));
}

// --- rows -------------------------------------------------------------------------

bool CardRow::from_board(const BoardRow &b, const std::string &loading, const std::string &error_prefix, CardRow &out) {
	out = CardRow();
	out.title = b.title;
	if (b.loading) {
		out.status = loading;
	} else if (!b.error.empty()) {
		out.status = error_prefix + b.error;
		out.status_is_error = true;
	} else if (b.items.empty()) {
		return false;
	}
	for (const MetaItem &m : b.items) out.cards.push_back(Card::from_meta(m));
	return true;
}

int next_row_with_cards(const std::vector<CardRow> &rows, int from, int step) {
	for (int i = from + step; i >= 0 && i < (int)rows.size(); i += step) {
		if (!rows[i].cards.empty()) return i;
	}
	return -1;
}

void carry_over(std::vector<CardRow> &rows, const std::vector<CardRow> &old) {
	std::map<std::string, const CardRow *> by_title;
	for (const CardRow &r : old) by_title[r.title] = &r;
	for (CardRow &r : rows) {
		auto it = by_title.find(r.title);
		if (it == by_title.end()) continue;
		const CardRow &o = *it->second;
		r.scroll = o.scroll;
		r.selected = std::min(o.selected, std::max(0, (int)r.cards.size() - 1));
		for (size_t i = 0; i < r.cards.size() && i < o.cards.size(); i++) r.cards[i].focus = o.cards[i].focus;
	}
}

float row_height(const CardRow &row, const CardRowLayout &l) {
	return row.cards.empty() ? l.status_height : l.height;
}

void update_row(CardRow &row, bool focused, const CardRowLayout &l, float view, double dt) {
	for (int i = 0; i < (int)row.cards.size(); i++) {
		row.cards[i].focus = ui::approach(row.cards[i].focus, (focused && i == row.selected) ? 1.0f : 0.0f, dt);
	}
	row.scroll = ui::follow(row.scroll, row.selected * (l.card_w + l.gap), l.card_w, view, dt);
}

void draw_row(Renderer &r, Fonts &fonts, const CardRow &row, const CardRowLayout &l, float x, float y, float view) {
	const bool compact = l.cards_offset < l.card_h * 0.30f;
	Font &hdr_font = compact ? (l.card_h < 105.0f ? fonts.body : fonts.card_title) : fonts.section;
	const float hdr_y = compact ? (y + l.cards_offset - std::max(6.0f, l.cards_offset * 0.28f)) : (y + S(34));
	r.text(hdr_font, hdr_font.ellipsize(row.title, view), x, hdr_y, theme::BASE_CONTENT);
	if (row.cards.empty()) {
		r.text(fonts.body, fonts.body.ellipsize(row.status, view), x, y + (compact ? l.status_height * 0.68f : S(80)),
				row.status_is_error ? theme::ERROR : theme::BASE_CONTENT_MUTED);
		return;
	}
	const float screen_w = 1920, margin = S(40); // the focused card grows past its bounds
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < (int)row.cards.size(); i++) {
			if ((pass == 0) == (row.cards[i].focus > 0.5f)) continue;
			const float cx = x + i * (l.card_w + l.gap) - row.scroll;
			if (cx > screen_w + margin || cx + l.card_w < -margin) continue; // off screen: don't load its art
			draw_card(r, fonts, row.cards[i], cx, y + l.cards_offset, l.card_w, l.card_h);
		}
	}
}
