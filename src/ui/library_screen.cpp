#include "library_screen.h"

#include "theme.h"

using theme::S;

void LibraryScreen::update(double dt, uint32_t pressed) {
	if (bar_.handle_shoulders(pressed, fonts_, navigator_)) return;
	if (bar_.handle_focused(pressed, fonts_, navigator_)) return;
	bar_.update(dt, true); // the bar is the only thing to focus
}

void LibraryScreen::draw(Renderer &r) {
	r.text(fonts_.page_header, "Library", 120, S(96), theme::TEXT);
	r.text(fonts_.body, "Your full library is coming soon. Continue watching is on Home.", 120, S(190), theme::TEXT_MUTED);
	bar_.draw(r, fonts_);
}
