// Details for one title: description, episode list (series) and the streams
// for the selected video from all add-ons. Picking a playable stream opens
// the player (stremio-core MetaDetails).
#pragma once

#include "../core/models.h"
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
	void draw_list(Renderer &r, ListState &list, const std::vector<std::string> &lines1,
			const std::vector<std::string> &lines2, const std::vector<bool> &muted, int active, bool focused, float x,
			float y, float w, float h);

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
	bool streams_loading_ = false;
	std::string streams_status_;

	ListState episodes_list_, streams_list_;
	int column_ = 1; // 0 = episodes, 1 = streams
	std::string toast_;
	float toast_time_ = 0;
};
