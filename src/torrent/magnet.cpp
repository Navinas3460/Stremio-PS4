#include "magnet.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace torrent {

namespace {

std::string url_decode(const std::string &s) {
	std::string out;
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
			out += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
			i += 2;
		} else if (s[i] == '+') {
			out += ' ';
		} else {
			out += s[i];
		}
	}
	return out;
}

std::string url_encode(const std::string &s) {
	static const char *hex = "0123456789ABCDEF";
	std::string out;
	for (unsigned char c : s) {
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			out += (char)c;
		} else {
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 15];
		}
	}
	return out;
}

} // namespace

bool parse_info_hash(const std::string &text, uint8_t out[20]) {
	if (text.size() == 40) {
		for (int i = 0; i < 20; i++) {
			const char h[3] = { text[i * 2], text[i * 2 + 1], 0 };
			if (!isxdigit((unsigned char)h[0]) || !isxdigit((unsigned char)h[1])) return false;
			out[i] = (uint8_t)strtol(h, nullptr, 16);
		}
		return true;
	}
	if (text.size() == 32) { // base32
		uint64_t acc = 0;
		int bits = 0, n = 0;
		for (char c : text) {
			c = (char)toupper((unsigned char)c);
			int v;
			if (c >= 'A' && c <= 'Z') v = c - 'A';
			else if (c >= '2' && c <= '7') v = c - '2' + 26;
			else return false;
			acc = acc << 5 | (uint64_t)v;
			bits += 5;
			if (bits >= 8) {
				bits -= 8;
				out[n++] = (uint8_t)(acc >> bits);
			}
		}
		return n == 20;
	}
	return false;
}

bool is_magnet(const std::string &url) {
	return url.rfind("magnet:?", 0) == 0;
}

bool parse_magnet(const std::string &url, Magnet &out) {
	if (!is_magnet(url)) return false;
	bool have_hash = false;
	size_t pos = 8;
	while (pos < url.size()) {
		size_t amp = url.find('&', pos);
		if (amp == std::string::npos) amp = url.size();
		const std::string param = url.substr(pos, amp - pos);
		pos = amp + 1;
		const size_t eq = param.find('=');
		if (eq == std::string::npos) continue;
		const std::string key = param.substr(0, eq);
		const std::string value = url_decode(param.substr(eq + 1));
		if (key == "xt" && value.rfind("urn:btih:", 0) == 0) {
			have_hash = parse_info_hash(value.substr(9), out.info_hash);
		} else if (key == "tr" && !value.empty()) {
			out.trackers.push_back(value);
		} else if (key == "so") {
			out.file_index = atoi(value.c_str()); // a list or range: the first file
		} else if (key == "dn") {
			out.name = value;
		}
	}
	return have_hash;
}

std::string make_magnet(const std::string &info_hash_hex, int file_index, const std::vector<std::string> &trackers) {
	std::string url = "magnet:?xt=urn:btih:" + info_hash_hex;
	if (file_index >= 0) url += "&so=" + std::to_string(file_index);
	for (const std::string &t : trackers) url += "&tr=" + url_encode(t);
	return url;
}

} // namespace torrent
