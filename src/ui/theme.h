// Design tokens: brand colours, type scale, radii and focus animation.
#pragma once

#include "../render/renderer.h"

namespace theme {

inline const Color BRAND_PRIMARY = rgb(0x0070D1);   // Sapphire Sky: accent + active states
inline const Color BRAND_SECONDARY = rgb(0x685CEE); // Majorelle Blue: sub-menus / alt active
inline const Color BRAND_TERTIARY = rgb(0x051923);  // Ink Black: backdrop canvas
inline const Color SURFACE_CARD = rgb(0x6BAA75);    // Sage Green: container outlines
inline const Color STATUS_ACCENT = rgb(0x84DD63);   // Radioactive Grass: notifications

inline const Color TEXT = rgb(0xEEF1F4);
inline const Color TEXT_MUTED = rgb(0xEEF1F4, 0.6f);
inline const Color SURFACE = rgb(0x0C2A38);
inline const Color SECTION_TITLE = mix(BRAND_PRIMARY, rgb(0xFFFFFF), 0.2f);

// Everything is 15% larger than the base sizes in docs/DESIGN.md (reads better
// on a TV).
constexpr float UI_SCALE = 1.15f;
constexpr float S(float v) { return v * UI_SCALE; }

// Typography (1920x1080 canvas): docs/DESIGN.md sizes x UI_SCALE
constexpr int SIZE_PAGE_HEADER = (int)(48 * UI_SCALE);    // Bold
constexpr int SIZE_SECTION_HEADER = (int)(32 * UI_SCALE); // Semi-Bold
constexpr int SIZE_CARD_TITLE = (int)(24 * UI_SCALE);     // Medium
constexpr int SIZE_BODY = (int)(20 * UI_SCALE);           // Regular
constexpr int SIZE_SMALL = (int)(16 * UI_SCALE);          // debug overlay
constexpr int SIZE_SUBTITLE = (int)(42 * UI_SCALE);       // video subtitles

// Shape / motion
constexpr float RADIUS_CARD = S(12);
constexpr float RADIUS_LAYOUT = S(24);
constexpr float RADIUS_PILL = S(44);
constexpr float FOCUS_SCALE = 1.1f;
constexpr float FOCUS_DURATION = 0.15f;
constexpr float ALPHA_FOCUSED = 1.0f;
constexpr float ALPHA_UNFOCUSED = 0.6f;

} // namespace theme
