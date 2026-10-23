// FFmpeg video backend (desktop, and the default on PS4): software decoding
// on a thread, the audio output queue as the master clock, RGBA frames
// uploaded on the main thread. Plays what AvPlayer can't (MKV, HEVC, 10-bit,
// E-AC3...): pPlay showed the PS4's CPU keeps up with 1080p HEVC 10-bit.
//
// Network input goes through a ByteSource (RangeReader over our HTTP stack:
// curl / libSceHttp, or TorrentStream for magnet links) as a custom
// AVIOContext, the same on both platforms; FFmpeg's own network code isn't
// built for PS4.
#include "video_player.h"

#include "nv12.h"

#include "../core/languages.h"
#include "../net/byte_source.h"
#include "../net/urls.h"
#include "../platform/platform.h"
#include "../platform/threads.h"

#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>

#if defined(STREMIO_PS4)
#include <orbis/AudioOut.h>
#include <orbis/Sysmodule.h>
#else
#include <SDL2/SDL.h>
#endif

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace {

// Wider frames are scaled down (swscale, slow). 2560 keeps common wide
// releases (e.g. 2160x1080) on the fast path.
const int MAX_WIDTH = 2560;
const int TEXTURE_ALIGN = 32; // frame rows are padded to this many pixels (see the converter)
const size_t MAX_FRAMES = 6;
const double MAX_QUEUED_AUDIO = 1.0; // seconds
const int RATE = 48000, CHANNELS = 2;
const size_t THREAD_STACK = 1024 * 1024;
const int AVIO_BUFFER = 256 * 1024;

std::string av_error(int err) {
	char buf[AV_ERROR_MAX_STRING_SIZE] = {};
	av_strerror(err, buf, sizeof(buf));
	return buf;
}

// "English · 5.1 E-AC3", "Commentary (English)"... for a stream.
std::string track_label(const AVStream *st, int ordinal) {
	const AVDictionaryEntry *lang = av_dict_get(st->metadata, "language", nullptr, 0);
	const AVDictionaryEntry *title = av_dict_get(st->metadata, "title", nullptr, 0);
	std::string name = lang && lang->value[0] ? language_name(lang->value) : "";
	if (name == "Unknown") name.clear();
	std::string label = name;
	if (title && title->value[0] && name.find(title->value) == std::string::npos) {
		label = label.empty() ? title->value : label + " \xC2\xB7 " + title->value;
	}
	const AVCodecParameters *par = st->codecpar;
	std::string details;
	if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
		const int ch = par->ch_layout.nb_channels;
		details = ch == 6 ? "5.1" : ch == 8 ? "7.1" : ch == 2 ? "Stereo" : ch == 1 ? "Mono" : std::to_string(ch) + " ch";
		std::string codec = avcodec_get_name(par->codec_id);
		for (char &c : codec) c = (char)toupper((unsigned char)c);
		if (codec == "EAC3") codec = "E-AC3";
		details += " " + codec;
	} else if (par->codec_id == AV_CODEC_ID_HDMV_PGS_SUBTITLE || par->codec_id == AV_CODEC_ID_DVD_SUBTITLE) {
		details = "image";
	}
	if (st->disposition & AV_DISPOSITION_FORCED) details += details.empty() ? "forced" : ", forced";
	if (st->disposition & AV_DISPOSITION_HEARING_IMPAIRED) details += details.empty() ? "SDH" : ", SDH";
	if (label.empty()) label = "Track " + std::to_string(ordinal);
	return details.empty() ? label : label + " \xC2\xB7 " + details;
}

// The text of an ASS event ("ReadOrder,Layer,Style,Name,MarginL,MarginR,
// MarginV,Effect,Text") without override tags; \N = new line.
std::string ass_text(const char *ass) {
	if (!ass) return "";
	const char *p = ass;
	for (int commas = 0; *p && commas < 8; p++) {
		if (*p == ',') commas++;
	}
	std::string out;
	for (; *p; p++) {
		if (*p == '{') {
			while (*p && *p != '}') p++;
			if (!*p) break;
		} else if (*p == '\\' && (p[1] == 'N' || p[1] == 'n')) {
			out += '\n';
			p++;
		} else if (*p == '\\' && p[1] == 'h') {
			out += ' ';
			p++;
		} else {
			out += *p;
		}
	}
	while (!out.empty() && (out.back() == '\n' || out.back() == ' ' || out.back() == '\r')) out.pop_back();
	return out;
}

// --- Audio output: 48 kHz s16 stereo, queued -------------------------------------

#if defined(STREMIO_PS4)

// sceAudioOut on its own thread: sceAudioOutOutput blocks until the previous
// grain has played, which paces it.
class AudioOut {
public:
	bool open() {
		static bool initialised = false;
		if (!initialised) {
			initialised = true;
			sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_AUDIOOUT);
			const int rc = sceAudioOutInit();
			platform::log("ffmpeg: sceAudioOutInit -> 0x%08X", rc);
		}
		port_ = sceAudioOutOpen(ORBIS_USER_SERVICE_USER_ID_SYSTEM, ORBIS_AUDIO_OUT_PORT_TYPE_MAIN, 0, GRAIN, RATE,
				ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
		platform::log("ffmpeg: audio port %d", port_);
		if (port_ < 0) return false;
		stop_ = false;
		running_ = true;
		threads::start(entry, this, 256 * 1024, "stremio_audio_out");
		return true;
	}

	void close() {
		stop_ = true;
		while (running_) usleep(1000);
		if (port_ >= 0) sceAudioOutClose(port_);
		port_ = -1;
		clear();
	}

	void write(const int16_t *samples, int frames) {
		threads::Lock lock(mutex_);
		ring_.insert(ring_.end(), samples, samples + (size_t)frames * CHANNELS);
	}

	double queued() {
		threads::Lock lock(mutex_);
		return (ring_.size() / CHANNELS + in_flight_) / (double)RATE;
	}

	void set_paused(bool paused) { paused_ = paused; }

	void clear() {
		threads::Lock lock(mutex_);
		ring_.clear();
	}

private:
	static const int GRAIN = 1024;

	static void *entry(void *self) {
		static_cast<AudioOut *>(self)->loop();
		return nullptr;
	}

	void loop() {
		std::vector<int16_t> grain(GRAIN * CHANNELS);
		while (!stop_) {
			bool have = false;
			{
				threads::Lock lock(mutex_);
				if (!paused_ && ring_.size() >= grain.size()) {
					std::copy(ring_.begin(), ring_.begin() + grain.size(), grain.begin());
					ring_.erase(ring_.begin(), ring_.begin() + grain.size());
					in_flight_ = GRAIN;
					have = true;
				}
			}
			if (!have) {
				usleep(4000);
				continue;
			}
			sceAudioOutOutput(port_, grain.data());
			threads::Lock lock(mutex_);
			in_flight_ = 0;
		}
		running_ = false;
	}

	int port_ = -1;
	threads::Mutex mutex_;
	std::deque<int16_t> ring_;
	int in_flight_ = 0;
	std::atomic<bool> paused_{ true }, stop_{ false }, running_{ false };
};

#else

class AudioOut {
public:
	bool open() {
		if (!SDL_WasInit(SDL_INIT_AUDIO)) SDL_InitSubSystem(SDL_INIT_AUDIO);
		SDL_AudioSpec want = {}, have = {};
		want.freq = RATE;
		want.format = AUDIO_S16SYS;
		want.channels = CHANNELS;
		want.samples = 1024;
		dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
		return dev_ != 0;
	}
	void close() {
		if (dev_) SDL_CloseAudioDevice(dev_);
		dev_ = 0;
	}
	void write(const int16_t *samples, int frames) { SDL_QueueAudio(dev_, samples, (Uint32)(frames * CHANNELS * 2)); }
	double queued() { return SDL_GetQueuedAudioSize(dev_) / (double)(RATE * CHANNELS * 2); }
	void set_paused(bool paused) { SDL_PauseAudioDevice(dev_, paused ? 1 : 0); }
	void clear() { SDL_ClearQueuedAudio(dev_); }

private:
	SDL_AudioDeviceID dev_ = 0;
};

#endif

// --- Player ---------------------------------------------------------------------------

class FfmpegPlayer : public VideoPlayer {
public:
	~FfmpegPlayer() override { close(); }

	void open(const std::string &url, const std::vector<std::string> &headers, double start) override {
		close();
		state_ = OPENING;
		error_.clear();
		abort_ = false;
		eof_ = false;
		has_audio_ = false;
		duration_ = 0;
		audio_end_ = start;
		wall_pts_ = start;
		wall_running_ = false;
		want_playing_ = true;
		url_ = url;
		headers_ = headers;
		start_ = start;
		audio_ok_ = audio_.open();
		running_ = true;
		if (!threads::start(decode_entry, this, THREAD_STACK, "stremio_decode")) {
			running_ = false;
			state_ = FAILED;
			error_ = "Couldn't start the player";
		}
	}

	void play() override {
		want_playing_ = true;
		if (state_ == PAUSED) {
			wall_running_ = true;
			wall_start_ = platform::now();
			if (audio_ok_) audio_.set_paused(false);
			state_ = PLAYING;
		}
	}

	void pause() override {
		want_playing_ = false;
		if (state_ == PLAYING || state_ == BUFFERING) {
			wall_pts_ = clock();
			wall_running_ = false;
			if (audio_ok_) audio_.set_paused(true);
			state_ = PAUSED;
		}
	}

	void seek(double seconds) override {
		if (state_ == IDLE || state_ == FAILED) return;
		double target = std::max(0.0, seconds);
		if (duration_ > 0) target = std::min(target, std::max(0.0, duration_.load() - 1.0));
		{
			threads::Lock lock(mutex_);
			recycle_all();
			generation_++;
			seek_target_ = target;
			seek_requested_ = true;
			eof_ = false;
			if (audio_ok_) audio_.clear();
			audio_end_ = target;
		}
		wall_pts_ = target;
		wall_start_ = platform::now();
		show_next_ = true;
	}

	void update() override {
		if (state_ == IDLE || state_ == FAILED || state_ == ENDED) return;
		if (failed_) {
			threads::Lock lock(mutex_);
			error_ = thread_error_;
			state_ = FAILED;
			return;
		}
		if (state_ == OPENING && opened_) {
			state_ = want_playing_ ? PLAYING : PAUSED;
			if (state_ == PLAYING) {
				wall_running_ = true;
				wall_start_ = platform::now();
				if (audio_ok_) audio_.set_paused(false);
			}
		}
		// Present the newest frame that is due.
		bool have = false, ended = false;
		{
			threads::Lock lock(mutex_);
			const double now = clock();
			const bool show_first = show_next_ && !frames_.empty();
			while (frames_.size() >= 2 && frames_[1].pts <= now) {
				recycle(frames_.front());
				frames_.pop_front();
			}
			if (!frames_.empty() && (show_first || frames_.front().pts <= now + 0.01)) {
				recycle(shown_);
				shown_ = std::move(frames_.front());
				frames_.pop_front();
				show_next_ = false;
				have = true;
			}
			const bool audio_drained = !has_audio_ || audio_.queued() < 0.03;
			ended = state_ == PLAYING && eof_ && frames_.empty() && audio_drained;
		}
		if (have) upload(shown_);
		if (ended) state_ = ENDED;
	}

	State state() const override { return state_; }
	double position() const override { return std::max(0.0, clock()); }
	double duration() const override { return duration_; }
	std::string error() const override { return error_; }
	const Texture &frame() const override { return textures_[tex_idx_]; }
	std::string source_status() const override {
		threads::Lock lock(mutex_);
		return reader_ ? reader_->status() : std::string();
	}

	std::vector<Track> audio_tracks() const override {
		threads::Lock lock(mutex_);
		return audio_tracks_;
	}
	int audio_track() const override { return audio_sel_; }
	void set_audio_track(int id) override {
		if (id == audio_sel_ || state_ == IDLE || state_ == FAILED) return;
		want_audio_ = id;
		seek(position()); // refill with the new audio from here
	}
	std::vector<Track> subtitle_tracks() const override {
		threads::Lock lock(mutex_);
		return sub_tracks_;
	}
	void set_subtitle_track(int id) override {
		if (id == sub_sel_ || state_ == IDLE || state_ == FAILED) return;
		want_sub_ = id;
		sub_sel_ = id;
		{
			threads::Lock lock(mutex_);
			cues_.clear();
		}
		// The packets around the current position were already read: read them
		// again so the new track's current line shows.
		if (id >= 0) seek(position());
	}

	bool current_subtitle(Subtitle &out) override {
		if (sub_sel_ < 0) return false;
		const double t = position();
		threads::Lock lock(mutex_);
		while (!cues_.empty() && cues_.front().end < t - 2) cues_.pop_front();
		const Cue *now = nullptr;
		for (const Cue &c : cues_) {
			if (c.start <= t && t < c.end) now = &c; // the latest one wins
		}
		if (!now) return false;
		if (now->rgba.empty()) {
			out.text = now->text;
			return !out.text.empty();
		}
		if (now->id != sub_texture_cue_) {
			if (sub_texture_.valid()) glDeleteTextures(1, &sub_texture_.id);
			sub_texture_ = make_texture(now->w, now->h, now->rgba.data());
			sub_texture_cue_ = now->id;
		}
		out.image = &sub_texture_;
		out.x = now->x;
		out.y = now->y;
		out.w = now->fw;
		out.h = now->fh;
		return true;
	}

private:
	struct Cue {
		uint32_t id = 0;
		double start = 0, end = INFINITY; // INFINITY: until the next image cue
		std::string text;
		std::vector<uint8_t> rgba; // image cues
		int w = 0, h = 0;
		float x = 0, y = 0, fw = 0, fh = 0; // fractions of the video frame
	};

	struct Frame {
		double pts = 0;
		int width = 0, height = 0; // of the buffer (and texture): width padded to TEXTURE_ALIGN
		int shown_width = 0;       // of the picture
		std::vector<uint8_t> rgba;
	};

	double clock() const {
		if (has_audio_) return audio_end_ - const_cast<AudioOut &>(audio_).queued();
		return wall_running_ ? wall_pts_ + (platform::now() - wall_start_) : wall_pts_.load();
	}

	// Frame buffers are reused: a 1080p frame is 8 MB. Call with mutex_ held.
	void recycle(Frame &f) {
		if (f.rgba.capacity() > 0 && pool_.size() < MAX_FRAMES + 2) pool_.push_back(std::move(f.rgba));
		f.rgba = std::vector<uint8_t>();
	}
	void recycle_all() {
		for (Frame &f : frames_) recycle(f);
		frames_.clear();
	}

	void upload(const Frame &f) {
		const double t0 = platform::now();
		tex_idx_ = (tex_idx_ + 1) % 3;
		Texture &tex = textures_[tex_idx_];
		if (tex.valid() && tex.width == f.width && tex.height == f.height) {
			glBindTexture(GL_TEXTURE_2D, tex.id);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, f.width, f.height, GL_RGBA, GL_UNSIGNED_BYTE, f.rgba.data());
		} else {
			if (tex.valid()) glDeleteTextures(1, &tex.id);
			tex = make_texture(f.width, f.height, f.rgba.data());
		}
		tex.shown_width = f.shown_width;
		glFlush();
		upload_time_ += platform::now() - t0;
		uploads_++;
	}

	void close() {
		abort_ = true;
		{
			threads::Lock lock(mutex_);
			if (reader_) reader_->abort();
		}
		while (running_) usleep(2000);
		if (audio_ok_) audio_.close();
		audio_ok_ = false;
		{
			threads::Lock lock(mutex_);
			recycle_all();
			recycle(shown_);
			pool_.clear();
		}
		for (Texture &t : textures_) {
			if (t.valid()) {
				glDeleteTextures(1, &t.id);
				t = Texture();
			}
		}
		tex_idx_ = 0;
		if (sub_texture_.valid()) {
			glDeleteTextures(1, &sub_texture_.id);
			sub_texture_ = Texture();
		}
		sub_texture_cue_ = 0;
		{
			threads::Lock lock(mutex_);
			cues_.clear();
			audio_tracks_.clear();
			sub_tracks_.clear();
		}
		audio_sel_ = -1;
		sub_sel_ = -1;
		want_audio_ = -2;
		want_sub_ = -2;
		opened_ = false;
		failed_ = false;
		state_ = IDLE;
	}

	static int interrupt(void *self) { return static_cast<FfmpegPlayer *>(self)->abort_.load(); }

	// AVIOContext callbacks over the RangeReader.
	static int avio_read(void *self, uint8_t *buf, int size) {
		FfmpegPlayer *p = static_cast<FfmpegPlayer *>(self);
		const int n = p->reader_->read(p->io_pos_, buf, size);
		if (n == 0) return AVERROR_EOF;
		if (n < 0) return p->abort_ ? AVERROR_EXIT : AVERROR(EIO);
		p->io_pos_ += n;
		return n;
	}

	static int64_t avio_seek(void *self, int64_t offset, int whence) {
		FfmpegPlayer *p = static_cast<FfmpegPlayer *>(self);
		const int64_t size = (int64_t)p->reader_->size();
		switch (whence & ~AVSEEK_FORCE) {
			case AVSEEK_SIZE: return size;
			case SEEK_SET: p->io_pos_ = offset; break;
			case SEEK_CUR: p->io_pos_ += offset; break;
			case SEEK_END: p->io_pos_ = size + offset; break;
			default: return -1;
		}
		return p->io_pos_;
	}

	void fail(const std::string &message) {
		platform::log("ffmpeg: failed: %s", message.c_str());
		threads::Lock lock(mutex_);
		thread_error_ = message;
		failed_ = true;
	}

	static void *decode_entry(void *self) {
		FfmpegPlayer *p = static_cast<FfmpegPlayer *>(self);
		p->decode_loop();
		p->running_ = false;
		return nullptr;
	}

	void decode_loop() {
		const bool network = ByteSource::supports(url_);
		AVFormatContext *fmt = avformat_alloc_context();
		fmt->interrupt_callback.callback = &FfmpegPlayer::interrupt;
		fmt->interrupt_callback.opaque = this;
		AVIOContext *io = nullptr;
		if (network) {
			{
				threads::Lock lock(mutex_);
				reader_ = ByteSource::create(url_, headers_);
			}
			if (abort_) return;
			if (!reader_->open()) {
				if (!abort_) fail(reader_->error().empty() ? "Couldn't open the stream" : reader_->error());
				avformat_free_context(fmt);
				free_io(nullptr);
				return;
			}
			io_pos_ = 0;
			uint8_t *buffer = (uint8_t *)av_malloc(AVIO_BUFFER);
			io = avio_alloc_context(buffer, AVIO_BUFFER, 0, this, avio_read, nullptr, avio_seek);
			fmt->pb = io;
			fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
		}
		const double t_open = platform::now();
		int err = avformat_open_input(&fmt, url_.c_str(), nullptr, nullptr);
		if (err < 0) {
			if (!abort_) fail("Couldn't open the stream: " + av_error(err));
			free_io(io);
			return;
		}
		AVCodecContext *vdec = nullptr, *adec = nullptr;
		SwsContext *sws = nullptr;
		SwrContext *swr = nullptr;
		AVPacket *pkt = av_packet_alloc();
		AVFrame *frame = av_frame_alloc();
		double offset = 0, skip_until = start_, start = start_;
		uint32_t gen = generation_;
		int vi = -1, ai = -1, si = -1;
		AVCodecContext *sdec = nullptr;
		int frames_converted = 0;
		double convert_time = 0;

		auto open_decoder = [&](int index, AVCodecContext *&ctx) {
			const AVCodec *codec = avcodec_find_decoder(fmt->streams[index]->codecpar->codec_id);
			if (!codec) return false;
			ctx = avcodec_alloc_context3(codec);
			avcodec_parameters_to_context(ctx, fmt->streams[index]->codecpar);
			ctx->pkt_timebase = fmt->streams[index]->time_base;
#if defined(STREMIO_PS4)
			ctx->thread_count = 5; // 6 usable cores; the rest of the app is light
#else
			ctx->thread_count = 0;
#endif
			return avcodec_open2(ctx, codec, nullptr) >= 0;
		};
		auto pts_of = [&](int index) {
			int64_t ts = frame->best_effort_timestamp != AV_NOPTS_VALUE ? frame->best_effort_timestamp : frame->pts;
			return ts == AV_NOPTS_VALUE ? -1.0 : ts * av_q2d(fmt->streams[index]->time_base) - offset;
		};
		auto do_seek = [&](double target) {
			const int64_t ts = (int64_t)((target + offset) * AV_TIME_BASE);
			avformat_seek_file(fmt, -1, INT64_MIN, ts, ts, 0);
			if (vdec) avcodec_flush_buffers(vdec);
			if (adec) avcodec_flush_buffers(adec);
			if (sdec) avcodec_flush_buffers(sdec);
			{
				threads::Lock lock(mutex_);
				cues_.clear();
			}
			skip_until = target;
		};
		auto on_video = [&]() {
			const double pts = pts_of(vi);
			if (pts >= 0 && pts < skip_until - 0.04) return;
			int w = frame->width & ~1, h = frame->height & ~1;
			if (w > MAX_WIDTH) {
				h = (int)((int64_t)h * MAX_WIDTH / w) & ~1;
				w = MAX_WIDTH;
			}
			// Common 4:2:0 formats without scaling: our SSE2 converter (swscale
			// took ~20 ms per 1080p 10-bit frame on PS4).
			const AVPixelFormat pf = (AVPixelFormat)frame->format;
			const bool fast = w <= frame->width && (frame->width & ~1) == w &&
					(pf == AV_PIX_FMT_YUV420P || pf == AV_PIX_FMT_YUVJ420P || pf == AV_PIX_FMT_NV12 ||
							pf == AV_PIX_FMT_YUV420P10LE);
			if (!fast) {
				sws = sws_getCachedContext(sws, frame->width, frame->height, pf, w, h, AV_PIX_FMT_RGBA, SWS_BILINEAR,
						nullptr, nullptr, nullptr);
				if (!sws) return;
			}
			// Rows padded to a multiple of 32 pixels: on PS4, uploading a
			// 1436-wide frame took ~50 ms, and 1.7 ms once padded to 1440
			// (Piglet has a slow path for other row widths).
			const int tw = (w + TEXTURE_ALIGN - 1) & ~(TEXTURE_ALIGN - 1);
			Frame f;
			f.pts = pts;
			f.width = tw;
			f.shown_width = w;
			f.height = h;
			{
				threads::Lock lock(mutex_);
				if (!pool_.empty()) {
					f.rgba = std::move(pool_.back());
					pool_.pop_back();
				}
			}
			f.rgba.resize((size_t)tw * h * 4);
			uint8_t *dst[4] = { f.rgba.data(), nullptr, nullptr, nullptr };
			int stride[4] = { tw * 4, 0, 0, 0 };
			const double t0 = platform::now();
			if (fast) {
				const bool bt709 = frame->colorspace == AVCOL_SPC_BT709 ||
						(frame->colorspace == AVCOL_SPC_UNSPECIFIED && frame->height >= 720);
				const bool full = frame->color_range == AVCOL_RANGE_JPEG || pf == AV_PIX_FMT_YUVJ420P;
				const size_t ys = frame->linesize[0], cs = frame->linesize[1];
				if (pf == AV_PIX_FMT_NV12) {
					nv12::convert_rows(nv12::Nv12{ frame->data[0], frame->data[1], ys, cs }, w, 0, h, bt709, full, f.rgba.data(), tw);
				} else if (pf == AV_PIX_FMT_YUV420P10LE) {
					nv12::convert_rows(nv12::P10{ frame->data[0], frame->data[1], frame->data[2], ys, cs }, w, 0, h, bt709, full,
							f.rgba.data(), tw);
				} else {
					nv12::convert_rows(nv12::I420{ frame->data[0], frame->data[1], frame->data[2], ys, cs }, w, 0, h, bt709, full,
							f.rgba.data(), tw);
				}
			} else {
				sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, stride);
			}
			// The padding repeats each row's last pixel, so filtering at the
			// picture's right edge doesn't pull in garbage.
			if (tw > w) {
				for (int y = 0; y < h; y++) {
					uint32_t *row = (uint32_t *)(f.rgba.data() + (size_t)y * tw * 4);
					std::fill(row + w, row + tw, row[w - 1]);
				}
			}
			convert_time += platform::now() - t0;
			if (++frames_converted % 300 == 0) {
				platform::log("ffmpeg: %d frames, avg convert %.1f ms, avg upload %.1f ms", frames_converted,
						convert_time * 1000 / frames_converted, upload_time_ * 1000 / std::max(1, uploads_.load()));
			}
			threads::Lock lock(mutex_);
			if (gen == generation_) {
				frames_.push_back(std::move(f));
			} else {
				recycle(f);
			}
		};
		std::vector<int16_t> audio_buf;
		auto on_audio = [&]() {
			const double pts = pts_of(ai);
			if (!swr) {
				AVChannelLayout out = AV_CHANNEL_LAYOUT_STEREO;
				if (swr_alloc_set_opts2(&swr, &out, AV_SAMPLE_FMT_S16, RATE, &frame->ch_layout,
							(AVSampleFormat)frame->format, frame->sample_rate, 0, nullptr) < 0 || swr_init(swr) < 0) {
					swr_free(&swr);
					return;
				}
			}
			const int max_out = swr_get_out_samples(swr, frame->nb_samples);
			if (max_out <= 0) return;
			audio_buf.resize((size_t)max_out * CHANNELS);
			uint8_t *out[1] = { reinterpret_cast<uint8_t *>(audio_buf.data()) };
			const int got = swr_convert(swr, out, max_out, (const uint8_t **)frame->extended_data, frame->nb_samples);
			if (got <= 0) return;
			const double end = pts + got / (double)RATE;
			if (pts >= 0 && end < skip_until) return;
			threads::Lock lock(mutex_);
			if (gen != generation_ || !audio_ok_) return;
			audio_.write(audio_buf.data(), got);
			audio_end_ = end;
		};
		auto on_subtitle = [&]() {
			AVSubtitle sub;
			int got = 0;
			if (avcodec_decode_subtitle2(sdec, &sub, &got, pkt) < 0 || !got) return;
			const double tb = av_q2d(fmt->streams[si]->time_base);
			const double pts = (sub.pts != AV_NOPTS_VALUE ? sub.pts / (double)AV_TIME_BASE : pkt->pts * tb) - offset;
			Cue cue;
			cue.start = pts + sub.start_display_time / 1000.0;
			if (sub.end_display_time > sub.start_display_time && sub.end_display_time != UINT32_MAX) {
				cue.end = pts + sub.end_display_time / 1000.0;
			} else if (pkt->duration > 0) {
				cue.end = pts + pkt->duration * tb;
			}
			bool bitmap = false;
			int x0 = INT32_MAX, y0 = INT32_MAX, x1 = 0, y1 = 0;
			for (unsigned i = 0; i < sub.num_rects; i++) {
				const AVSubtitleRect *r = sub.rects[i];
				if (r->type == SUBTITLE_BITMAP && r->w > 0 && r->h > 0) {
					bitmap = true;
					x0 = std::min(x0, r->x);
					y0 = std::min(y0, r->y);
					x1 = std::max(x1, r->x + r->w);
					y1 = std::max(y1, r->y + r->h);
				} else {
					const std::string line = r->type == SUBTITLE_ASS ? ass_text(r->ass) : (r->text ? r->text : "");
					if (!line.empty()) cue.text += (cue.text.empty() ? "" : "\n") + line;
				}
			}
			if (bitmap) {
				// Composite the palette images into one RGBA image.
				cue.w = x1 - x0;
				cue.h = y1 - y0;
				cue.rgba.assign((size_t)cue.w * cue.h * 4, 0);
				for (unsigned i = 0; i < sub.num_rects; i++) {
					const AVSubtitleRect *r = sub.rects[i];
					if (r->type != SUBTITLE_BITMAP || r->w <= 0 || r->h <= 0) continue;
					const uint32_t *pal = (const uint32_t *)r->data[1]; // 0xAARRGGBB
					for (int y = 0; y < r->h; y++) {
						const uint8_t *src = r->data[0] + (size_t)y * r->linesize[0];
						uint8_t *dst = cue.rgba.data() + ((size_t)(r->y - y0 + y) * cue.w + (r->x - x0)) * 4;
						for (int x = 0; x < r->w; x++, dst += 4) {
							const uint32_t c = pal[src[x]];
							dst[0] = (uint8_t)(c >> 16);
							dst[1] = (uint8_t)(c >> 8);
							dst[2] = (uint8_t)c;
							dst[3] = (uint8_t)(c >> 24);
						}
					}
				}
				const int fw = sdec->width > 0 ? sdec->width : (vdec ? vdec->width : 1920);
				const int fh = sdec->height > 0 ? sdec->height : (vdec ? vdec->height : 1080);
				cue.x = (float)x0 / fw;
				cue.y = (float)y0 / fh;
				cue.fw = (float)cue.w / fw;
				cue.fh = (float)cue.h / fh;
			}
			avsubtitle_free(&sub);
			threads::Lock lock(mutex_);
			if (gen != generation_) return;
			// An image cue (or an empty "clear" event) ends the open-ended one before.
			for (Cue &c : cues_) {
				if (std::isinf(c.end) && c.start < cue.start) c.end = cue.start;
			}
			if (cue.text.empty() && cue.rgba.empty()) return;
			cue.id = ++cue_ids_;
			cues_.push_back(std::move(cue));
		};
		auto open_subtitles = [&](int index) {
			avcodec_free_context(&sdec);
			si = -1;
			if (index < 0 || index >= (int)fmt->nb_streams) return;
			const AVCodec *codec = avcodec_find_decoder(fmt->streams[index]->codecpar->codec_id);
			if (!codec) return;
			sdec = avcodec_alloc_context3(codec);
			avcodec_parameters_to_context(sdec, fmt->streams[index]->codecpar);
			sdec->pkt_timebase = fmt->streams[index]->time_base;
			if (avcodec_open2(sdec, codec, nullptr) < 0) {
				avcodec_free_context(&sdec);
				return;
			}
			si = index;
		};
		auto drain = [&](AVCodecContext *ctx, bool video) {
			while (avcodec_receive_frame(ctx, frame) >= 0) {
				video ? on_video() : on_audio();
				av_frame_unref(frame);
			}
		};

		if ((err = avformat_find_stream_info(fmt, nullptr)) < 0) {
			fail("Couldn't read the stream: " + av_error(err));
			goto done;
		}
		if (fmt->start_time != AV_NOPTS_VALUE) offset = fmt->start_time / (double)AV_TIME_BASE;
		if (fmt->duration > 0) duration_ = fmt->duration / (double)AV_TIME_BASE;
		vi = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
		// Audio: the track marked default, else the first (FFmpeg's "best" picks
		// the most channels, e.g. a dubbed 5.1 over the original stereo).
		for (unsigned i = 0; i < fmt->nb_streams; i++) {
			const AVStream *st = fmt->streams[i];
			if (st->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) continue;
			if (ai < 0) ai = (int)i;
			if (st->disposition & AV_DISPOSITION_DEFAULT) {
				ai = (int)i;
				break;
			}
		}
		if (vi >= 0 && !open_decoder(vi, vdec)) vi = -1;
		if (ai >= 0 && !open_decoder(ai, adec)) ai = -1;
		if (vi < 0 && ai < 0) {
			fail("No playable video or audio in this stream");
			goto done;
		}
		platform::log("ffmpeg: opened %s in %.1f s: %s, video %s %dx%d, audio %s %d ch, %.0f s", urls::for_log(url_).c_str(),
				platform::now() - t_open, fmt->iformat->name, vi >= 0 ? avcodec_get_name(vdec->codec_id) : "-",
				vi >= 0 ? vdec->width : 0, vi >= 0 ? vdec->height : 0, ai >= 0 ? avcodec_get_name(adec->codec_id) : "-",
				ai >= 0 ? adec->ch_layout.nb_channels : 0, duration_.load());
		has_audio_ = ai >= 0 && audio_ok_;
		{
			threads::Lock lock(mutex_);
			for (unsigned i = 0; i < fmt->nb_streams; i++) {
				const AVStream *st = fmt->streams[i];
				const AVDictionaryEntry *lang = av_dict_get(st->metadata, "language", nullptr, 0);
				Track t;
				t.id = (int)i;
				t.lang = lang ? lang->value : "";
				if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
					t.label = track_label(st, (int)audio_tracks_.size() + 1);
					audio_tracks_.push_back(t);
				}
				if (st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE && avcodec_find_decoder(st->codecpar->codec_id)) {
					t.label = track_label(st, (int)sub_tracks_.size() + 1);
					sub_tracks_.push_back(t);
				}
			}
		}
		audio_sel_ = ai;
		// A saved position past the end (e.g. from a longer stream of the same
		// title) would leave nothing to play: start over instead.
		if (duration_ > 0 && start > duration_ - 5) {
			platform::log("ffmpeg: resume at %.0f s is past the end (%.0f s); starting over", start, duration_.load());
			start = 0;
			skip_until = 0;
			audio_end_ = 0;
			wall_pts_ = 0;
		}
		if (start > 0) do_seek(start);
		opened_ = true;

		while (!abort_) {
			const int wa = want_audio_.exchange(-2);
			if (wa >= 0 && wa != ai) {
				avcodec_free_context(&adec);
				swr_free(&swr); // the new track may have another layout / rate
				ai = wa;
				if (!open_decoder(ai, adec)) ai = -1;
				audio_sel_ = ai;
				has_audio_ = ai >= 0 && audio_ok_;
				platform::log("ffmpeg: audio track %d", ai);
			}
			const int ws = want_sub_.exchange(-2);
			if (ws != -2) {
				open_subtitles(ws);
				platform::log("ffmpeg: subtitle track %d", si);
			}
			if (seek_requested_.exchange(false)) {
				gen = generation_;
				do_seek(seek_target_);
			}
			bool full;
			{
				threads::Lock lock(mutex_);
				const double queued = audio_ok_ ? audio_.queued() : 0;
				full = frames_.size() >= MAX_FRAMES || (has_audio_ && queued >= MAX_QUEUED_AUDIO);
			}
			if (full || eof_) {
				usleep(5000);
				continue;
			}
			err = av_read_frame(fmt, pkt);
			if (err < 0) {
				if (err == AVERROR_EOF || avio_feof(fmt->pb)) {
					if (vdec) { avcodec_send_packet(vdec, nullptr); drain(vdec, true); avcodec_flush_buffers(vdec); }
					if (adec) { avcodec_send_packet(adec, nullptr); drain(adec, false); avcodec_flush_buffers(adec); }
					eof_ = true;
					continue;
				}
				if (!abort_) {
					const std::string why = reader_ ? reader_->error() : "";
					fail("Stream interrupted: " + (why.empty() ? av_error(err) : why));
				}
				break;
			}
			if (pkt->stream_index == vi && avcodec_send_packet(vdec, pkt) >= 0) drain(vdec, true);
			else if (pkt->stream_index == ai && avcodec_send_packet(adec, pkt) >= 0) drain(adec, false);
			else if (pkt->stream_index == si && sdec) on_subtitle();
			av_packet_unref(pkt);
		}
	done:
		sws_freeContext(sws);
		swr_free(&swr);
		avcodec_free_context(&vdec);
		avcodec_free_context(&adec);
		avcodec_free_context(&sdec);
		avformat_close_input(&fmt);
		av_packet_free(&pkt);
		av_frame_free(&frame);
		free_io(io);
	}

	void free_io(AVIOContext *io) {
		if (io) {
			av_freep(&io->buffer);
			avio_context_free(&io);
		}
		threads::Lock lock(mutex_);
		reader_.reset();
	}

	std::atomic<State> state_{ IDLE };
	std::string error_;
	std::atomic<bool> want_playing_{ true };
	Texture textures_[3];
	int tex_idx_ = 0;
	AudioOut audio_;
	bool audio_ok_ = false;

	std::string url_;
	std::vector<std::string> headers_;
	double start_ = 0;
	std::unique_ptr<ByteSource> reader_; // guarded by mutex_ for reset/abort
	int64_t io_pos_ = 0;

	std::atomic<bool> running_{ false };
	mutable threads::Mutex mutex_;
	std::deque<Frame> frames_;
	Frame shown_;
	std::vector<std::vector<uint8_t>> pool_;
	std::atomic<bool> abort_{ false }, opened_{ false }, failed_{ false }, eof_{ false }, has_audio_{ false };
	std::atomic<bool> seek_requested_{ false };
	std::atomic<double> seek_target_{ 0 }, duration_{ 0 }, audio_end_{ 0 };
	std::atomic<uint32_t> generation_{ 0 };
	// Tracks: lists guarded by mutex_; selections are stream indexes.
	std::vector<Track> audio_tracks_, sub_tracks_;
	std::atomic<int> audio_sel_{ -1 }, sub_sel_{ -1 };
	std::atomic<int> want_audio_{ -2 }, want_sub_{ -2 }; // -2: no change requested
	std::deque<Cue> cues_; // embedded subtitles, guarded by mutex_
	uint32_t cue_ids_ = 0, sub_texture_cue_ = 0;
	Texture sub_texture_;
	std::string thread_error_;
	bool show_next_ = true;
	// Wall clock, for media without audio.
	std::atomic<double> wall_pts_{ 0 };
	double wall_start_ = 0;
	std::atomic<bool> wall_running_{ false };
	// Timing stats.
	double upload_time_ = 0;
	std::atomic<int> uploads_{ 0 };
};

} // namespace

std::unique_ptr<VideoPlayer> create_ffmpeg_player() {
	return std::unique_ptr<VideoPlayer>(new FfmpegPlayer());
}
