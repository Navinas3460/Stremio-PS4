#include "components.h"

#include "../platform/platform.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace theme {

Color BASE_100 = rgb(0x051923);
Color BASE_200 = rgb(0x0C2A38);
Color BASE_300 = rgb(0x163D50);
Color BASE_CONTENT = rgb(0xEEF1F4);
Color BASE_CONTENT_MUTED = rgb(0xEEF1F4, 0.6f);

Color PRIMARY = rgb(0x0070D1);
Color PRIMARY_CONTENT = rgb(0xFFFFFF);
Color SECONDARY = rgb(0x685CEE);
Color ACCENT = rgb(0x84DD63);
Color ACCENT_CONTENT = rgb(0x051923);
Color WARNING = rgb(0xF2C14E);
Color ERROR = rgb(0xFF8A80);

namespace {
const char *UI_SCALE_CFG = "ui_scale.cfg";
int g_ui_scale_pct = 100;
int g_color_scheme = 0;

struct SchemeDef {
	const char *name;
	uint32_t base_100, base_200, base_300, base_content;
	uint32_t primary, primary_content, secondary, accent, warning, error;
};

const SchemeDef SCHEMES[] = {
	{ "PlayStation Blue", 0x051923, 0x0C2A38, 0x163D50, 0xEEF1F4, 0x0070D1, 0xFFFFFF, 0x685CEE, 0x84DD63, 0xF2C14E, 0xFF8A80 },
	{ "Stremio Purple",   0x0E0B16, 0x1A1528, 0x29223E, 0xF1EFF8, 0x7B5BF5, 0xFFFFFF, 0xA855F7, 0x22D3EE, 0xF2C14E, 0xFF8A80 },
	{ "OLED Pitch Black", 0x000000, 0x12151A, 0x202630, 0xF3F4F6, 0x2563EB, 0xFFFFFF, 0x7C3AED, 0x34D399, 0xFBBF24, 0xF87171 },
	{ "Crimson Cinema",   0x12080A, 0x221015, 0x361A22, 0xF6EEF0, 0xE50914, 0xFFFFFF, 0xD97706, 0xFBBF24, 0xF59E0B, 0xFCA5A5 },
	{ "Emerald Forest",   0x061510, 0x0E2920, 0x183F32, 0xECFDF5, 0x10B981, 0x051912, 0x0EA5E9, 0xA3E635, 0xFBBF24, 0xFF8A80 },
	{ "Sunset Gold",      0x140E08, 0x241A10, 0x38291A, 0xFEF3C7, 0xD97706, 0xFFFFFF, 0xEA580C, 0x38BDF8, 0xFBBF24, 0xFF8A80 },
	{ "Cyberpunk Neon",   0x0B0813, 0x181028, 0x261A40, 0xFDF4FF, 0xFF007F, 0xFFFFFF, 0x00F0FF, 0xFFE600, 0xF59E0B, 0xFF3366 },
	{ "Nordic Frost",     0x0B1117, 0x15222E, 0x203345, 0xE6F1F8, 0x38BDF8, 0x0B1117, 0x818CF8, 0x2DD4BF, 0xFBBF24, 0xFB7185 },
	{ "Midnight Amethyst",0x110C1E, 0x1D1432, 0x2E1E4E, 0xF5EEFC, 0x9333EA, 0xFFFFFF, 0xC084FC, 0xF472B6, 0xFBBF24, 0xF87171 },
	{ "Solar Flare",      0x160D08, 0x27170E, 0x3D2416, 0xFFF7ED, 0xF97316, 0xFFFFFF, 0xFB923C, 0xFACC15, 0xF59E0B, 0xEF4444 },
	{ "Retro Synthwave",  0x140727, 0x240D42, 0x391466, 0xFFEDFE, 0xEC4899, 0xFFFFFF, 0x8B5CF6, 0x06B6D4, 0xFBBF24, 0xF43F5E },
	{ "Tokyo Drift",      0x0E0E11, 0x1A1B22, 0x272935, 0xF3F4F6, 0xDC2626, 0xFFFFFF, 0x64748B, 0x38BDF8, 0xFBBF24, 0xEF4444 },
};
const int SCHEME_COUNT = (int)(sizeof(SCHEMES) / sizeof(SCHEMES[0]));

void save_ui_cfg() {
	const std::string path = platform::data_dir() + UI_SCALE_CFG;
	if (g_ui_scale_pct == 100 && g_color_scheme == 0) {
		platform::remove_file(path);
	} else {
		platform::write_file(path, "scale=" + std::to_string(g_ui_scale_pct) + "\nscheme=" + std::to_string(g_color_scheme) + "\n");
	}
}
} // namespace

int color_scheme_count() {
	return SCHEME_COUNT;
}

int color_scheme_index() {
	return g_color_scheme;
}

const char *color_scheme_name(int index) {
	if (index < 0 || index >= SCHEME_COUNT) return SCHEMES[0].name;
	return SCHEMES[index].name;
}

void set_color_scheme(int index, bool save) {
	if (index < 0) index = (index % SCHEME_COUNT + SCHEME_COUNT) % SCHEME_COUNT;
	else index = index % SCHEME_COUNT;
	g_color_scheme = index;
	const SchemeDef &s = SCHEMES[index];
	BASE_100 = rgb(s.base_100);
	BASE_200 = rgb(s.base_200);
	BASE_300 = rgb(s.base_300);
	BASE_CONTENT = rgb(s.base_content);
	BASE_CONTENT_MUTED = rgb(s.base_content, 0.6f);
	PRIMARY = rgb(s.primary);
	PRIMARY_CONTENT = rgb(s.primary_content);
	SECONDARY = rgb(s.secondary);
	ACCENT = rgb(s.accent);
	ACCENT_CONTENT = BASE_100;
	WARNING = rgb(s.warning);
	ERROR = rgb(s.error);
	if (save) save_ui_cfg();
}

int ui_scale_percent() {
	return g_ui_scale_pct;
}

float ui_scale() {
	return DEFAULT_UI_SCALE * (g_ui_scale_pct / 100.0f);
}

void set_ui_scale_percent(int pct, bool save) {
	pct = std::clamp(pct, UI_SCALE_MIN_PCT, UI_SCALE_MAX_PCT);
	pct = ((pct + UI_SCALE_STEP_PCT / 2) / UI_SCALE_STEP_PCT) * UI_SCALE_STEP_PCT;
	g_ui_scale_pct = std::clamp(pct, UI_SCALE_MIN_PCT, UI_SCALE_MAX_PCT);
	if (save) save_ui_cfg();
}

void load_ui_scale() {
	set_color_scheme(0, false);
	std::string cfg;
	if (!platform::read_file(platform::data_dir() + UI_SCALE_CFG, cfg)) return;
	const size_t pos = cfg.find("scale=");
	if (pos != std::string::npos) {
		const int pct = atoi(cfg.c_str() + pos + 6);
		if (pct >= UI_SCALE_MIN_PCT && pct <= UI_SCALE_MAX_PCT) {
			set_ui_scale_percent(pct, false);
		}
	}
	const size_t spos = cfg.find("scheme=");
	if (spos != std::string::npos) {
		const int scheme = atoi(cfg.c_str() + spos + 7);
		if (scheme >= 0 && scheme < SCHEME_COUNT) {
			set_color_scheme(scheme, false);
		}
	}
}

} // namespace theme

void Fonts::apply_scale() {
	page_header.set_size(theme::SIZE_PAGE_HEADER);
	section.set_size(theme::SIZE_SECTION_HEADER);
	card_title.set_size(theme::SIZE_CARD_TITLE);
	body.set_size(theme::SIZE_BODY);
	small.set_size(theme::SIZE_SMALL);
	subtitle.set_size(theme::SIZE_SUBTITLE);
}

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
	size_t first = 0; // whole UTF-8 character (e.g. "Ж", "千")
	if (!name.empty()) next_codepoint(name, first);
	std::string initial = name.empty() ? "?" : name.substr(0, first);
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
