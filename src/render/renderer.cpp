#include "renderer.h"

#include "../platform/platform.h"
#include "font.h"

#include <algorithm>
#include <cmath>
#include <cstring>

Color rgb(uint32_t hex, float alpha) {
	return Color{ ((hex >> 16) & 0xFF) / 255.0f, ((hex >> 8) & 0xFF) / 255.0f, (hex & 0xFF) / 255.0f, alpha };
}

Color mix(const Color &a, const Color &b, float t) {
	return Color{ a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t };
}

Texture make_texture(int width, int height, const uint8_t *rgba) {
	Texture t;
	t.width = width;
	t.height = height;
	glGenTextures(1, &t.id);
	glBindTexture(GL_TEXTURE_2D, t.id);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	return t;
}

// --- setup ----------------------------------------------------------------

bool Renderer::load(Program &p, const char *fragment) {
	p.id = platform::load_program("swordfish/vpBasic.vert", fragment);
	if (!p.id) {
		return false;
	}
	p.m_matrix = glGetUniformLocation(p.id, "uMMatrix");
	p.vp_matrix = glGetUniformLocation(p.id, "uVPMatrix");
	p.fill_color = glGetUniformLocation(p.id, "fillColor");
	p.texture = glGetUniformLocation(p.id, "texture");
	p.alpha = glGetUniformLocation(p.id, "alpha");
	static const float identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	glUseProgram(p.id);
	glUniformMatrix4fv(p.m_matrix, 1, GL_FALSE, identity);
	glUniformMatrix4fv(p.vp_matrix, 1, GL_FALSE, identity);
	if (p.texture >= 0) {
		glUniform1i(p.texture, 0);
	}
	return true;
}

bool Renderer::init(int logical_width, int logical_height) {
	width_ = logical_width;
	height_ = logical_height;
	if (!load(programs_[SOLID], "swordfish/fpSolidColor.frag") || !load(programs_[IMAGE], "swordfish/fpAlphaTexture.frag") ||
			!load(programs_[MASK], "swordfish/fpAlphaColor.frag")) {
		platform::log("renderer: shader programs failed to load");
		return false;
	}

	transforms_.push_back(Transform{ 0, 0, 1, 1 });
	batch_.reserve(6 * 2048);
	return true;
}

void Renderer::begin_frame(const Color &clear) {
	backdrop_ = clear;
	int vx, vy, vw, vh;
	platform::viewport(vx, vy, vw, vh);
	glViewport(vx, vy, vw, vh);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT); // letterbox bars (desktop)
	draw_calls_ = 0;
	rect(0, 0, (float)width_, (float)height_, clear);
}

void Renderer::end_frame() {
	flush();
	last_draw_calls_ = draw_calls_;
}

// --- transforms -------------------------------------------------------------

void Renderer::push(float cx, float cy, float scale, float opacity) {
	// Nested transforms compose: transform_point applies the innermost first.
	transforms_.push_back(Transform{ cx, cy, scale, transforms_.back().opacity * opacity });
}

void Renderer::pop() {
	if (transforms_.size() > 1) {
		transforms_.pop_back();
	}
}

// --- batching ---------------------------------------------------------------

void Renderer::flush() {
	if (batch_.empty()) {
		return;
	}
	const Program &p = programs_[batch_kind_];
	glUseProgram(p.id);
	if (batch_kind_ != SOLID) {
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, batch_texture_);
	}
	if (p.fill_color >= 0) {
		glUniform4f(p.fill_color, batch_color_.r, batch_color_.g, batch_color_.b, batch_color_.a);
	}
	if (p.alpha >= 0) {
		glUniform1f(p.alpha, batch_color_.a);
	}
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), &batch_[0].x);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), &batch_[0].u);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glDrawArrays(GL_TRIANGLES, 0, (GLsizei)batch_.size());
	batch_.clear();
	draw_calls_++;
}

void Renderer::transform_point(float &x, float &y) const {
	for (size_t i = transforms_.size(); i-- > 0;) {
		const Transform &t = transforms_[i];
		x = t.cx + (x - t.cx) * t.scale;
		y = t.cy + (y - t.cy) * t.scale;
	}
}

void Renderer::batch(Kind kind, GLuint texture, const Color &c, size_t vertices) {
	const bool same = kind == batch_kind_ && (kind == SOLID || texture == batch_texture_) && c.r == batch_color_.r &&
			c.g == batch_color_.g && c.b == batch_color_.b && c.a == batch_color_.a;
	if (!same || batch_.size() + vertices > batch_.capacity()) {
		flush();
		batch_kind_ = kind;
		batch_texture_ = texture;
		batch_color_ = c;
	}
}

// Solid triangle (fpSolidColor), batched with rectangles of the same colour.
void Renderer::triangle(float x0, float y0, float x1, float y1, float x2, float y2, const Color &color) {
	const Color c = color.with_alpha(transforms_.back().opacity);
	if (c.a <= 0.001f) {
		return;
	}
	batch(SOLID, 0, c, 3);
	const float sx = 2.0f / width_, sy = 2.0f / height_;
	const float xs[3] = { x0, x1, x2 }, ys[3] = { y0, y1, y2 };
	for (int i = 0; i < 3; i++) {
		float x = xs[i], y = ys[i];
		transform_point(x, y);
		batch_.push_back(Vertex{ x * sx - 1.0f, 1.0f - y * sy, 0, 1, 0, 0 });
	}
}

// Quarter-circle fan for corner `corner` (0 top-left, 1 top-right,
// 2 bottom-right, 3 bottom-left) of the circle centred at (cx, cy).
// inside = the rounded corner itself; otherwise the cap outside the arc
// (filling the square's corner, used to round images over a backdrop).
void Renderer::corner_fan(float cx, float cy, float r, int corner, bool inside, const Color &c) {
	const int segments = std::clamp((int)(r / 2.0f), 6, 16);
	const float start = 3.14159265f * (1.0f + 0.5f * corner); // 180, 270, 360, 450 degrees
	// Outer corner of the square this arc rounds off.
	const float ox = cx + ((corner == 1 || corner == 2) ? r : -r);
	const float oy = cy + ((corner == 2 || corner == 3) ? r : -r);
	const float px = inside ? cx : ox, py = inside ? cy : oy;
	float ax = cx + r * std::cos(start), ay = cy + r * std::sin(start);
	for (int i = 1; i <= segments; i++) {
		const float a = start + 0.5f * 3.14159265f * i / segments;
		const float bx = cx + r * std::cos(a), by = cy + r * std::sin(a);
		triangle(px, py, ax, ay, bx, by, c);
		ax = bx;
		ay = by;
	}
}

void Renderer::quad(Kind kind, GLuint texture, const Color &color, float x, float y, float w, float h, float u0,
		float v0, float u1, float v1) {
	float x0 = x, y0 = y, x1 = x + w, y1 = y + h;
	transform_point(x0, y0);
	transform_point(x1, y1);
	// MASK colours arrive opaque with opacity already applied (see mask_quad).
	const Color c = kind == MASK ? color : color.with_alpha(transforms_.back().opacity);
	if (c.a <= 0.001f) {
		return;
	}
	batch(kind, texture, c, 6);
	const float sx = 2.0f / width_, sy = 2.0f / height_;
	const float X0 = x0 * sx - 1.0f, X1 = x1 * sx - 1.0f;
	const float Y0 = 1.0f - y0 * sy, Y1 = 1.0f - y1 * sy;
	const Vertex tl{ X0, Y0, 0, 1, u0, v0 }, tr{ X1, Y0, 0, 1, u1, v0 };
	const Vertex bl{ X0, Y1, 0, 1, u0, v1 }, br{ X1, Y1, 0, 1, u1, v1 };
	batch_.push_back(tl);
	batch_.push_back(bl);
	batch_.push_back(tr);
	batch_.push_back(tr);
	batch_.push_back(bl);
	batch_.push_back(br);
}

// --- primitives ---------------------------------------------------------------

void Renderer::rect(float x, float y, float w, float h, const Color &c) {
	if (w > 0 && h > 0) {
		quad(SOLID, 0, c, x, y, w, h, 0, 0, 1, 1);
	}
}

void Renderer::mask_quad(GLuint texture, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
		const Color &c) {
	// fpAlphaColor ignores fillColor's alpha, so translucent text is drawn
	// opaque, pre-blended toward the backdrop (identical on solid backgrounds).
	const float a = c.a * transforms_.back().opacity;
	Color blended = a >= 0.999f ? c : mix(backdrop_, c, a);
	blended.a = 1;
	quad(MASK, texture, blended, x, y, w, h, u0, v0, u1, v1);
}

void Renderer::rounded_rect(float x, float y, float w, float h, float radius, const Color &c) {
	const float r = std::min(radius, std::min(w, h) / 2.0f);
	if (r <= 0.5f) {
		rect(x, y, w, h, c);
		return;
	}
	// Middle column + left/right strips, then the four anti-aliased corners.
	rect(x + r, y, w - 2 * r, h, c);
	rect(x, y + r, r, h - 2 * r, c);
	rect(x + w - r, y + r, r, h - 2 * r, c);
	// Corners as triangle fans with the solid shader. (Textured corners via
	// fpAlphaColor showed notches on PS4 hardware; solid geometry is exact.)
	corner_fan(x + r, y + r, r, 0, true, c);
	corner_fan(x + w - r, y + r, r, 1, true, c);
	corner_fan(x + w - r, y + h - r, r, 2, true, c);
	corner_fan(x + r, y + h - r, r, 3, true, c);
}

void Renderer::round_corners(float x, float y, float w, float h, float radius, const Color &background) {
	const float r = std::min(radius, std::min(w, h) / 2.0f);
	corner_fan(x + r, y + r, r, 0, false, background);
	corner_fan(x + w - r, y + r, r, 1, false, background);
	corner_fan(x + w - r, y + h - r, r, 2, false, background);
	corner_fan(x + r, y + h - r, r, 3, false, background);
}

void Renderer::image_cover(const Texture &t, float x, float y, float w, float h, float alpha) {
	if (!t.valid() || w <= 0 || h <= 0) {
		return;
	}
	// Crop the texture to the box's aspect ratio, centred.
	float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
	const float box = w / h, img = (float)t.width / t.height;
	if (img > box) {
		const float keep = box / img;
		u0 = (1 - keep) / 2;
		u1 = u0 + keep;
	} else {
		const float keep = img / box;
		v0 = (1 - keep) / 2;
		v1 = v0 + keep;
	}
	quad(IMAGE, t.id, Color{ 1, 1, 1, alpha }, x, y, w, h, u0, v0, u1, v1);
}

// A 1x256 texture: rgb = the fade colour, alpha = 0..1 top to bottom.
// Drawn with fpAlphaTexture (proven on PS4 by every image), not fpAlphaColor.
const Texture &Renderer::ramp(const Color &c) {
	const uint32_t key = ((uint32_t)(c.r * 255) << 16) | ((uint32_t)(c.g * 255) << 8) | (uint32_t)(c.b * 255);
	auto it = ramps_.find(key);
	if (it != ramps_.end()) {
		return it->second;
	}
	std::vector<uint8_t> px(256 * 4);
	for (int y = 0; y < 256; y++) {
		px[y * 4 + 0] = (uint8_t)(c.r * 255);
		px[y * 4 + 1] = (uint8_t)(c.g * 255);
		px[y * 4 + 2] = (uint8_t)(c.b * 255);
		px[y * 4 + 3] = (uint8_t)y;
	}
	return ramps_[key] = make_texture(1, 256, px.data());
}

void Renderer::vertical_fade(float x, float y, float w, float h, const Color &c, float alpha_top, float alpha_bottom) {
	// The ramp's alpha equals v, so sample v = alpha_top..alpha_bottom.
	const float v0 = std::clamp(alpha_top, 0.0f, 1.0f) * 255.0f / 256.0f + 0.5f / 256.0f;
	const float v1 = std::clamp(alpha_bottom, 0.0f, 1.0f) * 255.0f / 256.0f + 0.5f / 256.0f;
	quad(IMAGE, ramp(c).id, Color{ 1, 1, 1, c.a }, x, y, w, h, 0.5f, v0, 0.5f, v1);
}

float Renderer::text(Font &font, const std::string &utf8, float x, float baseline, const Color &c) {
	return font.draw(*this, utf8, x, baseline, c);
}
