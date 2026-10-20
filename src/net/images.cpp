#include "images.h"

#include "../platform/platform.h"
#include "http.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "stb_image.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

#include <cstring>
#include <deque>
#include <unordered_map>

namespace images {
namespace {

const int MAX_WIDTH = 640;          // artwork is shown at card size
const size_t MAX_TEXTURES = 160;    // ~160 x 0.9 MB of VRAM at most
const int UPLOADS_PER_FRAME = 2;    // avoid frame hitches
// Limits checked before decoding: a small file can claim a huge picture.
const size_t MAX_FILE = 16 * 1024 * 1024;
const int MAX_SIDE = 8192;
const int64_t MAX_PIXELS = 4096LL * 4096; // 64 MB as RGBA

bool size_ok(int w, int h) {
	return w > 0 && h > 0 && w <= MAX_SIDE && h <= MAX_SIDE && (int64_t)w * h <= MAX_PIXELS;
}

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

// WebP, which stb_image can't read (metahub serves newer posters as WebP):
// FFmpeg's decoder takes the whole RIFF file as one packet.
bool decode_webp(const std::string &data, Decoded &out) {
	if (data.size() < 12 || memcmp(data.data(), "RIFF", 4) != 0 || memcmp(data.data() + 8, "WEBP", 4) != 0) return false;
	const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_WEBP);
	if (!codec) return false;
	AVCodecContext *ctx = avcodec_alloc_context3(codec);
	AVPacket *packet = av_packet_alloc();
	AVFrame *frame = av_frame_alloc();
	bool ok = false;
	if (ctx && packet && frame) {
		ctx->thread_count = 1;
		ctx->max_pixels = MAX_PIXELS; // FFmpeg refuses larger frames before allocating them
		if (avcodec_open2(ctx, codec, nullptr) == 0 && av_new_packet(packet, (int)data.size()) == 0) {
			memcpy(packet->data, data.data(), data.size());
			int err = avcodec_send_packet(ctx, packet);
			if (err == 0) err = avcodec_receive_frame(ctx, frame);
			if (err == AVERROR(EAGAIN) && avcodec_send_packet(ctx, nullptr) == 0) err = avcodec_receive_frame(ctx, frame);
			const int w = frame->width, h = frame->height;
			SwsContext *sws = err == 0 && size_ok(w, h) ? sws_getContext(w, h, (AVPixelFormat)frame->format, w, h, AV_PIX_FMT_RGBA,
												   SWS_BILINEAR, nullptr, nullptr, nullptr)
									   : nullptr;
			if (sws) {
				out.width = w;
				out.height = h;
				out.rgba.resize((size_t)w * h * 4);
				uint8_t *dst[4] = { out.rgba.data() };
				int stride[4] = { w * 4 };
				ok = sws_scale(sws, frame->data, frame->linesize, 0, h, dst, stride) == h;
				sws_freeContext(sws);
			}
		}
	}
	av_frame_free(&frame);
	av_packet_free(&packet);
	avcodec_free_context(&ctx);
	return ok;
}

void start(const std::string &url) {
	g_entries[url].last_used = g_frame;
	http::Request request;
	request.url = url;
	request.max_body = MAX_FILE;
	http::send(
			request,
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
				auto img = std::make_shared<Decoded>();
				int w = 0, h = 0, n = 0;
				const bool stb = stbi_info_from_memory((const stbi_uc *)res.body.data(), (int)res.body.size(), &w, &h, &n);
				if (stb && !size_ok(w, h)) {
					res.error = "image too large";
					return;
				}
				uint8_t *px = !stb ? nullptr : stbi_load_from_memory((const stbi_uc *)res.body.data(), (int)res.body.size(), &w, &h, &n, 4);
				if (px) {
					img->width = w;
					img->height = h;
					img->rgba.assign(px, px + (size_t)w * h * 4);
					stbi_image_free(px);
				} else if (!decode_webp(res.body, *img)) {
					res.error = "image decode failed";
					return;
				}
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
