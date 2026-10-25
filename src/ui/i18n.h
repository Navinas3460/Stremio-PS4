// Interface translations, separate from audio and subtitle language names.
#pragma once

#include "../platform/platform.h"

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
	INTERFACE_LANGUAGE
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
	}
	return "";
}

} // namespace i18n
