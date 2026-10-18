// Streams a magnet link's file through TorrentStream the way the player
// reads it, and reports timings. For developing the engine on desktop.
//
//   build/torrent_fetch MAGNET [MiB to read, default 32] [seek fraction, default 0.5] [pace MB/s]
//
// Reads sequentially from the start (time to info, first byte, MB/s), then
// jumps to the seek fraction of the file and reads 4 MiB there (seek latency).
// With a pace, the sequential part reads no faster than that, like a player
// (the engine sizes its readahead from the read rate).
#include "../src/net/http.h"
#include "../src/platform/platform.h"
#include "../src/torrent/torrent_stream.h"

#include <time.h>
#include <unistd.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace platform {

double now() {
	timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

void log(const char *fmt, ...) {
	static const double start = now();
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "[%7.2f] ", now() - start);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

} // namespace platform

static double pace = 0; // MB/s, 0 = as fast as possible

static bool read_range(TorrentStream &s, uint64_t pos, uint64_t len, double *first_byte) {
	std::vector<uint8_t> buf(64 * 1024);
	const double t0 = platform::now();
	double last_print = t0;
	uint64_t done = 0;
	while (done < len) {
		const int n = s.read(pos + done, buf.data(), (int)std::min<uint64_t>(buf.size(), len - done));
		if (n <= 0) return n == 0;
		if (done == 0 && first_byte) *first_byte = platform::now() - t0;
		done += (uint64_t)n;
		if (pace > 0) {
			const double ahead = done / (pace * 1e6) - (platform::now() - t0);
			if (ahead > 0) usleep((useconds_t)(ahead * 1e6));
		}
		if (platform::now() - last_print > 2) {
			last_print = platform::now();
			printf("  %6.1f MiB  %s\n", done / 1048576.0, s.status().c_str());
		}
	}
	return true;
}

int main(int argc, char **argv) {
	if (argc < 2) {
		fprintf(stderr, "usage: %s MAGNET [MiB] [seek fraction]\n", argv[0]);
		return 2;
	}
	const uint64_t mib = argc > 2 ? strtoull(argv[2], nullptr, 10) : 32;
	const double seek = argc > 3 ? atof(argv[3]) : 0.5;
	if (argc > 4) pace = atof(argv[4]);
	http::init(2);

	const double t0 = platform::now();
	TorrentStream s(argv[1]);
	if (!s.open()) {
		printf("open failed: %s\n", s.error().c_str());
		return 1;
	}
	const double t_info = platform::now() - t0;
	printf("info after %.1f s, file %.1f MiB\n", t_info, s.size() / 1048576.0);

	const uint64_t len = std::min<uint64_t>(s.size(), mib << 20);
	double first = -1;
	const double t1 = platform::now();
	if (!read_range(s, 0, len, &first)) {
		printf("read failed: %s\n", s.error().c_str());
		return 1;
	}
	const double t_seq = platform::now() - t1;
	printf("sequential: %.1f MiB in %.1f s = %.2f MB/s, first byte %.2f s\n", len / 1048576.0, t_seq, len / t_seq / 1e6, first);

	if (seek > 0 && seek < 1 && s.size() > (8u << 20)) {
		const uint64_t at = (uint64_t)(s.size() * seek);
		const uint64_t n = std::min<uint64_t>(4u << 20, s.size() - at);
		pace = 0;
		double seek_first = -1;
		const double t2 = platform::now();
		if (!read_range(s, at, n, &seek_first)) {
			printf("seek read failed: %s\n", s.error().c_str());
			return 1;
		}
		printf("seek to %.0f%%: first byte %.2f s, 4 MiB in %.1f s\n", seek * 100, seek_first, platform::now() - t2);
	}
	printf("total %.1f s\n", platform::now() - t0);
	fflush(stdout);
	_exit(0); // the HTTP worker threads never end

}
