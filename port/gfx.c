#include "gfx.h"
#include "sar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* GAME.BIN @A456: PC-88 colour -> RGB in 5-bit units */
static const uint8_t BASE8[8][3] = {
    {0, 0, 0}, {31, 31, 31}, {31, 0, 0}, {0, 31, 0}, {0, 31, 31}, {0, 0, 31}, {31, 31, 0}, {31, 0, 31},
};

static uint8_t dac_to_8(int v) { return (uint8_t)((v * 255 + 31) / 63); }   /* round(v*255/63) like DOSBox */

/* built at load time from BASE8 (GAME.BIN @A41B: DAC[l*8+r] = BASE[l] + BASE[r]) */
uint8_t PAL_RGB[64][3];
static void pal_init(void)
{
    static int done = 0;
    if (done) return;
    done = 1;
    for (int l = 0; l < 8; l++)
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 3; c++)
                PAL_RGB[l * 8 + r][c] = dac_to_8(BASE8[l][c] + BASE8[r][c]);
}

const uint8_t PAL2BPP[5][16] = {
    {0x00, 0x01, 0x02, 0x03, 0x08, 0x09, 0x0A, 0x0B, 0x10, 0x11, 0x12, 0x13, 0x18, 0x19, 0x1A, 0x1B},
    {0x00, 0x02, 0x04, 0x06, 0x10, 0x12, 0x14, 0x16, 0x20, 0x22, 0x24, 0x26, 0x30, 0x32, 0x34, 0x36},
    {0x00, 0x01, 0x04, 0x05, 0x08, 0x09, 0x0C, 0x0D, 0x20, 0x21, 0x24, 0x25, 0x28, 0x29, 0x2C, 0x2D},
    {0x00, 0x05, 0x06, 0x07, 0x28, 0x2D, 0x2E, 0x2F, 0x30, 0x35, 0x36, 0x37, 0x38, 0x3D, 0x3E, 0x3F},
    {0x00, 0x06, 0x05, 0x07, 0x30, 0x36, 0x35, 0x37, 0x28, 0x2E, 0x2D, 0x2F, 0x38, 0x3E, 0x3D, 0x3F},
};

void gfx_decode48(const uint8_t *c, Cell8 *out)
{
    for (int row = 0; row < 8; row++) {
        const uint8_t *r = c + row * 6;
        unsigned a = r[0] << 8 | r[1], b = r[2] << 8 | r[3], cc = r[4] << 8 | r[5];
        for (int k = 0; k < 8; k++) {
            int x0 = 2 * k, x1 = 2 * k + 1;
            unsigned l = ((cc >> (15 - x0)) & 1) << 2 | ((b >> (15 - x0)) & 1) << 1 | ((a >> (15 - x0)) & 1);
            unsigned rr = ((cc >> (15 - x1)) & 1) << 2 | ((b >> (15 - x1)) & 1) << 1 | ((a >> (15 - x1)) & 1);
            out->px[row][k] = (uint8_t)(l << 3 | rr);            /* [0x2044]: pair -> 6-bit index */
        }
    }
}

void gfx_decode32(const uint8_t *c, const uint8_t *pal16, Cell2 *out)
{
    for (int row = 0; row < 8; row++) {
        unsigned a = c[row * 4] << 8 | c[row * 4 + 1], b = c[row * 4 + 2] << 8 | c[row * 4 + 3];
        unsigned m = a | b;
        unsigned d = (m | (m >> 1) | (m << 1)) & 0xFFFF;         /* gfmcga @4EDD: 1-px horizontal dilation */
        for (int k = 0; k < 8; k++) {
            int x0 = 2 * k, x1 = 2 * k + 1;
            unsigned l = ((b >> (15 - x0)) & 1) << 1 | ((a >> (15 - x0)) & 1);
            unsigned r = ((b >> (15 - x1)) & 1) << 1 | ((a >> (15 - x1)) & 1);
            int drawn = ((d >> (14 - 2 * k)) & 3) != 0;          /* pair transparent only when fully clear in ~d */
            out->mask[row][k] = (uint8_t)drawn;
            out->px[row][k] = drawn ? pal16[l << 2 | r] : 0;     /* table[0] = black outline */
        }
    }
}

/* ZELRES3 indices: MPP1..MPP9, MPPA, MPPB (fight.bin @9C43), DCHR.GRP */
static const int MPP_RES[11] = {74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84};
#define DCHR_RES 54
#define FMAN_RES 51

int gfx_load_tileset(Tileset *t, const char *dir, int mpp_index)
{
    pal_init();
    memset(t, 0, sizeof *t);
    if (mpp_index < 0 || mpp_index > 10) return -1;
    size_t len;
    uint8_t *mpp = sar_load(dir, 2, MPP_RES[mpp_index], 1, &len);
    if (!mpp) return -1;
    memcpy(t->lists, mpp, len < 48 ? len : 48);
    int n = (int)(len / 48);
    if (n > 0x40) n = 0x40;
    for (int i = 1; i < n; i++) { gfx_decode48(mpp + i * 48, &t->cell[i]); t->present[i] = 1; }
    free(mpp);
    uint8_t *dchr = sar_load(dir, 2, DCHR_RES, 1, &len);
    if (!dchr) return -1;
    n = (int)(len / 48);
    if (n > BANK_CELLS - 0x40) n = BANK_CELLS - 0x40;
    for (int i = 0; i < n; i++) { gfx_decode48(dchr + i * 48, &t->cell[0x40 + i]); t->present[0x40 + i] = 1; }
    free(dchr);
    t->mpp_index = mpp_index;
    return 0;
}

int gfx_load_hero(HeroGfx *h, const char *dir)
{
    pal_init();
    memset(h, 0, sizeof *h);
    size_t len;
    uint8_t *d = sar_load(dir, 2, FMAN_RES, 1, &len);
    if (!d || len < 0x333) { free(d); return -1; }
    memcpy(h->frame, d, sizeof h->frame);
    int n = (int)((len - 0x333) / 32);
    if (n > 256) n = 256;
    for (int i = 0; i < n; i++) gfx_decode32(d + 0x333 + i * 32, PAL2BPP[0], &h->cell[i]);
    h->ncells = n;
    free(d);
    return 0;
}
