#include "subtitles.h"

#include <algorithm>
#include <cstdlib>

namespace {

// "01:02:03,456" / "01:02:03.456" / "02:03.456" (WebVTT) -> seconds; -1 if invalid.
double parse_timestamp(const std::string &s) {
	int parts[4] = { 0, 0, 0, 0 };
	int n = 0, digits = 0;
	for (char c : s) {
		if (c >= '0' && c <= '9') {
			if (n == 0 && digits == 0) n = 1;
			parts[n - 1] = parts[n - 1] * 10 + (c - '0');
			digits++;
		} else if ((c == ':' || c == ',' || c == '.') && digits > 0 && n < 4) {
			n++;
			digits = 0;
		} else if (c == ' ' || c == '\t') {
			if (digits > 0) break;
		} else {
			break;
		}
	}
	if (n == 4) return parts[0] * 3600.0 + parts[1] * 60.0 + parts[2] + parts[3] / 1000.0;
	if (n == 3) return parts[0] * 60.0 + parts[1] + parts[2] / 1000.0;
	return -1;
}

// Removes <i>, </font ...>, {\an8} and similar markup.
std::string strip_tags(const std::string &line) {
	std::string out;
	for (size_t i = 0; i < line.size(); i++) {
		const char c = line[i];
		if (c == '<' || c == '{') {
			const char close = c == '<' ? '>' : '}';
			const size_t end = line.find(close, i);
			if (end != std::string::npos) {
				i = end;
				continue;
			}
		}
		out += c;
	}
	// Common HTML entities in WebVTT.
	const std::pair<const char *, const char *> entities[] = { { "&amp;", "&" }, { "&lt;", "<" }, { "&gt;", ">" }, { "&nbsp;", " " } };
	for (const auto &e : entities) {
		size_t at;
		while ((at = out.find(e.first)) != std::string::npos) out.replace(at, std::char_traits<char>::length(e.first), e.second);
	}
	return out;
}

} // namespace

bool parse_subtitles(const std::string &data_in, std::vector<SubtitleCue> &cues) {
	cues.clear();
	std::string data = data_in;
	if (data.compare(0, 3, "\xEF\xBB\xBF") == 0) data.erase(0, 3); // UTF-8 BOM
	std::vector<std::string> lines;
	size_t start = 0;
	while (start <= data.size()) {
		size_t end = data.find('\n', start);
		if (end == std::string::npos) end = data.size();
		std::string line = data.substr(start, end - start);
		if (!line.empty() && line.back() == '\r') line.pop_back();
		lines.push_back(line);
		start = end + 1;
	}
	for (size_t i = 0; i < lines.size(); i++) {
		const size_t arrow = lines[i].find("-->");
		if (arrow == std::string::npos) continue;
		SubtitleCue cue;
		cue.start = parse_timestamp(lines[i].substr(0, arrow));
		std::string rest = lines[i].substr(arrow + 3);
		rest.erase(0, rest.find_first_not_of(' '));
		cue.end = parse_timestamp(rest);
		if (cue.start < 0 || cue.end < cue.start) continue;
		// Text runs until a blank line.
		for (i++; i < lines.size() && !lines[i].empty(); i++) {
			if (lines[i].find("-->") != std::string::npos) { // malformed: no blank line
				i--;
				break;
			}
			const std::string text = strip_tags(lines[i]);
			if (!text.empty()) cue.text += (cue.text.empty() ? "" : "\n") + text;
		}
		if (!cue.text.empty()) cues.push_back(cue);
	}
	std::stable_sort(cues.begin(), cues.end(), [](const SubtitleCue &a, const SubtitleCue &b) { return a.start < b.start; });
	return !cues.empty();
}

const SubtitleCue *cue_at(const std::vector<SubtitleCue> &cues, double t) {
	// Cues starting after t can't show; among the rest, the latest start wins.
	auto it = std::upper_bound(cues.begin(), cues.end(), t, [](double v, const SubtitleCue &c) { return v < c.start; });
	for (int back = 0; it != cues.begin() && back < 16; back++) {
		--it;
		if (t < it->end) return &*it;
	}
	return nullptr;
}
