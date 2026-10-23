// Design tokens (docs/DESIGN.md). The names follow daisyUI's semantic theme
// roles (base-100/200/300, primary, accent, ...); the values are the brand
// palette. Screens use these roles, never raw colours.
#pragma once

#include "../render/renderer.h"

namespace theme {

// Base: the backdrop and surfaces one elevation step above it.
// Updated dynamically when the user switches color schemes in Settings.
extern Color BASE_100;           // backdrop canvas
extern Color BASE_200;           // cards, list rows, keys, fields
extern Color BASE_300;           // containers on BASE_200, kbd caps
extern Color BASE_CONTENT;
extern Color BASE_CONTENT_MUTED;

extern Color PRIMARY;            // focus, active tab, progress
extern Color PRIMARY_CONTENT;
extern Color SECONDARY;          // focus on an active item
extern Color ACCENT;             // badges, "in use", toasts
extern Color ACCENT_CONTENT;
extern Color WARNING;
extern Color ERROR;

int color_scheme_count();
int color_scheme_index();
const char *color_scheme_name(int index);
void set_color_scheme(int index, bool save = true);

// Everything is 15% larger than the base sizes in docs/DESIGN.md by default
// (reads better on a TV), and can be scaled from 80% to 125% in Settings.
constexpr float DEFAULT_UI_SCALE = 1.15f;
constexpr int UI_SCALE_MIN_PCT = 80;
constexpr int UI_SCALE_MAX_PCT = 125;
constexpr int UI_SCALE_STEP_PCT = 5;

float ui_scale();
int ui_scale_percent();
void set_ui_scale_percent(int pct, bool save = true);
void load_ui_scale();

inline float S(float v) { return v * ui_scale(); }

struct ScaledFloat {
	float base;
	operator float() const { return base * ui_scale(); }
};

struct ScaledInt {
	int base;
	operator int() const { return (int)(base * ui_scale() + 0.5f); }
	float operator*(float k) const { return (int)(*this) * k; }
};

inline const ScaledFloat UI_SCALE{ 1.0f };

// Typography (1920x1080 canvas): docs/DESIGN.md sizes x ui_scale()
inline const ScaledInt SIZE_PAGE_HEADER{ 48 };    // Bold
inline const ScaledInt SIZE_SECTION_HEADER{ 32 }; // Semi-Bold
inline const ScaledInt SIZE_CARD_TITLE{ 24 };     // Medium
inline const ScaledInt SIZE_BODY{ 20 };           // Regular
inline const ScaledInt SIZE_SMALL{ 16 };          // Regular: kbd hints, details
inline const ScaledInt SIZE_SUBTITLE{ 42 };       // Semi-Bold: video subtitles

// Shape (daisyUI --radius-field / --radius-box / --radius-selector)
inline const ScaledFloat RADIUS_FIELD{ 12.0f };    // cards, buttons, keys, list rows
inline const ScaledFloat RADIUS_BOX{ 24.0f };      // large panels
inline const ScaledFloat RADIUS_SELECTOR{ 99.0f }; // pills: tabs, badges (clamped to half the height)
inline const ScaledFloat FOCUS_RING{ 4.0f };

// Focus motion
constexpr float FOCUS_SCALE = 1.1f;
constexpr float FOCUS_DURATION = 0.15f;
constexpr float ALPHA_UNFOCUSED = 0.6f;

} // namespace theme
