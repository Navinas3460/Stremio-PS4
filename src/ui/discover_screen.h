// Discover tab (stremio-core CatalogWithFilters): browse one add-on catalog at
// a time as a poster grid. Square opens the filters, a drop-down with the
// type (Movies / Series / ...), catalog (Popular, New...) and its genre or
// year. More titles load as the selection nears the end of the grid.
#pragma once

#include "card.h"
#include "screen.h"
#include "tab_bar.h"

#include <string>
#include <vector>

class DiscoverScreen : public Screen {
public:
	DiscoverScreen(Fonts &fonts, Navigator &nav);
	void update(double dt, uint32_t pressed) override;
	void draw(Renderer &r) override;

private:
	enum Zone { GRID, BAR };
	// The filter drop-down's columns: type, catalog, genre / year.
	enum { TYPE, CATALOG, FILTER, COLUMNS };
	struct Column {
		int selected = 0;
		float scroll = 0;
	};

	void rebuild();
	void move(int dx, int dy);
	std::vector<std::string> column_items(int column) const;
	int column_active(int column) const; // the chosen item
	void open_menu();
	void update_menu(uint32_t pressed);
	void draw_menu(Renderer &r);

	Fonts &fonts_;
	Navigator &navigator_;
	tabs::TabBar bar_{ tabs::DISCOVER };
	std::vector<Card> cards_;
	Zone zone_ = GRID;
	bool user_moved_ = false;
	int selected_ = 0;
	float scroll_ = 0;
	std::string status_; // instead of the grid: loading, error, empty
	bool status_is_error_ = false;
	int seen_discover_rev_ = -1, seen_library_rev_ = -1, seen_notifications_rev_ = -1;

	bool menu_open_ = false;
	float menu_ = 0; // drop-down opacity
	int menu_column_ = TYPE;
	Column columns_[COLUMNS];
};
