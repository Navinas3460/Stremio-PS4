#include "card.h"

#include "../net/images.h"
#include "details_screen.h"
#include "theme.h"

using theme::S;

Card Card::from_meta(const MetaItem &m) {
	Card c;
	c.title = m.name;
	c.subtitle = m.release_info;
	c.images = m.landscape_images();
	c.meta_id = m.id;
	c.type = m.type;
	c.poster = m.poster;
	c.release_info = m.release_info;
	return c;
}

void draw_card(Renderer &r, Fonts &fonts, const Card &c, float x, float y, float w, float h) {
	const float R = theme::RADIUS_CARD;
	const float f = c.focus;
	r.push(x + w / 2, y + h / 2, 1.0f + (theme::FOCUS_SCALE - 1.0f) * f, 1.0f);

	const bool ring = f > 0.02f;
	if (ring) {
		r.rounded_rect(x - S(4), y - S(4), w + S(8), h + S(8), R + S(4), theme::BRAND_PRIMARY.with_alpha(f));
	}
	r.rounded_rect(x, y, w, h, R, theme::SURFACE);
	const Texture &art = images::first(c.images);
	if (art.valid()) {
		r.image_cover(art, x, y, w, h);
	} else {
		const std::string initial = c.title.substr(0, 1);
		const float iw = fonts.page_header.measure(initial);
		r.text(fonts.page_header, initial, x + (w - iw) / 2, y + h / 2 + S(16), theme::TEXT_MUTED);
	}
	r.vertical_fade(x, y + h * 0.35f, w, h * 0.65f, theme::BRAND_TERTIARY, 0.0f, 0.95f);
	const Color behind = ring ? mix(theme::BRAND_TERTIARY, theme::BRAND_PRIMARY, f) : theme::BRAND_TERTIARY;
	r.round_corners(x, y, w, h, R, behind);

	const float pad = S(24);
	float baseline = y + h - S(22);
	if (c.progress > 0) {
		const float bar_h = S(6);
		const float bar_y = y + h - S(20) - bar_h;
		r.rounded_rect(x + pad, bar_y, w - 2 * pad, bar_h, bar_h / 2, rgb(0xFFFFFF, 0.25f));
		r.rounded_rect(x + pad, bar_y, (w - 2 * pad) * c.progress, bar_h, bar_h / 2, theme::BRAND_PRIMARY);
		baseline = bar_y - S(14);
	}
	if (!c.subtitle.empty()) {
		r.text(fonts.body, fonts.body.ellipsize(c.subtitle, w - 2 * pad), x + pad, baseline, theme::TEXT_MUTED);
		baseline -= S(32);
	}
	r.text(fonts.card_title, fonts.card_title.ellipsize(c.title, w - 2 * pad), x + pad, baseline, theme::TEXT);

	if (!c.badge.empty()) {
		const float bw = fonts.body.measure(c.badge) + S(28);
		r.rounded_rect(x + w - S(16) - bw, y + S(16), bw, S(34), S(12), theme::STATUS_ACCENT);
		r.text(fonts.body, c.badge, x + w - S(16) - bw + S(14), y + S(16) + S(24), theme::BRAND_TERTIARY);
	}

	// Unfocused dimming (60% opacity, docs/DESIGN.md) as a backdrop-coloured overlay.
	const float opacity = theme::ALPHA_UNFOCUSED + (theme::ALPHA_FOCUSED - theme::ALPHA_UNFOCUSED) * f;
	if (opacity < 0.999f) {
		r.rounded_rect(x, y, w, h, R, theme::BRAND_TERTIARY.with_alpha(1.0f - opacity));
	}
	r.pop();
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
