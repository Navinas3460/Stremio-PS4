#include "charset.h"

#include "languages.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iconv.h>

namespace {

// Converts with the first of `names` iconv knows. False if none is known
// or `in` isn't valid in that encoding.
bool convert(const std::string &in, const std::vector<const char *> &names, std::string &out) {
	for (const char *name : names) {
		iconv_t cd = iconv_open("UTF-8", name);
		if (cd == (iconv_t)-1) continue;
		out.assign(in.size() * 2 + 16, '\0');
		char *src = const_cast<char *>(in.data());
		size_t src_left = in.size();
		size_t done = 0;
		bool ok = true;
		while (src_left > 0) {
			char *dst = &out[done];
			size_t dst_left = out.size() - done;
			const size_t r = iconv(cd, &src, &src_left, &dst, &dst_left);
			done = out.size() - dst_left;
			if (r != (size_t)-1) continue;
			if (errno == E2BIG) {
				out.resize(out.size() * 2);
			} else { // EILSEQ / EINVAL: not this encoding
				ok = false;
				break;
			}
		}
		if (ok) {
			// Flush: Hebrew and Vietnamese converters hold back the last
			// character in case a combining mark follows.
			out.resize(done + 16);
			char *dst = &out[done];
			size_t dst_left = 16;
			if (iconv(cd, nullptr, nullptr, &dst, &dst_left) != (size_t)-1) done = out.size() - dst_left;
		}
		iconv_close(cd);
		if (!ok) return false;
		out.resize(done);
		return true;
	}
	return false;
}

// UTF-16 without a BOM: one byte of most ASCII-range characters is zero.
// Returns "UTF-16LE", "UTF-16BE" or nullptr.
const char *utf16_without_bom(const std::string &data) {
	const size_t n = std::min<size_t>(data.size() & ~(size_t)1, 4096);
	if (n < 16) return nullptr;
	size_t even = 0, odd = 0;
	for (size_t i = 0; i < n; i += 2) {
		even += data[i] == 0;
		odd += data[i + 1] == 0;
	}
	if (odd > n / 2 * 3 / 10 && even == 0) return "UTF-16LE";
	if (even > n / 2 * 3 / 10 && odd == 0) return "UTF-16BE";
	return nullptr;
}

// Very common Chinese characters (simplified and traditional forms). GBK and
// Big5 accept each other's bytes, so the decoding with more of these wins.
int chinese_score(const std::string &utf8) {
	static const char *common = "的一是不了人我在有他这這个個们們你來来说說中大上为為和到会會要就出也时時好看吗嗎什么麼没沒知道去"
								"那她想还還可以过過里裡事情";
	int score = 0;
	for (size_t i = 0; i + 2 < utf8.size(); i++) {
		const unsigned char c = (unsigned char)utf8[i];
		if (c < 0xE0 || c > 0xEF) continue; // only 3-byte sequences
		if (strstr(common, utf8.substr(i, 3).c_str())) score++;
		i += 2;
	}
	return score;
}

} // namespace

bool is_utf8(const std::string &s) {
	for (size_t i = 0; i < s.size();) {
		const unsigned char c = (unsigned char)s[i];
		int extra;
		if (c < 0x80) {
			extra = 0;
		} else if (c >= 0xC2 && c <= 0xDF) {
			extra = 1;
		} else if (c >= 0xE0 && c <= 0xEF) {
			extra = 2;
		} else if (c >= 0xF0 && c <= 0xF4) {
			extra = 3;
		} else {
			return false;
		}
		if (extra > 0 && i + extra >= s.size()) return false;
		for (int k = 1; k <= extra; k++) {
			if (((unsigned char)s[i + k] & 0xC0) != 0x80) return false;
		}
		i += extra + 1;
	}
	return true;
}

std::vector<std::vector<const char *>> legacy_encodings(const std::string &language) {
	const std::string name = language_name(language);
	auto is = [&](std::initializer_list<const char *> names) {
		for (const char *n : names) {
			if (name == n) return true;
		}
		return false;
	};
	const std::vector<const char *> cp1250 = { "CP1250", "WINDOWS-1250" }, cp1251 = { "CP1251", "WINDOWS-1251" };
	if (is({ "Russian", "Ukrainian", "Bulgarian", "Macedonian" })) return { cp1251 };
	// Serbian subtitles come in both scripts; the Cyrillic guess is checked
	// by to_utf8.
	if (is({ "Serbian" })) return { cp1250, cp1251 };
	if (is({ "Polish", "Czech", "Slovak", "Hungarian", "Romanian", "Croatian", "Slovenian", "Bosnian", "Albanian" })) {
		return { cp1250 };
	}
	if (is({ "Greek" })) return { { "CP1253", "WINDOWS-1253" } };
	if (is({ "Turkish" })) return { { "CP1254", "WINDOWS-1254" } };
	if (is({ "Hebrew" })) return { { "CP1255", "WINDOWS-1255" } };
	if (is({ "Arabic", "Persian", "Urdu" })) return { { "CP1256", "WINDOWS-1256" } };
	if (is({ "Estonian", "Latvian", "Lithuanian" })) return { { "CP1257", "WINDOWS-1257" } };
	if (is({ "Vietnamese" })) return { { "CP1258", "WINDOWS-1258" } };
	if (is({ "Thai" })) return { { "CP874", "TIS-620", "WINDOWS-874" } };
	if (is({ "Japanese" })) return { { "CP932", "SHIFT_JIS" }, { "EUC-JP" } };
	if (is({ "Korean" })) return { { "CP949", "EUC-KR" } };
	if (is({ "Chinese (traditional)" })) return { { "BIG5" }, { "GB18030", "GBK" } };
	if (is({ "Chinese", "Chinese (bilingual)" })) return { { "GB18030", "GBK" }, { "BIG5" } };
	return {};
}

std::string to_utf8(const std::string &data, const std::string &language) {
	if (data.compare(0, 3, "\xEF\xBB\xBF") == 0) return data.substr(3);
	std::string out;
	if (data.compare(0, 2, "\xFF\xFE") == 0 && convert(data.substr(2), { "UTF-16LE" }, out)) return out;
	if (data.compare(0, 2, "\xFE\xFF") == 0 && convert(data.substr(2), { "UTF-16BE" }, out)) return out;
	if (const char *utf16 = utf16_without_bom(data)) {
		if (convert(data, { utf16 }, out)) return out;
	}
	if (is_utf8(data)) return data;

	const auto candidates = legacy_encodings(language);
	const std::string name = language_name(language);
	if (name == "Chinese" || name == "Chinese (traditional)" || name == "Chinese (bilingual)") {
		std::string best;
		int best_score = -1;
		for (const auto &names : candidates) {
			if (convert(data, names, out)) {
				const int score = chinese_score(out);
				if (score > best_score) {
					best_score = score;
					best.swap(out);
				}
			}
		}
		if (best_score >= 0) return best;
	} else if (name == "Serbian") {
		// Cyrillic text in Windows-1251 is mostly bytes >= 0xC0 (letters);
		// Latin text in Windows-1250 has few of them (č, ć, š, ž, đ).
		size_t high = 0, letters = 0;
		for (char ch : data) {
			const unsigned char c = (unsigned char)ch;
			high += c >= 0xC0;
			letters += c >= 0xC0 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
		}
		if (letters > 0 && high * 2 > letters && convert(data, candidates[1], out)) return out;
		if (convert(data, candidates[0], out)) return out;
	} else {
		for (const auto &names : candidates) {
			if (convert(data, names, out)) return out;
		}
	}
	if (convert(data, { "CP1252", "WINDOWS-1252" }, out)) return out;
	convert(data, { "ISO-8859-1", "LATIN1" }, out); // decodes any byte
	return out;
}
