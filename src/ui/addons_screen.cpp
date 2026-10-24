#include "addons_screen.h"

#include "../core/stremio.h"
#include "../net/images.h"
#include "../net/urls.h"
#include "../platform/platform.h"
#include "components.h"
#include "theme.h"

#include <algorithm>
#include <cctype>

namespace {

using theme::S;

const float W = 1920;
const float MARGIN_X = 120;

std::string pretty_word(const std::string &s) {
	if (s.empty()) return "Other";
	if (s == "tv") return "TV";
	std::string out = s;
	out[0] = (char)toupper((unsigned char)out[0]);
	for (size_t i = 1; i < out.size(); i++) {
		if (out[i] == '_' || out[i] == '-') out[i] = ' ';
	}
	return out;
}

std::string resource_label(const std::string &res) {
	if (res == "stream") return "Streams";
	if (res == "catalog") return "Catalogs";
	if (res == "subtitles") return "Subtitles";
	if (res == "meta") return "Meta";
	return pretty_word(res);
}

bool addon_has_resource(const Addon &a, const std::string &filter) {
	if (filter.empty()) return true;
	for (const std::string &r : a.resource_names()) {
		if (r == filter) return true;
	}
	return false;
}

} // namespace

AddonsScreen::AddonsScreen(Fonts &fonts, Navigator &nav) : fonts_(fonts), navigator_(nav) {
	chips_ = {
		{ "installed", "Installed", 0 },
		{ "community", "Community", 0 },
		{ "", "All Types", 0 },
		{ "stream", "Streams", 0 },
		{ "catalog", "Catalogs", 0 },
		{ "subtitles", "Subtitles", 0 },
		{ "meta", "Metadata", 0 },
		{ "sync", "Sync", 0 },
	};
	Stremio &s = stremio();
	s.fetch_community_addons();
	seen_addons_rev_ = s.addons_rev;
	seen_community_rev_ = s.community_addons_rev;
	rebuild_rows();
	if (rows_.empty()) zone_ = ZONE_CHIPS;
}

void AddonsScreen::rebuild_rows() {
	Stremio &s = stremio();
	const std::vector<Addon> &source = browse_community_ ? s.community_addons() : s.addons();
	std::vector<Row> next;
	for (const Addon &a : source) {
		if (addon_has_resource(a, active_filter_)) {
			next.push_back(Row{ a, s.is_addon_installed(a), 0.0f });
		}
	}
	for (size_t i = 0; i < next.size() && i < rows_.size(); i++) {
		next[i].focus = rows_[i].focus;
	}
	rows_.swap(next);
	if (rows_.empty()) {
		selected_ = 0;
		scroll_ = 0;
	} else {
		selected_ = std::clamp(selected_, 0, (int)rows_.size() - 1);
	}
}

void AddonsScreen::sync_account() {
	stremio().refresh();
	stremio().fetch_community_addons();
	bar_.show_toast("Syncing add-ons with your Stremio account\xE2\x80\xA6");
}

void AddonsScreen::toggle_selected_addon() {
	if (rows_.empty() || selected_ < 0 || selected_ >= (int)rows_.size()) return;
	const Row &row = rows_[selected_];
	std::weak_ptr<int> alive = alive_;
	if (row.installed) {
		if (row.addon.is_protected || row.addon.id == "com.linvo.cinemeta") {
			bar_.show_toast(row.addon.name + " is a protected core add-on and cannot be removed");
			return;
		}
		stremio().uninstall_addon(row.addon, [this, alive](bool, const std::string &msg) {
			if (alive.expired()) return;
			rebuild_rows();
			if (!msg.empty()) bar_.show_toast(msg);
		});
	} else {
		bar_.show_toast("Installing " + row.addon.name + "\xE2\x80\xA6");
		stremio().install_addon(row.addon, [this, alive](bool, const std::string &msg) {
			if (alive.expired()) return;
			rebuild_rows();
			if (!msg.empty()) bar_.show_toast(msg);
		});
	}
}

void AddonsScreen::update(double dt, uint32_t pressed) {
	if (bar_.handle_shoulders(pressed, fonts_, navigator_)) return;

	Stremio &s = stremio();
	if (s.addons_rev != seen_addons_rev_ || s.community_addons_rev != seen_community_rev_) {
		seen_addons_rev_ = s.addons_rev;
		seen_community_rev_ = s.community_addons_rev;
		rebuild_rows();
	}

	if (pressed & platform::BUTTON_TRIANGLE) {
		sync_account();
	}
	if (pressed & platform::BUTTON_SQUARE) {
		browse_community_ = !browse_community_;
		selected_ = 0;
		scroll_ = 0;
		if (browse_community_) s.fetch_community_addons();
		rebuild_rows();
		bar_.show_toast(browse_community_ ? "Showing Community Add-ons" : "Showing Installed Add-ons");
	}

	if (zone_ == ZONE_BAR) {
		if (bar_.handle_focused(pressed, fonts_, navigator_)) return;
		if (pressed & platform::BUTTON_UP) {
			zone_ = rows_.empty() ? ZONE_CHIPS : ZONE_LIST;
		}
	} else if (zone_ == ZONE_CHIPS) {
		if (pressed & platform::BUTTON_LEFT) chip_focus_ = std::max(0, chip_focus_ - 1);
		if (pressed & platform::BUTTON_RIGHT) chip_focus_ = std::min((int)chips_.size() - 1, chip_focus_ + 1);
		if (pressed & platform::BUTTON_DOWN) {
			zone_ = rows_.empty() ? ZONE_BAR : ZONE_LIST;
		}
		if (pressed & platform::BUTTON_ACCEPT) {
			const Chip &c = chips_[chip_focus_];
			if (c.id == "sync") {
				sync_account();
			} else if (c.id == "installed") {
				if (browse_community_) {
					browse_community_ = false;
					selected_ = 0;
					scroll_ = 0;
					rebuild_rows();
				}
			} else if (c.id == "community") {
				if (!browse_community_) {
					browse_community_ = true;
					selected_ = 0;
					scroll_ = 0;
					s.fetch_community_addons();
					rebuild_rows();
				}
			} else if (active_filter_ != c.id) {
				active_filter_ = c.id;
				selected_ = 0;
				scroll_ = 0;
				rebuild_rows();
			}
		}
	} else if (zone_ == ZONE_LIST) {
		if (pressed & platform::BUTTON_UP) {
			if (selected_ > 0) {
				selected_--;
			} else {
				zone_ = ZONE_CHIPS;
			}
		}
		if (pressed & platform::BUTTON_DOWN) {
			if (selected_ + 1 < (int)rows_.size()) {
				selected_++;
			} else {
				zone_ = ZONE_BAR;
			}
		}
		if (pressed & platform::BUTTON_LEFT) {
			zone_ = ZONE_CHIPS;
		}
		if (pressed & platform::BUTTON_ACCEPT) {
			toggle_selected_addon();
		}
	}

	for (int i = 0; i < (int)chips_.size(); i++) {
		const float t = (zone_ == ZONE_CHIPS && i == chip_focus_) ? 1.0f : 0.0f;
		chips_[i].focus = ui::approach(chips_[i].focus, t, dt);
	}
	for (int i = 0; i < (int)rows_.size(); i++) {
		const float t = (zone_ == ZONE_LIST && i == selected_) ? 1.0f : 0.0f;
		rows_[i].focus = ui::approach(rows_[i].focus, t, dt);
	}

	const float list_top = S(202);
	const float list_h = tabs::CONTENT_BOTTOM - list_top;
	const float row_h = S(92), row_gap = S(12);
	if (!rows_.empty()) {
		scroll_ = ui::follow(scroll_, selected_ * (row_h + row_gap), row_h, list_h, dt);
	}
	bar_.update(dt, zone_ == ZONE_BAR);
}

void AddonsScreen::draw(Renderer &r) {
	Stremio &s = stremio();

	const float header_y = S(92);
	const float chip_y = S(122), chip_h = S(50), chip_gap = S(10);
	const float list_top = S(202);
	const float bottom = tabs::CONTENT_BOTTOM;

	auto draw_header_and_chips = [&]() {
		const std::string header_title = browse_community_ ? "Community Addons" : "Installed Addons";
		const float title_w = r.text(fonts_.page_header, header_title, MARGIN_X, header_y, theme::BASE_CONTENT);
		const std::string count_str = browse_community_
				? (std::to_string(s.community_addons().size()) + " available \xC2\xB7 " + std::to_string(s.addons().size()) + " installed")
				: (std::to_string(s.addons().size()) + " installed");
		ui::badge(r, fonts_.small, count_str, MARGIN_X + title_w + S(20),
				header_y - fonts_.page_header.size() * 0.36f - ui::badge_height(fonts_.small) / 2,
				browse_community_ ? theme::SECONDARY : theme::BASE_300,
				browse_community_ ? theme::PRIMARY_CONTENT : theme::BASE_CONTENT);

		float cx = MARGIN_X;
		for (int i = 0; i < (int)chips_.size(); i++) {
			const Chip &c = chips_[i];
			const float w = ui::chip_width(fonts_.body, c.label);
			const bool is_sync = c.id == "sync";
			const float x = is_sync ? std::max(cx + S(12), W - MARGIN_X - w) : cx;
			bool active = false;
			if (c.id == "installed") active = !browse_community_;
			else if (c.id == "community") active = browse_community_;
			else if (!is_sync) active = (c.id == active_filter_);
			ui::chip(r, fonts_.body, c.label, x, chip_y, w, chip_h, active, c.focus);
			if (!is_sync) cx += w + chip_gap;
		}
	};

	draw_header_and_chips();

	if (!browse_community_ && !s.addons_loaded()) {
		const std::string msg = "Loading installed add-ons\xE2\x80\xA6";
		r.text(fonts_.section, msg, (W - fonts_.section.measure(msg)) / 2, (list_top + bottom) / 2, theme::BASE_CONTENT_MUTED);
		bar_.draw(r, fonts_);
		return;
	}
	if (browse_community_ && s.community_addons().empty() && s.community_addons_loading()) {
		const std::string msg = "Loading community add-ons catalog\xE2\x80\xA6";
		r.text(fonts_.section, msg, (W - fonts_.section.measure(msg)) / 2, (list_top + bottom) / 2, theme::BASE_CONTENT_MUTED);
		bar_.draw(r, fonts_);
		return;
	}

	if (rows_.empty()) {
		const std::string msg = !s.addons_error().empty()
				? ("Couldn't load add-ons: " + s.addons_error())
				: (browse_community_ ? "No community add-ons match this filter" : "No installed add-ons match this filter");
		r.text(fonts_.card_title, msg, MARGIN_X, list_top + S(60),
				!s.addons_error().empty() ? theme::ERROR : theme::BASE_CONTENT_MUTED);
		bar_.draw(r, fonts_);
		return;
	}

	const float list_w = S(760);
	const float row_h = S(92), row_gap = S(12);
	const float R = theme::RADIUS_FIELD;

	// Draw unfocused list rows first, then the focused row on top.
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < (int)rows_.size(); i++) {
			const Row &row = rows_[i];
			if ((pass == 0) == (row.focus > 0.5f)) continue;
			const float y = list_top + i * (row_h + row_gap) - scroll_;
			if (y + row_h < list_top - S(10) || y > bottom + S(10)) continue;

			const bool is_sel = (i == selected_);
			ui::begin_focus(r, MARGIN_X, y, list_w, row_h, R, row.focus, 1.03f, theme::PRIMARY);
			r.rounded_rect(MARGIN_X, y, list_w, row_h, R,
					is_sel ? mix(theme::BASE_200, theme::BASE_300, 0.65f) : theme::BASE_200);

			// Add-on icon or initial avatar
			const float icon_sz = S(56);
			const float icon_x = MARGIN_X + S(18), icon_y = y + (row_h - icon_sz) / 2;
			const Texture logo_tex = !row.addon.logo.empty() ? images::get(row.addon.logo) : Texture();
			if (logo_tex.valid()) {
				r.rounded_rect(icon_x, icon_y, icon_sz, icon_sz, S(12), theme::BASE_300);
				r.image_cover(logo_tex, icon_x, icon_y, icon_sz, icon_sz);
				r.round_corners(icon_x, icon_y, icon_sz, icon_sz, S(12),
						is_sel ? mix(theme::BASE_200, theme::BASE_300, 0.65f) : theme::BASE_200);
			} else {
				ui::avatar(r, fonts_.card_title, row.addon.name, icon_x, icon_y, icon_sz);
			}

			const float tx = icon_x + icon_sz + S(18);
			const float right_edge = MARGIN_X + list_w - S(20);

			// Status / Version badge on the right of the top line
			float badge_w = 0;
			if (row.installed && browse_community_) {
				badge_w = ui::badge_width(fonts_.small, "Installed");
				ui::badge(r, fonts_.small, "Installed", right_edge - badge_w, y + S(16), theme::ACCENT, theme::ACCENT_CONTENT);
			} else if (row.addon.is_protected) {
				badge_w = ui::badge_width(fonts_.small, "Official");
				ui::badge(r, fonts_.small, "Official", right_edge - badge_w, y + S(16), theme::SECONDARY, theme::PRIMARY_CONTENT);
			} else if (!row.addon.version.empty()) {
				const std::string v = "v" + row.addon.version;
				badge_w = ui::badge_width(fonts_.small, v);
				ui::badge(r, fonts_.small, v, right_edge - badge_w, y + S(16), theme::BASE_300, theme::BASE_CONTENT_MUTED);
			}

			const float name_max = std::max(S(100), right_edge - badge_w - S(12) - tx);
			r.text(fonts_.card_title, fonts_.card_title.ellipsize(row.addon.name, name_max), tx, y + S(38), theme::BASE_CONTENT);

			// Second line: types + resources summary
			std::string sub;
			for (size_t t = 0; t < row.addon.types.size(); t++) {
				if (t) sub += " \xC2\xB7 ";
				sub += pretty_word(row.addon.types[t]);
			}
			const std::vector<std::string> res = row.addon.resource_names();
			if (!res.empty()) {
				if (!sub.empty()) sub += "   \xE2\x80\x94   ";
				for (size_t k = 0; k < res.size(); k++) {
					if (k) sub += ", ";
					sub += resource_label(res[k]);
				}
			}
			r.text(fonts_.small, fonts_.small.ellipsize(sub, right_edge - tx), tx, y + S(68), theme::BASE_CONTENT_MUTED);

			ui::end_focus(r, MARGIN_X, y, list_w, row_h, R, is_sel ? 1.0f : row.focus);
		}
	}

	// Mask above and below the scrollable list
	r.rect(0, 0, list_w + MARGIN_X + S(20), list_top - S(4), theme::BASE_100);
	draw_header_and_chips();

	// Right column: Detail Inspector for the selected add-on
	const Row &sel_row = rows_[selected_];
	const Addon &sel = sel_row.addon;
	const float panel_x = MARGIN_X + list_w + S(40);
	const float panel_w = W - MARGIN_X - panel_x;
	const float panel_h = bottom - list_top - S(12);
	r.rounded_rect(panel_x, list_top, panel_w, panel_h, theme::RADIUS_BOX, theme::BASE_200);

	const float pad = S(32);
	const float ix = panel_x + pad;
	const float iw = panel_w - 2 * pad;
	float iy = list_top + pad + S(28);

	// Title + Version / Installed badge
	r.text(fonts_.section, fonts_.section.ellipsize(sel.name, iw - S(160)), ix, iy, theme::BASE_CONTENT);
	if (sel_row.installed) {
		const std::string lbl = sel.is_protected ? "Protected" : "Installed";
		const float bw = ui::badge_width(fonts_.small, lbl);
		ui::badge(r, fonts_.small, lbl, panel_x + panel_w - pad - bw, iy - S(24), theme::ACCENT, theme::ACCENT_CONTENT);
	} else if (!sel.version.empty()) {
		const std::string v = "v" + sel.version;
		const float bw = ui::badge_width(fonts_.small, v);
		ui::badge(r, fonts_.small, v, panel_x + panel_w - pad - bw, iy - S(24), theme::PRIMARY, theme::PRIMARY_CONTENT);
	}
	iy += S(30);

	// Addon ID & Version
	if (!sel.id.empty()) {
		std::string id_line = sel.id;
		if (!sel.version.empty()) id_line += "  \xC2\xB7  v" + sel.version;
		r.text(fonts_.small, fonts_.small.ellipsize(id_line, iw), ix, iy, theme::BASE_CONTENT_MUTED);
		iy += S(34);
	}

	// Description (up to 3 wrapped lines)
	if (!sel.description.empty()) {
		const std::vector<std::string> lines = fonts_.body.wrap(sel.description, iw);
		for (size_t i = 0; i < lines.size() && i < 3; i++) {
			r.text(fonts_.body, lines[i], ix, iy, theme::BASE_CONTENT);
			iy += S(28);
		}
		iy += S(14);
	}

	// Capabilities / Resources badges
	const std::vector<std::string> res = sel.resource_names();
	if (!res.empty()) {
		r.text(fonts_.small, "CAPABILITIES", ix, iy, theme::BASE_CONTENT_MUTED);
		iy += S(12);
		float bx = ix;
		for (const std::string &rn : res) {
			const std::string lbl = resource_label(rn);
			const float bw = ui::badge(r, fonts_.small, lbl, bx, iy, theme::ACCENT, theme::ACCENT_CONTENT);
			bx += bw + S(10);
			if (bx > ix + iw - S(80)) break;
		}
		iy += ui::badge_height(fonts_.small) + S(24);
	}

	// Supported Content Types badges
	if (!sel.types.empty()) {
		r.text(fonts_.small, "CONTENT TYPES", ix, iy, theme::BASE_CONTENT_MUTED);
		iy += S(12);
		float bx = ix;
		for (const std::string &t : sel.types) {
			const std::string lbl = pretty_word(t);
			const float bw = ui::badge(r, fonts_.small, lbl, bx, iy, theme::BASE_300, theme::BASE_CONTENT);
			bx += bw + S(10);
			if (bx > ix + iw - S(80)) break;
		}
		iy += ui::badge_height(fonts_.small) + S(24);
	}

	// Catalogs provided
	if (!sel.catalogs.empty() && iy + S(60) < list_top + panel_h - S(110)) {
		r.text(fonts_.small, "CATALOGS (" + std::to_string(sel.catalogs.size()) + ")", ix, iy, theme::BASE_CONTENT_MUTED);
		iy += S(26);
		for (size_t i = 0; i < sel.catalogs.size() && i < 3; i++) {
			if (iy > list_top + panel_h - S(110)) break;
			const Catalog &cat = sel.catalogs[i];
			std::string line = pretty_word(cat.type) + " \xC2\xB7 " + (cat.name.empty() ? cat.id : cat.name);
			if (!cat.extra.empty()) {
				line += "  (";
				for (size_t e = 0; e < cat.extra.size(); e++) {
					if (e) line += ", ";
					line += cat.extra[e].name;
				}
				line += ")";
			}
			r.text(fonts_.body, fonts_.body.ellipsize(line, iw), ix, iy, theme::BASE_CONTENT);
			iy += S(28);
		}
	}

	// Action button banner at the bottom of the inspector panel
	const float action_y = list_top + panel_h - S(96);
	if (sel_row.installed) {
		if (sel.is_protected || sel.id == "com.linvo.cinemeta") {
			ui::badge(r, fonts_.body, "Official Core Add-on (Always Installed)", ix, action_y, theme::BASE_300, theme::BASE_CONTENT_MUTED);
		} else {
			ui::badge(r, fonts_.body, "Press \xC3\x97 to Uninstall / Delete Add-on", ix, action_y, theme::ERROR, theme::BASE_100);
		}
	} else {
		ui::badge(r, fonts_.body, "Press \xC3\x97 to Install Add-on", ix, action_y, theme::PRIMARY, theme::PRIMARY_CONTENT);
	}

	// Transport URL at the bottom of the inspector panel
	if (!sel.transport_url.empty()) {
		const float url_y = list_top + panel_h - S(22);
		const std::string clean_url = urls::for_log(sel.transport_url);
		r.text(fonts_.small, fonts_.small.ellipsize("Manifest: " + clean_url, iw), ix, url_y, theme::BASE_CONTENT_MUTED);
	}

	const std::string x_hint = sel_row.installed ? (sel.is_protected ? "Protected" : "Uninstall") : "Install";
	ui::hints(r, fonts_.small,
			{ { "\xC3\x97", x_hint },
					{ "\xE2\x96\xA1", browse_community_ ? "Installed Addons" : "Community Addons" },
					{ "\xE2\x96\xB3", "Sync account" } },
			panel_x, list_top - S(18));

	bar_.draw(r, fonts_);
}
