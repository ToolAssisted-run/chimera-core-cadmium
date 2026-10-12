/* A stand-in for raylib, for the parts of Cadmium this core compiles outside
 * its emulation library (the program loader and the table of known
 * programs). They use raylib for log lines and one colour conversion and for
 * nothing else that this core calls; there is no window here. */
#ifndef CHIMERA_CADMIUM_RAYLIB_SHIM_H
#define CHIMERA_CADMIUM_RAYLIB_SHIM_H

enum { LOG_ALL = 0, LOG_TRACE, LOG_DEBUG, LOG_INFO, LOG_WARNING, LOG_ERROR, LOG_FATAL, LOG_NONE };

typedef struct Color { unsigned char r, g, b, a; } Color;

static inline void TraceLog(int, const char*, ...) {}
static inline int ColorToInt(Color c) { return (int)(((unsigned)c.r << 24) | ((unsigned)c.g << 16) | ((unsigned)c.b << 8) | (unsigned)c.a); }

#endif
