#include "font.h"

#include "../platform/platform.h"
#include "bidi.h"

#include <hb.h>
#include FT_MODULE_H

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
const int ATLAS = 1024;
// Atlas keys: a character of the primary font (fast path), or this bit plus
// face << 16 | glyph index for shaped text.
const uint32_t SHAPED = 0x80000000u;
const size_t MAX_LAYOUTS = 512; // shaped strings kept (titles on screen, ellipsize prefixes)
const size_t MAX_WRAPPED = 8;

bool in(uint32_t c, uint32_t lo, uint32_t hi) {
	return c >= lo && c <= hi;
}

hb_unicode_general_category_t category(uint32_t c) {
	return hb_unicode_general_category(hb_unicode_funcs_get_default(), c);
}

// Marks and joiners stay in the font of the character they attach to.
bool attaches(uint32_t c) {
	const hb_unicode_general_category_t cat = category(c);
	return cat == HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK || cat == HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK ||
			cat == HB_UNICODE_GENERAL_CATEGORY_ENCLOSING_MARK || c == 0x200C || c == 0x200D || in(c, 0xFE00, 0xFE0F) ||
			in(c, 0xE0100, 0xE01EF);
}

bool is_letter(uint32_t c) {
	const hb_unicode_general_category_t cat = category(c);
	return cat == HB_UNICODE_GENERAL_CATEGORY_LOWERCASE_LETTER || cat == HB_UNICODE_GENERAL_CATEGORY_UPPERCASE_LETTER ||
			cat == HB_UNICODE_GENERAL_CATEGORY_TITLECASE_LETTER || cat == HB_UNICODE_GENERAL_CATEGORY_MODIFIER_LETTER ||
			cat == HB_UNICODE_GENERAL_CATEGORY_OTHER_LETTER;
}

bool is_bidi_control(uint32_t c) {
	return c == 0x200E || c == 0x200F || c == 0x061C || in(c, 0x202A, 0x202E) || in(c, 0x2066, 0x2069);
}

// Chinese and Japanese characters and punctuation: text without spaces.
bool is_cjk(uint32_t c) {
	return in(c, 0x2E80, 0x9FFF) || in(c, 0xF900, 0xFAFF) || in(c, 0xFF00, 0xFFEF) || in(c, 0x20000, 0x3FFFF);
}

bool listed(const char *utf8_list, uint32_t c) {
	const std::string list = utf8_list;
	for (size_t i = 0; i < list.size();) {
		if (next_codepoint(list, i) == c) return true;
	}
	return false;
}

// Line breaks around CJK text, except before closing punctuation and small
// kana, or after opening brackets.
bool cjk_break(uint32_t before, uint32_t after) {
	if (!is_cjk(before) && !is_cjk(after)) return false;
	return !listed("、。，．：；！？）」』】〉》〕｝〗〙ー…・ぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮヵヶ.,:;!?)]}%", after) &&
			!listed("（「『【〈《〔｛〖〘([{", before);
}

// FreeType allocates from the app's heap rather than from the system
// library's own (the CJK fallback alone takes megabytes).
void *ft_alloc(FT_Memory, long size) {
	return malloc((size_t)size);
}
void ft_free(FT_Memory, void *block) {
	free(block);
}
void *ft_realloc(FT_Memory, long, long new_size, void *block) {
	return realloc(block, (size_t)new_size);
}
FT_MemoryRec_ ft_memory = { nullptr, ft_alloc, ft_free, ft_realloc };

// HarfBuzz reads the font files itself; one face per file, shared by sizes.
hb_face_t *shared_hb_face(const std::string &path) {
	static std::unordered_map<std::string, hb_face_t *> faces;
	auto it = faces.find(path);
	if (it != faces.end()) return it->second;
	hb_face_t *face = nullptr;
	if (FILE *f = fopen(path.c_str(), "rb")) {
		fseek(f, 0, SEEK_END);
		const long size = ftell(f);
		fseek(f, 0, SEEK_SET);
		char *data = size > 0 ? (char *)malloc((size_t)size) : nullptr;
		if (data && fread(data, 1, (size_t)size, f) == (size_t)size) {
			hb_blob_t *blob = hb_blob_create(data, (unsigned)size, HB_MEMORY_MODE_READONLY, data, free);
			face = hb_face_create(blob, 0);
			hb_blob_destroy(blob);
		} else {
			free(data);
		}
		fclose(f);
	}
	if (!face) platform::log("font %s: HarfBuzz couldn't read it", path.c_str());
	faces[path] = face;
	return face;
}

} // namespace

FT_Library Font::library_ = nullptr;
std::vector<std::string> Font::fallbacks_;

// Which fallback has a character (index into fallbacks_, or -1). Coverage
// doesn't depend on the size, so one face per file answers it for every
// Font, which then opens only the fallbacks it really draws with.
int Font::fallback_for(uint32_t codepoint) {
	static std::vector<FT_Face> probes;
	static std::unordered_map<uint32_t, int> cache;
	auto it = cache.find(codepoint);
	if (it != cache.end()) return it->second;
	int found = -1;
	for (size_t i = 0; i < fallbacks_.size() && found < 0; i++) {
		if (probes.size() <= i) {
			FT_Face f = nullptr;
			if (const FT_Error error = FT_New_Face(library_, fallbacks_[i].c_str(), 0, &f)) {
				f = nullptr;
				platform::log("font %s failed to load (FreeType error 0x%02x)", fallbacks_[i].c_str(), error);
			}
			probes.push_back(f);
		}
		if (probes[i] && FT_Get_Char_Index(probes[i], codepoint) != 0) found = (int)i;
	}
	cache[codepoint] = found;
	return found;
}

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

void Font::set_fallbacks(const std::vector<std::string> &paths) {
	fallbacks_.clear();
	for (const std::string &p : paths) {
		if (FILE *f = fopen(p.c_str(), "rb")) {
			fclose(f);
			fallbacks_.push_back(p);
		} else {
			platform::log("fallback font %s is missing", p.c_str());
		}
	}
}

bool Font::load(const std::string &path, int pixel_size) {
	if (!library_) {
		// If that library can't open the primary font (a module it lacks),
		// FreeType's default setup is used instead.
		FT_Face test = nullptr;
		if (FT_New_Library(&ft_memory, &library_) == 0) {
			FT_Add_Default_Modules(library_);
			if (FT_New_Face(library_, path.c_str(), 0, &test) == 0) {
				FT_Done_Face(test);
			} else {
				FT_Done_Library(library_);
				library_ = nullptr;
			}
		} else {
			library_ = nullptr;
		}
		if (!library_) {
			platform::log("FreeType: own allocator unavailable, using the default");
			if (FT_Init_FreeType(&library_) != 0) {
				platform::log("FT_Init_FreeType failed");
				library_ = nullptr;
				return false;
			}
		}
	}
	size_ = pixel_size;
	faces_.clear();
	faces_.push_back(Face{ path });
	for (const std::string &p : fallbacks_) faces_.push_back(Face{ p });
	if (!face(0)) {
		return false;
	}
	pixels_.assign((size_t)ATLAS * ATLAS * 4, 0);
	for (size_t i = 0; i < pixels_.size(); i += 4) {
		pixels_[i] = pixels_[i + 1] = pixels_[i + 2] = 255;
	}
	atlas_ = make_texture(ATLAS, ATLAS, pixels_.data());
	return true;
}

void Font::set_size(int pixel_size) {
	if (pixel_size <= 0 || pixel_size == size_) return;
	size_ = pixel_size;
	for (Face &f : faces_) {
		if (f.ft) FT_Set_Pixel_Sizes(f.ft, 0, size_);
		if (f.hb) hb_font_set_scale(f.hb, size_ * 64, size_ * 64);
	}
	const int rows = pen_y_ < ATLAS ? pen_y_ : ATLAS;
	for (size_t i = 3; i < (size_t)rows * ATLAS * 4; i += 4) pixels_[i] = 0;
	glyphs_.clear();
	layouts_.clear();
	wrapped_.clear();
	pen_x_ = pen_y_ = 1;
	row_height_ = 0;
	cleared_ = true;
	dirty_ = true;
	generation_++;
}

FT_Face Font::face(int index) {
	Face &f = faces_[index];
	if (!f.opened) {
		f.opened = true;
		if (const FT_Error error = FT_New_Face(library_, f.path.c_str(), 0, &f.ft)) {
			f.ft = nullptr;
			platform::log("font %s failed to load (FreeType error 0x%02x)", f.path.c_str(), error);
		} else {
			FT_Set_Pixel_Sizes(f.ft, 0, size_);
		}
	}
	return f.ft;
}

hb_font_t *Font::hb_font(int index) {
	Face &f = faces_[index];
	if (!f.hb) {
		hb_face_t *hb_face = shared_hb_face(f.path);
		if (!hb_face) return nullptr;
		f.hb = hb_font_create(hb_face);
		hb_font_set_scale(f.hb, size_ * 64, size_ * 64); // positions in 26.6 pixels, like FreeType
	}
	return f.hb;
}

int Font::face_for(uint32_t codepoint) {
	auto it = face_of_.find(codepoint);
	if (it != face_of_.end()) return it->second;
	int found = 0;
	FT_Face primary = face(0);
	if (!primary || FT_Get_Char_Index(primary, codepoint) == 0) {
		const int fallback = fallback_for(codepoint);
		found = fallback < 0 ? -1 : fallback + 1;
	}
	face_of_[codepoint] = found;
	return found;
}

const Font::Glyph &Font::rasterise(uint32_t key, FT_Face ft, FT_UInt index) {
	Glyph g{};
	// Characters no font has (or .notdef) take no space, instead of drawing
	// the font's empty box.
	if (ft && index != 0 && FT_Load_Glyph(ft, index, FT_LOAD_RENDER) == 0) {
		const FT_GlyphSlot slot = ft->glyph;
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
		if (pen_y_ + g.height + 1 >= ATLAS && g.height + 2 < ATLAS) {
			// Full: start over with an empty atlas. draw() re-adds what it
			// needs, and quads already queued are flushed before the upload.
			const int rows = pen_y_ < ATLAS ? pen_y_ : ATLAS;
			for (size_t i = 3; i < (size_t)rows * ATLAS * 4; i += 4) pixels_[i] = 0;
			glyphs_.clear();
			pen_x_ = pen_y_ = 1;
			row_height_ = 0;
			cleared_ = true;
			generation_++;
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
	return glyphs_.emplace(key, g).first->second;
}

const Font::Glyph &Font::glyph(uint32_t codepoint) {
	auto it = glyphs_.find(codepoint);
	if (it != glyphs_.end()) {
		return it->second;
	}
	FT_Face f = face(0);
	return rasterise(codepoint, f, f ? FT_Get_Char_Index(f, codepoint) : 0);
}

const Font::Glyph &Font::glyph_key(uint32_t key) {
	auto it = glyphs_.find(key);
	if (it != glyphs_.end()) {
		return it->second;
	}
	return rasterise(key, face((key >> 16) & 0x7FFF), key & 0xFFFF);
}

void Font::upload_pending(Renderer &r) {
	if (!dirty_) {
		return;
	}
	// Quads queued with the old atlas contents are drawn first.
	r.flush();
	// Re-upload the used part of the atlas (glyphs are added rarely), or all
	// of it after it was cleared.
	int rows = pen_y_ + row_height_ + 1 < ATLAS ? pen_y_ + row_height_ + 1 : ATLAS;
	if (cleared_) rows = ATLAS;
	glBindTexture(GL_TEXTURE_2D, atlas_.id);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ATLAS, rows, GL_RGBA, GL_UNSIGNED_BYTE, pixels_.data());
	dirty_ = cleared_ = false;
}

// Whether the fast path can draw `utf8`: every character is in the primary
// font (or in none), and nothing needs reordering.
bool Font::simple(const std::string &utf8) {
	for (size_t i = 0; i < utf8.size();) {
		const uint32_t c = next_codepoint(utf8, i);
		if (c < 0x80) continue;
		if (is_bidi_control(c) || face_for(c) > 0) return false;
	}
	return true;
}

void Font::shape(const std::string &utf8, Layout &out) {
	std::vector<uint32_t> text;
	for (size_t i = 0; i < utf8.size();) text.push_back(next_codepoint(utf8, i));
	const size_t n = text.size();
	out.glyphs.clear();
	out.width = 0;
	if (n == 0) return;

	// Font for each character: the first that has it. Marks stay with their
	// base, and spaces and punctuation with the text around them when that
	// font has them too (longer runs to shape).
	std::vector<int> faces(n);
	for (size_t i = 0; i < n; i++) {
		int f = face_for(text[i]);
		if (i > 0 && f != faces[i - 1]) {
			const int prev = faces[i - 1];
			if (attaches(text[i]) ||
					(!is_letter(text[i]) && face(prev) && FT_Get_Char_Index(face(prev), text[i]) != 0)) {
				f = prev;
			}
		}
		if (f < 0) f = i > 0 ? faces[i - 1] : 0;
		faces[i] = f;
	}
	std::vector<uint8_t> levels;
	bidi::resolve(text, levels);

	// Runs of one font and direction, shaped separately and then put in
	// display order.
	struct Run {
		size_t start, length;
		int face;
	};
	std::vector<Run> runs;
	std::vector<uint8_t> run_levels;
	for (size_t i = 0; i < n; i++) {
		if (runs.empty() || faces[i] != runs.back().face || levels[i] != run_levels.back()) {
			runs.push_back({ i, 0, faces[i] });
			run_levels.push_back(levels[i]);
		}
		runs.back().length++;
	}
	static hb_buffer_t *buffer = hb_buffer_create();
	float pen = 0;
	for (int index : bidi::visual_order(run_levels)) {
		const Run &run = runs[index];
		hb_font_t *font = hb_font(run.face);
		if (!font) continue;
		hb_buffer_clear_contents(buffer);
		// The whole line is context, so Arabic letters join across runs.
		hb_buffer_add_codepoints(buffer, text.data(), (int)n, (unsigned)run.start, (int)run.length);
		hb_buffer_set_direction(buffer, run_levels[index] % 2 ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
		hb_buffer_guess_segment_properties(buffer);
		hb_shape(font, buffer, nullptr, 0);
		unsigned count = 0;
		const hb_glyph_info_t *info = hb_buffer_get_glyph_infos(buffer, &count);
		const hb_glyph_position_t *pos = hb_buffer_get_glyph_positions(buffer, &count);
		for (unsigned k = 0; k < count; k++) {
			if (info[k].codepoint == 0) continue; // missing character: no box, no space
			const uint32_t key = SHAPED | (uint32_t)run.face << 16 | (info[k].codepoint & 0xFFFF);
			out.glyphs.push_back({ key, pen + pos[k].x_offset / 64.0f, -pos[k].y_offset / 64.0f });
			pen += pos[k].x_advance / 64.0f;
		}
	}
	out.width = pen;
}

const Font::Layout &Font::layout(const std::string &utf8) {
	auto it = layouts_.find(utf8);
	if (it != layouts_.end()) return it->second;
	if (layouts_.size() >= MAX_LAYOUTS) layouts_.clear();
	Layout &l = layouts_[utf8];
	shape(utf8, l);
	return l;
}

float Font::measure(const std::string &utf8) {
	if (!simple(utf8)) {
		return layout(utf8).width;
	}
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
	size_t cut = 0;
	if (simple(utf8)) {
		float w = 0;
		size_t i = 0;
		while (i < utf8.size()) {
			const float adv = glyph(next_codepoint(utf8, i)).advance;
			if (w + adv > limit) {
				break;
			}
			w += adv;
			cut = i;
		}
	} else {
		// Shaped widths aren't sums of characters: find the longest prefix
		// that fits by bisection over character boundaries.
		std::vector<size_t> ends;
		for (size_t i = 0; i < utf8.size();) {
			next_codepoint(utf8, i);
			ends.push_back(i);
		}
		size_t lo = 0, hi = ends.size(); // prefixes of `lo` characters fit
		while (lo < hi) {
			const size_t mid = (lo + hi + 1) / 2;
			if (measure(utf8.substr(0, ends[mid - 1])) <= limit) {
				lo = mid;
			} else {
				hi = mid - 1;
			}
		}
		cut = lo > 0 ? ends[lo - 1] : 0;
	}
	return utf8.substr(0, cut) + dots;
}

std::vector<std::string> Font::wrap(const std::string &utf8, float max_width, int max_lines) {
	for (const Wrapped &w : wrapped_) {
		if (w.width == max_width && w.max_lines == max_lines && w.text == utf8) return w.lines;
	}
	if (wrapped_.size() >= MAX_WRAPPED) wrapped_.erase(wrapped_.begin());
	wrapped_.push_back({ utf8, max_width, max_lines, wrap_lines(utf8, max_width, max_lines) });
	return wrapped_.back().lines;
}

std::vector<std::string> Font::wrap_lines(const std::string &utf8, float max_width, int max_lines) {
	std::vector<std::string> lines;
	size_t start = 0;
	while (start <= utf8.size()) {
		size_t end = utf8.find('\n', start);
		if (end == std::string::npos) end = utf8.size();
		// Words, each with what joins it to the one before: a space, or
		// nothing where CJK text may break.
		std::vector<std::pair<std::string, std::string>> words;
		std::string word, glue;
		uint32_t prev = 0;
		for (size_t i = start; i < end;) {
			const size_t at = i;
			const uint32_t c = next_codepoint(utf8, i);
			if (c == ' ') {
				if (!word.empty()) words.push_back({ glue, word });
				word.clear();
				glue = " ";
			} else {
				if (!word.empty() && cjk_break(prev, c)) {
					words.push_back({ glue, word });
					word.clear();
					glue = "";
				}
				word.append(utf8, at, i - at);
			}
			prev = c;
		}
		if (!word.empty()) words.push_back({ glue, word });
		std::string line;
		for (const auto &w : words) {
			const std::string candidate = line.empty() ? w.second : line + w.first + w.second;
			if (!line.empty() && measure(candidate) > max_width) {
				lines.push_back(line);
				line = w.second;
			} else {
				line = candidate;
			}
		}
		if (!line.empty()) lines.push_back(line);
		start = end + 1;
	}
	if (max_lines > 0 && (int)lines.size() > max_lines) {
		lines.resize(max_lines);
		lines.back() = ellipsize(lines.back() + " \xE2\x80\xA6", max_width);
	}
	return lines;
}

float Font::draw(Renderer &r, const std::string &utf8, float x, float baseline, const Color &c) {
	if (!simple(utf8)) {
		const Layout &l = layout(utf8);
		// Every glyph in the atlas before drawing any of them (again if the
		// atlas was cleared halfway).
		for (int pass = 0; pass < 2; pass++) {
			const unsigned generation = generation_;
			for (const Placed &p : l.glyphs) glyph_key(p.key);
			if (generation == generation_) break;
		}
		upload_pending(r);
		for (const Placed &p : l.glyphs) {
			const Glyph &g = glyph_key(p.key);
			if (g.valid && g.width > 0) {
				r.mask_quad(atlas_.id, x + p.x + g.left, baseline + p.y - g.top, (float)g.width, (float)g.height, g.u0,
						g.v0, g.u1, g.v1, c);
			}
		}
		return l.width;
	}
	// Make sure every glyph is in the atlas before drawing any of them.
	for (int pass = 0; pass < 2; pass++) {
		const unsigned generation = generation_;
		for (size_t i = 0; i < utf8.size();) {
			glyph(next_codepoint(utf8, i));
		}
		if (generation == generation_) break;
	}
	upload_pending(r);
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
