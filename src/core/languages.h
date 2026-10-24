// Language names for ISO 639-1 / 639-2 codes (subtitle and audio tracks,
// add-on subtitles). Unknown codes are returned as given, upper-cased.
// Some add-ons send full names ("English") or regional codes ("pt-BR",
// "zh_TW"); those are recognised too.
#pragma once

#include <cctype>
#include <string>

// The language's name, or nullptr for a code that isn't recognised.
inline const char *find_language(const std::string &code_in) {
	std::string code;
	for (char c : code_in) code += (char)tolower((unsigned char)c);
	struct Entry {
		const char *a, *b, *b2, *name; // 639-1, 639-2/B, 639-2/T (or alias)
	};
	static const Entry table[] = {
		{ "en", "eng", "", "English" }, { "es", "spa", "", "Spanish" }, { "fr", "fre", "fra", "French" },
		{ "de", "ger", "deu", "German" }, { "it", "ita", "", "Italian" }, { "pt", "por", "", "Portuguese" },
		{ "", "pob", "pb", "Portuguese (Brazil)" }, { "nl", "dut", "nld", "Dutch" }, { "sv", "swe", "", "Swedish" },
		{ "no", "nor", "nob", "Norwegian" }, { "da", "dan", "", "Danish" }, { "fi", "fin", "", "Finnish" },
		{ "pl", "pol", "", "Polish" }, { "cs", "cze", "ces", "Czech" }, { "sk", "slo", "slk", "Slovak" },
		{ "hu", "hun", "", "Hungarian" }, { "ro", "rum", "ron", "Romanian" }, { "bg", "bul", "", "Bulgarian" },
		{ "el", "gre", "ell", "Greek" }, { "tr", "tur", "", "Turkish" }, { "ru", "rus", "", "Russian" },
		{ "uk", "ukr", "", "Ukrainian" }, { "hr", "hrv", "scr", "Croatian" }, { "sr", "srp", "scc", "Serbian" },
		{ "sl", "slv", "", "Slovenian" }, { "bs", "bos", "", "Bosnian" }, { "mk", "mac", "mkd", "Macedonian" },
		{ "et", "est", "", "Estonian" }, { "lv", "lav", "", "Latvian" }, { "lt", "lit", "", "Lithuanian" },
		{ "ar", "ara", "", "Arabic" }, { "he", "heb", "", "Hebrew" }, { "fa", "per", "fas", "Persian" },
		{ "hi", "hin", "", "Hindi" }, { "bn", "ben", "", "Bengali" }, { "ta", "tam", "", "Tamil" },
		{ "te", "tel", "", "Telugu" }, { "ml", "mal", "", "Malayalam" }, { "kn", "kan", "", "Kannada" },
		{ "mr", "mar", "", "Marathi" }, { "ur", "urd", "", "Urdu" }, { "th", "tha", "", "Thai" },
		{ "vi", "vie", "", "Vietnamese" }, { "id", "ind", "", "Indonesian" }, { "ms", "may", "msa", "Malay" },
		{ "zh", "chi", "zho", "Chinese" }, { "", "zht", "", "Chinese (traditional)" }, { "", "zhe", "", "Chinese (bilingual)" },
		{ "ja", "jpn", "", "Japanese" }, { "ko", "kor", "", "Korean" }, { "ca", "cat", "", "Catalan" },
		{ "eu", "baq", "eus", "Basque" }, { "gl", "glg", "", "Galician" }, { "is", "ice", "isl", "Icelandic" },
		{ "ga", "gle", "", "Irish" }, { "sq", "alb", "sqi", "Albanian" }, { "hy", "arm", "hye", "Armenian" },
		{ "ka", "geo", "kat", "Georgian" }, { "", "und", "", "Unknown" },
	};
	if (code.empty()) return nullptr;
	for (const Entry &e : table) {
		if (code == e.a || code == e.b || code == e.b2) return e.name;
	}
	for (const Entry &e : table) {
		std::string name;
		for (const char *c = e.name; *c; c++) name += (char)tolower((unsigned char)*c);
		if (code == name) return e.name;
	}
	const size_t sep = code.find_first_of("-_");
	if (sep != std::string::npos) {
		const std::string base = code.substr(0, sep), region = code.substr(sep + 1);
		if (base == "pt" && region == "br") return find_language("pob");
		if (base == "zh" && (region == "tw" || region == "hk" || region == "hant")) return find_language("zht");
		return find_language(base);
	}
	return nullptr;
}

inline std::string language_name(const std::string &code_in) {
	if (const char *name = find_language(code_in)) return name;
	std::string upper;
	for (char c : code_in) upper += (char)toupper((unsigned char)c);
	return upper;
}
