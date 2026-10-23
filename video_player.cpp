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
	std::string cfg;
	platform::read_file(path, cfg);
	const bool pro_4k = (cfg.find("pro_4k=1") != std::string::npos);
	std::string out;
	if (hardware) out += "backend=avplayer\n";
	if (pro_4k) out += "pro_4k=1\n";
	if (out.empty()) {
		platform::remove_file(path);
	} else {
		platform::write_file(path, out);
	}
}

bool VideoPlayer::pro_4k_selected() {
	std::string cfg;
	return platform::read_file(platform::data_dir() + PLAYER_CFG, cfg) && cfg.find("pro_4k=1") != std::string::npos;
}

void VideoPlayer::select_pro_4k(bool enabled) {
	const std::string path = platform::data_dir() + PLAYER_CFG;
	std::string cfg;
	platform::read_file(path, cfg);
	const bool hw = (cfg.find("backend=avplayer") != std::string::npos);
	std::string out;
	// 4K video playback requires the hardware AvPlayer backend on PS4 Pro
	if (hw || enabled) out += "backend=avplayer\n";
	if (enabled) out += "pro_4k=1\n";
	if (out.empty()) {
		platform::remove_file(path);
	} else {
		platform::write_file(path, out);
	}
}

std::unique_ptr<VideoPlayer> VideoPlayer::create() {
#if defined(STREMIO_PS4)
	if (hardware_selected() || pro_4k_selected()) {
		platform::log("player: AvPlayer hardware backend (PS4 Pro 4K mode: %s)", pro_4k_selected() ? "active" : "disabled");
		return create_avplayer_player();
	}
#endif
	platform::log("player: FFmpeg backend");
	return create_ffmpeg_player();
}
