/* video.c — the mode table and the dispatcher (docs/VIDEO_DRIVERS.md §1).
 *
 * The `videoDrv` line of RESOURCE.CFG picks one of six spellings; ZELIARD.EXE
 * turns it into the mode index in [BASE:FF14] and loads GM<name>.BIN raw at
 * BASE:2000.  `cga` and `cga2` are the *same* driver file (GMCGA.BIN) shown in
 * INT 10h mode 5 and mode 6, which is why they share video_cga.c. */
#include <string.h>
#include "video.h"

/* -std=c11 hides POSIX strcasecmp, and the names are ASCII */
static int ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        int x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return *a == *b;
}

static const struct { const char *name, *cfg; int w, h; } MODES[VID_COUNT] = {
    [VID_MCGA]  = { "mcga",  "MCGA", 320, 200 },
    [VID_CGA]   = { "cga",   "CGA",  320, 200 },
    [VID_CGA2]  = { "cga2",  "CGA2", 640, 200 },
    [VID_EGA]   = { "ega",   "EGA",  640, 200 },
    [VID_HGC]   = { "hgc",   "HGC",  720, 348 },
    [VID_TANDY] = { "tandy", "TGA",  320, 200 },
};

int video_mode_by_name(const char *name)
{
    if (!name) return -1;
    for (int m = 0; m < VID_COUNT; m++)
        if (ieq(name, MODES[m].name) || ieq(name, MODES[m].cfg))
            return m;
    if (ieq(name, "tga")) return VID_TANDY;         /* the CFG spelling */
    if (ieq(name, "herc") || ieq(name, "hercules")) return VID_HGC;
    return -1;
}

const char *video_mode_name(int mode)
{
    return (mode >= 0 && mode < VID_COUNT) ? MODES[mode].name : "?";
}

const char *video_mode_cfg(int mode)
{
    return (mode >= 0 && mode < VID_COUNT) ? MODES[mode].cfg : "?";
}

void video_size(int mode, int *w, int *h)
{
    if (mode < 0 || mode >= VID_COUNT) mode = VID_MCGA;
    if (w) *w = MODES[mode].w;
    if (h) *h = MODES[mode].h;
}

void video_to_rgb(int mode, const uint8_t *fb, uint8_t *rgb)
{
    switch (mode) {
    case VID_CGA:   video_cga_to_rgb(fb, rgb);   break;
    case VID_CGA2:  video_cga2_to_rgb(fb, rgb);  break;
    case VID_EGA:   video_ega_to_rgb(fb, rgb);   break;
    case VID_HGC:   video_hgc_to_rgb(fb, rgb);   break;
    case VID_TANDY: video_tandy_to_rgb(fb, rgb); break;
    default:        video_mcga_to_rgb(fb, rgb);  break;
    }
}
