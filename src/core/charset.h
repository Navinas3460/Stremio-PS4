// Subtitle files in legacy encodings (Windows-1251 Russian, Windows-1256
// Arabic, GBK Chinese...) converted to UTF-8 for display, via iconv.
#pragma once

#include <string>
#include <vector>

// Whether `s` is well-formed UTF-8 (plain ASCII is).
bool is_utf8(const std::string &s);

// `data` as UTF-8, without a byte order mark. UTF-8 is returned as is;
// UTF-16 is recognised by its BOM or its zero bytes. Anything else is
// decoded with the legacy encoding usual for `language` (an ISO 639 code
// such as "ara" or "ru"), or Windows-1252 if the language has none.
std::string to_utf8(const std::string &data, const std::string &language);

// The legacy encodings tried for `language`, most likely first. Each entry
// lists alternative iconv names for one encoding (glibc and musl differ).
std::vector<std::vector<const char *>> legacy_encodings(const std::string &language);
