#ifndef _WINDOW_STATE_H_
#define _WINDOW_STATE_H_

#include <cstddef>
#include <cstdint>

// Size of the drawing area in pixels, maintained by the front-end.
extern size_t WindowWidth;
extern size_t WindowHeight;

// Size the fractal image was set up with, used to pick the initial resolution.
extern size_t InitialWindowWidth;
extern size_t InitialWindowHeight;

// Cursor position in window coordinates, used by the feature finder.
extern int32_t MouseX;
extern int32_t MouseY;

#endif
