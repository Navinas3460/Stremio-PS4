#include "urls.h"

#include <cctype>

namespace urls {
namespace {

std::string lower(std::string s) {
	for (char &c : s) c = (char)tolower((unsigned char)c);
	return s;
}

// Where the authority ("user@host:port") starts and ends.
void authority(const std::string &url, size_t &from, size_t &to) {
	const size_t scheme = url.find("://");
	from = scheme == std::string::npos ? 0 : scheme + 3;
	to = url.find_first_of("/?#", from);
	if (to == std::string::npos) to = url.size();
}

} // namespace

std::string origin(const std::string &url) {
	size_t from, to;
	authority(url, from, to);
	std::string host = url.substr(from, to - from);
	const size_t at = host.rfind('@');
	if (at != std::string::npos) host.erase(0, at + 1);
	return lower(url.substr(0, from) + host);
}

bool is_credential_header(const std::string &header) {
	const size_t colon = header.find(':');
	if (colon == std::string::npos) return false;
	std::string name = lower(header.substr(0, colon));
	while (!name.empty() && name.back() == ' ') name.pop_back();
	return name == "authorization" || name == "cookie" || name == "proxy-authorization";
}

std::vector<std::string> redirect_headers(const std::string &from, const std::string &to,
		const std::vector<std::string> &headers) {
	if (origin(from) == origin(to)) return headers;
	std::vector<std::string> kept;
	for (const std::string &h : headers) {
		if (!is_credential_header(h)) kept.push_back(h);
	}
	return kept;
}

std::string for_log(const std::string &url) {
	size_t from, to;
	authority(url, from, to);
	size_t end = url.find_first_of("?#", to);
	if (end == std::string::npos) end = url.size();
	const std::string path = url.substr(to, end - to);
	std::string out = origin(url);
	const size_t last = path.rfind('/');
	if (last == std::string::npos || last + 1 == path.size()) return out + (path.size() > 1 ? "/…/" : path);
	return out + (last == 0 ? "/" : "/…/") + path.substr(last + 1);
}

} // namespace urls
