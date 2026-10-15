// Picks the video backend. FFmpeg plays everything (MKV, HEVC...) in
// software; on PS4, Settings can switch to the hardware decoder (MP4/H.264).
#include "video_player.h"

#include "../platform/platform.h"

std::unique_ptr<VideoPlayer> create_ffmpeg_player();
#if defined(STREMIO_PS4)
std::unique_ptr<VideoPlayer> create_avplayer_player();
#endif

namespace {
const char *PLAYER_CFG = "player.cfg";
}

bool VideoPlayer::hardware_selected() {
	std::string cfg;
	return platform::read_file(platform::data_dir() + PLAYER_CFG, cfg) && cfg.find("backend=avplayer") != std::string::npos;
}

void VideoPlayer::select_hardware(bool hardware) {
	const std::string path = platform::data_dir() + PLAYER_CFG;
	if (hardware) {
		platform::write_file(path, "backend=avplayer\n");
	} else {
		platform::remove_file(path);
	}
}

std::unique_ptr<VideoPlayer> VideoPlayer::create() {
#if defined(STREMIO_PS4)
	if (hardware_selected()) {
		platform::log("player: AvPlayer backend");
		return create_avplayer_player();
	}
#endif
	platform::log("player: FFmpeg backend");
	return create_ffmpeg_player();
}
