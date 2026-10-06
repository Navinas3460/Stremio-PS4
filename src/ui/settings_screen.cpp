#include "settings_screen.h"

#include "../core/stremio.h"
#include "../platform/platform.h"
#include "theme.h"

#include <algorithm>

namespace {
using theme::S;
const float MARGIN_X = 120;
const float ITEM_W = S(900), ITEM_H = S(84), ITEM_GAP = S(16), ITEMS_TOP = S(170);
const float SIGN_OUT_CONFIRM_SECONDS = 3.0f;
const char *PLAYER_CFG = "player.cfg";
} // namespace

SettingsScreen::SettingsScreen(Fonts &fonts, Navigator &nav) : fonts_(fonts), navigator_(nav) {
	std::string cfg;
	avplayer_ = platform::read_file(platform::data_dir() + PLAYER_CFG, cfg) && cfg.find("backend=avplayer") != std::string::npos;
	refresh_items();
}

void SettingsScreen::refresh_items() {
	std::vector<Item> items;
	items.push_back({ sign_out_armed_ > 0 ? "Press \xC3\x97 again to sign out" : "Sign out", stremio().user_email() });
#if defined(STREMIO_PS4)
	items.push_back({ "Video player", avplayer_ ? "PS4 hardware (MP4 only)" : "FFmpeg (plays everything)" });
#endif
	for (size_t i = 0; i < items.size() && i < items_.size(); i++) items[i].focus = items_[i].focus;
	items_.swap(items);
}

void SettingsScreen::activate() {
	if (index_ == 0) {
		if (sign_out_armed_ > 0) {
			stremio().logout(); // the app switches to the sign-in screen
			return;
		}
		sign_out_armed_ = SIGN_OUT_CONFIRM_SECONDS;
	} else if (index_ == 1) {
		avplayer_ = !avplayer_;
		const std::string path = platform::data_dir() + PLAYER_CFG;
		if (avplayer_) {
			platform::write_file(path, "backend=avplayer\n");
		} else {
			platform::remove_file(path);
		}
		bar_.show_toast(avplayer_ ? "Videos will use the PS4's hardware player (MP4 only)" : "Videos will use FFmpeg");
	}
}

void SettingsScreen::update(double dt, uint32_t pressed) {
	if (bar_.handle_shoulders(pressed, fonts_, navigator_)) return;
	if (on_bar_) {
		if (bar_.handle_focused(pressed, fonts_, navigator_)) return;
		if (pressed & platform::BUTTON_UP) on_bar_ = false;
	} else {
		if (pressed & platform::BUTTON_UP) index_ = std::max(0, index_ - 1);
		if (pressed & platform::BUTTON_DOWN) {
			if (index_ + 1 < (int)items_.size()) {
				index_++;
			} else {
				on_bar_ = true;
			}
		}
		if (pressed & (platform::BUTTON_UP | platform::BUTTON_DOWN)) sign_out_armed_ = 0;
		if (pressed & platform::BUTTON_ACCEPT) activate();
	}
	if (sign_out_armed_ > 0) sign_out_armed_ -= (float)dt;
	refresh_items();
	for (int i = 0; i < (int)items_.size(); i++) {
		items_[i].focus = tabs::approach(items_[i].focus, (!on_bar_ && i == index_) ? 1.0f : 0.0f, dt);
	}
	bar_.update(dt, on_bar_);
}

void SettingsScreen::draw(Renderer &r) {
	r.text(fonts_.page_header, "Settings", MARGIN_X, S(96), theme::TEXT);
	for (int i = 0; i < (int)items_.size(); i++) {
		const Item &item = items_[i];
		const float y = ITEMS_TOP + i * (ITEM_H + ITEM_GAP);
		const float f = item.focus;
		if (f > 0.02f) {
			r.rounded_rect(MARGIN_X - S(4), y - S(4), ITEM_W + S(8), ITEM_H + S(8), theme::RADIUS_CARD + S(4),
					theme::BRAND_PRIMARY.with_alpha(f));
		}
		r.rounded_rect(MARGIN_X, y, ITEM_W, ITEM_H, theme::RADIUS_CARD, theme::SURFACE);
		r.text(fonts_.card_title, item.label, MARGIN_X + S(28), y + ITEM_H / 2 + S(9), theme::TEXT);
		const float vw = fonts_.body.measure(item.value);
		r.text(fonts_.body, item.value, MARGIN_X + ITEM_W - S(28) - vw, y + ITEM_H / 2 + S(8), theme::TEXT_MUTED);
		const float opacity = theme::ALPHA_UNFOCUSED + (theme::ALPHA_FOCUSED - theme::ALPHA_UNFOCUSED) * f;
		if (opacity < 0.999f) {
			r.rounded_rect(MARGIN_X, y, ITEM_W, ITEM_H, theme::RADIUS_CARD, theme::BRAND_TERTIARY.with_alpha(1.0f - opacity));
		}
	}
	const float info_y = ITEMS_TOP + items_.size() * (ITEM_H + ITEM_GAP) + S(40);
	r.text(fonts_.body, std::string("Stremio TV ") + APP_VERSION, MARGIN_X, info_y, theme::TEXT_MUTED);
	bar_.draw(r, fonts_);
}
