// Random-access input for the video players: a file over HTTP (RangeReader)
// or a file inside a torrent (TorrentStream, src/torrent).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class ByteSource {
public:
	virtual ~ByteSource() = default;

	// Connects and learns the size (blocking). False on failure; see error().
	virtual bool open() = 0;
	// Copies up to `len` bytes at `pos` into `buf`, waiting for the download
	// if needed. Returns the byte count (possibly fewer than asked), 0 at the
	// end, -1 on error or abort.
	virtual int read(uint64_t pos, uint8_t *buf, int len) = 0;
	virtual uint64_t size() const = 0;
	// Makes pending and future reads return -1 (e.g. the player is closing).
	virtual void abort() = 0;
	virtual std::string error() = 0;
	// A progress line for the player while it waits (torrent peers and
	// speed); empty when there's nothing to say. Any thread.
	virtual std::string status() { return std::string(); }

	// http(s):// URLs and magnet: links.
	static bool supports(const std::string &url);
	static std::unique_ptr<ByteSource> create(const std::string &url, const std::vector<std::string> &headers);
};
