// The SSE2 YUV -> RGBA conversion against the scalar reference, for every
// source layout, odd widths (SIMD + scalar tail) and both colour matrices.
#include "../src/video/nv12.h"
#include "test.h"

#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

const int TOLERANCE = 3; // SIMD uses 6-bit coefficients, the reference 8-bit

struct Picture {
	int w, h;
	std::vector<uint8_t> y, u, v, uv; // 8-bit planes (+ interleaved UV for NV12)
	std::vector<uint16_t> y10, u10, v10;
};

Picture random_picture(int w, int h, unsigned seed) {
	srand(seed);
	Picture p{ w, h };
	const int cw = (w + 1) / 2, ch = (h + 1) / 2;
	p.y.resize((size_t)w * h + 64);
	p.u.resize((size_t)cw * ch + 64);
	p.v.resize(p.u.size());
	for (auto &b : p.y) b = (uint8_t)(rand() & 0xFF);
	for (auto &b : p.u) b = (uint8_t)(rand() & 0xFF);
	for (auto &b : p.v) b = (uint8_t)(rand() & 0xFF);
	p.uv.resize((size_t)cw * 2 * ch + 64);
	for (int i = 0; i < cw * ch; i++) {
		p.uv[i * 2] = p.u[i];
		p.uv[i * 2 + 1] = p.v[i];
	}
	// 10-bit: the 8-bit value << 2 plus noise in the low bits, which the
	// conversion drops.
	auto widen = [](const std::vector<uint8_t> &src, std::vector<uint16_t> &dst) {
		dst.resize(src.size());
		for (size_t i = 0; i < src.size(); i++) dst[i] = (uint16_t)((src[i] << 2) | (rand() & 3));
	};
	widen(p.y, p.y10);
	widen(p.u, p.u10);
	widen(p.v, p.v10);
	return p;
}

template <class Src>
int max_difference(const Src &src, int w, int h, bool bt709, bool full) {
	std::vector<uint8_t> simd((size_t)w * h * 4), ref((size_t)w * h * 4);
	nv12::convert_rows(src, w, 0, h, bt709, full, simd.data());
	nv12::scalar_columns(src, 0, w, 0, h, nv12::coefficients(bt709, full, 8), ref.data());
	int worst = 0;
	for (size_t i = 0; i < simd.size(); i++) worst = std::max(worst, std::abs((int)simd[i] - (int)ref[i]));
	return worst;
}

} // namespace

TEST(nv12_simd_matches_reference) {
	for (int w : { 16, 34, 64, 158 }) {
		const int h = 6;
		const Picture p = random_picture(w, h, (unsigned)w);
		const int cw = (w + 1) / 2;
		for (int m = 0; m < 4; m++) {
			const bool bt709 = m & 1, full = m & 2;
			const int nv = max_difference(nv12::Nv12{ p.y.data(), p.uv.data(), (size_t)w, (size_t)cw * 2 }, w, h, bt709, full);
			const int i420 = max_difference(nv12::I420{ p.y.data(), p.u.data(), p.v.data(), (size_t)w, (size_t)cw }, w, h, bt709, full);
			const int p10 = max_difference(nv12::P10{ (const uint8_t *)p.y10.data(), (const uint8_t *)p.u10.data(),
					(const uint8_t *)p.v10.data(), (size_t)w * 2, (size_t)cw * 2 }, w, h, bt709, full);
			if (nv > TOLERANCE || i420 > TOLERANCE || p10 > TOLERANCE) {
				test::fail(__FILE__, __LINE__, "w=" + std::to_string(w) + " matrix=" + std::to_string(m) +
						": max difference nv12 " + std::to_string(nv) + ", i420 " + std::to_string(i420) + ", p10 " +
						std::to_string(p10));
			}
		}
	}
}

TEST(nv12_known_colours) {
	// Limited range: Y 16 = black, Y 235 = white; neutral chroma = grey.
	const int w = 16, h = 2;
	for (const auto &c : { std::make_pair(16, 0), std::make_pair(235, 255), std::make_pair(126, 128) }) {
		std::vector<uint8_t> y(w * h + 16, (uint8_t)c.first), uv(w * 2 + 16, 128), out(w * h * 4);
		nv12::convert_rows(nv12::Nv12{ y.data(), uv.data(), (size_t)w, (size_t)w }, w, 0, h, true, false, out.data());
		for (int ch = 0; ch < 3; ch++) CHECK_NEAR(out[ch], c.second, 2);
		CHECK_EQ((int)out[3], 255);
	}
}

TEST(nv12_padded_rows) {
	// Rows written `out_w` pixels apart (video frames padded for upload) hold
	// the same pixels as a tightly packed conversion, and the padding is untouched.
	const int w = 34, h = 4, out_w = 64;
	const Picture p = random_picture(w, h, 7);
	const nv12::I420 src{ p.y.data(), p.u.data(), p.v.data(), (size_t)w, (size_t)(w + 1) / 2 };
	std::vector<uint8_t> packed((size_t)w * h * 4), padded((size_t)out_w * h * 4, 0xAB);
	nv12::convert_rows(src, w, 0, h, true, false, packed.data());
	nv12::convert_rows(src, w, 0, h, true, false, padded.data(), out_w);
	for (int y = 0; y < h; y++) {
		CHECK(memcmp(packed.data() + (size_t)y * w * 4, padded.data() + (size_t)y * out_w * 4, (size_t)w * 4) == 0);
		CHECK_EQ((int)padded[((size_t)y * out_w + w) * 4], 0xAB);
	}
}
