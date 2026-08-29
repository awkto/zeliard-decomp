/* compat/SDL_config.h — generic build config so the public SDL2 headers compile on a box that has
 * libSDL2-2.0.so.0 but no libsdl2-dev; only used through local.mk (see README). */
#ifndef SDL_config_h_
#define SDL_config_h_
#include "SDL_platform.h"
#define HAVE_LIBC 1
#define STDC_HEADERS 1
#define HAVE_STDINT_H 1
#define HAVE_STDDEF_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_STDARG_H 1
#define HAVE_MATH_H 1
#define HAVE_CTYPE_H 1
#define HAVE_SYS_TYPES_H 1
#endif
