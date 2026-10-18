// Streams one file of a torrent to the video player, on the console itself
// (no streaming server). open() finds peers (trackers, DHT, PEX), fetches the
// torrent's info from them (ut_metadata) and picks the file; read() then
// waits for the pieces under the read position, which the engine downloads
// first, followed by the next ~40 MiB. Verified pieces live in a memory cache
// (no disk: random writes to the PS4's hard drive are slow and it's download-
// only, we never seed).
#pragma once

#include "../net/byte_source.h"

#include <memory>

namespace torrent {
class Engine;
}

class TorrentStream : public ByteSource {
public:
	explicit TorrentStream(const std::string &magnet);
	~TorrentStream() override; // stops the engine

	bool open() override;
	int read(uint64_t pos, uint8_t *buf, int len) override;
	uint64_t size() const override;
	void abort() override;
	std::string error() override;
	std::string status() override;

private:
	std::shared_ptr<torrent::Engine> engine_; // shared with the engine's threads
};
