/* video_mcga.c — GMMCGA.BIN (docs/VIDEO_DRIVERS.md §2.1).
 *
 * The identity stage: mode 13h is linear 320x200x8 and every byte in A000 is
 * already the pair `l*8+r`, so all this does is run it through the 64-entry
 * blend DAC GAME.BIN @A41B programs (`DAC[l*8+r] = BASE[l] + BASE[r]`,
 * tools/palette.py = port/gfx.c PAL_RGB). */
#include "video.h"
#include "gfx.h"
#include "render.h"

void video_mcga_to_rgb(const uint8_t *fb, uint8_t *rgb)
{
    gfx_pal_init();
    for (int i = 0; i < FB_W * FB_H; i++) {
        const uint8_t *p = PAL_RGB[fb[i] & 63];
        rgb[i * 3] = p[0]; rgb[i * 3 + 1] = p[1]; rgb[i * 3 + 2] = p[2];
    }
}
