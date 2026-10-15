// Small UI components shared by the screens, modelled on daisyUI's (btn,
// badge, progress, kbd, status, toast, avatar) and drawn with the theme
// tokens. Sizes are in canvas pixels (1920x1080).
#pragma once

#include "screen.h"
#include "theme.h"

#include <string>
#include <vector>

namespace ui {

// Exponential approach used for every animation (~95% in FOCUS_DURATION).
float approach(float current, float target, double dt);
// Animates `scroll` so the span [start, start + size) is inside a window of
// `view` pixels (lists and rows following the focused item).
float follow(float scroll, float start, float size, float view, double dt);

// Focus chrome of a focusable element (btn, card, key, list row), `focus`
// animated 0..1: begin_focus scales the element about its centre and draws
// the focus ring behind it; the caller then draws the element; end_focus
// dims it while unfocused (docs/DESIGN.md) with a `behind`-coloured overlay
// and ends the scale.
void begin_focus(Renderer &r, float x, float y, float w, float h, float radius, float focus,
		float scale = theme::FOCUS_SCALE, const Color &ring = theme::PRIMARY);
void end_focus(Renderer &r, float x, float y, float w, float h, float radius, float focus,
		const Color &behind = theme::BASE_100);

// Pill button (daisyUI btn / filter / tab), label centred: filled with the
// primary colour when `active`, else BASE_200 lightening with `focus`. An
// active chip is ringed in the secondary colour while focused so the two
// states stay distinguishable. `behind` is the colour around it.
float chip_width(Font &font, const std::string &label);
void chip(Renderer &r, Font &font, const std::string &label, float x, float y, float w, float h, bool active,
		float focus, const Color &behind = theme::BASE_100);

// Pill with a label. Returns its width.
float badge_width(Font &font, const std::string &text);
float badge(Renderer &r, Font &font, const std::string &text, float x, float y, const Color &bg, const Color &fg);
float badge_height(Font &font);

// Track and fill; `value` 0..1.
void progress(Renderer &r, float x, float y, float w, float h, float value, float alpha = 1.0f);

// A dot marking the item in use (the playing track, the current episode).
void status(Renderer &r, float cx, float cy, const Color &c = theme::ACCENT);

// Initial in a circle, for the signed-in account.
void avatar(Renderer &r, Font &font, const std::string &name, float x, float y, float size);

// Controller hints: each is a kbd key cap ("×", "○", "L1") and a label.
struct Hint {
	std::string key, label;
};
float hints_width(Font &font, const std::vector<Hint> &hints);
// Draws the hints from x (left edge) with the text baseline at `baseline`.
void hints(Renderer &r, Font &font, const std::vector<Hint> &hints, float x, float baseline);
// A single key cap, e.g. the L1 / R1 marks around the tab bar. Returns its width.
float kbd_width(Font &font, const std::string &key);
float kbd(Renderer &r, Font &font, const std::string &key, float x, float baseline);

// A short message that fades out by itself (alert style).
class Toast {
public:
	void show(const std::string &text, bool error = false);
	void update(double dt);
	bool visible() const { return time_ > 0; }
	void hide() { time_ = 0; }
	// Centred on cx, top edge at y.
	void draw(Renderer &r, Font &font, float cx, float y) const;

private:
	std::string text_;
	bool error_ = false;
	float time_ = 0;
};

} // namespace ui
