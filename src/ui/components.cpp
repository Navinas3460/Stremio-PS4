#include "components.h"

#include <algorithm>
#include <cmath>

namespace ui {

using theme::S;

namespace {
const float TOAST_SECONDS = 2.5f, TOAST_FADE = 0.3f;

// Baseline that centres a line of `font` vertically on cy.
float centred_baseline(Font &font, float cy) {
	return cy + font.size() * 0.36f;
}

float cap_height(Font &font) {
	return font.size() * 1.5f;
}

float cap_width(Font &font, const std::string &key) {
	return std::max(cap_height(font), font.measure(key) + font.size() * 0.8f);
}

float hint_gap(Font &font) {
	return font.size() * 1.4f;
}
} // namespace

float approach(float current, float target, double dt) {
	const float k = 1.0f - std::exp(-(float)dt * 3.0f / theme::FOCUS_DURATION);
	return current + (target - current) * k;
}

float follow(float scroll, float start, float size, float view, double dt) {
	float target = scroll;
	if (start - target < 0) target = start;
	if (start + size - target > view) target = start + size - view;
	return approach(scroll, target, dt);
}

void begin_focus(Renderer &r, float x, float y, float w, float h, float radius, float focus, float scale,
		const Color &ring) {
	r.push(x + w / 2, y + h / 2, 1.0f + (scale - 1.0f) * focus, 1.0f);
	if (focus > 0.02f) {
		const float t = theme::FOCUS_RING;
		r.rounded_rect(x - t, y - t, w + 2 * t, h + 2 * t, radius + t, ring.with_alpha(focus));
	}
}

void end_focus(Renderer &r, float x, float y, float w, float h, float radius, float focus, const Color &behind) {
	const float opacity = theme::ALPHA_UNFOCUSED + (1.0f - theme::ALPHA_UNFOCUSED) * focus;
	if (opacity < 0.999f) r.rounded_rect(x, y, w, h, radius, behind.with_alpha(1.0f - opacity));
	r.pop();
}

float chip_width(Font &font, const std::string &label) {
	return font.measure(label) + font.size() * 2.0f;
}

void chip(Renderer &r, Font &font, const std::string &label, float x, float y, float w, float h, bool active,
		float focus, const Color &behind) {
	const float R = theme::RADIUS_SELECTOR;
	begin_focus(r, x, y, w, h, R, focus, theme::FOCUS_SCALE, active ? theme::SECONDARY : theme::PRIMARY);
	r.rounded_rect(x, y, w, h, R, active ? theme::PRIMARY : mix(theme::BASE_200, theme::BASE_300, focus));
	// (Opaque: translucent text would be blended toward the backdrop, not the chip.)
	const Color muted = mix(theme::BASE_200, theme::BASE_CONTENT, theme::ALPHA_UNFOCUSED);
	const Color fg = active ? theme::PRIMARY_CONTENT : mix(muted, theme::BASE_CONTENT, focus);
	r.text(font, label, x + (w - font.measure(label)) / 2, centred_baseline(font, y + h / 2), fg);
	if (active) {
		end_focus(r, x, y, w, h, R, focus, behind);
	} else {
		r.pop(); // inactive chips are dimmed by their muted label
	}
}

float badge_height(Font &font) {
	return font.size() * 1.5f;
}

float badge_width(Font &font, const std::string &text) {
	return font.measure(text) + font.size() * 1.2f;
}

float badge(Renderer &r, Font &font, const std::string &text, float x, float y, const Color &bg, const Color &fg) {
	const float w = badge_width(font, text), h = badge_height(font);
	r.rounded_rect(x, y, w, h, theme::RADIUS_SELECTOR, bg);
	r.text(font, text, x + font.size() * 0.6f, centred_baseline(font, y + h / 2), fg);
	return w;
}

void progress(Renderer &r, float x, float y, float w, float h, float value, float alpha) {
	r.rounded_rect(x, y, w, h, h / 2, theme::BASE_CONTENT.with_alpha(0.25f * alpha));
	value = std::clamp(value, 0.0f, 1.0f);
	if (value > 0) r.rounded_rect(x, y, std::max(h, w * value), h, h / 2, theme::PRIMARY.with_alpha(alpha));
}

void status(Renderer &r, float cx, float cy, const Color &c) {
	const float d = S(12);
	r.rounded_rect(cx - d / 2, cy - d / 2, d, d, d / 2, c);
}

void avatar(Renderer &r, Font &font, const std::string &name, float x, float y, float size) {
	r.rounded_rect(x, y, size, size, size / 2, theme::BASE_300);
	std::string initial = name.empty() ? "?" : name.substr(0, 1);
	if (initial[0] >= 'a' && initial[0] <= 'z') initial[0] = (char)(initial[0] - 'a' + 'A');
	r.text(font, initial, x + (size - font.measure(initial)) / 2, centred_baseline(font, y + size / 2), theme::BASE_CONTENT);
}

float kbd_width(Font &font, const std::string &key) {
	return cap_width(font, key);
}

float kbd(Renderer &r, Font &font, const std::string &key, float x, float baseline) {
	// daisyUI kbd: a raised cap, its border thicker at the bottom.
	const float w = cap_width(font, key), h = cap_height(font);
	const float y = baseline - font.size() * 0.36f - h / 2;
	const float radius = S(8), edge = S(1.5f);
	r.rounded_rect(x, y, w, h, radius, mix(theme::BASE_300, theme::BASE_CONTENT, 0.25f));
	r.rounded_rect(x + edge, y + edge, w - 2 * edge, h - 3 * edge, radius - edge, theme::BASE_300);
	r.text(font, key, x + (w - font.measure(key)) / 2, centred_baseline(font, y + (h - edge) / 2), theme::BASE_CONTENT);
	return w;
}

float hints_width(Font &font, const std::vector<Hint> &list) {
	float w = 0;
	for (const Hint &h : list) {
		w += cap_width(font, h.key) + font.size() * 0.5f + font.measure(h.label) + hint_gap(font);
	}
	return list.empty() ? 0 : w - hint_gap(font);
}

void hints(Renderer &r, Font &font, const std::vector<Hint> &list, float x, float baseline) {
	for (const Hint &h : list) {
		x += kbd(r, font, h.key, x, baseline) + font.size() * 0.5f;
		x += r.text(font, h.label, x, baseline, theme::BASE_CONTENT_MUTED) + hint_gap(font);
	}
}

void Toast::show(const std::string &text, bool error) {
	text_ = text;
	error_ = error;
	time_ = TOAST_SECONDS;
}

void Toast::update(double dt) {
	if (time_ > 0) time_ -= (float)dt;
}

void Toast::draw(Renderer &r, Font &font, float cx, float y) const {
	if (time_ <= 0) return;
	const Color tone = error_ ? theme::ERROR : theme::ACCENT;
	const float pad = font.size() * 0.9f, dot = S(12);
	const float h = font.size() * 2.2f;
	const float w = pad + dot + font.size() * 0.6f + font.measure(text_) + pad;
	const float x = cx - w / 2;
	r.push(cx, y, 1.0f, std::min(1.0f, time_ / TOAST_FADE));
	r.rounded_rect(x, y, w, h, theme::RADIUS_FIELD, mix(theme::BASE_200, tone, 0.18f));
	status(r, x + pad + dot / 2, y + h / 2, tone);
	r.text(font, text_, x + pad + dot + font.size() * 0.6f, centred_baseline(font, y + h / 2), theme::BASE_CONTENT);
	r.pop();
}

} // namespace ui
