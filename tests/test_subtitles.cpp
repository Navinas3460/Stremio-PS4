#include "../src/core/subtitles.h"
#include "test.h"

TEST(subtitles_srt) {
	const std::string srt = "\xEF\xBB\xBF" // BOM
							"1\r\n00:00:01,000 --> 00:00:02,500\r\n<i>Hello</i>\r\nworld\r\n\r\n"
							"2\r\n01:02:03,004 --> 01:02:04,000\r\n{\\an8}Top &amp; centre\r\n";
	std::vector<SubtitleCue> cues;
	CHECK(parse_subtitles(srt, cues));
	CHECK_EQ(cues.size(), (size_t)2);
	CHECK_NEAR(cues[0].start, 1.0, 1e-6);
	CHECK_NEAR(cues[0].end, 2.5, 1e-6);
	CHECK_EQ(cues[0].text, "Hello\nworld");
	CHECK_NEAR(cues[1].start, 3723.004, 1e-6);
	CHECK_EQ(cues[1].text, "Top & centre");
}

TEST(subtitles_webvtt) {
	const std::string vtt = "WEBVTT\n\n00:05.000 --> 00:07.000 align:start\nShort timestamps\n\n"
							"cue-id\n00:00:10.000 --> 00:00:11.000\n<v Bob>Voice &lt;tag&gt;</v>\n";
	std::vector<SubtitleCue> cues;
	CHECK(parse_subtitles(vtt, cues));
	CHECK_EQ(cues.size(), (size_t)2);
	CHECK_NEAR(cues[0].start, 5.0, 1e-6);
	CHECK_NEAR(cues[0].end, 7.0, 1e-6);
	CHECK_EQ(cues[0].text, "Short timestamps");
	CHECK_EQ(cues[1].text, "Voice <tag>");
}

TEST(subtitles_sorted_and_lookup) {
	const std::string srt = "2\n00:00:10,000 --> 00:00:12,000\nsecond\n\n"
							"1\n00:00:01,000 --> 00:00:03,000\nfirst\n\n"
							"3\n00:00:11,000 --> 00:00:13,000\noverlapping\n";
	std::vector<SubtitleCue> cues;
	CHECK(parse_subtitles(srt, cues));
	CHECK_EQ(cues.size(), (size_t)3);
	CHECK_EQ(cues[0].text, "first");
	CHECK(cue_at(cues, 0.5) == nullptr);
	CHECK_EQ(cue_at(cues, 2.0)->text, "first");
	CHECK(cue_at(cues, 5.0) == nullptr);
	CHECK_EQ(cue_at(cues, 10.5)->text, "second");
	CHECK_EQ(cue_at(cues, 11.5)->text, "overlapping"); // latest start wins
	CHECK(cue_at(cues, 20.0) == nullptr);
}

TEST(subtitles_rejects_garbage) {
	std::vector<SubtitleCue> cues;
	CHECK(!parse_subtitles("", cues));
	CHECK(!parse_subtitles("<html>404 Not Found</html>", cues));
	CHECK(!parse_subtitles("1\n00:00:05,000 --> 00:00:01,000\nends before it starts\n", cues));
}
