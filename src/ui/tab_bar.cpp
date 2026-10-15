#include "tab_bar.h"

#include "../platform/platform.h"
#include "home_screen.h"
#include "library_screen.h"
#include "search_screen.h"
#include "settings_screen.h"

#include <algorithm>

namespace tabs {

namespace {
using theme::S;
const float W = 1920, H = 1080;
const char *LABELS[COUNT] = { "Home", "Search", "Library", "Settings" };
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
	for (int i = 0; i < COUNT; i++) focus_[i] = ui::approach(focus_[i], (focused && i == index_) ? 1.0f : 0.0f, dt);
	toast_.update(dt);
}

void TabBar::draw(Renderer &r, Fonts &fonts) {
	r.rect(0, CONTENT_BOTTOM, W, H - CONTENT_BOTTOM, theme::BASE_100);
	toast_.draw(r, fonts.body, W / 2, BOX_Y - S(76));

	const float box_w = COUNT * TAB_W + 2 * BOX_PAD;
	const float box_x = (W - box_w) / 2;
	r.rounded_rect(box_x, BOX_Y, box_w, BOX_H, theme::RADIUS_SELECTOR, theme::BASE_200);
	for (int i = 0; i < COUNT; i++) {
		const float x = box_x + BOX_PAD + i * TAB_W, y = BOX_Y + BOX_PAD;
		const bool active = i == active_;
		const float f = focus_[i];
		// The active tab is filled; focusing it rings it in the secondary
		// colour so the two states stay distinguishable.
		ui::begin_focus(r, x, y, TAB_W, TAB_H, theme::RADIUS_SELECTOR, f, theme::FOCUS_SCALE,
				active ? theme::SECONDARY : theme::PRIMARY);
		if (active || f > 0.02f) {
			const Color body = active ? theme::PRIMARY : theme::BASE_300.with_alpha(f);
			r.rounded_rect(x, y, TAB_W, TAB_H, theme::RADIUS_SELECTOR, body);
		}
		Font &font = fonts.card_title;
		const float lw = font.measure(LABELS[i]);
		// (Opaque: translucent text would be blended toward the backdrop, not the box.)
		const Color muted = mix(theme::BASE_200, theme::BASE_CONTENT, theme::ALPHA_UNFOCUSED);
		const Color label = active ? theme::PRIMARY_CONTENT : mix(muted, theme::BASE_CONTENT, f);
		r.text(font, LABELS[i], x + (TAB_W - lw) / 2, y + TAB_H / 2 + font.size() * 0.36f, label);
		if (active) {
			ui::end_focus(r, x, y, TAB_W, TAB_H, theme::RADIUS_SELECTOR, f, theme::BASE_200);
		} else {
			r.pop(); // inactive tabs are dimmed by their muted label
		}
	}
	// Shoulder-button marks at the ends of the box.
	const float hy = BOX_Y + BOX_H / 2 + fonts.small.size() * 0.36f;
	ui::kbd(r, fonts.small, "L1", box_x - S(20) - ui::kbd_width(fonts.small, "L1"), hy);
	ui::kbd(r, fonts.small, "R1", box_x + box_w + S(20), hy);
}

} // namespace tabs
