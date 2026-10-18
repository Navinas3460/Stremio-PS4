// Bencode (BitTorrent's serialisation): a decoder into a small tree, and
// helpers to write it. Strings keep raw bytes; `begin`/`end` point at a
// node's encoding inside the decoded buffer (the info dict's hash needs it).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace torrent {

struct BNode {
	enum Type : char { NONE = 0, INT = 'i', STRING = 's', LIST = 'l', DICT = 'd' };
	Type type = NONE;
	int64_t i = 0;
	std::string s;
	std::vector<BNode> list;
	std::vector<std::pair<std::string, BNode>> dict;
	const uint8_t *begin = nullptr, *end = nullptr;

	const BNode *get(const char *key) const;
	int64_t get_int(const char *key, int64_t fallback = -1) const;
	std::string get_string(const char *key) const; // "" if missing
};

// Decodes one value starting at `p`, advancing it past the value (any bytes
// after it, like ut_metadata's piece data, are left alone).
bool bdecode(const uint8_t *&p, const uint8_t *end, BNode &out);
bool bdecode(const std::string &data, BNode &out); // the whole string must be one value

std::string bstr(const std::string &s); // "4:spam"
std::string bint(int64_t v);            // "i42e"

} // namespace torrent
