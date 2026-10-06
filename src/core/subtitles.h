// Subtitle files from add-ons: SRT and WebVTT parsing into timed cues.
#pragma once

#include <string>
#include <vector>

struct SubtitleCue {
	double start = 0, end = 0; // seconds
	std::string text;          // lines separated by '\n', tags removed
};

// Parses SRT or WebVTT (detected by content). Cues are sorted by start.
// Returns false if nothing could be read.
bool parse_subtitles(const std::string &data, std::vector<SubtitleCue> &cues);

// The cue showing at `t` (latest start wins), or nullptr.
const SubtitleCue *cue_at(const std::vector<SubtitleCue> &cues, double t);
