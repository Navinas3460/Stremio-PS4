#include "settings_screen.h"

#include "../core/stremio.h"
#include "../platform/platform.h"
#include "../video/video_player.h"
#include "components.h"
#include "theme.h"

#include <algorithm>
#include <cstdio>

namespace {
using theme::S;
const float MARGIN_X = 120;
const float SIGN_OUT_CONFIRM_SECONDS = 3.0f;
} // namespace

SettingsScreen::SettingsScreen(Fonts &fonts, Navigator &nav) : fonts_(fonts), navigator_(nav) {
	avplayer_ = VideoPlayer::hardware_selected();
	autoplay_ = VideoPlayer::autoplay_selected();
	refresh_items();
}

void SettingsScreen::refresh_items() {
	std::vector<Item> items;
	const bool armed = sign_out_armed_ > 0;
	items.push_back({ armed ? "Press \xC3\x97 again to sign out" : "Sign out", stremio().user_email(), armed, false, false, 0 });

	const int pct = theme::ui_scale_percent();
	const float t = (float)(pct - theme::UI_SCALE_MIN_PCT) / (float)(theme::UI_SCALE_MAX_PCT - theme::UI_SCALE_MIN_PCT);
	items.push_back({ "App UI scale", std::to_string(pct) + "%", false, true, false, std::clamp(t, 0.0f, 1.0f) });

	const int hpct = theme::home_scale_percent();
	const float ht = (float)(hpct - theme::HOME_SCALE_MIN_PCT) / (float)(theme::HOME_SCALE_MAX_PCT - theme::HOME_SCALE_MIN_PCT);
	items.push_back({ "Home screen scale", std::to_string(hpct) + "%", false, true, false, std::clamp(ht, 0.0f, 1.0f) });

	const theme::DisplayResolution &disp = theme::current_display_resolution();
	items.push_back({ "Display resolution", disp.label, false, false, false, 0 });

	const int scheme = theme::color_scheme_index();
	items.push_back({ "Color scheme", theme::color_scheme_name(scheme), false, false, true, 0 });

#if defined(STREMIO_PS4)
	items.push_back({ "Video player", avplayer_ ? "PS4 hardware (1080p MP4 / H.264)" : "FFmpeg (1080p MKV, HEVC 10-bit, E-AC3)", false, false, false, 0 });
#endif
	items.push_back({ "Autoplay next episode", autoplay_ ? "Enabled (automatic series progression)" : "Disabled", false, false, false, 0 });
	for (size_t i = 0; i < items.size() && i < items_.size(); i++) items[i].focus = items_[i].focus;
	items_.swap(items);
}

void SettingsScreen::step_scale(int delta_pct) {
	const int prev = theme::ui_scale_percent();
	theme::set_ui_scale_percent(prev + delta_pct);
	if (theme::ui_scale_percent() != prev) {
		fonts_.apply_scale();
		bar_.show_toast("App UI scale: " + std::to_string(theme::ui_scale_percent()) + "%");
	}
}

void SettingsScreen::step_home_scale(int delta_pct) {
	const int prev = theme::home_scale_percent();
	theme::set_home_scale_percent(prev + delta_pct);
	if (theme::home_scale_percent() != prev) {
		bar_.show_toast("Home screen scale: " + std::to_string(theme::home_scale_percent()) + "%");
	}
}

void SettingsScreen::step_resolution(int delta) {
	const int count = theme::display_resolution_count();
	const int next = (theme::display_resolution_index() + delta + count) % count;
	theme::set_display_resolution(next);
	fonts_.apply_scale();
	bar_.show_toast(std::string("Display: ") + theme::current_display_resolution().label);
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
		// Pressing Cross on the App UI scale slider steps up, wrapping back to min from max.
		const int pct = theme::ui_scale_percent();
		const int next = pct >= theme::UI_SCALE_MAX_PCT ? theme::UI_SCALE_MIN_PCT : pct + theme::UI_SCALE_STEP_PCT;
		theme::set_ui_scale_percent(next);
		fonts_.apply_scale();
		bar_.show_toast("App UI scale: " + std::to_string(theme::ui_scale_percent()) + "%");
	} else if (index_ == 2) {
		// Pressing Cross on the Home screen scale slider steps up, wrapping back to min from max.
		const int hpct = theme::home_scale_percent();
		const int next = hpct >= theme::HOME_SCALE_MAX_PCT ? theme::HOME_SCALE_MIN_PCT : hpct + theme::HOME_SCALE_STEP_PCT;
		theme::set_home_scale_percent(next);
		bar_.show_toast("Home screen scale: " + std::to_string(theme::home_scale_percent()) + "%");
	} else if (index_ == 3) {
		preview_res_ = theme::display_resolution_index();
		res_modal_open_ = true;
	} else if (index_ == 4) {
		preview_scheme_ = theme::color_scheme_index();
		theme_modal_open_ = true;
#if defined(STREMIO_PS4)
	} else if (index_ == 5) {
		avplayer_ = !avplayer_;
		VideoPlayer::select_hardware(avplayer_);
		bar_.show_toast(avplayer_ ? "Videos will use PS4 hardware player (1080p MP4 / H.264 only)" : "Videos will use FFmpeg (1080p MKV, HEVC & E-AC3)");
	} else if (index_ == 6) {
#else
	} else if (index_ == 5) {
#endif
		autoplay_ = !autoplay_;
		VideoPlayer::select_autoplay(autoplay_);
		bar_.show_toast(autoplay_ ? "Autoplay enabled: TV series advance automatically" : "Autoplay disabled");
	}
}

void SettingsScreen::update(double dt, uint32_t pressed) {
	if (res_modal_open_) {
		if (pressed & (platform::BUTTON_BACK | platform::BUTTON_OPTIONS)) {
			res_modal_open_ = false;
			return;
		}
		const int count = theme::display_resolution_count();
		if (pressed & platform::BUTTON_UP) {
			preview_res_ = (preview_res_ - 1 + count) % count;
		}
		if (pressed & platform::BUTTON_DOWN) {
			preview_res_ = (preview_res_ + 1) % count;
		}
		if (pressed & platform::BUTTON_L1) {
			preview_res_ = std::max(0, preview_res_ - 5);
		}
		if (pressed & platform::BUTTON_R1) {
			preview_res_ = std::min(count - 1, preview_res_ + 5);
		}
		if (pressed & platform::BUTTON_ACCEPT) {
			theme::set_display_resolution(preview_res_);
			fonts_.apply_scale();
			res_modal_open_ = false;
			bar_.show_toast(std::string("Display resolution: ") + theme::current_display_resolution().label);
		}
		const float row_h = S(52), row_gap = S(8), list_h = S(390);
		res_scroll_ = ui::follow(res_scroll_, preview_res_ * (row_h + row_gap), row_h, list_h, dt);
		return;
	}

	if (theme_modal_open_) {
		if (pressed & (platform::BUTTON_BACK | platform::BUTTON_OPTIONS)) {
			theme_modal_open_ = false;
			return;
		}
		if (pressed & (platform::BUTTON_LEFT | platform::BUTTON_UP)) {
			preview_scheme_ = (preview_scheme_ - 1 + theme::color_scheme_count()) % theme::color_scheme_count();
		}
		if (pressed & (platform::BUTTON_RIGHT | platform::BUTTON_DOWN)) {
			preview_scheme_ = (preview_scheme_ + 1) % theme::color_scheme_count();
		}
		if (pressed & platform::BUTTON_ACCEPT) {
			theme::set_color_scheme(preview_scheme_);
			theme_modal_open_ = false;
			bar_.show_toast(std::string("Theme applied: ") + theme::color_scheme_name(theme::color_scheme_index()));
		}
		return;
	}

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
			if (pressed & platform::BUTTON_LEFT) step_home_scale(-theme::HOME_SCALE_STEP_PCT);
			if (pressed & platform::BUTTON_RIGHT) step_home_scale(theme::HOME_SCALE_STEP_PCT);
		} else if (index_ == 3) {
			if (pressed & platform::BUTTON_LEFT) step_resolution(-1);
			if (pressed & platform::BUTTON_RIGHT) step_resolution(1);
		} else if (index_ == 4) {
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
	const float item_w = S(980), item_h = S(68), item_gap = S(12), items_top = S(136);
	r.text(fonts_.page_header, "Settings", MARGIN_X, S(90), theme::BASE_CONTENT);
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
		} else if (i == 3) {
			const theme::DisplayResolution &d = theme::current_display_resolution();
			const float bw = ui::badge_width(fonts_.small, d.aspect_tag);
			ui::badge(r, fonts_.small, d.aspect_tag, vx - S(16) - bw, y + (item_h - ui::badge_height(fonts_.small)) / 2,
					theme::ACCENT.with_alpha(0.22f), theme::ACCENT);
		}
		ui::end_focus(r, MARGIN_X, y, item_w, item_h, R, item.focus);
	}
	const float info_y = items_top + items_.size() * (item_h + item_gap) + S(16);
	r.text(fonts_.body, std::string("Stremio PS4 v") + APP_VERSION, MARGIN_X, info_y, theme::BASE_CONTENT_MUTED);
	if (!on_bar_ && (index_ == 1 || index_ == 2 || index_ == 3 || index_ == 4)) {
		const char *action = index_ == 1 ? "Adjust App UI scale"
				: index_ == 2 ? "Adjust Home screen scale"
				: index_ == 3 ? "Cycle resolution" : "Change color scheme";
		const char *enter_label = index_ == 3 ? "All Resolutions (16:9 / 4:3 / 16:10)" : (index_ == 4 ? "Theme Gallery" : "Step");
		ui::hints(r, fonts_.small, { { "\xE2\x97\x80 \xE2\x96\xB6", action }, { "\xC3\x97", enter_label } },
				MARGIN_X, info_y + S(32));
#if defined(STREMIO_PS4)
	} else if (!on_bar_ && index_ == 5) {
		ui::hints(r, fonts_.small, { { "\xC3\x97", "Switch Video Backend" } }, MARGIN_X, info_y + S(32));
		r.text(fonts_.small, "Recommended: Keep on FFmpeg. Plays 1080p/720p MKV, HEVC 10-bit, and E-AC3 smoothly.", MARGIN_X, info_y + S(60), theme::ACCENT);
	} else if (!on_bar_ && index_ == 6) {
#else
	} else if (!on_bar_ && index_ == 5) {
#endif
		ui::hints(r, fonts_.small, { { "\xC3\x97", "Toggle Autoplay" } }, MARGIN_X, info_y + S(32));
		r.text(fonts_.small, "Note: Automatically finds matching stream and plays the next TV episode during end credits.", MARGIN_X, info_y + S(60), theme::ACCENT);
	}
	bar_.draw(r, fonts_);
	if (theme_modal_open_) {
		draw_theme_modal(r);
	}
	if (res_modal_open_) {
		draw_resolution_modal(r);
	}
}

void SettingsScreen::draw_resolution_modal(Renderer &r) {
	const float W = 1920.0f, H = 1080.0f;
	r.rect(0, 0, W, H, rgb(0x000000, 0.78f));

	const float modal_w = S(1040), modal_h = S(620);
	const float mx = (W - modal_w) / 2, my = (H - modal_h) / 2;
	const float R = theme::RADIUS_BOX;

	r.rounded_rect(mx - S(3), my - S(3), modal_w + S(6), modal_h + S(6), R + S(3), theme::PRIMARY.with_alpha(0.55f));
	r.rounded_rect(mx, my, modal_w, modal_h, R, theme::BASE_200);

	ui::badge(r, fonts_.small, "DISPLAY RESOLUTION & ASPECT RATIO SCALER", mx + S(36), my + S(26), theme::PRIMARY, theme::PRIMARY_CONTENT);
	r.text(fonts_.card_title, "Select Screen Resolution & Aspect Ratio", mx + S(36), my + S(80), theme::BASE_CONTENT);

	const int count = theme::display_resolution_count();
	char count_buf[32];
	snprintf(count_buf, sizeof(count_buf), "%d of %d", preview_res_ + 1, count);
	r.text(fonts_.body, count_buf, mx + modal_w - S(36) - fonts_.body.measure(count_buf), my + S(80), theme::BASE_CONTENT_MUTED);

	// Left column: scrollable list of all 19 common resolutions
	const float list_x = mx + S(36), list_y = my + S(108);
	const float list_w = S(580), list_h = S(420);
	const float row_h = S(52), row_gap = S(8);
	r.rounded_rect(list_x, list_y, list_w, list_h, theme::RADIUS_FIELD, theme::BASE_100);

	for (int i = 0; i < count; i++) {
		const float ry = list_y + S(10) + i * (row_h + row_gap) - res_scroll_;
		if (ry + row_h < list_y || ry > list_y + list_h) continue;
		const bool is_sel = (i == preview_res_);
		const bool is_active = (i == theme::display_resolution_index());
		const theme::DisplayResolution &d = theme::display_resolution(i);

		const float rx = list_x + S(10), rw = list_w - S(20);
		r.rounded_rect(rx, ry, rw, row_h, S(10), is_sel ? theme::PRIMARY : (is_active ? theme::BASE_300 : theme::BASE_200));
		if (is_active) {
			ui::status(r, rx + S(20), ry + row_h / 2, is_sel ? theme::PRIMARY_CONTENT : theme::ACCENT);
		}
		const float tx = rx + (is_active ? S(38) : S(18));
		r.text(fonts_.body, d.label, tx, ry + row_h / 2 + S(7), is_sel ? theme::PRIMARY_CONTENT : theme::BASE_CONTENT);

		const float bw = ui::badge_width(fonts_.small, d.aspect_tag);
		ui::badge(r, fonts_.small, d.aspect_tag, rx + rw - S(14) - bw, ry + (row_h - ui::badge_height(fonts_.small)) / 2,
				is_sel ? theme::BASE_100.with_alpha(0.35f) : theme::BASE_300,
				is_sel ? theme::PRIMARY_CONTENT : theme::ACCENT);
	}

	// Right column: live aspect ratio & viewport preview diagram
	const float prev_x = list_x + list_w + S(24), prev_y = list_y;
	const float prev_w = modal_w - S(72) - list_w - S(24), prev_h = list_h;
	r.rounded_rect(prev_x, prev_y, prev_w, prev_h, theme::RADIUS_FIELD, theme::BASE_100);

	const theme::DisplayResolution &sel = theme::display_resolution(preview_res_);
	r.text(fonts_.card_title, sel.aspect_tag + std::string(" Viewport Preview"), prev_x + S(24), prev_y + S(42), theme::BASE_CONTENT);

	// Outer 16:9 reference frame
	const float outer_w = prev_w - S(48), outer_h = outer_w * (9.0f / 16.0f);
	const float outer_x = prev_x + S(24), outer_y = prev_y + S(68);
	r.rounded_rect(outer_x, outer_y, outer_w, outer_h, S(8), rgb(0x000000));

	// Scaled active viewport inside outer frame
	float bvx = 0, bvy = 0, bvw = outer_w, bvh = outer_h;
	const float canvas_aspect = 16.0f / 9.0f;
	if (sel.aspect <= canvas_aspect) {
		bvh = outer_h * sel.safe_scale;
		bvw = bvh * sel.aspect;
	} else {
		bvw = outer_w * sel.safe_scale;
		bvh = bvw / sel.aspect;
	}
	bvx = outer_x + (outer_w - bvw) * 0.5f;
	bvy = outer_y + (outer_h - bvh) * 0.5f;
	r.rounded_rect(bvx - S(2), bvy - S(2), bvw + S(4), bvh + S(4), S(6), theme::ACCENT);
	r.rounded_rect(bvx, bvy, bvw, bvh, S(5), theme::BASE_200);

	char dim_buf[64];
	snprintf(dim_buf, sizeof(dim_buf), "%dx%d (%s)", sel.width, sel.height, sel.aspect_tag);
	r.text(fonts_.body, dim_buf, bvx + (bvw - fonts_.body.measure(dim_buf)) / 2, bvy + bvh / 2 + S(6), theme::BASE_CONTENT);

	// Details under preview box
	const float info_top = outer_y + outer_h + S(32);
	char scale_buf[96];
	snprintf(scale_buf, sizeof(scale_buf), "Safe Area: %d%%   \xC2\xB7   UI Adaptation: %d%%",
			(int)(sel.safe_scale * 100 + 0.5f), (int)(sel.ui_factor * 100 + 0.5f));
	r.text(fonts_.small, scale_buf, prev_x + S(24), info_top, theme::ACCENT);
	r.text(fonts_.small, "Aligns all UI screens, menus, subtitles, and", prev_x + S(24), info_top + S(32), theme::BASE_CONTENT_MUTED);
	r.text(fonts_.small, "video aspect ratio to fit your screen cleanly.", prev_x + S(24), info_top + S(54), theme::BASE_CONTENT_MUTED);

	ui::hints(r, fonts_.small,
			{ { "\xE2\x96\xB2 \xE2\x96\xBC", "Select Resolution" }, { "L1/R1", "Page" }, { "\xC3\x97", "Apply" }, { "\xE2\x97\x8B", "Close" } },
			mx + S(36), my + modal_h - S(24));
}

void SettingsScreen::draw_theme_modal(Renderer &r) {
	const float W = 1920.0f, H = 1080.0f;
	// Dark semi-transparent modal backdrop
	r.rect(0, 0, W, H, rgb(0x000000, 0.75f));

	const float modal_w = S(880), modal_h = S(580);
	const float mx = (W - modal_w) / 2, my = (H - modal_h) / 2;
	const float R = theme::RADIUS_BOX;

	// Glowing modal outline & container
	r.rounded_rect(mx - S(3), my - S(3), modal_w + S(6), modal_h + S(6), R + S(3), theme::PRIMARY.with_alpha(0.5f));
	r.rounded_rect(mx, my, modal_w, modal_h, R, theme::BASE_200);

	// Header badge & title
	ui::badge(r, fonts_.small, "THEME GALLERY & LIVE PREVIEW", mx + S(36), my + S(28), theme::PRIMARY, theme::PRIMARY_CONTENT);
	r.text(fonts_.card_title, theme::color_scheme_name(preview_scheme_), mx + S(36), my + S(84), theme::BASE_CONTENT);
	char count_buf[32];
	snprintf(count_buf, sizeof(count_buf), "%d of %d", preview_scheme_ + 1, theme::color_scheme_count());
	r.text(fonts_.body, count_buf, mx + modal_w - S(36) - fonts_.body.measure(count_buf), my + S(84), theme::BASE_CONTENT_MUTED);

	// Query colors for the theme being previewed
	const theme::SchemePreview p = theme::color_scheme_preview(preview_scheme_);

	// Preview Stage Box (simulates a TV canvas in this theme's colors)
	const float stage_x = mx + S(36), stage_y = my + S(114);
	const float stage_w = modal_w - S(72), stage_h = S(330);
	const float stage_r = theme::RADIUS_FIELD;
	r.rounded_rect(stage_x, stage_y, stage_w, stage_h, stage_r, p.base_100);

	// Inside stage: Mock Stremio card with active focus ring in theme's primary color
	const float card_x = stage_x + S(28), card_y = stage_y + S(28);
	const float card_w = S(280), card_h = S(170);
	// Focused glow ring using p.primary
	r.rounded_rect(card_x - S(4), card_y - S(4), card_w + S(8), card_h + S(8), stage_r + S(4), p.primary.with_alpha(0.6f));
	r.rounded_rect(card_x, card_y, card_w, card_h, stage_r, p.base_200);
	// Inner poster placeholder
	r.rounded_rect(card_x + S(12), card_y + S(12), card_w - S(24), S(96), S(8), p.base_300);
	// Badge on card in p.accent
	ui::badge(r, fonts_.small, "1080p FHD", card_x + S(20), card_y + S(20), p.accent, p.base_100);
	// Card title
	r.text(fonts_.card_title, "Sample Media", card_x + S(16), card_y + S(136), p.base_content);
	r.text(fonts_.small, "2026 \xC2\xB7 Series", card_x + S(16), card_y + S(158), p.base_content.with_alpha(0.65f));

	// Right side of stage: Buttons, Chips, and Progress bar
	const float right_x = stage_x + S(340);
	r.text(fonts_.body, "Theme Elements & Swatches:", right_x, stage_y + S(48), p.base_content);

	// Mock primary button in p.primary
	const float btn_w = S(180), btn_h = S(48);
	r.rounded_rect(right_x, stage_y + S(72), btn_w, btn_h, theme::RADIUS_SELECTOR, p.primary);
	r.text(fonts_.body, "Play Video", right_x + S(36), stage_y + S(72) + btn_h / 2 + S(6), p.base_content);

	// Mock accent chip in p.accent
	const float chip_x = right_x + btn_w + S(16);
	r.rounded_rect(chip_x, stage_y + S(72), S(140), btn_h, theme::RADIUS_SELECTOR, p.accent.with_alpha(0.25f));
	r.text(fonts_.body, "Active", chip_x + S(40), stage_y + S(72) + btn_h / 2 + S(6), p.accent);

	// Mock progress bar
	r.text(fonts_.small, "Timeline Progress (0:45 / 1:00)", right_x, stage_y + S(150), p.base_content.with_alpha(0.7f));
	ui::progress(r, right_x, stage_y + S(166), stage_w - S(368), S(8), 0.75f);

	// Color Swatches Row
	r.text(fonts_.small, "Color Palette:", right_x, stage_y + S(210), p.base_content.with_alpha(0.7f));
	const struct { const char *tag; Color c; } swatches[] = {
		{ "Base", p.base_100 }, { "Card", p.base_200 }, { "Primary", p.primary },
		{ "Trim", p.accent }, { "Alt", p.secondary }
	};
	const float dot_r = S(14), dot_gap = S(32);
	for (int k = 0; k < 5; k++) {
		const float sx = right_x + k * (dot_r * 2 + dot_gap) + dot_r;
		const float sy = stage_y + S(250);
		r.rounded_rect(sx - dot_r, sy - dot_r, dot_r * 2, dot_r * 2, dot_r, swatches[k].c);
		r.text(fonts_.small, swatches[k].tag, sx - fonts_.small.measure(swatches[k].tag) / 2, sy + dot_r + S(16), p.base_content.with_alpha(0.75f));
	}

	// 15 theme pagination dots under stage
	const float dot_spacing = S(18);
	const float dots_start = mx + (modal_w - (theme::color_scheme_count() * dot_spacing)) / 2;
	for (int k = 0; k < theme::color_scheme_count(); k++) {
		const float dx = dots_start + k * dot_spacing;
		const float dy = my + modal_h - S(68);
		const bool is_cur = (k == preview_scheme_);
		r.rounded_rect(dx, dy, is_cur ? S(18) : S(8), S(8), S(4), is_cur ? theme::PRIMARY : theme::BASE_CONTENT_MUTED.with_alpha(0.35f));
	}

	// Bottom controller hints
	ui::hints(r, fonts_.small, { { "\xE2\x97\x80 \xE2\x96\xB6", "Browse Themes" }, { "\xC3\x97", "Apply Theme" }, { "\xE2\x97\x8B", "Close" } },
			mx + S(36), my + modal_h - S(20));
}
