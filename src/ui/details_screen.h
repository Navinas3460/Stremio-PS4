// Details for one title: description, episode list (series) and the streams
// for the selected video from all add-ons. Picking a playable stream opens
// the player (stremio-core MetaDetails).
#pragma once

#include "../core/models.h"
#include "components.h"
#include "screen.h"

#include <memory>
#include <string>
#include <vector>

class DetailsScreen : public Screen {
public:
	struct Target {
		std::string meta_id, type, name, poster, release_info;
		std::vector<std::string> images;
	};
	DetailsScreen(Fonts &fonts, Navigator &nav, const Target &target);
	void update(double dt, uint32_t pressed) override;
	void draw(Renderer &r) override;

private:
	struct ListState {
		int selected = 0;
		float scroll = 0;
		std::vector<float> focus; // animated per row
	};
	void on_meta(const MetaItem &meta);
	void load_streams(const std::string &video_id);
	void play(const StreamItem &stream);
	void toggle_library();
	// One row of a list: a title, an optional second line, greyed out if
	// `muted`.
	struct ListRow {
		std::string line1, line2;
		bool muted = false;
		std::string badge; // optional, at the right end of line 1
		Color badge_bg;
		std::string note; // optional, muted, before the badge (e.g. the size)
	};
	void draw_list(Renderer &r, ListState &list, const std::vector<ListRow> &rows, int active, float x, float y, float w,
			float h);
	// Add-on filter above the streams (a small tab bar): All, then each add-on
	// in the order they answered. Shown once two or more have streams.
	bool has_filter() const { return addons_.size() > 1; }
	void set_filter(int filter);
	void refilter(); // rebuilds visible_ from streams_ and filter_
	void draw_filter(Renderer &r, float x, float y, float w);
	std::vector<float> filter_widths(); // per chip

	Fonts &fonts_;
	Navigator &nav_;
	Target target_;
	std::shared_ptr<int> alive_ = std::make_shared<int>(0); // guards async callbacks

	bool has_meta_ = false;
	MetaItem meta_;
	std::string description_;
	std::string meta_error_;
	std::string video_id_;

	std::vector<StreamItem> streams_;
	std::vector<std::string> addons_;
	int filter_ = 0;           // 0 = All, else addons_[filter_ - 1]
	std::vector<int> visible_; // indices into streams_ that pass the filter
	bool filter_focused_ = false;
	std::vector<float> filter_focus_;
	float filter_scroll_ = 0;
	bool streams_loading_ = false;
	std::string streams_status_;

	ListState episodes_list_, streams_list_;
	int column_ = 1; // 0 = episodes, 1 = streams
	ui::Toast toast_;
};
