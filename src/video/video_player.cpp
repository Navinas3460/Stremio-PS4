// Picks the video backend. FFmpeg plays everything (MKV, HEVC...) in
// software; on PS4, `backend=avplayer` in <data dir>/player.cfg switches to
// the hardware decoder (MP4/H.264 only).
#include "video_player.h"

#include "../platform/platform.h"

std::unique_ptr<VideoPlayer> create_ffmpeg_player();
#if defined(STREMIO_PS4)
std::unique_ptr<VideoPlayer> create_avplayer_player();
#endif

std::unique_ptr<VideoPlayer> VideoPlayer::create() {
#if defined(STREMIO_PS4)
	std::string cfg;
	if (platform::read_file(platform::data_dir() + "player.cfg", cfg) && cfg.find("backend=avplayer") != std::string::npos) {
		platform::log("player: AvPlayer backend");
		return create_avplayer_player();
	}
#endif
	platform::log("player: FFmpeg backend");
	return create_ffmpeg_player();
}
