// Desktop HTTP backend: libcurl.
#include "http.h"

#include "../platform/platform.h"
#include "urls.h"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace http {

namespace {
struct Body {
	std::string *out;
	size_t max;
	bool too_large;
};

size_t write_body(char *data, size_t size, size_t n, void *user) {
	Body &b = *static_cast<Body *>(user);
	if (b.out->size() + size * n > b.max) {
		b.too_large = true;
		return 0; // aborts the transfer
	}
	b.out->append(data, size * n);
	return size * n;
}
} // namespace

bool backend_init() {
	return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
}

Response perform(const Request &request) {
	Response res;
	CURL *curl = curl_easy_init();
	if (!curl) {
		res.error = "curl init failed";
		return res;
	}
	curl_slist *headers = nullptr;
	for (const std::string &h : request.headers) {
		headers = curl_slist_append(headers, h.c_str());
	}
	curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 8L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "StremioPS4/" APP_VERSION);
	curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body);
	Body body{ &res.body, request.max_body, false };
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
	curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE, (curl_off_t)request.max_body); // refuses a large Content-Length early
	// curl itself drops Authorization and Cookie on a redirect to another host.
	if (request.method == "POST") {
		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)request.body.size());
	}
	const CURLcode rc = curl_easy_perform(curl);
	if (body.too_large || rc == CURLE_FILESIZE_EXCEEDED) {
		res.error = "Response too large";
		res.body.clear();
	} else if (rc != CURLE_OK) {
		res.error = curl_easy_strerror(rc);
	} else {
		long status = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
		res.status = (int)status;
	}
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	return res;
}

namespace {

class CurlRange : public RangeConnection {
public:
	CurlRange(const std::string &url, const std::vector<std::string> &headers) : url_(url) {
		curl_ = curl_easy_init();
		set_headers(headers);
	}
	~CurlRange() override {
		curl_slist_free_all(headers_);
		if (curl_) curl_easy_cleanup(curl_);
	}

	int fetch(uint64_t start, uint64_t end, std::vector<uint8_t> &out, uint64_t *total, const std::atomic<bool> &cancel,
			std::string &error) override {
		out.clear();
		if (!curl_) {
			error = "curl init failed";
			return -1;
		}
		Transfer t{ &out, &cancel, end - start + 1, std::string() };
		char range[64];
		snprintf(range, sizeof(range), "%llu-%llu", (unsigned long long)start, (unsigned long long)end);
		curl_easy_reset(curl_); // keeps the connection
		curl_easy_setopt(curl_, CURLOPT_URL, url_.c_str());
		curl_easy_setopt(curl_, CURLOPT_RANGE, range);
		curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(curl_, CURLOPT_MAXREDIRS, 8L);
		curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, 15L);
		curl_easy_setopt(curl_, CURLOPT_LOW_SPEED_LIMIT, 1L);
		curl_easy_setopt(curl_, CURLOPT_LOW_SPEED_TIME, 30L);
		curl_easy_setopt(curl_, CURLOPT_NOSIGNAL, 1L);
		curl_easy_setopt(curl_, CURLOPT_USERAGENT, "StremioPS4/" APP_VERSION);
		curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers_); // curl drops Authorization/Cookie across hosts
		curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, on_body);
		curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &t);
		curl_easy_setopt(curl_, CURLOPT_HEADERFUNCTION, on_header);
		curl_easy_setopt(curl_, CURLOPT_HEADERDATA, &t);
		const CURLcode rc = curl_easy_perform(curl_);
		long status = 0;
		curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &status);
		// A write "error" is us stopping once we have the range.
		if (rc != CURLE_OK && !(rc == CURLE_WRITE_ERROR && out.size() >= t.want)) {
			error = curl_easy_strerror(rc);
			if (cancel) error = "cancelled";
			return -1;
		}
		if (total) {
			*total = parse_total(t.headers, status);
		}
		// Request the redirect target directly from now on (debrid links go
		// through the add-on's resolver first).
		char *effective = nullptr;
		if (curl_easy_getinfo(curl_, CURLINFO_EFFECTIVE_URL, &effective) == CURLE_OK && effective && url_ != effective) {
			set_headers(urls::redirect_headers(url_, effective, header_list_));
			url_ = effective;
			platform::log("range: redirected to %s", urls::for_log(url_).c_str());
		}
		return (int)status;
	}

	std::string url() const override { return url_; }

private:
	void set_headers(const std::vector<std::string> &headers) {
		header_list_ = headers;
		curl_slist_free_all(headers_);
		headers_ = nullptr;
		for (const std::string &h : headers) headers_ = curl_slist_append(headers_, h.c_str());
	}

	struct Transfer {
		std::vector<uint8_t> *out;
		const std::atomic<bool> *cancel;
		uint64_t want;
		std::string headers;
	};

	static size_t on_body(char *data, size_t size, size_t n, void *user) {
		Transfer &t = *static_cast<Transfer *>(user);
		if (*t.cancel) return 0;
		const size_t len = size * n;
		const size_t take = (size_t)std::min<uint64_t>(len, t.want - std::min<uint64_t>(t.want, t.out->size()));
		t.out->insert(t.out->end(), data, data + take);
		return t.out->size() >= t.want ? 0 : len; // stop once the range is complete
	}

	static size_t on_header(char *data, size_t size, size_t n, void *user) {
		Transfer &t = *static_cast<Transfer *>(user);
		std::string line(data, size * n);
		if (line.rfind("HTTP/", 0) == 0) t.headers.clear(); // a redirect's headers don't count
		t.headers += line;
		return size * n;
	}

	static uint64_t parse_total(std::string h, long status) {
		for (char &c : h) c = (char)tolower((unsigned char)c);
		size_t at = h.find("content-range:");
		if (at != std::string::npos) {
			const size_t slash = h.find('/', at);
			if (slash != std::string::npos) return strtoull(h.c_str() + slash + 1, nullptr, 10);
		}
		at = h.find("content-length:");
		if (status == 200 && at != std::string::npos) return strtoull(h.c_str() + at + 15, nullptr, 10);
		return 0;
	}

	std::string url_;
	CURL *curl_ = nullptr;
	std::vector<std::string> header_list_;
	curl_slist *headers_ = nullptr;
};

} // namespace

std::unique_ptr<RangeConnection> open_range(const std::string &url, const std::vector<std::string> &headers) {
	return std::unique_ptr<RangeConnection>(new CurlRange(url, headers));
}

} // namespace http
