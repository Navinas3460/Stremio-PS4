#include "settings_screen.h"

#include "../core/stremio.h"
#include "../platform/platform.h"
#include "../video/video_player.h"
#include "components.h"
#include "i18n.h"
#include "theme.h"

#include <algorithm>

namespace {
using theme::S;
const float MARGIN_X = 120;
float item_w() { return S(900); }
float item_h() { return S(84); }
float item_gap() { return S(16); }
float items_top() { return S(170); }
const float SIGN_OUT_CONFIRM_SECONDS = 3.0f;
} // namespace

SettingsScreen::SettingsScreen(Fonts &fonts, Navigator &nav) : fonts_(fonts), navigator_(nav) {
	avplayer_ = VideoPlayer::hardware_selected();
	refresh_items();
}

void SettingsScreen::refresh_items() {
	std::vector<Item> items;
	const bool armed = sign_out_armed_ > 0;
	items.push_back({ INTERFACE_LANGUAGE, i18n::tr(i18n::Text::INTERFACE_LANGUAGE), i18n::language_name() });
        items.push_back({ SIGN_OUT, armed ? i18n::tr(i18n::Text::SIGN_OUT_CONFIRM) : i18n::tr(i18n::Text::SIGN_OUT), stremio().user_email(), armed });
	// "◀ 110% ▶", with an arrow only where there's further to go.
	const int pct = theme::ui_scale_percent();
	items.push_back({ UI_SCALE, i18n::tr(i18n::Text::UI_SCALE),
		std::string(pct > theme::UI_SCALE_MIN ? "\xE2\x97\x80  " : "") + std::to_string(pct) + "%" +
				(pct < theme::UI_SCALE_MAX ? "  \xE2\x96\xB6" : "") });
#if defined(STREMIO_PS4)
	items.push_back({ VIDEO_PLAYER, i18n::tr(i18n::Text::VIDEO_PLAYER), avplayer_ ? i18n::tr(i18n::Text::PLAYER_HARDWARE) : i18n::tr(i18n::Text::PLAYER_FFMPEG) });
#endif
	for (size_t i = 0; i < items.size() && i < items_.size(); i++) items[i].focus = items_[i].focus;
	items_.swap(items);
}

// Left / Right on "UI scale": everything resizes at once.
void SettingsScreen::step_scale(int steps) {
	const int before = theme::ui_scale_percent();
	theme::set_ui_scale(before + steps * theme::UI_SCALE_STEP);
	if (theme::ui_scale_percent() == before) return;
	fonts_.resize();
	bar_.show_toast(std::string(i18n::tr(i18n::Text::UI_SCALE)) + " " + std::to_string(theme::ui_scale_percent()) + "%");
}

void SettingsScreen::activate() {
	const Action action = items_[index_].action;
	if (action == INTERFACE_LANGUAGE) {
                i18n::next_language();
	} else if (action == SIGN_OUT) {
		if (sign_out_armed_ > 0) {
			stremio().logout(); // the app switches to the sign-in screen
			return;
		}
		sign_out_armed_ = SIGN_OUT_CONFIRM_SECONDS;
	} else if (action == UI_SCALE) {
		step_scale((100 - theme::ui_scale_percent()) / theme::UI_SCALE_STEP); // Cross: back to 100%
	} else if (action == VIDEO_PLAYER) {
		avplayer_ = !avplayer_;
		VideoPlayer::select_hardware(avplayer_);
		bar_.show_toast(avplayer_ ? i18n::tr(i18n::Text::PLAYER_HARDWARE_TOAST) : i18n::tr(i18n::Text::PLAYER_FFMPEG_TOAST));
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
		if (items_[index_].action == UI_SCALE) {
			if (pressed & platform::BUTTON_LEFT) step_scale(-1);
			if (pressed & platform::BUTTON_RIGHT) step_scale(1);
		}
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
		r.text(fonts_.page_header, i18n::tr(i18n::Text::SETTINGS), MARGIN_X, S(96), theme::BASE_CONTENT);
	const float R = theme::radius_field();
	for (int i = 0; i < (int)items_.size(); i++) {
		const Item &item = items_[i];
		const float y = items_top() + i * (item_h() + item_gap());
		const Color tone = item.danger ? theme::ERROR : theme::PRIMARY;
		ui::begin_focus(r, MARGIN_X, y, item_w(), item_h(), R, item.focus, 1.03f, tone);
		r.rounded_rect(MARGIN_X, y, item_w(), item_h(), R, item.danger ? mix(theme::BASE_200, theme::ERROR, 0.2f) : theme::BASE_200);
		r.text(fonts_.card_title, item.label, MARGIN_X + S(28), y + item_h() / 2 + S(9), item.danger ? theme::ERROR : theme::BASE_CONTENT);
		const float vw = fonts_.body.measure(item.value);
		r.text(fonts_.body, item.value, MARGIN_X + item_w() - S(28) - vw, y + item_h() / 2 + S(8), theme::BASE_CONTENT_MUTED);
		ui::end_focus(r, MARGIN_X, y, item_w(), item_h(), R, item.focus);
	}
	const float info_y = items_top() + items_.size() * (item_h() + item_gap()) + S(40);
	r.text(fonts_.body, std::string("Stremio PS4 ") + APP_VERSION, MARGIN_X, info_y, theme::BASE_CONTENT_MUTED);
	bar_.draw(r, fonts_);
}
