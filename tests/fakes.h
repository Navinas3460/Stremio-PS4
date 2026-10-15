// Fake platform and HTTP layers, so the Stremio facade runs without a
// window, a network or the PS4: files live in memory, the clock is set by the
// test, and HTTP requests are answered by a test-provided handler.
#pragma once

#include "../src/net/http.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace fake {

extern std::map<std::string, std::string> files; // path -> contents
extern double now;                               // platform::now()
extern int64_t unix_time;                        // platform::unix_time()

// Answers a request (method, URL, body). Unset: every request fails.
extern std::function<http::Response(const http::Request &)> handler;
extern std::vector<http::Request> requests; // every request sent, in order

void reset();
// Delivers the answers to everything sent so far (http::poll), repeatedly
// until no new requests appear.
void settle();

// Helpers for handlers.
http::Response json(const std::string &body, int status = 200);
http::Response api_result(const std::string &result_json); // {"result": ...}

} // namespace fake
