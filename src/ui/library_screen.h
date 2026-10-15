// Library tab (stremio-core LibraryWithFilters): every title in the user's
// library as a poster grid, filtered by type (All / Movies / Series / ...)
// and sorted by last watched, name or times watched.
#pragma once

#include "card.h"
#include "screen.h"
#include "tab_bar.h"

#include <string>
#include <vector>

class LibraryScreen : public Screen {
public:
	LibraryScreen(Fonts &fonts, Navigator &nav);
	void update(double dt, uint32_t pressed) override;
	void draw(Renderer &r) override;

private:
	enum Zone { FILTERS, GRID, BAR };
	// A pill of the filter row: a type ("" = all) or a sort order.
	struct Chip {
		std::string label, type;
		int sort = -1; // a Stremio::LibrarySort, or -1 for a type chip
		float x = 0, w = 0, focus = 0;
	};
	// Rebuilds the chips and cards from the library; `keep_selection` keeps
	// the focused title (by id) when it's still there.
	void rebuild(bool keep_selection);
	void move(int dx, int dy);
	void activate_chip();
	bool chip_active(const Chip &c) const;

	Fonts &fonts_;
	Navigator &navigator_;
	tabs::TabBar bar_{ tabs::LIBRARY };
	std::vector<Chip> chips_;
	std::vector<Card> cards_;
	Zone zone_ = GRID;
	bool user_moved_ = false;
	int chip_ = 0, selected_ = 0;
	float scroll_ = 0;
	std::string status_; // instead of the grid: loading, error, empty
	bool status_is_error_ = false;
	int seen_library_rev_ = -1, seen_notifications_rev_ = -1;
};
