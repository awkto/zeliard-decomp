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

/* `w`/`h` are the driver's own framebuffer, which is what video_to_rgb emits
 * and what `make verify` compares; `dar_w`/`dar_h` are the shape the picture
 * occupied on the **display** it was designed for.  Every one of the six is a
 * 4:3 monitor, so none of these framebuffers is made of square pixels: MCGA's
 * 320x200 was shown 1:1.2 (taller than wide), EGA's 640x200 1:2.4, and
 * Hercules' 720x348 1:1.55.  Presenting a framebuffer 1:1 on a modern
 * square-pixel display is therefore ~20% too wide in the 320x200 modes. */
static const struct { const char *name, *cfg; int w, h, dar_w, dar_h; } MODES[VID_COUNT] = {
    [VID_MCGA]  = { "mcga",  "MCGA", 320, 200, 4, 3 },
    [VID_CGA]   = { "cga",   "CGA",  320, 200, 4, 3 },
    [VID_CGA2]  = { "cga2",  "CGA2", 640, 200, 4, 3 },
    [VID_EGA]   = { "ega",   "EGA",  640, 200, 4, 3 },
    [VID_HGC]   = { "hgc",   "HGC",  720, 348, 4, 3 },
    [VID_TANDY] = { "tandy", "TGA",  320, 200, 4, 3 },
};

/* The size to hand SDL_RenderSetLogicalSize: the framebuffer's own width, and
 * the height that makes the result the display aspect the driver was drawn for.
 * Widening rather than shortening keeps every source pixel represented. */
void video_display_size(int mode, int *w, int *h)
{
    int fw, fh;
    video_size(mode, &fw, &fh);
    if (mode < 0 || mode >= VID_COUNT) { if (w) *w = fw; if (h) *h = fh; return; }
    if (w) *w = fw;
    if (h) *h = fw * MODES[mode].dar_h / MODES[mode].dar_w;
}

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

/* gt*.bin's tile/sprite pair tables, rows l = blk wht red grn cyn blu yel mag.
 * Against GMCGA's @290B the CGA one differs at blk+red (2 -> 0), red+wht
 * (3 -> 2) and wht+red (3 -> 2); Tandy's is the same table. */
const uint8_t VID_GT_CGA[64] = {
    0, 1, 0, 1, 1, 0, 3, 2,
    1, 3, 2, 3, 1, 3, 3, 2,
    2, 2, 2, 1, 1, 2, 2, 2,
    1, 3, 1, 3, 1, 1, 2, 2,
    1, 1, 1, 1, 1, 1, 3, 2,
    0, 3, 2, 1, 1, 1, 3, 2,
    3, 3, 2, 2, 3, 3, 3, 2,
    1, 2, 2, 2, 2, 2, 2, 2,
};
const uint8_t VID_GT_TGA[64] = {
    0x0, 0x7, 0x4, 0x2, 0x3, 0x1, 0x8, 0x5,
    0x7, 0xF, 0xC, 0xE, 0xB, 0x9, 0xE, 0xD,
    0x4, 0xC, 0xC, 0xE, 0x7, 0x5, 0x6, 0xC,
    0x2, 0xE, 0xE, 0xA, 0xA, 0x3, 0xA, 0x7,
    0x3, 0xB, 0x7, 0xA, 0xB, 0x9, 0xA, 0x9,
    0x1, 0x9, 0x5, 0x3, 0x9, 0x9, 0x7, 0x5,
    0x8, 0xE, 0x6, 0xA, 0xA, 0x7, 0xE, 0xC,
    0x5, 0xD, 0xC, 0x7, 0x9, 0x5, 0xC, 0xD,
};

/* ympd.bin's and ckpd.bin's per-driver colour tables, indexed by the 4 bits
 * (left hi, left lo, right hi, right lo) of the two planes.  CGA, CGA2 and
 * Hercules share every one of them (GMHGC's painters use the CGA bytes).
 * ympd: mountains @3432 / @357D (Tandy), ground strips @36B6 / Tandy
 * @38C7 (first eight rows) and @38D7 (second eight).  ckpd: stalactites and
 * ceiling @3753 / @38D0, ground strips @3497 / @3654. */
const uint8_t VID_YM_MTN_C[16] = { 0, 3, 1, 2, 0, 3, 1, 2, 0, 3, 1, 2, 0, 3, 1, 2 };
const uint8_t VID_YM_MTN_T[16] = { 0, 7, 9, 1, 7, 0xF, 0xB, 7, 9, 0xB, 0xB, 3, 1, 7, 3, 9 };
const uint8_t VID_YM_GND_C[16] = { 0, 3, 2, 1, 1, 3, 3, 3, 2, 3, 1, 2, 2, 3, 3, 3 };
const uint8_t VID_YM_GND_T0[16] = { 0, 3, 4, 7, 3, 0xB, 5, 0xA, 4, 5, 0xC, 6, 7, 0xA, 6, 0xE };
const uint8_t VID_YM_GND_T1[16] = { 0, 7, 4, 2, 7, 0xF, 0xC, 0xE, 4, 0xC, 0xC, 2, 2, 0xE, 2, 0xA };
const uint8_t VID_CK_MTN_C[16] = { 0, 3, 2, 1, 1, 3, 2, 1, 0, 3, 2, 1, 1, 3, 2, 1 };
const uint8_t VID_CK_MTN_T[16] = { 0, 7, 9, 1, 7, 0xF, 0xB, 7, 9, 0xB, 0xB, 3, 1, 7, 3, 9 };
const uint8_t VID_CK_GND_C[16] = { 0, 2, 3, 1, 0, 3, 2, 1, 0, 2, 3, 1, 0, 2, 3, 1 };
const uint8_t VID_CK_GND_T[16] = { 0, 4, 3, 2, 4, 0xC, 5, 6, 3, 5, 0xB, 0xA, 2, 6, 0xA, 0xE };

#define BIT(v, n) (((v) >> (n)) & 1)

int video_town_pixel(int mode, int y, uint8_t v, uint8_t *out)
{
    if (!(v & FB_TAG)) return 0;
    int tandy = mode == VID_TANDY;
    if (!tandy && mode != VID_CGA && mode != VID_CGA2 && mode != VID_HGC) return 0;
    if ((v & FB_TAG) == FB_GT) { *out = (tandy ? VID_GT_TGA : VID_GT_CGA)[v & 63]; return 1; }
    int ck = (v & FB_TAG) == FB_BD_CK, idx;
    const uint8_t *t;
    if (y < 142) {                      /* mountains / stalactites / far strip: (p2a p1a p2b p1b) at bits 5 3 2 0 */
        idx = BIT(v, 5) << 3 | BIT(v, 3) << 2 | BIT(v, 2) << 1 | BIT(v, 0);
        t = ck ? (tandy ? VID_CK_MTN_T : VID_CK_MTN_C) : (tandy ? VID_YM_MTN_T : VID_YM_MTN_C);
    } else if (ck) {                    /* ckpd near strips: bits 5 4 2 1 */
        idx = BIT(v, 5) << 3 | BIT(v, 4) << 2 | BIT(v, 2) << 1 | BIT(v, 1);
        t = tandy ? VID_CK_GND_T : VID_CK_GND_C;
    } else if (y < 150) {               /* ympd first ground strip: (w2a w1a w2b w1b) at bits 4 5 1 2 */
        idx = BIT(v, 4) << 3 | BIT(v, 5) << 2 | BIT(v, 1) << 1 | BIT(v, 2);
        t = tandy ? VID_YM_GND_T0 : VID_YM_GND_C;
    } else {                            /* ... and the second, at bits 4 3 1 0 */
        idx = BIT(v, 4) << 3 | BIT(v, 3) << 2 | BIT(v, 1) << 1 | BIT(v, 0);
        t = tandy ? VID_YM_GND_T1 : VID_YM_GND_C;
    }
    *out = t[idx];
    return 1;
}
