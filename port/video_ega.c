/* video_ega.c — GMEGA.BIN (docs/VIDEO_DRIVERS.md §2.3).
 *
 * EGA is the one driver whose `[0x2044]` is a bare `ret` @2E92: the cells stay
 * as 8 rows x 3 big-endian plane words and the renderers copy those words
 * straight into planes 0-2 of A000, so a cell is 16 pixels of 640 and each
 * pixel keeps its own 3-bit PC-88 colour.  Those are exactly the two halves
 * every other driver blends into one pair, so the pair buffer expands back to
 * the EGA screen with no loss: pair p -> pixel 2x = p>>3, pixel 2x+1 = p&7.
 *
 * Colours: GAME.BIN @A3FE programs the 16 palette registers in PC-88 order
 * with INT 10h AX=1002h from the block at @A409, so writing colour number c is
 * writing PC-88 colour c.  A palette-register value is 00 r'g'b'RGB.
 *
 * What those six bits *look* like depends on the monitor, and a 200-line EGA
 * mode drives a CGA-class Color Display: the card puts out four signals, R G B
 * from bits 2-0 and the intensity line from the **secondary green** bit 4, so a
 * register value v shows as the 16-colour entry `(v & 7) | ((v & 0x10) >> 1)`.
 * That is what DOSBox reproduces (`machine=ega` and `machine=vgaonly` agree,
 * and the same 8 colours come back out of a capture), and it is what the port
 * renders.  Decoded that way the game's eight are
 *
 *   blk 00 -> 0 black      wht 3F -> 15 white       red 24 -> 4  red
 *   grn 12 -> 10 lt green  cyn 1B -> 11 lt cyan     blu 09 -> 1  blue
 *   yel 36 -> 14 yellow    mag 2D -> 5  magenta
 *
 * — dark red/blue/magenta against bright green/cyan/yellow, which is exactly
 * the lopsided EGA look.  On an Enhanced Color Display the same six bits would
 * instead be two bits a channel out of the full 64-colour set (0/85/170/255),
 * making all eight fully saturated; no capture of that exists, so the port does
 * not render it. */
#include "video.h"
#include "render.h"

/* GAME.BIN @A409: palette registers 0..15 then the border (register 0x10). */
const uint8_t VID_EGA_PALETTE[17] = {
    0x00, 0x3F, 0x24, 0x12, 0x1B, 0x09, 0x36, 0x2D,   /* blk wht red grn cyn blu yel mag */
    0x38, 0x07, 0x04, 0x02, 0x03, 0x01, 0x06, 0x05,   /* 8-15: the dim set, unused by the game */
    0x00,                                             /* border */
};

/* the 16 colours an RGBI Color Display can show */
static const uint8_t RGBI[16][3] = {
    { 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0xAA }, { 0x00, 0xAA, 0x00 }, { 0x00, 0xAA, 0xAA },
    { 0xAA, 0x00, 0x00 }, { 0xAA, 0x00, 0xAA }, { 0xAA, 0x55, 0x00 }, { 0xAA, 0xAA, 0xAA },
    { 0x55, 0x55, 0x55 }, { 0x55, 0x55, 0xFF }, { 0x55, 0xFF, 0x55 }, { 0x55, 0xFF, 0xFF },
    { 0xFF, 0x55, 0x55 }, { 0xFF, 0x55, 0xFF }, { 0xFF, 0xFF, 0x55 }, { 0xFF, 0xFF, 0xFF },
};

/* one 00r'g'b'RGB palette-register value -> 8-bit RGB, as a 200-line mode puts
 * it on a Color Display: RGB from bits 2-0, intensity from the g' bit */
static void ega_rgb(uint8_t v, uint8_t *out)
{
    const uint8_t *p = RGBI[(v & 7) | ((v & 0x10) >> 1)];
    out[0] = p[0]; out[1] = p[1]; out[2] = p[2];
}

#define EGA_W 640

void video_ega_to_rgb(const uint8_t *fb, uint8_t *rgb)
{
    uint8_t pal[8][3];
    for (int c = 0; c < 8; c++) ega_rgb(VID_EGA_PALETTE[c], pal[c]);
    for (int y = 0; y < FB_H; y++)
        for (int x = 0; x < FB_W; x++) {
            unsigned p = fb[y * FB_W + x] & 63;
            const uint8_t *l = pal[p >> 3], *r = pal[p & 7];
            uint8_t *o = rgb + ((size_t)y * EGA_W + x * 2) * 3;
            o[0] = l[0]; o[1] = l[1]; o[2] = l[2];
            o[3] = r[0]; o[4] = r[1]; o[5] = r[2];
        }
}
