#include "test.h"

#include "../src/torrent/bencode.h"
#include "../src/torrent/magnet.h"
#include "../src/torrent/sha1.h"

#include <cstring>

using namespace torrent;

TEST(torrent_sha1) {
	uint8_t h[20];
	sha1("abc", 3, h);
	CHECK_EQ(to_hex(h, 20), "a9993e364706816aba3e25717850c26c9cd0d89d");
	sha1("", 0, h);
	CHECK_EQ(to_hex(h, 20), "da39a3ee5e6b4b0d3255bfef95601890afd80709");
	// Fed in odd-sized parts, crossing block boundaries.
	std::string million(1000000, 'a');
	Sha1 s;
	for (size_t i = 0; i < million.size(); i += 777) s.update(million.data() + i, std::min<size_t>(777, million.size() - i));
	s.final(h);
	CHECK_EQ(to_hex(h, 20), "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
}

TEST(torrent_bencode) {
	BNode d;
	CHECK(bdecode(std::string("d3:bar4:spam3:fooi42e4:listli1ei-2e1:xee"), d));
	CHECK(d.type == BNode::DICT);
	CHECK_EQ(d.get_string("bar"), "spam");
	CHECK_EQ(d.get_int("foo"), (int64_t)42);
	const BNode *list = d.get("list");
	CHECK(list && list->list.size() == 3);
	CHECK_EQ(list->list[1].i, (int64_t)-2);
	CHECK_EQ(d.get_int("missing", 7), (int64_t)7);

	// Raw span of a nested value (how the info dict is hashed).
	const std::string text = "d4:infod6:lengthi5eee";
	CHECK(bdecode(text, d));
	const BNode *info = d.get("info");
	CHECK(info);
	CHECK_EQ(std::string((const char *)info->begin, info->end - info->begin), "d6:lengthi5ee");

	// A value followed by raw bytes (ut_metadata data messages).
	const std::string msg = "d8:msg_typei1ee" + std::string("\x01\x02\x03", 3);
	const uint8_t *p = (const uint8_t *)msg.data();
	CHECK(bdecode(p, p + msg.size(), d));
	CHECK_EQ((size_t)(p - (const uint8_t *)msg.data()), msg.size() - 3);

	CHECK(!bdecode(std::string("i42"), d));        // unterminated
	CHECK(!bdecode(std::string("5:abc"), d));      // string past the end
	CHECK(!bdecode(std::string("i1ei2e"), d));     // trailing data
	CHECK(!bdecode(std::string("d3:fooe"), d));    // key without value
	CHECK_EQ(bstr("spam") + bint(-3), "4:spami-3e");
}

TEST(torrent_magnet) {
	Magnet m;
	CHECK(parse_magnet("magnet:?xt=urn:btih:08ada5a7a6183aae1e09d831df6748d566095a10&dn=Sintel"
					   "&tr=udp%3A%2F%2Fexplodie.org%3A6969&tr=wss%3A%2F%2Ftracker.btorrent.xyz&so=3",
			m));
	CHECK_EQ(to_hex(m.info_hash, 20), "08ada5a7a6183aae1e09d831df6748d566095a10");
	CHECK_EQ(m.name, "Sintel");
	CHECK_EQ(m.file_index, 3);
	CHECK_EQ(m.trackers.size(), (size_t)2);
	CHECK_EQ(m.trackers[0], "udp://explodie.org:6969");

	// Base32 info-hash, no file selection.
	Magnet b;
	CHECK(parse_magnet("magnet:?xt=urn:btih:BCW2LJ5GDA5K4HQJ3AY56Z2I2VTASWQQ", b));
	CHECK_EQ(to_hex(b.info_hash, 20), "08ada5a7a6183aae1e09d831df6748d566095a10");
	CHECK_EQ(b.file_index, -1);

	CHECK(!parse_magnet("magnet:?dn=no-hash", b));
	CHECK(!parse_magnet("https://example.com/a.torrent", b));
	CHECK(is_magnet("magnet:?xt=urn:btih:x"));

	const std::string made = make_magnet("08ada5a7a6183aae1e09d831df6748d566095a10", 1, { "udp://t.example:80/announce" });
	CHECK_EQ(made, "magnet:?xt=urn:btih:08ada5a7a6183aae1e09d831df6748d566095a10&so=1&tr=udp%3A%2F%2Ft.example%3A80%2Fannounce");
	Magnet back;
	CHECK(parse_magnet(made, back));
	CHECK_EQ(back.file_index, 1);
	CHECK_EQ(back.trackers[0], "udp://t.example:80/announce");
}
