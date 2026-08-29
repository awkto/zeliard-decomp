/* png.h — minimal PNG writer (stored deflate blocks) for headless screenshots. */
#ifndef ZEL_PNG_H
#define ZEL_PNG_H
#include <stdint.h>
/* rgb: width*height*3 bytes, row-major.  Returns 0 on success. */
int png_write_rgb(const char *path, const uint8_t *rgb, int width, int height);
#endif
