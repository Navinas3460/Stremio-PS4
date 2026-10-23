// Addons tab: inspect installed Stremio add-ons, filter by capability
// (Streams, Catalogs, Subtitles, Meta), view manifest details and catalogs,
// and sync with the signed-in Stremio account.
#pragma once

#include "../core/models.h"
#include "screen.h"
#include "tab_bar.h"

#include <string>
#include <vector>

class AddonsScreen : public Screen {
public:
	AddonsScreen(Fonts &fonts, Navigator &nav);
	void update(double dt, uint32_t pressed) override;
	void draw(Renderer &r) override;

private:
	enum Zone { ZONE_CHIPS, ZONE_LIST, ZONE_BAR };

	struct Chip {
		std::string id; // "", "stream", "catalog", "subtitles", "meta", or "sync"
		std::string label;
		float focus = 0;
	};

	struct Row {
		Addon addon;
		float focus = 0;
	};

	void rebuild_rows();
	void sync_account();

	Fonts &fonts_;
	Navigator &navigator_;
	tabs::TabBar bar_{ tabs::ADDONS };

	Zone zone_ = ZONE_LIST;
	std::vector<Chip> chips_;
	int chip_focus_ = 0;
	std::string active_filter_; // "" = all

	std::vector<Row> rows_;
	int selected_ = 0;
	float scroll_ = 0;
	int seen_addons_rev_ = -1;
};
