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
inline const Color ERROR = rgb(0xFF8A80);

// Everything is 15% larger than the base sizes in docs/DESIGN.md (reads better
// on a TV).
constexpr float UI_SCALE = 1.15f;
constexpr float S(float v) { return v * UI_SCALE; }

// Typography (1920x1080 canvas): docs/DESIGN.md sizes x UI_SCALE
constexpr int SIZE_PAGE_HEADER = (int)(48 * UI_SCALE);    // Bold
constexpr int SIZE_SECTION_HEADER = (int)(32 * UI_SCALE); // Semi-Bold
constexpr int SIZE_CARD_TITLE = (int)(24 * UI_SCALE);     // Medium
constexpr int SIZE_BODY = (int)(20 * UI_SCALE);           // Regular
constexpr int SIZE_SMALL = (int)(16 * UI_SCALE);          // Regular: kbd hints, details
constexpr int SIZE_SUBTITLE = (int)(42 * UI_SCALE);       // Semi-Bold: video subtitles

// Shape (daisyUI --radius-field / --radius-box / --radius-selector)
constexpr float RADIUS_FIELD = S(12);    // cards, buttons, keys, list rows
constexpr float RADIUS_BOX = S(24);      // large panels
constexpr float RADIUS_SELECTOR = S(99); // pills: tabs, badges (clamped to half the height)
constexpr float FOCUS_RING = S(4);

// Focus motion
constexpr float FOCUS_SCALE = 1.1f;
constexpr float FOCUS_DURATION = 0.15f;
constexpr float ALPHA_UNFOCUSED = 0.6f;

} // namespace theme
