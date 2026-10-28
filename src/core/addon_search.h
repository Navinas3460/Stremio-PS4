// Local matching for the community add-on directory.
#pragma once

#include <string>

namespace addon_search {

inline std::string normalize(std::string text) {
	const struct {
		const char *from;
		const char *to;
	} accents[] = {
		{ "\xC3\x81", "a" }, { "\xC3\xA1", "a" },
		{ "\xC3\x89", "e" }, { "\xC3\xA9", "e" },
		{ "\xC3\x8D", "i" }, { "\xC3\xAD", "i" },
		{ "\xC3\x93", "o" }, { "\xC3\xB3", "o" },
		{ "\xC3\x9A", "u" }, { "\xC3\xBA", "u" },
		{ "\xC3\x9C", "u" }, { "\xC3\xBC", "u" },
		{ "\xC3\x91", "n" }, { "\xC3\xB1", "n" }
	};
	for (const auto &accent : accents) {
		size_t pos = 0;
		while ((pos = text.find(accent.from, pos)) != std::string::npos) {
			text.replace(pos, 2, accent.to);
			++pos;
		}
	}
	for (char &ch : text) {
		if (ch >= 'A' && ch <= 'Z') ch = char(ch - 'A' + 'a');
	}
	return text;
}

inline bool matches(const std::string &name, const std::string &id,
		const std::string &query) {
	const std::string needle = normalize(query);
	return needle.empty()
		|| normalize(name).find(needle) != std::string::npos
		|| normalize(id).find(needle) != std::string::npos;
}

} // namespace addon_search
