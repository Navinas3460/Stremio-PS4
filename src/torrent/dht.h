// A get_peers-only DHT client (BEP 5): walks the DHT toward the info-hash
// from the public bootstrap nodes and collects the peers nodes return. We
// don't keep a routing table or answer queries; we're a short-lived client,
// and say so with BEP 43's read-only flag so nodes don't add us to theirs.
// Runs inside the engine's poll loop (non-blocking UDP socket).
#pragma once

#include "net.h"

#include <cstdint>
#include <set>
#include <vector>

namespace torrent {

class Dht {
public:
	explicit Dht(const uint8_t info_hash[20]);
	~Dht();

	int fd() const { return fd_; }
	void add_node(const net::Endpoint &e); // bootstrap node, or a peer's DHT port
	// Starts a new lookup (the engine calls it again while peers are scarce).
	void search();
	// Sends queries; call often (every loop turn).
	void tick(double now);
	// Reads replies; new peers are appended to `peers`.
	void on_readable(std::vector<net::Endpoint> &peers);

	int replies() const { return replies_; }
	int peers_found() const { return (int)peers_seen_.size(); } // distinct

private:
	struct Node {
		uint8_t id[20];
		net::Endpoint ep;
	};
	bool closer(const uint8_t *a, const uint8_t *b) const;
	void query(const net::Endpoint &e);

	uint8_t info_hash_[20];
	uint8_t id_[20];
	int fd_ = -1;
	std::vector<net::Endpoint> bootstrap_;
	std::vector<Node> candidates_; // nodes heard of, not asked yet
	std::vector<Node> responded_;  // nodes that answered (seeds the next search)
	std::set<net::Endpoint> queried_, peers_seen_;
	double last_burst_ = 0;
	uint16_t tid_ = 0;
	int replies_ = 0, queries_this_search_ = 0;
	bool searching_ = false;
};

} // namespace torrent
