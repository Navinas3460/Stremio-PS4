#include "font.h"

#include "../platform/platform.h"

#include <cstring>

namespace {
const int ATLAS = 1024;
}

FT_Library Font::library_ = nullptr;

uint32_t next_codepoint(const std::string &s, size_t &i) {
	const unsigned char c = (unsigned char)s[i++];
	if (c < 0x80) {
		return c;
	}
	int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
	uint32_t cp = c & (0x3F >> extra);
	while (extra-- > 0 && i < s.size()) {
		cp = (cp << 6) | ((unsigned char)s[i++] & 0x3F);
	}
	return cp;
}

bool Font::load(const std::string &path, int pixel_size) {
	if (!library_ && FT_Init_FreeType(&library_) != 0) {
		platform::log("FT_Init_FreeType failed");
		return false;
	}
	if (FT_New_Face(library_, path.c_str(), 0, &face_) != 0) {
		platform::log("font %s failed to load", path.c_str());
		return false;
	}
	FT_Set_Pixel_Sizes(face_, 0, pixel_size);
	size_ = pixel_size;
	ascent_ = face_->size->metrics.ascender / 64.0f;
	line_height_ = face_->size->metrics.height / 64.0f;
	pixels_.assign((size_t)ATLAS * ATLAS * 4, 0);
	for (size_t i = 0; i < pixels_.size(); i += 4) {
		pixels_[i] = pixels_[i + 1] = pixels_[i + 2] = 255;
	}
	atlas_ = make_texture(ATLAS, ATLAS, pixels_.data());
	return true;
}

const Font::Glyph &Font::glyph(uint32_t codepoint) {
	auto it = glyphs_.find(codepoint);
	if (it != glyphs_.end()) {
		return it->second;
	}
	Glyph g{};
	if (FT_Load_Char(face_, codepoint, FT_LOAD_RENDER) == 0) {
		const FT_GlyphSlot slot = face_->glyph;
		const FT_Bitmap &bm = slot->bitmap;
		g.width = (int)bm.width;
		g.height = (int)bm.rows;
		g.left = slot->bitmap_left;
		g.top = slot->bitmap_top;
		g.advance = slot->advance.x / 64.0f;
		if (pen_x_ + g.width + 1 >= ATLAS) { // next shelf
			pen_x_ = 1;
			pen_y_ += row_height_ + 1;
			row_height_ = 0;
		}
		if (pen_y_ + g.height + 1 < ATLAS) {
			for (int y = 0; y < g.height; y++) {
				for (int x = 0; x < g.width; x++) {
					pixels_[(((size_t)pen_y_ + y) * ATLAS + pen_x_ + x) * 4 + 3] = bm.buffer[y * bm.pitch + x];
				}
			}
			g.u0 = pen_x_ / (float)ATLAS;
			g.v0 = pen_y_ / (float)ATLAS;
			g.u1 = (pen_x_ + g.width) / (float)ATLAS;
			g.v1 = (pen_y_ + g.height) / (float)ATLAS;
			g.valid = true;
			pen_x_ += g.width + 1;
			row_height_ = row_height_ > g.height ? row_height_ : g.height;
			dirty_ = true;
		}
	}
	return glyphs_.emplace(codepoint, g).first->second;
}

void Font::upload_pending() {
	if (!dirty_) {
		return;
	}
	// Re-upload the used part of the atlas (glyphs are added rarely).
	const int rows = pen_y_ + row_height_ + 1 < ATLAS ? pen_y_ + row_height_ + 1 : ATLAS;
	glBindTexture(GL_TEXTURE_2D, atlas_.id);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ATLAS, rows, GL_RGBA, GL_UNSIGNED_BYTE, pixels_.data());
	dirty_ = false;
}

float Font::measure(const std::string &utf8) {
	float w = 0;
	for (size_t i = 0; i < utf8.size();) {
		w += glyph(next_codepoint(utf8, i)).advance;
	}
	return w;
}

std::string Font::ellipsize(const std::string &utf8, float max_width) {
	if (measure(utf8) <= max_width) {
		return utf8;
	}
	const std::string dots = "\xE2\x80\xA6"; // …
	const float limit = max_width - measure(dots);
	float w = 0;
	size_t i = 0, cut = 0;
	while (i < utf8.size()) {
		const size_t start = i;
		const float adv = glyph(next_codepoint(utf8, i)).advance;
		if (w + adv > limit) {
			break;
		}
		w += adv;
		cut = i;
		(void)start;
	}
	return utf8.substr(0, cut) + dots;
}

float Font::draw(Renderer &r, const std::string &utf8, float x, float baseline, const Color &c) {
	// Make sure every glyph is in the atlas before drawing any of them.
	for (size_t i = 0; i < utf8.size();) {
		glyph(next_codepoint(utf8, i));
	}
	upload_pending();
	float pen = x;
	for (size_t i = 0; i < utf8.size();) {
		const Glyph &g = glyph(next_codepoint(utf8, i));
		if (g.valid && g.width > 0) {
			r.mask_quad(atlas_.id, pen + g.left, baseline - g.top, (float)g.width, (float)g.height, g.u0, g.v0, g.u1, g.v1, c);
		}
		pen += g.advance;
	}
	return pen - x;
}
