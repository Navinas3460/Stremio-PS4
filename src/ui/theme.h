// Design tokens (docs/DESIGN.md). The names follow daisyUI's semantic theme
// roles (base-100/200/300, primary, accent, ...); the values are the brand
// palette. Screens use these roles, never raw colours.
#pragma once

#include "../render/renderer.h"

namespace theme {

// Base: the backdrop and surfaces one elevation step above it.
inline const Color BASE_100 = rgb(0x051923); // Ink Black: backdrop canvas
inline const Color BASE_200 = rgb(0x0C2A38); // cards, list rows, keys, fields
inline const Color BASE_300 = rgb(0x163D50); // containers on BASE_200, kbd caps
inline const Color BASE_CONTENT = rgb(0xEEF1F4);
inline const Color BASE_CONTENT_MUTED = rgb(0xEEF1F4, 0.6f);

inline const Color PRIMARY = rgb(0x0070D1);   // Sapphire Sky: focus, active tab, progress
inline const Color PRIMARY_CONTENT = rgb(0xFFFFFF);
inline const Color SECONDARY = rgb(0x685CEE); // Majorelle Blue: focus on an active item
inline const Color ACCENT = rgb(0x84DD63);    // Radioactive Grass: badges, "in use", toasts
inline const Color ACCENT_CONTENT = BASE_100;
inline const Color WARNING = rgb(0xF2C14E);
inline const Color ERROR = rgb(0xFF8A80);

// Everything is drawn at the base sizes in docs/DESIGN.md x the UI scale:
// 15% larger by default (reads better on a TV), times the percentage chosen
// in Settings. It changes live, so sizes are computed when used.
constexpr float BASE_SCALE = 1.15f;
constexpr int UI_SCALE_MIN = 80, UI_SCALE_MAX = 130, UI_SCALE_STEP = 5; // percent of the default
inline float ui_scale_value = BASE_SCALE; // set by set_ui_scale()
inline float S(float v) { return v * ui_scale_value; }
int ui_scale_percent();
// Saved in <data dir>/ui_scale.txt; load_ui_scale() reads it at start-up.
void set_ui_scale(int percent);
void load_ui_scale();

// Typography (1920x1080 canvas): docs/DESIGN.md sizes x the UI scale
inline int size_page_header() { return (int)S(48); }    // Bold
inline int size_section_header() { return (int)S(32); } // Semi-Bold
inline int size_card_title() { return (int)S(24); }     // Medium
inline int size_body() { return (int)S(20); }           // Regular
inline int size_small() { return (int)S(16); }          // Regular: kbd hints, details
// Semi-Bold: video subtitles, which keep their size at any UI scale.
constexpr int SIZE_SUBTITLE = (int)(42 * BASE_SCALE);

// Shape (daisyUI --radius-field / --radius-box / --radius-selector)
inline float radius_field() { return S(12); }    // cards, buttons, keys, list rows
inline float radius_box() { return S(24); }      // large panels
inline float radius_selector() { return S(99); } // pills: tabs, badges (clamped to half the height)
inline float focus_ring() { return S(4); }

// Focus motion
constexpr float FOCUS_SCALE = 1.1f;
constexpr float FOCUS_DURATION = 0.15f;
constexpr float ALPHA_UNFOCUSED = 0.6f;

} // namespace theme
