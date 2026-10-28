#include "tab_bar.h"

#include "addons_screen.h"

#include "../platform/platform.h"
#include "discover_screen.h"
#include "home_screen.h"
#include "i18n.h"
#include "library_screen.h"
#include "search_screen.h"
#include "settings_screen.h"

#include <algorithm>

namespace tabs {

namespace {
using theme::S;
const float W = 1920, H = 1080;
const i18n::Text LABELS[COUNT] = {
	i18n::Text::HOME,
	i18n::Text::DISCOVER,
	i18n::Text::SEARCH,
        i18n::Text::LIBRARY,
        i18n::Text::ADDONS,
	i18n::Text::SETTINGS
};
} // namespace

void open(Tab tab, Fonts &fonts, Navigator &nav) {
	Screen *s = nullptr;
	switch (tab) {
		case HOME: s = new HomeScreen(fonts, nav); break;
		case DISCOVER: s = new DiscoverScreen(fonts, nav); break;
		case SEARCH: s = new SearchScreen(fonts, nav); break;
                case LIBRARY: s = new LibraryScreen(fonts, nav); break;
                case ADDONS: s = new AddonsScreen(fonts, nav); break;
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
	r.rect(0, content_bottom(), W, H - content_bottom(), theme::BASE_100);
	toast_.draw(r, fonts.body, W / 2, box_y() - S(76));

	const float box_w = COUNT * tab_w() + 2 * box_pad();
	const float box_x = (W - box_w) / 2;
	r.rounded_rect(box_x, box_y(), box_w, box_h(), theme::radius_selector(), theme::BASE_200);
	// The focused tab last, so its ring isn't covered by its neighbours.
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < COUNT; i++) {
			if ((pass == 0) == (focus_[i] > 0.5f)) continue;
			ui::chip(r, fonts.card_title,
                                        fonts.card_title.ellipsize(i18n::tr(LABELS[i]), tab_w() - S(32)), box_x + box_pad() + i * tab_w(), box_y() + box_pad(), tab_w(), tab_h(),
					i == active_, focus_[i], theme::BASE_200);
		}
	}
	// Shoulder-button marks at the ends of the box.
	const float hy = box_y() + box_h() / 2 + fonts.small.size() * 0.36f;
	ui::kbd(r, fonts.small, "L1", box_x - S(20) - ui::kbd_width(fonts.small, "L1"), hy);
	ui::kbd(r, fonts.small, "R1", box_x + box_w + S(20), hy);
}

} // namespace tabs
