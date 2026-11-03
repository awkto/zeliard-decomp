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
 *   smooth   plain linear sampling straight to the display rectangle;
 *   crt      sharp-bilinear, then a multiply overlay (SDL_BLENDMODE_MOD, so it
 *            needs nothing beyond SDL2's 2D renderer): one dark band between
 *            every pair of source scanlines plus a faint RGB aperture-grille
 *            tint, both computed by the pure functions below.
 *
 * The display size (dw x dh) is the aspect-corrected shape from
 * video_display_size(), or the framebuffer itself under `--aspect 1:1`.       */
#ifndef ZEL_PRESENT_H
#define ZEL_PRESENT_H
#include <stdint.h>

enum { FILTER_INTEGER = 0, FILTER_SHARP, FILTER_SMOOTH, FILTER_CRT, FILTER_COUNT };

/* --filter spellings: integer | sharp | sharp-bilinear | smooth | crt.  -1 unknown. */
int         present_filter_by_name(const char *name);
const char *present_filter_name(int filter);   /* "integer" / "sharp-bilinear" / "smooth" / "crt" */

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

/* The CRT overlay is a multiply mask over a cw x ch content rectangle that
 * shows a sw x sh source.  Scanline profile: the gain (0..255) of output row
 * `y` -- brightest (255) at the centre of each source line, darkest
 * (255 - PRESENT_CRT_SCAN_DARK) on the seam between two.  Flat 255 when a
 * source line gets fewer than 3 output rows (nothing to draw a gap with). */
#define PRESENT_CRT_SCAN_DARK 110
#define PRESENT_CRT_MASK_DARK 40
int      present_crt_row_gain(int y, int ch, int sh);
/* Aperture grille: gain of output column `x` for colour channel c (0 R, 1 G,
 * 2 B): 255 on its own stripe of the repeating R,G,B triad, 255 - MASK_DARK
 * elsewhere.  Flat 255 when a source pixel gets fewer than 3 output columns. */
int      present_crt_col_gain(int x, int c, int cw, int sw);
/* The combined multiply-mask pixel (0xAARRGGBB, alpha 255). */
uint32_t present_crt_mask(int x, int y, int cw, int ch, int sw, int sh);

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
