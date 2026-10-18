#include "dht.h"

#include "bencode.h"
#include "sha1.h"

#include "../platform/platform.h"

#include <algorithm>
#include <cstring>

namespace torrent {

static const uint16_t DHT_PORT = 6881;
static const int PER_BURST = 8;        // queries per 250 ms
static const int MAX_PER_SEARCH = 300; // then wait for search() again

Dht::Dht(const uint8_t info_hash[20]) {
	memcpy(info_hash_, info_hash, 20);
	// A random node id (not stored: we're a new node every time).
	uint8_t seed[32];
	const double t = platform::now();
	memcpy(seed, &t, sizeof(t));
	memcpy(seed + 8, info_hash, 20);
	sha1(seed, sizeof(seed), id_);
	fd_ = net::udp_socket(DHT_PORT);
}

Dht::~Dht() {
	net::close_socket(fd_);
}

bool Dht::closer(const uint8_t *a, const uint8_t *b) const {
	for (int i = 0; i < 20; i++) {
		const uint8_t da = a[i] ^ info_hash_[i], db = b[i] ^ info_hash_[i];
		if (da != db) return da < db;
	}
	return false;
}

void Dht::add_node(const net::Endpoint &e) {
	if (!e.routable()) return;
	if (std::find(bootstrap_.begin(), bootstrap_.end(), e) == bootstrap_.end()) bootstrap_.push_back(e);
	if (searching_) query(e);
}

void Dht::search() {
	queried_.clear();
	queries_this_search_ = 0;
	searching_ = true;
	// Start from the closest nodes that answered last time, and the bootstrap nodes.
	for (const Node &n : responded_) candidates_.push_back(n);
	responded_.clear();
	for (const net::Endpoint &e : bootstrap_) query(e);
}

void Dht::query(const net::Endpoint &e) {
	if (fd_ < 0 || queries_this_search_ >= MAX_PER_SEARCH || !queried_.insert(e).second) return;
	tid_++;
	const std::string tid{ (char)(tid_ >> 8), (char)(tid_ & 255) };
	const std::string q = "d1:ad2:id" + bstr(std::string((const char *)id_, 20)) + "9:info_hash" +
			bstr(std::string((const char *)info_hash_, 20)) + "e1:q9:get_peers2:roi1e1:t" + bstr(tid) + "1:y1:qe"; // ro: read-only node (BEP 43)
	if (net::send_to(fd_, q.data(), q.size(), e) > 0) queries_this_search_++;
}

void Dht::tick(double now) {
	if (fd_ < 0 || now - last_burst_ < 0.25 || candidates_.empty()) return;
	last_burst_ = now;
	std::sort(candidates_.begin(), candidates_.end(), [this](const Node &a, const Node &b) { return closer(a.id, b.id); });
	int sent = 0;
	size_t i = 0;
	for (; i < candidates_.size() && sent < PER_BURST; i++) {
		if (queried_.count(candidates_[i].ep)) continue;
		query(candidates_[i].ep);
		sent++;
	}
	candidates_.erase(candidates_.begin(), candidates_.begin() + i);
	if (candidates_.size() > 500) candidates_.resize(500); // only the closest matter
}

void Dht::on_readable(std::vector<net::Endpoint> &peers) {
	uint8_t buf[2048];
	net::Endpoint from;
	long n;
	while ((n = net::recv_from(fd_, buf, sizeof(buf), from)) > 0) {
		BNode msg;
		const uint8_t *p = buf;
		if (!bdecode(p, buf + n, msg) || msg.type != BNode::DICT) continue;
		const BNode *r = msg.get("r");
		if (!r || r->type != BNode::DICT) continue;
		replies_++;
		const std::string id = r->get_string("id");
		if (id.size() == 20) {
			Node node;
			memcpy(node.id, id.data(), 20);
			node.ep = from;
			responded_.push_back(node);
			if (responded_.size() > 32) {
				std::sort(responded_.begin(), responded_.end(), [this](const Node &a, const Node &b) { return closer(a.id, b.id); });
				responded_.resize(16);
			}
		}
		const std::string nodes = r->get_string("nodes");
		for (size_t i = 0; i + 26 <= nodes.size(); i += 26) {
			Node node;
			memcpy(node.id, nodes.data() + i, 20);
			node.ep = net::compact(nodes.data() + i + 20);
			if (node.ep.routable() && !queried_.count(node.ep)) candidates_.push_back(node);
		}
		const BNode *values = r->get("values");
		if (values && values->type == BNode::LIST) {
			for (const BNode &v : values->list) {
				if (v.type != BNode::STRING || v.s.size() != 6) continue;
				const net::Endpoint e = net::compact(v.s.data());
				if (e.routable()) {
					peers.push_back(e);
					peers_seen_.insert(e);
				}
			}
		}
	}
}

} // namespace torrent
