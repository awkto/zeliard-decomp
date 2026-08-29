/* gd.c — gdmcga, the intro/ending art renderer (docs/CUTSCENES.md §1/§3/§6).
 *
 * Hex tags in this file are addresses in GDMCGA.BIN, which is parked at
 * BASE:3000, so a file offset is `addr - 0x3000`.  The palette base table is
 * read straight out of that image, exactly as the shops read their text out of
 * theirs — no constants are copied into the port. */
#include <stdlib.h>
#include <string.h>
#include "gd.h"
#include "sar.h"

#define GD_IMG(g, addr) ((g)->img + ((addr) - 0x3000))

const uint8_t GD_W_P3[3]  = { 1, 2, 4 };
const uint8_t GD_W_P2H[2] = { 1, 8 };
const uint8_t GD_W_P12[2] = { 1, 2 };
const uint8_t GD_W_P21[2] = { 2, 1 };

/* 32B9 / 32C1 — the dissolve masks, even and odd rows (31B4) */
static const uint8_t DISS_EVEN[8] = { 0x80, 0x20, 0x08, 0x02, 0x40, 0x10, 0x04, 0x01 };
static const uint8_t DISS_ODD [8] = { 0x01, 0x04, 0x10, 0x40, 0x02, 0x08, 0x20, 0x80 };

/* ---------------------------------------------------------------- format */

size_t gd_unpack_mask(const uint8_t *src, size_t len, uint8_t *dst, size_t cap, int delta)
{                                                                   /* 6D5E */
    if (len < 2) return 0;
    size_t n = (size_t)src[0] | ((size_t)src[1] << 8);              /* 6D63 */
    const uint8_t *mask = src + 2, *data = mask + n;
    size_t o = 0, di = 0, avail = len > 2 + n ? len - 2 - n : 0;
    for (size_t i = 0; i < n; i++) {
        uint8_t m = mask[i];
        for (int b = 0; b < 8; b++) {
            uint8_t v = 0;
            if (m & (0x80 >> b)) v = di < avail ? data[di++] : 0;    /* 6D72 */
            if (o < cap) dst[o] = v;
            o++;
        }
    }
    if (o > cap) o = cap;
    if (delta) {                                                    /* 6D8D */
        uint8_t prev = 0;                     /* the accumulator runs across
                                                 the whole buffer, not per byte */
        for (size_t i = 0; i < o; i++) {
            uint8_t b = dst[i], out = 0;
            for (int k = 0; k < 4; k++) {
                prev ^= (uint8_t)((b >> (6 - 2 * k)) & 3);
                out = (uint8_t)((out << 2) | prev);
            }
            dst[i] = out;
        }
    }
    return o;
}

size_t gd_unpack_rle(const uint8_t *src, size_t len, uint8_t *dst, size_t cap)
{                                                                   /* 6DE1 */
    size_t i = 0, o = 0;
    while (i < len) {
        unsigned v, n;
        if (src[i] & 0x40) {                          /* 16-bit big-endian */
            if (i + 1 >= len) break;
            v = ((unsigned)src[i] << 8) | src[i + 1];
            i += 2;
            if (v == 0xFFFF) break;                                 /* 6DEB */
            n = v & 0x3FFF;
        } else {
            v = (unsigned)src[i] << 8;
            n = src[i] & 0x3F;
            i++;
        }
        if (v & 0x8000) {                                           /* run */
            if (i >= len) break;
            uint8_t b = src[i++];
            while (n--) { if (o < cap) dst[o] = b; o++; }
        } else {                                                /* literal */
            while (n--) { uint8_t b = i < len ? src[i++] : 0; if (o < cap) dst[o] = b; o++; }
        }
    }
    return o > cap ? cap : o;
}

/* -------------------------------------------------------------- palette */

uint8_t gd_dac8(uint8_t v)
{
    /* DOSBox/VGA expands a 6-bit DAC component as (v<<2)|(v>>4); on the values
     * these ten records use that is *not* round(v*255/63) — docs/CUTSCENES.md §3 */
    return (uint8_t)((v << 2) | (v >> 4));
}

void gd_set_palette(Gd *g, int rec)                                 /* 4221 */
{
    if (rec < 0 || rec > 9) return;
    g->pal_rec = rec;
    for (int l = 0; l < 16; l++)
        for (int r = 0; r < 16; r++)
            for (int k = 0; k < 3; k++)
                g->dac[l * 16 + r][k] = gd_dac8((uint8_t)(g->base[rec][l][k] + g->base[rec][r][k]));
}

void gd_to_rgb(const Gd *g, const uint8_t *fb, uint8_t *rgb)
{
    for (int i = 0; i < GD_W * GD_H; i++) {
        const uint8_t *c = g->dac[fb[i]];
        rgb[i * 3] = c[0]; rgb[i * 3 + 1] = c[1]; rgb[i * 3 + 2] = c[2];
    }
}

int gd_init(Gd *g, const char *dir, uint8_t *fb, uint8_t *scratch, const TextFont *font)
{
    memset(g, 0, sizeof *g);
    g->fb = fb; g->scratch = scratch; g->font = font;
    size_t len = 0;
    g->img = sar_load(dir, 0, 5, 1, &len);                /* ZELRES1[5] gdmcga */
    if (!g->img) return -1;
    g->imglen = len;
    if (len < (size_t)(0x4289 - 0x3000) + 10 * 0x30) { free(g->img); g->img = NULL; return -1; }
    const uint8_t *p = GD_IMG(g, 0x4289);                 /* ten 48-byte records */
    for (int rec = 0; rec < 10; rec++)
        for (int l = 0; l < 16; l++)
            for (int k = 0; k < 3; k++)
                g->base[rec][l][k] = p[rec * 0x30 + l * 3 + k];
    gd_set_palette(g, 0);
    g->loaded = 1;
    return 0;
}

void gd_free(Gd *g)
{
    free(g->img); g->img = NULL; g->loaded = 0;
}

/* --------------------------------------------------------------- pixels */

void gd_expand_row(const uint8_t *src, size_t planesz, int wbytes, int row,
                   const uint8_t *weights, int nplanes, uint8_t *out)
{                                                                   /* 4469 */
    for (int xb = 0; xb < wbytes; xb++) {
        uint8_t pl[4];
        for (int i = 0; i < nplanes; i++)
            pl[i] = src[i * planesz + (size_t)row * wbytes + xb];
        uint8_t px[8];
        for (int b = 0; b < 8; b++) {
            uint8_t v = 0;
            for (int i = 0; i < nplanes; i++)
                if (pl[i] & (0x80 >> b)) v |= weights[i];
            px[b] = v;
        }
        for (int k = 0; k < 4; k++)
            out[xb * 4 + k] = (uint8_t)((px[2 * k] << 4) | px[2 * k + 1]);
    }
}


/* ---------------------------------------------------------------- writers */
/* Every draw slot packs the picture into a staging buffer first (the original
 * uses the 64 KB at CS+0x3000) and then blits it to the screen through one of
 * three writers, either straight or through the 8-pass dissolve at 31B4. */

static uint8_t STAGE[GD_W * GD_H * 2];

/* pack `src` (nplanes planes of wbytes*rows) into STAGE, stride wbytes*4 */
static void stage_pack(const uint8_t *src, int wbytes, int rows,
                       const uint8_t *weights, int nplanes)
{
    size_t planesz = (size_t)wbytes * rows, w = (size_t)wbytes * 4;
    if (w * rows > sizeof STAGE) return;
    for (int r = 0; r < rows; r++)
        gd_expand_row(src, planesz, wbytes, r, weights, nplanes, STAGE + (size_t)r * w);
}

/* 30FC — the `ao` recolour: both planes -> 8, plane 0 only -> 10, plane 1 only
 * -> 12, neither -> 0 (weights 8 for "either", 2 for "p0 only", 4 for "p1 only") */
static void stage_pack_ao(const uint8_t *src, int wbytes, int rows)
{
    size_t planesz = (size_t)wbytes * rows, w = (size_t)wbytes * 4;
    if (w * rows > sizeof STAGE) return;
    for (int r = 0; r < rows; r++) {
        uint8_t *out = STAGE + (size_t)r * w;
        for (int xb = 0; xb < wbytes; xb++) {
            uint8_t a = src[(size_t)r * wbytes + xb];
            uint8_t b = src[planesz + (size_t)r * wbytes + xb];
            uint8_t px[8];
            for (int i = 0; i < 8; i++) {
                int pa = (a >> (7 - i)) & 1, pb = (b >> (7 - i)) & 1;
                px[i] = (uint8_t)(!(pa | pb) ? 0 : (pa & pb) ? 8 : pb ? 12 : 10);
            }
            for (int k = 0; k < 4; k++)
                out[xb * 4 + k] = (uint8_t)((px[2 * k] << 4) | px[2 * k + 1]);
        }
    }
}

enum { W_COPY = 0, W_OR = 1, W_TRANSPARENT = 2, W_AND = 3 };

/* 3239 / 3277 / 329D — one row of one dissolve pass.  `mask` selects one byte in
 * eight; column 0 uses bit 7 (the writers `rol bl,1` before testing CF). */
static void write_row(Gd *g, int x, int y, const uint8_t *row, int n, uint8_t mask, int writer)
{
    if (y < 0 || y >= GD_H) return;
    uint8_t *d = g->fb + (size_t)y * GD_W;
    for (int i = 0; i < n; i++) {
        int on = (mask & (0x80 >> (i & 7))) != 0;
        int xx = x + i;
        if (xx < 0 || xx >= GD_W) continue;
        switch (writer) {
        case W_COPY:        if (on) d[xx] = row[i]; break;
        case W_OR:          if (on) d[xx] |= row[i]; break;
        case W_TRANSPARENT: if (on && row[i]) d[xx] = row[i]; break;
        case W_AND:         if (on) d[xx] = 0; break;
        }
    }
}

static int gd_wait(Gd *g, int ticks)
{
    return g->wait ? g->wait(g->user, ticks) : 0;
}

/* 31B4 — eight passes; in pass p, row r uses mask_table[(p + r) & 7], even rows
 * from 32B9 and odd rows from 32C1, and waits 0x14 ticks between passes. */
static int dissolve(Gd *g, int x, int y, int w, int rows, int writer)
{
    for (int p = 0; p < 8; p++) {
        for (int r = 0; r < rows; r++) {
            uint8_t m = (r & 1) ? DISS_ODD[(p + r) & 7] : DISS_EVEN[(p + r) & 7];
            write_row(g, x, y + r, STAGE + (size_t)r * w, w, m, writer);
        }
        if (gd_wait(g, 0x14)) return 1;
    }
    return 0;
}

void gd_blit(Gd *g, const uint8_t *src, int x4, int y, int wbytes, int rows,
             const uint8_t *weights, int nplanes)                    /* 33B7 */
{
    int w = wbytes * 4;
    stage_pack(src, wbytes, rows, weights, nplanes);
    for (int r = 0; r < rows; r++)                                   /* 340D */
        write_row(g, x4 * 4, y + r, STAGE + (size_t)r * w, w, 0xFF, W_COPY);
}

int gd_draw(Gd *g, const uint8_t *src, int x4, int y, int wbytes, int rows,
            const uint8_t *weights, int nplanes, int al)             /* 3032/3088 */
{
    int w = wbytes * 4;
    stage_pack(src, wbytes, rows, weights, nplanes);
    if (al == 0 && dissolve(g, x4 * 4, y, w, rows, W_OR)) return 1;  /* 3189 */
    return dissolve(g, x4 * 4, y, w, rows, W_COPY);
}

int gd_draw_ao(Gd *g, const uint8_t *src, int x4, int y, int wbytes, int rows)
{                                                                    /* 30FC */
    int w = wbytes * 4;
    stage_pack_ao(src, wbytes, rows);
    if (dissolve(g, x4 * 4, y, w, rows, W_OR)) return 1;             /* 315E */
    return dissolve(g, x4 * 4, y, w, rows, W_TRANSPARENT);
}

int gd_erase(Gd *g, int x4, int y, int wbytes, int rows)             /* 30E4 */
{
    int w = wbytes * 4;
    if (w * rows > (int)sizeof STAGE) return 0;
    memset(STAGE, 0, (size_t)w * rows);
    return dissolve(g, x4 * 4, y, w, rows, W_AND);
}

/* 3C1C — the plane-masked draw.  The blit is a private 8-pass interlace over the
 * fixed 288 x 104 picture window at screen (16,16): pass p draws window rows
 * p, p+8, p+16 ... and every row is *cleared* outside the picture, so the slot
 * both puts the picture up and wipes the rest of waku.grp's inset. */
int gd_draw_masked(Gd *g, int mask, const uint8_t *src, int x4, int y,
                   int wbytes, int rows)
{
    uint8_t w[3]; int np = 0;
    for (int b = 0; b < 3; b++) if (mask & (1 << b)) w[np++] = (uint8_t)(1 << b);
    if (!np) return 0;
    stage_pack(src, wbytes, rows, w, np);
    int col0 = x4 - 4, row0 = y - 0x10;              /* 3C63: bx -= 0x410 */
    int sw = wbytes * 4;
    for (int p = 0; p < 8; p++) {
        for (int k = 0; k < 13; k++) {               /* 13 rows a pass, stride 8 */
            int wy = p + k * 8;
            if (wy >= 104) continue;
            uint8_t *d = g->fb + (size_t)(wy + 0x10) * GD_W + 16;
            if (wy < row0 || wy >= row0 + rows) { memset(d, 0, 288); continue; }
            const uint8_t *s = STAGE + (size_t)(wy - row0) * sw;
            for (int j = 0; j < 72; j++) {           /* 72 columns of 4 bytes */
                if (j >= col0 && j < col0 + wbytes) memcpy(d + j * 4, s + (j - col0) * 4, 4);
                else memset(d + j * 4, 0, 4);
            }
        }
        if (gd_wait(g, 0x14)) return 1;
    }
    return 0;
}

/* 3E35 — the colour swap Jashiin's portrait gets: 3 -> 0, 4 -> 3, in place. */
void gd_fx_recolour(Gd *g, uint8_t *pic, int x4, int y)
{
    const size_t P = 0x1028;                          /* 47 * 88 */
    for (size_t i = 0; i < P; i++) {
        uint8_t *p0 = pic + i, *p1 = p0 + P, *p2 = p1 + P;
        uint8_t m = (uint8_t)~(*p0 & *p1 & (uint8_t)~*p2);
        *p0 &= m; *p1 &= m; *p2 &= m;
        uint8_t q = (uint8_t)(*p2 & (uint8_t)~*p0 & (uint8_t)~*p1);
        *p0 |= q; *p1 |= q; *p2 &= (uint8_t)~q;
    }
    gd_blit(g, pic, x4, y, 0x2F, 0x58, GD_W_P3, 3);
}

/* ------------------------------------------------------------ primitives */

void gd_sky_dither(Gd *g)                                           /* 3707 */
{
    for (int y = 0; y < GD_H; y++) {
        uint8_t *d = g->fb + (size_t)y * GD_W;
        for (int x = 0; x < GD_W; x++)
            d[x] = (uint8_t)(((x + y) & 1) ? 0x10 : 0x00);
    }
}

/* 3E1C — the 7-byte dithered shadow the picture box leaves down its left edge */
static void box_shadow(Gd *g, int x, int y)
{
    if (y < 0 || y >= GD_H) return;
    for (int i = 7; i >= 0; i--) {
        int xx = x - i;
        if (xx >= 0 && xx < GD_W) g->fb[(size_t)y * GD_W + xx] = 0x02;
    }
}

static void box_row(Gd *g, int x, int y, int w, int style)
{
    box_shadow(g, x, y);
    if (y < 0 || y >= GD_H) return;
    uint8_t *d = g->fb + (size_t)y * GD_W;
    if (style == 0) {                                    /* 3DEA solid */
        for (int i = 0; i < w; i++) if (x + i < GD_W) d[x + i] = 0xFF;
        return;
    }
    if (style == 1) {                                    /* 3DF7 hollow */
        if (x < GD_W) d[x] = 0xFF;
        for (int i = 1; i < w - 1; i++) if (x + i < GD_W) d[x + i] = 0;
        if (x + w - 1 < GD_W) d[x + w - 1] = 0xFF;
        return;
    }
    /* 3DAE the middle rows: the two 4-byte margins are cleared, the interior
     * (the picture) is left alone */
    if (x < GD_W) d[x] = 0xFF;
    for (int i = 1; i < 4; i++) if (x + i < GD_W) d[x + i] = 0;
    if (x + w - 1 < GD_W) d[x + w - 1] = 0xFF;
    for (int i = 2; i < 5; i++) if (x + w - i < GD_W) d[x + w - i] = 0;
}

void gd_picture_box(Gd *g, int x4, int y, int w4, int rows)         /* 3D79 */
{
    int x = x4 * 4, w = w4 * 4;
    box_row(g, x, y, w, 0);
    box_row(g, x, y + 1, w, 1);
    box_row(g, x, y + 2, w, 1);
    for (int r = 3; r < rows - 2; r++) box_row(g, x, y + r, w, 2);
    box_row(g, x, y + rows - 2, w, 1);
    box_row(g, x, y + rows - 1, w, 0);
}

void gd_cursor_block(Gd *g, int colour, int x4, int y)              /* 4205 */
{
    int x = x4 * 4;
    for (int r = 0; r < 8; r++) {
        int yy = y + r;
        if (yy < 0 || yy >= GD_H) continue;
        for (int i = 0; i < 8; i++)
            if (x + i >= 0 && x + i < GD_W) g->fb[(size_t)yy * GD_W + x + i] = (uint8_t)colour;
    }
}

void gd_putchar(Gd *g, uint8_t ch, int colour, int x, int y)        /* 44DE -> 2022 */
{
    if (!g->font || ch < 0x20 || ch >= 0x80) return;
    /* video_mcga 27E9 with [FF77] set: the ink byte is (colour<<4) | colour */
    uint8_t ink = (uint8_t)(((colour & 0x0F) << 4) | (colour & 0x0F));
    const uint8_t *gl = g->font->glyph[ch - 0x20];
    for (int r = 0; r < 8; r++) {
        int yy = y + r;
        if (yy < 0 || yy >= GD_H) continue;
        uint8_t bits = gl[r];
        for (int b = 0; b < 8; b++)
            if (bits & (0x80 >> b)) {
                int xx = x + b;
                if (xx >= 0 && xx < GD_W) g->fb[(size_t)yy * GD_W + xx] = ink;
            }
    }
}

void gd_window(Gd *g, int style, int x4, int y, int w4, int rows)   /* 2000 */
{
    int x = x4 * 4, w = w4 * 4;
    for (int r = 0; r < rows; r++) {
        int yy = y + r;
        if (yy < 0 || yy >= GD_H) continue;
        uint8_t *d = g->fb + (size_t)yy * GD_W;
        for (int i = 0; i < w; i++) if (x + i >= 0 && x + i < GD_W) d[x + i] = 0;
    }
    if (!style) return;
    for (int i = 2; i < w - 2; i++) {
        for (int k = 0; k < 2; k++) {
            int yy = y + k;      if (yy >= 0 && yy < GD_H && x + i < GD_W) g->fb[(size_t)yy * GD_W + x + i] = 0xFF;
            yy = y + rows - 1 - k; if (yy >= 0 && yy < GD_H && x + i < GD_W) g->fb[(size_t)yy * GD_W + x + i] = 0xFF;
        }
    }
    for (int r = 2; r < rows - 2; r++) {
        int yy = y + r;
        if (yy < 0 || yy >= GD_H) continue;
        for (int k = 0; k < 2; k++) {
            if (x + k < GD_W) g->fb[(size_t)yy * GD_W + x + k] = 0xFF;
            if (x + w - 1 - k < GD_W) g->fb[(size_t)yy * GD_W + x + w - 1 - k] = 0xFF;
        }
    }
}

void gd_puts(Gd *g, const uint8_t *s, int x, int y)                 /* 202A */
{
    int colour = 7, cx = x, cy = y;                    /* colour 7 with [FF77] */
    for (;;) {
        uint8_t c = *s++;
        if (c == 0xFF) return;
        if (c == 0x0D) { cy += 8; cx = x; continue; }
        if (c & 0x80) { colour = c & 7; continue; }
        gd_putchar(g, c, colour, cx, cy);
        cx += 8;
    }
}

void gd_clear_scratch(Gd *g)                                        /* 44CC */
{
    if (g->scratch) memset(g->scratch, 0, GD_SCRATCH);
}

/* ------------------------------------------------------------------ text */
/* 32C9 — one line of a text block into the 320 x 10 buffer at 4511, one byte
 * per pixel (0x00 / 0xFF), 8 px per glyph, the video driver's own [F500] font.
 * A line ends at 0xFF (end of block) or at any byte below 0x20 (i.e. 0x0D). */
const uint8_t *gd_text_line(Gd *g, const uint8_t *s)
{
    memset(g->textbuf, 0, sizeof g->textbuf);
    int x = 0;
    for (;;) {
        uint8_t c = *s++;
        if (c == 0xFF || c < 0x20) return s;
        if (c == ' ') { x += 8; continue; }
        if (g->font && c < 0x80 && x + 8 <= GD_W) {
            const uint8_t *gl = g->font->glyph[c - 0x20];
            for (int r = 0; r < 8; r++) {
                uint8_t bits = gl[r];
                for (int b = 0; b < 8; b++)
                    g->textbuf[(size_t)r * GD_W + x + b] = (bits & (0x80 >> b)) ? 0xFF : 0;
            }
        }
        x += 8;
    }
}

void gd_text_scroll(Gd *g, int row, int x4, int y, int w4, int rows)  /* 332C */
{
    if (!g->scratch) return;
    memmove(g->scratch, g->scratch + GD_W, GD_SCRATCH - GD_W);
    memset(g->scratch + GD_SCRATCH - GD_W, 0, GD_W);
    int dst = y + rows;
    if (row >= 0 && row < 10 && dst >= 0 && (size_t)(dst + 1) * GD_W <= GD_SCRATCH)
        memcpy(g->scratch + (size_t)dst * GD_W, g->textbuf + (size_t)row * GD_W, GD_W);
    int x = x4 * 4, w = w4 * 4;
    for (int r = 0; r < rows; r++) {
        int yy = y + r;
        if (yy < 0 || yy >= GD_H) continue;
        uint8_t *d = g->fb + (size_t)yy * GD_W;
        const uint8_t *sc = g->scratch + (size_t)yy * GD_W;
        for (int i = 0; i < w; i++) {
            int xx = x + i;
            if (xx < 0 || xx >= GD_W) continue;
            d[xx] = (uint8_t)((d[xx] & 0x99) | (sc[xx] & 0x66));
        }
    }
}

/* --------------------------------------------------------- face frames */
/* 364F: plane 1 -> weight 1, plane 0 -> weight 2; 36AB: plane 0 -> 1, plane 1
 * -> 2.  Both blit straight (340D), no dissolve. */
void gd_face_eyes(Gd *g, const uint8_t *bank, int frame, int x4, int y)   /* 364F */
{
    gd_blit(g, bank + (size_t)frame * 0xCC0, x4, y, 34, 48, GD_W_P21, 2);
}

void gd_face_mouth(Gd *g, const uint8_t *bank, int frame, int x4, int y)  /* 36AB */
{
    gd_blit(g, bank + (size_t)frame * 0x480, x4, y, 18, 32, GD_W_P12, 2);
}

/* ------------------------------------------------------- the title screen */
/* 3732 — 25 x 34 map of 8-row tiles out of the 40 x 40 ttl2 bank into a
 * 34 x 200 three-plane picture in the scratch (the third plane is read past
 * the two-plane bank, exactly as the original does). */
void gd_tile_map(Gd *g, const uint8_t *map, const uint8_t *ttl2)
{
    const size_t SRC_PLANE = 0x640, DST_PLANE = 0x1A90;
    if (!g->scratch) return;
    memset(g->scratch, 0, DST_PLANE * 3);
    for (int mr = 0; mr < 25; mr++)
        for (int mc = 0; mc < 34; mc++) {
            unsigned n = map[mr * 34 + mc];
            unsigned band = n / 40, x = n % 40;
            size_t si = (size_t)band * 0x140 + x;
            size_t di = (size_t)mr * 0x110 + mc;
            for (int p = 0; p < 3; p++) {
                size_t s = si + (size_t)p * SRC_PLANE, d = di + (size_t)p * DST_PLANE;
                for (int r = 0; r < 8; r++) { g->scratch[d] = ttl2[s]; d += 0x22; s += 0x28; }
            }
        }
}

/* 37B4 — draw scratch row `row`: the left half straight at screen bytes 0..135
 * and its pixel-mirrored copy at 184..319.  plane0&plane1 -> weight 1,
 * plane1 -> weights 2+4+8, so "both" = 15, "plane 1 only" = 14, "plane 0 only"
 * = 0 (opaque black) and "neither" is transparent. */
void gd_ornament_row(Gd *g, int row)
{
    if (!g->scratch || row < 0 || row >= 200) return;
    const size_t DST_PLANE = 0x1A90;
    uint8_t px[136], vis[136];
    for (int i = 0; i < 34; i++) {
        uint8_t a = g->scratch[(size_t)row * 0x22 + i];
        uint8_t b = g->scratch[DST_PLANE + (size_t)row * 0x22 + i];
        uint8_t v[8], t[8];
        for (int k = 0; k < 8; k++) {
            int pa = (a >> (7 - k)) & 1, pb = (b >> (7 - k)) & 1;
            v[k] = (uint8_t)((pa & pb) + (pb ? 14 : 0));
            t[k] = (uint8_t)!(pa | pb);            /* [4503]: 1 = transparent */
        }
        for (int k = 0; k < 4; k++) {
            px[i * 4 + k]  = (uint8_t)((v[2 * k] << 4) | v[2 * k + 1]);
            /* 44AB: the AND mask is 0xFF when *either* pixel of the byte is
             * transparent, so a half-transparent byte ORs onto the background
             * instead of replacing it */
            vis[i * 4 + k] = (uint8_t)((t[2 * k] | t[2 * k + 1]) ? 0xFF : 0x00);
        }
    }
    uint8_t *d = g->fb + (size_t)row * GD_W;
    for (int c = 0; c < 136; c++) {
        uint8_t mirror = (uint8_t)((px[c] << 4) | (px[c] >> 4));
        d[c] = (uint8_t)((d[c] & vis[c]) | px[c]);
        d[GD_W - 1 - c] = (uint8_t)((d[GD_W - 1 - c] & vis[c]) | mirror);
    }
}

/* --------------------------------------------------------------- effects */

int gd_storm(Gd *g, const uint8_t *drops, const uint8_t *bolts)      /* 3437 */
{
    /* the sprite table at 3617 is {u16 arena ptr, u8 rows, u8 wbytes} — CX is
     * read as a word from +2, so CL (rows) is byte +2 and CH (wbytes) byte +3 */
    struct Rec { int live, y, x4, dy, dx, tick, frame, fmax, di_x, di_y, w, h;
                 uint8_t bg[6 * 4 * 32]; } r[9];
    const uint8_t *spr   = GD_IMG(g, 0x3617);
    const uint8_t *flash = GD_IMG(g, 0x3637);
    static const uint8_t WSPR[2] = { 1 | 4, 2 };   /* plane 0 -> 5, plane 1 -> 2 */
    for (int i = 0; i < 9; i++) {
        r[i].live = 1;
        r[i].y = drops[i * 6 + 0]; r[i].x4 = drops[i * 6 + 1];
        r[i].dy = (int8_t)drops[i * 6 + 2]; r[i].dx = (int8_t)drops[i * 6 + 3];
        r[i].frame = drops[i * 6 + 4]; r[i].fmax = drops[i * 6 + 5];
        r[i].tick = 0; r[i].w = r[i].h = 0;
    }
    int phase = 0;
    for (int guard = 0; guard < 600; guard++) {
        /* A — advance every live record and save its background */
        for (int i = 0; i < 9; i++) {
            if (!r[i].live) continue;
            if (r[i].frame != r[i].fmax) { r[i].tick++; if (!(r[i].tick & 1)) r[i].frame++; }
            int f = r[i].frame & 7;
            int rows = spr[f * 4 + 2], wb = spr[f * 4 + 3];
            r[i].x4 = (r[i].x4 + r[i].dx) & 0xFF;
            r[i].y  = (r[i].y  + r[i].dy) & 0xFF;
            r[i].w = wb * 4; r[i].h = rows;
            r[i].di_x = r[i].x4 * 4; r[i].di_y = r[i].y;
            for (int yy = 0; yy < rows; yy++)
                for (int xx = 0; xx < r[i].w; xx++) {
                    int sx = r[i].di_x + xx, sy = r[i].di_y + yy;
                    r[i].bg[yy * 24 + xx] = (sx >= 0 && sx < GD_W && sy >= 0 && sy < GD_H)
                                          ? g->fb[(size_t)sy * GD_W + sx] : 0;
                }
        }
        /* B — the lightning flash (colour 0 of record 0, all three components)
         * and the sprite, once per record */
        for (int i = 0; i < 9; i++) {
            for (int k = 0; k < 3; k++) g->base[0][0][k] = flash[(phase & 7) * 3 + k];
            phase++;
            gd_set_palette(g, 0);
            if (!r[i].live) continue;
            if (r[i].x4 >= 0x4B || r[i].y >= 0xA0) { r[i].live = 0; continue; }
            int f = r[i].frame & 7;
            unsigned ptr = (unsigned)spr[f * 4] | ((unsigned)spr[f * 4 + 1] << 8);
            int rows = spr[f * 4 + 2], wb = spr[f * 4 + 3];
            const uint8_t *src = bolts + (ptr >= 0x9000 ? ptr - 0x9000 : ptr);
            uint8_t row[GD_W + 8];
            for (int yy = 0; yy < rows; yy++) {
                gd_expand_row(src, (size_t)wb * rows, wb, yy, WSPR, 2, row);
                int sy = r[i].di_y + yy;
                if (sy < 0 || sy >= GD_H) continue;
                for (int xx = 0; xx < wb * 4; xx++) {
                    int sx = r[i].di_x + xx;
                    if (sx >= 0 && sx < GD_W) g->fb[(size_t)sy * GD_W + sx] |= row[xx];
                }
            }
        }
        if (gd_wait(g, 0x1E)) break;
        /* C — restore every background */
        for (int i = 0; i < 9; i++) {
            if (!r[i].w) continue;
            for (int yy = 0; yy < r[i].h; yy++)
                for (int xx = 0; xx < r[i].w; xx++) {
                    int sx = r[i].di_x + xx, sy = r[i].di_y + yy;
                    if (sx >= 0 && sx < GD_W && sy >= 0 && sy < GD_H)
                        g->fb[(size_t)sy * GD_W + sx] = r[i].bg[yy * 24 + xx];
                }
        }
        int any = 0;
        for (int i = 0; i < 9; i++) any |= r[i].live;
        if (!any) break;
    }
    for (int k = 0; k < 3; k++) g->base[0][0][k] = GD_IMG(g, 0x4289)[k];
    gd_set_palette(g, 2);
    return 0;
}

/* 38E6 — the built-in dither effect.  Mode table 3C16, 24 pattern records at
 * 3A5F (4 dither rows + 4 solid rows), the border script at 3B1F and the
 * inward-spiral run lengths at 3BE3; a tile is 4 screen bytes x 4 rows. */
static void fx_tile(Gd *g, int id, int di, int phase0, int flags)
{
    const uint8_t *rec = GD_IMG(g, 0x3A5F) + (id - 1) * 8;
    for (int r = 0; r < 4; r++) {
        uint8_t phase = (uint8_t)phase0;
        if (r & 1) phase = (uint8_t)((phase >> 1) | (phase << 7));
        uint8_t b = rec[4 + r], t = (uint8_t)(rec[r] & phase);
        for (int k = 0; k < 8; k++) {
            int bit = (b >> (7 - k)) & 1, dith = (t >> (7 - k)) & 1;
            uint8_t v = (uint8_t)(bit ? 6 : 0);
            if (dith) { if (flags & 1) v |= 1; if (flags & 2) v |= 2; if (flags & 4) v |= 4; }
            int off = di + r * GD_W + k / 2;
            if (off < 0 || off >= GD_W * GD_H) continue;
            if (k & 1) g->fb[off] = (uint8_t)((g->fb[off] & 0xF0) | v);
            else       g->fb[off] = (uint8_t)((g->fb[off] & 0x0F) | (v << 4));
        }
    }
}

int gd_fx_sand(Gd *g, int variant)                                   /* 38E6 */
{
    if (variant < 0 || variant > 2) variant = 0;
    int flags  = GD_IMG(g, 0x3C16)[variant * 2];
    int phase0 = GD_IMG(g, 0x3C17)[variant * 2];
    int di = 16 * GD_W + 16;
    const uint8_t *si = GD_IMG(g, 0x3B1F);
    static const int STEP[4] = { 0x500, 4, -0x500, -4 };
    static const int CORNER[4] = { -1276, -1284, 1276, 1284 };
    for (int seg = 0; seg < 4; seg++) {                              /* phase 1 */
        uint8_t id;
        while ((id = *si++) != 0) { fx_tile(g, id, di, phase0, flags); di += STEP[seg]; }
        di += CORNER[seg];
    }
    si = GD_IMG(g, 0x3BE3);                                          /* phase 2 */
    for (;;) {
        int n, done = 0;
        for (int seg = 0; seg < 4 && !done; seg++) {
            n = *si++;
            if (!n) { done = 1; break; }
            while (n--) { fx_tile(g, 0x18, di, phase0, flags); di += STEP[seg]; }
            di -= STEP[seg];
        }
        if (done) break;
        if (gd_wait(g, 0x0C)) return 1;
    }
    return 0;
}

/* 3E8B — the converging aperture wipe of an 80 x 136 three-plane picture; rows
 * 22..112 draw only the frame, skipping the two 96-px talking-head windows. */
static void wipe_row(Gd *g, const uint8_t *src, int y)
{
    const size_t P = 0x2A80;
    uint8_t row[GD_W + 8];
    if (y < 0 || y >= 136) return;
    gd_expand_row(src, P, 80, y, GD_W_P3, 3, row);
    uint8_t *d = g->fb + (size_t)y * GD_W;
    if (y >= 0x16 && y < 0x71) {
        memcpy(d, row, 44);
        memcpy(d + 140, row + 140, 40);
        memcpy(d + 276, row + 276, 44);
    } else memcpy(d, row, GD_W);
}

int gd_wipe(Gd *g, const uint8_t *src)                               /* 3E8B */
{
    for (int cx = 0x44; cx; cx--) {
        wipe_row(g, src, 2 * (0x44 - cx));
        wipe_row(g, src, 2 * cx - 1);
        if (gd_wait(g, 4)) return 1;
    }
    return 0;
}

/* 4080 / 4162 — the ending's balcony door, a 47 x 114 picture drawn left-aligned
 * with a fixed width profile, opening from the top and bottom at once. */
int gd_end_open(Gd *g, const uint8_t *src)
{
    uint8_t row[GD_W + 8];
    for (int cx = 0x39; cx; cx--) {
        int ys[2] = { 2 * (0x39 - cx), 2 * cx - 1 };
        for (int i = 0; i < 2; i++) {
            int y = ys[i];
            if (y < 0 || y >= 114) continue;
            int n = y < 0x14 ? 0x2F : y < 0x17 ? 0x23 : y < 0x1C ? 0x21 : 0x21;
            int extra = (y >= 0x17 && y < 0x1C) ? 3 : 0;
            gd_expand_row(src, (size_t)47 * 114, 47, y, GD_W_P3, 3, row);
            memcpy(g->fb + (size_t)y * GD_W, row, (size_t)n * 4 + extra);
        }
        if (gd_wait(g, 4)) return 1;
    }
    return 0;
}

int gd_end_close(Gd *g, const uint8_t *src)                          /* 4162 */
{
    uint8_t row[GD_W + 8];
    for (int cx = 0x39; cx; cx--) {
        int ys[2] = { 2 * (0x39 - cx), 2 * cx - 1 };
        for (int i = 0; i < 2; i++) {
            int y = ys[i];
            if (y < 0 || y >= 114) continue;
            int sy = y + 0x14, dy = y + 0x14;
            if (dy >= GD_H) continue;
            uint8_t *d = g->fb + (size_t)dy * GD_W + 132;
            if (y >= 0x5E) { memset(d, 0, 188); continue; }
            if (sy >= 114) { memset(d, 0, 188); continue; }
            gd_expand_row(src, (size_t)47 * 114, 47, sy, GD_W_P3, 3, row);
            memcpy(d, row + 33 * 4, 56);
            memset(d + 56, 0, 132);
        }
        if (gd_wait(g, 4)) return 1;
    }
    return 0;
}

/* ------------------------------------------------------- the resource table */
/* docs/CUTSCENES.md §6.3 — the geometry each demo passes to gdmcga.  This is
 * the C form of `GD_ART` in tools/grp2png.py; `port/tools/compare_gdart.py`
 * diffs the two renderers over every entry. */
#define P(o, w, h, m, c, s) { (o), (w), (h), (m), (c), (s) }
const GdArt GD_ART[] = {
 { "nec.grp",   0, 0x17, 0, 2, { P(0,44,104,GDM_P3,1,0), P(13728,16,64,GDM_P3,1,0) }, 2 },
 { "hou.grp",   0, 0x12, 0, 2, { P(0,6,32,GDM_SPR,4,0x180), P(0x600,4,24,GDM_SPR,4,0xC0) }, 2 },
 { "dmaou.grp", 0, 0x0F, 0, 3, { P(0,18,32,GDM_P12,4,0x480), P(0x1380,34,48,GDM_P21,5,0xCC0) }, 2 },
 { "ttl1.grp",  0, 0x1E, 1, 4, { P(0,49,128,GDM_P3,1,0) }, 1 },
 { "ttl2.grp",  0, 0x1F, 1, 4, { P(0,40,40,GDM_P12,1,0) }, 1 },
 { "ttl3.grp",  0, 0x20, 1, 4, { P(0,65,112,GDM_AO,1,0) }, 1 },
 { "waku.grp",  0, 0x21, 0, 5, { P(0,80,136,GDM_P3,1,0) }, 1 },
 { "ame.grp",   0, 0x0E, 0, 5, { P(0,72,104,GDM_P3,1,0) }, 1 },
 { "hime.grp",  0, 0x10, 0, 6, { P(0,72,104,GDM_P3,1,0) }, 1 },
 { "isi.grp",   0, 0x13, 0, 7, { P(0,72,104,GDM_P3,1,0) }, 1 },
 { "oui.grp",   0, 0x1A, 0, 7, { P(0,72,104,GDM_P3,1,0) }, 1 },
 { "sei.grp",   0, 0x1C, 0, 7, { P(0,36,104,GDM_P14,1,0) }, 1 },
 { "yuu1.grp",  0, 0x22, 0, 7, { P(0,72,104,GDM_P3,1,0) }, 1 },
 { "yuu2.grp",  0, 0x23, 0, 7, { P(0,49,96,GDM_P3,1,0) }, 1 },
 { "yuu3.grp",  0, 0x24, 0, 1, { P(0,64,192,GDM_P2H,1,0) }, 1 },
 { "yuu4.grp",  0, 0x25, 0, 1, { P(0,21,160,GDM_P3,1,0) }, 1 },
 { "maop.grp",  0, 0x14, 0, 8, { P(0,47,88,GDM_P3,1,0) }, 1 },
 { "yuup.grp",  0, 0x26, 0, 6, { P(0,24,88,GDM_P3,1,0), P(0x18C0,9,32,GDM_P3,6,864),
                                 P(0x2D00,11,16,GDM_P3,6,528) }, 3 },
 { "oup.grp",   0, 0x1B, 0, 6, { P(0,24,88,GDM_P3,1,0), P(0x18C0,14,32,GDM_P3,6,1344),
                                 P(0x3840,11,16,GDM_P3,3,528) }, 3 },
 { "himp.grp",  0, 0x11, 0, 6, { P(0,24,88,GDM_P3,1,0) }, 1 },
 { "seip.grp",  0, 0x1D, 0, 6, { P(0,24,88,GDM_P3,1,0) }, 1 },
 { "new1.grp",  0, 0x18, 0, 6, { P(0,24,265,GDM_P3,1,0) }, 1 },
 { "new2.grp",  0, 0x19, 0, 7, { P(0,28,100,GDM_P3,1,0) }, 1 },
 { "ne80.grp",  0, 0x15, 0, 7, { P(0,26,100,GDM_P3,1,0) }, 1 },
 { "ne81.grp",  0, 0x16, 0, 7, { P(0,18,81,GDM_P3,1,0) }, 1 },
 { "end5.grp",  1, 0x36, 0, 7, { P(0,57,154,GDM_P3,1,0) }, 1 },
 { "end4.grp",  1, 0x35, 0, 7, { P(0,47,114,GDM_P3,1,0) }, 1 },
 { "end6.grp",  1, 0x37, 0, 7, { P(0,47,114,GDM_P3,1,0) }, 1 },
 { "end7.grp",  1, 0x38, 0, 7, { P(0,80,134,GDM_P12,1,0) }, 1 },
 { "en72.grp",  1, 0x34, 3, 7, { P(0,80,134,GDM_P1,1,0) }, 1 },
 { "fin.grp",   1, 0x39, 2, 7, { P(0,38,53,GDM_P1,2,2014) }, 1 },
};
#undef P
const int GD_ART_N = (int)(sizeof GD_ART / sizeof *GD_ART);

const GdArt *gd_art_find(const char *name)
{
    for (int i = 0; i < GD_ART_N; i++) if (!strcmp(GD_ART[i].name, name)) return &GD_ART[i];
    return NULL;
}

void gd_art_rows(const uint8_t *buf, unsigned off, int wbytes, int rows, int mode, uint8_t *out)
{
    static const uint8_t W14[2] = { 1, 4 }, W1[1] = { 1 }, WSPR[2] = { 1 | 4, 2 };
    const uint8_t *w = GD_W_P3; int np = 3;
    switch (mode) {
    case GDM_P2H: w = GD_W_P2H; np = 2; break;
    case GDM_P12: w = GD_W_P12; np = 2; break;
    case GDM_P21: w = GD_W_P21; np = 2; break;
    case GDM_P14: w = W14; np = 2; break;
    case GDM_P1:  w = W1;  np = 1; break;
    case GDM_SPR: w = WSPR; np = 2; break;
    case GDM_AO:  np = 2; break;
    default: break;
    }
    if (mode == GDM_AO) {
        size_t planesz = (size_t)wbytes * rows;
        for (int r = 0; r < rows; r++) {
            uint8_t *o = out + (size_t)r * wbytes * 4;
            for (int xb = 0; xb < wbytes; xb++) {
                uint8_t a = buf[off + (size_t)r * wbytes + xb];
                uint8_t b = buf[off + planesz + (size_t)r * wbytes + xb];
                uint8_t px[8];
                for (int i = 0; i < 8; i++) {
                    int pa = (a >> (7 - i)) & 1, pb = (b >> (7 - i)) & 1;
                    px[i] = (uint8_t)(!(pa | pb) ? 0 : (pa & pb) ? 8 : pb ? 12 : 10);
                }
                for (int k = 0; k < 4; k++)
                    o[xb * 4 + k] = (uint8_t)((px[2 * k] << 4) | px[2 * k + 1]);
            }
        }
        return;
    }
    for (int r = 0; r < rows; r++)
        gd_expand_row(buf + off, (size_t)wbytes * rows, wbytes, r, w, np,
                      out + (size_t)r * wbytes * 4);
}
