/* video_gd.h — the cutscene renderer's per-mode tables (video_gd.c, issue #34). */
#ifndef ZEL_VIDEO_GD_H
#define ZEL_VIDEO_GD_H
#include <stdint.h>

typedef struct VideoGd {
    int     mode;                   /* VID_* */
    int     loaded;
    uint8_t tbl[10][256];           /* CGA/HGC/Tandy: [rec][l<<4|r] -> colour */
    uint8_t regs[10][16];           /* EGA: [rec][colour] -> palette register value */
    int     has_putc;               /* CGA/HGC/Tandy: the putchar colour map below is valid */
    uint8_t putc[8];                /* the colour gd_putchar(c) ends up as, 2 bits (CGA/HGC) or 4 (Tandy) */
    /* the intro's sand (gd_fx_sand, 38E6): gdcga / gdhgc carry their own 2 bpp
     * pattern records (24 x 8 bytes: four AND masks, four OR bytes) and a mode
     * byte per variant, and draw straight in the driver's colours, so the sand
     * is written as a pinned driver colour (SAND_PIN) that no later palette
     * record recolours -- the hardware does not either */
    int     has_sand2;              /* CGA / CGA2 / HGC: sand_pat / sand_mode are valid */
    uint8_t sand_pat[192];
    uint8_t sand_mode[3];
} VideoGd;

/* a sand pixel: the framebuffer byte SAND_PIN(c) with the Gd's `pin` plane (colour + 1)
 * agreeing carries driver colour c, bypassing the record's pair table */
#define SAND_PIN(c) ((uint8_t)(0x90 | ((c) & 15)))

/* read the tables of the gd*.bin that `mode` loads (MCGA: nothing to read) */
int  video_gd_load(VideoGd *v, int mode, const char *dir);
/* a 320x200 cutscene frame (bytes `l<<4|r`) through record `rec`; `rgb` holds
 * video_size(mode) * 3 bytes */
void video_gd_to_rgb(const VideoGd *v, int rec, const uint8_t *fb, const uint8_t *pin, uint8_t *rgb);

#endif
