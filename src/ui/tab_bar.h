// The four main tabs (Home, Search, Library, Settings): the bottom navigation
// bar they share, and switching between them (R1 / L1, or the bar itself).
#pragma once

#include "components.h"
#include "screen.h"
#include "theme.h"

#include <cstdint>
#include <string>

namespace tabs {

enum Tab { HOME, SEARCH, LIBRARY, SETTINGS, COUNT };

// Layout shared by the tab screens: content goes above CONTENT_BOTTOM.
// The tabs sit in one pill-shaped box (daisyUI "tabs-box").
constexpr float TAB_W = theme::S(190), TAB_H = theme::S(68), BOX_PAD = theme::S(10);
constexpr float BOX_H = TAB_H + 2 * BOX_PAD;
constexpr float BOX_Y = 1080 - 40 - BOX_H;
constexpr float CONTENT_BOTTOM = BOX_Y - theme::S(22);

// Shows `tab` as the bottom screen.
void open(Tab tab, Fonts &fonts, Navigator &nav);

class TabBar {
public:
	explicit TabBar(Tab active) : active_(active), index_(active) {}

	// R1 / L1 anywhere on a tab screen. True if it switched tabs.
	bool handle_shoulders(uint32_t pressed, Fonts &fonts, Navigator &nav);
	// Left / Right / Accept while the bar has focus. True if it switched tabs.
	bool handle_focused(uint32_t pressed, Fonts &fonts, Navigator &nav);
	void update(double dt, bool focused);
	// Bottom band, toast and the tabs.
	void draw(Renderer &r, Fonts &fonts);
	void show_toast(const std::string &text) { toast_.show(text); }

private:
	Tab active_;
	int index_;
	float focus_[COUNT] = {};
	ui::Toast toast_;
};

} // namespace tabs
