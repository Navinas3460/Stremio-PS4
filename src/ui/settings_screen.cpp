#include "settings_screen.h"

#include "../core/stremio.h"
#include "../platform/platform.h"
#include "../video/video_player.h"
#include "components.h"
#include "theme.h"

#include <algorithm>

namespace {
using theme::S;
const float MARGIN_X = 120;
const float ITEM_W = S(900), ITEM_H = S(84), ITEM_GAP = S(16), ITEMS_TOP = S(170);
const float SIGN_OUT_CONFIRM_SECONDS = 3.0f;
} // namespace

SettingsScreen::SettingsScreen(Fonts &fonts, Navigator &nav) : fonts_(fonts), navigator_(nav) {
	avplayer_ = VideoPlayer::hardware_selected();
	refresh_items();
}

void SettingsScreen::refresh_items() {
	std::vector<Item> items;
	const bool armed = sign_out_armed_ > 0;
	items.push_back({ armed ? "Press \xC3\x97 again to sign out" : "Sign out", stremio().user_email(), armed });
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
		VideoPlayer::select_hardware(avplayer_);
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
		items_[i].focus = ui::approach(items_[i].focus, (!on_bar_ && i == index_) ? 1.0f : 0.0f, dt);
	}
	bar_.update(dt, on_bar_);
}

void SettingsScreen::draw(Renderer &r) {
	r.text(fonts_.page_header, "Settings", MARGIN_X, S(96), theme::BASE_CONTENT);
	const float R = theme::RADIUS_FIELD;
	for (int i = 0; i < (int)items_.size(); i++) {
		const Item &item = items_[i];
		const float y = ITEMS_TOP + i * (ITEM_H + ITEM_GAP);
		const Color tone = item.danger ? theme::ERROR : theme::PRIMARY;
		ui::begin_focus(r, MARGIN_X, y, ITEM_W, ITEM_H, R, item.focus, 1.03f, tone);
		r.rounded_rect(MARGIN_X, y, ITEM_W, ITEM_H, R, item.danger ? mix(theme::BASE_200, theme::ERROR, 0.2f) : theme::BASE_200);
		r.text(fonts_.card_title, item.label, MARGIN_X + S(28), y + ITEM_H / 2 + S(9), item.danger ? theme::ERROR : theme::BASE_CONTENT);
		const float vw = fonts_.body.measure(item.value);
		r.text(fonts_.body, item.value, MARGIN_X + ITEM_W - S(28) - vw, y + ITEM_H / 2 + S(8), theme::BASE_CONTENT_MUTED);
		ui::end_focus(r, MARGIN_X, y, ITEM_W, ITEM_H, R, item.focus);
	}
	const float info_y = ITEMS_TOP + items_.size() * (ITEM_H + ITEM_GAP) + S(40);
	r.text(fonts_.body, std::string("Stremio PS4 ") + APP_VERSION, MARGIN_X, info_y, theme::BASE_CONTENT_MUTED);
	bar_.draw(r, fonts_);
}
