#include "theme.h"

#include "../platform/platform.h"

#include <algorithm>
#include <cstdlib>
#include <string>

namespace theme {

Color BASE_100 = rgb(0x051923);
Color BASE_200 = rgb(0x0C2A38);
Color BASE_300 = rgb(0x163D50);
Color BASE_CONTENT = rgb(0xEEF1F4);
Color BASE_CONTENT_MUTED = rgb(0xEEF1F4, 0.6f);

Color PRIMARY = rgb(0x0070D1);
Color PRIMARY_CONTENT = rgb(0xFFFFFF);
Color SECONDARY = rgb(0x685CEE);
Color ACCENT = rgb(0x84DD63);
Color ACCENT_CONTENT = rgb(0x051923);
Color WARNING = rgb(0xF2C14E);
Color ERROR = rgb(0xFF8A80);

namespace {
const char *UI_SCALE_CFG = "ui_scale.cfg";
int g_ui_scale_pct = 100;
int g_home_scale_pct = 100;
int g_color_scheme = 0;
int g_display_res = 0;

const DisplayResolution DISPLAY_RESOLUTIONS[] = {
	{ "1920x1080 (16:9 Full HD - Default)", "16:9",  1920, 1080, 16.0f / 9.0f,  1.00f, 1.00f },
	{ "1920x1080 (16:9 TV Overscan 95%)",   "16:9",  1920, 1080, 16.0f / 9.0f,  0.95f, 0.98f },
	{ "1920x1080 (16:9 TV Overscan 90%)",   "16:9",  1920, 1080, 16.0f / 9.0f,  0.90f, 0.95f },
	{ "1600x900 (16:9 HD+)",                "16:9",  1600,  900, 16.0f / 9.0f,  0.96f, 0.95f },
	{ "1366x768 (16:9 WXGA TV)",            "16:9",  1366,  768, 16.0f / 9.0f,  0.95f, 0.92f },
	{ "1280x720 (16:9 HD 720p)",            "16:9",  1280,  720, 16.0f / 9.0f,  1.00f, 0.92f },
	{ "1280x720 (16:9 720p Overscan Safe)", "16:9",  1280,  720, 16.0f / 9.0f,  0.92f, 0.90f },
	{ "1024x576 (16:9 PAL Widescreen)",     "16:9",  1024,  576, 16.0f / 9.0f,  0.94f, 0.88f },
	{ "960x540 (16:9 qHD)",                 "16:9",   960,  540, 16.0f / 9.0f,  0.94f, 0.88f },
	{ "1680x1050 (16:10 WSXGA+)",           "16:10", 1680, 1050, 16.0f / 10.0f, 1.00f, 0.95f },
	{ "1440x900 (16:10 WXGA+)",             "16:10", 1440,  900, 16.0f / 10.0f, 1.00f, 0.92f },
	{ "1280x800 (16:10 WXGA)",              "16:10", 1280,  800, 16.0f / 10.0f, 1.00f, 0.90f },
	{ "1440x1080 (4:3 HD Pillarbox)",       "4:3",   1440, 1080,  4.0f / 3.0f,  1.00f, 0.88f },
	{ "1280x960 (4:3 SXGA-)",               "4:3",   1280,  960,  4.0f / 3.0f,  0.96f, 0.86f },
	{ "1024x768 (4:3 XGA)",                 "4:3",   1024,  768,  4.0f / 3.0f,  0.95f, 0.85f },
	{ "800x600 (4:3 SVGA)",                 "4:3",    800,  600,  4.0f / 3.0f,  0.93f, 0.84f },
	{ "640x480 (4:3 VGA / 480p SDTV)",      "4:3",    640,  480,  4.0f / 3.0f,  0.91f, 0.82f },
	{ "1280x1024 (5:4 SXGA)",               "5:4",   1280, 1024,  5.0f / 4.0f,  1.00f, 0.85f },
	{ "2560x1080 (21:9 Ultrawide)",         "21:9",  2560, 1080, 21.0f / 9.0f,  1.00f, 0.94f },
};
const int DISPLAY_RES_COUNT = (int)(sizeof(DISPLAY_RESOLUTIONS) / sizeof(DISPLAY_RESOLUTIONS[0]));

struct SchemeDef {
	const char *name;
	uint32_t base_100, base_200, base_300, base_content;
	uint32_t primary, primary_content, secondary, accent, warning, error;
};

const SchemeDef SCHEMES[] = {
	{ "PlayStation Royal", 0x051329, 0x0A2446, 0x123B6B, 0xF0F4F8, 0x006FCD, 0xFFFFFF, 0x5C46E8, 0x00E5FF, 0xF2C14E, 0xFF5252 },
	{ "Cyberpunk 2077",    0x0D0B14, 0x1A1429, 0x2A1F42, 0xFFF6FF, 0xFF007F, 0xFFFFFF, 0x7928CA, 0x00F0FF, 0xFFE600, 0xFF003C },
	{ "Nordic Glacier",    0x0B1219, 0x13222E, 0x1C3242, 0xEDF6F9, 0x00B4D8, 0x03151E, 0x48CAE4, 0x90E0EF, 0xF59E0B, 0xFB7185 },
	{ "Blood Ruby Cinema", 0x12080A, 0x240F13, 0x38171E, 0xFBF1F2, 0xE50914, 0xFFFFFF, 0x990012, 0xFF5A36, 0xF59E0B, 0xFF4D4D },
	{ "Matrix Terminal",   0x020D05, 0x071D0C, 0x0E2E16, 0xE8FDEB, 0x00FF66, 0x001A06, 0x00CC52, 0x39FF14, 0xEAB308, 0xEF4444 },
	{ "Solar Horizon",     0x180D06, 0x2B180C, 0x402514, 0xFFF7ED, 0xF97316, 0xFFFFFF, 0xEA580C, 0xFACC15, 0xF59E0B, 0xDC2626 },
	{ "Midnight Amethyst", 0x110A1C, 0x1E1230, 0x2E1B4A, 0xF6EEFD, 0x9333EA, 0xFFFFFF, 0x7E22CE, 0xC084FC, 0xFBBF24, 0xF43F5E },
	{ "Tokyo Midnight",    0x0A0C14, 0x141824, 0x1F2538, 0xF1F5F9, 0xE11D48, 0xFFFFFF, 0x3B82F6, 0x38BDF8, 0xF59E0B, 0xFB7185 },
	{ "Desert Dunes",      0x16120E, 0x282019, 0x3D3228, 0xFEF3C7, 0xD97706, 0xFFFFFF, 0xB45309, 0x14B8A6, 0xFBBF24, 0xEF4444 },
	{ "Emerald Abyss",     0x051614, 0x0C2B27, 0x14423C, 0xECFDF5, 0x10B981, 0x022018, 0x059669, 0x34D399, 0xF59E0B, 0xF87171 },
	{ "Retro 80s Synth",   0x140727, 0x260C45, 0x3C146B, 0xFFEDFE, 0xEC4899, 0xFFFFFF, 0x8B5CF6, 0x06B6D4, 0xFBBF24, 0xF43F5E },
	{ "Monochrome Slate",  0x09090B, 0x18181B, 0x27272A, 0xF4F4F5, 0xE4E4E7, 0x09090B, 0xA1A1AA, 0x38BDF8, 0xFBBF24, 0xEF4444 },
	{ "Aqua Lagoon",       0x041921, 0x082C3A, 0x0F4357, 0xE0F7FA, 0x00E5FF, 0x00272E, 0x00B0FF, 0xFF6E40, 0xFFD740, 0xFF5252 },
	{ "Vampire Velvet",    0x140810, 0x250F1E, 0x3B1831, 0xFCE7F3, 0xD946EF, 0xFFFFFF, 0xA21CAF, 0xFB7185, 0xFBBF24, 0xE11D48 },
	{ "Amber CRT Terminal",0x140F04, 0x261B07, 0x3B2B0C, 0xFFFBEB, 0xF59E0B, 0x1E1403, 0xD97706, 0x10B981, 0xFBBF24, 0xEF4444 },
};
const int SCHEME_COUNT = (int)(sizeof(SCHEMES) / sizeof(SCHEMES[0]));

void save_ui_cfg() {
	const std::string path = platform::data_dir() + UI_SCALE_CFG;
	if (g_ui_scale_pct == 100 && g_home_scale_pct == 100 && g_color_scheme == 0 && g_display_res == 0) {
		platform::remove_file(path);
	} else {
		platform::write_file(path, "scale=" + std::to_string(g_ui_scale_pct) +
				"\nhome_scale=" + std::to_string(g_home_scale_pct) +
				"\nscheme=" + std::to_string(g_color_scheme) +
				"\ndisplay=" + std::to_string(g_display_res) + "\n");
	}
}
} // namespace

int display_resolution_count() {
	return DISPLAY_RES_COUNT;
}

int display_resolution_index() {
	return g_display_res;
}

const DisplayResolution &display_resolution(int index) {
	if (index < 0 || index >= DISPLAY_RES_COUNT) return DISPLAY_RESOLUTIONS[0];
	return DISPLAY_RESOLUTIONS[index];
}

const DisplayResolution &current_display_resolution() {
	return display_resolution(g_display_res);
}

void set_display_resolution(int index, bool save) {
	if (index < 0) index = (index % DISPLAY_RES_COUNT + DISPLAY_RES_COUNT) % DISPLAY_RES_COUNT;
	else index = index % DISPLAY_RES_COUNT;
	g_display_res = index;
	if (save) save_ui_cfg();
}

float display_aspect_ratio() {
	return current_display_resolution().aspect;
}

void display_viewport_box(float canvas_w, float canvas_h, float &vx, float &vy, float &vw, float &vh) {
	const DisplayResolution &d = current_display_resolution();
	const float canvas_aspect = canvas_w / canvas_h;
	if (d.aspect <= canvas_aspect) {
		vh = canvas_h * d.safe_scale;
		vw = vh * d.aspect;
	} else {
		vw = canvas_w * d.safe_scale;
		vh = vw / d.aspect;
	}
	vx = (canvas_w - vw) * 0.5f;
	vy = (canvas_h - vh) * 0.5f;
}

int color_scheme_count() {
	return SCHEME_COUNT;
}

int color_scheme_index() {
	return g_color_scheme;
}

const char *color_scheme_name(int index) {
	if (index < 0 || index >= SCHEME_COUNT) return SCHEMES[0].name;
	return SCHEMES[index].name;
}

SchemePreview color_scheme_preview(int index) {
	if (index < 0 || index >= SCHEME_COUNT) index = 0;
	const SchemeDef &s = SCHEMES[index];
	return SchemePreview{
		s.name,
		rgb(s.base_100), rgb(s.base_200), rgb(s.base_300), rgb(s.base_content),
		rgb(s.primary), rgb(s.secondary), rgb(s.accent)
	};
}

void set_color_scheme(int index, bool save) {
	if (index < 0) index = (index % SCHEME_COUNT + SCHEME_COUNT) % SCHEME_COUNT;
	else index = index % SCHEME_COUNT;
	g_color_scheme = index;
	const SchemeDef &s = SCHEMES[index];
	BASE_100 = rgb(s.base_100);
	BASE_200 = rgb(s.base_200);
	BASE_300 = rgb(s.base_300);
	BASE_CONTENT = rgb(s.base_content);
	BASE_CONTENT_MUTED = rgb(s.base_content, 0.6f);
	PRIMARY = rgb(s.primary);
	PRIMARY_CONTENT = rgb(s.primary_content);
	SECONDARY = rgb(s.secondary);
	ACCENT = rgb(s.accent);
	ACCENT_CONTENT = BASE_100;
	WARNING = rgb(s.warning);
	ERROR = rgb(s.error);
	if (save) save_ui_cfg();
}

int ui_scale_percent() {
	return g_ui_scale_pct;
}

float ui_scale() {
	return DEFAULT_UI_SCALE * (g_ui_scale_pct / 100.0f) * current_display_resolution().ui_factor;
}

void set_ui_scale_percent(int pct, bool save) {
	pct = std::clamp(pct, UI_SCALE_MIN_PCT, UI_SCALE_MAX_PCT);
	pct = ((pct + UI_SCALE_STEP_PCT / 2) / UI_SCALE_STEP_PCT) * UI_SCALE_STEP_PCT;
	g_ui_scale_pct = std::clamp(pct, UI_SCALE_MIN_PCT, UI_SCALE_MAX_PCT);
	if (save) save_ui_cfg();
}

int home_scale_percent() {
	return g_home_scale_pct;
}

float home_scale() {
	return DEFAULT_UI_SCALE * (g_home_scale_pct / 100.0f) * current_display_resolution().ui_factor;
}

void set_home_scale_percent(int pct, bool save) {
	pct = std::clamp(pct, HOME_SCALE_MIN_PCT, HOME_SCALE_MAX_PCT);
	pct = ((pct + HOME_SCALE_STEP_PCT / 2) / HOME_SCALE_STEP_PCT) * HOME_SCALE_STEP_PCT;
	g_home_scale_pct = std::clamp(pct, HOME_SCALE_MIN_PCT, HOME_SCALE_MAX_PCT);
	if (save) save_ui_cfg();
}

void load_ui_scale() {
	set_color_scheme(0, false);
	set_display_resolution(0, false);
	std::string cfg;
	if (!platform::read_file(platform::data_dir() + UI_SCALE_CFG, cfg)) return;
	const size_t pos = cfg.find("scale=");
	if (pos != std::string::npos && (pos == 0 || cfg[pos - 1] == '\n')) {
		const int pct = atoi(cfg.c_str() + pos + 6);
		if (pct >= UI_SCALE_MIN_PCT && pct <= UI_SCALE_MAX_PCT) {
			set_ui_scale_percent(pct, false);
		}
	}
	const size_t hpos = cfg.find("home_scale=");
	if (hpos != std::string::npos) {
		const int hpct = atoi(cfg.c_str() + hpos + 11);
		if (hpct >= HOME_SCALE_MIN_PCT && hpct <= HOME_SCALE_MAX_PCT) {
			set_home_scale_percent(hpct, false);
		}
	}
	const size_t spos = cfg.find("scheme=");
	if (spos != std::string::npos) {
		const int scheme = atoi(cfg.c_str() + spos + 7);
		if (scheme >= 0 && scheme < SCHEME_COUNT) {
			set_color_scheme(scheme, false);
		}
	}
	const size_t dpos = cfg.find("display=");
	if (dpos != std::string::npos) {
		const int disp = atoi(cfg.c_str() + dpos + 8);
		if (disp >= 0 && disp < DISPLAY_RES_COUNT) {
			set_display_resolution(disp, false);
		}
	}
}

} // namespace theme
