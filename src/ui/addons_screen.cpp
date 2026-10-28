#include "addons_screen.h"

#include "../core/stremio.h"
#include "../core/addon_search.h"
#include "../core/addon_config.h"
#include "../net/images.h"
#include "../net/http.h"
#include "../platform/platform.h"
#include "components.h"
#include "i18n.h"
#include "theme.h"

#include <algorithm>
#include <string>
#include <utility>

namespace {
using theme::S;
const float W = 1920;
const float MARGIN = 120;

constexpr char SEARCH_CHARACTERS[] = "abcdefghijklmnopqrstuvwxyz0123456789";
constexpr int SEARCH_CHARACTER_COUNT = sizeof(SEARCH_CHARACTERS) - 1;
constexpr int SEARCH_SPACE = SEARCH_CHARACTER_COUNT;
constexpr int SEARCH_DELETE = SEARCH_SPACE + 1;
constexpr int SEARCH_CLEAR = SEARCH_SPACE + 2;
constexpr int SEARCH_DONE = SEARCH_SPACE + 3;
constexpr int SEARCH_KEY_COUNT = SEARCH_DONE + 1;
constexpr std::string::size_type SEARCH_QUERY_LIMIT = 60;

float list_x() { return MARGIN; }
float list_y() { return S(260); }
float list_w() { return std::min(S(480), 560.0f); }
float row_h() { return S(82); }
float row_gap() { return S(12); }
float content_end() { return tabs::content_bottom() - S(68); }
float list_h() { return std::max(row_h(), content_end() - list_y()); }

bool declares_resource(const Addon &addon, const std::string &name) {
	if (name == "catalog" && !addon.catalogs.empty()) return true;
	for (const Json &r : addon.resources.elements()) {
		if (r.is_string() && r.as_string() == name) return true;
		if (r.is_object() && r["name"].as_string("") == name) return true;
	}
	return false;
}

std::string functions_label(const Addon &addon) {
	const struct {
		const char *resource;
		i18n::Text key;
	} entries[] = {
		{ "catalog", i18n::Text::ADDONS_RESOURCE_CATALOG },
		{ "meta", i18n::Text::ADDONS_RESOURCE_META },
		{ "stream", i18n::Text::ADDONS_RESOURCE_STREAM },
		{ "subtitles", i18n::Text::ADDONS_RESOURCE_SUBTITLES },
	};
	std::string result;
	for (const auto &entry : entries) {
		if (!declares_resource(addon, entry.resource)) continue;
		if (!result.empty()) result += " \xC2\xB7 ";
		result += i18n::tr(entry.key);
	}
	return result.empty() ? i18n::tr(i18n::Text::ADDONS_NO_FUNCTIONS) : result;
}

void draw_addon_logo(Renderer &r, Fonts &fonts, const Addon &addon,
		float x, float y, float side) {
	r.rounded_rect(x, y, side, side, theme::radius_field(), theme::BASE_300);

	const bool remote = addon.logo.rfind("https://", 0) == 0
		|| addon.logo.rfind("http://", 0) == 0;
	if (remote) {
		const Texture &texture = images::get(addon.logo);
		if (texture.valid() && texture.width > 0 && texture.height > 0) {
			const float pad = side * 0.08f;
			const float area = side - 2 * pad;
			const float scale = std::min(
				area / texture.width, area / texture.height);
			const float w = texture.width * scale;
			const float h = texture.height * scale;
			r.image_stretch(texture, x + (side - w) / 2,
				y + (side - h) / 2, w, h);
			return;
		}
	}

	const std::string mark = "+";
	r.text(fonts.section, mark,
		x + (side - fonts.section.measure(mark)) / 2,
		y + side / 2 + fonts.section.size() * 0.36f,
		theme::BASE_CONTENT_MUTED);
}

std::string display_name(const Addon &addon) {
	return addon.name.empty() ? addon.id : addon.name;
}

// Show only the server, never a configuration path, query or URL password.
std::string addon_server(const std::string &url) {
	const auto scheme = url.find("://");
	if (scheme == std::string::npos) return "";
	const auto begin = scheme + 3;
	const auto end = url.find_first_of("/?#", begin);
	std::string server = url.substr(begin,
		end == std::string::npos ? std::string::npos : end - begin);
	const auto at = server.rfind('@');
	if (at != std::string::npos) server.erase(0, at + 1);
	return server;
}
} // namespace

const std::vector<Addon> &AddonsScreen::current_addons() const {
	if (filter_ == FILTER_INSTALLED) return stremio().addons();
	const auto &state = *catalogs_[filter_ - FILTER_OFFICIAL];
	if (filter_ != FILTER_COMMUNITY || addon_query_.empty()) return state.items;
	if (filtered_revision_ != state.revision
		|| filtered_query_ != addon_query_) {
		filtered_addons_.clear();
		for (const Addon &addon : state.items) {
			if (addon_search::matches(addon.name, addon.id, addon_query_)) {
				filtered_addons_.push_back(addon);
			}
		}
		filtered_revision_ = state.revision;
		filtered_query_ = addon_query_;
	}
	return filtered_addons_;
}

void AddonsScreen::set_filter(int filter) {
	filter = std::clamp<int>(filter, FILTER_INSTALLED, FILTER_COMMUNITY);
	if (filter == filter_) return;
	filter_ = filter;
	selected_ = 0;
	scroll_ = 0;
	focus_.clear();
	load_catalog();
}

void AddonsScreen::load_catalog(bool force) {
	if (filter_ == FILTER_INSTALLED) return;
	const int index = filter_ - FILTER_OFFICIAL;
	auto state = catalogs_[index];
	if (state->loading || (state->loaded && !force)) return;

	state->loading = true;
	state->failed = false;

	// Public catalogs: community directory and official catalog.
	// No account credentials are sent to these endpoints.
	http::Request request;
	request.url = filter_ == FILTER_COMMUNITY
		? "https://stremio-addons.net/api/addon_catalog/all/stremio-addons.net.json"
		: "https://v3-cinemeta.strem.io/addon_catalog/all/official.json";
	request.max_body = 8 * 1024 * 1024;

	http::send(request, [state](http::Response &response) {
		state->loading = false;
		state->loaded = true;
		const auto parsed = std::static_pointer_cast<Json>(response.payload);
		if (!response.ok() || !parsed || !(*parsed)["addons"].is_array()) {
			state->failed = true;
			return;
		}

		std::vector<Addon> items;
		for (const Json &descriptor : (*parsed)["addons"].elements()) {
			if (!descriptor.is_object()
				|| !descriptor["manifest"].is_object()
				|| !descriptor["transportUrl"].is_string()) continue;

			Addon addon = Addon::from_json(descriptor);
			const bool http_url = addon.transport_url.rfind("https://", 0) == 0
				|| addon.transport_url.rfind("http://", 0) == 0;
			if (addon.id.empty() || !http_url) continue;

			const bool duplicate = std::any_of(items.begin(), items.end(),
				[&](const Addon &existing) {
					return existing.transport_url == addon.transport_url;
				});
			if (!duplicate) items.push_back(std::move(addon));
		}
		state->items.swap(items);
		++state->revision;
	}, [](http::Response &response) {
		if (!response.ok()) return;
		std::string error;
		Json parsed = Json::parse(response.body, &error);
		if (!error.empty() || !parsed.is_object()) return;
		response.payload = std::make_shared<Json>(parsed);
	});
}

void AddonsScreen::draw_filters(Renderer &r) {
	const i18n::Text labels[FILTER_COUNT] = {
		i18n::Text::ADDONS_FILTER_INSTALLED,
		i18n::Text::ADDONS_FILTER_OFFICIAL,
		i18n::Text::ADDONS_FILTER_COMMUNITY
	};
	const float y = S(168);
	const float pad = S(6);
	const float height = S(46);
	float widths[FILTER_COUNT];
	float total = 0;
	for (int i = 0; i < FILTER_COUNT; ++i) {
		widths[i] = std::max(S(150),
			ui::chip_width(fonts_.body, i18n::tr(labels[i])));
		total += widths[i];
	}
	const float mark_gap = S(16);
	const float box_x = MARGIN
		+ ui::kbd_width(fonts_.small, "L2") + mark_gap;
	const float box_w = total + 2 * pad;
	const float mark_y = y + pad + height / 2
		+ fonts_.small.size() * 0.36f;

	r.rounded_rect(box_x, y, box_w, height + 2 * pad,
		theme::radius_selector(), theme::BASE_200);
	ui::kbd(r, fonts_.small, "L2", MARGIN, mark_y);
	ui::kbd(r, fonts_.small, "R2",
		box_x + box_w + mark_gap, mark_y);

	float x = box_x + pad;
	for (int i = 0; i < FILTER_COUNT; ++i) {
		ui::chip(r, fonts_.body, i18n::tr(labels[i]), x, y + pad,
			widths[i], height, i == filter_,
			on_filters_ && i == filter_ ? 1.0f : 0.0f,
			theme::BASE_200);
		x += widths[i];
	}
	if (filter_ == FILTER_COMMUNITY) {
		const float hint_x = box_x + box_w + mark_gap
			+ ui::kbd_width(fonts_.small, "R2") + S(26);
		const float available = W - MARGIN - hint_x;
		if (available > S(40)) {
			std::string label = std::string("\xE2\x96\xB3 ")
				+ i18n::tr(i18n::Text::ADDONS_SEARCH);
			if (!addon_query_.empty()) label += ": " + addon_query_;
			r.text(fonts_.small,
				fonts_.small.ellipsize(label, available),
				hint_x, y + pad + height / 2
					+ fonts_.small.size() * 0.36f,
				theme::BASE_CONTENT_MUTED);
		}
	}
}

AddonsScreen::AddonsScreen(Fonts &fonts, Navigator &nav)
	: fonts_(fonts), navigator_(nav) {}

bool AddonsScreen::is_installed(const Addon &addon) const {
	const auto &installed = stremio().addons();
	return std::any_of(installed.begin(), installed.end(),
		[&](const Addon &item) {
			return item.transport_url == addon.transport_url;
		});
}

void AddonsScreen::activate_addon() {
	using i18n::Text;
	Stremio &s = stremio();

	if (operation_pending_ || s.addon_change_busy()) {
		toast_.show(i18n::tr(Text::ADDONS_WORKING));
		return;
	}
	if (!s.logged_in()) {
		toast_.show(i18n::tr(Text::ADDONS_SIGN_IN), true);
		return;
	}
	if (s.account_sync_busy()
		|| !s.addons_loaded() || !s.addons_error().empty()) {
		toast_.show(i18n::tr(Text::ADDONS_COLLECTION_UNAVAILABLE), true);
		return;
	}

	const auto &items = current_addons();
	if (selected_ < 0 || selected_ >= (int)items.size()) return;
	const Addon &addon = items[selected_];

	if (filter_ == FILTER_INSTALLED) {
		if (addon.is_protected) {
			toast_.show(i18n::tr(Text::ADDONS_PROTECTED), true);
			return;
		}
	} else {
		if (is_installed(addon)) {
			toast_.show(i18n::tr(Text::ADDONS_ALREADY_INSTALLED));
			return;
		}
	}

	// Freeze the exact target before displaying the confirmation.
	confirmed_addon_ = addon;
	confirm_auth_rev_ = s.auth_rev;
	confirm_install_ = filter_ != FILTER_INSTALLED;
	confirm_ok_ = false;
	config_option_ = 0;
	config_options_open_ = confirm_install_
		&& (addon.configurable || addon.configuration_required);
	confirming_ = !config_options_open_;
}

void AddonsScreen::execute_change() {
	using i18n::Text;
	if (confirm_auth_rev_ != stremio().auth_rev) {
		toast_.show(i18n::tr(Text::ADDONS_SESSION_CHANGED), true);
		return;
	}
	if (stremio().account_sync_busy()
		|| !stremio().addons_loaded()
		|| !stremio().addons_error().empty()) {
		toast_.show(i18n::tr(Text::ADDONS_COLLECTION_UNAVAILABLE), true);
		return;
	}
	const Addon target = confirmed_addon_;
	const bool install = confirm_install_;
	operation_pending_ = true;
	toast_.show(i18n::tr(Text::ADDONS_WORKING));

	std::weak_ptr<int> alive = alive_;
	stremio().change_addon(target, install,
		[this, alive, install](Stremio::AddonChangeResult result) {
		if (alive.expired()) return;
		operation_pending_ = false;

		using Result = Stremio::AddonChangeResult;
		Text message = Text::ADDONS_SAVE_ERROR;
		bool error = true;

		switch (result) {
			case Result::OK:
				message = install
					? Text::ADDONS_INSTALL_OK : Text::ADDONS_REMOVE_OK;
				error = false;
				break;
			case Result::ALREADY_INSTALLED:
				message = Text::ADDONS_ALREADY_INSTALLED;
				error = false;
				break;
			case Result::NOT_INSTALLED:
				message = Text::ADDONS_NOT_INSTALLED;
				break;
			case Result::PROTECTED:
				message = Text::ADDONS_PROTECTED;
				break;
			case Result::NEEDS_CONFIGURATION:
				message = Text::ADDONS_SETUP_REQUIRED;
				confirmed_addon_.configuration_required = true;
				config_option_ = 0;
				config_options_open_ = true;
				break;
			case Result::INVALID_MANIFEST:
				message = Text::ADDONS_INVALID_MANIFEST;
				break;
			case Result::NETWORK_ERROR:
				message = Text::ADDONS_NETWORK_ERROR;
				break;
			case Result::NOT_SIGNED_IN:
				message = Text::ADDONS_SIGN_IN;
				break;
			case Result::BUSY:
				message = Text::ADDONS_WORKING;
				break;
			case Result::SESSION_CHANGED:
				message = Text::ADDONS_SESSION_CHANGED;
				break;
			case Result::SAVE_ERROR:
				message = Text::ADDONS_SAVE_ERROR;
				break;
		}
		toast_.show(i18n::tr(message), error);
	});
}

void AddonsScreen::draw_action(Renderer &r) {
	using i18n::Text;
	const auto &items = current_addons();
	if (items.empty()) return;
	const int index = std::clamp(selected_, 0, (int)items.size() - 1);
	const Addon &addon = items[index];

	Text label;
	if (operation_pending_ || stremio().addon_change_busy()) {
		label = Text::ADDONS_WORKING;
	} else if (stremio().account_sync_busy()
		|| !stremio().addons_loaded()
		|| !stremio().addons_error().empty()) {
		label = Text::ADDONS_COLLECTION_UNAVAILABLE;
	} else if (filter_ == FILTER_INSTALLED) {
		label = addon.is_protected
			? Text::ADDONS_PROTECTED_SHORT : Text::ADDONS_UNINSTALL;
	} else if (is_installed(addon)) {
		label = Text::ADDONS_ALREADY_INSTALLED;
	} else {
		label = addon.configuration_required
			? Text::ADDONS_CONFIG_SHORT : Text::ADDONS_INSTALL;
	}

	const std::string line = std::string("\xC3\x97  ") + i18n::tr(label);
	r.text(fonts_.small, fonts_.small.ellipsize(line, W - 2 * MARGIN),
		MARGIN, S(242), theme::BASE_CONTENT_MUTED);
}

void AddonsScreen::draw_confirmation(Renderer &r) {
	using i18n::Text;
	r.rect(0, 0, W, 1080, theme::BASE_100.with_alpha(0.94f));

	const float w = std::min(S(1000), 1440.0f);
	const float h = S(400);
	const float x = (W - w) / 2;
	const float y = (1080 - h) / 2;
	const float pad = S(30);
	const float text_w = w - 2 * pad;

	r.rounded_rect(x, y, w, h, theme::radius_box(), theme::BASE_200);
	r.text(fonts_.section,
		i18n::tr(confirm_install_
			? Text::ADDONS_CONFIRM_INSTALL : Text::ADDONS_CONFIRM_REMOVE),
		x + pad, y + S(52), theme::BASE_CONTENT);

	r.text(fonts_.card_title,
		fonts_.card_title.ellipsize(display_name(confirmed_addon_), text_w),
		x + pad, y + S(98), theme::BASE_CONTENT);

	r.text(fonts_.small,
		fonts_.small.ellipsize(confirmed_addon_.id, text_w),
		x + pad, y + S(132), theme::BASE_CONTENT_MUTED);
	r.text(fonts_.small,
		fonts_.small.ellipsize(addon_server(confirmed_addon_.transport_url), text_w),
		x + pad, y + S(162), theme::BASE_CONTENT_MUTED);

	float baseline = y + S(206);
	for (const auto &line : fonts_.body.wrap(
		i18n::tr(Text::ADDONS_ACCOUNT_WARNING), text_w, 3)) {
		r.text(fonts_.body, line, x + pad, baseline, theme::BASE_CONTENT_MUTED);
		baseline += S(28);
	}

	const float gap = S(20);
	const float bw = (text_w - gap) / 2;
	const float by = y + h - S(82);
	const float bh = S(52);
	ui::chip(r, fonts_.body, i18n::tr(Text::ADDONS_CANCEL),
		x + pad, by, bw, bh, !confirm_ok_, !confirm_ok_ ? 1.0f : 0.0f,
		theme::BASE_200);
	ui::chip(r, fonts_.body,
		i18n::tr(confirm_install_ ? Text::ADDONS_INSTALL : Text::ADDONS_UNINSTALL),
		x + pad + bw + gap, by, bw, bh, confirm_ok_, confirm_ok_ ? 1.0f : 0.0f,
		theme::BASE_200);
}

void AddonsScreen::open_phone_config() {
	const std::string url =
		addon_config::configure_url(confirmed_addon_.transport_url);
	if (url.empty()) {
		toast_.show(i18n::tr(i18n::Text::ADDONS_PHONE_UNSUPPORTED), true);
		return;
	}

	auto code = addon_config::make_qr(url);
	if (code.empty()) {
		toast_.show(i18n::tr(i18n::Text::ADDONS_PHONE_QR_ERROR), true);
		return;
	}

	phone_qr_.swap(code);
	config_options_open_ = false;
	phone_config_open_ = true;
	phone_sync_pending_ = false;
}

void AddonsScreen::update_config(uint32_t pressed) {
	using i18n::Text;
	Stremio &s = stremio();

	if (confirm_auth_rev_ != s.auth_rev) {
		config_options_open_ = false;
		phone_config_open_ = false;
		phone_sync_pending_ = false;
		phone_qr_.clear();
		toast_.show(i18n::tr(Text::ADDONS_SESSION_CHANGED), true);
		return;
	}

	if (pressed & platform::BUTTON_BACK) {
		if (phone_config_open_) {
			phone_config_open_ = false;
			phone_sync_pending_ = false;
			phone_qr_.clear();
			config_options_open_ = true;
		} else {
			config_options_open_ = false;
		}
		return;
	}

	if (phone_config_open_) {
		if (phone_sync_pending_ && !s.account_sync_busy()) {
			phone_sync_pending_ = false;
			const bool failed = !s.addons_error().empty()
				|| !s.addons_loaded();
			toast_.show(i18n::tr(failed
				? Text::ADDONS_COLLECTION_UNAVAILABLE
				: Text::ADDONS_PHONE_SYNC_DONE), failed);
		}

		if (pressed & platform::BUTTON_ACCEPT) {
			if (!s.logged_in()) {
				toast_.show(i18n::tr(Text::ADDONS_SIGN_IN), true);
			} else if (s.account_sync_busy()
				|| s.addon_change_busy() || operation_pending_) {
				toast_.show(i18n::tr(Text::ADDONS_PHONE_SYNCING));
			} else {
				s.refresh();
				phone_sync_pending_ = true;
				toast_.show(i18n::tr(Text::ADDONS_PHONE_SYNCING));
			}
		}
		return;
	}

	const bool required = confirmed_addon_.configuration_required;
	const int count = required ? 2 : 3;
	if (pressed & platform::BUTTON_UP) {
		config_option_ = std::max(0, config_option_ - 1);
	}
	if (pressed & platform::BUTTON_DOWN) {
		config_option_ = std::min(count - 1, config_option_ + 1);
	}
	if (!(pressed & platform::BUTTON_ACCEPT)) return;

	if (config_option_ == 0) {
		open_phone_config();
	} else if (!required && config_option_ == 1) {
		config_options_open_ = false;
		confirm_ok_ = false;
		confirming_ = true;
	} else {
		config_options_open_ = false;
	}
}

void AddonsScreen::draw_config_options(Renderer &r) {
	using i18n::Text;
	r.rect(0, 0, W, 1080, theme::BASE_100.with_alpha(0.96f));
	const float w = std::min(S(1100), 1680.0f);
	const float h = std::min(S(500), 940.0f);
	const float x = (W - w) / 2;
	const float y = (1080 - h) / 2;
	const float pad = S(30);
	const float text_w = w - 2 * pad;

	r.rounded_rect(x, y, w, h, theme::radius_box(), theme::BASE_200);
	r.text(fonts_.section,
		fonts_.section.ellipsize(
			i18n::tr(Text::ADDONS_SETUP_TITLE), text_w),
		x + pad, y + S(52), theme::BASE_CONTENT);
	r.text(fonts_.card_title,
		fonts_.card_title.ellipsize(display_name(confirmed_addon_), text_w),
		x + pad, y + S(98), theme::BASE_CONTENT);

	const bool required = confirmed_addon_.configuration_required;
	float baseline = y + S(148);
	for (const auto &line : fonts_.body.wrap(i18n::tr(required
		? Text::ADDONS_SETUP_REQUIRED : Text::ADDONS_SETUP_OPTIONAL),
		text_w, 3)) {
		r.text(fonts_.body, line, x + pad, baseline,
			theme::BASE_CONTENT_MUTED);
		baseline += S(28);
	}

	Text labels[3] = {
		Text::ADDONS_SETUP_PHONE,
		required ? Text::ADDONS_CANCEL : Text::ADDONS_SETUP_DEFAULT,
		Text::ADDONS_CANCEL
	};
	const int count = required ? 2 : 3;
	for (int i = 0; i < count; ++i) {
		ui::chip(r, fonts_.body,
			fonts_.body.ellipsize(i18n::tr(labels[i]), text_w - S(24)),
			x + pad, y + S(250) + i * S(64),
			text_w, S(52), i == config_option_,
			i == config_option_ ? 1.0f : 0.0f, theme::BASE_200);
	}
}

void AddonsScreen::draw_phone_config(Renderer &r) {
	using i18n::Text;
	r.rect(0, 0, W, 1080, theme::BASE_100);

	r.text(fonts_.page_header,
		fonts_.page_header.ellipsize(
			i18n::tr(Text::ADDONS_PHONE_TITLE), W - 2 * MARGIN),
		MARGIN, 100, theme::BASE_CONTENT);

	r.text(fonts_.card_title,
		fonts_.card_title.ellipsize(
			display_name(confirmed_addon_), W - 2 * MARGIN),
		MARGIN, 156, theme::BASE_CONTENT);
	r.text(fonts_.small,
		fonts_.small.ellipsize(
			addon_server(confirmed_addon_.transport_url),
			W - 2 * MARGIN),
		MARGIN, 195, theme::BASE_CONTENT_MUTED);

	const float top = 235;
	const float panel = 540;
	const float px = W - MARGIN - panel;
	const float py = top + 30;
	const float left_w = px - MARGIN - 48;
	const float pad = 30;

	r.rounded_rect(MARGIN, top, left_w, 600,
		theme::radius_box(), theme::BASE_200);
	r.rounded_rect(px - 18, top, panel + 36, 600,
		theme::radius_box(), theme::BASE_200);

	const Text steps[] = {
		Text::ADDONS_PHONE_STEP_SCAN,
		Text::ADDONS_PHONE_STEP_INSTALL,
		Text::ADDONS_PHONE_STEP_SYNC
	};
	for (int i = 0; i < 3; ++i) {
		const float y = top + 28 + i * 150;
		r.rounded_rect(MARGIN + pad, y, 46, 46,
			theme::radius_field(), theme::PRIMARY);
		const std::string number = std::to_string(i + 1);
		r.text(fonts_.card_title, number,
			MARGIN + pad + (46 - fonts_.card_title.measure(number)) / 2,
			y + 23 + fonts_.card_title.size() * 0.36f,
			theme::PRIMARY_CONTENT);

		float baseline = y + fonts_.body.size();
		for (const auto &line : fonts_.body.wrap(
			i18n::tr(steps[i]), left_w - 2 * pad - 68, 3)) {
			r.text(fonts_.body, line,
				MARGIN + pad + 68, baseline,
				theme::BASE_CONTENT);
			baseline += fonts_.body.size() * 1.3f;
		}
	}

	float note_y = top + 508;
	for (const auto &line : fonts_.small.wrap(
		i18n::tr(Text::ADDONS_PHONE_SCAN_NOTE),
		left_w - 2 * pad, 2)) {
		r.text(fonts_.small, line,
			MARGIN + pad, note_y, theme::BASE_CONTENT_MUTED);
		note_y += fonts_.small.size() * 1.3f;
	}

	const int size = addon_config::qr_size(phone_qr_);
	r.rect(px, py, panel, panel, rgb(0xFFFFFF));
	if (size > 0) {
		// Integer-sized modules, with at least a four-module quiet zone.
		const int module = std::max(1, (int)(panel / (size + 8)));
		const float qx = (float)(int)(px + (panel - size * module) / 2);
		const float qy = (float)(int)(py + (panel - size * module) / 2);
		for (int row = 0; row < size; ++row) {
			int start = -1;
			for (int col = 0; col <= size; ++col) {
				const bool dark = col < size
					&& addon_config::qr_dark(phone_qr_, col, row);
				if (dark && start < 0) start = col;
				if (!dark && start >= 0) {
					r.rect(qx + start * module,
						qy + row * module,
						(col - start) * module,
						module, rgb(0x000000));
					start = -1;
				}
			}
		}
	}


	const bool busy = phone_sync_pending_ || stremio().account_sync_busy();
	ui::chip(r, fonts_.body,
		fonts_.body.ellipsize(i18n::tr(busy
			? Text::ADDONS_PHONE_SYNCING : Text::ADDONS_PHONE_SYNC),
			left_w - 40),
		MARGIN, 865, left_w, 64,
		true, 1.0f, theme::BASE_100);

	ui::hints(r, fonts_.small,
		{
			{ "\xC3\x97", i18n::tr(Text::ADDONS_PHONE_SYNC) },
			{ "\xE2\x97\x8B", i18n::tr(Text::ADDONS_HINT_BACK) }
		},
		MARGIN, 1005);

	toast_.draw(r, fonts_.body, W / 2, 210);
}

void AddonsScreen::move_search_selection(uint32_t pressed) {
	search_key_ = std::clamp(search_key_, 0, (SEARCH_KEY_COUNT - 1));
	int row = search_key_ < SEARCH_CHARACTER_COUNT ? search_key_ / 6 : 6;
	int col = search_key_ < SEARCH_CHARACTER_COUNT ? search_key_ % 6 : search_key_ - SEARCH_CHARACTER_COUNT;

	const int columns = row == 6 ? 4 : 6;
	if (pressed & platform::BUTTON_LEFT)
		col = std::max(0, col - 1);
	if (pressed & platform::BUTTON_RIGHT)
		col = std::min(columns - 1, col + 1);

	if ((pressed & platform::BUTTON_UP) && row > 0) {
		if (row == 6) col = std::min(5, (2 * col + 1) * 6 / 8);
		--row;
	} else if ((pressed & platform::BUTTON_DOWN) && row < 6) {
		if (row == 5) col = std::min(3, col * 4 / 6);
		++row;
	}

	search_key_ = row < 6 ? row * 6 + col : SEARCH_CHARACTER_COUNT + col;
}

void AddonsScreen::activate_search_key() {
	if (search_key_ == SEARCH_DONE) {
		searching_ = false;
		return;
	}

	const std::string before = addon_query_;
	if (search_key_ < SEARCH_SPACE) {
		if (addon_query_.size() < SEARCH_QUERY_LIMIT)
			addon_query_ += SEARCH_CHARACTERS[search_key_];
	} else if (search_key_ == SEARCH_SPACE) {
		if (addon_query_.size() < SEARCH_QUERY_LIMIT) addon_query_ += ' ';
	} else if (search_key_ == SEARCH_DELETE) {
		if (!addon_query_.empty()) addon_query_.pop_back();
	} else if (search_key_ == SEARCH_CLEAR) {
		addon_query_.clear();
	}

	if (addon_query_ != before) {
		selected_ = 0;
		scroll_ = 0;
		focus_.clear();
	}
}

void AddonsScreen::update_search(uint32_t pressed) {
	if (pressed & platform::BUTTON_BACK) {
		addon_query_.clear();
		selected_ = 0;
		scroll_ = 0;
		focus_.clear();
		search_key_ = 0;
		searching_ = false;
		return;
	}

	if (pressed & platform::BUTTON_R2) {
		searching_ = false;
		return;
	}

	move_search_selection(pressed);
	if (!(pressed & platform::BUTTON_ACCEPT)) return;
	activate_search_key();
}

void AddonsScreen::draw_search(Renderer &r) {
	using i18n::Text;
	r.rect(0, 0, W, 1080, theme::BASE_100.with_alpha(0.98f));

	const float w = 1220, h = 940;
	const float x = (W - w) / 2, y = (1080 - h) / 2;
	const float pad = 40, gap = 14;
	const float inner_w = w - 2 * pad;
	const float top = y + 230;
	const float key_h = 66;
	const float key_w = (inner_w - 5 * gap) / 6;
	const float action_w = (inner_w - 3 * gap) / 4;
	const float action_y = top + 6 * (key_h + gap) + 10;

	r.rounded_rect(x + 8, y + 12, w, h,
		theme::radius_box(), theme::BASE_100);
	r.rounded_rect(x, y, w, h,
		theme::radius_box(), theme::BASE_200);
	r.rounded_rect(x + pad, y + 26, 64, 5,
		2.5f, theme::PRIMARY);

	r.text(fonts_.section,
		fonts_.section.ellipsize(i18n::tr(Text::ADDONS_SEARCH), inner_w),
		x + pad, y + 86, theme::BASE_CONTENT);

	const float field_y = y + 116;
	r.rounded_rect(x + pad, field_y, inner_w, 82,
		theme::radius_field(), theme::BASE_100);
	r.rounded_rect(x + pad + 18, field_y + 20, 4, 42,
		2, theme::PRIMARY);

	const std::string query = addon_query_.empty() ? "_" : addon_query_;
	r.text(fonts_.body,
		fonts_.body.ellipsize(query, inner_w - 68),
		x + pad + 40,
		field_y + 41 + fonts_.body.size() * 0.36f,
		theme::BASE_CONTENT);

	for (int i = 0; i < SEARCH_KEY_COUNT; ++i) {
		const bool action = i >= SEARCH_SPACE;
		const bool done = i == SEARCH_DONE;
		const bool focused = i == search_key_;
		const float kw = action ? action_w : key_w;
		const float kh = action ? 76 : key_h;
		const float kx = x + pad
			+ (action ? i - SEARCH_SPACE : i % 6) * (kw + gap);
		const float ky = action ? action_y : top + (i / 6) * (key_h + gap);

		std::string label;
		if (!action) label = std::string(1, SEARCH_CHARACTERS[i]);
		else if (i == SEARCH_SPACE) label = i18n::tr(Text::SPACE);
		else if (i == SEARCH_DELETE) label = i18n::tr(Text::DELETE_KEY);
		else if (i == SEARCH_CLEAR) label = i18n::tr(Text::CLEAR);
		else label = i18n::tr(Text::ADDONS_SEARCH_DONE);

		if (focused) {
			r.rounded_rect(kx - 3, ky - 3, kw + 6, kh + 6,
				theme::radius_field() + 3, theme::PRIMARY);
		}

		const Color fill = focused ? theme::PRIMARY
			: done ? mix(theme::BASE_300, theme::PRIMARY, 0.45f)
			: action ? theme::BASE_300 : theme::BASE_100;
		r.rounded_rect(kx, ky, kw, kh,
			theme::radius_field(), fill);

		Font &font = action ? fonts_.body : fonts_.card_title;
		label = font.ellipsize(label, kw - 24);
		r.text(font, label,
			kx + (kw - font.measure(label)) / 2,
			ky + kh / 2 + font.size() * 0.36f,
			focused || done
				? theme::PRIMARY_CONTENT : theme::BASE_CONTENT);
	}

	r.rect(x + pad, y + h - 106, inner_w, 1, theme::BASE_300);
	ui::hints(r, fonts_.small,
		{
			{ "\xC3\x97", i18n::tr(Text::ADDONS_SEARCH_TYPE) },
			{ "R2", i18n::tr(Text::ADDONS_SEARCH_DONE) },
			{ "\xE2\x97\x8B", i18n::tr(Text::ADDONS_SEARCH_CLOSE) }
		},
		x + pad, y + h - 48);
}

void AddonsScreen::update(double dt, uint32_t pressed) {
	toast_.update(dt);

	if (config_options_open_ || phone_config_open_) {
		update_config(pressed);
		return;
	}

	if (searching_) {
		update_search(pressed);
		return;
	}

	if (confirming_) {
		if (confirm_auth_rev_ != stremio().auth_rev) {
			confirming_ = false;
			toast_.show(i18n::tr(i18n::Text::ADDONS_SESSION_CHANGED), true);
			return;
		}
		if (pressed & platform::BUTTON_BACK) {
			confirming_ = false;
			return;
		}
		if (pressed & platform::BUTTON_LEFT) confirm_ok_ = false;
		if (pressed & platform::BUTTON_RIGHT) confirm_ok_ = true;
		if (pressed & platform::BUTTON_ACCEPT) {
			const bool approved = confirm_ok_;
			confirming_ = false;
			if (approved) execute_change();
		}
		return;
	}

	if (filter_ == FILTER_COMMUNITY && (pressed & platform::BUTTON_TRIANGLE)) {
		searching_ = true;
		on_bar_ = false;
		on_filters_ = false;
		return;
	}

	const bool previous_filter = (pressed & platform::BUTTON_L2) != 0;
	const bool next_filter = (pressed & platform::BUTTON_R2) != 0;
	if (previous_filter != next_filter) {
		set_filter((filter_ + (next_filter ? 1 : FILTER_COUNT - 1))
				% FILTER_COUNT);
		on_bar_ = false;
		on_filters_ = true;
		pressed = 0;
	}

	if (bar_.handle_shoulders(pressed, fonts_, navigator_)) return;

	if (on_bar_) {
		if (bar_.handle_focused(pressed, fonts_, navigator_)) return;
		if (pressed & platform::BUTTON_UP) {
			on_bar_ = false;
			on_filters_ = current_addons().empty();
		}
	} else if (on_filters_) {
		if (pressed & platform::BUTTON_LEFT) set_filter(filter_ - 1);
		if (pressed & platform::BUTTON_RIGHT) set_filter(filter_ + 1);
		if (pressed & (platform::BUTTON_DOWN | platform::BUTTON_ACCEPT)) {
			on_filters_ = false;
			if (current_addons().empty()) on_bar_ = true;
		}
		if (pressed & platform::BUTTON_BACK) {
			on_filters_ = false;
			on_bar_ = true;
		}
	} else {
		const int count = (int)current_addons().size();
		if (pressed & platform::BUTTON_UP) {
			if (selected_ == 0 || count == 0) on_filters_ = true;
			else --selected_;
		}
		if (pressed & platform::BUTTON_DOWN) {
			if (selected_ + 1 < count) ++selected_;
			else on_bar_ = true;
		}
		if (pressed & platform::BUTTON_BACK) on_bar_ = true;
		if ((pressed & platform::BUTTON_ACCEPT) && !on_bar_) activate_addon();
	}

	if (pressed & platform::BUTTON_SQUARE) {
		if (filter_ != FILTER_INSTALLED) {
			load_catalog(true);
		} else if (!stremio().logged_in()) {
			toast_.show(i18n::tr(i18n::Text::ADDONS_SIGN_IN), true);
		} else if (operation_pending_ || stremio().addon_change_busy()
			|| stremio().account_sync_busy()) {
			toast_.show(i18n::tr(i18n::Text::ADDONS_WORKING));
		} else {
			stremio().refresh();
		}
	}

	const int count = (int)current_addons().size();
	selected_ = std::clamp(selected_, 0, std::max(0, count - 1));
	focus_.resize(count, 0.0f);
	for (int i = 0; i < count; ++i) {
		const bool focused = !on_bar_ && !on_filters_ && i == selected_;
		focus_[i] = ui::approach(focus_[i], focused ? 1.0f : 0.0f, dt);
	}
	if (count > 0) {
		scroll_ = ui::follow(scroll_, selected_ * (row_h() + row_gap()),
			row_h(), list_h(), dt);
	} else {
		scroll_ = 0;
	}
	bar_.update(dt, on_bar_);
}

void AddonsScreen::draw(Renderer &r) {
	const Stremio &s = stremio();
	const auto &addons = current_addons();
	r.text(fonts_.page_header, i18n::tr(i18n::Text::ADDONS),
		MARGIN, S(96), theme::BASE_CONTENT);

	std::string status;
	bool failed = false;
	bool loading = false;

	if (filter_ == FILTER_INSTALLED) {
		failed = !s.addons_error().empty();
		loading = !s.addons_loaded();
		if (failed) {
			status = std::string(i18n::tr(i18n::Text::ADDONS_LOAD_ERROR))
				+ s.addons_error();
		} else if (loading) {
			status = i18n::tr(i18n::Text::LOADING_ADDONS);
		} else {
			status = std::string(i18n::tr(i18n::Text::ADDONS_INSTALLED_PREFIX))
				+ std::to_string(addons.size());
		}
	} else {
		const auto &state = *catalogs_[filter_ - FILTER_OFFICIAL];
		failed = state.failed;
		loading = state.loading;
		if (loading) status = i18n::tr(i18n::Text::LOADING_ADDONS);
		else if (failed) status = i18n::tr(i18n::Text::ADDONS_CATALOG_ERROR);
		else status = std::string(i18n::tr(i18n::Text::ADDONS_AVAILABLE_PREFIX))
				+ std::to_string(addons.size());
	}

	r.text(fonts_.body, fonts_.body.ellipsize(status, W - 2 * MARGIN),
		MARGIN, S(135), failed ? theme::ERROR : theme::BASE_CONTENT_MUTED);
	draw_filters(r);
	draw_action(r);

	for (size_t i = 0; i < addons.size(); ++i) {
		const float y = list_y() + i * (row_h() + row_gap()) - scroll_;
		if (y < list_y() - 1 || y + row_h() > list_y() + list_h() + 1) continue;
		const float f = i < focus_.size() ? focus_[i] : 0.0f;
		const bool selected = (int)i == selected_;
		const float radius = theme::radius_field();
		ui::begin_focus(r, list_x(), y, list_w(), row_h(), radius, f, 1.02f);
		r.rounded_rect(list_x(), y, list_w(), row_h(), radius,
			selected ? mix(theme::BASE_200, theme::PRIMARY, 0.30f) : theme::BASE_200);
		draw_addon_logo(r, fonts_, addons[i], list_x() + S(16),
			y + (row_h() - S(58)) / 2, S(58));
		r.text(fonts_.card_title,
			fonts_.card_title.ellipsize(display_name(addons[i]), list_w() - S(112)),
			list_x() + S(94), y + row_h() / 2 + S(9), theme::BASE_CONTENT);
		ui::end_focus(r, list_x(), y, list_w(), row_h(), radius, f);
	}

	if (addons.empty()) {
		if (!loading && !failed) {
			r.text(fonts_.body, i18n::tr(filter_ == FILTER_INSTALLED
				? i18n::Text::ADDONS_EMPTY
				: filter_ == FILTER_COMMUNITY && !addon_query_.empty()
					? i18n::Text::ADDONS_SEARCH_EMPTY
					: i18n::Text::ADDONS_CATALOG_EMPTY),
				MARGIN, list_y() + S(40), theme::BASE_CONTENT_MUTED);
		}
	} else {
		const int index = std::clamp(selected_, 0, (int)addons.size() - 1);
		const Addon &addon = addons[index];
		const float x = list_x() + list_w() + S(32);
		const float w = W - MARGIN - x;
		const float pad = S(24);
		const float text_w = w - 2 * pad;
		r.rounded_rect(x, list_y(), w, list_h(),
			theme::radius_box(), theme::BASE_200);

		draw_addon_logo(r, fonts_, addon,
			x + w - pad - S(96), list_y() + pad, S(96));
		float y = list_y() + S(42);
		r.text(fonts_.section, fonts_.section.ellipsize(display_name(addon), text_w - S(120)),
			x + pad, y, theme::BASE_CONTENT);
		y += S(36);
		if (!addon.version.empty()) {
			const std::string version =
				std::string(i18n::tr(i18n::Text::ADDONS_VERSION_PREFIX))
				+ addon.version;
			r.text(fonts_.body, fonts_.body.ellipsize(version, text_w - S(120)),
				x + pad, y, theme::BASE_CONTENT_MUTED);
			y += S(36);
		}
		y = std::max(y, list_y() + pad + S(96) + S(36));
		r.text(fonts_.card_title, i18n::tr(i18n::Text::ADDONS_FUNCTIONS),
			x + pad, y, theme::BASE_CONTENT);
		y += S(30);
		for (const auto &line : fonts_.body.wrap(functions_label(addon), text_w, 3)) {
			r.text(fonts_.body, line, x + pad, y, theme::BASE_CONTENT_MUTED);
			y += S(28);
		}
		y += S(20);
		const std::string description = addon.description.empty()
			? i18n::tr(i18n::Text::ADDONS_NO_DESCRIPTION) : addon.description;
		const float bottom = list_y() + list_h() - pad;
		const int max_lines = std::max(0, (int)((bottom - y) / S(28)));
		if (max_lines > 0) {
			for (const auto &line : fonts_.body.wrap(description, text_w, max_lines)) {
				r.text(fonts_.body, line, x + pad, y, theme::BASE_CONTENT);
				y += S(28);
			}
		}
	}

	std::vector<ui::Hint> hints = {
		{ "\xC3\x97", i18n::tr(i18n::Text::ADDONS_HINT_ACTION) },
		{ "\xE2\x96\xA1", i18n::tr(i18n::Text::ADDONS_HINT_RELOAD) },
		{ "\xE2\x9C\x9A", i18n::tr(i18n::Text::ADDONS_HINT_NAVIGATION) }
	};
	if (filter_ == FILTER_COMMUNITY) {
		hints.push_back({
			"\xE2\x96\xB3",
			i18n::tr(i18n::Text::ADDONS_SEARCH)
		});
	}
	ui::hints(r, fonts_.small, hints,
		MARGIN, tabs::content_bottom() - S(24));
	bar_.draw(r, fonts_);
	toast_.draw(r, fonts_.body, W / 2, tabs::box_y() - S(88));
	if (confirming_) draw_confirmation(r);
	if (searching_) draw_search(r);
	if (config_options_open_) {
		draw_config_options(r);
		toast_.draw(r, fonts_.body, W / 2, S(100));
	}
	if (phone_config_open_) draw_phone_config(r);
}
