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

// Cleans up legacy `pro_4k=1` configs that forced `backend=avplayer` in older builds.
void migrate_legacy_pro_4k() {
	static bool checked = false;
	if (checked) return;
	checked = true;
	const std::string path = platform::data_dir() + PLAYER_CFG;
	std::string cfg;
	if (platform::read_file(path, cfg) && cfg.find("pro_4k=1") != std::string::npos) {
		const bool autoplay = (cfg.find("autoplay=0") == std::string::npos);
		if (autoplay) {
			platform::remove_file(path);
		} else {
			platform::write_file(path, "autoplay=0\n");
		}
		platform::log("player: migrated legacy pro_4k=1 config back to default FFmpeg backend");
	}
}
} // namespace

bool VideoPlayer::hardware_selected() {
	migrate_legacy_pro_4k();
	std::string cfg;
	return platform::read_file(platform::data_dir() + PLAYER_CFG, cfg) && cfg.find("backend=avplayer") != std::string::npos;
}

void VideoPlayer::select_hardware(bool hardware) {
	migrate_legacy_pro_4k();
	const std::string path = platform::data_dir() + PLAYER_CFG;
	const bool autoplay = autoplay_selected();
	std::string out;
	if (hardware) out += "backend=avplayer\n";
	if (!autoplay) out += "autoplay=0\n";
	if (out.empty()) {
		platform::remove_file(path);
	} else {
		platform::write_file(path, out);
	}
}

bool VideoPlayer::autoplay_selected() {
	migrate_legacy_pro_4k();
	std::string cfg;
	if (platform::read_file(platform::data_dir() + PLAYER_CFG, cfg)) {
		if (cfg.find("autoplay=0") != std::string::npos) return false;
	}
	return true; // Enabled by default
}

void VideoPlayer::select_autoplay(bool enabled) {
	migrate_legacy_pro_4k();
	const std::string path = platform::data_dir() + PLAYER_CFG;
	std::string cfg;
	platform::read_file(path, cfg);
	const bool hw = (cfg.find("backend=avplayer") != std::string::npos);
	std::string out;
	if (hw) out += "backend=avplayer\n";
	if (!enabled) out += "autoplay=0\n";
	if (out.empty()) {
		platform::remove_file(path);
	} else {
		platform::write_file(path, out);
	}
}

std::unique_ptr<VideoPlayer> VideoPlayer::create() {
#if defined(STREMIO_PS4)
	if (hardware_selected()) {
		platform::log("player: AvPlayer hardware backend");
		return create_avplayer_player();
	}
#endif
	platform::log("player: FFmpeg backend");
	return create_ffmpeg_player();
}
