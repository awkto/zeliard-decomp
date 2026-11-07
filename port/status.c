/* status.c — the status / inventory screen and the potion effects.
 *
 * Port of SELECT.BIN (ZELRES2[1] @A000).  Every routine names the original
 * address; the layout constants, the name tables and the effect table come
 * straight out of src/select.c / docs/TOWN.md §12.  The screen is drawn with
 * the BASE:2000 video-driver primitives only (text.c), because the original
 * has to run under both town.bin and fight.bin. */
#include "status.h"
#include "render.h"
#include "audio.h"
#include "town.h"
#include "sar.h"
#include "player.h"
#include "enemy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======================================================================== */
/* itemp.grp (ZELRES2[27]) and the two icon blitters (video_mcga 2748/254C)  */
/* ======================================================================== */

int itemp_load(ItemPics *p, const char *dir)
{
    memset(p, 0, sizeof *p);
    size_t len;
    uint8_t *d = sar_load(dir, 1, 27, 1, &len);
    if (!d) return -1;
    if (len < 14) { free(d); return -1; }
    for (int i = 0; i < 7; i++) {                       /* GAME.BIN A113..A12A */
        unsigned o = (unsigned)(d[2 * i] | d[2 * i + 1] << 8);
        p->sec[i] = o < len ? o : 0;
    }
    p->raw = d; p->len = len; p->loaded = 1;
    /* the driver's built-in empty slot (GMMCGA.BIN @2658, origin 0x2000) */
    FILE *f = game_fopen(dir, "GMMCGA.BIN");
    if (f) {
        if (fseek(f, 0x2658 - 0x2000, SEEK_SET) == 0 &&
            fread(p->blank, 1, sizeof p->blank, f) == sizeof p->blank) p->have_blank = 1;
        fclose(f);
    }
    return 0;
}

void itemp_free(ItemPics *p)
{ free(p->raw); p->raw = NULL; p->loaded = 0; }

static inline void spx(uint8_t *fb, int x, int y, uint8_t v)
{ if (x >= 0 && x < TEXT_W && y >= 0 && y < TEXT_H) fb[y * TEXT_W + x] = v; }

/* video_mcga 27B2 pack6_x8: two PC-88 pixels (each C<<2|B<<1|A, MSB first out
 * of the three plane words) become one MCGA byte `left << 3 | right`, which is
 * exactly the blend-DAC index PAL_RGB is indexed by. */
static void pack6(uint8_t *fb, int *x, int y, unsigned A, unsigned B, unsigned C, int n)
{
    for (int i = 0; i < n; i++) {
        unsigned v = 0;
        for (int h = 0; h < 2; h++) {
            unsigned c = (C >> 15) & 1, b = (B >> 15) & 1, a = (A >> 15) & 1;
            A = (A << 1) & 0xFFFF; B = (B << 1) & 0xFFFF; C = (C << 1) & 0xFFFF;
            v = (v << 3) | (c << 2) | (b << 1) | a;
        }
        spx(fb, (*x)++, y, (uint8_t)v);
    }
}
#define BE16(p) ((unsigned)((p)[0] << 8 | (p)[1]))
#define LE16(p) ((unsigned)((p)[0] | (p)[1] << 8))

/* video_mcga 2748 icon32x16: 12 bytes/row x 16 rows -> 16x16 at (x4*4+2, y).
 * Row: A = BE words +0,+2; B = LE words +6,+4; C = BE words +8,+A. */
static void icon32x16(uint8_t *fb, const uint8_t *si, int x4, int y)
{
    for (int r = 0; r < 16; r++, si += 12) {
        int x = x4 * 4 + 2;
        pack6(fb, &x, y + r, BE16(si + 0), LE16(si + 6), BE16(si + 8), 8);
        pack6(fb, &x, y + r, BE16(si + 2), LE16(si + 4), BE16(si + 0xA), 8);
    }
}

void itemp_icon(uint8_t *fb, const ItemPics *p, int section, int index, int x4, int y)
{
    if (!p || !p->loaded) return;
    if (index < 0) { if (p->have_blank) icon32x16(fb, p->blank, x4, y); return; }
    if (section < 0 || section > 6) return;
    size_t o = (size_t)p->sec[section] + (size_t)index * 192;
    if (o + 192 > p->len) return;
    icon32x16(fb, p->raw + o, x4, y);
}

/* video_mcga 254C vid_icon_sword: 15 bytes/row x 18 rows -> 20x18 at (x8*8, y) */
void itemp_sword(uint8_t *fb, const ItemPics *p, int index, int x8, int y)
{
    if (!p || !p->loaded || index < 0) return;
    size_t o = (size_t)p->sec[0] + (size_t)index * 270;
    if (o + 270 > p->len) return;
    const uint8_t *si = p->raw + o;
    for (int r = 0; r < 18; r++, si += 15) {
        int x = x8 * 8;
        pack6(fb, &x, y + r, BE16(si + 0), LE16(si + 8), BE16(si + 0xA), 8);
        pack6(fb, &x, y + r, BE16(si + 2), LE16(si + 6), BE16(si + 0xC), 8);
        pack6(fb, &x, y + r, (unsigned)si[4] << 8, (unsigned)si[5] << 8, (unsigned)si[0xE] << 8, 4);
    }
}

/* MCGA [2018] 23CC / [201A] 23F5: the HUD's two 3-digit counters.  Both go
 * through to_digits_blank_leading (241B: vid_to_decimal's 7 digits, the first
 * six zeros turned into 0xFF) and vid_draw_digits (24A3) with the last three
 * digits, AH x4, AL 0xBB, CX 0x0103 (+2 px nudge), BX 0xFF01: every cell gets
 * its 6x7 box of 0x05 (24F2), a blank cell no glyph, a digit PC-88 colour 1. */
static void hud_count(uint8_t *fb, const TextFont *f, unsigned v, int x4)
{
    uint8_t d[7];
    for (int i = 6; i >= 0; i--) { d[i] = (uint8_t)(v % 10); v /= 10; }
    for (int i = 0; i < 6 && d[i] == 0; i++) d[i] = 0xFF;                   /* 2424 */
    for (int i = 0; i < 3; i++) {
        int x = x4 * 4 + 2 + i * 6;
        for (int r = 0; r < 7; r++)
            for (int c = 0; c < 6; c++) spx(fb, x + c, 0xBB + r, 0x05);     /* 24FB */
        uint8_t n = d[4 + i];
        if (n == 0xFF || !f || !f->loaded) continue;                        /* 2512 */
        for (int r = 0; r < 7; r++)
            for (int c = 0; c < 6; c++)
                if ((f->digit[n][r] >> (5 - c)) & 1) spx(fb, x + c, 0xBB + r, PC88[1]);
    }
}

/* town.bin 610F/6127, on every town entry (60B7..61B1, the edge-to-edge
 * walk included): `test [9D]` / `test [93]`, and each set one gets its
 * [2004] trough (6116 bh 0xAA / 612E bh 0xC6, bl 0x1C, ch 0x17) before its
 * counter.  An unset one is left alone: town.bin never clears the HUD strip
 * (2002 is the playfield only), so a trough drawn earlier stays. */
void hud_town_panels(Game *g)
{
    if (g->magic_sel) g->hud_panels |= 1;
    if (g->shield)    g->hud_panels |= 2;
}

/* GAME.BIN A19C/A1AE/A1C0: the HUD's three picture boxes and their counts.
 * docs/TOWN.md §12.6: [201E] is the *magic* icon (charges via [2018]) and
 * [2020] the *shield* icon (durability via [201A]). */
void itemp_hud(uint8_t *fb, const ItemPics *p, const TextFont *f, const Game *g)
{
    if (!p || !p->loaded) return;
    /* GAME.BIN A195/A1A7/A1B9 each `test byte [..],0xff / jz`: an empty slot is
     * not drawn at all, it keeps the white frame mole.bin painted there. */
    if (g->sword)     itemp_sword(fb, p, g->sword - 1, 0x18, 0xAB);        /* (192,171) */
    if (g->magic_sel) itemp_icon(fb, p, 3, g->magic_sel - 1, 0x37, 0xA4);  /* (222,164) */
    if (g->shield)    itemp_icon(fb, p, 1, g->shield - 1, 0x3E, 0xA4);     /* (250,164) */
    /* [2018]/[201A] draw only the 6x7 boxes; the trough round each counter is
     * whatever earlier code left in the HUD strip (Game.hud_panels): a
     * [2004] style-0 trough, bh 0xAA / 0xC6, bl 0x1C, 0x17 columns — black
     * column + top row, 0x05 fill, 0x2D lip at (218..241 / 246..269, 186..195). */
    if ((g->hud_panels & 1) && g->magic_sel) vid_gauge_bar(fb, 0xAA, 0x1C, 0x17);
    if ((g->hud_panels & 2) && g->shield)    vid_gauge_bar(fb, 0xC6, 0x1C, 0x17);
    if (g->magic_sel)                                                      /* [2018] */
        hud_count(fb, f, g->magic_count[g->magic_sel - 1], 0x37);          /* 23EA */
    if (g->shield)                                                         /* [201A] */
        hud_count(fb, f, g->shield_hp, 0x3E);                              /* 2410 */
}

/* ======================================================================== */
/* select.bin data (src/select.c)                                            */
/* ======================================================================== */

/* ADE8 */
static const struct { int x4, y, w4, h; } WINDOW[4] = {
    { 0x0C, 0x0E, 0x38, 0x33 }, { 0x0C, 0x3F, 0x22, 0x30 },
    { 0x0C, 0x6D, 0x22, 0x30 }, { 0x2D, 0x3F, 0x17, 0x5E },
};
/* A9FC */
static const struct { int x, y; const char *text; } HEADER[4] = {
    { 0x34, 0x12, "SELECT-MAGIC:" }, { 0x34, 0x43, "WEAR:" },
    { 0x34, 0x71, "USE:" },          { 0xB8, 0x43, "INVENTORY" },
};
static const char S_NOTHING[] = "NOTHING";              /* AA92 */

const char *const MAGIC_NAME[7] =                       /* AAB8 */
    { "Espada", "Saeta", "Fuego", "Lanzar", "Rascar", "Agua", "Guerra" };
const char *const ITEM_NAME[6][2] = {                   /* AAF3, indexed by [9E] */
    { "NO USE", "" },        { "Feruza",  "      shoes" }, { "Pirika",   "      shoes" },
    { "Silkarn", "      shoes" }, { "Ruzeria", "      shoes" }, { "Asbestos", "       cape" },
};
const char *const POTION_NAME[9][2] = {                 /* AC32, indexed by the slot value */
    { "NO USE", "" },      { "Ken\\ko", "      Potion" }, { "Juu-en ", "       Fruit" },
    { "Elixir", "    of Kashi" }, { "Chikara", "      Powder" }, { "Magia Stone", "" },
    { "Holy Water", "    of Acero" }, { "Sabre Oil", "" }, { "Kioku", "     feather" },
};
const char *const POTION_USED[8] = {                    /* AB62, indexed by the drug id */
    "       a Ken\\ko Potion.", "        a Juu-en Fruit.",
    "     a Elixir of Kashi.",  "      a Chikara Powder.",
    "         a Magia Stone.",  " a Holy Water of Acero.",
    "           a Sabre Oil.",  "       a Kioku Feather.",
};
const char *const SWORD_NAME[6][2] = {                  /* ACD9 */
    { "Training", "     Sword" },     { "Wise man\\s", "      Sword" },
    { "Spirit", "    Sword" },        { "Knight\\s", "    Sword" },
    { "Illumination", "       Sword" }, { "Enchantment", "       Sword" },
};
const char *const SHIELD_NAME[6][2] = {                 /* AD67 */
    { "Clay", "     Shield" },  { "Wise Man\\s", "      Shield" },
    { "Stone", "     Shield" }, { "Honor", "     Shield" },
    { "Light", "     Shield" }, { "Titanium", "      Shield" },
};
const uint16_t HOLY_WATER[6] = { 80, 90, 100, 110, 115, 120 };   /* A520 */

#define MAGIC_ICON_X4  0x0E
#define MAGIC_ICON_Y   0x1C
#define MAGIC_CUR_Y    0x1A
#define ITEM_ICON_X4   0x0E
#define ITEM_ICON_Y    0x55
#define ITEM_CUR_Y     0x53
#define POTION_ICON_X4 0x0E
#define POTION_ICON_Y  0x83
#define POTION_CUR_Y   0x81

/* ======================================================================== */
/* Drawing primitives                                                        */
/* ======================================================================== */

/* AA2B puts_shadow: 8-px pitch; every colour but 1 gets a blue (5) drop shadow */
static void sputs(Status *s, const char *t, int x, int y, int colour)
{
    for (; *t; t++, x += 8) {
        if (colour != 1) vid_putchar(s->fb, s->font, (uint8_t)*t, 5, x + 1, y + 1);
        vid_putchar(s->fb, s->font, (uint8_t)*t, colour, x, y);
    }
}
/* A9B3 draw_number: the last `n` of vid_to_decimal's 7 digits, leading zeros
 * included, no background box */
static void snum(Status *s, unsigned v, int n, int x4, int y, int colour)
{ vid_draw_digits_raw(s->fb, s->font, v, n, x4, y, colour, 0, 0); }

/* A9D5 draw_headers */
static void draw_headers(Status *s)
{
    for (int i = 0; i < 4; i++)
        sputs(s, HEADER[i].text, HEADER[i].x, HEADER[i].y, s->pane == i ? 2 : 3);
}
/* 202E vid_cursor_frame: a hollow 20x20 box, 2 px thick, at (x4*4, y) */
static void cursor_frame(Status *s, int colour, int x4, int y)
{
    uint8_t v = PC88[colour & 7];
    for (int r = 0; r < 20; r++)
        for (int c = 0; c < 20; c++)
            if (r < 2 || r >= 18 || c < 2 || c >= 18) spx(s->fb, x4 * 4 + c, y + r, v);
}
static void magic_cursor(Status *s, int colour)
{ cursor_frame(s, colour, MAGIC_ICON_X4 + s->magic_cursor * 8, MAGIC_CUR_Y); }
static void item_cursor(Status *s, int colour)
{ cursor_frame(s, colour, ITEM_ICON_X4 + s->item_cursor * 5, ITEM_CUR_Y); }
static void potion_cursor(Status *s, int colour)
{ cursor_frame(s, colour, POTION_ICON_X4 + s->potion_cursor * 5, POTION_CUR_Y); }

static void win(Status *s, int style, int x4, int y, int w4, int h)
{ vid_window(s->fb, style, x4, y, w4, h); }

/* A6F6 / A8D7: `repne scasb` — the position of `v`, or the last slot */
static int index_of(const uint8_t *list, int n, uint8_t v)
{
    for (int i = 0; i < n; i++) if (list[i] == v) return i;
    return n - 1;
}

/* ======================================================================== */
/* The three rows                                                            */
/* ======================================================================== */

/* A929 draw_magic_counts: "nnn" over "(mmm)" under each spell icon */
static void draw_magic_counts(Status *s)
{
    for (int i = 0; i < s->n_magic; i++) {
        int x4 = 0x0E + i * 8, y = 0x2E;
        int n = s->magic_list[i] - 1;
        win(s, 0, x4, y, 5, 8);                                     /* A94B */
        snum(s, s->g->magic_count[n], 3, x4, y, 1);                 /* A960 */
        int y2 = y + 9;                                             /* A964 */
        vid_putchar(s->fb, s->font, '(', 4, (x4 - 2) * 4 + 2, y2);  /* A978 */
        snum(s, s->g->magic_max[n], 3, x4, y2, 4);                  /* A98D */
        vid_putchar(s->fb, s->font, ')', 4, (x4 + 4) * 4 - 1, y2);  /* A9A0 */
    }
}
/* A904..A919 draw_magic_row's name print.  The original has no `test [9D]`
 * guard: with magic_sel [9D] == 0, `mov bl,[9d]; dec bl` wraps to 0xFF,
 * `xor bh,bh; add bx,bx` gives bx = 0x01FE, and `mov si,[bx-0x5548]` reads the
 * word at BASE:0xAAB8+0x1FE = 0xACB6 — the "cero" bytes of the potion-name
 * table, i.e. the pointer 0x6563 — then puts_shadow AA2B prints the byte
 * string at BASE:0x6563 until NUL.  0x6563 lies in the engine overlay swapped
 * at 6000 (fight.bin in a cavern, town.bin in a town), so the "string" is that
 * overlay's code; the original never clears it.  The port keeps the two byte
 * strings (ZELRES2[0] / ZELRES1[6] at offset 0x563) and prints them with the
 * extended font, so the stray draw matches. */
static const char STALE_MAGIC_CAVE[] =
    "\x09\x9f\x3a\x06\x0d\x9f\x73\x4f\xe8\x43\x08\x83\xee\x23\xe8\x1a"
    "\x08\x8a\x04\xe8\x6c\x08\x75\x2a\xc6\x06\xe7";
static const char STALE_MAGIC_TOWN[] =
    "\x47\x03\x5b\x59\xf6\x06\x5c\x7c\xff\x75\x08\xf6\x06\x1e\xff\xff"
    "\x74\x01\xc3\xf6\x06\x1d\xff\xff\x74\xe0\xd1\xeb\xd1\xeb\x8a\xfb"
    "\x8a\xd9\x32\xc0\xb9\x08\x02\x2e\xff\x16";

/* A8AF draw_magic_row */
static void draw_magic_row(Status *s)
{
    if (!s->n_magic) { sputs(s, S_NOTHING, 0x9E, 0x12, 1); return; }         /* A91C */
    for (int i = 0; i < s->n_magic; i++)
        itemp_icon(s->fb, s->pics, 3, s->magic_list[i] - 1, MAGIC_ICON_X4 + i * 8, MAGIC_ICON_Y);
    draw_magic_counts(s);
    s->magic_cursor = index_of(s->magic_list, 7, s->g->magic_sel);           /* A8D7 */
    magic_cursor(s, 5);
    /* A919 jmp AA2B: unconditional; magic_sel == 0 prints the overlay bytes */
    if (s->g->magic_sel) sputs(s, MAGIC_NAME[s->g->magic_sel - 1], 0x9E, 0x12, 1);
    else sputs(s, s->in_town ? STALE_MAGIC_TOWN : STALE_MAGIC_CAVE, 0x9E, 0x12, 1);
}
/* A6D1 draw_item_row */
static void draw_item_row(Status *s)
{
    if (!s->n_items) { sputs(s, S_NOTHING, 0x5C, 0x43, 1); return; }         /* A745 */
    for (int i = 0; i < s->n_items; i++)
        itemp_icon(s->fb, s->pics, 6, (int)s->item_list[i] - 1, ITEM_ICON_X4 + i * 5, ITEM_ICON_Y);
    s->item_cursor = index_of(s->item_list, 6, s->g->shoes);                 /* A6F6 */
    item_cursor(s, 5);
    int id = s->g->shoes <= 5 ? s->g->shoes : 0;
    sputs(s, ITEM_NAME[id][0], 0x5C, 0x43, 1);
    sputs(s, ITEM_NAME[id][1], 0x5C, 0x4B, 1);
}
/* A669 draw_potion_row */
static void draw_potion_row(Status *s)
{
    if (!s->n_potions) { sputs(s, S_NOTHING, 0x54, 0x71, 1); return; }       /* A6C4 */
    for (int i = 0; i < s->n_potions; i++)
        itemp_icon(s->fb, s->pics, 5, (int)s->potion_list[i] - 1, POTION_ICON_X4 + i * 5, POTION_ICON_Y);
    s->potion_sel = 0; s->potion_cursor = 0;                                 /* A68E */
    if (s->in_town) return;                                                  /* A698 */
    cursor_frame(s, 5, POTION_ICON_X4, POTION_CUR_Y);
    win(s, 0, 0x15, 0x70, 0x18, 0x11);
    sputs(s, POTION_NAME[0][0], 0x54, 0x71, 1);                              /* "NO USE" */
}

/* A752 draw_equipment — the INVENTORY window */
static void draw_power(Status *s)                                            /* A86E */
{
    if (!s->g->attack_bonus) return;
    win(s, 0, 0x32, 0x57, 0x08, 0x04);
    vid_putchar(s->fb, s->font, '(', 1, 0xCA, 0x57);
    snum(s, s->g->attack_bonus, 1, 0x34, 0x57, 1);
    vid_putchar(s->fb, s->font, ')', 1, 0xD4, 0x57);
}
static void draw_shield_hp(Status *s)                                        /* A844 */
{
    snum(s, s->g->page[P_SHIELD_MAX] | s->g->page[P_SHIELD_MAX + 1] << 8, 3, 0x34, 0x69, 4);
    vid_putchar(s->fb, s->font, '(', 4, 0xCA, 0x69);
    vid_putchar(s->fb, s->font, ')', 4, 0xE0, 0x69);
}
static void draw_equipment(Status *s)
{
    const Game *g = s->g;
    if (g->sword && g->sword <= 6) {                                         /* A752 */
        itemp_sword(s->fb, s->pics, g->sword - 1, 0x17, 0x4D);
        vid_label_asciiz(s->fb, s->font, SWORD_NAME[g->sword - 1][0], 0x34, 0x4E, 0);
        vid_label_asciiz(s->fb, s->font, SWORD_NAME[g->sword - 1][1], 0x34, 0x56, 0);
        draw_power(s);
    }
    if (g->shield && g->shield <= 6) {                                       /* A789 */
        itemp_icon(s->fb, s->pics, 1, g->shield - 1, 0x2E, 0x61);
        vid_label_asciiz(s->fb, s->font, SHIELD_NAME[g->shield - 1][0], 0x34, 0x61, 0);
        vid_label_asciiz(s->fb, s->font, SHIELD_NAME[g->shield - 1][1], 0x34, 0x69, 0);
        draw_shield_hp(s);
    }
    if (g->keys) {                                                           /* A7C0 */
        itemp_icon(s->fb, s->pics, 4, 0, 0x2E, 0x75);
        vid_putchar(s->fb, s->font, 0x5E, 1, 0xC8, 0x7E);
        snum(s, g->keys, 1, 0x34, 0x7E, 1);
    }
    if (g->lion_keys) {                                                      /* A7EF */
        itemp_icon(s->fb, s->pics, 4, 1, 0x3A, 0x75);
        vid_putchar(s->fb, s->font, 0x5E, 1, 0xF8, 0x7E);
        snum(s, g->lion_keys, 1, 0x40, 0x7E, 1);
    }
    int x4 = 0x30;                                                           /* A81E */
    for (int i = 0; i < 3; i++)
        if (g->page[0x9A + i]) { itemp_icon(s->fb, s->pics, 2, i, x4, 0x89); x4 += 6; }
}

/* ======================================================================== */
/* Lists                                                                     */
/* ======================================================================== */

void status_build_lists(Status *s)
{
    Game *g = s->g;
    s->n_magic = 0;                                                          /* A033 */
    for (int n = 1; n <= 7; n++) if (g->page[P_SPELLS + n - 1]) s->magic_list[s->n_magic++] = (uint8_t)n;
    int c = 0;                                                               /* A052 */
    s->item_list[0] = 0;
    for (int i = 0; i < 5; i++) if (g->page[0xA1 + i]) s->item_list[1 + c++] = g->page[0xA1 + i];
    s->n_items = c ? c + 1 : 0;
    c = 0;                                                                   /* A643 */
    s->potion_list[0] = 0;
    for (int i = 0; i < 5; i++) if (g->page[P_POTIONS + i]) s->potion_list[1 + c++] = g->page[P_POTIONS + i];
    s->n_potions = c ? c + 1 : 0;
}

/* ======================================================================== */
/* Selection                                                                 */
/* ======================================================================== */

/* A135 magic_select */
void status_select_magic(Status *s)
{
    s->g->magic_sel = s->magic_list[s->magic_cursor];
    s->g->page[0x9D] = s->g->magic_sel;
    win(s, 0, 0x27, 0x11, 0x10, 0x09);
    if (s->g->magic_sel) sputs(s, MAGIC_NAME[s->g->magic_sel - 1], 0x9E, 0x12, 1);
}
/* A228 item_select: the chosen key item becomes the *worn* one ([9E]) */
void status_select_item(Status *s)
{
    s->g->shoes = s->item_list[s->item_cursor];
    s->g->page[0x9E] = s->g->shoes;
    win(s, 0, 0x17, 0x42, 0x16, 0x11);
    int id = s->g->shoes <= 5 ? s->g->shoes : 0;
    sputs(s, ITEM_NAME[id][0], 0x5C, 0x43, 1);
    sputs(s, ITEM_NAME[id][1], 0x5C, 0x4B, 1);
}
/* A33C potion_show */
static void potion_show(Status *s)
{
    s->potion_sel = s->potion_list[s->potion_cursor];
    win(s, 0, 0x15, 0x70, 0x18, 0x11);
    int id = s->potion_sel <= 8 ? s->potion_sel : 0;
    sputs(s, POTION_NAME[id][0], 0x54, 0x70, 1);
    sputs(s, POTION_NAME[id][1], 0x54, 0x78, 1);
}

/* A60F / A629: park the 224x36 region behind a message box and put it back */
static void save_box(Status *s)
{
    if (s->box_open) return;
    s->box_open = 1;
    for (int r = 0; r < 0x24; r++)
        memcpy(s->box_save + r * 224, s->fb + (0x43 + r) * TEXT_W + 6 * 8, 224);
}
static void close_box(Status *s)
{
    if (!s->box_open) return;
    s->box_open = 0;
    for (int r = 0; r < 0x24; r++)
        memcpy(s->fb + (0x43 + r) * TEXT_W + 6 * 8, s->box_save + r * 224, 224);
}
/* A5DA show_used_box */
static void show_used_box(Status *s)
{
    save_box(s);
    win(s, 0xFF, 0x0F, 0x43, 0x32, 0x24);
    sputs(s, "I have used", 0x44, 0x4C, 1);
    if (s->potion_sel >= 1 && s->potion_sel <= 8) {
        sputs(s, POTION_USED[s->potion_sel - 1], 0x48, 0x56, 1);
        snprintf(s->last_used, sizeof s->last_used, "%s", POTION_USED[s->potion_sel - 1]);
    }
}
/* A3B7 show_level_box — the hidden LEVEL / EXP panel */
static void show_level_box(Status *s)
{
    if (s->box_open) return;
    save_box(s);
    win(s, 0xFF, 0x1B, 0x43, 0x1A, 0x24);
    sputs(s, "LEVEL", 0x80, 0x4C, 1);
    snum(s, (unsigned)s->g->level + 1, 2, 0x2C, 0x4C, 6);
    sputs(s, "EXP", 0x80, 0x56, 1);
    snum(s, s->g->exp, 5, 0x28, 0x56, 6);
}
/* A5B4 potion_epilogue */
static void potion_epilogue(Status *s)
{
    potion_cursor(s, 0);
    win(s, 0, 0x0E, 0x83, 0x1E, 0x10);
    if (!s->n_potions) s->n_potions = 1;
    draw_potion_row(s);
    potion_cursor(s, 2);
}

/* ======================================================================== */
/* A40D use_potion + the eight effects (A452 jump table)                     */
/* ======================================================================== */

static void status_raw_present(Status *s);
static void status_dissolve(Status *s);

int status_use_potion(Status *s)
{
    Game *g = s->g;
    if (!s->potion_sel) return 0;                                            /* A40D */
    close_box(s);
    /* A422..A437: zero the [A6..AA] slot the cursor points at */
    int seen = 0, bx = P_POTIONS;
    while (seen != s->potion_cursor) { if (g->page[bx]) seen++; bx++; }
    if (bx > P_POTIONS) g->page[bx - 1] = 0;
    status_build_lists(s);                                                   /* A43B */
    s->menu_result = s->potion_sel;                                          /* A43E  FF4B */
    g->sfx_request = (s->potion_sel == 8) ? 0x0F : 0x0E;

    switch (s->potion_sel - 1) {
    case 0:                                                                  /* A462 Ken'ko */
        g->hp = (uint16_t)(g->hp + 0x50);
        if (g->hp > g->max_hp) g->hp = g->max_hp;
        break;
    case 1:                                                                  /* A483 Juu-en */
        g->hp = g->max_hp;
        break;
    case 2:                                                                  /* A496 Elixir */
        if (!g->magic_sel) break;
        g->magic_count[g->magic_sel - 1] = g->magic_max[g->magic_sel - 1];
        break;
    case 3:                                                                  /* A4BE Chikara */
        memcpy(g->magic_count, g->magic_max, 7);
        break;
    case 4:                                                                  /* A52C Magia Stone */
        orbs_arm(g, 4, 1, 0x50);
        for (int i = 0; i < 4; i++) { g->orbs[i].phase = (uint8_t)(i * 4); g->orbs[i].hits = 0x50; }
        g->orbs[0].speed = 1; g->orbs[1].speed = 0xFF;
        g->orbs[2].speed = 0xFF; g->orbs[3].speed = 1;
        break;
    case 5:                                                                  /* A4EA Holy Water */
        if (!g->shield || g->shield > 6) break;
        g->shield_hp = (uint16_t)(g->shield_hp + HOLY_WATER[g->shield - 1]);
        {
            uint16_t mx = (uint16_t)(g->page[P_SHIELD_MAX] | g->page[P_SHIELD_MAX + 1] << 8);
            if (g->shield_hp > mx) g->shield_hp = mx;
        }
        break;
    case 6:                                                                  /* A4DB Sabre Oil */
        g->attack_bonus++;
        break;
    case 7:                                                                  /* A58B Kioku Feather */
        show_used_box(s);
        potion_epilogue(s);
        audio_music_fade(8);                                                 /* A598: FF24 = 8 */
        for (int f = 0; f < 6; f++) status_raw_present(s);                   /* A59D: 120 ticks, 6 frames of 20, no poll */
        if (s->user) {                                                       /* the wait's exit: the oracle's wA5A2 picture (#119) */
            if (s->in_town) { Town *t = s->user; if (t->side) t->side(t, SIDE_WAIT, 0xA5A2, 0); }
            else { Game *gg = s->user; if (gg->side) gg->side(gg, SIDE_WAIT, 0xA5A2, 0); }
        }
        status_dissolve(s);                                                  /* A5A9 [2040] over the status screen */
        audio_music_stop();                                                  /* A5AE: int 60h AX=1 */
        s->done = 1;                                                         /* returns out of the overlay */
        player_page_push(g);
        return s->menu_result;
    default: break;
    }
    show_used_box(s);                                                        /* A5DA (pushed A5B4/A2C7) */
    potion_epilogue(s);
    player_page_push(g);
    /* the INVENTORY window shows the Sabre Oil bonus and the shield line */
    draw_power(s);
    return s->menu_result;
}

/* ======================================================================== */
/* The screen                                                                */
/* ======================================================================== */

const uint8_t *status_framebuffer(const Status *s) { return s->fb; }

static void status_frame(Status *s)
{
    s->frames++;
    if (s->frame_guard && (int)s->frames > s->frame_guard) { s->done = 1; return; }
    /* STICK's timer plays and clears FF75 within a few ticks; select.bin's
     * drawing and its polls take longer than that, so a request made on the
     * screen (open 0x0B, move 0x0C, row 0x0D) is gone by the next poll */
    sound_request(s->g);
    if (s->present) s->present(s);
}

/* a frame on screen that is not a poll of select:AA58: no boundary, no input read */
static void status_raw_present(Status *s)
{
    if (!s->user) return;
    if (s->in_town) { Town *t = s->user; if (t->present) t->present(t); }
    else { Game *g = s->user; if (g->present) g->present(g); }
}

/* A5A9 [2040]: eight destructive passes over the status screen's own buffer, one presented
 * frame each, flagged so the oracle's dsv_*.png and the port's line up (#118) */
static void status_dissolve(Status *s)
{
    int *flag = !s->user ? NULL : s->in_town ? &((Town *)s->user)->dsv_pic : &((Game *)s->user)->dsv_pic;
    if (flag) *flag = 1;
    for (int n = 0; n < 8; n++) {
        render_dissolve_pass(s->fb, n);
        status_raw_present(s);
    }
    if (flag) *flag = 0;
}

void status_open(Status *s, Game *g, const TextFont *f, const ItemPics *p, int in_town)
{
    StatusPresentFn pf = s->present; void *user = s->user; int guard = s->frame_guard;
    memset(s, 0, sizeof *s);
    s->present = pf; s->user = user; s->frame_guard = guard;
    s->g = g; s->font = f; s->pics = p;
    s->in_town = in_town ? 0xFF : 0;
    player_page_push(g);
    /* the caller cleared the playfield first (town 6901 / fight 728C); the
     * screen furniture around it is mole.bin's, painted once at boot */
    for (int y = 14; y < 158; y++) memset(s->fb + y * TEXT_W + 48, 0, 224);
    render_screen_frame(s->fb, g->screen, 0, TEXT_H);
    for (int i = 0; i < 4; i++)                                              /* A015 */
        win(s, 0xFF, WINDOW[i].x4, WINDOW[i].y, WINDOW[i].w4, WINDOW[i].h);
    draw_headers(s);                                                         /* A02E */
    status_build_lists(s);
    draw_magic_row(s);                                                       /* A078 */
    draw_item_row(s);                                                        /* A07B */
    draw_potion_row(s);                                                      /* A07E */
    draw_equipment(s);                                                       /* A081 */
    s->menu_key_held = 0xFF;         /* A084: the Enter that opened it is down */
    /* A08C: the first non-empty row; the potion row is skipped in town */
    if (s->n_magic)                       s->pane = 0;
    else if (s->n_items)                  s->pane = 1;
    else if (!s->in_town && s->n_potions) s->pane = 2;
    else                                  s->pane = 0xFF;                    /* A0AE: idle only */
    if (s->pane != 0xFF) draw_headers(s);
}

/* AA58 idle_poll: Enter closes the screen, but the Enter that opened it
 * (menu_key_held, set by the A084 probe) has to be released first */
static int status_idle_poll(Status *s)
{
    if (!s->menu_key_held) return s->menu_key != 0;
    if (!s->menu_key) s->menu_key_held = 0;                                  /* AA7F */
    return 0;
}

static void row_cursor(Status *s, int colour)
{
    if (s->pane == 0) magic_cursor(s, colour);
    else if (s->pane == 1) item_cursor(s, colour);
    else potion_cursor(s, colour);
}

/* a release wait (`do int 61h while (al & mask)`): every pass is a poll site
 * with a boundary and reads nothing but the directions */
static void release_wait(Status *s, uint8_t mask)
{
    do status_frame(s); while (!s->done && (s->dirs & mask));
}

/* A0CA / A1BB / A2B9 — one input loop per row, re-entered through the A0B8
 * dispatch on every row change.  The port folds the three into one loop with
 * the same rules and the same polls:
 *   - row entry: headers, the red cursor, then the release wait for U/D
 *     (A0D2 / A1C3 / A2C1);
 *   - a pass: idle_poll AA58 (Enter) then int 61h;
 *   - after an L/R step, the release wait for L/R (A174 / A25F / A373).
 * Each status_frame() is one poll that takes a boundary (select:AA58 under
 * --trace).  The row loop's first idle_poll after a release wait comes in the
 * same tick as the wait's last poll and reads the same input: no frame. */
void status_loop(Status *s)
{
    if (s->pane == 0xFF) {                                                   /* A0AE: idle only */
        while (!s->done) {
            status_frame(s);
            if (s->done || status_idle_poll(s)) break;
        }
        player_page_push(s->g);
        return;
    }
    int enter = 1, fresh = 0;
    while (!s->done) {
        if (enter) {                                                         /* A0B8 dispatch */
            enter = 0;
            draw_headers(s);
            row_cursor(s, 2);
            release_wait(s, 3);
            fresh = 1;
        }
        if (!fresh) status_frame(s);
        fresh = 0;
        if (s->done) break;
        if (status_idle_poll(s)) break;                                      /* A0D8 / A1C9 / A2C7 */
        uint8_t d = s->dirs;
        int want = -1;
        if (s->pane == 0) {
            if (!(d & 0x0E)) continue;                                       /* A0E0 */
            if (!(d & 0x0C)) {                                               /* A190 down */
                /* reaches the potion row even in town: the `test [ADF8]` at
                 * A199 is clobbered by the `mov cl,2` that follows */
                if (s->n_items) want = 1; else if (s->n_potions) want = 2;
                if (want < 0) continue;
            }
        } else if (s->pane == 1) {
            if (!(d & 0x0F)) continue;                                       /* A1D1 */
            if (!(d & 0x0C)) {                                               /* A27D */
                if (d & DIR_UP) { if (!s->n_magic) continue; want = 0; }
                else { if (s->in_town || !s->n_potions) continue; want = 2; } /* A293 */
            }
        } else {
            if (s->buttons & 4) { show_level_box(s); continue; }            /* A2CD: FF18 == 0286 */
            if (s->buttons & 1) {                                            /* A2DA */
                status_use_potion(s);
                continue;
            }
            if (!(d & 0x0D)) continue;                                       /* A2E2 */
            close_box(s);                                                    /* A2E7 */
            if (!(d & 0x0C)) {                                               /* A391 up */
                if (s->n_items) want = 1; else if (s->n_magic) want = 0;
                if (want < 0) continue;
            }
        }
        if (want >= 0) {                                                     /* a row change */
            s->g->sfx_request = 0x0D;
            row_cursor(s, 5);
            s->pane = (uint8_t)want;
            enter = 1;
            continue;
        }
        /* left / right inside the row */
        int left = (d & DIR_LEFT) != 0;
        int *cur = s->pane == 0 ? &s->magic_cursor : s->pane == 1 ? &s->item_cursor : &s->potion_cursor;
        int n    = s->pane == 0 ? s->n_magic       : s->pane == 1 ? s->n_items      : s->n_potions;
        if (left ? (*cur == 0) : (n - 1 < *cur + 1)) continue;
        row_cursor(s, 0);
        *cur += left ? -1 : 1;
        row_cursor(s, 2);
        s->g->sfx_request = 0x0C;
        if (s->pane == 0) status_select_magic(s);
        else if (s->pane == 1) status_select_item(s);
        else potion_show(s);
        release_wait(s, 0x0C);                                               /* A174 / A25F / A373 */
        fresh = 1;
    }
    player_page_push(s->g);
}

/* ------------------------------------------------------------ the shells */
static void status_present_town(Status *s)
{
    Town *t = s->user;
    t->frame_no++;
    if (t->present) t->present(t);
    if (wait_clock.fn) wait_boundary_now(t->g, t, "select:AA58");
    s->dirs = t->dirs; s->buttons = t->buttons; s->menu_key = t->menu_key;
}
static void status_present_fight(Status *s)
{
    Game *g = s->user;
    g->frame_no++;
    if (g->present) g->present(g);
    if (wait_clock.fn) wait_boundary_now(g, NULL, "select:AA58");
    s->dirs = g->dirs; s->buttons = g->buttons; s->menu_key = g->menu_key;
}

int status_run_town(struct Town *t)
{
    static Status st;
    memset(&st, 0, sizeof st);
    st.present = status_present_town; st.user = t;
    status_open(&st, t->g, (const TextFont *)t->font, t->pics, 1);           /* [A002] */
    t->status = &st;
    status_loop(&st);
    t->status = NULL;
    t->g->sfx_request = 0x0B;
    return st.menu_result;
}

int status_run_fight(Game *g)
{
    static Status st;
    memset(&st, 0, sizeof st);
    st.present = status_present_fight; st.user = g;
    status_open(&st, g, g->font, g->pics, 0);                                /* [A000] */
    g->status = &st;
    status_loop(&st);
    g->status = NULL;
    g->sfx_request = 0x0B;
    return st.menu_result;
}
