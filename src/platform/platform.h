// Platform layer: window + GPU context, controller input, time, files, logs.
// Implemented by platform_ps4.cpp (Piglet, scePad) and platform_desktop.cpp
// (SDL2 + OpenGL ES 2). Everything above this layer is shared.
#pragma once

#ifndef APP_VERSION
#define APP_VERSION "dev" // set from the VERSION file by CMake / Makefile.ps4
#endif

#include <cstdint>
#include <string>

namespace platform {

// Logical buttons (bit flags), mapped from the DualShock 4 or keyboard/gamepad.
enum Button : uint32_t {
	BUTTON_UP = 1u << 0,
	BUTTON_DOWN = 1u << 1,
	BUTTON_LEFT = 1u << 2,
	BUTTON_RIGHT = 1u << 3,
	BUTTON_ACCEPT = 1u << 4, // Cross / Enter
	BUTTON_BACK = 1u << 5,   // Circle / Escape
	BUTTON_OPTIONS = 1u << 6,
	BUTTON_L1 = 1u << 7,     // previous tab (keyboard: Q / Page Up)
	BUTTON_R1 = 1u << 8,     // next tab (keyboard: E / Page Down)
	BUTTON_TRIANGLE = 1u << 9, // secondary action (keyboard: T)
	BUTTON_SQUARE = 1u << 10,  // menus such as Discover's filters (keyboard: F)
	BUTTON_L2 = 1u << 11, // previous add-ons tab (keyboard: Z)
	BUTTON_R2 = 1u << 12, // next add-ons tab (keyboard: C)
	DIRECTIONS = BUTTON_UP | BUTTON_DOWN | BUTTON_LEFT | BUTTON_RIGHT,
};

// Creates the window / display and a current OpenGL ES 2 context.
// The UI always draws in a logical `width` x `height` space.
bool init(int width, int height);

// Pumps events. Returns false when the app should quit (desktop only).
// `held` receives the buttons currently held down.
bool poll(uint32_t &held);

// Shows the frame that was just drawn.
void present();

// Seconds since an arbitrary start point.
double now();

// Full path of a bundled asset, e.g. asset_path("fonts/Inter-Regular.ttf").
std::string asset_path(const std::string &relative);

// Viewport (in pixels) the logical space is drawn into.
void viewport(int &x, int &y, int &w, int &h);

void log(const char *fmt, ...);

// Writable folder for the session and settings (ends with '/').
// PS4: /data/stremio_ps4/   Desktop: $XDG_DATA_HOME/stremio_ps4/ (~/.local/share)
std::string data_dir();

// Wall-clock time, seconds since the Unix epoch.
int64_t unix_time();

bool read_file(const std::string &path, std::string &out);
bool write_file(const std::string &path, const std::string &data);
bool remove_file(const std::string &path);

#if defined(STREMIO_PS4)
// Loads libSceGnmDriver (AvPlayer's H.264 decoder needs it). Once loaded, the
// system treats the app as a GNM app and won't suspend/close it until it
// calls sceGnmSubmitDone; present() does that while the system UI is up.
bool load_gnm_driver();
#endif

// Builds a shader program from one of Sony's precompiled shader names
// (e.g. "swordfish/vpBasic.vert" + "swordfish/fpSolidColor.frag").
// On desktop, equivalent GLSL is compiled instead. Returns 0 on failure.
unsigned int load_program(const char *vertex_name, const char *fragment_name);

// Desktop testing hook: true once the scripted test run is complete.
bool should_quit();

} // namespace platform
