#include "byte_source.h"

#include "../torrent/magnet.h"
#include "../torrent/torrent_stream.h"
#include "range_reader.h"

bool ByteSource::supports(const std::string &url) {
	return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0 || torrent::is_magnet(url);
}

std::unique_ptr<ByteSource> ByteSource::create(const std::string &url, const std::vector<std::string> &headers) {
	if (torrent::is_magnet(url)) return std::unique_ptr<ByteSource>(new TorrentStream(url));
	return std::unique_ptr<ByteSource>(new RangeReader(url, headers));
}
