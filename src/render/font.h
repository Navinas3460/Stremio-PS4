// FreeType text: glyphs are rasterised on demand into a 1024x1024 atlas
// texture (white, alpha = coverage) and drawn with the fpAlphaColor shader.
//
// Characters the font lacks come from fallback fonts (Noto: Arabic, Hebrew,
// Indic, Thai, CJK...). Text that needs them is shaped with HarfBuzz and
// put in display order (bidi.h); text the primary font covers takes the
// fast path, one glyph per character. When the atlas fills up (thousands of
// CJK glyphs over a film), it's cleared and refilled with what's on screen.
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

struct hb_font_t;

class Font {
public:
	// Font files tried in order for characters a font lacks; set once,
	// before any text is drawn. Missing files are skipped.
	static void set_fallbacks(const std::vector<std::string> &paths);

	// One Font per (file, pixel size).
	bool load(const std::string &path, int pixel_size);

	float draw(Renderer &r, const std::string &utf8, float x, float baseline, const Color &c);
	float measure(const std::string &utf8);
	// Shortens `utf8` with "…" so it fits in `max_width`.
	std::string ellipsize(const std::string &utf8, float max_width);
	// Word-wraps `utf8` (each '\n' starts a new line) to `max_width`. Chinese
	// and Japanese, which have no spaces, may break between any two
	// characters. With `max_lines` > 0, the last line kept ends in "…" if
	// text was cut.
	std::vector<std::string> wrap(const std::string &utf8, float max_width, int max_lines = 0);

	int size() const { return size_; }

private:
	struct Glyph {
		float u0, v0, u1, v1;
		int width, height, left, top;
		float advance;
		bool valid;
	};
	// The primary font or a fallback, opened at this size on first use.
	struct Face {
		std::string path;
		FT_Face ft = nullptr;
		hb_font_t *hb = nullptr;
		bool opened = false;
	};
	// Shaped text: glyphs (atlas keys) at offsets from the pen start.
	struct Placed {
		uint32_t key;
		float x, y;
	};
	struct Layout {
		std::vector<Placed> glyphs;
		float width = 0;
	};
	struct Wrapped {
		std::string text;
		float width;
		int max_lines;
		std::vector<std::string> lines;
	};

	const Glyph &glyph(uint32_t codepoint); // fast path: primary font, by character
	const Glyph &glyph_key(uint32_t key);   // a shaped glyph (face and glyph index)
	const Glyph &rasterise(uint32_t key, FT_Face face, FT_UInt index);
	FT_Face face(int index);
	hb_font_t *hb_font(int index);
	int face_for(uint32_t codepoint); // the first face that has it, or -1
	static int fallback_for(uint32_t codepoint);
	bool simple(const std::string &utf8);
	const Layout &layout(const std::string &utf8);
	void shape(const std::string &utf8, Layout &out);
	std::vector<std::string> wrap_lines(const std::string &utf8, float max_width, int max_lines);
	void upload_pending(Renderer &r);

	static FT_Library library_;
	static std::vector<std::string> fallbacks_;
	int size_ = 0;
	std::vector<Face> faces_; // [0] is the primary font
	std::unordered_map<uint32_t, int> face_of_;
	std::unordered_map<uint32_t, Glyph> glyphs_;
	std::unordered_map<std::string, Layout> layouts_;
	std::vector<Wrapped> wrapped_; // recent wrap() results (it runs every frame)
	Texture atlas_;
	std::vector<uint8_t> pixels_; // CPU copy of the atlas (RGBA)
	int pen_x_ = 1, pen_y_ = 1, row_height_ = 0;
	bool dirty_ = false, cleared_ = false;
	unsigned generation_ = 0; // bumped when the atlas is cleared
};

// Decodes the next UTF-8 code point from `s` at `i` (advancing `i`).
uint32_t next_codepoint(const std::string &s, size_t &i);
