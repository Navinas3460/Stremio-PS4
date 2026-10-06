// Random access to a remote file for the video decoder: HTTP Range requests
// in 1 MiB blocks, downloaded ahead of the read position by several threads
// in parallel (servers such as archive.org limit each connection to a few
// hundred KB/s). Each thread has its own connection and a 1 MiB stack
// (libSceHttp needs it on PS4). A read far from the current window (a seek)
// cancels the downloads that are no longer useful.
#pragma once

#include "../platform/threads.h"
#include "http.h"

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <vector>

class RangeReader {
public:
	RangeReader(const std::string &url, const std::vector<std::string> &headers);
	~RangeReader(); // stops and waits for the download threads

	// Connects and learns the size (blocking). False on failure; see error().
	bool open();
	// Copies up to `len` bytes at `pos` into `buf`, waiting for the download
	// if needed. Returns the byte count, 0 at the end, -1 on error or abort.
	int read(uint64_t pos, uint8_t *buf, int len);
	uint64_t size() const { return size_; }
	// Makes pending and future reads return -1 (e.g. the player is closing).
	void abort();
	std::string error();

private:
	static const int WORKERS = 4;
	struct Worker {
		RangeReader *reader;
		int index;
		std::atomic<bool> cancel{ false };
		uint64_t fetching = UINT64_MAX; // guarded by mutex_
	};

	static void *thread_entry(void *worker);
	void download_loop(Worker &w);
	bool in_window(uint64_t block) const;
	bool taken(uint64_t block) const;
	void fail(const std::string &message);

	std::string url_; // after open(): the final URL (redirects resolved)
	std::vector<std::string> headers_;
	std::atomic<uint64_t> size_{ 0 };

	threads::Mutex mutex_;
	threads::CondVar wake_;    // download threads: new read position, abort
	threads::CondVar arrived_; // readers: a block arrived, open finished, error
	std::map<uint64_t, std::shared_ptr<std::vector<uint8_t>>> blocks_;
	uint64_t want_ = 0;         // block the reader needs (window start)
	bool opened_ = false;
	std::string error_;
	Worker workers_[WORKERS];
	std::atomic<bool> abort_{ false };
	std::atomic<int> running_{ 0 };
	int requests_ = 0;
	uint64_t bytes_ = 0;
	double started_ = 0;
};
