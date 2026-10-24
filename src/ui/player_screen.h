// Full-screen video player. Controls: Cross play/pause, Left/Right seek 10 s
// (presses add up), Up opens Audio & subtitles, Down shows the controls,
// Circle back. Watch progress is reported to Stremio (stremio-core's player
// rules) so it resumes everywhere.
//
// Subtitles come from the video itself (embedded tracks, drawn by the
// backend's cues) or from subtitle add-ons (SRT/VTT, parsed here). The
// chosen subtitle and audio languages are remembered for the next video.
#pragma once

#include "../core/models.h"
#include "../core/subtitles.h"
#include "../video/video_player.h"
#include "components.h"
#include "screen.h"

#include <memory>
#include <string>
#include <vector>

class PlayerScreen : public Screen {
public:
	struct Args {
		StreamItem stream;
		std::string meta_id, type, video_id, title, subtitle;
		bool has_meta = false;
		MetaItem meta;
	};
	PlayerScreen(Fonts &fonts, Navigator &nav, const Args &args);
	~PlayerScreen() override;
	void update(double dt, uint32_t pressed) override;
	void draw(Renderer &r) override;
	Color backdrop() const override { return Color{ 0, 0, 0, 1 }; }

private:
	// One row of the subtitles column.
	struct SubtitleOption {
		enum Kind { OFF, EMBEDDED, ADDON } kind = OFF;
		int id = -1;          // embedded: track id; add-on: index into addon_subs_
		std::string lang;     // ISO code
		std::string label, detail;
	};
	struct Column {
		int selected = 0;
		float scroll = 0;
	};

	void queue_seek(double step);
	void exit();
	void show_overlay();

	void rebuild_subtitle_options();
	void choose_subtitle(const SubtitleOption &option, bool remember);
	void choose_audio(int track, bool remember);
	void auto_select();
	void update_menu(uint32_t pressed);
	void draw_subtitles(Renderer &r, float vx, float vy, float vw, float vh);
	void draw_menu(Renderer &r);

	Fonts &fonts_;
	Navigator &nav_;
	Args args_;
	std::unique_ptr<VideoPlayer> player_;
	std::shared_ptr<int> alive_ = std::make_shared<int>(0); // guards async callbacks
	double pending_seek_ = -1; // target while Left/Right presses are still coming
	double seek_timer_ = 0;
	double report_timer_ = 0;
	float overlay_ = 1;          // overlay opacity
	double overlay_timer_ = 0;
	bool session_ = false;
	VideoPlayer::State last_state_ = VideoPlayer::IDLE;
	ui::Toast toast_;

	// Tracks and subtitles.
	std::vector<SubtitleItem> addon_subs_;
	std::vector<SubtitleOption> sub_options_;
	std::vector<VideoPlayer::Track> audio_tracks_;
	SubtitleOption::Kind sub_kind_ = SubtitleOption::OFF;
	int sub_id_ = -1;                 // embedded track id or add-on index
	std::vector<SubtitleCue> addon_cues_;
	int addon_cues_for_ = -1;         // add-on index the cues belong to
	bool user_chose_subtitles_ = false, user_chose_audio_ = false;
	bool audio_auto_done_ = false;
	std::string pref_sub_lang_, pref_audio_lang_; // "" = none, "off" = subtitles off

	void check_next_episode();
	void trigger_autoplay(double countdown_seconds = 15.0);
	void cancel_autoplay();
	void fetch_next_streams_and_play();
	void play_next_stream(const StreamItem &stream);
	void draw_autoplay(Renderer &r);

	// The Audio & subtitles panel.
	bool menu_open_ = false;
	float menu_ = 0; // panel opacity
	int menu_column_ = 0; // 0 subtitles, 1 audio
	Column columns_[2];

	// Autoplay TV series
	bool autoplay_enabled_ = true;
	const Video *next_episode_ = nullptr;
	bool autoplay_active_ = false;
	bool autoplay_triggered_ = false;
	bool autoplay_dismissed_ = false;
	double autoplay_countdown_ = 0;
	double autoplay_total_countdown_ = 15.0;
	bool autoplay_loading_ = false;
	std::vector<StreamItem> next_streams_;
};
