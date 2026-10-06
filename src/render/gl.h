// OpenGL ES 2 headers for the current platform.
#pragma once

#if defined(STREMIO_PS4)
#include <stdint.h>             // Pigletv2VSH.h uses fixed-width types without including this
#include <orbis/Pigletv2VSH.h> // Piglet: GLES2 + EGL
#else
#include <GLES2/gl2.h>
#endif
