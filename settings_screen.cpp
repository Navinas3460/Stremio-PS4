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
const float SIGN_OUT_CONFIRM_SECONDS = 3.0f;
} // namespace

SettingsScreen::SettingsScreen(Fonts &fonts, Navigator &nav) : fonts_(fonts), navigator_(nav) {
	avplayer_ = VideoPlayer::hardware_selected();
	pro_4k_ = VideoPlayer::pro_4k_selected();
	refresh_items();
}

void SettingsScreen::refresh_items() {
	std::vector<Item> items;
	const bool armed = sign_out_armed_ > 0;
	items.push_back({ armed ? "Press \xC3\x97 again to sign out" : "Sign out", stremio().user_email(), armed, false, false, 0 });

	const int pct = theme::ui_scale_percent();
	const float t = (float)(pct - theme::UI_SCALE_MIN_PCT) / (float)(theme::UI_SCALE_MAX_PCT - theme::UI_SCALE_MIN_PCT);
	items.push_back({ "UI scale", std::to_string(pct) + "%", false, true, false, std::clamp(t, 0.0f, 1.0f) });

	const int scheme = theme::color_scheme_index();
	items.push_back({ "Color scheme", theme::color_scheme_name(scheme), false, false, true, 0 });

#if defined(STREMIO_PS4)
	items.push_back({ "Video player", avplayer_ ? "PS4 hardware (MP4 / HEVC)" : "FFmpeg (plays everything)", false, false, false, 0 });
	items.push_back({ "4K Video Output (PS4 Pro)", pro_4k_ ? "Enabled (3840x2160 hardware VPU)" : "Disabled (1080p standard)", false, false, false, 0 });
#endif
	for (size_t i = 0; i < items.size() && i < items_.size(); i++) items[i].focus = items_[i].focus;
	items_.swap(items);
}

void SettingsScreen::step_scale(int delta_pct) {
	const int prev = theme::ui_scale_percent();
	theme::set_ui_scale_percent(prev + delta_pct);
	if (theme::ui_scale_percent() != prev) {
		fonts_.apply_scale();
		bar_.show_toast("UI scale: " + std::to_string(theme::ui_scale_percent()) + "%");
	}
}

void SettingsScreen::step_scheme(int delta) {
	const int next = (theme::color_scheme_index() + delta + theme::color_scheme_count()) % theme::color_scheme_count();
	theme::set_color_scheme(next);
	bar_.show_toast(std::string("Theme: ") + theme::color_scheme_name(theme::color_scheme_index()));
}

void SettingsScreen::activate() {
	if (index_ == 0) {
		if (sign_out_armed_ > 0) {
			stremio().logout(); // the app switches to the sign-in screen
			return;
		}
		sign_out_armed_ = SIGN_OUT_CONFIRM_SECONDS;
	} else if (index_ == 1) {
		// Pressing Cross on the slider steps up, wrapping back to 100% from max.
		const int pct = theme::ui_scale_percent();
		const int next = pct >= theme::UI_SCALE_MAX_PCT ? 100 : pct + theme::UI_SCALE_STEP_PCT;
		theme::set_ui_scale_percent(next);
		fonts_.apply_scale();
		bar_.show_toast("UI scale: " + std::to_string(theme::ui_scale_percent()) + "%");
	} else if (index_ == 2) {
		step_scheme(1);
	} else if (index_ == 3) {
		avplayer_ = !avplayer_;
		VideoPlayer::select_hardware(avplayer_);
		bar_.show_toast(avplayer_ ? "Videos will use PS4 hardware player (MP4 / HEVC)" : "Videos will use FFmpeg");
	} else if (index_ == 4) {
		pro_4k_ = !pro_4k_;
		VideoPlayer::select_pro_4k(pro_4k_);
		if (pro_4k_) avplayer_ = true;
		bar_.show_toast(pro_4k_ ? "4K video output enabled (PS4 Pro only - UI stays 1080p)" : "4K video output disabled (1080p mode)");
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
		if (index_ == 1) {
			if (pressed & platform::BUTTON_LEFT) step_scale(-theme::UI_SCALE_STEP_PCT);
			if (pressed & platform::BUTTON_RIGHT) step_scale(theme::UI_SCALE_STEP_PCT);
		} else if (index_ == 2) {
			if (pressed & platform::BUTTON_LEFT) step_scheme(-1);
			if (pressed & platform::BUTTON_RIGHT) step_scheme(1);
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
	const float item_w = S(900), item_h = S(84), item_gap = S(16), items_top = S(170);
	r.text(fonts_.page_header, "Settings", MARGIN_X, S(96), theme::BASE_CONTENT);
	const float R = theme::RADIUS_FIELD;
	for (int i = 0; i < (int)items_.size(); i++) {
		const Item &item = items_[i];
		const float y = items_top + i * (item_h + item_gap);
		const Color tone = item.danger ? theme::ERROR : theme::PRIMARY;
		ui::begin_focus(r, MARGIN_X, y, item_w, item_h, R, item.focus, 1.03f, tone);
		r.rounded_rect(MARGIN_X, y, item_w, item_h, R, item.danger ? mix(theme::BASE_200, theme::ERROR, 0.2f) : theme::BASE_200);
		r.text(fonts_.card_title, item.label, MARGIN_X + S(28), y + item_h / 2 + S(9), item.danger ? theme::ERROR : theme::BASE_CONTENT);
		const float vw = fonts_.body.measure(item.value);
		const float vx = MARGIN_X + item_w - S(28) - vw;
		r.text(fonts_.body, item.value, vx, y + item_h / 2 + S(8), theme::BASE_CONTENT_MUTED);
		if (item.slider) {
			const float bar_w = S(240), bar_h = S(10);
			const float bar_x = vx - S(24) - bar_w;
			const float bar_y = y + (item_h - bar_h) / 2;
			ui::progress(r, bar_x, bar_y, bar_w, bar_h, item.slider_progress);
			const float knob_r = S(9);
			const float knob_cx = bar_x + item.slider_progress * bar_w;
			r.rounded_rect(knob_cx - knob_r, y + item_h / 2 - knob_r, knob_r * 2, knob_r * 2, knob_r, theme::PRIMARY_CONTENT);
		} else if (item.swatches) {
			const Color sw[4] = { theme::PRIMARY, theme::SECONDARY, theme::ACCENT, theme::BASE_300 };
			const float dot = S(20), gap = S(10);
			float sx = vx - S(24) - (4 * dot + 3 * gap);
			const float sy = y + (item_h - dot) / 2;
			for (int k = 0; k < 4; k++) {
				r.rounded_rect(sx, sy, dot, dot, dot / 2, sw[k]);
				sx += dot + gap;
			}
		}
		ui::end_focus(r, MARGIN_X, y, item_w, item_h, R, item.focus);
	}
	const float info_y = items_top + items_.size() * (item_h + item_gap) + S(28);
	r.text(fonts_.body, std::string("Stremio PS4 Enhanced v") + APP_VERSION, MARGIN_X, info_y, theme::BASE_CONTENT_MUTED);
	if (!on_bar_ && (index_ == 1 || index_ == 2)) {
		const char *action = index_ == 1 ? "Adjust scale" : "Change color scheme";
		ui::hints(r, fonts_.small, { { "\xE2\x97\x80 \xE2\x96\xB6", action }, { "\xC3\x97", "Next" } },
				MARGIN_X, info_y + S(40));
	} else if (!on_bar_ && index_ == 4) {
		ui::hints(r, fonts_.small, { { "\xC3\x97", "Toggle 4K Video Mode" } }, MARGIN_X, info_y + S(40));
		r.text(fonts_.small, "Note: PS4 Pro only. Video plays at native 4K; UI stays 1080p to prevent menu lag.", MARGIN_X, info_y + S(72), theme::ACCENT);
		r.text(fonts_.small, "Will work (4K Hardware): MP4, M4V, MOV, TS \xC2\xB7 H.264 & H.265/HEVC \xC2\xB7 AAC, AC3, E-AC3", MARGIN_X, info_y + S(98), theme::ACCENT);
		r.text(fonts_.small, "Will NOT work: AV1, VP9/WebM, 10-bit H.264 (Hi10P), raw 4K MKV software decoding (causes lag)", MARGIN_X, info_y + S(124), theme::ERROR);
	}
	bar_.draw(r, fonts_);
}
