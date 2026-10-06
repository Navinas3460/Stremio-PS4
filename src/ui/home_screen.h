// Home screen (TV-1.png): "Continue watching" from the user's library, then
// one row per add-on catalog ("Popular - Movie", ...), bottom navigation and
// D-pad focus with the focus animation from docs/DESIGN.md.
#pragma once

#include "card.h"
#include "screen.h"
#include "tab_bar.h"

#include <string>
#include <vector>

class HomeScreen : public Screen {
public:
	HomeScreen(Fonts &fonts, Navigator &nav);
	void update(double dt, uint32_t pressed) override;
	void draw(Renderer &r) override;

private:
	struct Row {
		std::string title;
		std::string status; // shown instead of cards (loading / empty / error)
		bool status_is_error = false;
		std::vector<Card> cards;
		float scroll = 0;   // animated x offset
		int selected = 0;
	};

	void rebuild_rows();
	void move(int dx, int dy);
	int next_focusable_row(int from, int step) const;
	float row_height(const Row &row) const;
	float row_top(int index) const;
	void activate();

	Fonts &fonts_;
	Navigator &navigator_;
	std::vector<Row> rows_;
	tabs::TabBar bar_{ tabs::HOME };
	int focus_row_ = -1; // index into rows_, or -1 for the tab bar
	bool user_moved_ = false;
	float content_scroll_ = 0;
	int seen_library_rev_ = -1, seen_board_rev_ = -1, seen_notifications_rev_ = -1;
	double fps_time_ = 0;
	int fps_frames_ = 0, fps_ = 0;
};
