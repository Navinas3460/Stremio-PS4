#include "fakes.h"

#include "../src/platform/platform.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <memory>

namespace fake {

std::map<std::string, std::string> files;
double now = 1000;
int64_t unix_time = 1760000000; // 2025-10-09
std::function<http::Response(const http::Request &)> handler;
std::vector<http::Request> requests;

namespace {
struct Pending {
	http::Request request;
	http::Callback on_done, on_worker;
};
std::deque<Pending> g_pending;
} // namespace

void reset() {
	files.clear();
	now = 1000;
	unix_time = 1760000000;
	handler = nullptr;
	requests.clear();
	g_pending.clear();
}

void settle() {
	for (int round = 0; round < 100 && !g_pending.empty(); round++) http::poll();
}

http::Response json(const std::string &body, int status) {
	http::Response r;
	r.status = status;
	r.body = body;
	return r;
}

http::Response api_result(const std::string &result_json) {
	return json("{\"result\":" + result_json + "}");
}

} // namespace fake

// --- platform ------------------------------------------------------------------------

namespace platform {

double now() {
	return fake::now;
}

int64_t unix_time() {
	return fake::unix_time;
}

void log(const char *fmt, ...) {
	if (!getenv("TEST_VERBOSE")) return;
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fputc('\n', stderr);
}

std::string data_dir() {
	return "/fake/";
}

bool read_file(const std::string &path, std::string &out) {
	auto it = fake::files.find(path);
	if (it == fake::files.end()) return false;
	out = it->second;
	return true;
}

bool write_file(const std::string &path, const std::string &data) {
	fake::files[path] = data;
	return true;
}

bool remove_file(const std::string &path) {
	return fake::files.erase(path) > 0;
}

} // namespace platform

// --- http ----------------------------------------------------------------------------

namespace http {

void send(const Request &request, Callback on_done, Callback on_worker) {
	fake::requests.push_back(request);
	fake::g_pending.push_back({ request, std::move(on_done), std::move(on_worker) });
}

void get(const std::string &url, Callback on_done, Callback on_worker) {
	Request r;
	r.url = url;
	send(r, std::move(on_done), std::move(on_worker));
}

void poll() {
	std::deque<fake::Pending> batch;
	batch.swap(fake::g_pending);
	for (fake::Pending &p : batch) {
		Response res;
		if (fake::handler) {
			res = fake::handler(p.request);
		} else {
			res.error = "no handler";
		}
		if (res.status == 0 && res.error.empty()) res.error = "not found: " + p.request.url;
		if (p.on_worker) p.on_worker(res);
		if (p.on_done) p.on_done(res);
	}
}

} // namespace http
