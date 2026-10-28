// Interface localization, separate from audio and subtitle languages.
#pragma once

#include "../platform/platform.h"
#include "locales/keys.h"
#include "locales/en.h"
#include "locales/es.h"

#include <cctype>
#include <cstddef>
#include <string>

namespace i18n {

enum class Language { ENGLISH, SPANISH };

struct LanguageInfo {
    Language id;
    const char *code;
    const char *name;
    const char *(*lookup)(Text);
};

// Register supported languages here.
inline const LanguageInfo languages[] = {
    { Language::ENGLISH, "en", "English", locales::en },
    { Language::SPANISH, "es", "Español", locales::es }
};

inline constexpr std::size_t language_count =
    sizeof(languages) / sizeof(languages[0]);

inline Language current_language = Language::ENGLISH;

inline const LanguageInfo &language_info(Language id) {
    for (const auto &entry : languages) {
        if (entry.id == id) return entry;
    }
    return languages[0];
}

inline Language language() {
    return current_language;
}

inline void load_language() {
    current_language = Language::ENGLISH;
    std::string code;
    if (!platform::read_file(platform::data_dir() + "ui_language.txt", code)) return;
    const std::size_t end = code.find_last_not_of(" \t\r\n");
    if (end == std::string::npos) return;
    code.erase(end + 1);
    for (const auto &entry : languages) {
        if (code == entry.code) {
            current_language = entry.id;
            return;
        }
    }
}

inline void set_language(Language value) {
    const LanguageInfo &entry = language_info(value);
    current_language = entry.id;
    platform::write_file(platform::data_dir() + "ui_language.txt",
        std::string(entry.code) + "\n");
}

inline const char *language_name() {
    return language_info(current_language).name;
}

inline void next_language() {
    for (std::size_t i = 0; i < language_count; ++i) {
        if (languages[i].id == current_language) {
            set_language(languages[(i + 1) % language_count].id);
            return;
        }
    }
    set_language(Language::ENGLISH);
}

inline const char *tr(Text key) {
    const char *value = language_info(current_language).lookup(key);
    if (value != nullptr) return value;
    value = locales::en(key);
    return value != nullptr ? value : "";
}

inline std::string lowercase_ascii(std::string value) {
    for (char &c : value) {
        const unsigned char ch = static_cast<unsigned char>(c);
        if (ch < 128) c = static_cast<char>(std::tolower(ch));
    }
    return value;
}

// Match known external display labels, never API values or content titles.
inline std::string label(const std::string &value) {
    if (current_language == Language::ENGLISH) return value;

    static const struct { const char *source; Text key; } aliases[] = {
        { "Other", Text::PLAYER_OTHER },
        { "English", Text::TRACK_LANG_ENGLISH },
        { "Spanish", Text::TRACK_LANG_SPANISH },
        { "French", Text::TRACK_LANG_FRENCH },
        { "German", Text::TRACK_LANG_GERMAN },
        { "Italian", Text::TRACK_LANG_ITALIAN },
        { "Portuguese", Text::TRACK_LANG_PORTUGUESE },
        { "Portuguese (Brazil)", Text::TRACK_LANG_PORTUGUESE_BRAZIL },
        { "Dutch", Text::TRACK_LANG_DUTCH },
        { "Swedish", Text::TRACK_LANG_SWEDISH },
        { "Norwegian", Text::TRACK_LANG_NORWEGIAN },
        { "Danish", Text::TRACK_LANG_DANISH },
        { "Finnish", Text::TRACK_LANG_FINNISH },
        { "Polish", Text::TRACK_LANG_POLISH },
        { "Czech", Text::TRACK_LANG_CZECH },
        { "Slovak", Text::TRACK_LANG_SLOVAK },
        { "Hungarian", Text::TRACK_LANG_HUNGARIAN },
        { "Romanian", Text::TRACK_LANG_ROMANIAN },
        { "Bulgarian", Text::TRACK_LANG_BULGARIAN },
        { "Greek", Text::TRACK_LANG_GREEK },
        { "Turkish", Text::TRACK_LANG_TURKISH },
        { "Russian", Text::TRACK_LANG_RUSSIAN },
        { "Ukrainian", Text::TRACK_LANG_UKRAINIAN },
        { "Croatian", Text::TRACK_LANG_CROATIAN },
        { "Serbian", Text::TRACK_LANG_SERBIAN },
        { "Slovenian", Text::TRACK_LANG_SLOVENIAN },
        { "Bosnian", Text::TRACK_LANG_BOSNIAN },
        { "Macedonian", Text::TRACK_LANG_MACEDONIAN },
        { "Estonian", Text::TRACK_LANG_ESTONIAN },
        { "Latvian", Text::TRACK_LANG_LATVIAN },
        { "Lithuanian", Text::TRACK_LANG_LITHUANIAN },
        { "Arabic", Text::TRACK_LANG_ARABIC },
        { "Hebrew", Text::TRACK_LANG_HEBREW },
        { "Persian", Text::TRACK_LANG_PERSIAN },
        { "Hindi", Text::TRACK_LANG_HINDI },
        { "Bengali", Text::TRACK_LANG_BENGALI },
        { "Tamil", Text::TRACK_LANG_TAMIL },
        { "Telugu", Text::TRACK_LANG_TELUGU },
        { "Malayalam", Text::TRACK_LANG_MALAYALAM },
        { "Kannada", Text::TRACK_LANG_KANNADA },
        { "Marathi", Text::TRACK_LANG_MARATHI },
        { "Urdu", Text::TRACK_LANG_URDU },
        { "Thai", Text::TRACK_LANG_THAI },
        { "Vietnamese", Text::TRACK_LANG_VIETNAMESE },
        { "Indonesian", Text::TRACK_LANG_INDONESIAN },
        { "Malay", Text::TRACK_LANG_MALAY },
        { "Chinese", Text::TRACK_LANG_CHINESE },
        { "Chinese (traditional)", Text::TRACK_LANG_CHINESE_TRADITIONAL },
        { "Chinese (bilingual)", Text::TRACK_LANG_CHINESE_BILINGUAL },
        { "Japanese", Text::TRACK_LANG_JAPANESE },
        { "Korean", Text::TRACK_LANG_KOREAN },
        { "Catalan", Text::TRACK_LANG_CATALAN },
        { "Basque", Text::TRACK_LANG_BASQUE },
        { "Galician", Text::TRACK_LANG_GALICIAN },
        { "Icelandic", Text::TRACK_LANG_ICELANDIC },
        { "Irish", Text::TRACK_LANG_IRISH },
        { "Albanian", Text::TRACK_LANG_ALBANIAN },
        { "Armenian", Text::TRACK_LANG_ARMENIAN },
        { "Georgian", Text::TRACK_LANG_GEORGIAN },
        { "Unknown", Text::TRACK_LANG_UNKNOWN },
        { "Space", Text::SPACE },
        { "Delete", Text::DELETE_KEY },
        { "Clear", Text::CLEAR },
        { "Results", Text::RESULTS },
        { "Movie", Text::TYPE_MOVIES },
        { "Movies", Text::TYPE_MOVIES },
        { "Series", Text::TYPE_SERIES },
        { "Channel", Text::TYPE_CHANNELS },
        { "Channels", Text::TYPE_CHANNELS },
        { "Popular", Text::POPULAR },
        { "Featured", Text::FEATURED },
        { "Featured Movies", Text::FEATURED_MOVIES },
        { "Featured Series", Text::FEATURED_SERIES },
        { "New", Text::CATALOG_NEW },
        { "Trending", Text::TRENDING },
        { "Top Airing", Text::TOP_AIRING },
        { "Most Popular", Text::MOST_POPULAR },
        { "Highest Rated", Text::HIGHEST_RATED },
        { "Kitsu Trending", Text::KITSU_TRENDING },
        { "Kitsu Top Airing", Text::KITSU_TOP_AIRING },
        { "Kitsu Most Popular", Text::KITSU_MOST_POPULAR },
        { "Kitsu Highest Rated", Text::KITSU_HIGHEST_RATED },
        { "Top Seeded", Text::TOP_SEEDED },
        { "Type", Text::FILTER_TYPE },
        { "Catalog", Text::FILTER_CATALOG },
        { "Year", Text::FILTER_YEAR },
        { "Genre", Text::FILTER_GENRE },
        { "All", Text::FILTER_ALL },
        { "Action", Text::GENRE_ACTION },
        { "Adventure", Text::GENRE_ADVENTURE },
        { "Animation", Text::GENRE_ANIMATION },
        { "Comedy", Text::GENRE_COMEDY },
        { "Crime", Text::GENRE_CRIME },
        { "Documentary", Text::GENRE_DOCUMENTARY },
        { "Drama", Text::GENRE_DRAMA },
        { "Family", Text::GENRE_FAMILY },
        { "Fantasy", Text::GENRE_FANTASY },
        { "History", Text::GENRE_HISTORY },
        { "Horror", Text::GENRE_HORROR },
        { "Music", Text::GENRE_MUSIC },
        { "Mystery", Text::GENRE_MYSTERY },
        { "Romance", Text::GENRE_ROMANCE },
        { "Science Fiction", Text::GENRE_SCIENCE_FICTION },
        { "Sci-Fi", Text::GENRE_SCIENCE_FICTION },
        { "Thriller", Text::GENRE_THRILLER },
        { "War", Text::GENRE_WAR },
        { "Western", Text::GENRE_WESTERN }
    };

    const std::string key = lowercase_ascii(value);
    for (const auto &entry : aliases) {
        if (key == lowercase_ascii(entry.source)) return tr(entry.key);
    }
    return value;
}

inline std::string catalog_label(const std::string &value) {
    if (current_language == Language::ENGLISH) return value;
    const std::string translated = label(value);
    if (translated != value) return translated;
    const std::size_t suffix = value.rfind(" (");
    if (suffix != std::string::npos && !value.empty() && value.back() == ')') {
        return label(value.substr(0, suffix)) + value.substr(suffix);
    }
    return value;
}

inline std::string row_title(const std::string &value) {
    if (current_language == Language::ENGLISH) return value;
    const std::size_t separator = value.rfind(" - ");
    if (separator == std::string::npos) return catalog_label(value);
    return catalog_label(value.substr(0, separator)) + " - "
        + label(value.substr(separator + 3));
}

} // namespace i18n
