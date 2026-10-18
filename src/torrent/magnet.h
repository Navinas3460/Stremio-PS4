// Magnet links: how a torrent stream reaches the players. Stremio add-ons
// give { infoHash, fileIdx, sources: ["tracker:udp://…", "dht:…"] }; the app
// turns that into magnet:?xt=urn:btih:<hash>&so=<fileIdx>&tr=<tracker>…
// (`so` = BEP 53 "select only").
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace torrent {

struct Magnet {
	uint8_t info_hash[20] = {};
	int file_index = -1; // -1: pick the largest video file
	std::vector<std::string> trackers;
	std::string name; // dn=, may be empty
};

bool is_magnet(const std::string &url);
// Accepts 40-char hex and 32-char base32 info-hashes.
bool parse_magnet(const std::string &url, Magnet &out);
std::string make_magnet(const std::string &info_hash_hex, int file_index, const std::vector<std::string> &trackers);

bool parse_info_hash(const std::string &text, uint8_t out[20]);

} // namespace torrent
