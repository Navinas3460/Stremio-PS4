// PS4 HTTP backend: libSceHttp (+ SSL). Must run on a thread with a large
// stack (see http.cpp).
#include "http.h"

#include "../platform/platform.h"
#include "urls.h"

#include <stdint.h>
#include <orbis/Http.h>
#include <orbis/Net.h>
#include <orbis/Ssl.h>
#include <orbis/Sysmodule.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// The OpenOrbis headers only declare these as `void f()`; real signatures:
extern "C" int32_t http_set_auto_redirect(int32_t id, int32_t enable) __asm__("sceHttpSetAutoRedirect");
extern "C" int32_t http_set_recv_timeout(int32_t id, uint32_t usec) __asm__("sceHttpSetRecvTimeOut");

namespace http {

namespace {
int g_http_ctx = -1;
const char *USER_AGENT = "StremioPS4/" APP_VERSION " (PlayStation 4)";

std::string describe(int32_t code) {
	char buf[96];
	switch ((uint32_t)code) {
		case 0x80431075: return "secure connection failed (TLS/certificate)";
		case 0x80431076: return "HTTP worker stack too small";
		case 0x80431022: return "out of HTTP memory";
		default: snprintf(buf, sizeof(buf), "network error 0x%08X", (uint32_t)code); return buf;
	}
}
} // namespace

bool backend_init() {
	sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET);
	sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SSL);
	sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_HTTP);
	sceNetInit();
	const int net_pool = sceNetPoolCreate("stremio_net", 256 * 1024, 0);
	const int ssl = sceSslInit(1024 * 1024);
	g_http_ctx = sceHttpInit(net_pool, ssl, 2 * 1024 * 1024);
	platform::log("http: net pool %d, ssl %d, http ctx %d", net_pool, ssl, g_http_ctx);
	return net_pool >= 0 && ssl >= 0 && g_http_ctx >= 0;
}

Response perform(const Request &request) {
	Response res;
	const bool post = request.method == "POST";
	int tpl = sceHttpCreateTemplate(g_http_ctx, USER_AGENT, ORBIS_HTTP_VERSION_1_1, 1);
	if (tpl < 0) {
		res.error = describe(tpl);
		return res;
	}
	http_set_auto_redirect(tpl, 1); // only the app's own requests: no credential headers
	sceHttpSetConnectTimeOut(tpl, 10 * 1000 * 1000);
	http_set_recv_timeout(tpl, 30 * 1000 * 1000);
	int conn = sceHttpCreateConnectionWithURL(tpl, request.url.c_str(), 1);
	int req = conn >= 0 ? sceHttpCreateRequestWithURL(conn, post ? ORBIS_METHOD_POST : ORBIS_METHOD_GET,
										request.url.c_str(), post ? request.body.size() : 0)
						: conn;
	if (req < 0) {
		res.error = describe(req);
	} else {
		for (const std::string &h : request.headers) {
			const size_t colon = h.find(':');
			if (colon != std::string::npos) {
				std::string name = h.substr(0, colon);
				std::string value = h.substr(colon + 1);
				value.erase(0, value.find_first_not_of(' '));
				sceHttpAddRequestHeader(req, name.c_str(), value.c_str(), 0 /* overwrite */);
			}
		}
		int rc = sceHttpSendRequest(req, post ? request.body.data() : nullptr, post ? request.body.size() : 0);
		int32_t status = 0;
		if (rc >= 0) {
			rc = sceHttpGetStatusCode(req, &status);
		}
		if (rc < 0) {
			res.error = describe(rc);
		} else {
			res.status = status;
			std::vector<char> buf(64 * 1024); // heap: keep big buffers off thread stacks
			for (;;) {
				const int n = sceHttpReadData(req, buf.data(), (uint32_t)buf.size());
				if (n < 0) {
					res.error = describe(n);
					break;
				}
				if (n == 0) {
					break;
				}
				if (res.body.size() + (size_t)n > request.max_body) {
					res.error = "Response too large";
					res.body.clear();
					break;
				}
				res.body.append(buf.data(), (size_t)n);
			}
		}
		sceHttpDeleteRequest(req);
	}
	if (conn >= 0) {
		sceHttpDeleteConnection(conn);
	}
	sceHttpDeleteTemplate(tpl);
	return res;
}

namespace {

uint64_t content_range_total(const char *headers, size_t len) {
	std::string h(headers, len);
	for (char &c : h) c = (char)tolower((unsigned char)c);
	const size_t at = h.find("content-range:");
	if (at == std::string::npos) return 0;
	const size_t slash = h.find('/', at);
	const size_t eol = h.find('\n', at);
	if (slash == std::string::npos || (eol != std::string::npos && slash > eol)) return 0;
	return strtoull(h.c_str() + slash + 1, nullptr, 10);
}

// One template + connection per stream, a request per range.
class SceRange : public RangeConnection {
public:
	SceRange(const std::string &url, const std::vector<std::string> &headers) : url_(url), headers_(headers) {
		tpl_ = sceHttpCreateTemplate(g_http_ctx, USER_AGENT, ORBIS_HTTP_VERSION_1_1, 1);
		if (tpl_ < 0) return;
		// Redirects are followed by hand, to then request the final URL directly
		// (debrid links go through the add-on's resolver first).
		http_set_auto_redirect(tpl_, 0);
		sceHttpSetConnectTimeOut(tpl_, 15 * 1000 * 1000);
		http_set_recv_timeout(tpl_, 30 * 1000 * 1000);
		conn_ = sceHttpCreateConnectionWithURL(tpl_, url.c_str(), 1);
	}
	~SceRange() override {
		if (conn_ >= 0) sceHttpDeleteConnection(conn_);
		if (tpl_ >= 0) sceHttpDeleteTemplate(tpl_);
	}

	int fetch(uint64_t start, uint64_t end, std::vector<uint8_t> &out, uint64_t *total, const std::atomic<bool> &cancel,
			std::string &error) override {
		for (int hops = 0; hops < 8; hops++) {
			std::string location;
			const int rc = fetch_once(start, end, out, total, cancel, error, location);
			if (location.empty()) return rc;
			const std::string next = resolve(url_, location);
			headers_ = urls::redirect_headers(url_, next, headers_);
			url_ = next;
			platform::log("range: redirected to %s", urls::for_log(url_).c_str());
			if (conn_ >= 0) sceHttpDeleteConnection(conn_);
			conn_ = sceHttpCreateConnectionWithURL(tpl_, url_.c_str(), 1);
		}
		error = "Too many redirects";
		return -1;
	}

	std::string url() const override { return url_; }

private:
	// Location relative to the current URL ("/path" or a full URL).
	static std::string resolve(const std::string &base, const std::string &location) {
		if (location.find("://") != std::string::npos) return location;
		const size_t scheme = base.find("://");
		const size_t host_end = base.find('/', scheme == std::string::npos ? 0 : scheme + 3);
		const std::string origin = host_end == std::string::npos ? base : base.substr(0, host_end);
		if (!location.empty() && location[0] == '/') return origin + location;
		const size_t last = base.rfind('/');
		return (last != std::string::npos && last > scheme + 2 ? base.substr(0, last + 1) : origin + "/") + location;
	}

	static std::string header_value(const char *headers, size_t len, const char *name) {
		std::string h = "\n" + std::string(headers, len), lower = h; // "\n" so the first line matches too
		for (char &c : lower) c = (char)tolower((unsigned char)c);
		const size_t at = lower.find(std::string("\n") + name + ":");
		if (at == std::string::npos) return "";
		size_t from = at + strlen(name) + 2;
		while (from < h.size() && h[from] == ' ') from++;
		const size_t eol = h.find_first_of("\r\n", from);
		return h.substr(from, eol == std::string::npos ? std::string::npos : eol - from);
	}

	int fetch_once(uint64_t start, uint64_t end, std::vector<uint8_t> &out, uint64_t *total,
			const std::atomic<bool> &cancel, std::string &error, std::string &location) {
		out.clear();
		if (tpl_ < 0 || conn_ < 0) {
			error = describe(tpl_ < 0 ? tpl_ : conn_);
			return -1;
		}
		const int req = sceHttpCreateRequestWithURL(conn_, ORBIS_METHOD_GET, url_.c_str(), 0);
		if (req < 0) {
			error = describe(req);
			return req;
		}
		for (const std::string &h : headers_) {
			const size_t colon = h.find(':');
			if (colon == std::string::npos) continue;
			std::string value = h.substr(colon + 1);
			value.erase(0, value.find_first_not_of(' '));
			sceHttpAddRequestHeader(req, h.substr(0, colon).c_str(), value.c_str(), 0);
		}
		char range[64];
		snprintf(range, sizeof(range), "bytes=%llu-%llu", (unsigned long long)start, (unsigned long long)end);
		sceHttpAddRequestHeader(req, "Range", range, 0);
		int rc = sceHttpSendRequest(req, nullptr, 0);
		int32_t status = 0;
		if (rc >= 0) rc = sceHttpGetStatusCode(req, &status);
		if (rc < 0) {
			error = describe(rc);
			sceHttpDeleteRequest(req);
			return rc;
		}
		char *headers = nullptr;
		size_t headers_len = 0;
		const bool have_headers = sceHttpGetAllResponseHeaders(req, &headers, &headers_len) >= 0 && headers;
		if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
			if (have_headers) location = header_value(headers, headers_len, "location");
			sceHttpDeleteRequest(req);
			if (location.empty()) {
				error = "Redirect without a location";
				return -1;
			}
			return status;
		}
		if (total) {
			*total = 0;
			if (have_headers) *total = content_range_total(headers, headers_len);
			if (*total == 0 && status == 200) {
				int32_t type = 0;
				size_t length = 0;
				if (sceHttpGetResponseContentLength(req, &type, &length) >= 0) *total = length;
			}
		}
		if (status == 206 || status == 200) {
			const uint64_t want = end - start + 1;
			out.reserve((size_t)want);
			chunk_.resize(64 * 1024);
			while (out.size() < want && !cancel) {
				const int n = sceHttpReadData(req, chunk_.data(), (uint32_t)chunk_.size());
				if (n < 0) {
					error = describe(n);
					rc = n;
					break;
				}
				if (n == 0) break;
				const size_t take = std::min<size_t>((size_t)n, (size_t)(want - out.size()));
				out.insert(out.end(), chunk_.data(), chunk_.data() + take);
			}
			if (cancel) {
				error = "cancelled";
				rc = -1;
			}
		}
		sceHttpDeleteRequest(req);
		return rc < 0 ? rc : status;
	}

	std::string url_;
	std::vector<std::string> headers_;
	int tpl_ = -1, conn_ = -1;
	std::vector<uint8_t> chunk_; // heap, not stack
};

} // namespace

std::unique_ptr<RangeConnection> open_range(const std::string &url, const std::vector<std::string> &headers) {
	return std::unique_ptr<RangeConnection>(new SceRange(url, headers));
}

} // namespace http
