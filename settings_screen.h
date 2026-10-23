// Settings tab: account (sign out), UI scale slider, color scheme, video player choice, build info.
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
	struct Item {
		std::string label, value;
		bool danger = false;   // daisyUI btn-error look (sign-out confirmation)
		bool slider = false;   // draws a progress slider bar alongside the value
		bool swatches = false; // draws color scheme preview swatches alongside the value
		float slider_progress = 0;
		float focus = 0;
	};
	void refresh_items();
	void activate();
	void step_scale(int delta_pct);
	void step_scheme(int delta);

	Fonts &fonts_;
	Navigator &navigator_;
	tabs::TabBar bar_{ tabs::SETTINGS };
	std::vector<Item> items_;
	int index_ = 0;
	bool on_bar_ = false;
	float sign_out_armed_ = 0; // seconds left to confirm sign-out
	bool avplayer_ = false;
	bool pro_4k_ = false;
};
