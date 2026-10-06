// A title card (landscape artwork, title, subtitle, progress, badge) as used
// in the Home and Search rows, and opening its details.
#pragma once

#include "../core/models.h"
#include "screen.h"

#include <string>
#include <vector>

struct Card {
	std::string title, subtitle, badge;
	float progress = 0;              // 0..1, 0 = no bar
	std::vector<std::string> images; // artwork URLs, best first
	std::string meta_id, type;       // what the card opens
	std::string poster, release_info;
	float focus = 0;                 // animated 0..1

	static Card from_meta(const MetaItem &m);
};

// Draws `c` at (x, y), w x h, scaled up and ringed by its focus.
void draw_card(Renderer &r, Fonts &fonts, const Card &c, float x, float y, float w, float h);

// Pushes the details screen for `c`.
void open_details(const Card &c, Fonts &fonts, Navigator &nav);
