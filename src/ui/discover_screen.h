// Discover tab (stremio-core CatalogWithFilters): pick a content type,
// primary catalog (Popular / Featured), or open neat alphabetical dropdowns
// for Streaming Source (Netflix, Disney+, HBO Max...) and Genre/Year filters.
#pragma once

#include "../core/stremio.h"
#include "card.h"
#include "screen.h"
#include "tab_bar.h"

#include <memory>
#include <string>
#include <vector>

class DiscoverScreen : public Screen {
public:
	DiscoverScreen(Fonts &fonts, Navigator &nav);
	void update(double dt, uint32_t pressed) override;
	void draw(Renderer &r) override;

private:
	enum Zone { FILTERS, DROPDOWNS, GRID, BAR };
	// A chip in the top filter bar (either a content type or Popular/Featured)
	// or an option inside the alphabetical Source / Genre dropdowns.
	struct Chip {
		std::string label, value;
		int catalog_index = -1; // >= 0 for a catalog chip, -1 for a type/genre chip
		float x = 0, w = 0, focus = 0;
	};

	void rebuild_filters(bool reload_items);
	void layout_chips();
	void load_page(bool reset);
	void sync_card_badges();
	void move(int dx, int dy);
	void activate_filter_chip();
	void open_dropdown(int col);
	void apply_dropdown_selection();
	void draw_dropdown_menu(Renderer &r);
	bool filter_chip_active(const Chip &c) const;
	std::string active_source_label() const;
	std::string active_genre_label() const;
	int grid_columns() const;

	Fonts &fonts_;
	Navigator &navigator_;
	tabs::TabBar bar_{ tabs::DISCOVER };
	std::shared_ptr<int> alive_ = std::make_shared<int>(0);

	std::vector<DiscoverCatalog> catalogs_;
	std::vector<Chip> filter_chips_; // Top row: Movies / Series / Other + Popular / Featured
	std::vector<Chip> source_options_; // Alphabetically sorted Sources (Netflix, Disney+, etc.)
	std::vector<Chip> genre_options_;  // Alphabetically sorted Genres (Action, Adventure, etc.)
	std::vector<Card> cards_;

	Zone zone_ = FILTERS;
	bool user_moved_ = false;
	int filter_idx_ = 0;
	int dropdown_col_ = 0; // 0 = Source dropdown, 1 = Genre dropdown
	float dropdown_focus_[2] = { 0.0f, 0.0f };
	float dropdown_x_[2] = { 0.0f, 0.0f };
	float dropdown_w_[2] = { 0.0f, 0.0f };
	bool dropdown_open_ = false;
	int dropdown_sel_ = 0;
	float dropdown_scroll_ = 0.0f;

	int selected_ = 0;
	float filter_scroll_ = 0;
	float scroll_ = 0;

	bool loading_ = false;
	bool loading_more_ = false;
	bool has_more_ = false;
	int fetch_gen_ = 0;
	std::string status_;
	bool status_is_error_ = false;
	int seen_board_rev_ = -1, seen_library_rev_ = -1, seen_notifications_rev_ = -1;
};
