// Tracker announces (UDP, BEP 15; HTTP(S), BEP 3 + compact BEP 23). They
// block for a few seconds, so the engine runs them on a thread of its own.
#pragma once

#include "net.h"

#include <cstdint>
#include <string>
#include <vector>

namespace torrent {

struct AnnounceResult {
	std::vector<net::Endpoint> peers;
	int interval = 0; // seconds until the tracker wants to hear from us again
	int seeders = -1, leechers = -1;
	std::string error;
};

// `url`: udp://host:port[/…], http://… or https://…
AnnounceResult announce(const std::string &url, const uint8_t info_hash[20], const uint8_t peer_id[20], uint64_t left);

// Peers from an HTTP tracker reply body (compact string or a list of dicts).
bool parse_http_announce(const std::string &body, AnnounceResult &out);

// Trackers tried for every torrent, on top of the ones in the magnet link.
const std::vector<std::string> &default_trackers();

} // namespace torrent
