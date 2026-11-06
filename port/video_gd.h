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
} VideoGd;

/* read the tables of the gd*.bin that `mode` loads (MCGA: nothing to read) */
int  video_gd_load(VideoGd *v, int mode, const char *dir);
/* a 320x200 cutscene frame (bytes `l<<4|r`) through record `rec`; `rgb` holds
 * video_size(mode) * 3 bytes */
void video_gd_to_rgb(const VideoGd *v, int rec, const uint8_t *fb, uint8_t *rgb);

#endif
