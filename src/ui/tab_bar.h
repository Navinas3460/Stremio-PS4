// The five main tabs (Home, Discover, Search, Library, Settings): the bottom
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

// Layout shared by the tab screens: content goes above CONTENT_BOTTOM.
// The tabs sit in one pill-shaped box (daisyUI "tabs-box"), scaled by UI scale.
inline float tab_w() { return theme::S(164); }
inline float tab_h() { return theme::S(68); }
inline float box_pad() { return theme::S(10); }
inline float box_h() { return tab_h() + 2 * box_pad(); }
inline float box_y() { return 1080 - 40 - box_h(); }
inline float content_bottom() { return box_y() - theme::S(22); }

struct DynamicMetric {
	float (*fn)();
	operator float() const { return fn(); }
};

inline const DynamicMetric TAB_W{ tab_w };
inline const DynamicMetric TAB_H{ tab_h };
inline const DynamicMetric BOX_PAD{ box_pad };
inline const DynamicMetric BOX_H{ box_h };
inline const DynamicMetric BOX_Y{ box_y };
inline const DynamicMetric CONTENT_BOTTOM{ content_bottom };

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
