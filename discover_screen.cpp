#include "discover_screen.h"

#include "../platform/platform.h"
#include "components.h"
#include "theme.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace {

using theme::S;

const float W = 1920;
const float MARGIN_X = 120;
const float VIEW_W = W - 2 * MARGIN_X;
const size_t MAX_DISCOVER_ITEMS = 480;

// Remembered while the app runs (switching tabs or returning from Details).
std::string g_type;
std::string g_catalog_key; // addon_id + "/" + catalog_id
std::string g_filter_value;
std::string g_selected_meta_id;
std::vector<Card> g_cached_cards;
std::string g_cached_signature;
bool g_cached_has_more = false;

std::string type_label(const std::string &type) {
	if (type == "movie") return "Movies";
	if (type == "series") return "Series";
	if (type == "channel") return "Channels";
	if (type == "tv") return "TV";
	std::string label = type;
	if (!label.empty()) label[0] = (char)toupper((unsigned char)label[0]);
	return label;
}

std::string catalog_key(const DiscoverCatalog &c) {
	return c.addon_id + "/" + c.id;
}

std::string signature_for(const std::string &type, const std::string &cat_key, const std::string &filter) {
	return type + "|" + cat_key + "|" + filter;
}

} // namespace

DiscoverScreen::DiscoverScreen(Fonts &fonts, Navigator &nav) : fonts_(fonts), navigator_(nav) {
	rebuild_filters(true);
}

int DiscoverScreen::grid_columns() const {
	return std::clamp((int)std::round(8.0f * theme::DEFAULT_UI_SCALE / theme::ui_scale()), 6, 10);
}

bool DiscoverScreen::filter_chip_active(const Chip &c) const {
	if (c.catalog_index >= 0) {
		return c.catalog_index < (int)catalogs_.size() && catalog_key(catalogs_[c.catalog_index]) == g_catalog_key;
	}
	return c.value == g_type;
}

bool DiscoverScreen::genre_chip_active(const Chip &c) const {
	return c.value == g_filter_value;
}

void DiscoverScreen::layout_chips() {
	float x = 0;
	bool in_catalogs = false;
	for (Chip &c : filter_chips_) {
		if (c.catalog_index >= 0 && !in_catalogs) {
			if (x > 0) x += S(28); // visual separation between Types and Catalogs
			in_catalogs = true;
		}
		c.w = ui::chip_width(fonts_.body, c.label);
		c.x = x;
		x += c.w + S(12);
	}
	float gx = 0;
	for (Chip &c : genre_chips_) {
		c.w = ui::chip_width(fonts_.body, c.label);
		c.x = gx;
		gx += c.w + S(12);
	}
}

void DiscoverScreen::rebuild_filters(bool reload_items) {
	Stremio &s = stremio();
	seen_board_rev_ = s.board_rev;
	seen_library_rev_ = s.library_rev;
	seen_notifications_rev_ = s.notifications_rev;

	const std::vector<std::string> types = s.discover_types();
	if (types.empty()) {
		catalogs_.clear();
		filter_chips_.clear();
		genre_chips_.clear();
		cards_.clear();
		if (!s.addons_loaded()) {
			status_ = "Loading add-on catalogs\xE2\x80\xA6";
			status_is_error_ = false;
		} else if (!s.addons_error().empty()) {
			status_ = "Couldn't load your add-ons: " + s.addons_error();
			status_is_error_ = true;
		} else {
			status_ = "No browsable catalogs found in your add-ons.";
			status_is_error_ = false;
		}
		zone_ = BAR;
		return;
	}

	if (std::find(types.begin(), types.end(), g_type) == types.end()) g_type = types[0];
	catalogs_ = s.discover_catalogs(g_type);

	int active_cat = 0;
	for (size_t i = 0; i < catalogs_.size(); i++) {
		if (catalog_key(catalogs_[i]) == g_catalog_key) {
			active_cat = (int)i;
			break;
		}
	}
	if (!catalogs_.empty()) g_catalog_key = catalog_key(catalogs_[active_cat]);

	// Count duplicate catalog names so we can disambiguate with the add-on name.
	std::map<std::string, int> name_counts;
	for (const DiscoverCatalog &cat : catalogs_) name_counts[cat.name]++;

	const std::string prev_filter_label = filter_idx_ < (int)filter_chips_.size() ? filter_chips_[filter_idx_].label : "";
	std::vector<Chip> fchips;
	for (const std::string &t : types) {
		Chip c;
		c.label = type_label(t);
		c.value = t;
		c.catalog_index = -1;
		fchips.push_back(c);
	}
	for (size_t i = 0; i < catalogs_.size(); i++) {
		const DiscoverCatalog &cat = catalogs_[i];
		Chip c;
		c.label = (name_counts[cat.name] > 1 && !cat.addon_name.empty()) ? cat.name + " \xC2\xB7 " + cat.addon_name : cat.name;
		c.value = catalog_key(cat);
		c.catalog_index = (int)i;
		fchips.push_back(c);
	}
	for (size_t i = 0; i < fchips.size(); i++) {
		for (const Chip &old : filter_chips_) {
			if (old.label == fchips[i].label && (old.catalog_index >= 0) == (fchips[i].catalog_index >= 0)) {
				fchips[i].focus = old.focus;
			}
		}
		if (fchips[i].label == prev_filter_label) filter_idx_ = (int)i;
	}
	filter_idx_ = std::clamp(filter_idx_, 0, std::max(0, (int)fchips.size() - 1));
	filter_chips_.swap(fchips);

	// Genre / filter options for the active catalog.
	const std::string prev_genre_label = genre_idx_ < (int)genre_chips_.size() ? genre_chips_[genre_idx_].label : "";
	std::vector<Chip> gchips;
	if (!catalogs_.empty()) {
		const DiscoverCatalog &cat = catalogs_[active_cat];
		if (!cat.filter_options.empty()) {
			if (!cat.filter_required) {
				gchips.push_back({ "All", "", -1 });
			}
			for (const std::string &opt : cat.filter_options) {
				gchips.push_back({ opt, opt, -1 });
			}
			bool valid = (!cat.filter_required && g_filter_value.empty());
			for (const std::string &opt : cat.filter_options) {
				if (opt == g_filter_value) valid = true;
			}
			if (!valid) g_filter_value = cat.filter_required ? cat.filter_options[0] : "";
		} else {
			g_filter_value.clear();
		}
	}
	genre_idx_ = 0;
	for (size_t i = 0; i < gchips.size(); i++) {
		for (const Chip &old : genre_chips_) {
			if (old.label == gchips[i].label) gchips[i].focus = old.focus;
		}
		if (gchips[i].value == g_filter_value) genre_idx_ = (int)i;
		if (gchips[i].label == prev_genre_label) genre_idx_ = (int)i;
	}
	genre_idx_ = std::clamp(genre_idx_, 0, std::max(0, (int)gchips.size() - 1));
	genre_chips_.swap(gchips);

	layout_chips();

	if (reload_items) {
		const std::string sig = signature_for(g_type, g_catalog_key, g_filter_value);
		if (sig == g_cached_signature && !g_cached_cards.empty()) {
			cards_ = g_cached_cards;
			has_more_ = g_cached_has_more;
			loading_ = false;
			loading_more_ = false;
			status_.clear();
			status_is_error_ = false;
			sync_card_badges();
			selected_ = 0;
			for (size_t i = 0; i < cards_.size(); i++) {
				if (cards_[i].meta_id == g_selected_meta_id) selected_ = (int)i;
			}
			if (!user_moved_) zone_ = GRID;
		} else {
			load_page(true);
		}
	}
}

void DiscoverScreen::sync_card_badges() {
	Stremio &s = stremio();
	for (Card &c : cards_) {
		c.progress = 0;
		c.badge.clear();
		if (const LibraryItem *li = s.library_item(c.meta_id)) {
			if (li->is_in_continue_watching()) c.progress = li->progress();
			const int n = s.notification_count(li->id);
			if (n > 0) c.badge = std::to_string(n) + " new";
		}
	}
}

void DiscoverScreen::load_page(bool reset) {
	if (catalogs_.empty()) return;
	int active_cat = 0;
	for (size_t i = 0; i < catalogs_.size(); i++) {
		if (catalog_key(catalogs_[i]) == g_catalog_key) {
			active_cat = (int)i;
			break;
		}
	}
	const DiscoverCatalog cat = catalogs_[active_cat];
	if (reset) {
		cards_.clear();
		selected_ = 0;
		scroll_ = 0;
		loading_ = true;
		loading_more_ = false;
		has_more_ = cat.supports_skip;
		status_ = "Loading " + cat.name + "\xE2\x80\xA6";
		status_is_error_ = false;
		fetch_gen_++;
	} else {
		if (loading_ || loading_more_ || !has_more_ || !cat.supports_skip || cards_.size() >= MAX_DISCOVER_ITEMS) return;
		loading_more_ = true;
	}

	const int skip = reset ? 0 : (int)cards_.size();
	const int gen = fetch_gen_;
	std::weak_ptr<int> alive = alive_;
	stremio().fetch_discover(cat, g_filter_value, skip,
			[this, alive, gen, reset, cat](bool ok, std::vector<MetaItem> &items, const std::string &error) {
				if (alive.expired() || gen != fetch_gen_) return;
				loading_ = false;
				loading_more_ = false;
				if (!ok) {
					if (reset) {
						status_ = "Couldn't load catalog: " + error;
						status_is_error_ = true;
					}
					has_more_ = false;
					return;
				}
				size_t added = 0;
				for (const MetaItem &m : items) {
					bool exists = false;
					for (const Card &existing : cards_) {
						if (existing.meta_id == m.id && !m.id.empty()) {
							exists = true;
							break;
						}
					}
					if (exists) continue;
					Card c = Card::from_meta(m);
					// Poster artwork for the grid (portrait), with Metahub fallback.
					c.images.clear();
					if (!m.poster.empty()) c.images.push_back(m.poster);
					if (m.id.compare(0, 2, "tt") == 0) {
						c.images.push_back("https://images.metahub.space/poster/medium/" + m.id + "/img");
					}
					cards_.push_back(std::move(c));
					added++;
				}
				sync_card_badges();
				if (items.empty() || added == 0 || cards_.size() >= MAX_DISCOVER_ITEMS) {
					has_more_ = false;
				}
				if (cards_.empty()) {
					status_ = "No titles found for this filter.";
					status_is_error_ = false;
					if (zone_ == GRID) zone_ = !genre_chips_.empty() ? GENRES : FILTERS;
				} else {
					status_.clear();
					status_is_error_ = false;
					if (!user_moved_ && reset) zone_ = GRID;
				}
				g_cached_cards = cards_;
				g_cached_has_more = has_more_;
				g_cached_signature = signature_for(g_type, g_catalog_key, g_filter_value);
			});
}

void DiscoverScreen::activate_filter_chip() {
	if (filter_idx_ < 0 || filter_idx_ >= (int)filter_chips_.size()) return;
	const Chip &c = filter_chips_[filter_idx_];
	if (filter_chip_active(c)) return;
	if (c.catalog_index >= 0) {
		g_catalog_key = c.value;
		g_filter_value.clear();
	} else {
		g_type = c.value;
		g_catalog_key.clear();
		g_filter_value.clear();
	}
	g_cached_signature.clear();
	genre_scroll_ = 0;
	rebuild_filters(true);
}

void DiscoverScreen::activate_genre_chip() {
	if (genre_idx_ < 0 || genre_idx_ >= (int)genre_chips_.size()) return;
	const Chip &c = genre_chips_[genre_idx_];
	if (genre_chip_active(c)) return;
	g_filter_value = c.value;
	g_cached_signature.clear();
	load_page(true);
}

void DiscoverScreen::move(int dx, int dy) {
	const int count = (int)cards_.size();
	const bool has_genres = !genre_chips_.empty();
	const int cols = grid_columns();
	switch (zone_) {
		case FILTERS:
			filter_idx_ = std::clamp(filter_idx_ + dx, 0, std::max(0, (int)filter_chips_.size() - 1));
			if (dy > 0) zone_ = has_genres ? GENRES : (count > 0 ? GRID : BAR);
			break;
		case GENRES:
			genre_idx_ = std::clamp(genre_idx_ + dx, 0, std::max(0, (int)genre_chips_.size() - 1));
			if (dy < 0) zone_ = FILTERS;
			else if (dy > 0) zone_ = count > 0 ? GRID : BAR;
			break;
		case GRID: {
			const int row = selected_ / cols, last_row = (count - 1) / cols;
			if (dx != 0) {
				const int target = selected_ + dx;
				if (target >= 0 && target < count && target / cols == row) selected_ = target;
			} else if (dy < 0) {
				if (row > 0) selected_ -= cols;
				else zone_ = has_genres ? GENRES : FILTERS;
			} else if (dy > 0) {
				if (row < last_row) {
					selected_ = std::min(selected_ + cols, count - 1);
					// Fetch the next page as the user approaches the bottom.
					if (selected_ / cols >= last_row - 1) load_page(false);
				} else {
					zone_ = BAR;
				}
			}
			break;
		}
		case BAR:
			if (dy < 0) zone_ = count > 0 ? GRID : (has_genres ? GENRES : FILTERS);
			break;
	}
}

void DiscoverScreen::update(double dt, uint32_t pressed) {
	Stremio &s = stremio();
	if (s.board_rev != seen_board_rev_) {
		rebuild_filters(cards_.empty());
	} else if (s.library_rev != seen_library_rev_ || s.notifications_rev != seen_notifications_rev_) {
		seen_library_rev_ = s.library_rev;
		seen_notifications_rev_ = s.notifications_rev;
		sync_card_badges();
	}

	if (bar_.handle_shoulders(pressed, fonts_, navigator_)) return;
	if (zone_ == BAR && bar_.handle_focused(pressed, fonts_, navigator_)) return;
	if (zone_ != BAR && (pressed & platform::BUTTON_LEFT)) move(-1, 0);
	if (zone_ != BAR && (pressed & platform::BUTTON_RIGHT)) move(1, 0);
	if (pressed & platform::BUTTON_UP) move(0, -1);
	if (pressed & platform::BUTTON_DOWN) move(0, 1);
	if (pressed & platform::DIRECTIONS) user_moved_ = true;

	if (pressed & platform::BUTTON_ACCEPT) {
		if (zone_ == FILTERS) {
			activate_filter_chip();
		} else if (zone_ == GENRES) {
			activate_genre_chip();
		} else if (zone_ == GRID && selected_ < (int)cards_.size()) {
			Card c = cards_[selected_];
			g_selected_meta_id = c.meta_id;
			c.images = wide_artwork(c.meta_id, "", c.poster);
			open_details(c, fonts_, navigator_);
		}
	}

	layout_chips();

	for (int i = 0; i < (int)filter_chips_.size(); i++) {
		filter_chips_[i].focus = ui::approach(filter_chips_[i].focus, (zone_ == FILTERS && i == filter_idx_) ? 1.0f : 0.0f, dt);
	}
	for (int i = 0; i < (int)genre_chips_.size(); i++) {
		genre_chips_[i].focus = ui::approach(genre_chips_[i].focus, (zone_ == GENRES && i == genre_idx_) ? 1.0f : 0.0f, dt);
	}
	for (int i = 0; i < (int)cards_.size(); i++) {
		cards_[i].focus = ui::approach(cards_[i].focus, (zone_ == GRID && i == selected_) ? 1.0f : 0.0f, dt);
	}

	const float filter_x0 = MARGIN_X + fonts_.page_header.measure("Discover") + S(44);
	const float filter_view = W - MARGIN_X - filter_x0;
	if (!filter_chips_.empty() && filter_idx_ < (int)filter_chips_.size()) {
		const Chip &fc = filter_chips_[filter_idx_];
		filter_scroll_ = ui::follow(filter_scroll_, fc.x, fc.w, filter_view, dt);
	}
	if (!genre_chips_.empty() && genre_idx_ < (int)genre_chips_.size()) {
		const Chip &gc = genre_chips_[genre_idx_];
		genre_scroll_ = ui::follow(genre_scroll_, gc.x, gc.w, VIEW_W, dt);
	}

	const int cols = grid_columns();
	const float gap = S(28), row_gap = S(20);
	const float card_w = (VIEW_W - (cols - 1) * gap) / cols;
	const float card_h = card_w * 1.5f;
	const float cell_h = card_h + POSTER_CAPTION_H;
	const float pitch = cell_h + row_gap;
	const float grid_top = genre_chips_.empty() ? S(150) : S(214);
	const float view_h = tabs::CONTENT_BOTTOM - grid_top;

	if (zone_ == GRID && !cards_.empty()) {
		scroll_ = ui::follow(scroll_, (selected_ / cols) * pitch, cell_h, view_h, dt);
	}
	bar_.update(dt, zone_ == BAR);
}

void DiscoverScreen::draw(Renderer &r) {
	const bool has_genres = !genre_chips_.empty();
	const float grid_top = has_genres ? S(214) : S(150);
	const int cols = grid_columns();
	const float gap = S(28), row_gap = S(20);
	const float card_w = (VIEW_W - (cols - 1) * gap) / cols;
	const float card_h = card_w * 1.5f;
	const float cell_h = card_h + POSTER_CAPTION_H;
	const float pitch = cell_h + row_gap;

	if (!status_.empty()) {
		r.text(fonts_.body, fonts_.body.ellipsize(status_, VIEW_W), MARGIN_X, grid_top + S(40),
				status_is_error_ ? theme::ERROR : theme::BASE_CONTENT_MUTED);
	}

	// Poster grid: focused card drawn on pass 1 so it overlaps its neighbours.
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < (int)cards_.size(); i++) {
			if ((pass == 0) == (cards_[i].focus > 0.5f)) continue;
			const float x = MARGIN_X + (i % cols) * (card_w + gap);
			const float y = grid_top + (i / cols) * pitch - scroll_;
			if (y > tabs::CONTENT_BOTTOM || y + cell_h < 0) continue;
			draw_poster(r, fonts_, cards_[i], x, y, card_w, card_h);
		}
	}

	if (loading_more_ && !cards_.empty()) {
		const int rows = ((int)cards_.size() + cols - 1) / cols;
		const float more_y = grid_top + rows * pitch - scroll_ + S(24);
		if (more_y < tabs::CONTENT_BOTTOM) {
			r.text(fonts_.body, "Loading more\xE2\x80\xA6", MARGIN_X, more_y, theme::BASE_CONTENT_MUTED);
		}
	}

	// Header band (covers cards scrolled under it): title, type/catalog row,
	// and optional genre/year filter row.
	r.rect(0, 0, W, grid_top - S(20), theme::BASE_100);
	r.text(fonts_.page_header, "Discover", MARGIN_X, S(96), theme::BASE_CONTENT);

	const float chip_h = S(50);
	const float row0_y = S(78) - chip_h / 2;
	const float filter_x0 = MARGIN_X + fonts_.page_header.measure("Discover") + S(44);
	for (int pass = 0; pass < 2; pass++) {
		for (const Chip &c : filter_chips_) {
			if ((pass == 0) == (c.focus > 0.5f)) continue;
			const float cx = filter_x0 + c.x - filter_scroll_;
			if (cx < filter_x0 - 2 || cx + c.w > W - MARGIN_X + 2) continue;
			ui::chip(r, fonts_.body, c.label, cx, row0_y, c.w, chip_h, filter_chip_active(c), c.focus);
		}
	}

	if (has_genres) {
		const float genre_h = S(46);
		const float row1_y = S(130);
		for (int pass = 0; pass < 2; pass++) {
			for (const Chip &c : genre_chips_) {
				if ((pass == 0) == (c.focus > 0.5f)) continue;
				const float cx = MARGIN_X + c.x - genre_scroll_;
				if (cx < MARGIN_X - 2 || cx + c.w > W - MARGIN_X + 2) continue;
				ui::chip(r, fonts_.body, c.label, cx, row1_y, c.w, genre_h, genre_chip_active(c), c.focus);
			}
		}
	}

	bar_.draw(r, fonts_);
}
