// Search tab: an on-screen keyboard (left) and one row of results per add-on
// catalog that supports searching (right). Searches after a short pause in
// typing, like Stremio's TV app.
#pragma once

#include "card.h"
#include "screen.h"
#include "tab_bar.h"

#include <string>
#include <vector>

class SearchScreen : public Screen {
public:
	SearchScreen(Fonts &fonts, Navigator &nav);
	void update(double dt, uint32_t pressed) override;
	void draw(Renderer &r) override;

private:
	enum Area { KEYBOARD, RESULTS, BAR };
	struct Key {
		std::string label, text; // text typed; "" for the special keys
		int col, row, span;
		float focus = 0;
	};
	void build_keys();
	int key_at(int col, int row) const;
	void press_key(const Key &k);
	void move_keyboard(int dx, int dy);
	void move_results(int dx, int dy);
	void rebuild_rows();

	Fonts &fonts_;
	Navigator &navigator_;
	tabs::TabBar bar_{ tabs::SEARCH };
	Area area_ = KEYBOARD;
	std::vector<Key> keys_;
	int key_ = 0;
	std::string query_;
	float search_delay_ = -1; // seconds until the typed query is searched
	std::vector<CardRow> rows_;
	int row_ = 0;
	float content_scroll_ = 0;
	int seen_search_rev_ = -1;
	float cursor_time_ = 0;
};
