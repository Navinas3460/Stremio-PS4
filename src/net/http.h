// Asynchronous HTTP: requests run on worker threads; completion callbacks run
// on the main thread from poll(). Redirects are followed. TLS certificates are
// verified (the Stremio auth key travels over these connections).
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace http {

struct Request {
	std::string method = "GET";
	std::string url;
	std::vector<std::string> headers; // "Name: value"
	std::string body;
};

struct Response {
	int status = 0;
	std::string body;
	std::string error; // transport error; empty on success
	std::shared_ptr<void> payload; // set by an on_worker step (e.g. decoded image)
	bool ok() const { return error.empty() && status >= 200 && status < 300; }
};

using Callback = std::function<void(Response &)>;

// Starts `workers` worker threads (and the platform network stack).
bool init(int workers);

// Queues a request. `on_worker` (optional) runs on the worker thread after the
// download, e.g. to decode an image; `on_done` runs later on the main thread.
void send(const Request &request, Callback on_done, Callback on_worker = nullptr);

// Convenience for JSON APIs.
void post_json(const std::string &url, const std::string &json_body, Callback on_done);
void get(const std::string &url, Callback on_done, Callback on_worker = nullptr);

// Runs finished callbacks. Call once per frame on the main thread.
void poll();

// Platform backend: performs one request synchronously (worker thread).
Response perform(const Request &request);
bool backend_init();

// Byte ranges of one URL over a kept-alive connection, for streaming video
// (see range_reader.h). Synchronous; on PS4 use it from a thread with a large
// stack, the same thread for its whole life.
class RangeConnection {
public:
	virtual ~RangeConnection() = default;
	// Fetches bytes [start, end] into `out`. Returns the HTTP status (206, or
	// 200 when the server ignored the range and sent the file from 0), or < 0
	// on a transport error, described in `error`. `total` (optional) receives
	// the full size (Content-Range, else Content-Length). Gives up early once
	// `cancel` is set.
	virtual int fetch(uint64_t start, uint64_t end, std::vector<uint8_t> &out, uint64_t *total,
			const std::atomic<bool> &cancel, std::string &error) = 0;
	// The URL requested now: the original one until a fetch followed redirects.
	virtual std::string url() const = 0;
};
std::unique_ptr<RangeConnection> open_range(const std::string &url, const std::vector<std::string> &headers);

#if defined(STREMIO_PS4)
int ps4_context(); // the libSceHttp context, for the video streamer
#endif

} // namespace http
