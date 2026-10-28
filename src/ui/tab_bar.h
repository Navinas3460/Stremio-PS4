// The six main tabs (Home, Discover, Search, Library, Add-ons, Settings): the bottom
// navigation bar they share, and switching between them (R1 / L1, or the bar
// itself).
#pragma once

#include "components.h"
#include "screen.h"
#include "theme.h"

#include <cstdint>
#include <string>

namespace tabs {

enum Tab { HOME, DISCOVER, SEARCH, LIBRARY, ADDONS, SETTINGS, COUNT };

// Layout shared by the tab screens: content goes above content_bottom().
// The tabs sit in one pill-shaped box (daisyUI "tabs-box").
inline float tab_w() {
        const float preferred = theme::S(190);
        const float available = (1920.0f - 240.0f - 2 * theme::S(10)) / COUNT;
        return preferred < available ? preferred : available;
}
inline float tab_h() { return theme::S(68); }
inline float box_pad() { return theme::S(10); }
inline float box_h() { return tab_h() + 2 * box_pad(); }
inline float box_y() { return 1080 - 40 - box_h(); }
inline float content_bottom() { return box_y() - theme::S(22); }

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
