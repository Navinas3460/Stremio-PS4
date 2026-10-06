// Video playback behind one interface:
//   video_ffmpeg.cpp   desktop: FFmpeg decoding, SDL audio
//   video_avplayer.cpp PS4: sceAvPlayer (hardware decoder) + sceAudioOut
// Frames are delivered as an RGBA texture (the PS4 has no YUV shader, so
// NV12 -> RGBA happens on a CPU thread).
#pragma once

#include "../render/renderer.h"

#include <memory>
#include <string>
#include <vector>

class VideoPlayer {
public:
	enum State { IDLE, OPENING, BUFFERING, PLAYING, PAUSED, ENDED, FAILED };

	// An audio or embedded subtitle track of the open stream.
	struct Track {
		int id = -1;         // backend's stream id
		std::string lang;    // ISO 639 code from the file, may be empty
		std::string label;   // e.g. "English · 5.1 E-AC3", "English (SDH)"
	};
	// The embedded subtitle showing now: text, or an image (PGS/DVD).
	struct Subtitle {
		std::string text;               // lines separated by '\n'
		const Texture *image = nullptr; // when bitmap
		float x = 0, y = 0, w = 0, h = 0; // image rect as fractions of the video frame
	};

	static std::unique_ptr<VideoPlayer> create();
	virtual ~VideoPlayer() = default;

	// `headers`: extra HTTP request headers ("Name: value").
	virtual void open(const std::string &url, const std::vector<std::string> &headers, double start_seconds) = 0;
	virtual void play() = 0;
	virtual void pause() = 0;
	virtual void seek(double seconds) = 0;

	// Main thread, once per frame: uploads the newest frame, advances state.
	virtual void update() = 0;

	virtual State state() const = 0;
	virtual double position() const = 0; // seconds
	virtual double duration() const = 0; // seconds, 0 if unknown
	virtual std::string error() const = 0;
	virtual const Texture &frame() const = 0; // invalid until the first frame

	// Tracks (known once playing). Selecting one refills the buffers at the
	// current position. Backends without track support keep the defaults.
	virtual std::vector<Track> audio_tracks() const { return {}; }
	virtual int audio_track() const { return -1; }
	virtual void set_audio_track(int) {}
	virtual std::vector<Track> subtitle_tracks() const { return {}; }
	virtual int subtitle_track() const { return -1; } // -1 = off
	virtual void set_subtitle_track(int) {}
	// The embedded subtitle to show now (main thread). False if none.
	virtual bool current_subtitle(Subtitle &) { return false; }
};
