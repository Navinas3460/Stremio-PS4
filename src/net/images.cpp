#include "images.h"

#include "../platform/platform.h"
#include "http.h"

#include "stb_image.h"

#include <cstring>
#include <deque>
#include <unordered_map>

namespace images {
namespace {

const int MAX_WIDTH = 640;          // artwork is shown at card size
const size_t MAX_TEXTURES = 160;    // ~160 x 0.9 MB of VRAM at most
const int UPLOADS_PER_FRAME = 2;    // avoid frame hitches

struct Decoded {
	int width = 0, height = 0;
	std::vector<uint8_t> rgba;
};

struct Entry {
	State state = State::LOADING;
	Texture texture;
	uint64_t last_used = 0;
};

std::unordered_map<std::string, Entry> g_entries;
std::deque<std::pair<std::string, std::shared_ptr<Decoded>>> g_pending;
uint64_t g_frame = 0;
const Texture INVALID;

// Halves the image (2x2 box filter) until it is at most MAX_WIDTH wide.
void shrink(Decoded &img) {
	while (img.width > MAX_WIDTH && img.width >= 2 && img.height >= 2) {
		const int w = img.width / 2, h = img.height / 2;
		std::vector<uint8_t> out((size_t)w * h * 4);
		for (int y = 0; y < h; y++) {
			const uint8_t *r0 = &img.rgba[(size_t)(2 * y) * img.width * 4];
			const uint8_t *r1 = &img.rgba[(size_t)(2 * y + 1) * img.width * 4];
			uint8_t *o = &out[(size_t)y * w * 4];
			for (int x = 0; x < w; x++) {
				for (int c = 0; c < 4; c++) {
					o[x * 4 + c] = (uint8_t)((r0[x * 8 + c] + r0[x * 8 + 4 + c] + r1[x * 8 + c] + r1[x * 8 + 4 + c] + 2) / 4);
				}
			}
		}
		img.width = w;
		img.height = h;
		img.rgba.swap(out);
	}
}

void start(const std::string &url) {
	g_entries[url].last_used = g_frame;
	http::get(
			url,
			[url](http::Response &res) {
				auto decoded = std::static_pointer_cast<Decoded>(res.payload);
				if (!res.ok() || !decoded) {
					g_entries[url].state = State::FAILED;
					return;
				}
				g_pending.emplace_back(url, decoded);
			},
			[](http::Response &res) { // worker thread: decode + shrink
				if (!res.ok()) {
					return;
				}
				int w = 0, h = 0, n = 0;
				uint8_t *px = stbi_load_from_memory((const stbi_uc *)res.body.data(), (int)res.body.size(), &w, &h, &n, 4);
				if (!px) {
					res.error = "image decode failed";
					return;
				}
				auto img = std::make_shared<Decoded>();
				img->width = w;
				img->height = h;
				img->rgba.assign(px, px + (size_t)w * h * 4);
				stbi_image_free(px);
				shrink(*img);
				res.body.clear();
				res.payload = img;
			});
}

} // namespace

const Texture &get(const std::string &url, State *state) {
	if (url.empty()) {
		if (state) *state = State::FAILED;
		return INVALID;
	}
	auto it = g_entries.find(url);
	if (it == g_entries.end()) {
		start(url);
		if (state) *state = State::LOADING;
		return INVALID;
	}
	it->second.last_used = g_frame;
	if (state) *state = it->second.state;
	return it->second.state == State::READY ? it->second.texture : INVALID;
}

const Texture &first(const std::vector<std::string> &urls) {
	for (const std::string &url : urls) {
		State state;
		const Texture &t = get(url, &state);
		if (state == State::READY) return t;
		if (state == State::LOADING) return INVALID; // wait before trying fallbacks
	}
	return INVALID;
}

void update() {
	g_frame++;
	for (int i = 0; i < UPLOADS_PER_FRAME && !g_pending.empty(); i++) {
		auto job = g_pending.front();
		g_pending.pop_front();
		Entry &e = g_entries[job.first];
		e.texture = make_texture(job.second->width, job.second->height, job.second->rgba.data());
		e.state = State::READY;
	}
	// Evict the least recently used textures beyond the budget.
	size_t ready = 0;
	for (auto &kv : g_entries) {
		ready += kv.second.state == State::READY;
	}
	while (ready > MAX_TEXTURES) {
		auto oldest = g_entries.end();
		for (auto it = g_entries.begin(); it != g_entries.end(); ++it) {
			if (it->second.state == State::READY && (oldest == g_entries.end() || it->second.last_used < oldest->second.last_used)) {
				oldest = it;
			}
		}
		if (oldest == g_entries.end() || oldest->second.last_used + 2 > g_frame) {
			break; // everything is in use right now
		}
		glDeleteTextures(1, &oldest->second.texture.id);
		g_entries.erase(oldest);
		ready--;
	}
}

} // namespace images
