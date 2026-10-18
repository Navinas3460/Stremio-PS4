// Sockets for the torrent engine: BSD sockets from libkernel on PS4, POSIX on
// desktop. What the console needs (measured by the hardware probe):
//   - fcntl(O_NONBLOCK) does nothing (the static libc's fcntl is a Linux
//     wrapper); setsockopt(SO_NBIO) is the way to get non-blocking sockets.
//   - getaddrinfo fails; DNS goes through sceNetResolver.
//   - errno lives behind libkernel's __error().
//   - 256 descriptors per process.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace torrent {
namespace net {

struct Endpoint {
	uint32_t ip = 0;   // network byte order
	uint16_t port = 0; // host byte order
	bool operator<(const Endpoint &o) const { return ip != o.ip ? ip < o.ip : port < o.port; }
	bool operator==(const Endpoint &o) const { return ip == o.ip && port == o.port; }
	std::string str() const;
	bool routable() const; // not 0.0.0.0, multicast or broadcast; port != 0
};

// From 6 bytes: 4 address + 2 port, both big-endian (compact peer format).
Endpoint compact(const char *p);

// Non-blocking TCP / UDP sockets, -1 on failure.
int tcp_socket();
int udp_socket(uint16_t bind_port); // 0 = any port

// Starts a TCP connect. True if it is under way (or done).
bool connect_start(int fd, const Endpoint &to);
// After poll() reported the socket writable: did the connect succeed?
bool connect_ok(int fd);

// Return the byte count, 0 when the peer closed (recv), or -1. would_block()
// tells a full/empty buffer apart from an error.
long send_some(int fd, const void *data, size_t len);
long recv_some(int fd, void *data, size_t len);
long send_to(int fd, const void *data, size_t len, const Endpoint &to);
long recv_from(int fd, void *data, size_t len, Endpoint &from);
bool would_block();
int last_error();
void close_socket(int fd);

// Blocking DNS lookup (A record). Dotted quads are parsed directly.
bool resolve(const std::string &host, uint32_t &ip);

} // namespace net
} // namespace torrent
