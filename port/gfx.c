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
/* The DAC GAME.BIN @A41B builds is only needed once; every loader calls this,
 * and so does the MCGA output stage, so a caller that renders without loading
 * anything (test_video) still gets the real palette. */
void gfx_pal_init(void)
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

void gfx_decode32_raw(const uint8_t *c, Cell2R *out)
{
    for (int row = 0; row < 8; row++) {
        unsigned a = c[row * 4] << 8 | c[row * 4 + 1], b = c[row * 4 + 2] << 8 | c[row * 4 + 3];
        unsigned m = a | b;
        unsigned d = (m | (m >> 1) | (m << 1)) & 0xFFFF;         /* gfmcga @4EDD */
        for (int k = 0; k < 8; k++) {
            int x0 = 2 * k, x1 = 2 * k + 1;
            unsigned l = ((b >> (15 - x0)) & 1) << 1 | ((a >> (15 - x0)) & 1);
            unsigned r = ((b >> (15 - x1)) & 1) << 1 | ((a >> (15 - x1)) & 1);
            out->mask[row][k] = (uint8_t)(((d >> (14 - 2 * k)) & 3) != 0);
            out->idx[row][k] = (uint8_t)(l << 2 | r);
        }
    }
}

/* ZELRES3 indices: MPP1..MPP9, MPPA, MPPB (fight.bin @9C43), DCHR.GRP */
static const int MPP_RES[11] = {74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84};
#define DCHR_RES 54
#define FMAN_RES 51

int gfx_load_tileset(Tileset *t, const char *dir, int mpp_index)
{
    gfx_pal_init();
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
    gfx_pal_init();
    memset(h, 0, sizeof *h);
    size_t len;
    uint8_t *d = sar_load(dir, 2, FMAN_RES, 1, &len);
    if (!d || len < 0x333) { free(d); return -1; }
    memcpy(h->frame, d, sizeof h->frame);
    /* fman.grp's last cell is two bytes short of a full 32-byte cell (the
     * resource ends at 0x1FF0, and cell 0xE5 -- which frames 80..82, the
     * no-shield attack overlay facing right, do use -- would need 0x1FF2).
     * The game reads it out of the arena regardless, so pad instead of
     * dropping it: rounding the count down cost two pixels of Garland's
     * sleeve on every swing. */
    int n = (int)((len - 0x333 + 31) / 32);
    if (n > 256) n = 256;
    for (int i = 0; i < n; i++) {
        uint8_t raw[32] = {0};
        size_t off = 0x333 + (size_t)i * 32, avail = len > off ? len - off : 0;
        memcpy(raw, d + off, avail < 32 ? avail : 32);
        gfx_decode32(raw, PAL2BPP[0], &h->cell[i]);
    }
    h->ncells = n;
    free(d);
    /* The blade is part of the hero's frame: gfmcga's row hook draws the 3x3
     * fman sprite and then composites the sword block over it, so the two banks
     * are loaded together and a caller that has a HeroGfx has the sword too.
     * A missing sword.grp is not fatal -- the hero simply swings empty-handed,
     * which is what the port did before issue #35. */
    if (gfx_load_sword(&h->sword, dir))
        fprintf(stderr, "note: no sword.grp (the blade will not be drawn)\n");
    return 0;
}

/* ------------------------------------------------------------------------ */
/* sword.grp — the swing art (see gfx.h)                                     */
/* ------------------------------------------------------------------------ */
#define SWORD_RES 26                            /* ZELRES2[26] */
#define GFMCGA_RES 6                            /* ZELRES2[6], loaded at BASE+0x2000:3000 */

const int SWORD_FRAMES[3]  = {6, 4, 1};         /* slash, upward slash, down-thrust */
const int SWORD_SECTION[6] = {0, 0, 0, 1, 1, 2};    /* kernel 0x0BA0, by sword 1..6 */

/* the six group bases inside a section, in SWORD_GROUPS order */
static const unsigned SWORD_BLOCK_OFF[SWORD_GROUPS] = {0x01E, 0x07E, 0x0BE, 0x0CE, 0x12E, 0x16E};
/* their {row,col} offset tables; the two thrusts have none (gfmcga 3E63/3E76
 * store the pair inline as 0xFF01 / 0x0001, i.e. row +1 col -1 / row +1 col 0) */
static const unsigned SWORD_DELTA_OFF[SWORD_GROUPS] = {0x17E, 0x18A, 0, 0x192, 0x19E, 0};
static const int8_t   SWORD_THRUST_DELTA[2][2] = {{1, 0}, {1, -1}};      /* right, left */

int gfx_load_sword(SwordGfx *s, const char *dir)
{
    memset(s, 0, sizeof *s);
    size_t len, glen;
    uint8_t *d = sar_load(dir, 1, SWORD_RES, 1, &len);
    if (!d || len < 6) { free(d); return -1; }
    /* ARCHITECTURE.md "3-section shape containers": {u16 hdr_len, u16 off2, u16 off3} */
    size_t bound[SWORD_SECTIONS + 1];
    bound[0] = d[0] | d[1] << 8;
    bound[1] = d[2] | d[3] << 8;
    bound[2] = d[4] | d[5] << 8;
    bound[3] = len;
    for (int i = 0; i < SWORD_SECTIONS; i++)
        if (bound[i + 1] <= bound[i] || bound[i + 1] > len) { free(d); return -1; }
    for (int sec = 0; sec < SWORD_SECTIONS; sec++) {
        const uint8_t *p = d + bound[sec];
        size_t slen = bound[sec + 1] - bound[sec];
        if (slen < 0x1A6) { free(d); return -1; }
        size_t data_off = p[0] | p[1] << 8;      /* header word 0, +0xB000 in the game */
        if (data_off >= slen) { free(d); return -1; }
        int nc = (int)((slen - data_off) / 16);
        if (nc > SWORD_CELLS) nc = SWORD_CELLS;
        s->ncells[sec] = nc;
        for (int c = 0; c < nc; c++) {
            const uint8_t *cp = p + data_off + (size_t)c * 16;
            for (int r = 0; r < 8; r++) {
                unsigned w = (unsigned)cp[r * 2] << 8 | cp[r * 2 + 1];    /* 4039: xchg al,ah */
                for (int k = 0; k < 8; k++)
                    s->idx[sec][c][r][k] = (uint8_t)((w >> (14 - 2 * k)) & 3);  /* 4092 */
            }
        }
        for (int gp = 0; gp < SWORD_GROUPS; gp++) {
            int nf = SWORD_FRAMES[gp % 3];
            memcpy(s->block[sec][gp], p + SWORD_BLOCK_OFF[gp], (size_t)nf * 16);
            for (int f = 0; f < nf; f++) {
                if (!SWORD_DELTA_OFF[gp]) {                              /* the thrusts */
                    s->delta[sec][gp][f][0] = SWORD_THRUST_DELTA[gp / 3][0];
                    s->delta[sec][gp][f][1] = SWORD_THRUST_DELTA[gp / 3][1];
                } else {    /* 3ED9: bl (low byte) = cell rows, bh = cell columns */
                    s->delta[sec][gp][f][0] = (int8_t)p[SWORD_DELTA_OFF[gp] + 2 * f];
                    s->delta[sec][gp][f][1] = (int8_t)p[SWORD_DELTA_OFF[gp] + 2 * f + 1];
                }
            }
        }
    }
    free(d);
    /* gfmcga 0x4086: six words {[4FF5], [4FF6]} indexed by [0x92] - 1 */
    uint8_t *gf = sar_load(dir, 1, GFMCGA_RES, 1, &glen);
    if (gf && glen >= 0x4086 - 0x3000 + 12) memcpy(s->colour, gf + (0x4086 - 0x3000), 12);
    else { static const uint8_t fallback[12] = {0x01,0x09, 0x04,0x24, 0x03,0x1B,
                                                0x01,0x09, 0x04,0x24, 0x06,0x36};
           memcpy(s->colour, fallback, 12); }
    free(gf);
    s->loaded = 1;
    return 0;
}

/* fight.bin 9D8D: the enemy sprite bank per level-record byte +4.  The table
 * interleaves the cavern banks with the boss banks, exactly like the AI table
 * at 9CBC: 0 ENP1, 1 CRAB, 2 ENP2, 3 TAKO, ... 14 ENP8, 15 AKMA, 16 MAO1,
 * 17 MAO2.  Values are 0-based ZELRES3 indices (the table's res# minus 1). */
static const int ENP_RES[18] = {56, 64, 57, 65, 58, 66, 59, 67, 60, 68, 61, 69, 62, 70, 63, 71, 72, 73};

int gfx_load_enemy_cells(EnemyGfx *e, const char *dir, int enp_index)
{
    gfx_pal_init();
    memset(e, 0, sizeof *e);
    if (enp_index < 0 || enp_index > 17) return -1;
    size_t len;
    uint8_t *d = sar_load(dir, 2, ENP_RES[enp_index], 1, &len);
    if (!d) return -1;
    int n = (int)(len / 32);
    if (n > ENEMY_CELLS) n = ENEMY_CELLS;
    for (int i = 0; i < n; i++) gfx_decode32_raw(d + i * 32, &e->cell[i]);
    e->ncells = n;
    free(d);
    return 0;
}

/* font.grp = ZELRES1[12], a 3-section container {u16 hdr=6, u16 off2, u16 off3}.
 * Section 2 (off2..off3) holds the 6x7 digit glyphs; the MCGA variant stores
 * 8 bytes per glyph (bits 5..0 of rows 0..6), the EGA mode-0 stream 16 (16 px). */
int gfx_load_digits(DigitFont *f, const char *dir)
{
    memset(f, 0, sizeof *f);
    size_t len;
    uint8_t *d = sar_load(dir, 0, 12, 1, &len);
    if (!d) return -1;
    if (len < 6) { free(d); return -1; }
    size_t o2 = d[2] | d[3] << 8, o3 = d[4] | d[5] << 8;
    if (o3 <= o2 || o3 > len) { free(d); return -1; }
    size_t span = o3 - o2, stride = span >= 160 ? 16 : 8;
    if (span < 10 * 8) { free(d); return -1; }
    for (int g = 0; g < 10; g++) {
        const uint8_t *p = d + o2 + (size_t)g * stride;
        for (int r = 0; r < 7; r++) {
            if (stride == 8) f->glyph[g][r] = (uint8_t)(p[r] & 0x3F);
            else {                                  /* 16-px EGA glyph: take every 2nd pixel */
                unsigned v = p[2 * r] << 8 | p[2 * r + 1];
                uint8_t b = 0;
                for (int c = 0; c < 6; c++) if ((v >> (15 - (4 + 2 * c))) & 1) b |= (uint8_t)(0x20 >> c);
                f->glyph[g][r] = b;
            }
        }
    }
    f->loaded = 1;
    free(d);
    return 0;
}

/* ------------------------------------------------------------------------ */
/* mole.bin (ZELRES2[7]) — the boot-time screen furniture (see gfx.h)        */
/* ------------------------------------------------------------------------ */

/* mole.bin 0x454: an RLE over one byte.  `b & 0xF0` == the block's own run
 * marker gives `b & 0x0F` copies of 0xAA, 0x40 gives that many 0x00, 0xD0
 * (only where the block enables it) that many 0xFF; anything else is one
 * literal byte.  0x00 ends the stream. */
static void mole_unpack(const uint8_t *img, size_t len, size_t off, uint8_t marker, int allow_ff,
                        uint8_t *out, size_t outlen)
{
    size_t o = 0;
    memset(out, 0, outlen);
    while (off < len) {
        uint8_t b = img[off++];
        if (!b) return;
        uint8_t hi = (uint8_t)(b & 0xF0);
        int n = b & 0x0F;
        uint8_t v;
        if (hi == marker)                  v = 0xAA;
        else if (hi == 0x40)               v = 0x00;
        else if (allow_ff && hi == 0xD0)   v = 0xFF;
        else                             { v = b; n = 1; }
        if (!n) n = 256;
        while (n-- > 0 && o < outlen) out[o++] = v;
    }
}

/* mole.bin 0x24B/0x294: two planes, 2 bits per PC-88 pixel; each source byte
 * is 4 screen pixels, and the 4-bit (left<<2 | right) pair indexes the
 * 16-entry table at 0x2AE, which is already in the driver's `l<<3 | r` form. */
static void mole_blit(ScreenFrame *f, const uint8_t *s1, const uint8_t *s2,
                      const uint8_t *pal, int x4, int y, int wbytes, int rows)
{
    for (int r = 0; r < rows; r++) {
        int di = (y + r) * SCREEN_FRAME_W + x4 * 4;
        for (int c = 0; c < wbytes; c++) {
            uint8_t dh = s2[r * wbytes + c], dl = s1[r * wbytes + c];
            for (int k = 0; k < 4; k++) {
                int i = ((dh >> (7 - 2 * k)) & 1) << 3 | ((dl >> (7 - 2 * k)) & 1) << 2
                      | ((dh >> (6 - 2 * k)) & 1) << 1 | ((dl >> (6 - 2 * k)) & 1);
                if (di >= 0 && di < SCREEN_FRAME_W * SCREEN_FRAME_H) {
                    f->px[di] = pal[i];
                    f->on[di] = 1;
                }
                di++;
            }
        }
    }
}

int gfx_load_screen_frame(ScreenFrame *f, const char *dir)
{
    memset(f, 0, sizeof *f);
    size_t len;
    uint8_t *d = sar_load(dir, 1, 7, 1, &len);              /* ZELRES2[7] = mole.bin */
    if (!d) return -1;
    if (len < 0x2800) { free(d); return -1; }
    const uint8_t *pal = d + 0x2AE;                          /* 0x294's 16-entry table */
    /* {stream 1, stream 2 (0 = a blank plane), run marker, 0xD0 allowed, x4, y,
     * bytes per row, rows} — the four calls at 0x0E, 0x34, 0x55 and 0x80 */
    static const struct { unsigned s1, s2; uint8_t marker; uint8_t ff; int x4, y, w, rows; } BLK[4] = {
        {0x04AE, 0x073D, 0x90, 0, 0x0C, 0x00, 0x38, 0x0D},
        {0x08CD, 0x10DB, 0x10, 0, 0x00, 0x00, 0x0C, 0xC8},
        {0x1861, 0x2088, 0x10, 0, 0x44, 0x00, 0x0C, 0xC8},
        {0x2799, 0,      0x50, 1, 0x0C, 0x9E, 0x38, 0x2A},
    };
    static uint8_t buf1[2400], buf2[2400];                   /* 0x2926 / 0x3286, BP = 0x960 apart */
    for (int i = 0; i < 4; i++) {
        mole_unpack(d, len, BLK[i].s1, BLK[i].marker, BLK[i].ff, buf1, sizeof buf1);
        if (BLK[i].s2) mole_unpack(d, len, BLK[i].s2, BLK[i].marker, BLK[i].ff, buf2, sizeof buf2);
        else           memset(buf2, 0, sizeof buf2);         /* 0x89: the second plane is zeroed */
        mole_blit(f, buf1, buf2, pal, BLK[i].x4, BLK[i].y, BLK[i].w, BLK[i].rows);
    }
    /* 0x38C/0x3B3: 10 bytes per mark, 2 bits per pixel OR-ed in as colour 4 */
    unsigned si = 0x49A;
    static const int MARK[2] = {47 * SCREEN_FRAME_W + 8, 47 * SCREEN_FRAME_W + 304};
    for (int m = 0; m < 2; m++)
        for (int r = 0; r < 5; r++) {
            int di = MARK[m] + r * SCREEN_FRAME_W;
            for (int b = 0; b < 2; b++) {
                uint8_t al = d[si++];
                for (int k = 0; k < 4; k++) {
                    int hi = (al >> (7 - 2 * k)) & 1, lo = (al >> (6 - 2 * k)) & 1;
                    f->px[di] |= (uint8_t)(hi << 5 | lo << 2);
                    f->on[di] = 1;
                    di++;
                }
            }
        }
    free(d);
    f->loaded = 1;
    return 0;
}

/* ------------------------------------------------------------------------ */
/* encnt.grp — the encounter card (see gfx.h)                                */
/* ------------------------------------------------------------------------ */
int gfx_load_encounter(EncounterCard *c, const char *dir)
{
    memset(c, 0, sizeof *c);
    size_t glen, elen;
    uint8_t *gf = sar_load(dir, 1, 6, 1, &glen);            /* ZELRES2[6] = gfmcga.bin @3000 */
    uint8_t *en = sar_load(dir, 2, 55, 1, &elen);           /* ZELRES3[55] = encnt.grp */
    if (!gf || !en || glen < 0x4588 - 0x3000 + 140) { free(gf); free(en); return -1; }
    const uint8_t *map = gf + (0x4588 - 0x3000);            /* 5 rows x 28 cell indices */
    for (int cr = 0; cr < 5; cr++)
        for (int cc = 0; cc < 28; cc++) {
            unsigned cell = map[cr * 28 + cc];
            if ((cell + 1u) * 16u > elen) continue;
            const uint8_t *src = en + cell * 16;
            for (int r = 0; r < 8; r++) {
                unsigned w = (unsigned)src[r * 2] << 8 | src[r * 2 + 1];   /* 4550: xchg al,ah */
                for (int k = 0; k < 8; k++) {
                    unsigned v = (w >> (14 - 2 * k)) & 3;                  /* 4092 */
                    uint8_t px = v == 0 ? 0x00 : v == 3 ? 0x12 : 0x10;
                    c->px[(cr * 8 + r) * ENCNT_W + cc * 8 + k] = px;
                }
            }
        }
    free(gf); free(en);
    c->loaded = 1;
    return 0;
}
