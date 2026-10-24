#include "theme.h"

#include "../platform/platform.h"

#include <algorithm>
#include <cstdlib>
#include <string>

namespace theme {

namespace {
const char *UI_SCALE_FILE = "ui_scale.txt";
int g_percent = 100;
} // namespace

int ui_scale_percent() {
	return g_percent;
}

void set_ui_scale(int percent) {
	g_percent = std::clamp(percent, UI_SCALE_MIN, UI_SCALE_MAX);
	ui_scale_value = BASE_SCALE * g_percent / 100.0f;
	const std::string path = platform::data_dir() + UI_SCALE_FILE;
	if (g_percent == 100) {
		platform::remove_file(path);
	} else {
		platform::write_file(path, std::to_string(g_percent) + "\n");
	}
}

void load_ui_scale() {
	std::string text;
	if (!platform::read_file(platform::data_dir() + UI_SCALE_FILE, text)) return;
	g_percent = std::clamp(atoi(text.c_str()), UI_SCALE_MIN, UI_SCALE_MAX);
	ui_scale_value = BASE_SCALE * g_percent / 100.0f;
}

} // namespace theme
