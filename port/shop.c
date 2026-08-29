/* shop.c — the shop overlays and the presentation layer they need.
 *
 * Sources: src/shops.c (every overlay decompiled, with the address of every
 * string and table), src/town.c 706C/7344/751A/7539/7469/74D3/7570/7589/72C7
 * (the text box, the menus and the gold helpers), docs/TOWN.md §7 (the price
 * and level tables) and docs/VIDEO_DRIVERS.md (the drawing slots).
 *
 * The overlay images are loaded raw the way town.bin 6E7E loads them, so all
 * the text, the names, the descriptions and the price tables are read out of
 * ZELRES2[10..17] at the documented addresses instead of being retyped. */
#include "shop.h"
#include "enemy.h"
#include "render.h"
#include "sar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define A(x) (x)                        /* overlay addresses are A000-based */
#define BOX_X4 0x0D
#define BOX_Y  0x60
#define BOX_W4 0x36
#define BOX_H  0x37
#define TEXT_ORIGIN_X 0x38              /* 706C: 56 px */
#define TEXT_ORIGIN_Y 0x63              /* 99 px */
#define TEXT_WRAP     0xD0              /* 208 px */

/* {overlay ZELRES2 index, portrait .grp index, label record, portrait map,
 *  portrait x4} — src/shops.c per-shop headers */
static const struct {
    int ovl, grp; unsigned label, portrait; int px4; const char *name;
} SHOPS[SHOP_COUNT] = {
    {10, 18, 0xA41A, 0xA16E, 0x0E, "king"},
    {11, 19, 0xA245, 0,      0x1E, "omoya"},
    {17, 25, 0,      0xA9B6, 0x07, "sage"},
    {12, 20, 0xACAE, 0,      0x07, "armour"},   /* armr: frame table AA10 */
    {15, 23, 0xA81C, 0xA5E4, 0x07, "drug"},
    {14, 22, 0xA2A6, 0xA177, 0x0E, "church"},
    {13, 21, 0xA8EE, 0xA6C8, 0x07, "bank"},
    {16, 24, 0xA2EB, 0xA1CF, 0x07, "inn"},
};

const uint16_t SHIELD_HP[6] = {30, 80, 180, 300, 300, 600};             /* armr A6BF */
const uint16_t EXP_NEXT[16] = {50, 150, 300, 420, 1000, 1500, 3000, 5000,
                               6000, 8000, 10000, 15000, 20000, 40000, 50000, 60000};  /* kenj A28C */
const uint8_t SAGE_MAX_LEVEL[8] = {3, 6, 9, 11, 13, 15, 18, 0xFF};      /* kenj A2AC */
static const uint8_t BIT_OF_ID[8] = {0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};  /* AC9C / A494 */

/* ---------------------------------------------------------- image access */
static uint8_t img8(const Shop *s, unsigned a)
{
    unsigned o = a - 0xA000;
    return (s->img && o < s->imglen) ? s->img[o] : 0;
}
static uint16_t img16(const Shop *s, unsigned a) { return (uint16_t)(img8(s, a) | img8(s, a + 1) << 8); }
static const uint8_t *imgp(const Shop *s, unsigned a)
{
    unsigned o = a - 0xA000;
    return (s->img && o < s->imglen) ? s->img + o : NULL;
}
/* {u8 hi, u16 lo} 24-bit price entries */
static uint32_t img24(const Shop *s, unsigned a)
{
    return (uint32_t)img8(s, a) << 16 | img16(s, a + 1);
}
/* the n-th NUL-terminated string of a table of pointers */
static unsigned name_addr(const Shop *s, int i) { return img16(s, s->names_tbl + 2u * (unsigned)i); }

uint32_t shop_price(const Shop *s, int item)
{
    return (item >= 0 && item < s->nprice) ? s->price[item] : 0;
}

/* ------------------------------------------------------------- the frame */
const uint8_t *shop_framebuffer(const Shop *s) { return s->fb; }

static void shop_hook_tick(Shop *s);            /* the [A002] hook, one tick */

/* one rendered frame is 4*speed = 20 ticks of the 236.7 Hz clock, and town.bin
 * calls idle_poll (and so the hook) far faster than that, so step the hook a
 * tick at a time before presenting. */
#define SHOP_TICKS_PER_FRAME 20

static void shop_frame(Shop *s)
{
    s->frames++;
    if (s->headless_guard && (int)s->frames > s->headless_guard) { s->leave = 1; return; }
    for (int t = 0; t < SHOP_TICKS_PER_FRAME; t++) shop_hook_tick(s);
    if (s->t && s->t->present) { s->t->frame_no++; s->t->present(s->t); }
}

/* the shops measure time in 236.7 Hz ticks; one frame is 4*speed = 20 of them */
static void shop_wait(Shop *s, int ticks)
{
    for (int n = (ticks + 19) / 20; n > 0 && !s->leave; n--) shop_frame(s);
}

static int shop_btn(Shop *s, int *cancel)
{
    if (!s->t) return 1;
    if (s->t->btn2_edge) { s->t->btn2_edge = 0; if (cancel) *cancel = 1; return 1; }
    if (s->t->btn1_edge) { s->t->btn1_edge = 0; return 1; }
    return 0;
}

static void shop_wait_key(Shop *s)                              /* 71DF */
{
    if (s->t) s->t->btn1_edge = s->t->btn2_edge = 0;
    for (int guard = 0; guard < 4000 && !s->leave; guard++) {
        shop_frame(s);
        int cancel = 0;
        if (shop_btn(s, &cancel)) break;
    }
    s->g->sfx_request = 0x1D;
}

/* ------------------------------------------------------- the text box 706C */
static void box_clear(Shop *s) { vid_window(s->fb, 0xFF, BOX_X4, BOX_Y, BOX_W4, BOX_H); }

/* 718E: the box holds four 10-px lines; a fifth scrolls them up */
static void box_scroll(Shop *s)
{
    for (int n = 0; n < 10; n++)
        for (int y = 98; y < 98 + 49; y++)
            memmove(s->fb + y * TEXT_W + 56, s->fb + (y + 1) * TEXT_W + 56, 208);
}

/* 7224: the width of the next word (stops at a space, '/', 0, 0x0C, 0x0D, 0xFF) */
static int word_width(const Shop *s, unsigned p)
{
    int w = 0;
    for (int n = 0; n < 64; n++) {
        uint8_t c = img8(s, p + (unsigned)n);
        if (c == ' ' || c == '/' || c == 0 || c == 0x0C || c == 0x0D || c == 0xFF) break;
        if (c >= 0x20 && c < 0x80) w += FONT_ADVANCE[c - 0x20];
    }
    return w;
}

static void box_newline(Shop *s);

/* 71BC: the red page marker, then a keypress, then a fresh box */
static void box_wait_and_clear(Shop *s)
{
    vid_putchar(s->fb, s->font, 0x7C, 2, 156, 139);
    shop_wait_key(s);
    vid_window(s->fb, 0, 0x27, 0x8B, 2, 10);
    s->lines = 0;
}

/* 7269: how many more lines the rest of this text will take */
static int lines_pending(const Shop *s)
{
    int n = 1, x = s->tx;
    unsigned p = s->tp;
    for (int guard = 0; guard < 1024; guard++) {
        uint8_t c = img8(s, p++);
        if (c == 0 || c == 0x0C) break;
        if (c == 0xFF) break;
        if (c == '/' || c == 0x0D) { n++; x = 0; continue; }
        if (c < 0x20 || c >= 0x80) continue;
        x += FONT_ADVANCE[c - 0x20];
        if (x >= TEXT_WRAP) { n++; x = 0; }
    }
    return n;
}

static void box_newline(Shop *s)                                /* 7169 */
{
    s->tx = 0; s->lines++; s->tline++;
    if (s->lines < 4) return;
    int pending = lines_pending(s);
    if (s->tline >= 4) { box_scroll(s); s->tline = 3; }
    if (pending >= 2) box_wait_and_clear(s);
}

/* 0x706C  Print until an opcode: returns 0 on a 0x00 byte, or the byte after
 * 0xFF (0xFF 0xFF = leave the shop). */
static uint8_t print_text(Shop *s)
{
    if (s->tx + word_width(s, s->tp) >= TEXT_WRAP) box_newline(s);
    int per_frame = 0;
    for (int guard = 0; guard < 8192 && !s->leave; guard++) {
        if (++per_frame >= 3) { per_frame = 0; shop_frame(s); }      /* 6 ticks per glyph */
        uint8_t c = img8(s, s->tp++);
        if (c == 0x2F || c == 0x0D) { box_newline(s); continue; }
        if (c == 0x0C) { s->tx = s->tline = s->lines = 0; box_clear(s); s->last_len = 0; s->last_text[0] = 0; continue; }
        if (c == 0x0F) { box_wait_and_clear(s); continue; }
        if (c == 0x11) { shop_wait_key(s); continue; }
        if (c == 0x13) { s->mute = 1; continue; }
        if (c == 0x15) { s->mute = 0; continue; }
        if (c == 0xFF) return img8(s, s->tp++);
        if (c == 0x00) return 0;
        if (c < 0x20 || c >= 0x80) continue;
        if (s->tx >= TEXT_WRAP) box_newline(s);
        vid_putchar(s->fb, s->font, c, 1, TEXT_ORIGIN_X + s->tx - FONT_XOFF[c - 0x20],
                    TEXT_ORIGIN_Y + s->tline * 10);
        s->tx += FONT_ADVANCE[c - 0x20];
        if (s->last_len < (int)sizeof s->last_text - 1) s->last_text[s->last_len++] = (char)(c == '\\' ? '\'' : c);
        s->last_text[s->last_len] = 0;
        if (!s->mute && c != ' ') s->g->sfx_request = 5;
        if (c == ' ' && s->tx + word_width(s, s->tp) >= TEXT_WRAP) box_newline(s);
    }
    return 0xFF;
}

/* print a C string through the same box (the shops do this for names/numbers) */
static void print_str(Shop *s, const char *str)
{
    for (const char *p = str; *p; p++) {
        uint8_t c = (uint8_t)*p;
        if (c < 0x20 || c >= 0x80) continue;
        if (s->tx + FONT_ADVANCE[c - 0x20] >= TEXT_WRAP) box_newline(s);
        vid_putchar(s->fb, s->font, c, 1, TEXT_ORIGIN_X + s->tx - FONT_XOFF[c - 0x20],
                    TEXT_ORIGIN_Y + s->tline * 10);
        s->tx += FONT_ADVANCE[c - 0x20];
        if (s->last_len < (int)sizeof s->last_text - 1) s->last_text[s->last_len++] = (char)c;
        s->last_text[s->last_len] = 0;
    }
    shop_frame(s);
}

static void print_name(Shop *s, int id)
{
    unsigned a = name_addr(s, id);
    const uint8_t *p = imgp(s, a);
    if (!p) return;
    char buf[64];
    int n = 0;
    while (n < 63 && p[n]) { buf[n] = (char)(p[n] == '\\' ? '\'' : p[n]); n++; }
    buf[n] = 0;
    print_str(s, buf);
}

static void print_number(Shop *s, uint32_t v)
{
    char buf[16];
    format_number(v, buf);
    print_str(s, buf);
}

static void say(Shop *s, unsigned addr) { s->tp = addr; }

/* --------------------------------------------------------------- menus */
static void menu_cursor(Shop *s, int row, int on)               /* 7469 */
{
    gt_cursor(s->fb, s->mx4 + 1, s->my + row * 10, on);
}

static void menu_draw_items(Shop *s, unsigned addr, int count)  /* 751A */
{
    const uint8_t *p = imgp(s, addr);
    if (!p) return;
    for (int i = 0; i < count; i++) {
        vid_label_asciiz(s->fb, s->font, (const char *)p, s->mx4 + 3, s->my + 1 + i * 10, 0);
        while (*p) p++;
        p++;
    }
}

/* 7539 + gtmcga 3805/37CC: one line per row — the item name in the narrow
 * font and, with menu_show_prices, its 24-bit price right-aligned. */
static void menu_draw_icons(Shop *s, int first, int rows)
{
    for (int r = 0; r < rows; r++) {
        int id = s->ids[first + r];
        int y = s->my + r * 10;
        vid_window(s->fb, 0, s->mx4 + 3, y, s->mw4, 9);
        const uint8_t *p = imgp(s, name_addr(s, id));
        if (p) {
            char buf[48];
            int n = 0;
            while (n < 47 && p[n]) { buf[n] = (char)(p[n] == '\\' ? '\'' : p[n]); n++; }
            buf[n] = 0;
            vid_label_asciiz(s->fb, s->font, buf, s->mx4 + 3, y, 0);
        }
        if (s->mprices) {
            char num[16];
            int n = format_number(shop_price(s, id), num);
            int xend = (s->mx4 + 3) * 4 + s->mw4 * 4;
            vid_label_narrow(s->fb, s->font, num, n, 0, y, xend - 5 * n, 1, -1);
        }
    }
}

/* 0x7344  BL = cursor row; returns 1 when cancelled (Alt). */
static int menu_select(Shop *s, int *cursor)
{
    if (s->t) s->t->btn1_edge = s->t->btn2_edge = 0;
    int row = *cursor;
    if (row >= s->mvis) row = s->mvis ? s->mvis - 1 : 0;
    menu_cursor(s, row, 1);
    int held = 0;
    s->in_menu = 1; s->menu_n = s->mvis;
    for (int guard = 0; guard < 8000 && !s->leave; guard++) {
        s->menu_row = row + s->mscroll;
        shop_frame(s);
        if (s->t && s->t->btn2_edge) { s->t->btn2_edge = 0; menu_cursor(s, row, 0); s->in_menu = 0; return 1; }
        if (s->t && s->t->btn1_edge) {
            s->t->btn1_edge = 0; s->g->sfx_request = 0x1F;
            menu_cursor(s, row, 0);
            *cursor = row;
            s->in_menu = 0;
            return 0;
        }
        uint8_t d = s->t ? (uint8_t)(s->t->dirs & 3) : 0;
        if (!d) { held = 0; continue; }
        if (held) continue;
        held = 1;
        if (d == 1) {                                           /* up */
            if (row) { menu_cursor(s, row, 0); row--; menu_cursor(s, row, 1); }
            else if (s->mscroll) { s->mscroll--; menu_draw_icons(s, s->mscroll, s->mvis); menu_cursor(s, row, 1); }
        } else if (d == 2) {                                    /* down */
            if (row < s->mvis - 1) { menu_cursor(s, row, 0); row++; menu_cursor(s, row, 1); }
            else if (row + s->mscroll + 1 < s->mtot) { s->mscroll++; menu_draw_icons(s, s->mscroll, s->mvis); menu_cursor(s, row, 1); }
        }
    }
    *cursor = row;
    s->in_menu = 0;
    return 1;
}

/* 0x74D3  the two-line Yes/No list; 1 = Yes */
static int yes_no(Shop *s)
{
    int sv = s->mvis, st = s->mtot, ss = s->mscroll;
    s->mvis = s->mtot = 2; s->mscroll = 0;
    vid_label_asciiz(s->fb, s->font, "Yes", s->mx4 + 3, s->my + 1, 0);
    vid_label_asciiz(s->fb, s->font, "No",  s->mx4 + 3, s->my + 11, 0);
    int row = 0;
    int cancel = menu_select(s, &row);
    if (cancel) row = 1;
    s->mvis = sv; s->mtot = st; s->mscroll = ss;
    return row == 0;
}

static void menu_box(Shop *s, int x4, int y, int w4, int h, int px4, int py)
{
    vid_window(s->fb, 0xFF, x4, y, w4, h);
    s->mx4 = px4; s->my = py;
}

/* 0x7570 / 0x7589 */
static int gold_can_pay(const Shop *s, uint32_t amount) { return s->g->gold >= amount; }
static void gold_pay(Shop *s, uint32_t amount) { s->g->gold -= amount; }
static void shop_gold_add(Shop *s, uint32_t amount) { s->g->gold += amount; if (s->g->gold > 0xFFFFFF) s->g->gold = 0xFFFFFF; }

/* ------------------------------------------------------------ the portrait */
static void draw_portrait_map(Shop *s, unsigned map, int rows, int cols, int x8, int y)
{
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
            gt_draw_cell(s->fb, s->cell, s->present, img8(s, map + (unsigned)(r * cols + c)),
                         x8 + c, y + r * 8);
}

/* armrpro A9CF: two blocks of {u8 rows, u16 map} per frame (table AA10) */
static void armr_draw_frame(Shop *s, int frame)
{
    unsigned t = 0xAA10 + 6u * (unsigned)frame;
    int y = 0x17;
    for (int b = 0; b < 2; b++) {
        int rows = img8(s, t + 3u * (unsigned)b);
        unsigned map = img16(s, t + 3u * (unsigned)b + 1);
        if (!rows) break;
        draw_portrait_map(s, map, rows, 12, 0x07, y);
        y += rows * 8;
    }
}

static void draw_portrait(Shop *s)
{
    if (s->dest == SHOP_ARMOUR) { armr_draw_frame(s, 0); return; }
    if (!SHOPS[s->dest].portrait) return;
    int rows = 8, cols = 12;
    if (s->dest == SHOP_OMOYA) { rows = 16; cols = 17; }
    draw_portrait_map(s, SHOPS[s->dest].portrait, rows, cols, SHOPS[s->dest].px4,
                      s->dest == SHOP_OMOYA ? 0x0C : 0x17);
}

/* like draw_portrait_map, but a cell of 0xFF leaves the screen alone
 * (churpro's two little maps are the only ones that use the hole) */
static void draw_cells_holes(Shop *s, unsigned map, int rows, int cols, int x8, int y)
{
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++) {
            uint8_t v = img8(s, map + (unsigned)(r * cols + c));
            if (v == 0xFF) continue;                            /* A21B / A263 */
            gt_draw_cell(s->fb, s->cell, s->present, v, x8 + c, y + r * 8);
        }
}

/* ===================================================== the [A002] hooks ===
 * town.bin idle_poll 7042 does `call [cs:0xA002]` on every pass while [7C42]
 * says a shop overlay is loaded.  Every overlay but omoypro has one; omoypro's
 * A002 word is A004, the `ret` byte in front of its entry point.
 *
 * The addresses in the comments are the overlay's own; the tables are read out
 * of the loaded image at those addresses rather than retyped, exactly like the
 * rest of shop.c.  See shop.h for the tick model. */

/* --- kingpro A302 ------------------------------------------------------- */
/* A142 (its lower half): the king's face, 7 rows x 6 cells at (x8 0x11, y 0x17),
 * the map taken from the seven-entry pointer table at A1CE */
static void king_draw_face(Shop *s, int frame)
{
    draw_portrait_map(s, img16(s, 0xA1CE + 2u * (unsigned)frame), 7, 6, 0x11, 0x17);
}
/* A3A6: the 2x5-cell mouth at (x8 0x11, y 0x3F); table A3D4, ten bytes a frame */
static void king_draw_mouth(Shop *s, int open)
{
    draw_portrait_map(s, 0xA3D4 + 10u * (unsigned)(open & 1), 2, 5, 0x11, 0x3F);
}
static void king_hook(Shop *s)
{
    ShopHook *h = &s->hk;
    if (h->ff50 < 4) return;                                    /* A302 */
    h->ff50 = 0;                                                /* A30A */
    /* A315 the blink: a 26-entry frame sequence at A360, then a random pause */
    if (h->king_blink_on) {                                     /* A315 [A7A0] */
        if (++h->king_blink_i >= 0x1A) {                        /* A31D..A326 */
            /* A328: `call [11A] / or al,al / jz` — one chance in 256 per step
             * to run the sequence again; 0xFF++ wraps back to entry 0 */
            if ((krn_random(s->g) & 0xFF) == 0) h->king_blink_i = 0xFF;
        } else {
            unsigned seq = img8(s, 0xA360 + h->king_blink_i);   /* A338 xlatb */
            draw_portrait_map(s, 0xA37A + 4u * seq, 1, 4, 0x11, 0x2F);  /* the eyes */
        }
    }
    /* A386 the mouth: toggles every 6 steps while the lip-sync is on */
    if (!h->king_mouth_on) return;                              /* A386 [A79D] */
    if (++h->king_mouth_t < 6) return;                          /* A38E..A397 */
    h->king_mouth_t = 0;
    h->king_mouth_p++;                                          /* A39F */
    king_draw_mouth(s, h->king_mouth_p & 1);
}

/* --- armrpro A90F ------------------------------------------------------- */
static void armr_hook(Shop *s)
{
    ShopHook *h = &s->hk;
    if (!h->armr_on) return;                                    /* A90F [BC23] */
    if (h->ff50 < 2) return;                                    /* A917 */
    h->ff50 = 0;
    if (++h->armr_t0 < 0x1E) return;                            /* A925..A931 */
    h->armr_t0 = 0;
    h->armr_phase++;                                            /* A936 */
    if (h->armr_state) {                                        /* A93A [BC26] */
        if (h->armr_state == 0x7F) { h->armr_state = 0xFF; armr_draw_frame(s, 2); return; }
        if (h->armr_state == 0x80) { h->armr_state = 0;    armr_draw_frame(s, 0); return; }
        /* A961: two cells DOWN one column — the smith's mouth */
        for (int i = 0; i < 2; i++)
            gt_draw_cell(s->fb, s->cell, s->present,
                         img8(s, 0xAB68 + (unsigned)((h->armr_phase & 3) * 2 + i)),
                         0x0B, 0x37 + i * 8);
    } else {
        /* A985: two cells ACROSS — the eyes.  AAD0 holds {50,51} three times
         * and {53,54} once, so he blinks a quarter of every cycle. */
        for (int i = 0; i < 2; i++)
            gt_draw_cell(s->fb, s->cell, s->present,
                         img8(s, 0xAAD0 + (unsigned)((h->armr_phase & 3) * 2 + i)),
                         0x10 + i, 0x4F);
    }
    if (krn_random(s->g) & 1) return;                           /* A9A6..A9AD */
    if (++h->armr_t1 < 0x1E) return;                            /* A9B0..A9BC */
    h->armr_t1 = 0;
    h->armr_state = (uint8_t)(~h->armr_state ^ 0x80);           /* A9C1: 0 -> 7F -> FF -> 80 -> 0 */
    armr_draw_frame(s, 1);
}

/* --- bankpro A728 ------------------------------------------------------- */
static void bank_hook(Shop *s)
{
    ShopHook *h = &s->hk;
    if (!h->bank_on) return;                                    /* A728 [AD21] */
    if (h->ff50 < 0x1E) return;                                 /* A730 */
    h->ff50 = 0;
    h->bank_phase++;                                            /* A73E */
    /* A742: the pair of 5x8 cell maps [AD1F] points at, 0x28 bytes apart */
    draw_portrait_map(s, h->bank_map + (unsigned)((h->bank_phase & 1) * 0x28),
                      5, 8, 0x09, 0x1F);
}

/* A813: the teller's own cell-map list, one 5x8 map every 0x28 ticks — he
 * stands up (A82F) when you come in and sits back down (A839) when you go.
 * A751 (the map loop the hook itself uses) does the drawing. */
static void bank_play_anim(Shop *s, unsigned list)
{
    for (int i = 0; i < 16 && !s->leave; i++) {
        unsigned map = img16(s, list + 2u * (unsigned)i);
        if (map == 0xFFFF) return;
        draw_portrait_map(s, map, 5, 8, 0x09, 0x1F);
        shop_wait(s, 0x28);
    }
}

/* --- churpro A1D7 ------------------------------------------------------- */
static void chur_hook(Shop *s)
{
    ShopHook *h = &s->hk;
    if (h->ff50 < 0x20) return;                                 /* A1D7 */
    h->ff50 = 0;
    if (++h->chur_phase == 3) h->chur_phase = 0;                /* A1E5..A1F0 */
    draw_cells_holes(s, 0xA234 + 6u * h->chur_phase, 2, 3, 0x10, 0x37);   /* A1FA */
    draw_cells_holes(s, 0xA27C + 4u * h->chur_phase, 2, 2, 0x15, 0x37);   /* A246 */
}

/* --- drugpro A644 ------------------------------------------------------- */
static void drug_hook(Shop *s)
{
    ShopHook *h = &s->hk;
    if (h->ff50 < 2) return;                                    /* A644 */
    h->ff50 = 0;
    if (++h->drug_t < 0x14) return;                             /* A652..A65E */
    h->drug_t = 0;
    h->drug_phase = (uint8_t)(h->drug_phase + 1 < 3 ? h->drug_phase + 1 : 0);   /* A663 */
    draw_portrait_map(s, 0xA69C + 0x24u * h->drug_phase, 6, 6, 0x0D, 0x17);
}
/* A708: a 0xFFFF-terminated list of 7x4 cell maps, one every 0x28 ticks —
 * the keeper walking in (A745) / out (A74F) and his bow (A759) / rise (A761).
 * Not the hook, but it is the hook's timebase and the ops that play it are the
 * ones the shop's own scripts fire. */
static void drug_play_anim(Shop *s, unsigned list)
{
    for (int i = 0; i < 16 && !s->leave; i++) {
        unsigned map = img16(s, list + 2u * (unsigned)i);
        if (map == 0xFFFF) return;
        draw_portrait_map(s, map, 7, 4, 0x09, 0x1F);
        shop_wait(s, 0x28);
    }
}

/* --- innapro A22F ------------------------------------------------------- */
static void inn_hook(Shop *s)
{
    ShopHook *h = &s->hk;
    if (!h->inn_on) return;                                     /* A22F [A505] */
    if (h->ff50 < 0x28) return;                                 /* A237 */
    h->ff50 = 0;
    /* A245: the only use of KRN_RANDOM in the town — the innkeeper's blink
     * frame is picked fresh every 0x28 ticks, open or shut, 50/50 */
    draw_portrait_map(s, 0xA279 + 4u * (unsigned)(krn_random(s->g) & 1), 2, 2, 0x08, 0x27);
}
/* A17F: the four 4x5 "going to sleep" frames at A281, played by op 2 */
static void inn_draw_night(Shop *s, int frame)
{
    draw_portrait_map(s, 0xA281 + 0x14u * (unsigned)frame, 4, 5, 0x08, 0x27);
}

/* --- kenjpro AB47 ------------------------------------------------------- */
/* AA16: the ritual aura, 4 rows x 8 cells at [BB12]+0x210 = (x8 9, y 0x27) */
static void kenj_draw_ritual(Shop *s, int frame)
{
    draw_portrait_map(s, 0xAA47 + 0x20u * (unsigned)frame, 4, 8, 0x09, 0x27);
}
static void kenj_hook(Shop *s)
{
    ShopHook *h = &s->hk;
    if (h->ff50 < 2) return;                                    /* AB47 */
    h->ff50 = 0;
    if (h->kj_ritual_on) {                                      /* AB55 [BB18] */
        if (h->kj_fade) {                                       /* AB5C [BB1A] */
            h->kj_ctr = (uint8_t)((h->kj_ctr + 1) & 15);        /* AB63 */
            if (h->kj_ctr == 1)                                 /* AB6C: on the wrap, stop */
                h->kj_ritual_on = h->kj_fade = h->kj_ctr = h->kj_frame = 0;
        } else {
            if (++h->kj_t_ritual < 0x14) return;                /* AB89..AB94 */
            h->kj_t_ritual = 0;
            h->kj_frame++;                                      /* AB9A */
            /* ABA2: the eight-step aura sequence at ABFF, indexed frame-1 */
            kenj_draw_ritual(s, img8(s, 0xABFF + (unsigned)((h->kj_frame - 1) & 7)));
            h->kj_ctr = (uint8_t)((h->kj_ctr + 1) & 15);        /* ABB0 */
        }
    }
    if (h->kj_blink_off) return;                                /* ABB9 [BB19] */
    if (++h->kj_t_blink < 0x14) return;                         /* ABC1..ABCD */
    h->kj_t_blink = 0;
    uint8_t bl = (uint8_t)(h->kj_blink_state & 1);              /* ABD2 */
    h->kj_blink_state = (uint8_t)~h->kj_blink_state;
    /* ABDF: one cell at [BB12]+0x718 = (x8 0x0E, y 0x2F); ABFB open, ABFD shut */
    gt_draw_cell(s->fb, s->cell, s->present,
                 img8(s, (h->kj_eyes_closed ? 0xABFD : 0xABFB) + bl), 0x0E, 0x2F);
}

/* one 236.7 Hz tick of whichever hook is loaded */
static void shop_hook_tick(Shop *s)
{
    s->hk.ff50++;
    switch (s->dest) {
    case SHOP_KING:   king_hook(s); break;
    case SHOP_ARMOUR: armr_hook(s); break;
    case SHOP_BANK:   bank_hook(s); break;
    case SHOP_CHURCH: chur_hook(s); break;
    case SHOP_DRUG:   drug_hook(s); break;
    case SHOP_INN:    inn_hook(s);  break;
    case SHOP_SAGE:   kenj_hook(s); break;
    default: break;                     /* omoypro: [A002] = A004, a bare `ret` */
    }
}

/* ---------------------------------------------------------- the prologue */
static void shop_prologue(Shop *s)
{
    /* [2002] vid_clear_playfield clears the *playfield* only — rows 14..157,
     * x 48..271 — and everything around it is mole.bin's boot-time frame. */
    for (int y = 14; y < 158; y++) memset(s->fb + y * TEXT_W + 48, 0, 224);
    render_screen_frame(s->fb, s->g ? s->g->screen : NULL, 0, TEXT_H);
    unsigned label = SHOPS[s->dest].label;
    if (s->dest == SHOP_SAGE) label = img16(s, 0xACBD + 2u * (unsigned)(s->town_id - 1));
    if (label) vid_label_text(s->fb, s->font, imgp(s, label));
    draw_portrait(s);
    s->tx = s->tline = s->lines = 0;
    box_clear(s);
}

/* =========================================================== ARMOUR SHOP == */
static void armr_build(Shop *s, int shields, uint8_t *list, int *n)
{
    uint8_t mask = s->g->page[(shields ? P_SHIELD_STOCK : P_SWORD_STOCK) + s->town_id - 1];
    *n = 0;
    for (int i = 0; i < 6; i++) if (mask & (0x80 >> i)) list[(*n)++] = (uint8_t)i;
}

static uint8_t armr_swords[6], armr_shields[6];
static int armr_nsw, armr_nsh;

static void armr_clear_menu(Shop *s) { vid_window(s->fb, 0, 0x27, 0x17, 0x1C, 0x41); }

/* A259 / A498: the two purchase flows differ only in the tables they use */
static void armr_buy(Shop *s, int shields)
{
    s->bought = 1;
    const uint8_t *list = shields ? armr_shields : armr_swords;
    int n = shields ? armr_nsh : armr_nsw;
    memcpy(s->ids, list, 6);
    s->mtot = n; s->mvis = n < 3 ? n : 3; s->mscroll = 0;
    if (!n) { say(s, 0xADEF); return; }
    menu_box(s, 0x15, 0x6E, 0x25, 0x24, 0x15, 0x71);
    s->mprices = 1; s->mw4 = 0x21; s->mpricex = 0x17;
    int base = shields ? 6 : 0;
    for (int i = 0; i < n; i++) s->ids[i] = (uint8_t)(list[i] + base);
    menu_draw_icons(s, 0, s->mvis);
    int row = s->cursor_list;
    if (menu_select(s, &row)) { armr_clear_menu(s); s->mprices = 0; say(s, 0xADEF); return; }
    s->cursor_list = row;
    int id = s->ids[row + s->mscroll] - base;                   /* 0-based sword/shield */
    armr_clear_menu(s); s->mprices = 0;
    /* Tumba withholds the Knight's sword until the Crest of Glory is traded */
    if (!shields && id == 3 && !(s->g->page[P_CREST_FLAGS] & 2) && s->town_id == 5) { say(s, 0xB24C); return; }
    say(s, 0xB0DC); print_text(s);                              /* "Oh, the " */
    s->names_tbl = 0xAD05;
    print_name(s, id + base);
    print_text(s);                                              /* "?/" */
    uint32_t price = shop_price(s, id + base);
    if (!gold_can_pay(s, price)) { say(s, shields ? 0xAF54 : 0xAF54); return; }
    say(s, 0xB106); print_text(s); print_number(s, price); print_text(s);   /* "That will be N golds./" */
    uint32_t trade = 0;
    int old = shields ? s->g->shield : s->g->sword;
    if (old) {
        say(s, 0xB046); print_text(s);                          /* "I'll give you " */
        trade = shop_price(s, old - 1 + base) >> 1;
        print_number(s, trade);
        print_text(s);                                          /* " golds ... trade-in." */
    }
    say(s, 0xB0ED); print_text(s);                              /* "Will that be all right?" */
    menu_box(s, 0x2F, 0x2B, 0x0C, 0x19, 0x30, 0x2E);
    int yes = yes_no(s);
    vid_window(s->fb, 0, 0x2F, 0x2B, 0x0C, 0x19);
    if (!yes) { say(s, 0xADEF); return; }
    say(s, 0xAE1C);
    gold_pay(s, price);
    shop_gold_add(s, trade);
    if (old) s->g->page[(shields ? P_SHIELD_STOCK : P_SWORD_STOCK) + s->town_id - 1] |= BIT_OF_ID[old - 1];
    if (shields) {
        s->g->shield = (uint8_t)(id + 1);
        s->g->shield_hp = SHIELD_HP[id];
        s->g->page[P_SHIELD_MAX] = (uint8_t)s->g->shield_hp;
        s->g->page[P_SHIELD_MAX + 1] = (uint8_t)(s->g->shield_hp >> 8);
        armr_build(s, 1, armr_shields, &armr_nsh);
    } else {
        s->g->sword = (uint8_t)(id + 1);
        if (s->g->sword == 6) s->g->page[P_SWORD_STOCK + s->town_id - 1] &= (uint8_t)~0x04;   /* unique */
        armr_build(s, 0, armr_swords, &armr_nsw);
    }
}

static void armr_repair(Shop *s)                                /* A198 */
{
    armr_clear_menu(s);
    if (!s->g->shield) { say(s, 0xAE4A); return; }
    unsigned smax = (unsigned)(s->g->page[P_SHIELD_MAX] | s->g->page[P_SHIELD_MAX + 1] << 8);
    if (smax < s->g->shield_hp) smax = s->g->shield_hp;
    unsigned missing = smax - s->g->shield_hp;
    if (!missing) { say(s, 0xAEB1); return; }
    s->bought = 1;
    uint32_t cost = (missing + 1) >> 1;
    say(s, 0xAEF8); print_text(s); print_number(s, cost); print_text(s);
    menu_box(s, 0x2F, 0x2B, 0x0C, 0x19, 0x30, 0x2E);
    int yes = yes_no(s);
    armr_clear_menu(s);
    say(s, 0xADEF);
    if (!yes) return;
    if (!gold_can_pay(s, cost)) { say(s, 0xAF53); return; }
    gold_pay(s, cost);
    s->g->shield_hp = (uint16_t)smax;
    say(s, 0xAFAF);
}

static void armr_explain(Shop *s)                               /* A759 */
{
    s->names_tbl = 0xAD05;
    int n = 0;
    for (int i = 0; i < armr_nsw; i++) s->ids[n++] = armr_swords[i];
    for (int i = 0; i < armr_nsh && n < 16; i++) s->ids[n++] = (uint8_t)(armr_shields[i] + 6);
    s->mtot = n; s->mvis = n < 6 ? n : 6; s->mscroll = 0; s->mprices = 0; s->mw4 = 0x17;
    if (!n) { say(s, 0xADEF); return; }
    menu_box(s, 0x27, 0x17, 0x1B, 0x41, 0x27, 0x1A);
    menu_draw_icons(s, 0, s->mvis);
    int row = s->cursor_list;
    if (menu_select(s, &row)) { armr_clear_menu(s); say(s, 0xADEF); return; }
    int id = s->ids[row + s->mscroll];
    armr_clear_menu(s);
    say(s, 0xB0DD); print_text(s); print_name(s, id); print_text(s);
    say(s, img16(s, 0xB3DE + 2u * (unsigned)id));               /* the description */
}

static void armr_action(Shop *s, uint8_t op)
{
    switch (op) {
    case 0: {                                                   /* A12D main menu */
        armr_clear_menu(s);
        menu_box(s, 0x29, 0x1D, 0x18, 0x37, 0x29, 0x20);
        s->mvis = s->mtot = 5; s->mscroll = 0;
        menu_draw_items(s, 0xACC8, 5);
        int row = s->cursor_main;
        if (menu_select(s, &row)) row = 0;
        s->cursor_main = row;
        switch (row) {
        case 0: armr_clear_menu(s); say(s, s->bought ? 0xB1DE : 0xB1FF); return;
        case 1: armr_repair(s); return;
        case 2: armr_clear_menu(s); say(s, 0xB026); return;
        case 3: armr_clear_menu(s); say(s, 0xB081); return;
        default: armr_clear_menu(s); say(s, 0xB11F); return;
        } }
    case 1: armr_buy(s, 0); return;
    case 2: armr_buy(s, 1); return;
    case 3: {                                                   /* A6CB: back to the anvil */
        s->hk.armr_on = 0;                                      /* A6CB */
        if (s->hk.armr_state) { armr_draw_frame(s, 1); shop_wait(s, 50); }   /* A6D7 */
        for (int i = 0; i < 12 && !s->leave; i++) {             /* A6DF: sequence A6FD */
            uint8_t f = img8(s, 0xA6FD + (unsigned)i);
            if (f == 0xFF) break;
            if (f & 0x80) s->g->sfx_request = 0x20;             /* A6ED: the hammer */
            armr_draw_frame(s, f & 7);
            shop_wait(s, 50);                                   /* A870 */
        }
        return; }
    case 4: shop_wait(s, 150); return;                          /* A706 */
    case 5:                                                     /* A716: the repair (done in C
                                                                 * by armr_repair; this is the
                                                                 * wait and the animation reset) */
        shop_wait(s, 400);
        s->hk.armr_t0 = s->hk.armr_phase = s->hk.armr_state = s->hk.armr_t1 = 0;   /* A734 */
        armr_draw_frame(s, 0);
        s->hk.armr_on = 0xFF;                                   /* A74D */
        return;
    case 6: armr_explain(s); return;
    case 7: shop_wait(s, 50); return;
    case 8:                                                     /* A880 the Crest of Glory trade */
        menu_box(s, 0x2F, 0x2B, 0x0C, 0x19, 0x30, 0x2E);
        if (!yes_no(s)) { armr_clear_menu(s); say(s, 0xB336); return; }
        armr_clear_menu(s);
        say(s, 0xB375);
        s->g->sword = 4; s->g->page[P_GLORY_CREST] = 0;
        s->g->page[P_SWORD_STOCK + 4] &= (uint8_t)~0x10;
        s->g->page[P_CREST_FLAGS] |= 2;
        return;
    case 9: armr_draw_frame(s, 3); return;                      /* A8FD: the smith looks up */
    default: return;
    }
}

/* ============================================================= DRUG SHOP == */
static uint8_t drug_stock[8];
static int drug_n;

static void drug_build(Shop *s)
{
    uint8_t mask = s->g->page[P_DRUG_STOCK + s->town_id - 1];
    drug_n = 0;
    for (int i = 0; i < 8; i++) if (mask & (0x80 >> i)) drug_stock[drug_n++] = (uint8_t)i;
}
static void drug_clear_menu(Shop *s) { vid_window(s->fb, 0, 0x27, 0x17, 0x1D, 0x41); }

static void drug_buy(Shop *s)                                   /* A1AA */
{
    s->names_tbl = 0xB08A;
    s->mtot = drug_n; s->mvis = drug_n < 3 ? drug_n : 3;
    if (!drug_n) { say(s, 0xA965); return; }
    menu_box(s, 0x15, 0x6E, 0x25, 0x24, 0x15, 0x71);
    s->mprices = 1; s->mw4 = 0x21; s->mpricex = 0x17;
    menu_draw_icons(s, s->mscroll, s->mvis);
    int row = s->cursor_list;
    if (menu_select(s, &row)) { drug_clear_menu(s); s->mprices = 0; say(s, 0xA965); return; }
    s->cursor_list = row;
    int id = s->ids[row + s->mscroll];
    drug_clear_menu(s); s->mprices = 0;
    say(s, 0xA8C4); print_text(s); print_name(s, id); print_text(s);
    uint32_t price = shop_price(s, id);
    if (!gold_can_pay(s, price)) { say(s, 0xA928); print_text(s); }
    else {
        int slot = -1;
        for (int i = 0; i < 5; i++) if (!s->g->page[P_POTIONS + i]) { slot = i; break; }
        if (slot < 0) { say(s, 0xA940); return; }
        gold_pay(s, price);
        s->g->page[P_POTIONS + slot] = (uint8_t)(id + 1);
        say(s, 0xA8F2); print_text(s); print_number(s, price); print_text(s);
    }
    say(s, 0xA909); print_text(s);
    menu_box(s, 0x2F, 0x2B, 0x0C, 0x19, 0x30, 0x2E);
    int yes = yes_no(s);
    drug_clear_menu(s);
    say(s, yes ? 0xA8A8 : 0xA965);
}

static void drug_sell(Shop *s)                                  /* A300 */
{
    s->names_tbl = 0xB08A;
    int n = 0;
    for (int i = 0; i < 5; i++) if (s->g->page[P_POTIONS + i]) s->ids[n++] = (uint8_t)(s->g->page[P_POTIONS + i] - 1);
    s->mtot = n; s->mvis = n < 2 ? n : 2; s->mscroll = 0; s->mprices = 0; s->mw4 = 0x19;
    if (!n) { say(s, 0xAA79); return; }
    menu_box(s, 0x17, 0x78, 0x21, 0x1A, 0x19, 0x7B);
    menu_draw_icons(s, 0, s->mvis);
    int row = 0;
    if (menu_select(s, &row)) { vid_window(s->fb, 0, 0x17, 0x78, 0x21, 0x1A); say(s, 0xA965); return; }
    int id = s->ids[row + s->mscroll];
    vid_window(s->fb, 0, 0x17, 0x78, 0x21, 0x1A);
    say(s, 0xA8D7); print_text(s); print_name(s, id); print_text(s);
    uint32_t price = shop_price(s, id) >> 1;
    say(s, 0xA9C4); print_text(s); print_number(s, price); print_text(s);
    menu_box(s, 0x34, 0x21, 0x0C, 0x19, 0x35, 0x24);
    if (!yes_no(s)) { vid_window(s->fb, 0, 0x34, 0x21, 0x0C, 0x19); say(s, 0xA9FE); return; }
    vid_window(s->fb, 0, 0x34, 0x21, 0x0C, 0x19);
    shop_gold_add(s, price);
    say(s, 0xA9AD); print_text(s);
    for (int i = 0; i < 5; i++) if (s->g->page[P_POTIONS + i] == id + 1) { s->g->page[P_POTIONS + i] = 0; break; }
    s->g->page[P_DRUG_STOCK + s->town_id - 1] |= BIT_OF_ID[id];
    drug_build(s);
    say(s, 0xA966);
}

static void drug_describe(Shop *s)                              /* A4BA */
{
    s->names_tbl = 0xB08A;
    memcpy(s->ids, drug_stock, 8);
    s->mtot = drug_n; s->mvis = drug_n < 2 ? drug_n : 2; s->mscroll = 0; s->mprices = 0; s->mw4 = 0x19;
    if (!drug_n) { say(s, 0xA965); return; }
    menu_box(s, 0x17, 0x78, 0x21, 0x1A, 0x19, 0x7B);
    menu_draw_icons(s, 0, s->mvis);
    int row = s->cursor_list;
    if (menu_select(s, &row)) { vid_window(s->fb, 0, 0x17, 0x78, 0x21, 0x1A); say(s, 0xA965); return; }
    int id = s->ids[row + s->mscroll];
    vid_window(s->fb, 0, 0x17, 0x78, 0x21, 0x1A);
    say(s, 0xAACA); print_text(s); print_name(s, id); print_text(s);
    say(s, img16(s, 0xAB3A + 2u * (unsigned)id));
}

static void drug_action(Shop *s, uint8_t op)
{
    switch (op) {
    case 2: {                                                   /* A10C main menu */
        drug_clear_menu(s);
        menu_box(s, 0x27, 0x22, 0x1C, 0x2D, 0x27, 0x25);
        s->mvis = s->mtot = 4; s->mscroll = 0;
        menu_draw_items(s, 0xA839, 4);
        int row = s->cursor_main;
        if (menu_select(s, &row)) row = 0;
        s->cursor_main = row;
        drug_clear_menu(s);
        switch (row) {
        case 0: say(s, 0xAB0E); return;
        case 1: say(s, 0xA88C); return;
        case 2: { int n = 0; for (int i = 0; i < 5; i++) if (s->g->page[P_POTIONS + i]) n++;
                  say(s, n ? 0xA98D : 0xAA79); return; }
        default: say(s, 0xAAA6); return;
        } }
    case 3: memcpy(s->ids, drug_stock, 8); s->mtot = drug_n; s->mscroll = 0; s->cursor_list = 0;
            drug_buy(s); return;
    case 4: drug_buy(s); return;
    case 5: drug_sell(s); return;
    case 6: drug_describe(s); return;
    case 0: shop_wait(s, 0x50); drug_play_anim(s, 0xA745); return;  /* A0D5 walks in */
    case 1: shop_wait(s, 0x50); drug_play_anim(s, 0xA74F); return;  /* A0EB walks out */
    case 7: drug_play_anim(s, 0xA759); return;                      /* A100 bows */
    case 8: drug_play_anim(s, 0xA761); return;                      /* A106 straightens up */
    default: return;
    }
}

/* ================================================================ CHURCH == */
static void chur_action(Shop *s, uint8_t op)
{
    switch (op) {
    case 1: say(s, 0xA36A); return;                             /* A082 */
    case 2: shop_wait(s, 250); return;
    case 3:                                                     /* A099: +8 HP every 20 ticks */
        while (s->g->hp + 8 < s->g->max_hp && !s->leave) { s->g->hp += 8; shop_wait(s, 20); }
        s->g->hp = s->g->max_hp;
        return;
    case 4: memcpy(s->g->magic_count, s->g->magic_max, 7); return;   /* A0CB */
    default: return;
    }
}

/* =================================================================== INN == */
static void inn_action(Shop *s, uint8_t op)
{
    static const int INN_PRICE_A = 0xA2D1;
    switch (op) {
    case 0: print_number(s, img16(s, INN_PRICE_A + 2u * (unsigned)(s->town_id - 1))); return;
    case 1: {                                                   /* A0BE */
        menu_box(s, 0x2F, 0x2B, 0x0C, 0x19, 0x30, 0x2E);
        int yes = yes_no(s);
        vid_window(s->fb, 0, 0x2F, 0x2B, 0x0C, 0x19);
        if (!yes) { say(s, 0xA3BD); return; }
        uint32_t price = img16(s, INN_PRICE_A + 2u * (unsigned)(s->town_id - 1));
        if (!gold_can_pay(s, price)) { say(s, 0xA41A); return; }
        gold_pay(s, price);
        say(s, 0xA483);
        return; }
    case 2:                                                     /* A114: the keeper turns in */
        s->hk.inn_on = 0;
        for (int i = 0; i < 4 && !s->leave; i++) { inn_draw_night(s, i); shop_wait(s, 50); }
        return;
    case 3:                                                     /* A12A: the night */
        shop_wait(s, 150);
        s->g->hp = s->g->max_hp;
        memcpy(s->g->magic_count, s->g->magic_max, 7);
        return;
    case 4: shop_wait(s, 150); return;
    default: return;
    }
}

/* ================================================================== BANK == */
static void bank_action(Shop *s, uint8_t op)
{
    if (op != 1) {                                              /* 0/2/3 are animations */
        if (op == 0) { shop_wait(s, 60); bank_play_anim(s, 0xA82F); }    /* A0C0 he stands up */
        if (op == 2) {                                          /* A5F3 he sits back down */
            s->hk.bank_on = 0;
            bank_play_anim(s, 0xA839);
            s->hk.bank_on = 0xFF; s->hk.bank_map = 0xA773;
            shop_wait(s, 100);
        }
        if (op == 3) s->bought = 1;                             /* A619 [AD24] */
        return;
    }
    /* A0D2 main menu */
    vid_window(s->fb, 0, 0x17, 0x27, 0x41, 0x1C);
    menu_box(s, 0x27, 0x1D, 0x1C, 0x37, 0x27, 0x20);
    s->mvis = s->mtot = 5; s->mscroll = 0;
    menu_draw_items(s, 0xA90C, 5);
    int row = s->cursor_main;
    if (menu_select(s, &row)) row = 0;
    s->cursor_main = row;
    vid_window(s->fb, 0, 0x17, 0x27, 0x41, 0x1C);
    /* A14B / A23B / A3D0: every branch off the menu stops the idle and puts the
     * teller back in his A8BB pose before it talks */
    s->hk.bank_on = 0;
    if (row >= 1 && row <= 3) draw_portrait_map(s, 0xA8BB, 5, 8, 0x09, 0x1F);
    uint32_t bank = page_gold24(s->g->page, P_BANK_HI);
    switch (row) {
    case 0: say(s, 0xAC5A); return;                             /* A125 "busy man" */
    case 1: {                                                   /* A14B exchange ALL almas */
        if (!s->g->almas) { say(s, 0xA9B2); return; }
        int in = img8(s, 0xA8FA + 2u * (unsigned)(s->town_id - 1));
        int out = img8(s, 0xA8FA + 2u * (unsigned)(s->town_id - 1) + 1);
        say(s, 0xA9D9); print_text(s); print_number(s, (uint32_t)in); print_text(s);
        print_number(s, (uint32_t)out); print_text(s);
        menu_box(s, 0x2F, 0x2B, 0x0C, 0x19, 0x30, 0x2E);
        int yes = yes_no(s);
        vid_window(s->fb, 0, 0x2F, 0x2B, 0x0C, 0x19);
        if (!yes) { say(s, 0xAA82); return; }
        unsigned units = s->g->almas / (unsigned)in;
        if (!units) { say(s, 0xAA1D); return; }
        s->g->almas = (uint16_t)(s->g->almas - units * (unsigned)in);
        shop_gold_add(s, units * (uint32_t)out);
        say(s, 0xAB10);
        return; }
    case 2: {                                                   /* A23B deposit */
        if (!s->g->gold) { say(s, 0xAAA1); return; }
        uint32_t amt = s->g->gold;                              /* the port deposits everything */
        gold_pay(s, amt);
        page_set_gold24(s->g->page, P_BANK_HI, bank + amt);
        /* A331: a deposit of 1000 or more sets him counting — the second cell-map
         * pair at A7C3 — for the rest of the visit */
        if (amt >= 1000) { s->bought = 1; s->hk.bank_on = 0xFF; s->hk.bank_map = 0xA7C3; }
        say(s, 0xAAF4); print_text(s); print_number(s, bank + amt); print_text(s);
        return; }
    case 3: {                                                   /* A3D0 withdraw */
        if (!bank) { say(s, 0xAB32); return; }
        shop_gold_add(s, bank);
        page_set_gold24(s->g->page, P_BANK_HI, 0);
        say(s, 0xABA4); print_text(s); print_number(s, bank); print_text(s);
        return; }
    default:                                                    /* A595 balance */
        if (!bank) { say(s, 0xABF7); return; }
        say(s, 0xAC10); print_text(s); print_number(s, bank); print_text(s);
        return;
    }
}

/* ================================================================== SAGE == */
uint16_t sage_level_hp(const Shop *s, int level)
{
    if (level >= 16) return 800;
    return img16(s, 0xA380 + 9u * (unsigned)level);
}
void sage_level_magic(const Shop *s, int level, uint8_t out[7])
{
    if (level >= 16) { for (int i = 0; i < 7; i++) { int v = s->g->magic_max[i] + 2; out[i] = (uint8_t)(v > 255 ? 255 : v); } return; }
    for (int i = 0; i < 7; i++) out[i] = img8(s, 0xA380 + 9u * (unsigned)level + 2u + (unsigned)i);
}

int sage_assess(Shop *s)                                        /* A22E */
{
    int lvl = s->g->level < 15 ? s->g->level : 15;
    unsigned need = EXP_NEXT[lvl];
    if (s->g->exp < need / 2) return 0;
    if (s->g->exp < need - need / 4) return 1;
    if (s->g->exp < need) return 2;
    if (s->g->level < SAGE_MAX_LEVEL[s->town_id - 1]) return 3;
    s->cursor_row = 1;                                          /* kj_level_capped */
    return 4;
}

void sage_level_up(Shop *s)                                     /* A2B4 */
{
    Game *g = s->g;
    uint16_t hp = sage_level_hp(s, g->level);
    uint8_t mg[7];
    sage_level_magic(s, g->level, mg);
    if (g->level != 0xFF) g->level++;
    g->max_hp = g->hp = hp;
    memcpy(g->magic_max, mg, 7);
    memcpy(g->magic_count, mg, 7);
    int prev = g->level - 1; if (prev > 15) prev = 15; if (prev < 0) prev = 0;
    unsigned spent = EXP_NEXT[prev];
    g->exp = (uint16_t)(g->exp > spent ? g->exp - spent : 0);
    int cur = g->level < 15 ? g->level : 15;
    if (g->exp >= EXP_NEXT[cur]) g->exp = (uint16_t)(EXP_NEXT[cur] - 1);
}

static void sage_action(Shop *s, uint8_t op)
{
    Game *g = s->g;
    if (op >= 7 && op <= 0x0D) {                                /* A957 teach spell 1..7 */
        int n = op - 6;
        g->magic_sel = (uint8_t)n;
        g->page[P_SPELLS + n - 1] = 0xFF;
        return;
    }
    switch (op) {
    case 0: {                                                   /* A0CB main menu */
        vid_window(s->fb, 0, 0x27, 0x17, 0x1D, 0x41);
        menu_box(s, 0x27, 0x22, 0x1C, 0x2D, 0x27, 0x25);
        s->mvis = s->mtot = 4; s->mscroll = 0;
        menu_draw_items(s, 0xAD65, 4);
        int row = s->cursor_main;
        if (menu_select(s, &row)) row = 0;
        s->cursor_main = row;
        vid_window(s->fb, 0, 0x27, 0x17, 0x1D, 0x41);
        switch (row) {
        case 0: say(s, 0xADEB); return;
        case 1: say(s, s->bought ? (s->cursor_row ? 0xAF03 : 0xAEA7) : 0xAE08); return;
        case 2: say(s, img16(s, 0xB5EB + 2u * (unsigned)(s->town_id - 1))); print_text(s);
                say(s, 0xADBF); return;
        default:                                                /* A178 Record Experience */
            if (s->t) town_page_push(s->t);      /* [80]/[83]/[C2]: the page is the file */
            if (player_save_usr(g, s->dir, g->player_name[0] ? g->player_name : "ZELIARD") == 0) {
                s->saved = 1;
                say(s, 0xAF7C);                                 /* "I shall record your experiences." */
            } else say(s, 0xADBF);
            return;
        } }
    case 1: {                                                   /* A18E See Power */
        s->bought = 1;
        /* A1D1: the sage raises his arms — three frames at 0x19 ticks, eyes shut
         * and the blink held off while they play */
        s->hk.kj_blink_off = s->hk.kj_eyes_closed = 0xFF;
        for (int i = 0; i < 3 && !s->leave; i++) {
            kenj_draw_ritual(s, img8(s, 0xA1FD + (unsigned)i));
            shop_wait(s, 0x19);
        }
        s->hk.kj_blink_off = 0;
        shop_wait(s, 140);                                      /* A410 */
        s->hk.kj_ritual_on = s->hk.kj_blink_off = 0xFF;         /* A199: the aura runs */
        say(s, 0xAFDE);
        uint8_t r;
        do { shop_wait(s, 140); r = print_text(s); } while (r == 4 && !s->leave);
        s->hk.kj_fade = 0xFF;                                   /* A1B5 */
        /* A200: and lowers them again, frames A1FE then A1FD */
        s->hk.kj_blink_off = 0xFF;
        for (int i = 1; i >= 0 && !s->leave; i--) {
            kenj_draw_ritual(s, img8(s, 0xA1FD + (unsigned)i));
            shop_wait(s, 0x19);
        }
        s->hk.kj_blink_off = s->hk.kj_eyes_closed = 0;
        say(s, img16(s, 0xB029 + 2u * (unsigned)sage_assess(s)));
        return; }
    case 2:                                                     /* A914 "continue your quest?" */
        menu_box(s, 0x2B, 0x2F, 0x0C, 0x19, 0x30, 0x2E);
        yes_no(s);
        vid_window(s->fb, 0, 0x2B, 0x2F, 0x0C, 0x19);
        return;
    case 4: shop_wait(s, 140); return;
    case 5: sage_level_up(s); return;                           /* A2B4 */
    case 6: say(s, 0xADBF); return;
    default: return;
    }
}

/* ================================================================== KING == */
static void king_action(Shop *s, uint8_t op)
{
    switch (op) {
    case 0:                                                     /* A0E4: the twelve-step face */
        /* the 0x00 byte the scripts use as "he speaks": twelve frames from the
         * list at A0F8, 0x19 ticks each, with the hook running underneath */
        for (int i = 0; i < 12 && !s->leave; i++) {
            king_draw_face(s, img8(s, 0xA0F8 + (unsigned)i));
            shop_wait(s, 0x19);                                 /* A104 */
        }
        return;
    case 1:                                                     /* A09A: the 1000-gold gift */
        for (int i = 0; i < 10 && !s->leave; i++) { shop_gold_add(s, 100); shop_wait(s, 20); }
        s->g->page[P_KING_GIFT] = 0xFF;
        return;
    case 2: shop_wait(s, 150); return;                          /* A0D4 */
    case 3:                                                     /* A092 */
        s->hk.king_blink_on = 0xFF; king_draw_mouth(s, 1); return;
    case 4: s->hk.king_mouth_on = 0xFF; return;                 /* A084: lip-sync on */
    case 5:                                                     /* A08A: lip-sync off */
        s->hk.king_mouth_on = 0; king_draw_mouth(s, 0); return;
    default: return;
    }
}

/* ------------------------------------------------------------ the driver */
static unsigned shop_first_script(Shop *s)
{
    Game *g = s->g;
    switch (s->dest) {
    case SHOP_ARMOUR:
        if (!(g->page[P_CREST_FLAGS] & 2) && s->town_id == 5 && g->page[P_GLORY_CREST]) return 0xB2A2;
        return 0xADD3;
    case SHOP_DRUG:   return 0xA86B;
    case SHOP_CHURCH: return g->hp == g->max_hp ? 0xA2B4 : 0xA2F2;
    case SHOP_INN:    return 0xA2F6;
    case SHOP_BANK:   return 0xA98D;
    case SHOP_SAGE: {                                           /* AC07 */
        uint8_t bit = (uint8_t)(0x80 >> (s->town_id - 1));
        static const unsigned intro[8] = {0xB1B8, 0xB22D, 0xB29F, 0xB317, 0xB38C, 0xB400, 0xB488, 0xB51E};
        if (!(g->page[P_SAGES] & bit)) { g->page[P_SAGES] |= bit; return intro[s->town_id - 1]; }
        return 0xAD9D; }
    case SHOP_KING:
        if (!(g->page[P_KING_GIFT] | g->page[P_ENTERED])) return 0xA42F;
        if (!g->page[P_ENTERED]) return 0xA53C;
        if (!g->jashiin_defeated) return 0xA5D2;
        return 0xA6C1;
    default: return 0;
    }
}

static void shop_dispatch(Shop *s, uint8_t op)
{
    switch (s->dest) {
    case SHOP_ARMOUR: armr_action(s, op); break;
    case SHOP_DRUG:   drug_action(s, op); break;
    case SHOP_CHURCH: chur_action(s, op); break;
    case SHOP_INN:    inn_action(s, op);  break;
    case SHOP_BANK:   bank_action(s, op); break;
    case SHOP_SAGE:   sage_action(s, op); break;
    case SHOP_KING:   king_action(s, op); break;
    default: break;
    }
}

int shop_open(Shop *s, Town *t, int dest)
{
    if (dest < 0 || dest >= SHOP_COUNT) return -1;
    memset(s, 0, sizeof *s);
    s->t = t; s->g = t->g; s->dest = dest;
    s->town_id = t->map->town_id ? t->map->town_id : 1;
    s->font = (const TextFont *)t->font;
    const char *dir = t->dir ? t->dir : "../zeliard";
    s->dir = dir;
    size_t len;
    s->img = sar_load(dir, 1, SHOPS[dest].ovl, 1, &len);
    if (!s->img) return -1;
    s->imglen = len;
    /* the portrait bank: 256 PC-88 cells to arena:8000 */
    size_t plen;
    uint8_t *p = sar_load(dir, 1, SHOPS[dest].grp, 1, &plen);
    if (p) {
        int n = (int)(plen / 48);
        if (n > 256) n = 256;
        for (int i = 0; i < n; i++) { gfx_decode48(p + (size_t)i * 48, &s->cell[i]); s->present[i] = 1; }
        free(p);
    }
    /* this town's price table */
    if (dest == SHOP_ARMOUR) {
        unsigned tb = img16(s, 0xBAA7 + 2u * (unsigned)(s->town_id - 1));
        for (int i = 0; i < 12; i++) s->price[i] = img24(s, tb + 3u * (unsigned)i);
        s->nprice = 12;
        s->names_tbl = 0xAD05;
        armr_build(s, 0, armr_swords, &armr_nsw);
        armr_build(s, 1, armr_shields, &armr_nsh);
    } else if (dest == SHOP_DRUG) {
        unsigned tb = img16(s, 0xB10C + 2u * (unsigned)(s->town_id - 1));
        for (int i = 0; i < 8; i++) s->price[i] = img24(s, tb + 3u * (unsigned)i);
        s->nprice = 8;
        s->names_tbl = 0xB08A;
        drug_build(s);
    }
    shop_prologue(s);
    s->tp = shop_first_script(s);
    /* the enables the overlays' own entry code sets (everything else is zero
     * in the overlay image, which is where these variables live) */
    switch (dest) {
    case SHOP_ARMOUR:                                           /* A06B, cleared at A09E */
        s->hk.armr_on = (s->tp == 0xB2A2) ? 0 : 0xFF; break;
    case SHOP_INN:    s->hk.inn_on = 0xFF; break;               /* A06F */
    case SHOP_BANK:   s->hk.bank_on = 0xFF; s->hk.bank_map = 0xA773; break;  /* A058 */
    default: break;
    }
    return 0;
}

void shop_close(Shop *s) { free(s->img); s->img = NULL; }

/* bankpro A063..A08F: the box is cleared, then the teller's idle runs for five
 * 0x3F-tick passes before the greeting.  (The two "scripts" A989 and A98B the
 * original prints there are `0C FF 2E` and `FF 2E`: a clear and a no-op whose
 * return value it throws away.)  The idle is off for the rest of the visit
 * unless a big deposit or the goodbye turns it back on. */
static void bank_entry(Shop *s)
{
    for (int i = 0; i < 5 && !s->leave; i++) shop_wait(s, 0x3F);
    s->hk.bank_on = 0;                                          /* A08F */
}

void shop_loop(Shop *s)
{
    if (!s->tp) return;
    if (s->dest == SHOP_BANK) bank_entry(s);
    for (int guard = 0; guard < 200 && !s->leave; guard++) {
        uint8_t op = print_text(s);
        if (getenv("ZEL_SHOP_DEBUG")) fprintf(stderr, "[shop] op %02X at %04X frames %u leave %d text \"%s\"\n", op, s->tp, s->frames, s->leave, s->last_text);
        if (op == 0xFF) break;
        shop_dispatch(s, op);
    }
}

int shop_run(Town *t, int dest)
{
    static Shop shop;
    if (shop_open(&shop, t, dest)) return -1;
    t->shop = &shop;
    player_page_push(t->g);
    shop_loop(&shop);
    player_page_push(t->g);
    t->shop = NULL;
    shop_close(&shop);
    return 0;
}
