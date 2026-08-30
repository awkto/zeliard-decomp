/* video_hgc.c — GMHGC.BIN (docs/VIDEO_DRIVERS.md §2.4).
 *
 * 720x348 mono in B000, 90 bytes a row, four 8 KB banks addressed by
 * `line % 4`.  Two things make Hercules the odd one out:
 *
 *  * `[0x2044]` @2E37 uses the *same* 64-entry pair table as CGA (@2994 is
 *    byte-identical to GMCGA.BIN @290B), but the 2-bit result is painted as two
 *    adjacent mono pixels — the bits themselves, so 0/1/2/3 light 0, 1, 1 and 2
 *    of them.  A cell therefore stays 16 px wide on a 640-px-wide game area.
 *  * the row helper @2E11 is `g = (y+28)/3, b = (y+28)%3,
 *    addr = g*0x5A + b*0x2000 + x8*2 + 5`.  `g*0x5A + b*0x2000` is Hercules
 *    line `4g + b`, so game row y lands on **line 4*((y+28)/3) + (y+28)%3** —
 *    row 0 on line 37.  The formula never yields b = 3; bank 3 is filled by the
 *    routines that step row by row, which notice they have walked past 0x5FFF,
 *    write the row *again* there and wrap to bank 0 of the next group.  So the
 *    line after every b = 2 row repeats it, three game rows cover four
 *    Hercules lines, and 200 rows cover lines 37..303.
 *
 * The `+5` in the helper is 5 bytes = **40 px** of left margin, so the 640-px
 * game area sits at x = 40..679 and the rest of the 720x348 screen stays as the
 * bootstrap left it: cleared.
 *
 * A lit pixel is drawn at the "normal" intensity a Hercules card puts out in
 * graphics mode — 0xAA, not full white; graphics mode has no bright bit, that
 * only exists for text attributes.  (What a DOSBox `machine=hercules` capture
 * shows, and what makes the comparison exact.) */
#include <string.h>
#include "video.h"
#include "render.h"

#define HGC_W 720
#define HGC_H 348
#define HGC_MARGIN 40                   /* the helper's +5 bytes */
#define HGC_ON     0xAA                 /* graphics-mode "normal" intensity */

/* game row -> Hercules line (@2E11) */
int video_hgc_line(int y)
{
    int t = y + 28;
    return 4 * (t / 3) + t % 3;
}

void video_hgc_to_rgb(const uint8_t *fb, uint8_t *rgb)
{
    memset(rgb, 0, (size_t)HGC_W * HGC_H * 3);
    for (int y = 0; y < FB_H; y++) {
        int line = video_hgc_line(y);
        if (line < 0 || line >= HGC_H) continue;
        uint8_t *row = rgb + ((size_t)line * HGC_W + HGC_MARGIN) * 3;
        for (int x = 0; x < FB_W; x++) {
            unsigned v = VID_CGA_PAIR[fb[y * FB_W + x] & 63];   /* 2 bits = 2 mono px */
            for (int b = 0; b < 2; b++) {
                uint8_t on = (v >> (1 - b)) & 1 ? HGC_ON : 0x00;
                uint8_t *o = row + (x * 2 + b) * 3;
                o[0] = o[1] = o[2] = on;
            }
        }
        /* the bank-3 repeat: stepping off a b = 2 row writes it once more on
         * the next line before wrapping to bank 0 of the next group */
        if ((y + 28) % 3 == 2 && line + 1 < HGC_H)
            memcpy(rgb + (size_t)(line + 1) * HGC_W * 3, rgb + (size_t)line * HGC_W * 3,
                   (size_t)HGC_W * 3);
    }
}
