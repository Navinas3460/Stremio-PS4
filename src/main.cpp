// Stremio PS4: native PS4 client (OpenOrbis homebrew), also buildable on
// desktop with SDL2 for development. See README.md.

#include "core/stremio.h"
#include "net/http.h"
#include "net/images.h"
#include "platform/platform.h"
#include "render/font.h"
#include "render/renderer.h"
#include "ui/home_screen.h"
#include "ui/login_screen.h"
#include "ui/theme.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <vector>

namespace {

const int WIDTH = 1920, HEIGHT = 1080;
const int HTTP_WORKERS = 4;
const double REPEAT_DELAY = 0.4, REPEAT_INTERVAL = 0.12;

// Turns held buttons into "pressed this frame" events, with TV-style
// auto-repeat for the directions.
class InputRepeater {
public:
	uint32_t update(uint32_t held, double now) {
		uint32_t pressed = held & ~previous_;
		for (int bit = 0; bit < 32; bit++) {
			const uint32_t b = 1u << bit;
			if (!(platform::DIRECTIONS & b)) {
				continue;
			}
			if (pressed & b) {
				next_repeat_[bit] = now + REPEAT_DELAY;
			} else if ((held & b) && now >= next_repeat_[bit]) {
				pressed |= b;
				next_repeat_[bit] = now + REPEAT_INTERVAL;
			}
		}
		previous_ = held;
		return pressed;
	}

private:
	uint32_t previous_ = 0;
	double next_repeat_[32] = {};
};

bool load_fonts(Fonts &f) {
	auto font = [](const char *file) { return platform::asset_path(std::string("fonts/") + file); };
	return f.page_header.load(font("Inter-Bold.ttf"), theme::SIZE_PAGE_HEADER) &&
			f.section.load(font("Inter-SemiBold.ttf"), theme::SIZE_SECTION_HEADER) &&
			f.card_title.load(font("Inter-Medium.ttf"), theme::SIZE_CARD_TITLE) &&
			f.body.load(font("Inter-Regular.ttf"), theme::SIZE_BODY) &&
			f.small.load(font("Inter-Regular.ttf"), theme::SIZE_SMALL) &&
			f.subtitle.load(font("Inter-SemiBold.ttf"), theme::SIZE_SUBTITLE);
}

} // namespace

int main() {
	if (!platform::init(WIDTH, HEIGHT)) {
		platform::log("platform init failed");
		return 1;
	}
	Renderer renderer;
	static Fonts fonts; // large (glyph atlases); keep off the stack
	bool ok = renderer.init(WIDTH, HEIGHT);
	platform::log("renderer init: %d", ok);
	ok = ok && load_fonts(fonts);
	platform::log("fonts: %d", ok);
	ok = ok && http::init(HTTP_WORKERS);
	platform::log("http: %d", ok);
	if (!ok) {
		platform::log("startup failed");
		for (;;) { // keep the process alive on PS4 so the log can be read
			uint32_t held;
			if (!platform::poll(held) || platform::should_quit()) {
				return 1;
			}
			platform::present();
		}
	}

	Stremio &s = stremio();
	s.init();
#if defined(STREMIO_DEV)
	if (!s.logged_in() && getenv("STREMIO_PS4_DEMO")) {
		s.start_demo();
	}
#endif
	platform::log("Stremio PS4 %s", APP_VERSION);
	platform::log("ready");

	// Screen stack: sign-in or Home at the bottom (switched whenever the session
	// changes), with Details and Player pushed on top.
	struct Stack : Navigator {
		std::vector<std::unique_ptr<Screen>> screens;
		std::vector<std::unique_ptr<Screen>> to_push;
		int to_pop = 0;
		std::unique_ptr<Screen> new_root;
		void push(std::unique_ptr<Screen> screen) override { to_push.push_back(std::move(screen)); }
		void pop() override { to_pop++; }
		void replace_root(std::unique_ptr<Screen> screen) override { new_root = std::move(screen); }
		void apply() {
			if (new_root) {
				while (!screens.empty()) screens.pop_back();
				screens.push_back(std::move(new_root));
			}
			for (; to_pop > 0 && screens.size() > 1; to_pop--) screens.pop_back();
			to_pop = 0;
			for (auto &s : to_push) screens.push_back(std::move(s));
			to_push.clear();
		}
		void clear() {
			new_root.reset();
			to_push.clear();
			to_pop = 0;
			while (!screens.empty()) screens.pop_back(); // top first
		}
	} stack;
	int seen_auth_rev = -1;
	int frames = 0;
	InputRepeater input;
	double last = platform::now();
	uint32_t held = 0;
	while (platform::poll(held) && !platform::should_quit()) {
		const double now = platform::now();
		const double dt = std::min(now - last, 0.1);
		last = now;

		http::poll();
		images::update();
		s.update(dt);
		if (s.auth_rev != seen_auth_rev) {
			seen_auth_rev = s.auth_rev;
			stack.clear(); // destroy old screens first (stops link polling / playback)
			if (s.logged_in()) {
				stack.screens.push_back(std::unique_ptr<Screen>(new HomeScreen(fonts, stack)));
				s.refresh();
			} else {
				stack.screens.push_back(std::unique_ptr<Screen>(new LoginScreen(fonts)));
			}
		}

		Screen &top = *stack.screens.back();
		top.update(dt, input.update(held, now));
		renderer.begin_frame(theme::BASE_100);
		top.draw(renderer);
		renderer.end_frame();
		platform::present();
		stack.apply();
		if (++frames == 1) platform::log("first frame drawn");
	}
#if !defined(STREMIO_PS4)
	std::quick_exit(0);
#endif
	return 0;
}
