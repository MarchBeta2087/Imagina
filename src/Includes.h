#ifndef _INCLUDES_H_
#define _INCLUDES_H_

#include "Configuration.h"

#ifdef _WIN32
#define NOMINMAX
#if __has_include(<Windows.h>)
#include <Windows.h>
#else
#include <windows.h>
#endif

#define GLEW_STATIC
#endif
#if defined(__APPLE__)
// macOS has no GL/gl.h unless XQuartz is installed; the framework provides the
// 3.2 core profile.
#include <OpenGL/gl3.h>
#elif __has_include(<GL/gl.h>)
#include <GL/gl.h>
#elif __has_include(<GLES3/gl3.h>)
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>
#else
#error OpenGL Headers not found
#endif

// The extension entry points are loaded at run time (GLUtils.cpp), so only the
// PFN...PROC typedefs are needed here, never the prototypes.
#if defined(_WIN32)
// The headers come from vcpkg's opengl-registry port, from include/ (see
// tools/fetch-headers.ps1) or from the MinGW-w64 toolchain; the Windows SDK only
// ships gl.h.
#if __has_include(<glext.h>)
#include <glext.h>
#elif __has_include(<GL/glext.h>)
#include <GL/glext.h>
#else
#error "GL/glext.h is missing: run tools/fetch-headers.ps1 (or tools/fetch-headers.sh) to download the Khronos headers into include/, or build with vcpkg, where the opengl-registry port provides them."
#endif
#if __has_include(<wglext.h>)
#include <wglext.h>
#elif __has_include(<GL/wglext.h>)
#include <GL/wglext.h>
#else
#error "GL/wglext.h is missing: run tools/fetch-headers.ps1 (or tools/fetch-headers.sh) to download the Khronos headers into include/, or build with vcpkg, where the opengl-registry port provides them."
#endif
#elif __has_include(<GL/glext.h>)
#include <GL/glext.h>
#elif __has_include(<OpenGL/glext.h>)
#include <OpenGL/glext.h>
#endif

#ifdef GL_TEXTURE_RECTANGLE
#define USE_GL_TEXTURE_RECTANGLE
#endif

#ifdef USE_GL_TEXTURE_RECTANGLE
#define TARGET_TEXTRUE GL_TEXTURE_RECTANGLE
#else
#define TARGET_TEXTRUE GL_TEXTURE_2D
#endif

#define unused [[maybe_unused]]

#if defined(_MSC_VER)
#define IMAGINA_ALWAYS_INLINE __forceinline
#define IMAGINA_NOINLINE __declspec(noinline)
#else
#define IMAGINA_ALWAYS_INLINE inline __attribute__((always_inline))
#define IMAGINA_NOINLINE __attribute__((noinline))
#endif

#include <thread>
#include <stddef.h>
#include <stdint.h>
#include <chrono>
#include <iostream>
#include <glm/glm.hpp>

#include "PlatformDependent.h"
#include "Types.h"
#include "Global.h"
#include "Computation.h"
#include "FractalContext.h"
#include "Evaluator.h"
#include "HInfLAEvaluator.h"
#include "JitEvaluator.h"
#include "Render.h"
#include "Platform.h"
#include "Version.h"
#include "WindowState.h"

inline uint32_t CountLeadingZeros(uint32_t n) {
#if defined(__GNUC__) || defined(__clang__)
	return __builtin_clz(n);
#elif defined(_WIN32)
	return _lzcnt_u32(n);
#else
#error Unsupported compiler
#endif
}

#define if_likely(...) if (__VA_ARGS__) [[likely]]
#define if_unlikely(...) if (__VA_ARGS__) [[unlikely]]

#endif