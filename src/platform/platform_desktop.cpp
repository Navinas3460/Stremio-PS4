// Desktop platform: SDL2 window with an OpenGL ES 2 context. Used to develop
// and check the UI quickly on a PC; the PS4 build uses platform_ps4.cpp.
//
// Development builds (cmake -DSTREMIO_DEV=ON) can run scripted sessions:
//   STREMIO_PS4_SCRIPT="wait 30; right; right; down; wait 20; shot /tmp/a.png; quit"
// Steps run one after another; button steps press and release a button,
// `wait N` waits N frames, `shot PATH` saves the window as PNG.

#include "platform.h"

#include "../render/gl.h"

#include <SDL2/SDL.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdarg>
#include <ctime>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "stb_image.h" // implementation in net/images.cpp

#if defined(STREMIO_DEV)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#endif

namespace {

SDL_Window *g_window = nullptr;
SDL_GLContext g_context = nullptr;
SDL_GameController *g_pad = nullptr;
int g_logical_w = 1920, g_logical_h = 1080;
std::string g_base_path;

#if defined(STREMIO_DEV)
// Scripted input (development builds)
std::vector<std::string> g_script;
size_t g_script_pos = 0;
int g_script_wait = 0;
uint32_t g_script_held = 0;
bool g_script_quit = false;
std::string g_pending_shot;

uint32_t button_for_word(const std::string &w) {
	if (w == "up") return platform::BUTTON_UP;
	if (w == "down") return platform::BUTTON_DOWN;
	if (w == "left") return platform::BUTTON_LEFT;
	if (w == "right") return platform::BUTTON_RIGHT;
	if (w == "accept") return platform::BUTTON_ACCEPT;
	if (w == "back") return platform::BUTTON_BACK;
	if (w == "l1") return platform::BUTTON_L1;
	if (w == "r1") return platform::BUTTON_R1;
	if (w == "l2") return platform::BUTTON_L2;
	if (w == "r2") return platform::BUTTON_R2;
	if (w == "triangle") return platform::BUTTON_TRIANGLE;
	if (w == "square") return platform::BUTTON_SQUARE;
	return 0;
}

void load_script() {
	const char *s = getenv("STREMIO_PS4_SCRIPT");
	if (!s) {
		return;
	}
	std::stringstream ss(s);
	std::string step;
	while (std::getline(ss, step, ';')) {
		size_t a = step.find_first_not_of(" \t");
		size_t b = step.find_last_not_of(" \t");
		if (a != std::string::npos) {
			g_script.push_back(step.substr(a, b - a + 1));
		}
	}
}

// Advances the script by one frame; returns buttons it holds this frame.
uint32_t run_script() {
	if (g_script_held) { // release a pressed button for a frame
		g_script_held = 0;
		g_script_wait = 6;
		return 0;
	}
	if (g_script_wait > 0) {
		g_script_wait--;
		return 0;
	}
	if (g_script_pos >= g_script.size()) {
		return 0;
	}
	const std::string step = g_script[g_script_pos++];
	if (step.rfind("wait ", 0) == 0) {
		g_script_wait = atoi(step.c_str() + 5);
	} else if (step.rfind("shot ", 0) == 0) {
		g_pending_shot = step.substr(5);
	} else if (step == "quit") {
		g_script_quit = true;
	} else {
		g_script_held = button_for_word(step);
	}
	return g_script_held;
}

void save_screenshot(const std::string &path) {
	int w = 0, h = 0;
	SDL_GL_GetDrawableSize(g_window, &w, &h);
	std::vector<unsigned char> px((size_t)w * h * 4);
	glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
	std::vector<unsigned char> flipped(px.size());
	for (int y = 0; y < h; y++) {
		memcpy(&flipped[(size_t)y * w * 4], &px[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
	}
	stbi_write_png(path.c_str(), w, h, 4, flipped.data(), w * 4);
	platform::log("screenshot saved: %s (%dx%d)", path.c_str(), w, h);
}
#else
const bool g_script_quit = false;
std::string g_pending_shot;
void load_script() {}
uint32_t run_script() {
	return 0;
}
void save_screenshot(const std::string &) {}
#endif

// GLSL equivalents of Sony's precompiled "swordfish" UI shaders, with the
// same attribute/uniform names (introspected on PS4 FW 11.02).
const char *VP_BASIC = R"(
attribute vec4 aPosition;
attribute vec2 aTextureCoord;
uniform mat4 uMMatrix;
uniform mat4 uVPMatrix;
varying vec2 vTextureCoord;
void main() {
	gl_Position = uVPMatrix * uMMatrix * aPosition;
	vTextureCoord = aTextureCoord;
}
)";

const std::map<std::string, const char *> FRAGMENT_SHADERS = {
	{ "swordfish/fpSolidColor.frag", R"(
precision mediump float;
uniform vec4 fillColor;
void main() { gl_FragColor = fillColor; }
)" },
	{ "swordfish/fpAlphaTexture.frag", R"(
precision mediump float;
uniform sampler2D texture;
uniform float alpha;
varying vec2 vTextureCoord;
void main() {
	vec4 c = texture2D(texture, vTextureCoord);
	gl_FragColor = vec4(c.rgb, c.a * alpha);
}
)" },
	{ "swordfish/fpAlphaColor.frag", R"(
precision mediump float;
uniform sampler2D texture;
uniform vec4 fillColor;
varying vec2 vTextureCoord;
void main() {
	// Matches the PS4's precompiled shader, which ignores fillColor's alpha
	// (seen on hardware: translucent rounded corners came out opaque).
	gl_FragColor = vec4(fillColor.rgb, texture2D(texture, vTextureCoord).a);
}
)" },
};

GLuint compile(GLenum type, const char *src, const char *name) {
	GLuint sh = glCreateShader(type);
	glShaderSource(sh, 1, &src, nullptr);
	glCompileShader(sh);
	GLint ok = 0;
	glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char info[1024] = {};
		glGetShaderInfoLog(sh, sizeof(info), nullptr, info);
		platform::log("shader %s failed: %s", name, info);
		glDeleteShader(sh);
		return 0;
	}
	return sh;
}

} // namespace

namespace platform {

bool init(int width, int height) {
	g_logical_w = width;
	g_logical_h = height;
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
		log("SDL_Init failed: %s", SDL_GetError());
		return false;
	}
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
	const char *size = getenv("STREMIO_PS4_WINDOW");
	int ww = 1280, wh = 720;
	if (size) {
		sscanf(size, "%dx%d", &ww, &wh);
	}
	g_window = SDL_CreateWindow("Stremio PS4 (desktop)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh,
			SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
	if (!g_window) {
		log("SDL_CreateWindow failed: %s", SDL_GetError());
		return false;
	}
	g_context = SDL_GL_CreateContext(g_window);
	if (!g_context) {
		log("SDL_GL_CreateContext failed: %s", SDL_GetError());
		return false;
	}
	SDL_GL_SetSwapInterval(1);
	char *base = SDL_GetBasePath();
	g_base_path = base ? base : "./";
	SDL_free(base);
	// Window icon: the PS4 tile (sce_sys/icon0.png, copied next to the exe).
	int iw = 0, ih = 0, n = 0;
	if (uint8_t *px = stbi_load((g_base_path + "icon0.png").c_str(), &iw, &ih, &n, 4)) {
		if (SDL_Surface *icon = SDL_CreateRGBSurfaceWithFormatFrom(px, iw, ih, 32, iw * 4, SDL_PIXELFORMAT_RGBA32)) {
			SDL_SetWindowIcon(g_window, icon);
			SDL_FreeSurface(icon);
		}
		stbi_image_free(px);
	}
	log("GL_VERSION=%s", glGetString(GL_VERSION));
	load_script();
	return true;
}

bool poll(uint32_t &held) {
	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		if (e.type == SDL_QUIT) {
			return false;
		}
		if (e.type == SDL_CONTROLLERDEVICEADDED && !g_pad) {
			g_pad = SDL_GameControllerOpen(e.cdevice.which);
		}
	}
	const Uint8 *k = SDL_GetKeyboardState(nullptr);
	held = 0;
	if (k[SDL_SCANCODE_UP]) held |= BUTTON_UP;
	if (k[SDL_SCANCODE_DOWN]) held |= BUTTON_DOWN;
	if (k[SDL_SCANCODE_LEFT]) held |= BUTTON_LEFT;
	if (k[SDL_SCANCODE_RIGHT]) held |= BUTTON_RIGHT;
	if (k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_SPACE]) held |= BUTTON_ACCEPT;
	if (k[SDL_SCANCODE_ESCAPE] || k[SDL_SCANCODE_BACKSPACE]) held |= BUTTON_BACK;
	if (k[SDL_SCANCODE_Q] || k[SDL_SCANCODE_PAGEUP]) held |= BUTTON_L1;
	if (k[SDL_SCANCODE_E] || k[SDL_SCANCODE_PAGEDOWN]) held |= BUTTON_R1;
	if (k[SDL_SCANCODE_Z]) held |= BUTTON_L2;
	if (k[SDL_SCANCODE_C]) held |= BUTTON_R2;
	if (k[SDL_SCANCODE_T]) held |= BUTTON_TRIANGLE;
	if (k[SDL_SCANCODE_F]) held |= BUTTON_SQUARE;
	if (g_pad) {
		auto b = [](SDL_GameControllerButton btn) { return SDL_GameControllerGetButton(g_pad, btn) != 0; };
		if (b(SDL_CONTROLLER_BUTTON_DPAD_UP)) held |= BUTTON_UP;
		if (b(SDL_CONTROLLER_BUTTON_DPAD_DOWN)) held |= BUTTON_DOWN;
		if (b(SDL_CONTROLLER_BUTTON_DPAD_LEFT)) held |= BUTTON_LEFT;
		if (b(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) held |= BUTTON_RIGHT;
		if (b(SDL_CONTROLLER_BUTTON_A)) held |= BUTTON_ACCEPT;
		if (b(SDL_CONTROLLER_BUTTON_B)) held |= BUTTON_BACK;
		if (b(SDL_CONTROLLER_BUTTON_START)) held |= BUTTON_OPTIONS;
		if (b(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) held |= BUTTON_L1;
		if (b(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) held |= BUTTON_R1;
		if (SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000)
			held |= BUTTON_L2;
		if (SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000)
			held |= BUTTON_R2;
		if (b(SDL_CONTROLLER_BUTTON_Y)) held |= BUTTON_TRIANGLE;
		if (b(SDL_CONTROLLER_BUTTON_X)) held |= BUTTON_SQUARE;
	}
	held |= run_script();
	return true;
}

void present() {
	if (!g_pending_shot.empty()) {
		save_screenshot(g_pending_shot);
		g_pending_shot.clear();
	}
	SDL_GL_SwapWindow(g_window);
	// Cap at 60 fps like the PS4 build (the offscreen driver has no vsync, and
	// test scripts count frames).
	static Uint64 last = SDL_GetPerformanceCounter();
	const double frame = 1.0 / 60.0;
	const double elapsed = (SDL_GetPerformanceCounter() - last) / (double)SDL_GetPerformanceFrequency();
	if (elapsed < frame) {
		SDL_Delay((Uint32)((frame - elapsed) * 1000.0));
	}
	last = SDL_GetPerformanceCounter();
}

double now() {
	return SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
}

std::string asset_path(const std::string &relative) {
	return g_base_path + "assets/" + relative;
}

void viewport(int &x, int &y, int &w, int &h) {
	int dw = 0, dh = 0;
	SDL_GL_GetDrawableSize(g_window, &dw, &dh);
	// Letterbox the 16:9 logical space into the window.
	const float target = (float)g_logical_w / g_logical_h;
	if ((float)dw / dh > target) {
		h = dh;
		w = (int)(dh * target);
	} else {
		w = dw;
		h = (int)(dw / target);
	}
	x = (dw - w) / 2;
	y = (dh - h) / 2;
}

void log(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "[stremio_ps4] ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}

unsigned int load_program(const char *vertex_name, const char *fragment_name) {
	auto it = FRAGMENT_SHADERS.find(fragment_name);
	if (strcmp(vertex_name, "swordfish/vpBasic.vert") != 0 || it == FRAGMENT_SHADERS.end()) {
		log("no desktop GLSL for %s + %s", vertex_name, fragment_name);
		return 0;
	}
	GLuint vs = compile(GL_VERTEX_SHADER, VP_BASIC, vertex_name);
	GLuint fs = compile(GL_FRAGMENT_SHADER, it->second, fragment_name);
	if (!vs || !fs) {
		return 0;
	}
	GLuint prog = glCreateProgram();
	glAttachShader(prog, vs);
	glAttachShader(prog, fs);
	// Same attribute locations Piglet reports for the precompiled shaders.
	glBindAttribLocation(prog, 0, "aPosition");
	glBindAttribLocation(prog, 1, "aTextureCoord");
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
	return g_script_quit;
}

std::string data_dir() {
	// STREMIO_PS4_DATA lets tests use a throwaway folder.
	std::string dir;
	if (const char *d = getenv("STREMIO_PS4_DATA")) {
		dir = d;
	} else {
		const char *x = getenv("XDG_DATA_HOME");
		const std::string base = x ? x : std::string(getenv("HOME") ? getenv("HOME") : ".") + "/.local/share";
		dir = base + "/stremio_ps4";
		struct stat st;
		if (stat(dir.c_str(), &st) != 0) rename((base + "/stremio_tv").c_str(), dir.c_str()); // the app's earlier name
		chmod(dir.c_str(), 0700); // earlier versions made it 0755
	}
	// Private to this user: the folder holds the Stremio session (auth key).
	mkdir(dir.c_str(), 0700);
	return dir + "/";
}

int64_t unix_time() {
	return (int64_t)time(nullptr);
}

bool read_file(const std::string &path, std::string &out) {
	std::ifstream f(path, std::ios::binary);
	if (!f) return false;
	out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	return true;
}

bool write_file(const std::string &path, const std::string &data) {
	const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (fd < 0) return false;
	fchmod(fd, 0600); // files written by earlier versions were 0644
	size_t done = 0;
	while (done < data.size()) {
		const ssize_t n = ::write(fd, data.data() + done, data.size() - done);
		if (n <= 0) break;
		done += (size_t)n;
	}
	return ::close(fd) == 0 && done == data.size();
}

bool remove_file(const std::string &path) {
	return ::remove(path.c_str()) == 0;
}

} // namespace platform
