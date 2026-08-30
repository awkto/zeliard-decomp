/* video_cga.c — GMCGA.BIN in INT 10h mode 5 and mode 6 (docs/VIDEO_DRIVERS.md §2.2).
 *
 * `[0x2044]` @2D99 walks the same 48-byte PC-88 cells the MCGA driver does and
 * looks each 6-bit pair `(l<<3|r)` up in the 64-entry table @290B, so the whole
 * screen is one table lookup away from the pair buffer.  Output cells are 16
 * bytes (8x8x2bpp) written contiguously — a packing detail that changes the
 * bank stride but not a single pixel, so it does not appear here.
 *
 * mode 5 (`videoDrv:CGA`) is 320x200x4.  ZELIARD.EXE sets it with AX=0005h,
 * which clears the colour-burst bit, and the driver never touches 3D9 — so on
 * an RGB monitor the four entries are the burst-off palette black / cyan / red
 * / white.  The BIOS mode set leaves 3D9 with the intensity bit on, so it is
 * the *bright* three: CGA colours 11, 12 and 15, not 3, 4 and 7.  (Confirmed
 * against a DOSBox `machine=cga` capture — see port/README.md.)
 *
 * mode 6 (`videoDrv:CGA2`) is the *same* B800 bytes displayed as 640x200x1:
 * each 2-bit pixel becomes two mono pixels, i.e. the value's own two bits.
 * That is the "2-px mono dither" docs/VIDEO_DRIVERS.md flags as inferred; it
 * is the identical trick GMHGC.BIN uses deliberately (§2.4). */
#include "video.h"
#include "render.h"

/* GMCGA.BIN @290B, 64 bytes, rows l = blk wht red grn cyn blu yel mag.
 * Result values are CGA colour numbers 0..3 (0 blk, 1 cyn, 2 red, 3 wht). */
const uint8_t VID_CGA_PAIR[64] = {
    0, 1, 2, 1, 1, 0, 3, 2,     /* blk */
    1, 3, 3, 3, 1, 3, 3, 2,     /* wht */
    2, 3, 2, 1, 1, 2, 2, 2,     /* red */
    1, 3, 1, 3, 1, 1, 2, 2,     /* grn */
    1, 1, 1, 1, 1, 1, 3, 2,     /* cyn */
    0, 3, 2, 1, 1, 1, 3, 2,     /* blu */
    3, 3, 2, 2, 3, 3, 3, 2,     /* yel */
    1, 2, 2, 2, 2, 2, 2, 2,     /* mag */
};

/* mode 5 with the colour burst off: CGA colours 0, 11, 12, 15 */
static const uint8_t CGA5_RGB[4][3] = {
    { 0x00, 0x00, 0x00 },       /* 0 black            */
    { 0x55, 0xFF, 0xFF },       /* 1 light cyan  (11) */
    { 0xFF, 0x55, 0x55 },       /* 2 light red   (12) */
    { 0xFF, 0xFF, 0xFF },       /* 3 white       (15) */
};

void video_cga_to_rgb(const uint8_t *fb, uint8_t *rgb)
{
    for (int i = 0; i < FB_W * FB_H; i++) {
        const uint8_t *p = CGA5_RGB[VID_CGA_PAIR[fb[i] & 63]];
        rgb[i * 3] = p[0]; rgb[i * 3 + 1] = p[1]; rgb[i * 3 + 2] = p[2];
    }
}

#define CGA2_W 640

void video_cga2_to_rgb(const uint8_t *fb, uint8_t *rgb)
{
    for (int y = 0; y < FB_H; y++)
        for (int x = 0; x < FB_W; x++) {
            unsigned v = VID_CGA_PAIR[fb[y * FB_W + x] & 63];    /* 2 bits = 2 px */
            for (int b = 0; b < 2; b++) {
                uint8_t on = (v >> (1 - b)) & 1 ? 0xFF : 0x00;
                uint8_t *o = rgb + ((size_t)y * CGA2_W + x * 2 + b) * 3;
                o[0] = o[1] = o[2] = on;
            }
        }
}
