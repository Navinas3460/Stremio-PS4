#include "trackers.h"

#include "../net/http.h"
#include "../platform/platform.h"
#include "bencode.h"

#include <poll.h>

#include <cstdlib>
#include <cstring>

namespace torrent {

namespace {

void put32(std::string &s, uint32_t v) {
	for (int i = 3; i >= 0; i--) s += (char)(v >> (i * 8));
}
void put64(std::string &s, uint64_t v) {
	for (int i = 7; i >= 0; i--) s += (char)(v >> (i * 8));
}
uint32_t get32(const uint8_t *p) {
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

uint32_t random32() {
	static uint64_t state = 0;
	if (!state) state = (uint64_t)(platform::now() * 1e6) ^ 0x9E3779B97F4A7C15ull;
	state ^= state << 13;
	state ^= state >> 7;
	state ^= state << 17;
	return (uint32_t)(state >> 16);
}

// Waits up to `ms` for a datagram from anyone.
long recv_wait(int fd, uint8_t *buf, size_t len, int ms) {
	pollfd pf = { fd, POLLIN, 0 };
	if (poll(&pf, 1, ms) != 1) return -1;
	net::Endpoint from;
	return net::recv_from(fd, buf, len, from);
}

bool split_host_port(const std::string &rest, std::string &host, uint16_t &port) {
	const size_t end = rest.find_first_of("/?");
	const std::string hp = rest.substr(0, end);
	const size_t colon = hp.rfind(':');
	if (colon == std::string::npos) return false;
	host = hp.substr(0, colon);
	port = (uint16_t)atoi(hp.c_str() + colon + 1);
	return !host.empty() && port != 0;
}

AnnounceResult announce_udp(const std::string &url, const uint8_t info_hash[20], const uint8_t peer_id[20], uint64_t left) {
	AnnounceResult r;
	std::string host;
	uint16_t port;
	if (!split_host_port(url.substr(6), host, port)) {
		r.error = "bad tracker url";
		return r;
	}
	uint32_t ip;
	if (!net::resolve(host, ip)) {
		r.error = "dns failed";
		return r;
	}
	const net::Endpoint to{ ip, port };
	const int fd = net::udp_socket(0);
	if (fd < 0) {
		r.error = "no socket";
		return r;
	}
	uint8_t buf[2048];
	// Connect: a connection id, retried once (UDP may drop it).
	uint32_t tid = random32();
	std::string req;
	put64(req, 0x41727101980ull);
	put32(req, 0);
	put32(req, tid);
	long n = -1;
	for (int attempt = 0; attempt < 2 && n < 16; attempt++) {
		net::send_to(fd, req.data(), req.size(), to);
		n = recv_wait(fd, buf, sizeof(buf), 3000);
	}
	if (n < 16 || get32(buf) != 0 || get32(buf + 4) != tid) {
		net::close_socket(fd);
		r.error = "no reply";
		return r;
	}
	std::string ann((const char *)buf + 8, 8);
	tid = random32();
	put32(ann, 1); // announce
	put32(ann, tid);
	ann.append((const char *)info_hash, 20);
	ann.append((const char *)peer_id, 20);
	put64(ann, 0);    // downloaded
	put64(ann, left); // left
	put64(ann, 0);    // uploaded
	put32(ann, 0);    // event: none
	put32(ann, 0);    // ip: the sender's
	put32(ann, random32());
	put32(ann, (uint32_t)-1); // num_want: default
	ann += (char)0x1A;        // port 6881 (we don't listen; trackers want one anyway)
	ann += (char)0xE1;
	net::send_to(fd, ann.data(), ann.size(), to);
	n = recv_wait(fd, buf, sizeof(buf), 4000);
	net::close_socket(fd);
	if (n < 20 || get32(buf) != 1 || get32(buf + 4) != tid) {
		r.error = n >= 8 && get32(buf) == 3 ? std::string((const char *)buf + 8, n - 8) : "no announce reply";
		return r;
	}
	r.interval = (int)get32(buf + 8);
	r.leechers = (int)get32(buf + 12);
	r.seeders = (int)get32(buf + 16);
	for (long i = 20; i + 6 <= n; i += 6) {
		const net::Endpoint e = net::compact((const char *)buf + i);
		if (e.routable()) r.peers.push_back(e);
	}
	return r;
}

std::string escape_bytes(const uint8_t *p, size_t n) {
	static const char *hex = "0123456789ABCDEF";
	std::string s;
	for (size_t i = 0; i < n; i++) {
		const uint8_t c = p[i];
		if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_' || c == '.' || c == '~') {
			s += (char)c;
		} else {
			s += '%';
			s += hex[c >> 4];
			s += hex[c & 15];
		}
	}
	return s;
}

AnnounceResult announce_http(const std::string &url, const uint8_t info_hash[20], const uint8_t peer_id[20], uint64_t left) {
	AnnounceResult r;
	http::Request req;
	req.url = url + (url.find('?') == std::string::npos ? "?" : "&") + "info_hash=" + escape_bytes(info_hash, 20) +
			"&peer_id=" + escape_bytes(peer_id, 20) + "&port=6881&uploaded=0&downloaded=0&left=" + std::to_string(left) +
			"&compact=1&numwant=80";
	http::Response res = http::perform(req);
	if (!res.ok()) {
		r.error = res.error.empty() ? "HTTP " + std::to_string(res.status) : res.error;
		return r;
	}
	if (!parse_http_announce(res.body, r) && r.error.empty()) r.error = "bad reply";
	return r;
}

} // namespace

bool parse_http_announce(const std::string &body, AnnounceResult &out) {
	BNode d;
	if (!bdecode(body, d) || d.type != BNode::DICT) return false;
	const std::string failure = d.get_string("failure reason");
	if (!failure.empty()) {
		out.error = failure;
		return false;
	}
	out.interval = (int)d.get_int("interval", 0);
	out.seeders = (int)d.get_int("complete", -1);
	out.leechers = (int)d.get_int("incomplete", -1);
	const BNode *peers = d.get("peers");
	if (!peers) return false;
	if (peers->type == BNode::STRING) {
		for (size_t i = 0; i + 6 <= peers->s.size(); i += 6) {
			const net::Endpoint e = net::compact(peers->s.data() + i);
			if (e.routable()) out.peers.push_back(e);
		}
	} else if (peers->type == BNode::LIST) {
		for (const BNode &p : peers->list) {
			uint32_t ip;
			const std::string host = p.get_string("ip");
			const int64_t port = p.get_int("port", 0);
			if (port > 0 && port < 65536 && !host.empty() && host.find(':') == std::string::npos && net::resolve(host, ip)) {
				const net::Endpoint e{ ip, (uint16_t)port };
				if (e.routable()) out.peers.push_back(e);
			}
		}
	}
	return true;
}

AnnounceResult announce(const std::string &url, const uint8_t info_hash[20], const uint8_t peer_id[20], uint64_t left) {
	if (url.rfind("udp://", 0) == 0) return announce_udp(url, info_hash, peer_id, left);
	if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) return announce_http(url, info_hash, peer_id, left);
	AnnounceResult r;
	r.error = "unsupported tracker";
	return r;
}

const std::vector<std::string> &default_trackers() {
	// The ones that answered from the PS4 in the hardware probe, plus a few
	// long-running public trackers.
	static const std::vector<std::string> list = {
		"udp://tracker.opentrackr.org:1337/announce",
		"udp://open.stealth.si:80/announce",
		"udp://tracker.torrent.eu.org:451/announce",
		"udp://exodus.desync.com:6969/announce",
		"udp://open.demonii.com:1337/announce",
		"udp://explodie.org:6969/announce",
	};
	return list;
}

} // namespace torrent
