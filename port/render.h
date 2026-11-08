/* render.h — 320x200 MCGA framebuffer: the 28x19-cell playfield at (48,14)
 * (VIDEO_DRIVERS.md vid_clear_playfield: rows 14..157, x 48..271) and the hero
 * sprite passes of gfmcga @3A95. */
#ifndef ZEL_RENDER_H
#define ZEL_RENDER_H
#include <stdint.h>
#include "physics.h"

#define FB_W 320
#define FB_H 200
#define PF_X 48
#define PF_Y 14
#define PF_W (SCREEN_COLS * 8)
#define PF_H 144                    /* 18 rows visible; the 19th window row is under the HUD */

void render_fadein_pass(uint8_t *fb, const Game *g, int c);          /* gfmcga 4192, one pass (c = 8 .. 1) over fb (#119) */
/* gfmcga 42F7 (spell 7): the XOR flash, three sets of nine expanding rectangles drawn in 0x36 then again in 0, the flash
 * undone.  `wait` is called after each of the 54 rectangles (the 444A timer wait), with the screen as it is then. */
typedef void (*BurstWaitFn)(void *user);
void render_burst(uint8_t *fb, const Game *g, BurstWaitFn wait, void *user);
void render_frame(uint8_t *fb, const Game *g, const HeroGfx *h);     /* fb: FB_W*FB_H VGA indices */

/* Smooth scrolling (#127, port/smooth.h): pixel offsets the presentation layer
 * hands render_frame so the 8 px cell steps can be drawn part-way through their
 * slide.  All in framebuffer pixels, applied on top of the cell grid; never set
 * on the engine's own path (render_set_smooth(NULL) is the default), so
 * `make verify`, the traces and the scenarios render exactly as before. */
typedef struct SmoothOfs {
    int active;                     /* any offset non-zero */
    int wx, wy;                     /* the background tiles (the world scroll) */
    int hx, hy;                     /* the hero and his blade */
    int ox[MAX_OBJS], oy[MAX_OBJS]; /* enemy / object record i */
    int sx[MAX_SHOTS + 1], sy[MAX_SHOTS + 1];   /* projectile slot i */
    int mx[4], my[4];               /* spell sprite slot i */
} SmoothOfs;
void render_set_smooth(const SmoothOfs *ofs);        /* NULL = off (the default) */
/* LIFE bar, GOLD/ALMAS digits, the four narrow-font labels and the place name.
 * `tf` supplies the [F504] narrow glyphs (NULL = bars and digits only) and
 * `place` is the {x4, y, xoff, len, chars} record video [2010] draws: the
 * cavern map's [C00E] or the town map's [C004]. */
struct TextFont;
void vid_gauge_bar(uint8_t *fb, int bh, int bl, int w);   /* [2004] */
void render_hud(uint8_t *fb, const Game *g, const DigitFont *font,
                const struct TextFont *tf, const uint8_t *place);
void render_window(uint8_t *fb, int x, int y, int w, int h, int framed);   /* [2000] */
/* mole.bin's stone frame / HUD panel over rows y0..y1-1 (see gfx.h).  Only the
 * pixels it actually covers are written, so it can be re-blitted after a
 * [2002] playfield clear or a HUD-band clear without disturbing either. */
struct ScreenFrame;
void render_screen_frame(uint8_t *fb, const struct ScreenFrame *f, int y0, int y1);
/* gmmcga [2040] = 2130, the screen dissolve (#118): eight passes with the masks 01,03,07,..,FF
 * (218D), each pixel of the playfield (x 48..271, rows 14..157) zeroed where the rotating mask
 * bit is set.  render_dissolve_pass(fb, n) is pass n (0..7) done in place (the shop's own
 * screen buffer takes the passes one by one, like VRAM); the town and the cavern are redrawn
 * from state every frame, so they use render_dissolve(fb, n) = passes 0..n-1 over a fresh
 * frame.  The other drivers' dissolves (CGA 2124, EGA 214E, HGC 2143, TGA 2124) have their
 * own masks: under --video cga/cga2/hgc/ega/tandy this runs video_dissolve_pass() (video.h)
 * instead, on the pair buffer, with the driver's mask. */
void render_dissolve_pass(uint8_t *fb, int pass);
void render_dissolve(uint8_t *fb, int passes);
void render_to_rgb(const uint8_t *fb, uint8_t *rgb);                  /* FB_W*FB_H*3 */

#endif
