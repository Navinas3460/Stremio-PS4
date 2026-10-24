// Details for one title: description, ratings (IMDb, Rotten Tomatoes,
// Metacritic), Cast & Director preview, "See More · Cast & Crew" modal with
// interactive actor/director search, episode list (series) and streams.
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
	struct PersonChip {
		std::string name;
		std::string role; // "Director", "Cast", "Writer"
		float focus = 0;
	};

	void on_meta(const MetaItem &meta);
	void on_ratings(const RatingsInfo &info);
	void rebuild_people();
	void load_streams(const std::string &video_id);
	void play(const StreamItem &stream);
	void toggle_library();
	void draw_more_modal(Renderer &r);

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
	// Season selector bar & episode preview card (for TV series).
	bool has_seasons() const { return seasons_.size() > 1; }
	void rebuild_seasons();
	void set_season(int idx);
	void rebuild_season_videos();
	std::string season_label(int season_num) const;
	std::vector<float> season_widths();
	void draw_seasons(Renderer &r, float x, float y, float w);
	void draw_episode_preview(Renderer &r, const Video &ep, float x, float y, float w, float h);

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
	bool has_ratings_ = false;
	RatingsInfo ratings_;
	std::string description_;
	std::string meta_error_;
	std::string video_id_;

	// "See More · Cast & Crew" button & modal state
	bool more_focused_ = false;
	float more_btn_focus_ = 0.0f;
	bool more_open_ = false;
	std::vector<PersonChip> people_;
	int person_selected_ = 0;

	// Seasons & filtered episodes for the selected season
	std::vector<int> seasons_;
	int season_idx_ = 0;
	std::vector<int> season_videos_; // indices into meta_.videos
	bool season_focused_ = false;
	std::vector<float> season_focus_;
	float season_scroll_ = 0;

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
