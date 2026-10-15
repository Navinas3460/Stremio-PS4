// YUV 4:2:0 -> RGBA conversion on the CPU (Piglet has no YUV shader).
//
// SSE2, 16 pixels x 2 rows per step, 6-bit fixed point with saturating
// 16-bit math (~1 ms per 1080p frame on desktop, ~4 ms on PS4). Sources:
//   Nv12   AvPlayer output (Y plane + interleaved UV)
//   I420   FFmpeg yuv420p (8-bit planar)
//   P10    FFmpeg yuv420p10le (10-bit planar, little-endian 16-bit words;
//          reduced to 8 bits). FFmpeg's swscale took ~20 ms per 1080p frame
//          for this on PS4, which made 10-bit HEVC lag.
// The scalar path (8-bit fixed point) is the reference and handles the
// columns left over when the width isn't a multiple of 16.
#pragma once

#include <emmintrin.h>
#include <stddef.h>
#include <stdint.h>

namespace nv12 {

struct Coefficients {
	int y_offset;           // 16 (limited range) or 0 (full range)
	int ky, rv, gu, gv, bu; // scaled by 1 << shift
};

// BT.709 for HD, BT.601 for SD; limited or full range.
inline Coefficients coefficients(bool bt709, bool full_range, int shift) {
	// Values for shift 8; shift 6 divides by 4 with rounding.
	Coefficients c;
	c.y_offset = full_range ? 0 : 16;
	c.ky = full_range ? 256 : 298;
	c.rv = bt709 ? (full_range ? 403 : 459) : (full_range ? 359 : 409);
	c.gu = bt709 ? (full_range ? 48 : 55) : (full_range ? 88 : 100);
	c.gv = bt709 ? (full_range ? 120 : 136) : (full_range ? 183 : 208);
	c.bu = bt709 ? (full_range ? 475 : 541) : (full_range ? 454 : 516);
	if (shift == 6) {
		int *v[] = { &c.ky, &c.rv, &c.gu, &c.gv, &c.bu };
		for (int *p : v) *p = (*p + 2) / 4;
	}
	return c;
}

inline uint8_t clamp8(int v) {
	return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

// --- Sources: luma/chroma access for the SIMD and scalar paths ----------------------

struct Nv12 {
	const uint8_t *y, *uv;
	size_t y_stride, uv_stride;
	__m128i luma16(int row, int col) const { return _mm_loadu_si128((const __m128i *)(y + row * y_stride + col)); }
	// U and V (as int16, not yet centred) for luma columns col..col+15.
	void chroma8(int row, int col, __m128i &u, __m128i &v) const {
		const __m128i uvv = _mm_loadu_si128((const __m128i *)(uv + (row / 2) * uv_stride + col));
		u = _mm_and_si128(uvv, _mm_set1_epi16(0x00FF));
		v = _mm_srli_epi16(uvv, 8);
	}
	int luma(int row, int col) const { return y[row * y_stride + col]; }
	int u_at(int row, int col) const { return uv[(row / 2) * uv_stride + (col & ~1)]; }
	int v_at(int row, int col) const { return uv[(row / 2) * uv_stride + (col & ~1) + 1]; }
};

struct I420 {
	const uint8_t *y, *u, *v;
	size_t y_stride, uv_stride;
	__m128i luma16(int row, int col) const { return _mm_loadu_si128((const __m128i *)(y + row * y_stride + col)); }
	void chroma8(int row, int col, __m128i &uo, __m128i &vo) const {
		const size_t off = (row / 2) * uv_stride + col / 2;
		const __m128i zero = _mm_setzero_si128();
		uo = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(u + off)), zero);
		vo = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(v + off)), zero);
	}
	int luma(int row, int col) const { return y[row * y_stride + col]; }
	int u_at(int row, int col) const { return u[(row / 2) * uv_stride + col / 2]; }
	int v_at(int row, int col) const { return v[(row / 2) * uv_stride + col / 2]; }
};

// Strides in bytes, like FFmpeg's linesize.
struct P10 {
	const uint8_t *y, *u, *v;
	size_t y_stride, uv_stride;
	static const uint16_t *at(const uint8_t *plane, size_t stride, int row, int col) {
		return (const uint16_t *)(plane + row * stride) + col;
	}
	__m128i luma16(int row, int col) const {
		const uint16_t *p = at(y, y_stride, row, col);
		const __m128i a = _mm_srli_epi16(_mm_loadu_si128((const __m128i *)p), 2);
		const __m128i b = _mm_srli_epi16(_mm_loadu_si128((const __m128i *)(p + 8)), 2);
		return _mm_packus_epi16(a, b);
	}
	void chroma8(int row, int col, __m128i &uo, __m128i &vo) const {
		uo = _mm_srli_epi16(_mm_loadu_si128((const __m128i *)at(u, uv_stride, row / 2, col / 2)), 2);
		vo = _mm_srli_epi16(_mm_loadu_si128((const __m128i *)at(v, uv_stride, row / 2, col / 2)), 2);
	}
	int luma(int row, int col) const { return *at(y, y_stride, row, col) >> 2; }
	int u_at(int row, int col) const { return *at(u, uv_stride, row / 2, col / 2) >> 2; }
	int v_at(int row, int col) const { return *at(v, uv_stride, row / 2, col / 2) >> 2; }
};

// --- Conversion -------------------------------------------------------------------------

// Columns [x0, w) of rows [row0, row1), scalar (reference).
template <class Src>
void scalar_columns(const Src &src, int x0, int w, int row0, int row1, const Coefficients &c, uint8_t *out) {
	for (int row = row0; row < row1; row++) {
		uint8_t *o = out + (size_t)row * w * 4;
		for (int col = x0; col < w; col++) {
			const int u = src.u_at(row, col) - 128, v = src.v_at(row, col) - 128;
			const int l = c.ky * (src.luma(row, col) - c.y_offset);
			uint8_t *px = o + col * 4;
			px[0] = clamp8((l + c.rv * v + 128) >> 8);
			px[1] = clamp8((l - c.gu * u - c.gv * v + 128) >> 8);
			px[2] = clamp8((l + c.bu * u + 128) >> 8);
			px[3] = 255;
		}
	}
}

// Writes 16 RGBA pixels from 8 x int16 R/G/B pairs (lo = pixels 0-7, hi = 8-15).
inline void store16(uint8_t *dst, __m128i r_lo, __m128i r_hi, __m128i g_lo, __m128i g_hi, __m128i b_lo, __m128i b_hi) {
	const __m128i r = _mm_packus_epi16(_mm_srai_epi16(r_lo, 6), _mm_srai_epi16(r_hi, 6));
	const __m128i g = _mm_packus_epi16(_mm_srai_epi16(g_lo, 6), _mm_srai_epi16(g_hi, 6));
	const __m128i b = _mm_packus_epi16(_mm_srai_epi16(b_lo, 6), _mm_srai_epi16(b_hi, 6));
	const __m128i a = _mm_set1_epi8((char)0xFF);
	const __m128i rg_lo = _mm_unpacklo_epi8(r, g), rg_hi = _mm_unpackhi_epi8(r, g);
	const __m128i ba_lo = _mm_unpacklo_epi8(b, a), ba_hi = _mm_unpackhi_epi8(b, a);
	_mm_storeu_si128((__m128i *)dst, _mm_unpacklo_epi16(rg_lo, ba_lo));
	_mm_storeu_si128((__m128i *)(dst + 16), _mm_unpackhi_epi16(rg_lo, ba_lo));
	_mm_storeu_si128((__m128i *)(dst + 32), _mm_unpacklo_epi16(rg_hi, ba_hi));
	_mm_storeu_si128((__m128i *)(dst + 48), _mm_unpackhi_epi16(rg_hi, ba_hi));
}

// Rows [row0, row1) (even) of a w-wide picture into tightly packed RGBA.
template <class Src>
void convert_rows(const Src &src, int w, int row0, int row1, bool bt709, bool full_range, uint8_t *out) {
	const Coefficients c = coefficients(bt709, full_range, 6);
	const int simd_w = w & ~15;
	const __m128i zero = _mm_setzero_si128();
	const __m128i y_off = _mm_set1_epi16((short)c.y_offset), c128 = _mm_set1_epi16(128);
	const __m128i ky = _mm_set1_epi16((short)c.ky), rv = _mm_set1_epi16((short)c.rv);
	const __m128i gu = _mm_set1_epi16((short)c.gu), gv = _mm_set1_epi16((short)c.gv);
	const __m128i bu = _mm_set1_epi16((short)c.bu), round = _mm_set1_epi16(32);
	for (int row = row0; row + 1 < row1; row += 2) {
		uint8_t *oa = out + (size_t)row * w * 4;
		for (int col = 0; col < simd_w; col += 16) {
			__m128i u, v;
			src.chroma8(row, col, u, v);
			u = _mm_sub_epi16(u, c128);
			v = _mm_sub_epi16(v, c128);
			const __m128i rt = _mm_add_epi16(_mm_mullo_epi16(v, rv), round);
			const __m128i gt = _mm_sub_epi16(round, _mm_add_epi16(_mm_mullo_epi16(u, gu), _mm_mullo_epi16(v, gv)));
			const __m128i bt = _mm_add_epi16(_mm_mullo_epi16(u, bu), round);
			// Each chroma value covers two horizontal pixels.
			const __m128i r_lo = _mm_unpacklo_epi16(rt, rt), r_hi = _mm_unpackhi_epi16(rt, rt);
			const __m128i g_lo = _mm_unpacklo_epi16(gt, gt), g_hi = _mm_unpackhi_epi16(gt, gt);
			const __m128i b_lo = _mm_unpacklo_epi16(bt, bt), b_hi = _mm_unpackhi_epi16(bt, bt);
			for (int k = 0; k < 2; k++) { // both rows share the chroma
				const __m128i yy = src.luma16(row + k, col);
				const __m128i y_lo = _mm_mullo_epi16(_mm_sub_epi16(_mm_unpacklo_epi8(yy, zero), y_off), ky);
				const __m128i y_hi = _mm_mullo_epi16(_mm_sub_epi16(_mm_unpackhi_epi8(yy, zero), y_off), ky);
				store16(oa + (size_t)k * w * 4 + col * 4, _mm_adds_epi16(y_lo, r_lo), _mm_adds_epi16(y_hi, r_hi),
						_mm_adds_epi16(y_lo, g_lo), _mm_adds_epi16(y_hi, g_hi), _mm_adds_epi16(y_lo, b_lo),
						_mm_adds_epi16(y_hi, b_hi));
			}
		}
	}
	if (simd_w < w) scalar_columns(src, simd_w, w, row0, row1, coefficients(bt709, full_range, 8), out);
}

// AvPlayer's NV12 (both planes share one pitch).
inline void nv12_to_rgba(const uint8_t *y_plane, const uint8_t *uv_plane, size_t pitch, int w, int h, bool bt709,
		bool full_range, uint8_t *out) {
	convert_rows(Nv12{ y_plane, uv_plane, pitch, pitch }, w, 0, h, bt709, full_range, out);
}

} // namespace nv12
