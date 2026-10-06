#include "range_reader.h"

#include "../platform/platform.h"

#include <unistd.h>

#include <algorithm>
#include <cstring>

namespace {
const uint64_t BLOCK = 1024 * 1024;
const uint64_t AHEAD = 24;  // blocks downloaded ahead of the read position
const uint64_t BEHIND = 2;  // blocks kept behind it (small backward seeks)
const int RETRIES = 3;
} // namespace

RangeReader::RangeReader(const std::string &url, const std::vector<std::string> &headers)
	: url_(url), headers_(headers) {
	for (int i = 0; i < WORKERS; i++) {
		workers_[i].reader = this;
		workers_[i].index = i;
	}
}

RangeReader::~RangeReader() {
	abort();
	while (running_ > 0) usleep(2000);
}

bool RangeReader::open() {
	started_ = platform::now();
	// Worker 0 connects first (resolving redirects); the others start once
	// the final URL is known, so they don't each go through the redirect.
	running_ = 1;
	if (!threads::start(thread_entry, &workers_[0], 1024 * 1024, "stremio_range0")) {
		running_ = 0;
		fail("Couldn't start the download thread");
		return false;
	}
	{
		threads::Lock lock(mutex_);
		while (!opened_ && error_.empty() && !abort_) arrived_.wait(mutex_);
		if (!opened_) return false;
	}
	for (int i = 1; i < WORKERS; i++) {
		running_++;
		if (!threads::start(thread_entry, &workers_[i], 1024 * 1024, "stremio_range")) running_--;
	}
	return true;
}

void RangeReader::abort() {
	abort_ = true;
	for (Worker &w : workers_) w.cancel = true;
	threads::Lock lock(mutex_);
	wake_.notify_all();
	arrived_.notify_all();
}

std::string RangeReader::error() {
	threads::Lock lock(mutex_);
	return error_;
}

void RangeReader::fail(const std::string &message) {
	threads::Lock lock(mutex_);
	if (error_.empty()) error_ = message;
	platform::log("range: %s", message.c_str());
	arrived_.notify_all();
}

bool RangeReader::in_window(uint64_t block) const {
	return block + BEHIND >= want_ && block < want_ + AHEAD;
}

bool RangeReader::taken(uint64_t block) const {
	for (const Worker &w : workers_) {
		if (w.fetching == block) return true;
	}
	return false;
}

int RangeReader::read(uint64_t pos, uint8_t *buf, int len) {
	if (pos >= size_ || len <= 0) return 0;
	const uint64_t block = pos / BLOCK;
	std::shared_ptr<std::vector<uint8_t>> data;
	{
		threads::Lock lock(mutex_);
		if (want_ != block) {
			want_ = block;
			// Downloads that are no longer useful are dropped (a seek).
			for (Worker &w : workers_) {
				if (w.fetching != UINT64_MAX && !in_window(w.fetching)) w.cancel = true;
			}
			wake_.notify_all();
		}
		double waited_from = 0;
		for (;;) {
			auto it = blocks_.find(block);
			if (it != blocks_.end()) {
				data = it->second;
				break;
			}
			if (abort_ || !error_.empty()) return -1;
			if (waited_from == 0) waited_from = platform::now();
			arrived_.wait(mutex_);
		}
		// Playback had to wait for the network (a stall, unless it was a seek).
		if (waited_from > 0 && platform::now() - waited_from > 0.2) {
			platform::log("range: waited %.1f s for block %llu", platform::now() - waited_from, (unsigned long long)block);
		}
	}
	const uint64_t offset = pos - block * BLOCK;
	if (offset >= data->size()) return -1;
	const int n = (int)std::min<uint64_t>((uint64_t)len, data->size() - offset);
	memcpy(buf, data->data() + offset, (size_t)n);
	return n;
}

void *RangeReader::thread_entry(void *worker) {
	Worker *w = static_cast<Worker *>(worker);
	w->reader->download_loop(*w);
	w->reader->running_--;
	return nullptr;
}

void RangeReader::download_loop(Worker &w) {
	std::string url;
	{
		threads::Lock lock(mutex_);
		url = url_;
	}
	std::unique_ptr<http::RangeConnection> conn = http::open_range(url, headers_);
	std::string err;
	if (w.index == 0) {
		std::vector<uint8_t> probe;
		uint64_t total = 0;
		const int status = conn->fetch(0, 0, probe, &total, w.cancel, err);
		if (status < 0) {
			fail(err.empty() ? "Network error" : err);
			return;
		}
		if (status >= 400) {
			fail("The server answered HTTP " + std::to_string(status));
			return;
		}
		if (total == 0) {
			fail("The server didn't say how big the video is");
			return;
		}
		size_ = total;
		threads::Lock lock(mutex_);
		url_ = conn->url(); // the other workers skip the redirects
		platform::log("range: %s: %llu bytes (HTTP %d), %d connections", url_.c_str(), (unsigned long long)total, status,
				WORKERS);
		opened_ = true;
		arrived_.notify_all();
	}

	const uint64_t blocks_total = (size_ + BLOCK - 1) / BLOCK;
	while (!abort_) {
		uint64_t next = UINT64_MAX;
		{
			threads::Lock lock(mutex_);
			// Forget blocks outside the window.
			for (auto it = blocks_.begin(); it != blocks_.end();) {
				it = in_window(it->first) ? std::next(it) : blocks_.erase(it);
			}
			for (uint64_t b = want_; b < want_ + AHEAD && b < blocks_total; b++) {
				if (!blocks_.count(b) && !taken(b)) {
					next = b;
					break;
				}
			}
			if (next == UINT64_MAX) {
				wake_.wait(mutex_);
				continue;
			}
			w.fetching = next;
			w.cancel = false;
		}
		auto data = std::make_shared<std::vector<uint8_t>>();
		const uint64_t start = next * BLOCK, end = std::min(start + BLOCK, (uint64_t)size_) - 1;
		int got = -1;
		const double t0 = platform::now();
		for (int attempt = 0; attempt < RETRIES && !abort_ && !w.cancel; attempt++) {
			got = conn->fetch(start, end, *data, nullptr, w.cancel, err);
			if (got == 206 || (got == 200 && start == 0)) break;
			if (got == 200 && attempt == RETRIES - 1) { // range ignored every time: can't seek in this file
				fail("This server doesn't support seeking in the video");
				break;
			}
			// (A CDN may answer the first request with the whole file while it
			// fills its cache, then ranges: retry.)
			if (!w.cancel && !abort_) {
				platform::log("range: block %llu failed (%d %s), retrying", (unsigned long long)next, got, err.c_str());
				usleep(300 * 1000);
			}
		}
		threads::Lock lock(mutex_);
		w.fetching = UINT64_MAX;
		if (abort_ || !error_.empty()) break;
		if (w.cancel) continue; // the reader moved elsewhere
		if (data->size() != end - start + 1) {
			error_ = err.empty() ? "The video download stopped" : err;
			platform::log("range: %s", error_.c_str());
			arrived_.notify_all();
			break;
		}
		requests_++;
		bytes_ += data->size();
		if (requests_ % 32 == 0) {
			platform::log("range: %llu MB in %.0f s (%.1f MB/s average), last block %.2f s",
					(unsigned long long)(bytes_ >> 20), platform::now() - started_,
					bytes_ / 1048576.0 / std::max(0.001, platform::now() - started_), platform::now() - t0);
		}
		blocks_[next] = data;
		arrived_.notify_all();
		wake_.notify_all(); // another worker may be waiting for work
	}
	if (w.index == 0) platform::log("range: done after %d requests, %llu bytes", requests_, (unsigned long long)bytes_);
}
