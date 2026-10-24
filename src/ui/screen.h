// Shared UI types: fonts and the screen interface.
#pragma once

#include "../render/font.h"
#include "../render/renderer.h"

#include <cstdint>
#include <memory>

struct Fonts {
	Font page_header; // bold
	Font section;     // semibold
	Font card_title;  // medium
	Font body;        // regular
	Font small;       // regular (controller hints, details)
	Font subtitle;    // semibold, large (video subtitles)

	void apply_scale();
};

#include "theme.h"

class Screen {
public:
	virtual ~Screen() = default;
	// `pressed` = platform::Button flags pressed this frame (with auto-repeat).
	virtual void update(double dt, uint32_t pressed) = 0;
	virtual void draw(Renderer &r) = 0;
	virtual Color backdrop() const { return theme::BASE_100; }
};

// Screen stack: Home -> Details -> Player. Changes are applied after the
// current frame, so a screen may pop itself from its own update().
class Navigator {
public:
	virtual ~Navigator() = default;
	virtual void push(std::unique_ptr<Screen> screen) = 0;
	virtual void pop() = 0;
	// Replaces the bottom screen (switching tabs) and drops everything above.
	virtual void replace_root(std::unique_ptr<Screen> screen) = 0;
};
