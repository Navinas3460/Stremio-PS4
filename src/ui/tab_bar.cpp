#include "tab_bar.h"

#include "../platform/platform.h"
#include "home_screen.h"
#include "library_screen.h"
#include "search_screen.h"
#include "settings_screen.h"

#include <algorithm>
#include <cmath>

namespace tabs {

namespace {
using theme::S;
const float W = 1920, H = 1080;
const char *LABELS[COUNT] = { "Home", "Search", "Library", "Settings" };
const float TOAST_SECONDS = 2.5f;
} // namespace

void open(Tab tab, Fonts &fonts, Navigator &nav) {
	Screen *s = nullptr;
	switch (tab) {
		case HOME: s = new HomeScreen(fonts, nav); break;
		case SEARCH: s = new SearchScreen(fonts, nav); break;
		case LIBRARY: s = new LibraryScreen(fonts, nav); break;
		default: s = new SettingsScreen(fonts, nav); break;
	}
	nav.replace_root(std::unique_ptr<Screen>(s));
}

float approach(float current, float target, double dt) {
	const float k = 1.0f - std::exp(-(float)dt * 3.0f / theme::FOCUS_DURATION);
	return current + (target - current) * k;
}

bool TabBar::handle_shoulders(uint32_t pressed, Fonts &fonts, Navigator &nav) {
	int step = 0;
	if (pressed & platform::BUTTON_R1) step = 1;
	if (pressed & platform::BUTTON_L1) step = -1;
	if (step == 0) return false;
	open((Tab)((active_ + step + COUNT) % COUNT), fonts, nav);
	return true;
}

bool TabBar::handle_focused(uint32_t pressed, Fonts &fonts, Navigator &nav) {
	if (pressed & platform::BUTTON_LEFT) index_ = std::max(0, index_ - 1);
	if (pressed & platform::BUTTON_RIGHT) index_ = std::min((int)COUNT - 1, index_ + 1);
	if ((pressed & platform::BUTTON_ACCEPT) && index_ != active_) {
		open((Tab)index_, fonts, nav);
		return true;
	}
	return false;
}

void TabBar::update(double dt, bool focused) {
	if (!focused) index_ = active_; // re-entering the bar starts on the current tab
	for (int i = 0; i < COUNT; i++) focus_[i] = approach(focus_[i], (focused && i == index_) ? 1.0f : 0.0f, dt);
	if (toast_time_ > 0) toast_time_ -= (float)dt;
}

void TabBar::show_toast(const std::string &text) {
	toast_ = text;
	toast_time_ = TOAST_SECONDS;
}

void TabBar::draw(Renderer &r, Fonts &fonts) {
	const Color bg = theme::BRAND_TERTIARY;
	r.rect(0, CONTENT_BOTTOM, W, H - CONTENT_BOTTOM, bg);
	if (toast_time_ > 0) {
		const float a = std::min(1.0f, toast_time_ / 0.3f);
		const float tw = fonts.body.measure(toast_);
		r.text(fonts.body, toast_, (W - tw) / 2, BAR_Y - S(18), theme::STATUS_ACCENT.with_alpha(a));
	}
	const float total = COUNT * BAR_W + (COUNT - 1) * BAR_GAP;
	for (int i = 0; i < COUNT; i++) {
		const float x = (W - total) / 2 + i * (BAR_W + BAR_GAP);
		const bool active = i == active_;
		const float f = focus_[i];
		r.push(x + BAR_W / 2, BAR_Y + BAR_H / 2, 1.0f + (theme::FOCUS_SCALE - 1.0f) * f, 1.0f);
		const float radius = active ? theme::RADIUS_PILL : theme::RADIUS_CARD;
		if (f > 0.02f) {
			const Color ring = active ? theme::TEXT : theme::BRAND_SECONDARY;
			r.rounded_rect(x - S(4), BAR_Y - S(4), BAR_W + S(8), BAR_H + S(8), radius + S(4), ring.with_alpha(f));
		}
		r.rounded_rect(x, BAR_Y, BAR_W, BAR_H, radius, active ? theme::BRAND_PRIMARY : theme::SURFACE);
		const float lw = fonts.card_title.measure(LABELS[i]);
		r.text(fonts.card_title, LABELS[i], x + (BAR_W - lw) / 2, BAR_Y + BAR_H / 2 + S(9), theme::TEXT);
		const float opacity = theme::ALPHA_UNFOCUSED + (theme::ALPHA_FOCUSED - theme::ALPHA_UNFOCUSED) * f;
		if (opacity < 0.999f) r.rounded_rect(x, BAR_Y, BAR_W, BAR_H, radius, bg.with_alpha(1.0f - opacity));
		r.pop();
	}
	// Shoulder-button hints at the ends of the bar.
	const float hy = BAR_Y + BAR_H / 2 + S(9);
	r.text(fonts.body, "L1", (W - total) / 2 - S(60), hy, theme::TEXT_MUTED);
	r.text(fonts.body, "R1", (W + total) / 2 + S(30), hy, theme::TEXT_MUTED);
}

} // namespace tabs
