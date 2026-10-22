#include "../src/core/charset.h"
#include "test.h"

TEST(charset_utf8_passthrough) {
	CHECK(is_utf8("plain ASCII"));
	CHECK(is_utf8("\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82")); // Привет
	CHECK(!is_utf8("\xCF\xF0\xE8\xE2\xE5\xF2")); // Привет in Windows-1251
	CHECK(!is_utf8("\xE4\xBD")); // truncated
	CHECK_EQ(to_utf8("\xEF\xBB\xBFHello", "eng"), "Hello");
	CHECK_EQ(to_utf8("\xD0\x9F\xD1\x80\xD0\xB8", "ara"), "\xD0\x9F\xD1\x80\xD0\xB8"); // UTF-8 wins over the language
}

TEST(charset_legacy_by_language) {
	CHECK_EQ(to_utf8("\xCF\xF0\xE8\xE2\xE5\xF2", "rus"), "Привет");
	CHECK_EQ(to_utf8("\xE3\xD1\xCD\xC8\xC7", "ar"), "مرحبا");
	CHECK_EQ(to_utf8("\xF9\xEC\xE5\xED", "heb"), "שלום");
	CHECK_EQ(to_utf8("\xC3\xE5\xE9\xDC", "gre"), "Γειά");
	CHECK_EQ(to_utf8("\xC4\xE3\xBA\xC3", "chi"), "你好");                 // GBK
	CHECK_EQ(to_utf8("\xA7\x41\xA6\x6E\xA1\x41\xA7\xDA\xAC\x4F", "zht"), "你好，我是"); // Big5
	CHECK_EQ(to_utf8("\x82\xB1\x82\xF1\x82\xC9\x82\xBF\x82\xCD", "jpn"), "こんにちは"); // Shift_JIS
	CHECK_EQ(to_utf8("\xBE\xC8\xB3\xE7", "kor"), "안녕");                 // EUC-KR
	CHECK_EQ(to_utf8("caf\xE9", "fre"), "café");                          // Windows-1252
	CHECK_EQ(to_utf8("\x9Ajedi", "pol"), "šjedi");                       // Windows-1250
}

TEST(charset_chinese_big5_vs_gbk) {
	// Big5 bytes are valid GBK too; the common-character score picks Big5.
	const std::string big5 = "\xA7\x41\xAA\xBE\xB9\x44\xA7\xDA\xA6\x62\xBB\xA1\xA4\xB0\xBB\xF2\xB6\xDC"; // 你知道我在說什麼嗎
	CHECK_EQ(to_utf8(big5, "chi"), "你知道我在說什麼嗎");
}

TEST(charset_utf16) {
	const std::string le("\xFF\xFE" "H\0i\0", 6);
	CHECK_EQ(to_utf8(le, ""), "Hi");
	const std::string be("\xFE\xFF\0H\0i", 6);
	CHECK_EQ(to_utf8(be, ""), "Hi");
	// No BOM: every other byte is zero.
	const std::string text = "1\r\n00:00:01,000 --> 00:00:02,000\r\nHello\r\n";
	std::string no_bom;
	for (char c : text) {
		no_bom += c;
		no_bom += '\0';
	}
	CHECK_EQ(to_utf8(no_bom, "eng"), text);
}

TEST(charset_serbian_scripts) {
	CHECK_EQ(to_utf8("\xC7\xE4\xF0\xE0\xE2\xEE", "srp"), "Здраво"); // Cyrillic, Windows-1251
	CHECK_EQ(to_utf8("Ka\x9E" "e \x9A" "ta", "srp"), "Kaže šta");           // Latin, Windows-1250
}
