/* video_gd.c — the cutscene renderer's own video tables (issue #34).
 *
 * The opening and the ending draw through gd{ega,cga,hgc,tga,mcga}.bin
 * (ZELRES1[1..5], docs/CUTSCENES.md §1), not through the pair buffer: every
 * gd image carries its own palette machinery, and the five differ.
 *
 *   MCGA   a 256-entry DAC, base[rec][l] + base[rec][r] (gd.c)
 *   CGA    `gd_set_palette` (@40EF) = `dec ax; mul 0x100; add 0x40FD`: a 256-byte
 *          table per record 1..9 indexed by the pixel pair `l<<4 | r` (both
 *          4-bit PC-88 colours), whose value is the 2-bit CGA colour
 *   HGC    the same, table base @4288 (and the 2 bits are two mono pixels)
 *   Tandy  the same, base @41E4, 4-bit colours
 *   EGA    no pair at all: 16 palette registers per record at @416A + 16*rec,
 *          each PC-88 pixel is its own 4-bit pixel on a 640-pixel line
 *
 * so a cutscene frame is still the MCGA byte `l<<4 | r` and the mode is a pure
 * function of it plus the record number.  Everything is read out of the
 * overlay, nothing is copied.  Record 0 (the storm's flash) has no table in
 * the 320x200 drivers: `dec ax` of 0 wraps and the pointer lands 256 bytes
 * before the table, which is read literally as the original would. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "video.h"
#include "video_gd.h"
#include "sar.h"
#include "render.h"

/* ZELRES1 entry and the immediate of the `add ax,imm` in gd_set_palette */
static const struct { int res; unsigned imm_at; unsigned imm_op; } GDMODE[VID_COUNT] = {
    [VID_CGA]   = { 2, 0x40F5, 0x05 },
    [VID_CGA2]  = { 2, 0x40F5, 0x05 },
    [VID_HGC]   = { 3, 0x4280, 0x05 },
    [VID_TANDY] = { 4, 0x41DC, 0x05 },
    [VID_EGA]   = { 1, 0x413B, 0x05 },
};

static unsigned rd16(const uint8_t *img, unsigned addr) { return img[addr - 0x3000] | (img[addr - 0x3000 + 1] << 8); }

int video_gd_load(VideoGd *v, int mode, const char *dir)
{
    memset(v, 0, sizeof *v);
    v->mode = mode;
    if (mode < 0 || mode >= VID_COUNT || mode == VID_MCGA) return 0;     /* MCGA: gd.c's DAC */
    size_t len = 0;
    uint8_t *img = sar_load(dir, 0, GDMODE[mode].res, 1, &len);
    if (!img) return -1;
    unsigned at = GDMODE[mode].imm_at;
    if (len < at - 0x3000 + 3 || img[at - 0x3000] != GDMODE[mode].imm_op) { free(img); return -1; }
    unsigned base = rd16(img, at + 1);
    if (mode == VID_EGA) {
        for (int rec = 0; rec < 10; rec++) {
            unsigned a = base + 16u * rec;
            if (a - 0x3000 + 16 > len) { free(img); return -1; }
            memcpy(v->regs[rec], img + (a - 0x3000), 16);
        }
    } else {
        for (int rec = 0; rec < 10; rec++) {
            unsigned a = (base + (unsigned)(((rec - 1) * 0x100) & 0xFFFF)) & 0xFFFF;
            if (a < 0x3000 || a - 0x3000 + 256 > len) { memset(v->tbl[rec], 0, 256); continue; }
            memcpy(v->tbl[rec], img + (a - 0x3000), 256);
        }
    }
    /* gd_putchar -> [2022]: gd's own 8-entry colour map (@4A97 gdcga, @4C23 gdhgc,
     * @4BD4 gdtga) and then the video driver's pattern table (GMCGA @259A, GMHGC
     * @2600, GMTGA @262E).  The pattern's low bits are the colour the glyph is
     * drawn in, whatever the record's pair table says for the pair (c, c). */
    if (mode != VID_EGA) {
        static const struct { const char *file; unsigned gdmap, drvtab; unsigned mask; } P[VID_COUNT] = {
            [VID_CGA] = { "GMCGA.BIN", 0x4A97, 0x259A, 3 }, [VID_CGA2] = { "GMCGA.BIN", 0x4A97, 0x259A, 3 },
            [VID_HGC] = { "GMHGC.BIN", 0x4C23, 0x2600, 3 }, [VID_TANDY] = { "GMTGA.BIN", 0x4BD4, 0x262E, 15 },
        };
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dir, P[mode].file);
        FILE *f = fopen(path, "rb");
        if (f) {
            uint8_t drv[0x800];
            size_t n = fread(drv, 1, sizeof drv, f);
            fclose(f);
            if (P[mode].gdmap - 0x3000 + 8 <= len && P[mode].drvtab - 0x2000 + 16 <= n) {
                int ok = 1;
                for (int c = 0; c < 8; c++) {
                    unsigned m = img[P[mode].gdmap - 0x3000 + c];
                    if (m >= 16) { ok = 0; break; }
                    v->putc[c] = (uint8_t)(drv[P[mode].drvtab - 0x2000 + m] & P[mode].mask);
                }
                v->has_putc = ok;
            }
        }
    }
    if (mode != VID_EGA) v->pins = 1;
    if (mode == VID_CGA || mode == VID_CGA2 || mode == VID_HGC) {
        /* sand: 24 pattern records and the mode byte of each variant (CGA @3945 / @3AFC,
         * HGC @3A0B / @3BC2); the variant indexes the bytes directly, no doubling */
        unsigned pat = mode == VID_HGC ? 0x3A0B : 0x3945, mb = mode == VID_HGC ? 0x3BC2 : 0x3AFC;
        if (pat - 0x3000 + 192 <= len && mb - 0x3000 + 3 <= len) {
            memcpy(v->sand_pat, img + (pat - 0x3000), 192);
            memcpy(v->sand_mode, img + (mb - 0x3000), 3);
            v->has_sand2 = 1;
        }
    }
    free(img);
    v->loaded = 1;
    return 0;
}

void video_gd_size(int mode, int *w, int *h) { video_size(mode, w, h); }

void video_gd_to_rgb(const VideoGd *v, int rec, const uint8_t *fb, uint8_t *rgb)
{
    static uint8_t idx[FB_W * FB_H];
    if (rec < 0 || rec > 9) rec = 0;
    if (v->mode == VID_EGA) { video_ega_regs_to_rgb(fb, v->regs[rec], rgb); return; }
    uint8_t t[256];
    memcpy(t, v->tbl[rec], 256);
    if (v->pins)
        for (int c = 0; c < 16; c++) t[SAND_PIN(c)] = (uint8_t)c;
    for (int i = 0; i < FB_W * FB_H; i++) idx[i] = t[fb[i]];
    switch (v->mode) {
    case VID_CGA:   video_cga_idx_to_rgb(idx, rgb);   break;
    case VID_CGA2:  video_cga2_idx_to_rgb(idx, rgb);  break;
    case VID_HGC:   video_hgc_idx_to_rgb(idx, rgb);   break;
    default:        video_tandy_idx_to_rgb(idx, rgb); break;
    }
}
