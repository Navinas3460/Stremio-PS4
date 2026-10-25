// Interface translations, separate from audio and subtitle language names.
#pragma once

#include "../platform/platform.h"

#include <cctype>
#include <string>

namespace i18n {

enum class Language { ENGLISH, SPANISH };

enum class Text {
	HOME,
	DISCOVER,
	SEARCH,
	LIBRARY,
	SETTINGS,
	SIGN_OUT,
	SIGN_OUT_CONFIRM,
	UI_SCALE,
	VIDEO_PLAYER,
	PLAYER_HARDWARE,
	PLAYER_FFMPEG,
	PLAYER_HARDWARE_TOAST,
	PLAYER_FFMPEG_TOAST,
        INTERFACE_LANGUAGE,
        CONTINUE_WATCHING,
        FILTERS
};

inline Language current_language = Language::ENGLISH;

inline Language language() {
	return current_language;
}

inline void load_language() {
	current_language = Language::ENGLISH;
	std::string text;
	if (!platform::read_file(platform::data_dir() + "ui_language.txt", text)) return;
	const size_t end = text.find_last_not_of(" \t\r\n");
	if (end == std::string::npos) return;
	text.erase(end + 1);
	if (text == "es") current_language = Language::SPANISH;
}

inline void set_language(Language value) {
	current_language = value;
	platform::write_file(platform::data_dir() + "ui_language.txt",
			value == Language::SPANISH ? "es\n" : "en\n");
}

inline const char *language_name() {
	return current_language == Language::SPANISH ? "Español" : "English";
}

inline const char *tr(const char *english, const char *spanish) {
        return current_language == Language::SPANISH ? spanish : english;
}

inline const char *tr(Text text) {
	const bool es = current_language == Language::SPANISH;
	switch (text) {
		case Text::HOME:
			return es ? "Inicio" : "Home";
		case Text::DISCOVER:
			return es ? "Explorar" : "Discover";
		case Text::SEARCH:
			return es ? "Buscar" : "Search";
		case Text::LIBRARY:
			return es ? "Biblioteca" : "Library";
		case Text::SETTINGS:
			return es ? "Ajustes" : "Settings";
		case Text::SIGN_OUT:
			return es ? "Cerrar sesión" : "Sign out";
		case Text::SIGN_OUT_CONFIRM:
			return es ? "Pulsa \xC3\x97 otra vez para cerrar sesión"
			          : "Press \xC3\x97 again to sign out";
		case Text::UI_SCALE:
			return es ? "Escala de interfaz" : "UI scale";
		case Text::VIDEO_PLAYER:
			return es ? "Reproductor de vídeo" : "Video player";
		case Text::PLAYER_HARDWARE:
			return es ? "Hardware de PS4 (solo MP4)" : "PS4 hardware (MP4 only)";
		case Text::PLAYER_FFMPEG:
			return es ? "FFmpeg (reproduce todo)" : "FFmpeg (plays everything)";
		case Text::PLAYER_HARDWARE_TOAST:
			return es ? "Se usará el reproductor de PS4 (solo MP4)"
			          : "Videos will use the PS4's hardware player (MP4 only)";
		case Text::PLAYER_FFMPEG_TOAST:
			return es ? "Se usará FFmpeg" : "Videos will use FFmpeg";
		case Text::INTERFACE_LANGUAGE:
			return es ? "Idioma de la interfaz" : "Interface language";
                case Text::CONTINUE_WATCHING:
                        return es ? "Continuar viendo" : "Continue watching";
                case Text::FILTERS:
                        return es ? "Filtros" : "Filters";
	}
	return "";
}

// Translate display labels only; keep unknown labels unchanged.
inline std::string label(const std::string &value) {
        if (current_language != Language::SPANISH) return value;
        static const struct { const char *en; const char *es; } labels[] = {
                { "Space", "Espacio" },
                { "Delete", "Borrar" },
                { "Clear", "Limpiar" },
                { "Results", "Resultados" },
                { "Movie", "Películas" },
                { "Movies", "Películas" },
                { "Series", "Series" },
                { "Channel", "Canales" },
                { "Channels", "Canales" },
                { "Popular", "Popular" },
                { "Featured", "Destacados" },
                { "Featured Movies", "Películas destacadas" },
                { "Featured Series", "Series destacadas" },
                { "New", "Novedades" },
                { "Trending", "Tendencias" },
                { "Top Airing", "Lo mejor en emisión" },
                { "Most Popular", "Lo más popular" },
                { "Highest Rated", "Lo mejor valorado" },
                { "Kitsu Trending", "Tendencias en Kitsu" },
                { "Kitsu Top Airing", "Lo mejor en emisión en Kitsu" },
                { "Kitsu Most Popular", "Lo más popular en Kitsu" },
                { "Kitsu Highest Rated", "Lo mejor valorado en Kitsu" },
                { "Top Seeded", "Con más fuentes" },
                { "Action", "Acción" },
                { "Adventure", "Aventura" },
                { "Animation", "Animación" },
                { "Comedy", "Comedia" },
                { "Crime", "Crimen" },
                { "Documentary", "Documental" },
                { "Drama", "Drama" },
                { "Family", "Familiar" },
                { "Fantasy", "Fantasía" },
                { "History", "Historia" },
                { "Horror", "Terror" },
                { "Music", "Música" },
                { "Mystery", "Misterio" },
                { "Romance", "Romance" },
                { "Science Fiction", "Ciencia ficción" },
                { "Sci-Fi", "Ciencia ficción" },
                { "Thriller", "Suspenso" },
                { "War", "Bélico" },
                { "Western", "Oeste" },
                { "Type", "Tipo" },
                { "Catalog", "Catálogo" },
                { "Year", "Año" },
                { "Genre", "Género" },
                { "All", "Todos" }
        };
        auto lowercase = [](std::string text) {
                for (char &c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                return text;
        };
        const std::string key = lowercase(value);
        for (const auto &entry : labels) {
                if (key == lowercase(entry.en)) return entry.es;
        }
        return value;
}

inline std::string catalog_label(const std::string &value) {
        if (current_language != Language::SPANISH) return value;
        const std::string translated = label(value);
        if (translated != value) return translated;
        const size_t suffix = value.rfind(" (");
        if (suffix != std::string::npos && !value.empty() && value.back() == ')') {
                return label(value.substr(0, suffix)) + value.substr(suffix);
        }
        return value;
}

inline std::string row_title(const std::string &value) {
        if (current_language != Language::SPANISH) return value;
        const size_t separator = value.rfind(" - ");
        if (separator == std::string::npos) return catalog_label(value);
        return catalog_label(value.substr(0, separator)) + " - "
                + label(value.substr(separator + 3));
}

} // namespace i18n
