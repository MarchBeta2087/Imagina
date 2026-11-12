#ifndef _PLATFORM_H_
#define _PLATFORM_H_

#include <cstddef>
#include <cstdint>
#include <string>

// Platform interface.  Each front-end implements the first group, this
// repository implements the second group in PlatformMemory.cpp.

// Returns the address of an OpenGL entry point beyond OpenGL 1.1, or nullptr.
// Implemented with wglGetProcAddress() by the Win32 front-end and with
// SDL_GL_GetProcAddress() by the SDL front-end.
void *PlatformGetGLProcAddress(const char *Name);

// Shows the rendered frame (SwapBuffers() / SDL_GL_SwapWindow()).
void PlatformPresent();

// Breaks into the debugger when one is attached, aborts otherwise.
void PlatformDebugBreak();

// Reserve/commit memory, like VirtualAlloc(MEM_RESERVE) followed by
// VirtualAlloc(MEM_COMMIT): the reservation is address space only, the commit
// makes it usable.  Release frees both.
void *PlatformReserveMemory(size_t Size);
bool PlatformCommitMemory(void *Address, size_t Size);
void PlatformDecommitMemory(void *Address, size_t Size);
void PlatformReleaseMemory(void *Address, size_t Size);

// File choosers.  Filters are "Description(*.ext)|*.ext" pairs separated by
// '|', like the Win32 filter string.  Path is UTF-8, FilterIndex is 1 based.
// Both return false when the dialog was cancelled.
bool PlatformOpenFileDialog(const char *Title, const char *Filters, std::string &Path, int &FilterIndex);
bool PlatformSaveFileDialog(const char *Title, const char *Filters, std::string &Path, int &FilterIndex);

// Writable and executable memory for the JIT evaluator.
void *PlatformAllocateExecutableMemory(size_t Size);

#endif
