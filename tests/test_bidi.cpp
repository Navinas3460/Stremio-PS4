#include "../src/render/bidi.h"
#include "test.h"

#include <string>

namespace {

std::vector<uint32_t> codepoints(const std::string &s) {
	std::vector<uint32_t> out;
	for (size_t i = 0; i < s.size();) {
		const unsigned char c = (unsigned char)s[i++];
		if (c < 0x80) {
			out.push_back(c);
			continue;
		}
		int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
		uint32_t cp = c & (0x3F >> extra);
		while (extra-- > 0) cp = (cp << 6) | ((unsigned char)s[i++] & 0x3F);
		out.push_back(cp);
	}
	return out;
}

// Display order as text, with Hebrew letters standing in for themselves.
std::string visual(const std::string &logical) {
	const std::vector<uint32_t> cps = codepoints(logical);
	std::vector<uint8_t> levels;
	bidi::resolve(cps, levels);
	std::vector<uint32_t> out;
	for (int i : bidi::visual_order(levels)) out.push_back(cps[i]);
	std::string s;
	for (uint32_t c : out) {
		if (c < 0x80) {
			s += (char)c;
		} else if (c < 0x800) {
			s += (char)(0xC0 | (c >> 6));
			s += (char)(0x80 | (c & 0x3F));
		} else {
			s += (char)(0xE0 | (c >> 12));
			s += (char)(0x80 | ((c >> 6) & 0x3F));
			s += (char)(0x80 | (c & 0x3F));
		}
	}
	return s;
}

} // namespace

TEST(bidi_left_to_right_untouched) {
	CHECK_EQ(visual("Hello, world!"), "Hello, world!");
}

TEST(bidi_hebrew_reversed) {
	std::vector<uint8_t> levels;
	CHECK_EQ(bidi::resolve(codepoints("שלום"), levels), 1);
	CHECK_EQ(visual("שלום"), "םולש");
	// Trailing punctuation of an RTL line ends up on its left.
	CHECK_EQ(visual("שלום!"), "!םולש");
}

TEST(bidi_numbers_keep_their_order) {
	// "בשנת 2024 ." : the number stays left to right inside RTL text.
	CHECK_EQ(visual("בשנת 2024"), "2024 תנשב");
	CHECK_EQ(visual("מחיר 12.50$"), "12.50$ ריחמ");
}

TEST(bidi_mixed_ltr_paragraph) {
	CHECK_EQ(visual("I said שלום to her"), "I said םולש to her");
	CHECK_EQ(visual("שלום Bob ושלום"), "םולשו Bob םולש");
}

TEST(bidi_arabic_with_marks) {
	// Marks keep their base's level; Arabic digits are AN.
	const std::vector<uint32_t> text = { 0x0628, 0x0650, 0x0633, 0x0020, 0x0661, 0x0662 }; // بِس ١٢
	std::vector<uint8_t> levels;
	CHECK_EQ(bidi::resolve(text, levels), 1);
	CHECK_EQ((int)levels[1], 1);
	CHECK_EQ((int)levels[4], 2);
	const std::vector<int> order = bidi::visual_order(levels);
	CHECK_EQ(order.size(), (size_t)6);
	CHECK_EQ(order[0], 4); // "١٢" on the left, in reading order
	CHECK_EQ(order[1], 5);
	CHECK_EQ(order[5], 0);
}
