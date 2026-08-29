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

void render_frame(uint8_t *fb, const Game *g, const HeroGfx *h);     /* fb: FB_W*FB_H VGA indices */
void render_to_rgb(const uint8_t *fb, uint8_t *rgb);                  /* FB_W*FB_H*3 */

#endif
