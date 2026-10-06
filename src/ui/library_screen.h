// Library tab (placeholder until the library view is ported).
#pragma once

#include "screen.h"
#include "tab_bar.h"

class LibraryScreen : public Screen {
public:
	LibraryScreen(Fonts &fonts, Navigator &nav) : fonts_(fonts), navigator_(nav) {}
	void update(double dt, uint32_t pressed) override;
	void draw(Renderer &r) override;

private:
	Fonts &fonts_;
	Navigator &navigator_;
	tabs::TabBar bar_{ tabs::LIBRARY };
};
