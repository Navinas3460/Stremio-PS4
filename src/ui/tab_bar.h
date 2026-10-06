// The four main tabs (Home, Search, Library, Settings): the bottom navigation
// bar they share, and switching between them (R1 / L1, or the bar itself).
#pragma once

#include "screen.h"
#include "theme.h"

#include <cstdint>
#include <string>

namespace tabs {

enum Tab { HOME, SEARCH, LIBRARY, SETTINGS, COUNT };

// Layout shared by the tab screens: content goes above CONTENT_BOTTOM.
constexpr float BAR_W = theme::S(200), BAR_H = theme::S(88), BAR_GAP = theme::S(16);
constexpr float BAR_Y = 1080 - 40 - BAR_H;
constexpr float CONTENT_BOTTOM = BAR_Y - theme::S(22);

// Shows `tab` as the bottom screen.
void open(Tab tab, Fonts &fonts, Navigator &nav);

// Exponential approach used for focus animations (~95% in FOCUS_DURATION).
float approach(float current, float target, double dt);

class TabBar {
public:
	explicit TabBar(Tab active) : active_(active), index_(active) {}

	// R1 / L1 anywhere on a tab screen. True if it switched tabs.
	bool handle_shoulders(uint32_t pressed, Fonts &fonts, Navigator &nav);
	// Left / Right / Accept while the bar has focus. True if it switched tabs.
	bool handle_focused(uint32_t pressed, Fonts &fonts, Navigator &nav);
	void update(double dt, bool focused);
	// Bottom band, toast line and the tab buttons.
	void draw(Renderer &r, Fonts &fonts);
	void show_toast(const std::string &text);

private:
	Tab active_;
	int index_;
	float focus_[COUNT] = {};
	std::string toast_;
	float toast_time_ = 0;
};

} // namespace tabs
