#include "addon_config.h"

#include "../../third_party/qrcodegen/qrcodegen.h"

namespace addon_config {

std::string configure_url(const std::string &url) {
	const bool https = url.rfind("https://", 0) == 0;
	const bool http = url.rfind("http://", 0) == 0;
	if (!https && !http) return "";

	for (unsigned char ch : url) {
		if (ch <= 0x20 || ch == 0x7F || ch == '\\') return "";
	}
	if (url.find_first_of("?#") != std::string::npos) return "";

	const size_t authority_start = https ? 8 : 7;
	const size_t path_start = url.find('/', authority_start);
	if (path_start == std::string::npos || path_start == authority_start) {
		return "";
	}
	const std::string authority =
		url.substr(authority_start, path_start - authority_start);
	if (authority.find('@') != std::string::npos) return "";

	const std::string suffix = "/manifest.json";
	if (url.size() < suffix.size()
			|| url.compare(url.size() - suffix.size(),
				suffix.size(), suffix) != 0) {
		return "";
	}
	return url.substr(0, url.size() - suffix.size()) + "/configure";
}

std::vector<uint8_t> make_qr(const std::string &text) {
	if (text.empty() || text.find('\0') != std::string::npos) return {};
	std::vector<uint8_t> scratch(qrcodegen_BUFFER_LEN_MAX);
	std::vector<uint8_t> code(qrcodegen_BUFFER_LEN_MAX);
	const bool ok = qrcodegen_encodeText(
		text.c_str(), scratch.data(), code.data(),
		qrcodegen_Ecc_MEDIUM,
		qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
		qrcodegen_Mask_AUTO, true);
	if (!ok) return {};
	return code;
}

int qr_size(const std::vector<uint8_t> &code) {
	if (code.size() != qrcodegen_BUFFER_LEN_MAX) return 0;
	const int size = code[0];
	const int minimum = qrcodegen_VERSION_MIN * 4 + 17;
	const int maximum = qrcodegen_VERSION_MAX * 4 + 17;
	if (size < minimum || size > maximum || (size - 17) % 4 != 0) {
		return 0;
	}
	return qrcodegen_getSize(code.data());
}

bool qr_dark(const std::vector<uint8_t> &code, int x, int y) {
	const int size = qr_size(code);
	return size > 0 && x >= 0 && y >= 0 && x < size && y < size
		&& qrcodegen_getModule(code.data(), x, y);
}

} // namespace addon_config
