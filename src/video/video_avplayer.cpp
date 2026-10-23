// PS4 hardware video backend: sceAvPlayer (MP4 / H.264) + sceAudioOut.
// Optional: FFmpeg (video_ffmpeg.cpp) is the default; this one is chosen in
// Settings. What it relies on (verified on FW 11.02):
//   - AddSource(https URL) fails, so AvPlayer reads the stream through its
//     file-replacement callbacks. They're answered on our own 1 MiB-stack
//     thread from a ByteSource (AvPlayer's threads have tiny stacks, and
//     libSceHttp fails there with INSUFFICIENT_STACKSIZE).
//   - Decoded video is NV12 from GetVideoDataEx (pitch = align64(width),
//     height = align16, UV plane right after the aligned height). There is no
//     YUV shader on Piglet, so frames are converted to RGBA on a CPU thread.
//   - Video frames only flow while audio is pulled at real-time pace, so an
//     audio thread feeds sceAudioOutOutput (which blocks for one grain).
//   - With autoStart the first event is PLAY (no READY); STOP at the end.
#include "video_player.h"

#include "nv12.h"

#include "../net/byte_source.h"
#include "../platform/platform.h"
#include "../platform/threads.h"
#include "avplayer_api.h"

#include <orbis/AudioOut.h>
#include <orbis/Sysmodule.h>
#include <orbis/libkernel.h>

#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

// The H.264 decoder's pixel reconstruction thread (libSceVdecSavc) calls into
// libSceGnmDriver, which every game has loaded but this Piglet app didn't: it
// crashed with PRX_NOT_RESOLVED_FUNCTION. init_once() loads it, and VideoOut
// (this reference keeps VideoOut imported).
extern "C" void sceVideoOutGetResolutionStatus();
__attribute__((used)) void *const g_force_imports[] = { (void *)&sceVideoOutGetResolutionStatus };

namespace {

const size_t THREAD_STACK = 1024 * 1024;
// RGBA frame buffers live in direct memory (the libc heap size is unknown
// on PS4 and the frames are big). When PS4 Pro 4K mode is active, buffers
// accommodate up to 3840x2160 native frames.
const int BASE_MAX_FRAME_W = 1920, BASE_MAX_FRAME_H = 1088;
const int PRO_MAX_FRAME_W = 3840, PRO_MAX_FRAME_H = 2160;
const size_t BASE_FRAME_BYTES = (size_t)BASE_MAX_FRAME_W * BASE_MAX_FRAME_H * 4;
const size_t PRO_FRAME_BYTES = (size_t)PRO_MAX_FRAME_W * PRO_MAX_FRAME_H * 4;
const size_t IO_STACK = 1024 * 1024;
const uint32_t AUDIO_GRAIN = 1024;       // frames per sceAudioOutOutput
const double OPEN_TIMEOUT = 45.0;        // seconds without a first PLAY event

uint32_t align_up(uint32_t v, uint32_t a) {
	return (v + a - 1) / a * a;
}

std::string hex(int32_t code) {
	char buf[16];
	snprintf(buf, sizeof(buf), "0x%08X", (uint32_t)code);
	return buf;
}

// --- AvPlayer memory -------------------------------------------------------
// Decoder output ("texture") memory must be direct memory; CPU-cached Onion
// lets our conversion thread read it quickly.

struct DirectBlock {
	off_t phys;
	size_t size;
};
std::map<uintptr_t, DirectBlock> g_direct;
threads::Mutex g_direct_mutex;

void *direct_alloc(uint32_t alignment, uint32_t size);

// General decoder memory. The libc heap shares the app's flexible memory with
// Piglet and may be small, so large blocks come from direct memory (plenty
// is free), and a failed heap allocation falls back to it as well. Returning
// NULL here likely crashes AvPlayer.
const uint32_t DIRECT_THRESHOLD = 256 * 1024;

// (The callbacks run on AvPlayer threads with tiny stacks: no logging here.)
void *av_alloc(void *, uint32_t alignment, uint32_t size) {
	void *p = nullptr;
	if (size < DIRECT_THRESHOLD &&
			posix_memalign(&p, std::max<size_t>(alignment, sizeof(void *)), size) == 0 && p) {
		return p;
	}
	return direct_alloc(alignment, size);
}

bool direct_free(void *p);

void av_free(void *, void *p) {
	if (p && !direct_free(p)) free(p);
}

void *av_alloc_texture(void *, uint32_t alignment, uint32_t size) {
	return direct_alloc(alignment, size);
}

void av_free_texture(void *, void *p) {
	direct_free(p);
}

void *direct_alloc(uint32_t alignment, uint32_t size) {
	const size_t align = std::max<size_t>(alignment, 0x10000);
	const size_t sz = align_up(size, 0x10000);
	off_t phys = 0;
	int rc = sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), sz, align, ORBIS_KERNEL_WB_ONION, &phys);
	if (rc != 0) return nullptr;
	void *addr = nullptr;
	rc = sceKernelMapDirectMemory(&addr, sz, ORBIS_KERNEL_PROT_CPU_RW | ORBIS_KERNEL_PROT_GPU_RW, 0, phys, align);
	if (rc != 0) {
		sceKernelReleaseDirectMemory(phys, sz);
		return nullptr;
	}
	threads::Lock lock(g_direct_mutex);
	g_direct[(uintptr_t)addr] = DirectBlock{ phys, sz };
	return addr;
}

bool direct_free(void *p) {
	threads::Lock lock(g_direct_mutex);
	auto it = g_direct.find((uintptr_t)p);
	if (it == g_direct.end()) return false;
	sceKernelMunmap(p, it->second.size);
	sceKernelReleaseDirectMemory(it->second.phys, it->second.size);
	g_direct.erase(it);
	return true;
}

// --- HTTP-backed file -------------------------------------------------------
// AvPlayer calls the file callbacks on its own threads; each call is handed
// to one I/O thread and waited for. The RangeReader downloads (and reads
// ahead) on threads of its own.

struct Stream {
	Stream(const std::string &url, const std::vector<std::string> &headers) : reader(ByteSource::create(url, headers)) {}
	std::unique_ptr<ByteSource> reader; // RangeReader or TorrentStream

	threads::Mutex error_mutex;
	std::string error; // user-facing reason the stream can't be read
	void fail(const std::string &message) {
		threads::Lock lock(error_mutex);
		if (error.empty()) error = message;
		platform::log("avplayer: stream error: %s", message.c_str());
	}
	std::string get_error() {
		{
			threads::Lock lock(error_mutex);
			if (!error.empty()) return error;
		}
		return reader->error();
	}
};

int do_open(Stream &s) {
	if (!s.reader->open()) {
		if (s.reader->error().empty()) s.fail("Couldn't open the stream");
		return -1;
	}
	// Look at the container: AvPlayer only plays MP4 (and HLS via AddSourceEx).
	uint8_t d[12];
	if (s.reader->read(0, d, sizeof(d)) != (int)sizeof(d)) {
		s.fail("Couldn't read the video");
		return -1;
	}
	if (d[0] == 0x1A && d[1] == 0x45 && d[2] == 0xDF && d[3] == 0xA3) {
		s.fail("MKV videos can't be played by the PS4 player - choose FFmpeg in Settings");
		return -1;
	}
	if (memcmp(d, "#EXTM3U", 7) == 0) {
		s.fail("HLS streams aren't supported yet");
		return -1;
	}
	if (memcmp(d + 4, "ftyp", 4) != 0) {
		platform::log("avplayer: no 'ftyp' at the start; trying anyway");
	}
	return 0;
}

int do_read(Stream &s, uint8_t *buffer, uint64_t position, uint32_t length) {
	uint32_t done = 0;
	while (done < length) {
		const int n = s.reader->read(position + done, buffer + done, (int)(length - done));
		if (n < 0) return done > 0 ? (int)done : -1;
		if (n == 0) break; // end of the file
		done += (uint32_t)n;
	}
	return (int)done;
}

enum JobKind { JOB_OPEN, JOB_READ, JOB_CLOSE };
struct Job {
	JobKind kind;
	Stream *stream;
	uint8_t *buffer;
	uint64_t position;
	uint32_t length;
	int result;
	bool done;
};

threads::Mutex g_io_mutex;      // guards the fields below
threads::CondVar g_io_wake, g_io_done;
Job *g_job = nullptr;
threads::Mutex g_call_mutex;    // one callback at a time
bool g_io_started = false;

void *io_thread(void *) {
	g_io_mutex.lock();
	for (;;) {
		while (!g_job) g_io_wake.wait(g_io_mutex);
		Job *job = g_job;
		g_job = nullptr;
		g_io_mutex.unlock();
		switch (job->kind) {
			case JOB_OPEN: job->result = do_open(*job->stream); break;
			case JOB_READ: job->result = do_read(*job->stream, job->buffer, job->position, job->length); break;
			case JOB_CLOSE: job->result = 0; break; // the round trip waits for any read in progress
		}
		g_io_mutex.lock();
		job->done = true;
		g_io_done.notify_all();
	}
	return nullptr;
}

int run_job(Job job) {
	threads::Lock call(g_call_mutex);
	threads::Lock lock(g_io_mutex);
	g_job = &job;
	g_io_wake.notify_one();
	while (!job.done) g_io_done.wait(g_io_mutex);
	return job.result;
}

int fr_open(void *p, const char *) {
	return run_job(Job{ JOB_OPEN, (Stream *)p, nullptr, 0, 0, -1, false });
}

int fr_read(void *p, uint8_t *buffer, uint64_t position, uint32_t length) {
	return run_job(Job{ JOB_READ, (Stream *)p, buffer, position, length, -1, false });
}

int fr_close(void *p) {
	return run_job(Job{ JOB_CLOSE, (Stream *)p, nullptr, 0, 0, -1, false });
}

uint64_t fr_size(void *p) {
	return ((Stream *)p)->reader->size();
}

// sceAudioOutInit crashes unless its module was loaded first.
bool g_audio_ok = false;

void init_audio() {
	sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_AUDIOOUT);
	const int rc = sceAudioOutInit();
	g_audio_ok = rc == 0 || (uint32_t)rc == 0x8026000E; // already initialised
	if (!g_audio_ok) platform::log("avplayer: sceAudioOutInit failed 0x%08X", rc);
}

bool init_once() {
	static bool done = false, ok = false;
	if (done) return ok;
	done = true;
	// Libraries the H.264 decoder needs (see g_force_imports). In this app the
	// loader loads nothing by itself, so load them explicitly.
	platform::log("avplayer: load VideoOut module -> 0x%08X",
			sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_VIDEO_OUT));
	// libSceVdecSavc imports only from libkernel, libc and libSceGnmDriver
	// (checked in its import table): GnmDriver is the one this app lacks.
	platform::load_gnm_driver();
	const int rc = sceSysmoduleLoadModule(ORBIS_SYSMODULE_AV_PLAYER);
	platform::log("avplayer: load module -> 0x%08X", rc);
	init_audio();
	g_io_started = threads::start(io_thread, nullptr, IO_STACK, "stremio_video_io");
	ok = rc == 0 && g_io_started;
	return ok;
}

// --- Player -----------------------------------------------------------------------

class AvPlayerVideo : public VideoPlayer {
public:
	~AvPlayerVideo() override { close(); }

	void open(const std::string &url, const std::vector<std::string> &headers, double start) override {
		close();
		error_.clear();
		state_ = OPENING;
		open_time_ = platform::now();
		start_ms_ = (uint64_t)(std::max(0.0, start) * 1000);
		if (!init_once()) {
			fail("The PS4 video player couldn't start");
			return;
		}
		stream_ = new Stream(url, headers);

		SceAvPlayerInitData init;
		memset(&init, 0, sizeof(init));
		init.memoryReplacement.allocate = av_alloc;
		init.memoryReplacement.deallocate = av_free;
		init.memoryReplacement.allocateTexture = av_alloc_texture;
		init.memoryReplacement.deallocateTexture = av_free_texture;
		init.fileReplacement.objectPointer = stream_;
		init.fileReplacement.open = fr_open;
		init.fileReplacement.close = fr_close;
		init.fileReplacement.readOffset = fr_read;
		init.fileReplacement.size = fr_size;
		init.eventReplacement.objectPointer = this;
		init.eventReplacement.eventCallback = on_event;
		init.basePriority = 700;
		init.numOutputVideoFrameBuffers = 2;
		init.autoStart = 1;
		init.defaultLanguage = "eng";
		handle_ = sceAvPlayerInit(&init);
		if (!handle_) {
			fail("The PS4 video player couldn't start");
			return;
		}
		// The name only reaches fr_open (which ignores it); keep a .mp4 ending
		// in case AvPlayer looks at it.
		std::string name = url;
		const std::string path = url.substr(0, url.find_first_of("?#"));
		if (path.size() < 4 || path.compare(path.size() - 4, 4, ".mp4") != 0) name += "#stremio.mp4";
		const int rc = sceAvPlayerAddSource(handle_, name.c_str());
		platform::log("avplayer: AddSource -> 0x%08X", rc);
		if (rc < 0) {
			const std::string reason = stream_->get_error();
			fail(reason.empty() ? "This video can't be played (" + hex(rc) + ")" : reason);
			return;
		}
		is_pro_4k_ = VideoPlayer::pro_4k_selected();
		max_w_ = is_pro_4k_ ? PRO_MAX_FRAME_W : BASE_MAX_FRAME_W;
		max_h_ = is_pro_4k_ ? PRO_MAX_FRAME_H : BASE_MAX_FRAME_H;
		frame_bytes_ = is_pro_4k_ ? PRO_FRAME_BYTES : BASE_FRAME_BYTES;
		platform::log("avplayer: frame buffers -> max %dx%d (%zu MB, PS4 Pro 4K: %s)",
				max_w_, max_h_, (frame_bytes_ * 3) / (1024 * 1024), is_pro_4k_ ? "ON" : "OFF");
		for (int i = 0; i < 3; i++) {
			if (!buffers_[i]) buffers_[i] = (uint8_t *)av_alloc_texture(nullptr, 0x10000, (uint32_t)frame_bytes_);
			if (!buffers_[i]) {
				fail("Not enough memory for video frames");
				return;
			}
		}
		if (g_audio_ok) audio_port_ = sceAudioOutOpen(ORBIS_USER_SERVICE_USER_ID_SYSTEM, ORBIS_AUDIO_OUT_PORT_TYPE_MAIN, 0, AUDIO_GRAIN,
				48000, ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
		stop_ = false;
		threads_running_ = 2;
		threads::start(audio_entry, this, THREAD_STACK, "stremio_audio");
		threads::start(video_entry, this, THREAD_STACK, "stremio_video");
	}

	void play() override {
		user_paused_ = false;
		if (handle_ && started_ && state_ == PAUSED) {
			sceAvPlayerResume(handle_);
			state_ = PLAYING;
		}
	}

	void pause() override {
		user_paused_ = true;
		if (handle_ && started_ && (state_ == PLAYING || state_ == BUFFERING)) {
			sceAvPlayerPause(handle_);
			state_ = PAUSED;
		}
	}

	void seek(double seconds) override {
		const uint64_t ms = (uint64_t)(std::max(0.0, seconds) * 1000);
		if (!handle_ || !started_) {
			start_ms_ = ms;
			return;
		}
		jump(ms);
	}

	void update() override {
		if (state_ == IDLE || state_ == FAILED) return;
		handle_events();
		if (state_ == OPENING) {
			const std::string reason = stream_ ? stream_->get_error() : "";
			if (!reason.empty()) {
				fail(reason);
				return;
			}
			if (platform::now() - open_time_ > OPEN_TIMEOUT) {
				fail("The video took too long to start");
				return;
			}
		}
		if (started_ && state_ != ENDED) {
			const uint64_t now_ms = sceAvPlayerCurrentTime(handle_);
			// Leave BUFFERING once time moves again (no event is guaranteed).
			if (state_ == BUFFERING && now_ms != last_time_ms_) state_ = user_paused_ ? PAUSED : PLAYING;
			last_time_ms_ = now_ms;
			if (!sceAvPlayerIsActive(handle_)) state_ = ENDED;
		}
		upload_ready_frame();
	}

	State state() const override { return state_; }
	double position() const override {
		if (!handle_ || !started_) return start_ms_ / 1000.0;
		return sceAvPlayerCurrentTime(handle_) / 1000.0;
	}
	double duration() const override { return duration_; }
	std::string error() const override { return error_; }
	std::string source_status() const override { return stream_ ? stream_->reader->status() : std::string(); }
	const Texture &frame() const override { return textures_[tex_idx_]; }

private:
	void fail(const std::string &message) {
		platform::log("avplayer: failed: %s", message.c_str());
		error_ = message;
		state_ = FAILED;
	}

	void jump(uint64_t ms) {
		sceAvPlayerJumpToTime(handle_, ms);
		// Hide frames decoded before the jump (they can still be queued).
		skip_until_ms_ = ms > 10000 ? ms - 10000 : 0;
	}

	// Runs on an AvPlayer thread with a tiny stack: logging there crashed the
	// app. Only record the event in a fixed ring; update() handles it.
	static void on_event(void *p, int32_t id, int32_t, void *data) {
		AvPlayerVideo *self = (AvPlayerVideo *)p;
		const uint32_t slot = self->event_write_.load() % EVENT_RING;
		self->event_ids_[slot] = id;
		self->event_extra_[slot] = (id == SCE_AVPLAYER_WARNING_ID && data) ? *(int32_t *)data : 0;
		self->event_write_++;
	}

	void handle_events() {
		std::vector<int32_t> events;
		const uint32_t end = event_write_.load();
		if (end - event_read_ > EVENT_RING) event_read_ = end - EVENT_RING; // overflowed: keep the newest
		for (; event_read_ != end; event_read_++) {
			const uint32_t slot = event_read_ % EVENT_RING;
			if (event_ids_[slot] == SCE_AVPLAYER_WARNING_ID) {
				platform::log("avplayer: warning 0x%08X", event_extra_[slot].load());
			}
			events.push_back(event_ids_[slot]);
		}
		for (int32_t id : events) {
			switch (id) {
				case SCE_AVPLAYER_STATE_READY:
				case SCE_AVPLAYER_STATE_PLAY:
					if (!started_) on_started();
					if (user_paused_) {
						sceAvPlayerPause(handle_);
						state_ = PAUSED;
					} else if (state_ != ENDED) {
						state_ = PLAYING;
					}
					break;
				case SCE_AVPLAYER_STATE_BUFFERING:
					if (started_ && state_ != PAUSED) state_ = BUFFERING;
					break;
				case SCE_AVPLAYER_STATE_PAUSE:
					if (started_) state_ = PAUSED;
					break;
				case SCE_AVPLAYER_STATE_STOP:
					if (started_) {
						state_ = ENDED;
					} else {
						const std::string reason = stream_ ? stream_->get_error() : "";
						fail(reason.empty() ? "This video can't be played on PS4" : reason);
						return;
					}
					break;
				default: break;
			}
		}
	}

	void on_started() {
		started_ = true;
		const int count = sceAvPlayerStreamCount(handle_);
		for (int i = 0; i < count && i < 16; i++) {
			SceAvPlayerStreamInfo info;
			memset(&info, 0, sizeof(info));
			if (sceAvPlayerGetStreamInfo(handle_, i, &info) < 0) continue;
			duration_ = std::max(duration_, info.duration / 1000.0);
			if (info.type == SCE_AVPLAYER_VIDEO) {
				platform::log("avplayer: video %ux%u, %.0f s", info.details.video.width, info.details.video.height,
						info.duration / 1000.0);
			} else if (info.type == SCE_AVPLAYER_AUDIO) {
				platform::log("avplayer: audio %u ch, %u Hz", info.details.audio.channelCount, info.details.audio.sampleRate);
			}
		}
		// A saved position past the end (e.g. it came from a longer stream of
		// the same title) would make the jump fail and nothing play.
		if (duration_ > 0 && start_ms_ / 1000.0 > duration_ - 5) {
			platform::log("avplayer: resume at %.0f s is past the end (%.0f s); starting over", start_ms_ / 1000.0, duration_);
			start_ms_ = 0;
		}
		if (start_ms_ > 0) jump(start_ms_);
	}

	void upload_ready_frame() {
		int tw = 0, sw = 0, h = 0;
		{
			threads::Lock lock(frame_mutex_);
			if (!frame_ready_) return;
			frame_ready_ = false;
			std::swap(front_, ready_);
			tw = ready_tw_;
			sw = ready_w_;
			h = ready_h_;
		}
		tex_idx_ = (tex_idx_ + 1) % 3;
		Texture &tex = textures_[tex_idx_];
		if (tex.valid() && tex.width == tw && tex.height == h) {
			glBindTexture(GL_TEXTURE_2D, tex.id);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tw, h, GL_RGBA, GL_UNSIGNED_BYTE, buffers_[front_]);
		} else {
			if (tex.valid()) glDeleteTextures(1, &tex.id);
			tex = make_texture(tw, h, buffers_[front_]);
		}
		tex.shown_width = sw;
		glFlush();
	}

	static void *audio_entry(void *p) {
		((AvPlayerVideo *)p)->audio_loop();
		return nullptr;
	}

	static void *video_entry(void *p) {
		((AvPlayerVideo *)p)->video_loop();
		return nullptr;
	}

	// Pulls one decoded audio chunk and plays whole grains of it. Playing
	// blocks (sceAudioOutOutput waits for the previous grain), which paces
	// the decoder; without a port the caller paces. Returns the ms pulled.
	double pull_audio(std::vector<int16_t> &pending) {
		SceAvPlayerFrameInfo af;
		memset(&af, 0, sizeof(af));
		if (!sceAvPlayerGetAudioData(handle_, &af) || !af.pData) return 0;
		const int ch = af.details.audio.channelCount ? af.details.audio.channelCount : 2;
		const size_t frames = af.details.audio.size / (2 * ch);
		const int16_t *src = (const int16_t *)af.pData;
		for (size_t i = 0; i < frames; i++) {
			const int16_t l = src[i * ch], r = ch > 1 ? src[i * ch + 1] : l;
			pending.push_back(l);
			pending.push_back(r);
		}
		size_t consumed = 0;
		while (pending.size() - consumed >= AUDIO_GRAIN * 2 && !stop_) {
			if (audio_port_ >= 0) sceAudioOutOutput(audio_port_, pending.data() + consumed);
			consumed += AUDIO_GRAIN * 2;
		}
		pending.erase(pending.begin(), pending.begin() + consumed);
		const uint32_t rate = af.details.audio.sampleRate ? af.details.audio.sampleRate : 48000;
		return frames * 1000.0 / rate;
	}

	// Pulls one decoded video frame (if one is due) and converts it.
	bool pull_video(int &back, int &frames) {
		SceAvPlayerFrameInfoEx vf;
		memset(&vf, 0, sizeof(vf));
		if (!sceAvPlayerGetVideoDataEx(handle_, &vf) || !vf.pData) return false;
		if (vf.timeStamp < skip_until_ms_) return true;
		skip_until_ms_ = 0;
		const SceAvPlayerVideoEx &v = vf.details.video;
		const uint32_t pitch = v.pitch ? v.pitch : align_up(v.width, 64);
		const int w = (int)(v.width - v.cropLeftOffset - v.cropRightOffset) & ~1;
		const int h = (int)(v.height - v.cropTopOffset - v.cropBottomOffset) & ~1;
		if (++frames == 1) platform::log("avplayer: video stream %ux%u (4K mode: %s)", v.width, v.height, is_pro_4k_ ? "active" : "disabled");
		if (w <= 0 || h <= 0 || w > max_w_ || h > max_h_) {
			if (w > max_w_ || h > max_h_) {
				platform::log("avplayer: frame %dx%d exceeds max %dx%d (enable 4K in Settings)", w, h, max_w_, max_h_);
			}
			return true;
		}
		const int tw = (w + 31) & ~31;
		const uint8_t *y_plane = (const uint8_t *)vf.pData;
		const uint8_t *uv_plane = y_plane + (size_t)pitch * v.height;
		y_plane += (size_t)v.cropTopOffset * pitch + (v.cropLeftOffset & ~1u);
		uv_plane += (size_t)(v.cropTopOffset / 2) * pitch + (v.cropLeftOffset & ~1u);
		nv12::convert_rows(nv12::Nv12{ y_plane, uv_plane, pitch, pitch }, w, 0, h, v.height >= 720, v.videoFullRangeFlag != 0,
				buffers_[back], tw);
		if (tw > w) {
			for (int y = 0; y < h; y++) {
				uint32_t *row = (uint32_t *)(buffers_[back] + (size_t)y * tw * 4);
				std::fill(row + w, row + tw, row[w - 1]);
			}
		}
		threads::Lock lock(frame_mutex_);
		std::swap(ready_, back);
		ready_tw_ = tw;
		ready_w_ = w;
		ready_h_ = h;
		frame_ready_ = true;
		return true;
	}

	void audio_loop() {
		std::vector<int16_t> pending; // interleaved stereo
		while (!stop_) {
			if (!started_ || state_ == PAUSED) {
				usleep(10000);
				continue;
			}
			const double ms = pull_audio(pending);
			if (ms <= 0) {
				usleep(3000);
			} else if (audio_port_ < 0) {
				usleep((useconds_t)(ms * 1000));
			}
		}
		threads_running_--;
	}

	void video_loop() {
		int back = 2, frames = 0;
		while (!stop_) {
			if (!started_ || !pull_video(back, frames)) usleep(2000);
		}
		threads_running_--;
	}

	void close() {
		stop_ = true;
		for (int i = 0; i < 300 && threads_running_ > 0; i++) usleep(10000);
		if (threads_running_ > 0) platform::log("avplayer: player threads didn't stop");
		if (stream_) stream_->reader->abort();
		if (handle_) {
			sceAvPlayerStop(handle_);
			sceAvPlayerClose(handle_); // calls fr_close
			handle_ = nullptr;
		}
		if (audio_port_ >= 0) {
			sceAudioOutClose(audio_port_);
			audio_port_ = -1;
		}
		if (stream_) {
			// An empty job round trip: the I/O thread is done with the stream.
			run_job(Job{ JOB_CLOSE, stream_, nullptr, 0, 0, -1, false });
			delete stream_;
			stream_ = nullptr;
		}
		for (Texture &t : textures_) {
			if (t.valid()) {
				glDeleteTextures(1, &t.id);
				t = Texture();
			}
		}
		tex_idx_ = 0;
		event_read_ = event_write_.load();
		if (threads_running_ == 0) {
			for (uint8_t *&b : buffers_) {
				if (b) av_free_texture(nullptr, b);
				b = nullptr;
			}
		}
		frame_ready_ = false;
		started_ = false;
		user_paused_ = false;
		duration_ = 0;
		state_ = IDLE;
	}

	SceAvPlayerHandle handle_ = nullptr;
	Stream *stream_ = nullptr;
	int audio_port_ = -1;

	std::atomic<State> state_{ IDLE };
	std::string error_;
	double open_time_ = 0;
	uint64_t start_ms_ = 0;
	std::atomic<uint64_t> skip_until_ms_{ 0 };
	uint64_t last_time_ms_ = 0;
	double duration_ = 0;
	std::atomic<bool> started_{ false }, user_paused_{ false }, stop_{ true };
	std::atomic<int> threads_running_{ 0 };

	static const uint32_t EVENT_RING = 32;
	std::atomic<int32_t> event_ids_[EVENT_RING] = {}, event_extra_[EVENT_RING] = {};
	std::atomic<uint32_t> event_write_{ 0 };
	uint32_t event_read_ = 0;

	threads::Mutex frame_mutex_;
	bool is_pro_4k_ = false;
	int max_w_ = BASE_MAX_FRAME_W;
	int max_h_ = BASE_MAX_FRAME_H;
	size_t frame_bytes_ = BASE_FRAME_BYTES;
	uint8_t *buffers_[3] = { nullptr, nullptr, nullptr };
	int ready_ = 0, front_ = 1; // indices; the video thread owns the third
	int ready_tw_ = 0, ready_w_ = 0, ready_h_ = 0;
	bool frame_ready_ = false;
	Texture textures_[3];
	int tex_idx_ = 0;

};

} // namespace

std::unique_ptr<VideoPlayer> create_avplayer_player() {
	return std::unique_ptr<VideoPlayer>(new AvPlayerVideo());
}
