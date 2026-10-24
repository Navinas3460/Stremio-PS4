#include "login_screen.h"

#include "../core/stremio.h"
#include "../net/images.h"
#include "theme.h"

#include <cstring>

using theme::S;

LoginScreen::LoginScreen(Fonts &fonts) : fonts_(fonts) {
	stremio().start_link_login();
}

LoginScreen::~LoginScreen() {
	stremio().stop_link_login();
}

void LoginScreen::update(double, uint32_t) {}

void LoginScreen::draw(Renderer &r) {
	const LinkLogin &link = stremio().link();
	const float W = 1920, H = 1080;

	// QR code on a white panel (QR codes need a light quiet zone). Drawn at
	// its native pixel size so the modules stay crisp.
	const float qr = 396, pad = S(24);
	const float panel = qr + 2 * pad;
	const float panel_x = W - 240 - panel, panel_y = (H - panel) / 2;
	r.rounded_rect(panel_x, panel_y, panel, panel, theme::RADIUS_BOX, rgb(0xFFFFFF));
	const Texture &code = images::get(link.qr_url);
	if (code.valid()) {
		r.image_cover(code, panel_x + pad, panel_y + pad, qr, qr);
	} else {
		const std::string waiting = "Loading QR code\xE2\x80\xA6";
		r.text(fonts_.body, waiting, panel_x + (panel - fonts_.body.measure(waiting)) / 2, panel_y + panel / 2,
				theme::BASE_100);
	}

	// Instructions on the left.
	const float x = 240;
	float y = panel_y + S(60);
	r.text(fonts_.page_header, "Stremio", x, y, theme::BASE_CONTENT);
	y += S(70);
	r.text(fonts_.section, "Sign in with your phone", x, y, theme::BASE_CONTENT);
	y += S(56);
	r.text(fonts_.body, "Scan the QR code, or open this link on any device:", x, y, theme::BASE_CONTENT_MUTED);
	y += S(64);
	std::string shown = link.link;
	for (const char *scheme : { "https://", "http://" }) {
		if (shown.rfind(scheme, 0) == 0) shown = shown.substr(strlen(scheme));
	}
	r.text(fonts_.page_header, shown.empty() ? "\xE2\x80\xA6" : shown, x, y, theme::BASE_CONTENT);
	y += S(64);
	r.text(fonts_.body, link.status, x, y, link.error ? theme::ERROR : theme::BASE_CONTENT_MUTED);
}
