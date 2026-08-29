/* text.c — font.grp and the MCGA text/window primitives (docs/VIDEO_DRIVERS.md
 * §1.2 and §2.1, src/video_mcga.c).  Everything draws into a plain 320x200
 * index buffer, exactly as the driver draws into A000. */
#include "text.h"
#include "sar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* video_mcga @24EA: PC-88 colour c -> blend-DAC index c*9 */
const uint8_t PC88[8] = {0x00, 0x09, 0x12, 0x1B, 0x24, 0x2D, 0x36, 0x3F};

/* town.bin 7B82 */
const uint8_t FONT_XOFF[96] = {
    0,2,2,3,1,0,0,2,2,3,1,1,1,2,2,0, 1,2,1,1,1,1,1,1,1,1,3,2,1,1,2,1,
    0,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0, 0,0,0,0,1,0,0,0,0,0,1,2,2,2,1,1,
    1,0,0,1,0,1,1,0,0,2,1,0,2,0,1,1, 0,0,0,1,1,0,0,0,1,1,1,2,0,3,1,0,
};
/* town.bin 7BE2 */
const uint8_t FONT_ADVANCE[96] = {
    5,4,4,4,6,8,5,3,4,4,6,6,6,5,6,8, 7,5,7,7,7,7,7,7,7,7,3,4,6,6,6,7,
    8,8,8,8,8,8,8,8,8,5,8,8,8,8,8,8, 8,8,8,8,7,8,8,8,8,8,7,5,3,5,6,7,
    7,8,8,7,8,7,7,8,8,5,6,8,5,8,7,7, 8,8,8,7,6,8,8,8,7,7,7,4,8,4,7,8,
};

/* font.grp = ZELRES1[12]: {u16 6, u16 off1, u16 off2} then the three sections.
 * Section 0 = 8x8 text glyphs (8 bytes each from char 0x20), section 1 = the
 * 6x7 digits, section 2 = the narrow label glyphs. */
int text_load_font(TextFont *f, const char *dir)
{
    memset(f, 0, sizeof *f);
    size_t len;
    uint8_t *d = sar_load(dir, 0, 12, 1, &len);
    if (!d) return -1;
    if (len < 6) { free(d); return -1; }
    size_t o1 = (size_t)(d[2] | d[3] << 8), o2 = (size_t)(d[4] | d[5] << 8);
    if (o1 > len || o2 > len || o1 < 6 || o2 < o1) { free(d); return -1; }
    size_t stride0 = (o1 - 6) >= 96u * 16u ? 16 : 8;        /* the EGA stream is 16 px wide */
    for (int c = 0; c < 96; c++) {
        size_t o = 6 + (size_t)c * stride0;
        if (o + 8 <= o1) memcpy(f->glyph[c], d + o, 8);
    }
    size_t span1 = o2 - o1, stride1 = span1 >= 160 ? 16 : 8;
    for (int g = 0; g < 10; g++) {
        size_t o = o1 + (size_t)g * stride1;
        if (o + 7 <= o2) for (int r = 0; r < 7; r++) f->digit[g][r] = d[o + r] & 0x3F;
    }
    for (int c = 0; c < 96; c++) {
        size_t o = o2 + (size_t)c * 8;
        if (o + 8 <= len) memcpy(f->narrow[c], d + o, 8);
    }
    f->loaded = 1;
    free(d);
    return 0;
}

static inline void px(uint8_t *fb, int x, int y, uint8_t v)
{
    if (x >= 0 && x < TEXT_W && y >= 0 && y < TEXT_H) fb[y * TEXT_W + x] = v;
}

/* 0x2000 (MCGA 2046).  AL = 0: clear.  Otherwise a 2-px white frame with the
 * four corner pixels cut, interior cleared. */
void vid_window(uint8_t *fb, int style, int x4, int y, int w4, int h)
{
    int x = x4 * 4, w = w4 * 4;
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++) px(fb, x + c, y + r, 0);
    if (!style) return;
    /* the corner is a 2-px diagonal step, measured off
     * docs/screenshots/shop_armour.png: the outermost row is inset 2 px, the
     * next 1 px, the rest flush. */
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++) {
            int dr = r < h - 1 - r ? r : h - 1 - r;
            int dc = c < w - 1 - c ? c : w - 1 - c;
            if (dr > 1 && dc > 1) continue;                 /* interior */
            if (dr == 0 && dc < 2) continue;                /* cut corners */
            if (dr == 1 && dc < 1) continue;
            if (dc == 0 && dr < 2) continue;
            if (dc == 1 && dr < 1) continue;
            px(fb, x + c, y + r, PC88[1]);
        }
}

/* 0x2022: one glyph, only the set bits are written (transparent background) */
void vid_putchar(uint8_t *fb, const TextFont *f, uint8_t ch, int colour, int x, int y)
{
    if (!f || !f->loaded || ch < 0x20) return;
    const uint8_t *g = f->glyph[ch - 0x20];
    uint8_t v = PC88[colour & 7];
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
            if (g[r] & (0x80 >> c)) px(fb, x + c, y + r, v);
}

/* 0x202A: colour 1 initially; 0x0D = newline; bytes >= 0x80 set colour b & 7 */
void vid_puts(uint8_t *fb, const TextFont *f, const uint8_t *s, int x, int y)
{
    int colour = 1, cx = x;
    for (; *s != 0xFF; s++) {
        if (*s == 0x0D) { cx = x; y += 8; continue; }
        if (*s >= 0x80) { colour = *s & 7; continue; }
        vid_putchar(fb, f, *s, colour, cx, y);
        cx += 8;
    }
}

/* 0x2038 / 0x200E / 0x2010: the narrow font writes TWO pixels per set bit
 * ([di] = fg, [di+1] = bg) on a 5-px pitch — that is the drop-shadow look. */
void vid_label_narrow(uint8_t *fb, const TextFont *f, const char *s, int len, int x4, int y,
                      int xoff, int fg, int bg)
{
    if (!f || !f->loaded) return;
    int x = x4 * 4 + xoff;
    for (int i = 0; i < len && s[i]; i++) {
        uint8_t ch = (uint8_t)s[i];
        if (ch >= 0x20) {
            const uint8_t *g = f->narrow[ch - 0x20];
            for (int r = 0; r < 8; r++)
                for (int c = 0; c < 4; c++)
                    if (g[r] & (0x80 >> c)) {
                        px(fb, x + c, y + r, PC88[fg & 7]);
                        if (bg >= 0) px(fb, x + c + 1, y + r, PC88[bg & 7]);
                    }
        }
        x += 5;
    }
}

void vid_label_asciiz(uint8_t *fb, const TextFont *f, const char *s, int x4, int y, int xoff)
{
    vid_label_narrow(fb, f, s, (int)strlen(s), x4, y, xoff, 1, -1);
}

void vid_label_text(uint8_t *fb, const TextFont *f, const uint8_t *rec)
{
    if (!rec) return;
    vid_label_narrow(fb, f, (const char *)rec + 4, rec[3], rec[0], rec[1], rec[2], 1, 5);
}

void vid_draw_digits(uint8_t *fb, const TextFont *f, unsigned value, int n, int x, int y, int box)
{
    if (box)
        for (int i = 0; i < n; i++)
            for (int r = 0; r < 8; r++)
                for (int c = 0; c < 6; c++) px(fb, x + i * 6 + c, y + r, 0x28);
    for (int i = n - 1; i >= 0; i--, value /= 10) {
        unsigned d = value % 10;
        if (value == 0 && i != n - 1) return;
        int gx = x + i * 6 + 2;
        for (int r = 0; r < 7; r++)
            for (int c = 0; c < 6; c++)
                if (f && f->loaded && (f->digit[d][r] >> (5 - c)) & 1) px(fb, gx + c, y + r, PC88[1]);
    }
}

/* town.bin 72C7 / video [2032]: decimal, no leading zeros, 0xFF terminated */
int format_number(uint32_t v, char *out)
{
    char tmp[12];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    for (int i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    out[n] = 0;
    return n;
}

/* gtmcga 3716 GT_DRAW_CELL: cell AL of the bank at arena:8000 at (BH, BL).
 * BH is a **cell** column (8 px), not the 4-px unit the other slots use: the
 * armour shop's portrait table says "x4 7" and the DOSBox capture
 * docs/screenshots/shop_armour.png puts it at x = 56 = 7 * 8 (the yellow frame
 * measures exactly 12 x 8 cells at (56, 23)).  src/shops.c's "= 56 px" gloss
 * for the 0x0E bases is therefore off by a factor of two. */
void gt_draw_cell(uint8_t *fb, const Cell8 *bank, const uint8_t *present, int cell, int x8, int y)
{
    if (cell < 0 || cell > 255 || (present && !present[cell])) return;
    int x = x8 * 8;
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++) px(fb, x + c, y + r, bank[cell].px[r][c]);
}

/* gtmcga 3785 GT_CURSOR: the red menu arrow.  Measured off
 * docs/screenshots/shop_armour.png: 7 rows of 2,3,4,5,4,3,2 px starting one
 * pixel right and one pixel down from (BH*4, BL); the slot clears 9 rows. */
void gt_cursor(uint8_t *fb, int x4, int y, int on)
{
    static const uint8_t arrow[7] = {0xC0, 0xE0, 0xF0, 0xF8, 0xF0, 0xE0, 0xC0};
    int x = x4 * 4 + 1;
    for (int r = 0; r < 9; r++)
        for (int c = 0; c < 8; c++) {
            int set = (r >= 1 && r <= 7) && (arrow[r - 1] & (0x80 >> c));
            if (on) { if (set) px(fb, x + c, y + r, PC88[2]); }
            else px(fb, x + c, y + r, 0);
        }
}
