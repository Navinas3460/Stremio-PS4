// URL helpers for the HTTP backends: which headers may follow a redirect, and
// how URLs appear in the log. Platform-independent (unit tested).
#pragma once

#include <string>
#include <vector>

namespace urls {

// "scheme://host[:port]", lowercased, without any "user:pass@".
std::string origin(const std::string &url);

// Authorization, Cookie and Proxy-Authorization ("Name: value"). Like browsers
// and curl, the app drops these when a redirect leaves the original origin.
bool is_credential_header(const std::string &header);

// `headers` for a request to `to` that was redirected from `from`: all of them
// on the same origin, else all but the credential headers.
std::vector<std::string> redirect_headers(const std::string &from, const std::string &to,
		const std::vector<std::string> &headers);

// The URL as it may appear in the log: origin and last path segment only.
// Debrid links and add-on URLs carry account keys in the path or the query.
std::string for_log(const std::string &url);

} // namespace urls
