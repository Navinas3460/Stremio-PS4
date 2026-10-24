// Addons tab: inspect installed Stremio add-ons, browse Community & Official
// add-ons, filter by capability (Streams, Catalogs, Subtitles, Meta), and
// install or delete add-ons synced with the signed-in Stremio account.
#pragma once

#include "../core/models.h"
#include "screen.h"
#include "tab_bar.h"

#include <memory>
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
		std::string id; // "installed", "community", "", "stream", "catalog", "subtitles", "meta", "sync"
		std::string label;
		float focus = 0;
	};

	struct Row {
		Addon addon;
		bool installed = false;
		float focus = 0;
	};

	void rebuild_rows();
	void sync_account();
	void toggle_selected_addon();

	Fonts &fonts_;
	Navigator &navigator_;
	tabs::TabBar bar_{ tabs::ADDONS };
	std::shared_ptr<int> alive_ = std::make_shared<int>(0);

	Zone zone_ = ZONE_LIST;
	std::vector<Chip> chips_;
	int chip_focus_ = 0;
	bool browse_community_ = false; // false = Installed, true = Community catalog
	std::string active_filter_;     // "" = all capabilities, or "stream", "catalog", "subtitles", "meta"

	std::vector<Row> rows_;
	int selected_ = 0;
	float scroll_ = 0;
	int seen_addons_rev_ = -1;
	int seen_community_rev_ = -1;
};
