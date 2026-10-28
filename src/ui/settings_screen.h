// Settings tab: account (sign out), UI scale, video player choice, build info.
#pragma once

#include "screen.h"
#include "tab_bar.h"

#include <string>
#include <vector>

class SettingsScreen : public Screen {
public:
	SettingsScreen(Fonts &fonts, Navigator &nav);
	void update(double dt, uint32_t pressed) override;
	void draw(Renderer &r) override;

private:
	enum Action { SIGN_OUT, UI_SCALE, VIDEO_PLAYER, INTERFACE_LANGUAGE };
	struct Item {
		Action action;
		std::string label, value;
		bool danger = false; // daisyUI btn-error look (sign-out confirmation)
		float focus = 0;
	};
	void refresh_items();
	void activate();
	void step_scale(int steps);

	Fonts &fonts_;
	Navigator &navigator_;
	tabs::TabBar bar_{ tabs::SETTINGS };
	std::vector<Item> items_;
	int index_ = 0;
	bool on_bar_ = false;
	float sign_out_armed_ = 0; // seconds left to confirm sign-out
	bool avplayer_ = false;
};
