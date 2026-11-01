/* present.h — display filters for the SDL presentation path (issue #53).
 *
 * The engine's framebuffer is never touched: these are pure sampling choices
 * applied when a mode's RGB image is blitted to the window, so `make verify`
 * (which renders headlessly through video_to_rgb) cannot see them.  The three
 * filters differ only in how the source is sampled into the aspect-corrected
 * content rectangle:
 *
 *   integer  the largest integer multiple of the *source* framebuffer that
 *            fits the window, nearest-neighbour, centred -- a purist's uniform
 *            pixel grid.  This is the one filter that does not use the display
 *            aspect: 4:3 would need 200 -> 240, which is not an integer.
 *   sharp    an integer pre-scale of the source into an intermediate render
 *            target (nearest), then linear to the display rectangle; the
 *            "sharp-bilinear" most fidelity-first ports ship;
 *   smooth   plain linear sampling straight to the display rectangle.
 *
 * The display size (dw x dh) is the aspect-corrected shape from
 * video_display_size(), or the framebuffer itself under `--aspect 1:1`.       */
#ifndef ZEL_PRESENT_H
#define ZEL_PRESENT_H
#include <stdint.h>

enum { FILTER_INTEGER = 0, FILTER_SHARP, FILTER_SMOOTH, FILTER_COUNT };

/* --filter spellings: integer | sharp | sharp-bilinear | smooth.  -1 unknown. */
int         present_filter_by_name(const char *name);
const char *present_filter_name(int filter);   /* "integer" / "sharp-bilinear" / "smooth" */

/* The rectangle the picture occupies inside a win_w x win_h window, in window
 * pixels, centred.  `sw`/`sh` is the source framebuffer, `dw`/`dh` the display
 * shape.  FILTER_INTEGER uses the largest integer multiple of sw x sh that
 * fits (falling back to a fit when the window is smaller than 1:1); the others
 * use the largest dw:dh box that fits. */
void present_content_rect(int filter, int win_w, int win_h,
                          int sw, int sh, int dw, int dh,
                          int *x, int *y, int *w, int *h);

/* The integer pre-scale factor sharp-bilinear uses: floor of the scale from a
 * sw x sh source to the cw x ch content rectangle, at least 1. */
int  present_prescale(int cw, int ch, int sw, int sh);

#ifdef HAVE_SDL
struct SDL_Renderer;
struct SDL_Texture;
/* Blit `src` (a sw x sh texture already filled with this frame) to `ren`
 * through `filter`, into a dw x dh display shape.  Returns 0, or -1 with
 * SDL_GetError() set.  The intermediate render target sharp-bilinear needs is
 * owned here; present_shutdown() releases it. */
int  present_render(struct SDL_Renderer *ren, struct SDL_Texture *src,
                    int sw, int sh, int dw, int dh, int filter);
void present_shutdown(void);
#endif

#endif
