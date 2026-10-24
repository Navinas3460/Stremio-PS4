// Home screen (TV-1.png): "Continue watching" from the user's library, then
// one row per add-on catalog ("Popular - Movie", ...), bottom navigation and
// D-pad focus with the focus animation from docs/DESIGN.md.
#pragma once

#include "card.h"
#include "screen.h"
#include "tab_bar.h"

#include <vector>

class HomeScreen : public Screen {
public:
	HomeScreen(Fonts &fonts, Navigator &nav);
	void update(double dt, uint32_t pressed) override;
	void draw(Renderer &r) override;

private:
	void rebuild_rows();
	void move(int dx, int dy);
	float row_top(int index) const;

	Fonts &fonts_;
	Navigator &navigator_;
	std::vector<CardRow> rows_;
	tabs::TabBar bar_{ tabs::HOME };
	int focus_row_ = -1; // index into rows_, or -1 for the tab bar
	bool user_moved_ = false;
	float content_scroll_ = 0;
	int seen_library_rev_ = -1, seen_board_rev_ = -1, seen_notifications_rev_ = -1;
	struct ContextOption {
		std::string label;
		int id; // 1: Remove CW, 2: Toggle Library, 3: Details, 4: Cancel
	};
	bool context_menu_open_ = false;
	int context_menu_selected_ = 0;
	std::vector<ContextOption> context_options_;
	Card context_card_;
	void open_context_menu(const Card &card, bool is_cw);
	void draw_context_menu(Renderer &r);
#if defined(STREMIO_DEV)
	double fps_time_ = 0;
	int fps_frames_ = 0, fps_ = 0;
#endif
};
