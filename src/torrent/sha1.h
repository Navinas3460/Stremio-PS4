// SHA-1, for piece and metadata checks (the PS4 manages ~60 MB/s per core).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace torrent {

class Sha1 {
public:
	void update(const void *data, size_t len);
	void final(uint8_t out[20]);

private:
	void block(const uint8_t *p);
	uint32_t h_[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
	uint8_t buf_[64];
	uint64_t len_ = 0;
};

void sha1(const void *data, size_t len, uint8_t out[20]);
std::string to_hex(const uint8_t *data, size_t len);

} // namespace torrent
