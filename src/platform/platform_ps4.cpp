// PS4 platform (OpenOrbis homebrew): Piglet GPU context, DualShock 4 input,
// precompiled shaders, logging and files. Verified on FW 11.02.

#include "platform.h"
#include "threads.h"

#include "../render/gl.h"

#include <orbis/Pad.h>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>
#include <orbis/libkernel.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

// Only callable once libSceGnmDriver is loaded (see load_gnm_driver).
extern "C" int32_t sceGnmSubmitDone();
bool g_gnm_loaded = false;

// SceSystemServiceStatus (Sony SDK layout; the OpenOrbis header has no
// signature). Padded generously.
struct SystemServiceStatus {
	int32_t event_count;
	bool system_ui_overlaid;
	bool in_background;
	bool cpu_mode7_normal;
	bool live_streaming;
	bool out_of_vr_area;
	uint8_t reserved[251];
};
extern "C" int32_t system_service_get_status(SystemServiceStatus *status) __asm__("sceSystemServiceGetStatus");
bool g_system_service = false;

namespace {

const char *LOG_PATH = "/data/stremio_ps4.log";
const double FRAME_SECONDS = 1.0 / 60.0;

FILE *g_log = nullptr;
EGLDisplay g_display = EGL_NO_DISPLAY;
EGLSurface g_surface = EGL_NO_SURFACE;
EGLContext g_context = EGL_NO_CONTEXT;
int g_width = 1920, g_height = 1080;
int g_pad = -1;
double g_last_present = 0.0;

// Table exported by libScePrecompiledShaders (layout from the OpenOrbis
// Piglet sample).
struct PrecompiledShader {
	char *ident;
	unsigned char hash[16];
	uint64_t len;
	unsigned char *code;
};
extern "C" PrecompiledShader scePrecompiledShaderEntries[];
std::map<std::string, PrecompiledShader *> g_shaders;

GLuint load_binary(GLenum type, const char *name) {
	auto it = g_shaders.find(name);
	if (it == g_shaders.end()) {
		platform::log("precompiled shader %s not found", name);
		return 0;
	}
	GLuint sh = glCreateShader(type);
	glShaderBinary(1, &sh, 0, it->second->code, (GLsizei)it->second->len);
	if (glGetError() != GL_NO_ERROR) {
		platform::log("glShaderBinary(%s) failed", name);
		glDeleteShader(sh);
		return 0;
	}
	return sh;
}

bool init_gpu() {
	const std::string prefix = std::string("/") + sceKernelGetFsSandboxRandomWord() + "/common/lib/";
	int start_rc = 0;
	int piglet = (int)sceKernelLoadStartModule((prefix + "libScePigletv2VSH.sprx").c_str(), 0, nullptr, 0, nullptr, &start_rc);
	int shaders = (int)sceKernelLoadStartModule((prefix + "libScePrecompiledShaders.sprx").c_str(), 0, nullptr, 0, nullptr, &start_rc);
	platform::log("Piglet module 0x%08X, precompiled shaders module 0x%08X", piglet, shaders);
	if (piglet < 0) {
		return false;
	}
	for (PrecompiledShader *e = scePrecompiledShaderEntries; e && e->ident; ++e) {
		g_shaders[e->ident] = e;
	}

	OrbisPglConfig cfg;
	memset(&cfg, 0, sizeof(cfg));
	cfg.size = sizeof(cfg);
	cfg.flags = ORBIS_PGL_FLAGS_USE_COMPOSITE_EXT | ORBIS_PGL_FLAGS_USE_FLEXIBLE_MEMORY | 0x60;
	cfg.processOrder = 1;
	// Piglet reserves this as direct memory up front. The OpenOrbis sample's
	// 250 + 512 MB left none for the video decoder (AvPlayer's allocations
	// failed with 0x80020023); the UI needs far less (image cache <= ~150 MB).
	cfg.systemSharedMemorySize = 128 * 1024 * 1024;
	cfg.videoSharedMemorySize = 256 * 1024 * 1024;
	cfg.maxMappedFlexibleMemory = 170 * 1024 * 1024;
	cfg.drawCommandBufferSize = 1 * 1024 * 1024;
	cfg.lcueResourceBufferSize = 1 * 1024 * 1024;
	cfg.dbgPosCmd_0x40 = g_width;
	cfg.dbgPosCmd_0x44 = g_height;
	cfg.unk_0x5C = 2;
	if (!scePigletSetConfigurationVSH(&cfg)) {
		platform::log("scePigletSetConfigurationVSH failed");
		return false;
	}
	g_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	EGLint major = 0, minor = 0;
	if (g_display == EGL_NO_DISPLAY || !eglInitialize(g_display, &major, &minor)) {
		platform::log("EGL init failed 0x%X", eglGetError());
		return false;
	}
	eglBindAPI(EGL_OPENGL_ES_API);
	eglSwapInterval(g_display, 0); // with vsync on, eglSwapBuffers blocks forever on Piglet
	const EGLint attribs[] = { EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_NONE };
	EGLConfig config;
	EGLint n = 0;
	if (!eglChooseConfig(g_display, attribs, &config, 1, &n) || n < 1) {
		platform::log("no EGL config");
		return false;
	}
	static OrbisPglWindow window = { 0, (khronos_uint32_t)g_width, (khronos_uint32_t)g_height };
	const EGLint window_attribs[] = { EGL_RENDER_BUFFER, EGL_BACK_BUFFER, EGL_NONE };
	g_surface = eglCreateWindowSurface(g_display, config, &window, window_attribs);
	const EGLint context_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	g_context = eglCreateContext(g_display, config, EGL_NO_CONTEXT, context_attribs);
	if (g_surface == EGL_NO_SURFACE || g_context == EGL_NO_CONTEXT || !eglMakeCurrent(g_display, g_surface, g_surface, g_context)) {
		platform::log("EGL surface/context failed 0x%X", eglGetError());
		return false;
	}
	platform::log("GL_VERSION=%s", glGetString(GL_VERSION));
	return true;
}

// Controller: with the Piglet privileges this app needs, sceUserServiceInitialize
// crashes unless the UserService and Pad modules are loaded first.
bool init_pad() {
	sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_USER_SERVICE);
	sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_PAD);
	OrbisUserServiceInitializeParams param;
	memset(&param, 0, sizeof(param));
	param.priority = ORBIS_KERNEL_PRIO_FIFO_LOWEST;
	sceUserServiceInitialize(&param);
	int user = ORBIS_USER_SERVICE_USER_ID_SYSTEM;
	sceUserServiceGetInitialUser(&user);
	scePadInit();
	g_pad = scePadOpen(user, 0, 0, nullptr);
	if (g_pad < 0) platform::log("pad: scePadOpen failed 0x%08X", g_pad);
	return g_pad >= 0;
}

} // namespace

namespace platform {

bool init(int width, int height) {
	g_log = fopen(LOG_PATH, "w");
	g_width = width;
	g_height = height;
	log("Stremio PS4 starting");
	// FreeType is a system module that must be loaded before FT_Init_FreeType.
	log("FreeType module: 0x%08X", sceSysmoduleLoadModule(ORBIS_SYSMODULE_FREETYPE_OL));
	const int ss = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE);
	g_system_service = ss == 0;
	log("SystemService module: 0x%08X", ss);
	// Controller first (the order of the OpenOrbis input sample), then GPU.
	init_pad(); // the UI still runs (without input) if this fails
	if (!init_gpu()) {
		return false;
	}
	g_last_present = now();
	return true;
}

bool poll(uint32_t &held) {
	held = 0;
	if (g_pad < 0) {
		return true;
	}
	OrbisPadData data;
	memset(&data, 0, sizeof(data));
	if (scePadReadState(g_pad, &data) != 0 || !data.connected) {
		return true;
	}
	const uint32_t b = data.buttons;
	if (b & ORBIS_PAD_BUTTON_UP) held |= BUTTON_UP;
	if (b & ORBIS_PAD_BUTTON_DOWN) held |= BUTTON_DOWN;
	if (b & ORBIS_PAD_BUTTON_LEFT) held |= BUTTON_LEFT;
	if (b & ORBIS_PAD_BUTTON_RIGHT) held |= BUTTON_RIGHT;
	if (b & ORBIS_PAD_BUTTON_CROSS) held |= BUTTON_ACCEPT;
	if (b & ORBIS_PAD_BUTTON_CIRCLE) held |= BUTTON_BACK;
	if (b & ORBIS_PAD_BUTTON_OPTIONS) held |= BUTTON_OPTIONS;
	if (b & ORBIS_PAD_BUTTON_L1) held |= BUTTON_L1;
	if (b & ORBIS_PAD_BUTTON_R1) held |= BUTTON_R1;
	if (b & ORBIS_PAD_BUTTON_L2) held |= BUTTON_L2;
	if (b & ORBIS_PAD_BUTTON_R2) held |= BUTTON_R2;
	if (b & ORBIS_PAD_BUTTON_TRIANGLE) held |= BUTTON_TRIANGLE;
	if (b & ORBIS_PAD_BUTTON_SQUARE) held |= BUTTON_SQUARE;
	// Left stick works like the D-pad.
	const int sx = (int)data.leftStick.x - 128, sy = (int)data.leftStick.y - 128;
	if (sx < -64) held |= BUTTON_LEFT;
	if (sx > 64) held |= BUTTON_RIGHT;
	if (sy < -64) held |= BUTTON_UP;
	if (sy > 64) held |= BUTTON_DOWN;
	return true;
}

bool load_gnm_driver() {
	if (g_gnm_loaded) return true;
	// System libraries live under the sandbox's random folder name.
	const std::string path = std::string("/") + sceKernelGetFsSandboxRandomWord() + "/common/lib/libSceGnmDriver.sprx";
	int rc = (int)sceKernelLoadStartModule(path.c_str(), 0, nullptr, 0, nullptr, nullptr);
	log("load %s -> 0x%08X", path.c_str(), rc);
	if (rc < 0) {
		rc = (int)sceKernelLoadStartModule("/system/common/lib/libSceGnmDriver.sprx", 0, nullptr, 0, nullptr, nullptr);
		log("load /system/common/lib/libSceGnmDriver.sprx -> 0x%08X", rc);
	}
	g_gnm_loaded = rc >= 0;
	return g_gnm_loaded;
}

// Once GnmDriver is loaded the system won't suspend (or close) the app until
// it calls sceGnmSubmitDone, but each call makes the kernel wait for the GPU
// to go idle, which froze playback when done every frame. Closing always
// starts with the app behind the system UI or in the background, so only
// signal then.
void gnm_suspend_check() {
	static int frame = 0;
	static bool away = false;
	if (!g_gnm_loaded || !g_system_service) return;
	if (++frame % 6 == 0) { // ~10 times a second
		SystemServiceStatus st;
		memset(&st, 0, sizeof(st));
		if (system_service_get_status(&st) == 0) {
			const bool now_away = st.system_ui_overlaid || st.in_background;
			if (now_away != away) log("system UI %s (overlaid %d, background %d)", now_away ? "over the app" : "gone",
					st.system_ui_overlaid, st.in_background);
			away = now_away;
		}
	}
	if (away) sceGnmSubmitDone();
}

void present() {
	eglSwapBuffers(g_display, g_surface);
	gnm_suspend_check();
	// Vsync is off (see init_gpu), so cap at 60 fps ourselves.
	const double elapsed = now() - g_last_present;
	if (elapsed < FRAME_SECONDS) {
		sceKernelUsleep((uint32_t)((FRAME_SECONDS - elapsed) * 1e6));
	}
	g_last_present = now();
}

double now() {
	return sceKernelGetProcessTime() / 1e6;
}

std::string asset_path(const std::string &relative) {
	return "/app0/assets/" + relative;
}

void viewport(int &x, int &y, int &w, int &h) {
	x = 0;
	y = 0;
	w = g_width;
	h = g_height;
}

// Formats into a static buffer under a lock: callers include threads with
// small stacks (system library callbacks).
void log(const char *fmt, ...) {
	static threads::Mutex mutex;
	static char buf[1024];
	threads::Lock lock(mutex);
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	printf("[stremio_ps4] %s\n", buf);
	if (g_log) {
		fprintf(g_log, "%s\n", buf);
		fflush(g_log);
	}
}

unsigned int load_program(const char *vertex_name, const char *fragment_name) {
	GLuint vs = load_binary(GL_VERTEX_SHADER, vertex_name);
	GLuint fs = load_binary(GL_FRAGMENT_SHADER, fragment_name);
	if (!vs || !fs) {
		return 0;
	}
	GLuint prog = glCreateProgram();
	glAttachShader(prog, vs);
	glAttachShader(prog, fs);
	glLinkProgram(prog);
	glDeleteShader(vs);
	glDeleteShader(fs);
	GLint ok = 0;
	glGetProgramiv(prog, GL_LINK_STATUS, &ok);
	if (!ok) {
		log("link %s + %s failed", vertex_name, fragment_name);
		glDeleteProgram(prog);
		return 0;
	}
	return prog;
}

bool should_quit() {
	return false;
}

std::string data_dir() {
	// Versions named "Stremio TV" used /data/stremio_tv: move it, keeping the
	// session and settings.
	OrbisKernelStat st;
	if (sceKernelStat("/data/stremio_ps4", &st) != 0) sceKernelRename("/data/stremio_tv", "/data/stremio_ps4");
	sceKernelMkdir("/data/stremio_ps4", 0777); // fine if it exists
	return "/data/stremio_ps4/";
}

int64_t unix_time() {
	OrbisKernelTimeval tv;
	if (sceKernelGettimeofday(&tv) != 0) {
		return 0;
	}
	return (int64_t)tv.tv_sec;
}

bool read_file(const std::string &path, std::string &out) {
	FILE *f = fopen(path.c_str(), "rb");
	if (!f) return false;
	out.clear();
	char buf[16384];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
		out.append(buf, n);
	}
	fclose(f);
	return true;
}

bool write_file(const std::string &path, const std::string &data) {
	FILE *f = fopen(path.c_str(), "wb");
	if (!f) return false;
	const bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
	fclose(f);
	return ok;
}

bool remove_file(const std::string &path) {
	return ::remove(path.c_str()) == 0;
}

} // namespace platform
