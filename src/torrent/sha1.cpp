#include "sha1.h"

#include <algorithm>
#include <cstring>

namespace torrent {

static inline uint32_t rol(uint32_t v, int n) {
	return (v << n) | (v >> (32 - n));
}

void Sha1::block(const uint8_t *p) {
	uint32_t w[80];
	for (int i = 0; i < 16; i++) {
		w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
	}
	for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
	uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4];
	for (int i = 0; i < 80; i++) {
		uint32_t f, k;
		if (i < 20) {
			f = (b & c) | (~b & d);
			k = 0x5A827999;
		} else if (i < 40) {
			f = b ^ c ^ d;
			k = 0x6ED9EBA1;
		} else if (i < 60) {
			f = (b & c) | (b & d) | (c & d);
			k = 0x8F1BBCDC;
		} else {
			f = b ^ c ^ d;
			k = 0xCA62C1D6;
		}
		const uint32_t t = rol(a, 5) + f + e + k + w[i];
		e = d;
		d = c;
		c = rol(b, 30);
		b = a;
		a = t;
	}
	h_[0] += a;
	h_[1] += b;
	h_[2] += c;
	h_[3] += d;
	h_[4] += e;
}

void Sha1::update(const void *data, size_t n) {
	const uint8_t *p = (const uint8_t *)data;
	const size_t fill = len_ % 64;
	len_ += n;
	if (fill) {
		const size_t take = std::min(n, 64 - fill);
		memcpy(buf_ + fill, p, take);
		p += take;
		n -= take;
		if (fill + take < 64) return;
		block(buf_);
	}
	for (; n >= 64; p += 64, n -= 64) block(p);
	memcpy(buf_, p, n);
}

void Sha1::final(uint8_t out[20]) {
	const uint64_t bits = len_ * 8;
	const uint8_t pad = 0x80, zero = 0;
	update(&pad, 1);
	while (len_ % 64 != 56) update(&zero, 1);
	uint8_t lb[8];
	for (int i = 0; i < 8; i++) lb[i] = (uint8_t)(bits >> (56 - 8 * i));
	update(lb, 8);
	for (int i = 0; i < 5; i++) {
		for (int j = 0; j < 4; j++) out[i * 4 + j] = (uint8_t)(h_[i] >> (24 - 8 * j));
	}
}

void sha1(const void *data, size_t len, uint8_t out[20]) {
	Sha1 s;
	s.update(data, len);
	s.final(out);
}

std::string to_hex(const uint8_t *data, size_t len) {
	static const char *digits = "0123456789abcdef";
	std::string s;
	s.reserve(len * 2);
	for (size_t i = 0; i < len; i++) {
		s += digits[data[i] >> 4];
		s += digits[data[i] & 15];
	}
	return s;
}

} // namespace torrent
