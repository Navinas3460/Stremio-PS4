#include "discover_screen.h"

#include "../platform/platform.h"
#include "components.h"
#include "theme.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>

namespace {

using theme::S;

const float W = 1920;
const float H = 1080;
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

std::string to_lower_copy(const std::string &s) {
	std::string out = s;
	for (char &c : out) c = (char)tolower((unsigned char)c);
	return out;
}

std::string type_label(const std::string &type) {
	if (type == "movie") return "Movies";
	if (type == "series") return "Series";
	if (type == "channel") return "Channels";
	if (type == "tv") return "TV";
	if (type == "other") return "Other";
	std::string label = type;
	if (!label.empty()) label[0] = (char)toupper((unsigned char)label[0]);
	return label;
}

std::string catalog_key(const DiscoverCatalog &c) {
	return c.addon_id + "/" + c.id;
}

bool is_primary_top_catalog(const DiscoverCatalog &c) {
	if (c.addon_id == "builtin.streaming") return false;
	const std::string low = to_lower_copy(c.name);
	const std::string id_low = to_lower_copy(c.id);
	return low == "popular" || low == "featured" || id_low == "top" || id_low == "imdbRating";
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

std::string DiscoverScreen::active_source_label() const {
	for (size_t i = 1; i < source_options_.size(); i++) {
		if (source_options_[i].value == g_catalog_key) {
			return source_options_[i].label;
		}
	}
	return "All Sources";
}

std::string DiscoverScreen::active_genre_label() const {
	if (g_filter_value.empty()) return "All Genres";
	for (const Chip &c : genre_options_) {
		if (c.value == g_filter_value && !c.value.empty()) return c.label;
	}
	return g_filter_value;
}

void DiscoverScreen::layout_chips() {
	float x = 0;
	bool in_catalogs = false;
	for (Chip &c : filter_chips_) {
		if (c.catalog_index >= 0 && !in_catalogs) {
			if (x > 0) x += S(28); // visual separation between Types and Popular/Featured
			in_catalogs = true;
		}
		c.w = ui::chip_width(fonts_.body, c.label);
		c.x = x;
		x += c.w + S(12);
	}

	const std::string src_btn = "Source: " + active_source_label() + "  \xE2\x96\xBE";
	const std::string gen_btn = "Genre: " + active_genre_label() + "  \xE2\x96\xBE";
	dropdown_x_[0] = MARGIN_X;
	dropdown_w_[0] = std::max(S(260), ui::chip_width(fonts_.body, src_btn) + S(12));
	dropdown_x_[1] = dropdown_x_[0] + dropdown_w_[0] + S(18);
	dropdown_w_[1] = std::max(S(250), ui::chip_width(fonts_.body, gen_btn) + S(12));
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
		source_options_.clear();
		genre_options_.clear();
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

	// Primary top-row catalogs: keep Popular and Featured in their exact top-row placement.
	int primary_added = 0;
	int default_primary_idx = 0;
	for (size_t i = 0; i < catalogs_.size(); i++) {
		const DiscoverCatalog &cat = catalogs_[i];
		if (is_primary_top_catalog(cat)) {
			if (primary_added == 0) default_primary_idx = (int)i;
			Chip c;
			c.label = cat.name;
			c.value = catalog_key(cat);
			c.catalog_index = (int)i;
			fchips.push_back(c);
			primary_added++;
		}
	}
	// If this content type has no catalog named Popular/Featured, place the first catalog in the top row.
	if (primary_added == 0 && !catalogs_.empty()) {
		const DiscoverCatalog &cat = catalogs_[0];
		Chip c;
		c.label = cat.name;
		c.value = catalog_key(cat);
		c.catalog_index = 0;
		fchips.push_back(c);
		default_primary_idx = 0;
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

	// Build Alphabetical Source Dropdown Options (Netflix, Disney+, HBO Max, Prime Video, etc.)
	std::vector<Chip> soptions;
	if (!catalogs_.empty()) {
		Chip all_src;
		all_src.label = "All Sources (" + catalogs_[default_primary_idx].name + ")";
		all_src.value = catalog_key(catalogs_[default_primary_idx]);
		all_src.catalog_index = default_primary_idx;
		soptions.push_back(all_src);

		std::vector<Chip> extra_sources;
		for (size_t i = 0; i < catalogs_.size(); i++) {
			const DiscoverCatalog &cat = catalogs_[i];
			if (is_primary_top_catalog(cat)) continue;
			Chip c;
			c.label = (name_counts[cat.name] > 1 && !cat.addon_name.empty() && cat.addon_id != "builtin.streaming")
					? (cat.name + " \xC2\xB7 " + cat.addon_name)
					: cat.name;
			c.value = catalog_key(cat);
			c.catalog_index = (int)i;
			extra_sources.push_back(c);
		}
		std::stable_sort(extra_sources.begin(), extra_sources.end(), [](const Chip &a, const Chip &b) {
			return to_lower_copy(a.label) < to_lower_copy(b.label);
		});
		for (Chip &c : extra_sources) soptions.push_back(std::move(c));
	}
	source_options_.swap(soptions);

	// Build Alphabetical Genre Dropdown Options for the active catalog.
	std::vector<Chip> goptions;
	if (!catalogs_.empty()) {
		const DiscoverCatalog &cat = catalogs_[active_cat];
		std::vector<std::string> raw_opts = cat.filter_options;
		if (raw_opts.empty() && (g_type == "movie" || g_type == "series")) {
			raw_opts = {
				"Action", "Adventure", "Animation", "Biography", "Comedy", "Crime",
				"Documentary", "Drama", "Family", "Fantasy", "History", "Horror",
				"Music", "Mystery", "Romance", "Sci-Fi", "Sport", "Thriller", "War", "Western"
			};
		}
		if (!raw_opts.empty()) {
			if (!cat.filter_required) {
				goptions.push_back({ "All Genres", "", -1 });
			}
			// Sort genres alphabetically A-Z (keep chronological order only if options are 4-digit years)
			const bool is_years = !raw_opts[0].empty() && std::isdigit((unsigned char)raw_opts[0][0]);
			if (!is_years) {
				std::stable_sort(raw_opts.begin(), raw_opts.end(), [](const std::string &a, const std::string &b) {
					return to_lower_copy(a) < to_lower_copy(b);
				});
			}
			for (const std::string &opt : raw_opts) {
				goptions.push_back({ opt, opt, -1 });
			}
			bool valid = (!cat.filter_required && g_filter_value.empty());
			for (const std::string &opt : raw_opts) {
				if (opt == g_filter_value) valid = true;
			}
			if (!valid) g_filter_value = cat.filter_required ? raw_opts[0] : "";
		} else {
			goptions.push_back({ "All Genres", "", -1 });
			g_filter_value.clear();
		}
	}
	genre_options_.swap(goptions);

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
					if (zone_ == GRID) zone_ = DROPDOWNS;
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
	rebuild_filters(true);
}

void DiscoverScreen::open_dropdown(int col) {
	dropdown_col_ = std::clamp(col, 0, 1);
	const std::vector<Chip> &opts = dropdown_col_ == 0 ? source_options_ : genre_options_;
	if (opts.empty()) return;
	dropdown_sel_ = 0;
	for (size_t i = 0; i < opts.size(); i++) {
		if (dropdown_col_ == 0) {
			if (i > 0 && opts[i].value == g_catalog_key) {
				dropdown_sel_ = (int)i;
				break;
			}
		} else {
			if (opts[i].value == g_filter_value) {
				dropdown_sel_ = (int)i;
				break;
			}
		}
	}
	const float row_h = S(48), row_gap = S(6);
	dropdown_scroll_ = std::max(0.0f, dropdown_sel_ * (row_h + row_gap) - S(120));
	dropdown_open_ = true;
}

void DiscoverScreen::apply_dropdown_selection() {
	const std::vector<Chip> &opts = dropdown_col_ == 0 ? source_options_ : genre_options_;
	if (dropdown_sel_ < 0 || dropdown_sel_ >= (int)opts.size()) {
		dropdown_open_ = false;
		return;
	}
	const Chip chosen = opts[dropdown_sel_];
	dropdown_open_ = false;
	if (dropdown_col_ == 0) {
		if (chosen.value != g_catalog_key) {
			g_catalog_key = chosen.value;
			g_filter_value.clear();
			g_cached_signature.clear();
			rebuild_filters(true);
		}
	} else {
		if (chosen.value != g_filter_value) {
			g_filter_value = chosen.value;
			g_cached_signature.clear();
			layout_chips();
			load_page(true);
		}
	}
}

void DiscoverScreen::move(int dx, int dy) {
	const int count = (int)cards_.size();
	const int cols = grid_columns();
	switch (zone_) {
		case FILTERS:
			if (dx != 0) {
				const int next = filter_idx_ + dx;
				if (next >= 0 && next < (int)filter_chips_.size()) {
					filter_idx_ = next;
				} else if (next >= (int)filter_chips_.size()) {
					zone_ = DROPDOWNS;
					dropdown_col_ = 0;
				}
			}
			if (dy > 0) zone_ = DROPDOWNS;
			break;
		case DROPDOWNS:
			if (dx < 0) {
				if (dropdown_col_ > 0) dropdown_col_--;
				else {
					zone_ = FILTERS;
					filter_idx_ = std::max(0, (int)filter_chips_.size() - 1);
				}
			} else if (dx > 0) {
				if (dropdown_col_ < 1) dropdown_col_++;
			}
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
				else zone_ = DROPDOWNS;
			} else if (dy > 0) {
				if (row < last_row) {
					selected_ = std::min(selected_ + cols, count - 1);
					if (selected_ / cols >= last_row - 1) load_page(false);
				} else {
					zone_ = BAR;
				}
			}
			break;
		}
		case BAR:
			if (dy < 0) zone_ = count > 0 ? GRID : DROPDOWNS;
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

	if (dropdown_open_) {
		const std::vector<Chip> &opts = dropdown_col_ == 0 ? source_options_ : genre_options_;
		const int n = (int)opts.size();
		if (pressed & (platform::BUTTON_BACK | platform::BUTTON_OPTIONS)) {
			dropdown_open_ = false;
			return;
		}
		if (n > 0) {
			if (pressed & platform::BUTTON_UP) {
				dropdown_sel_ = (dropdown_sel_ - 1 + n) % n;
			}
			if (pressed & platform::BUTTON_DOWN) {
				dropdown_sel_ = (dropdown_sel_ + 1) % n;
			}
			if (pressed & platform::BUTTON_L1) {
				dropdown_sel_ = std::max(0, dropdown_sel_ - 6);
			}
			if (pressed & platform::BUTTON_R1) {
				dropdown_sel_ = std::min(n - 1, dropdown_sel_ + 6);
			}
			if (pressed & platform::BUTTON_LEFT && dropdown_col_ == 1) {
				open_dropdown(0);
				return;
			}
			if (pressed & platform::BUTTON_RIGHT && dropdown_col_ == 0) {
				open_dropdown(1);
				return;
			}
			if (pressed & platform::BUTTON_ACCEPT) {
				apply_dropdown_selection();
				return;
			}
		}
		const float row_h = S(48), row_gap = S(6), max_list_h = S(420);
		dropdown_scroll_ = ui::follow(dropdown_scroll_, dropdown_sel_ * (row_h + row_gap), row_h, max_list_h, dt);
		return;
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
		} else if (zone_ == DROPDOWNS) {
			open_dropdown(dropdown_col_);
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
	for (int k = 0; k < 2; k++) {
		dropdown_focus_[k] = ui::approach(dropdown_focus_[k], (zone_ == DROPDOWNS && dropdown_col_ == k) ? 1.0f : 0.0f, dt);
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

	const int cols = grid_columns();
	const float gap = S(28), row_gap = S(20);
	const float card_w = (VIEW_W - (cols - 1) * gap) / cols;
	const float card_h = card_w * 1.5f;
	const float cell_h = card_h + POSTER_CAPTION_H;
	const float pitch = cell_h + row_gap;
	const float grid_top = S(204);
	const float view_h = tabs::CONTENT_BOTTOM - grid_top;

	if (zone_ == GRID && !cards_.empty()) {
		scroll_ = ui::follow(scroll_, (selected_ / cols) * pitch, cell_h, view_h, dt);
	}
	bar_.update(dt, zone_ == BAR);
}

void DiscoverScreen::draw_dropdown_menu(Renderer &r) {
	const std::vector<Chip> &opts = dropdown_col_ == 0 ? source_options_ : genre_options_;
	if (opts.empty()) return;

	// Subtle dark backdrop so the open dropdown pops cleanly above the poster grid
	r.rect(0, S(182), W, H - S(182), rgb(0x000000, 0.58f));

	const float menu_x = dropdown_x_[dropdown_col_];
	const float menu_y = S(182);
	const float menu_w = std::max(dropdown_w_[dropdown_col_], S(380));
	const float row_h = S(48), row_gap = S(6);
	const int visible_rows = std::min((int)opts.size(), 9);
	const float list_h = visible_rows * row_h + std::max(0, visible_rows - 1) * row_gap;
	const float menu_h = list_h + S(74);
	const float R = theme::RADIUS_BOX;

	r.rounded_rect(menu_x - S(3), menu_y - S(3), menu_w + S(6), menu_h + S(6), R + S(2), theme::PRIMARY.with_alpha(0.6f));
	r.rounded_rect(menu_x, menu_y, menu_w, menu_h, R, theme::BASE_200);

	const char *hdr = dropdown_col_ == 0 ? "SELECT SOURCE (A-Z)" : "SELECT GENRE (A-Z)";
	ui::badge(r, fonts_.small, hdr, menu_x + S(18), menu_y + S(14), theme::PRIMARY, theme::PRIMARY_CONTENT);

	char pos_buf[32];
	snprintf(pos_buf, sizeof(pos_buf), "%d / %d", dropdown_sel_ + 1, (int)opts.size());
	r.text(fonts_.small, pos_buf, menu_x + menu_w - S(18) - fonts_.small.measure(pos_buf), menu_y + S(32), theme::BASE_CONTENT_MUTED);

	const float list_top = menu_y + S(52);
	const float list_bottom = list_top + list_h;
	for (int i = 0; i < (int)opts.size(); i++) {
		const float iy = list_top + i * (row_h + row_gap) - dropdown_scroll_;
		if (iy + row_h < list_top - S(2) || iy > list_bottom + S(2)) continue;
		const bool is_sel = (i == dropdown_sel_);
		const bool is_active = (dropdown_col_ == 0)
				? ((i == 0 && active_source_label() == "All Sources") || (i > 0 && opts[i].value == g_catalog_key))
				: (opts[i].value == g_filter_value);

		const float rx = menu_x + S(12), rw = menu_w - S(24);
		r.rounded_rect(rx, iy, rw, row_h, S(10), is_sel ? theme::PRIMARY : (is_active ? theme::BASE_300 : theme::BASE_100));
		if (is_active) {
			ui::status(r, rx + S(20), iy + row_h / 2, is_sel ? theme::PRIMARY_CONTENT : theme::ACCENT);
		}
		const float tx = rx + (is_active ? S(38) : S(18));
		r.text(fonts_.body, fonts_.body.ellipsize(opts[i].label, rw - S(48)), tx, iy + row_h / 2 + S(7),
				is_sel ? theme::PRIMARY_CONTENT : theme::BASE_CONTENT);
	}
}

void DiscoverScreen::draw(Renderer &r) {
	const float grid_top = S(204);
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

	// Header band (covers cards scrolled under it): title, Movie/Series/Other + Popular/Featured row,
	// and neat alphabetical Source + Genre dropdown selectors below it.
	r.rect(0, 0, W, grid_top - S(16), theme::BASE_100);
	r.text(fonts_.page_header, "Discover", MARGIN_X, S(92), theme::BASE_CONTENT);

	const float chip_h = S(48);
	const float row0_y = S(74) - chip_h / 2;
	const float filter_x0 = MARGIN_X + fonts_.page_header.measure("Discover") + S(44);
	for (int pass = 0; pass < 2; pass++) {
		for (const Chip &c : filter_chips_) {
			if ((pass == 0) == (c.focus > 0.5f)) continue;
			const float cx = filter_x0 + c.x - filter_scroll_;
			if (cx < filter_x0 - 2 || cx + c.w > W - MARGIN_X + 2) continue;
			ui::chip(r, fonts_.body, c.label, cx, row0_y, c.w, chip_h, filter_chip_active(c), c.focus);
		}
	}

	// Row 1: Neat alphabetical dropdown buttons for Source (Netflix, Disney+...) and Genre
	const float drop_h = S(46);
	const float row1_y = S(124);
	const bool src_custom = (active_source_label() != "All Sources");
	const bool gen_custom = !g_filter_value.empty();
	const std::string src_label = "Source: " + active_source_label() + "  \xE2\x96\xBE";
	const std::string gen_label = "Genre: " + active_genre_label() + "  \xE2\x96\xBE";

	ui::chip(r, fonts_.body, src_label, dropdown_x_[0], row1_y, dropdown_w_[0], drop_h,
			src_custom || (dropdown_open_ && dropdown_col_ == 0), dropdown_focus_[0]);
	ui::chip(r, fonts_.body, gen_label, dropdown_x_[1], row1_y, dropdown_w_[1], drop_h,
			gen_custom || (dropdown_open_ && dropdown_col_ == 1), dropdown_focus_[1]);

	if (zone_ == DROPDOWNS && !dropdown_open_) {
		ui::hints(r, fonts_.small, { { "\xC3\x97", "Open Dropdown (A-Z)" }, { "\xE2\x97\x80 \xE2\x96\xB6", "Switch Dropdown" } },
				dropdown_x_[1] + dropdown_w_[1] + S(28), row1_y + drop_h / 2 + S(6));
	}

	if (dropdown_open_) {
		draw_dropdown_menu(r);
	}

	bar_.draw(r, fonts_);
}
