// 2D renderer built on Sony's precompiled "swordfish" UI shaders (the only
// shaders a PS4 homebrew app can use; there is no runtime GLSL compiler).
//
// Only three programs are needed (all verified on FW 11.02):
//   vpBasic + fpSolidColor   rectangles               (uniform fillColor)
//   vpBasic + fpAlphaTexture images                   (uniforms texture, alpha)
//   vpBasic + fpAlphaColor   text, rounded corners    (texture alpha x fillColor)
// Both matrices stay identity and vertices are transformed on the CPU into
// clip space: the one configuration verified on hardware. Quads sharing a
// program, texture and colour are batched into a single draw call.
#pragma once

#include "gl.h"

#include <cstdint>
#include <string>
#include <map>
#include <vector>

struct Color {
	float r = 0, g = 0, b = 0, a = 1;
	Color with_alpha(float alpha) const { return Color{ r, g, b, a * alpha }; }
};

// 0xRRGGBB -> Color
Color rgb(uint32_t hex, float alpha = 1.0f);
Color mix(const Color &a, const Color &b, float t);

struct Texture {
	GLuint id = 0;
	int width = 0;
	int height = 0;
	// Columns of the picture when the texture is wider (padded video frames);
	// 0 = all of it.
	int shown_width = 0;
	bool valid() const { return id != 0; }
	int picture_width() const { return shown_width > 0 ? shown_width : width; }
};

Texture make_texture(int width, int height, const uint8_t *rgba);

class Font;

class Renderer {
public:
	bool init(int logical_width, int logical_height);
	// `clear` also becomes the backdrop translucent text is blended toward.
	void begin_frame(const Color &clear);
	void end_frame();

	void rect(float x, float y, float w, float h, const Color &c);
	void rounded_rect(float x, float y, float w, float h, float radius, const Color &c);
	// Image scaled to cover the box (cropped, like "keep aspect covered").
	void image_cover(const Texture &t, float x, float y, float w, float h, float alpha = 1.0f);
	// Rounds an image's corners by drawing `background`-coloured caps over
	// them (works on solid backgrounds, which is all the UI uses).
	void round_corners(float x, float y, float w, float h, float radius, const Color &background);
	// Vertical fade of `c` from alpha_top to alpha_bottom (e.g. card shading).
	void vertical_fade(float x, float y, float w, float h, const Color &c, float alpha_top, float alpha_bottom);
	// Text with its baseline at y. Returns the advance width.
	float text(Font &font, const std::string &utf8, float x, float baseline, const Color &c);

	// Transform applied to everything drawn until pop(): uniform scale about
	// a point (focus animation), plus a global opacity multiplier.
	void push(float cx, float cy, float scale, float opacity);
	void pop();

	int draw_calls() const { return last_draw_calls_; }

	// Draws what's queued. Font calls it before changing its glyph atlas.
	void flush();

	// Used by Font to draw glyph quads (texture alpha x colour).
	void mask_quad(GLuint texture, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
			const Color &c);

private:
	struct Vertex {
		float x, y, z, w; // aPosition (clip space)
		float u, v;       // aTextureCoord
	};
	struct Program {
		GLuint id = 0;
		GLint m_matrix = -1, vp_matrix = -1, fill_color = -1, texture = -1, alpha = -1;
	};
	enum Kind { SOLID, IMAGE, MASK };
	struct Transform {
		float cx, cy, scale, opacity;
	};

	bool load(Program &p, const char *fragment);
	void quad(Kind kind, GLuint texture, const Color &c, float x, float y, float w, float h, float u0, float v0,
			float u1, float v1);
	// Starts a new batch unless the current one matches and has room.
	void batch(Kind kind, GLuint texture, const Color &c, size_t vertices);
	void transform_point(float &x, float &y) const;
	void triangle(float x0, float y0, float x1, float y1, float x2, float y2, const Color &c);
	void corner_fan(float cx, float cy, float r, int corner, bool inside, const Color &c);
	const Texture &ramp(const Color &c);

	Program programs_[3];
	int width_ = 1920, height_ = 1080;
	std::vector<Vertex> batch_;
	Kind batch_kind_ = SOLID;
	GLuint batch_texture_ = 0;
	Color batch_color_;
	std::vector<Transform> transforms_;
	Color backdrop_;
	std::map<uint32_t, Texture> ramps_; // fade textures by colour
	int draw_calls_ = 0;
	int last_draw_calls_ = 0;
};
