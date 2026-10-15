// FreeType text: glyphs are rasterised on demand into a 1024x1024 atlas
// texture (white, alpha = coverage) and drawn with the fpAlphaColor shader.
#pragma once

#include "renderer.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#if defined(STREMIO_PS4)
#include <proto-include.h> // OpenOrbis: prototypes for Sony's FreeType (libSceFreeType)
#endif

#include <string>
#include <unordered_map>
#include <vector>

class Font {
public:
	// One Font per (file, pixel size).
	bool load(const std::string &path, int pixel_size);

	float draw(Renderer &r, const std::string &utf8, float x, float baseline, const Color &c);
	float measure(const std::string &utf8);
	// Shortens `utf8` with "…" so it fits in `max_width`.
	std::string ellipsize(const std::string &utf8, float max_width);
	// Word-wraps `utf8` (each '\n' starts a new line) to `max_width`. With
	// `max_lines` > 0, the last line kept ends in "…" if text was cut.
	std::vector<std::string> wrap(const std::string &utf8, float max_width, int max_lines = 0);

	int size() const { return size_; }

private:
	struct Glyph {
		float u0, v0, u1, v1;
		int width, height, left, top;
		float advance;
		bool valid;
	};
	const Glyph &glyph(uint32_t codepoint);
	void upload_pending();

	static FT_Library library_;
	FT_Face face_ = nullptr;
	int size_ = 0;
	std::unordered_map<uint32_t, Glyph> glyphs_;
	Texture atlas_;
	std::vector<uint8_t> pixels_; // CPU copy of the atlas (RGBA)
	int pen_x_ = 1, pen_y_ = 1, row_height_ = 0;
	bool dirty_ = false;
};

// Decodes the next UTF-8 code point from `s` at `i` (advancing `i`).
uint32_t next_codepoint(const std::string &s, size_t &i);
