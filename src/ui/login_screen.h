// Sign-in with a QR code / short link (no typing on a TV): stremio-core's
// link flow. Approving on another device signs the PS4 in.
#pragma once

#include "screen.h"

class LoginScreen : public Screen {
public:
	explicit LoginScreen(Fonts &fonts);
	~LoginScreen() override;
	void update(double dt, uint32_t pressed) override;
	void draw(Renderer &r) override;

private:
	Fonts &fonts_;
};
