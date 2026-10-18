#include "net.h"

#include "../platform/platform.h"
#include "../platform/threads.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

#if defined(STREMIO_PS4)
#include <orbis/Net.h>
extern "C" int *__error();
#endif

#ifndef SO_NOSIGPIPE
#define SO_NOSIGPIPE 0x0800 // FreeBSD
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace torrent {
namespace net {

std::string Endpoint::str() const {
	const uint8_t *b = (const uint8_t *)&ip;
	char s[32];
	snprintf(s, sizeof(s), "%u.%u.%u.%u:%u", b[0], b[1], b[2], b[3], port);
	return s;
}

bool Endpoint::routable() const {
	const uint8_t first = ((const uint8_t *)&ip)[0];
	return ip != 0 && port != 0 && first < 224 && ip != 0xFFFFFFFFu;
}

Endpoint compact(const char *p) {
	Endpoint e;
	memcpy(&e.ip, p, 4);
	e.port = (uint16_t)((uint8_t)p[4] << 8 | (uint8_t)p[5]);
	return e;
}

namespace {

sockaddr_in to_sockaddr(const Endpoint &e) {
	sockaddr_in a;
	memset(&a, 0, sizeof(a));
#if defined(STREMIO_PS4)
	a.sin_len = sizeof(a);
#endif
	a.sin_family = AF_INET;
	a.sin_port = htons(e.port);
	a.sin_addr.s_addr = e.ip;
	return a;
}

bool make_nonblocking(int fd) {
#if defined(STREMIO_PS4)
	const int one = 1;
	return setsockopt(fd, SOL_SOCKET, 0x1200 /* SCE_NET_SO_NBIO */, &one, sizeof(one)) == 0;
#else
	const int flags = fcntl(fd, F_GETFL, 0);
	return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

int prepare(int fd) {
	if (fd < 0) return -1;
	if (!make_nonblocking(fd)) {
		close(fd);
		return -1;
	}
	const int one = 1;
	setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
	return fd;
}

} // namespace

int last_error() {
#if defined(STREMIO_PS4)
	return *__error();
#else
	return errno;
#endif
}

bool would_block() {
	const int e = last_error();
	return e == EAGAIN || e == EWOULDBLOCK || e == EINPROGRESS;
}

int tcp_socket() {
	const int fd = prepare(socket(AF_INET, SOCK_STREAM, 0));
	if (fd >= 0) {
		const int one = 1;
		setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
		const int rcvbuf = 256 * 1024;
		setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
	}
	return fd;
}

int udp_socket(uint16_t bind_port) {
	const int fd = prepare(socket(AF_INET, SOCK_DGRAM, 0));
	if (fd >= 0 && bind_port) {
		const sockaddr_in a = to_sockaddr(Endpoint{ 0, bind_port });
		if (bind(fd, (const sockaddr *)&a, sizeof(a)) != 0) {
			const sockaddr_in any = to_sockaddr(Endpoint{ 0, 0 }); // port taken: any port will do
			bind(fd, (const sockaddr *)&any, sizeof(any));
		}
	}
	return fd;
}

bool connect_start(int fd, const Endpoint &to) {
	const sockaddr_in a = to_sockaddr(to);
	return connect(fd, (const sockaddr *)&a, sizeof(a)) == 0 || would_block();
}

bool connect_ok(int fd) {
	int err = 0;
	socklen_t len = sizeof(err);
	return getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0;
}

long send_some(int fd, const void *data, size_t len) {
	return (long)send(fd, data, len, MSG_NOSIGNAL);
}

long recv_some(int fd, void *data, size_t len) {
	return (long)recv(fd, data, len, 0);
}

long send_to(int fd, const void *data, size_t len, const Endpoint &to) {
	const sockaddr_in a = to_sockaddr(to);
	return (long)sendto(fd, data, len, 0, (const sockaddr *)&a, sizeof(a));
}

long recv_from(int fd, void *data, size_t len, Endpoint &from) {
	sockaddr_in a;
	socklen_t al = sizeof(a);
	const long n = (long)recvfrom(fd, data, len, 0, (sockaddr *)&a, &al);
	if (n >= 0) {
		from.ip = a.sin_addr.s_addr;
		from.port = ntohs(a.sin_port);
	}
	return n;
}

void close_socket(int fd) {
	if (fd >= 0) close(fd);
}

bool resolve(const std::string &host, uint32_t &ip) {
	unsigned a, b, c, d;
	char extra;
	if (sscanf(host.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra) == 4 && a < 256 && b < 256 && c < 256 && d < 256) {
		const uint8_t bytes[4] = { (uint8_t)a, (uint8_t)b, (uint8_t)c, (uint8_t)d };
		memcpy(&ip, bytes, 4);
		return true;
	}
#if defined(STREMIO_PS4)
	static threads::Mutex pool_mutex;
	static int pool = -1;
	{
		threads::Lock lock(pool_mutex);
		if (pool < 0) pool = sceNetPoolCreate("stremio_dns", 64 * 1024, 0);
	}
	const int rid = sceNetResolverCreate("stremio_dns", pool, 0);
	if (rid < 0) return false;
	OrbisNetInAddr addr;
	addr.s_addr = 0;
	const int rc = sceNetResolverStartNtoa(rid, host.c_str(), &addr, 0, 0, 0);
	sceNetResolverDestroy(rid);
	if (rc < 0 || addr.s_addr == 0) return false;
	ip = addr.s_addr;
	return true;
#else
	addrinfo hints, *res = nullptr;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
	ip = ((const sockaddr_in *)res->ai_addr)->sin_addr.s_addr;
	freeaddrinfo(res);
	return true;
#endif
}

} // namespace net
} // namespace torrent
