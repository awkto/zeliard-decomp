/*
 * video_mcga.c — hand-cleaned C of GMMCGA.BIN, the Zeliard MCGA video driver.
 *
 * Loaded raw at BASE:2000 (3273 bytes). Words 0x2000..0x2044 are a 35-entry
 * near-call vector table; callers do `call [cs:0x20xx]`. Register arguments
 * are written as C parameters; register scratch is not modelled. Addresses in
 * comments are the original offsets. NOT compilable — faithful to the asm.
 *
 * Conventions used throughout:
 *   VRAM      = A000:0000, 320x200, 1 byte/pixel, stride 320 (0x140).
 *   Pixel values are indices into the 64-entry blend palette built by
 *   GAME.BIN @A41B: DAC[l*8+r] = BASE[l]+BASE[r]; plain PC-88 colour c = c*9.
 *   x4 = x in 4-px columns, x8 = x in 8-px cell columns, y = 0..199.
 *   BASE:0000 = player record (STDPLY.BIN layout); BASE:FFxx = state page.
 *   STAGING   = segment CS+0x3000 (last 64 KB of the 256 KB block).
 */

typedef unsigned char  u8;
typedef unsigned short u16;

#define VRAM_SEG      0xA000
#define STRIDE        320
#define STAGING_SEG   (CS + 0x3000)

/* --- shared memory read by the driver ----------------------------------- */
#define ARENA_SEG     (*(u16 *)0xFF2C)        /* data arena segment          */
#define FLAG_DEMO_PAL (*(u8  *)0xFF77)        /* intro/demo palette mode     */
#define FONT_TEXT     (*(u16 *)0xF500)        /* font.grp section 1: 8x8     */
#define FONT_DIGITS   (*(u16 *)0xF502)        /* section 2: 6x7 digits       */
#define FONT_NARROW   (*(u16 *)0xF504)        /* section 3: 4-px labels      */
#define ITEMP_SEC(n)  (*(u16 far *)(ARENA_SEG:0xE200 + 2*(n)))  /* itemp.grp */

#define PL_GOLD_HI    (*(u8  *)0x0085)        /* gold bits 16..23            */
#define PL_GOLD_LO    (*(u16 *)0x0086)        /* gold bits 0..15             */
#define PL_ALMAS      (*(u16 *)0x008B)
#define PL_LIFE_CUR   (*(u16 *)0x0090)        /* inferred (12 callers)       */
#define PL_MAGIC_SEL  (*(u8  *)0x0093)
#define PL_MAGIC_VAL  (*(u16 *)0x0094)
#define PL_ITEM_SEL   (*(u8  *)0x009D)        /* 1-based                     */
#define PL_ITEM_CNT   ((u8 *)0x00AB)          /* byte per slot               */
#define PL_LIFE_MAX   (*(u16 *)0x00B2)        /* inferred (3 callers)        */

/* --- driver-private data ------------------------------------------------ */
static u8  bar_style;                 /* 0x2226: arg AL of vid_gauge_bar     */
static u8  digits[7];                 /* 0x2433..0x2439: decimal scratch     */
static const u8 colour9[8] =          /* 0x24EA: PC-88 colour -> VGA index   */
    { 0x00, 0x09, 0x12, 0x1B, 0x24, 0x2D, 0x36, 0x3F };
static const u8 blank_icon[0xC0];     /* 0x2658: empty 32x16 slot picture    */
static const u8 dissolve_masks[8] =   /* 0x218D                              */
    { 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF };
static const u8 *tear_icons[2] =      /* 0x2A5D -> 0x2A61, 0x2B31 (13x16 B)  */
    { (u8 *)0x2A61, (u8 *)0x2B31 };
static u8  fg_colour;                 /* 0x2CBD                              */
static u8  bg_colour;                 /* 0x2CBE (also "draw digit box" flag) */
static u8  str_colour;                /* 0x2CBF                              */
static u16 str_x;                     /* 0x2CC0                              */
static u8  str_y;                     /* 0x2CC2                              */
static u16 planeA, planeB, planeC;    /* 0x2CC3, 0x2CC5, 0x2CC7              */

/* ======================================================================== */
/* slot 0x2000 -> 0x2046  vid_window                                        */
/* AL style (0 = clear rect, else window frame), BH x4, BL y, CH w4, CL h   */
/* ======================================================================== */

/* 0x20E8: clear CL rows of CH*4 pixels at ES:DI (ES set to VRAM here) */
static void clear_rect(u16 di, u8 w4, u8 rows)
{
    ES = VRAM_SEG;
    do {
        memset(ES:di, 0, w4 * 4);                /* rep stosw, CH*2 words */
        di += STRIDE;
    } while (--rows);
}

/* 0x20B6: one horizontal frame line. First word: keep bits ~AX, set (AX&DX);
 * middle CH*4-4 bytes = DL; last word: keep ~BX, set (BX&DX). Advances one row.
 * (AX=0,BX=0 -> corner words untouched; AX=FF00,BX=00FF -> inner corner px) */
static u16 frame_hline(u16 di, u16 ax, u16 bx, u16 dx, u8 w4)
{
    u16 *p = (u16 *)(ES:di);
    *p = (*p & ~ax) | (ax & dx);
    memset(ES:di + 2, (u8)dx, w4 * 4 - 4);
    p = (u16 *)(ES:di + 2 + w4 * 4 - 4);
    *p = (*p & ~bx) | (bx & dx);
    return di + STRIDE;
}

void vid_window(u8 al_style, u8 bh_x4, u8 bl_y, u8 ch_w4, u8 cl_h)   /* 0x2046 */
{
    u16 di = bh_x4 * 4 + bl_y * STRIDE;
    if (al_style == 0) { clear_rect(di, ch_w4, cl_h); return; }        /* 0x205F */

    u16 dx = 0x0909;                                  /* white (1*8+1) */
    if (FLAG_DEMO_PAL) dx = 0xFFFF;                   /* demo palette: 0xFF */
    cl_h -= 4;
    clear_rect(di + 2 * STRIDE, ch_w4, cl_h);         /* interior            */
    di = frame_hline(di, 0x0000, 0x0000, dx, ch_w4);  /* row 0: px 2..w-3    */
    di = frame_hline(di, 0xFF00, 0x00FF, dx, ch_w4);  /* row 1: px 1..w-2    */
    {                                                 /* 0x209A: sides, 2 px */
        u16 right = (ch_w4 - 1) * 4;
        u8 rows = cl_h;
        do {
            *(u16 *)(ES:di)             = dx;
            *(u16 *)(ES:di + right + 2) = dx;
            di += STRIDE;
        } while (--rows);
    }
    di = frame_hline(di, 0xFF00, 0x00FF, dx, ch_w4);  /* bottom, mirrored    */
    di = frame_hline(di, 0x0000, 0x0000, dx, ch_w4);
}

/* ======================================================================== */
/* slot 0x2002 -> 0x2106  vid_clear_playfield: rows 14..157, x 48..271      */
/* (written in an 8 x 18 interleaved order; the union is the rectangle)    */
/* ======================================================================== */
void vid_clear_playfield(void)                                      /* 0x2106 */
{
    u16 di = 0x11B0;                    /* 14*320 + 48 */
    ES = VRAM_SEG;
    for (int i = 0; i < 8; i++, di += STRIDE) {
        u16 p = di;
        for (int j = 0; j < 18; j++, p += 8 * STRIDE)
            memset(ES:p, 0, 224);
    }
}

/* ======================================================================== */
/* slot 0x2040 -> 0x2130  vid_dissolve_playfield                            */
/* 8 passes with masks 01..FF; each mask rotates across the pixels of a row */
/* and is phase-shifted 3 bits per row; set bits black out the pixel.       */
/* Even rows use ROL, odd rows ROR (checkerboard-ish dissolve).             */
/* ======================================================================== */
void vid_dissolve_playfield(void)                                   /* 0x2130 */
{
    ES = VRAM_SEG;
    for (int step = 0; step < 8; step++) {
        u8 al = dissolve_masks[step];
        u16 di = 0x11B0;
        for (int r = 0; r < 72; r++) {                 /* rows 14,16,..,156 */
            for (int x = 0; x < 224; x++, di++) {
                al = rol8(al, 1);
                if (carry) *(u8 *)(ES:di) = 0;
            }
            al = ror8(al, 3);
            di += 0x1A0;                               /* +2 rows - 224 */
        }
        di = 0x11B0 + STRIDE;
        for (int r = 0; r < 72; r++) {                 /* rows 15,17,..,157 */
            for (int x = 0; x < 224; x++, di++) {
                al = ror8(al, 1);
                if (carry) *(u8 *)(ES:di) = 0;
            }
            al = rol8(al, 3);
            di += 0x1A0;
        }
        for (u16 d = 0x1F40; d; d--) ;                 /* 0x2184 delay */
    }
}

/* ======================================================================== */
/* slot 0x2004 -> 0x2195  vid_gauge_bar                                     */
/* AL style, BH x (px), BL y, CH width px; drawn at (48+BH, 158+BL), 10 rows */
/* style 0    : trough — row0 = 0, rows1-8 = 0x05 (blk+blu), row9 = 0x2D    */
/* style 0x80 : erase the 10 rows                                           */
/* other      : set the 10 rows to colour 1 (grey line)                     */
/* ======================================================================== */

/* 0x21CE: one column, AX = FFFF for a bar column, 0 for the leading column */
static void gauge_column(u16 di, u16 ax)
{
    u8 ah = ax >> 8, al = (u8)ax;
    if (bar_style == 0) {                                          /* 0x21D6 */
        ah &= 0x05; al &= 0x2D;
        *(u8 *)(ES:di) = 0;  di += STRIDE;
        for (int i = 0; i < 8; i++, di += STRIDE) *(u8 *)(ES:di) = ah;
        *(u8 *)(ES:di) = al;
    } else if (bar_style != 0x80) {                                /* 0x21FD */
        ah = ~al; al &= 1;
        for (int i = 0; i < 10; i++, di += STRIDE)
            *(u8 *)(ES:di) = (*(u8 *)(ES:di) & ah) | al;
    } else {                                                       /* 0x2215 */
        al = ~al;
        for (int i = 0; i < 10; i++, di += STRIDE)
            *(u8 *)(ES:di) &= al;
    }
}

void vid_gauge_bar(u8 al_style, u8 bh_x, u8 bl_y, u8 ch_w)          /* 0x2195 */
{
    bar_style = al_style;
    ES = VRAM_SEG;
    u16 di = (bl_y + 0x9E) * STRIDE + bh_x + 0x30;
    gauge_column(di, 0x0000);              /* column left of the bar */
    di++;
    do { gauge_column(di, 0xFFFF); di++; } while (--ch_w);
}

/* slot 0x2012 -> 0x2385: the ENEMY trough, 136 px at (50,174) */
void vid_enemy_gauge_trough(void)                                   /* 0x2385 */
{
    vid_gauge_bar(0, 2, 0x10, 0x88);
}

/* ======================================================================== */
/* slots 0x2006/0x200A/0x2008/0x200C — LIFE and ENEMY bars                  */
/* value/8 one-px segments (cap 100) starting at x=84; life row 163         */
/* (0xCC14), enemy row 175 (0xDB14). The two bars of a pair are composed    */
/* with complementary masks: red 0x12 keeps bits 0x2D, white 0x09 keeps     */
/* bits 0x12 -> overlap = 0x1B = green.                                     */
/* ======================================================================== */

/* 0x229E: BX -> segment count (BX > 0x320 -> 100, else BX>>3) */
static u16 bar_segments(u16 bx) { return bx > 0x320 ? 100 : bx >> 3; }

/* 0x22B0: BH rows: [di] = ([di] & AH) | AL */
static void fill_column(u16 di, u8 rows, u8 al_or, u8 ah_and)
{
    do { *(u8 *)(ES:di) = (*(u8 *)(ES:di) & ah_and) | al_or; di += STRIDE; }
    while (--rows);
}

/* 0x2236: red 6-row bar of n segments (no clearing) */
static void bar_red6(u16 di, u16 bx)
{
    ES = VRAM_SEG;
    u16 n = bar_segments(bx);
    if (!n) return;
    do { fill_column(di, 6, 0x12, 0x2D); di++; } while (--n);
}

/* 0x2265: white 5-row bar of n segments, then 100-n segments cleared (keep 0x12) */
static void bar_white5(u16 di, u16 bx)
{
    ES = VRAM_SEG;
    u16 n = bar_segments(bx), rest;
    for (rest = n; rest; rest--, di++) fill_column(di, 5, 0x09, 0x12);
    rest = 100 - n;
    if (!rest) return;
    do { fill_column(di, 5, 0x00, 0x12); di++; } while (--rest);
}

void vid_life_bar_max(void)        { bar_red6  (0xCC14, PL_LIFE_MAX); }  /* 0x2227 */
void vid_enemy_bar_max(u16 bx)     { bar_red6  (0xDB14, bx); }           /* 0x2231 */
void vid_life_bar_cur(void)        { bar_white5(0xCC14, PL_LIFE_CUR); }  /* 0x2256 */
void vid_enemy_bar_cur(u16 bx)     { bar_white5(0xDB14, bx); }           /* 0x2260 */

/* ======================================================================== */
/* Narrow-font labels: slots 0x200E / 0x2010 / 0x2038                       */
/* Glyph = 8 rows, top 4 bits of each row; every set bit writes TWO pixels: */
/* [di] = fg, [di+1] = bg (drop shadow). 5-px pitch.                        */
/* ======================================================================== */

/* 0x2345: draw one narrow glyph AL at ES:DI, DI += 5 */
static u16 narrow_glyph(u8 al, u16 di)
{
    const u8 *g = (const u8 *)(FONT_NARROW + (al - 0x20) * 8);
    u16 save = di;
    for (int row = 0; row < 8; row++, di += STRIDE) {
        u8 bits = *g++;
        u16 p = di;
        for (int b = 0; b < 4; b++, p++) {
            bits <<= 1;
            if (carry) { *(u8 *)(ES:p + 1) = bg_colour; *(u8 *)(ES:p) = fg_colour; }
        }
    }
    return save + 5;
}

/* 0x2312: positioned record {x4, y, xoff, len, chars} at DS:SI */
static void draw_label_record(const u8 **si)
{
    u8 x4 = *(*si)++;
    u8 y  = *(*si)++;
    u16 di = x4 * 4 + y * STRIDE;
    u8 xoff = *(*si)++;  di += xoff;
    u8 n = *(*si)++;
    ES = VRAM_SEG;
    do { di = narrow_glyph(*(*si)++, di); } while (--n);
}

void vid_label_hud(const u8 *si)                                    /* 0x22BF */
{
    fg_colour = 0x1B; bg_colour = 0x12;          /* green, red shadow */
    draw_label_record(&si);
}

void vid_label_text(const u8 *si)                                   /* 0x22CD */
{
    fg_colour = 0x09; bg_colour = 0x2D;          /* white, blue shadow */
    draw_label_record(&si);
}

/* slot 0x2038: ASCIIZ at DS:SI, BH x4, BL y, CL x offset px, white/black */
void vid_label_asciiz(const u8 *si, u8 bh_x4, u8 bl_y, u8 cl_xoff)  /* 0x22DB */
{
    fg_colour = 0x09; bg_colour = 0x00;
    u16 di = bh_x4 * 4 + bl_y * STRIDE + cl_xoff;                   /* 0x22E7 */
    ES = VRAM_SEG;
    u8 c;
    while ((c = *si++) != 0) di = narrow_glyph(c, di);
}

/* ======================================================================== */
/* Numbers: slots 0x2014/0x2016/0x2018/0x201A, 0x2030, 0x2032               */
/* ======================================================================== */

/* 0x2480: repeated 24-bit subtract of (CL:BX) from (DL:AX); DH = quotient */
static u8 sub_digit(u8 *dl, u16 *ax, u8 cl, u16 bx)
{
    u8 dh = 0;
    for (;;) {
        if (*dl < cl) { *dl += cl; return dh; }          /* 0x2496 */
        *dl -= cl;
        if (*ax >= bx) { *ax -= bx; dh++; continue; }
        if (*dl == 0) { *ax += bx; *dl += cl; return dh; }/* 0x2494 */
        (*dl)--; dh++;                                    /* borrow from DL */
    }
}

/* 0x2499: AX / BX -> DH = quotient (8 bit), AX = remainder */
static u8 div_digit(u16 *ax, u16 bx)
{
    u8 q = *ax / bx; *ax %= bx; return q;
}

/* slot 0x2032: DL:AX (24-bit) -> 7 decimal digits at CS:DI */
void vid_to_decimal(u8 dl, u16 ax, u8 *di)                          /* 0x243A */
{
    di[0] = sub_digit(&dl, &ax, 0x0F, 0x4240);   /* 1,000,000 */
    di[1] = sub_digit(&dl, &ax, 0x01, 0x86A0);   /*   100,000 */
    di[2] = sub_digit(&dl, &ax, 0x00, 0x2710);   /*    10,000 */
    di[3] = div_digit(&ax, 1000);
    di[4] = div_digit(&ax, 100);
    di[5] = div_digit(&ax, 10);
    di[6] = (u8)ax;
}

/* 0x241B: to decimal at 0x2433, blank (0xFF) up to 6 leading zeros */
static void to_digits_blank_leading(u8 dl, u16 ax)
{
    vid_to_decimal(dl, ax, digits);
    for (int i = 0; i < 6; i++) {
        if (digits[i] != 0) return;
        digits[i] = 0xFF;
    }
}

/* 0x24F2: one 6x7 digit glyph (AL, 0xFF = blank) at ES:DI; optional 0x05 box */
static void digit_glyph(u8 al, u16 di)
{
    if (bg_colour) {                                   /* 6x7 box of 0x0505 */
        u16 p = di;
        for (int r = 0; r < 7; r++, p += STRIDE) {
            u16 *w = (u16 *)(ES:p); w[0] = w[1] = w[2] = 0x0505;
        }
    }
    if (al == 0xFF) return;
    const u8 *g = (const u8 *)(FONT_DIGITS + al * 8);  /* DS = CS */
    for (int r = 0; r < 7; r++, di += 0x13A) {
        u8 bits = *g++ << 2;                           /* skip 2 top bits */
        for (int b = 0; b < 6; b++, di++) {
            bits <<= 1;
            if (carry) *(u8 *)(ES:di) = fg_colour;
        }
    }
}

/* slot 0x2030: DS:DI digits, AH x4, AL y, CL count, CH&1 -> +2 px, BH bg, BL colour */
void vid_draw_digits(const u8 *di, u8 ah_x4, u8 al_y, u8 cl_n, u8 ch_nudge,
                     u8 bh_bg, u8 bl_colour)                        /* 0x24A3 */
{
    bg_colour = bh_bg;
    fg_colour = colour9[bl_colour];
    u16 bx = al_y * STRIDE + ah_x4 * 4 + ((ch_nudge & 1) ? 2 : 0);
    ES = VRAM_SEG;
    do { digit_glyph(*di++, bx); bx += 6; } while (--cl_n);
}

void vid_num_almas(void)                                            /* 0x238F */
{
    to_digits_blank_leading(0, PL_ALMAS);
    vid_draw_digits(digits + 2, 0x26, 0xBB, 5, 1, 0xFF, 1);   /* (152,187) */
}

void vid_num_gold(void)                                             /* 0x23AC */
{
    to_digits_blank_leading(PL_GOLD_HI, PL_GOLD_LO);
    vid_draw_digits(digits + 1, 0x13, 0xBB, 6, 1, 0xFF, 1);   /* (76,187) */
}

void vid_num_item_count(void)                                       /* 0x23CC */
{
    u8 n = PL_ITEM_CNT[PL_ITEM_SEL - 1];
    to_digits_blank_leading(0, n);
    vid_draw_digits(digits + 4, 0x37, 0xBB, 3, 1, 0xFF, 1);   /* (220,187) */
}

void vid_num_magic(void)                                            /* 0x23F5 */
{
    if (!PL_MAGIC_SEL) return;
    to_digits_blank_leading(0, PL_MAGIC_VAL);
    vid_draw_digits(digits + 4, 0x3E, 0xBB, 3, 1, 0xFF, 1);   /* (248,187) */
}

/* ======================================================================== */
/* PC-88 3-plane -> 6-bit packing helpers                                  */
/* ======================================================================== */

/* 0x27B2: emit 4 bytes = 8 source pixels from planeA/B/C (MSB first);
 * each byte = (C B A)(C B A) of two adjacent pixels = left<<3 | right */
static u16 pack6_x8(u16 bp)
{
    for (int i = 0; i < 4; i++, bp++) {
        u8 al = 0;
        for (int px = 0; px < 2; px++) {
            planeC = rol16(planeC, 1); al = (al << 1) | carry;
            planeB = rol16(planeB, 1); al = (al << 1) | carry;
            planeA = rol16(planeA, 1); al = (al << 1) | carry;
        }
        *(u8 *)(ES:bp) = al;
    }
    return bp;
}

/* 0x2748: 32x16 PC-88 picture (12 bytes/row) -> 16x16 at (BH*4+2, BL).
 * Row layout: A = BE words +0,+2; B = LE words +6,+4; C = BE words +8,+A. */
static void icon32x16(const u8 far *si, u8 bh_x4, u8 bl_y)
{
    u16 bp = bh_x4 * 4 + 2 + bl_y * STRIDE;
    ES = VRAM_SEG;
    for (int r = 0; r < 16; r++, si += 12, bp += 0x130) {
        planeA = be16(si + 0); planeB = le16(si + 6); planeC = be16(si + 8);
        bp = pack6_x8(bp); bp = pack6_x8(bp);
        planeA = be16(si + 2); planeB = le16(si + 4); planeC = be16(si + 0xA);
        bp = pack6_x8(bp); bp = pack6_x8(bp);
    }
}

/* slot 0x201C: 40x18 sword picture, itemp section 0, 15 bytes/row x 18 rows */
void vid_icon_sword(u8 al_idx, u8 bh_x8, u8 bl_y)                   /* 0x254C */
{
    DS = ARENA_SEG;
    const u8 far *si = (al_idx - 1) * 0x10E + ITEMP_SEC(0);
    u16 bp = bh_x8 * 8 + bl_y * STRIDE;
    ES = VRAM_SEG;
    for (int r = 0; r < 18; r++, si += 15, bp += 0x12C) {
        planeA = be16(si + 0); planeB = le16(si + 8); planeC = be16(si + 0xA);
        bp = pack6_x8(bp); bp = pack6_x8(bp);
        planeA = be16(si + 2); planeB = le16(si + 6); planeC = be16(si + 0xC);
        bp = pack6_x8(bp); bp = pack6_x8(bp);
        planeA = si[4] << 8;   planeB = si[5] << 8;   planeC = si[0xE] << 8;
        bp = pack6_x8(bp);
    }
}

/* slots 0x201E / 0x2020 / 0x2034 / 0x2036 / 0x203A / 0x203C */
void vid_icon_item(u8 al, u8 bh, u8 bl)                             /* 0x25E2 */
{ DS = ARENA_SEG; icon32x16((al - 1) * 0xC0 + ITEMP_SEC(3), bh, bl); }

void vid_icon_magic(u8 al, u8 bh, u8 bl)                            /* 0x25FC */
{ DS = ARENA_SEG; icon32x16((al - 1) * 0xC0 + ITEMP_SEC(1), bh, bl); }

void vid_icon_sec6(u8 al, u8 bh, u8 bl)                             /* 0x2616 */
{
    if (al == 0) { DS = CS; icon32x16(blank_icon, bh, bl); return; }
    DS = ARENA_SEG; icon32x16((al - 1) * 0xC0 + ITEMP_SEC(6), bh, bl);
}

void vid_icon_sec5(u8 al, u8 bh, u8 bl)                             /* 0x2637 */
{
    if (al == 0) { DS = CS; icon32x16(blank_icon, bh, bl); return; }
    DS = ARENA_SEG; icon32x16((al - 1) * 0xC0 + ITEMP_SEC(5), bh, bl);
}

void vid_icon_sec4(u8 al, u8 bh, u8 bl)                             /* 0x2718 */
{ DS = ARENA_SEG; icon32x16(al * 0xC0 + ITEMP_SEC(4), bh, bl); }     /* 0-based */

void vid_icon_sec2(u8 al, u8 bh, u8 bl)                             /* 0x2730 */
{ DS = ARENA_SEG; icon32x16(al * 0xC0 + ITEMP_SEC(2), bh, bl); }     /* 0-based */

/* ======================================================================== */
/* slot 0x2022 -> 0x27E9  vid_putchar: AL char, AH colour, BX x px, CL y    */
/* 8x8 glyph, 1 px per bit, only set bits written                           */
/* ======================================================================== */
void vid_putchar(u8 al_ch, u8 ah_colour, u16 bx_x, u8 cl_y)         /* 0x27E9 */
{
    DS = CS;
    u8 c = colour9[ah_colour];
    if (FLAG_DEMO_PAL) c = (ah_colour << 4) | ah_colour;   /* 16x16 demo palette */
    fg_colour = c;
    const u8 *si = (const u8 *)(FONT_TEXT + (al_ch - 0x20) * 8);
    bg_colour = (bx_x & 3) * 2;                            /* stored, unused here */
    u16 di = cl_y * STRIDE + bx_x;
    ES = VRAM_SEG;
    for (int r = 0; r < 8; r++, di += 0x138) {
        u8 bits = *si++;
        for (int b = 0; b < 8; b++, di++) {
            bits <<= 1;
            if (carry) *(u8 *)(ES:di) = fg_colour;
        }
    }
}

/* ======================================================================== */
/* slot 0x202A -> 0x291A  vid_puts: DS:SI string, BX x px, CL y             */
/* 0xFF end, 0x0D newline (+8), >=0x80 colour = b&7, else glyph, x += 8     */
/* ======================================================================== */
void vid_puts(const u8 *si, u16 bx, u8 cl)                          /* 0x291A */
{
    str_x = bx; str_y = cl;
    str_colour = FLAG_DEMO_PAL ? 7 : 1;
    for (;;) {
        u8 c = *si++;
        if (c == 0xFF) return;
        if (c == 0x0D) { str_y += 8; cl = str_y; bx = str_x; continue; }
        if (c & 0x80)  { str_colour = c & 7; continue; }
        vid_putchar(c, str_colour, bx, cl);
        bx += 8;
    }
}

/* ======================================================================== */
/* Rect moves: slots 0x2024, 0x2026, 0x2028, 0x202C                         */
/* ======================================================================== */

/* slot 0x2024: BH x8, BL y, CH w8, CL rows — shift rect up by one row */
void vid_scroll_up_1(u8 bh_x8, u8 bl_y, u8 ch_w8, u8 cl_rows)       /* 0x2857 */
{
    u16 di = bh_x8 * 8 + bl_y * STRIDE, si = di + STRIDE;
    ES = DS = VRAM_SEG;
    do { memmove(ES:di, DS:si, ch_w8 * 8); di += STRIDE; si += STRIDE; }
    while (--cl_rows);
}

/* slot 0x2026: AH x8, AL y (BH must be 0: row = BH:AL), CH w8, CL rows,
 * DI = destination offset in STAGING */
void vid_save_rect(u8 ah_x8, u8 al_y, u8 ch_w8, u8 cl_rows, u16 di) /* 0x289A */
{
    u16 si = al_y * STRIDE + ah_x8 * 8;
    ES = STAGING_SEG; DS = VRAM_SEG;
    do { memcpy(ES:di, DS:si, ch_w8 * 8); di += ch_w8 * 8; si += STRIDE; }
    while (--cl_rows);
}

/* slot 0x2028: inverse of vid_save_rect (DI = source offset in STAGING) */
void vid_restore_rect(u8 ah_x8, u8 al_y, u8 ch_w8, u8 cl_rows, u16 si) /* 0x28D9 */
{
    u16 di = al_y * STRIDE + ah_x8 * 8;
    DS = STAGING_SEG; ES = VRAM_SEG;
    do { memcpy(ES:di, DS:si, ch_w8 * 8); si += ch_w8 * 8; di += STRIDE; }
    while (--cl_rows);
}

/* slot 0x202C: DH sx8, DL sy, BH dx8, BL dy, CH w8, CL rows (forward copy) */
void vid_copy_rect(u8 dh_sx8, u8 dl_sy, u8 bh_dx8, u8 bl_dy, u8 ch_w8, u8 cl_rows) /* 0x296F */
{
    u16 si = dl_sy * STRIDE + dh_sx8 * 8;
    u16 di = bl_dy * STRIDE + bh_dx8 * 8;
    ES = DS = VRAM_SEG;
    do { memmove(ES:di, DS:si, ch_w8 * 8); di += STRIDE; si += STRIDE; }
    while (--cl_rows);
}

/* ======================================================================== */
/* slot 0x202E -> 0x29C3  vid_cursor_frame: AL colour, BH x4, BL y          */
/* hollow 20x20 box, 2 px thick                                             */
/* ======================================================================== */

/* 0x2A06: two full 20-px rows of fg_colour, DI += 2 rows */
static u16 frame_two_rows(u16 di)
{
    for (int i = 0; i < 2; i++, di += STRIDE) memset(ES:di, fg_colour, 20);
    return di;
}

void vid_cursor_frame(u8 al_colour, u8 bh_x4, u8 bl_y)              /* 0x29C3 */
{
    fg_colour = colour9[al_colour];
    u16 di = bh_x4 * 4 + bl_y * STRIDE;
    ES = VRAM_SEG;
    di = frame_two_rows(di);
    for (int r = 0; r < 16; r++, di += STRIDE) {
        u8 *p = (u8 *)(ES:di);
        p[0] = p[1] = p[0x12] = p[0x13] = fg_colour;
    }
    frame_two_rows(di);                    /* falls through into 0x2A06 */
}

/* ======================================================================== */
/* slot 0x203E -> 0x2A1C  vid_tear_icon: AL 0/1, BH x4, BL y                */
/* built-in 16x13 pictures at 0x2A61 (orb) / 0x2B31 (face); 0x80 = skip     */
/* ======================================================================== */
void vid_tear_icon(u8 al_idx, u8 bh_x4, u8 bl_y)                    /* 0x2A1C */
{
    DS = CS;
    const u8 *si = tear_icons[al_idx];
    u16 di = bh_x4 * 4 + bl_y * STRIDE;
    ES = VRAM_SEG;
    for (int r = 0; r < 13; r++, di += 0x130) {
        for (int x = 0; x < 16; x++, di++) {
            u8 v = *si++;
            if (v != 0x80) *(u8 *)(ES:di) = v;
        }
    }
}

/* ======================================================================== */
/* slot 0x2042 -> 0x2C01  vid_clear_screen (8 x 25 interleaved row groups)  */
/* ======================================================================== */
void vid_clear_screen(void)                                         /* 0x2C01 */
{
    ES = VRAM_SEG;
    u16 di = 0;
    for (int i = 0; i < 8; i++, di += STRIDE) {
        u16 p = di;
        for (int j = 0; j < 25; j++, p += 8 * STRIDE) memset(ES:p, 0, 320);
    }
}

/* ======================================================================== */
/* slot 0x2044 -> 0x2C2A  vid_convert_cells: DS:SI bank, CX cell count      */
/* Copies CX*48 bytes to STAGING:0, then rewrites the bank in place:        */
/* each 16x8 3-plane cell -> 8x8 six-bit cell (still 48 bytes, stride 0x30) */
/* ======================================================================== */

/* 0x2CA7: shift one pixel's (C,B,A) bits into AX */
static u16 pack_pair_bits(u16 ax)
{
    planeC = rol16(planeC, 1); ax = (ax << 1) | carry;
    planeB = rol16(planeB, 1); ax = (ax << 1) | carry;
    planeA = rol16(planeA, 1); ax = (ax << 1) | carry;
    return ax;
}

/* 0x2C75: one row -> 6 bytes, as two groups of (stosw, stosb).
 * Bit stream is p0..p7 (6 bits each, MSB first) but the first 16 bits go out
 * with STOSW (little-endian), so in memory: b0 = bits 8..15, b1 = bits 0..7,
 * b2 = bits 16..23. gfmcga @412F undoes this. */
static u8 far *pack_row(u8 far *di)
{
    u16 ax;
    for (int half = 0; half < 2; half++) {
        ax = pack_pair_bits(ax);            /* p0 */
        ax = pack_pair_bits(ax);            /* p1 */
        ax = pack_pair_bits(ax);            /* p2 */
        ax = pack_pair_bits(ax);            /* p3 */
        ax = pack_pair_bits(ax);            /* p4  -> 15 bits */
        planeC = rol16(planeC, 1); ax = (ax << 1) | carry;   /* C5 -> 16 bits */
        *(u16 far *)di = ax; di += 2;                         /* stosw (LE) */
        planeB = rol16(planeB, 1); ax = (ax << 1) | carry;   /* B5 */
        planeA = rol16(planeA, 1); ax = (ax << 1) | carry;   /* A5 */
        ax = pack_pair_bits(ax);            /* p6 */
        ax = pack_pair_bits(ax);            /* p7  -> low 8 bits */
        *di++ = (u8)ax;                                       /* stosb */
    }
    return di;
}

/* 0x2C55: one cell = 8 rows of three big-endian plane words A, B, C */
static u8 far *pack_cell(const u8 far **si, u8 far *di)
{
    for (int r = 0; r < 8; r++) {
        planeA = be16(*si); *si += 2;
        planeB = be16(*si); *si += 2;
        planeC = be16(*si); *si += 2;
        di = pack_row(di);
    }
    return di;
}

void vid_convert_cells(const u8 far *ds_si, u16 cx)                 /* 0x2C2A */
{
    u8 far *dst = ds_si;                            /* rewrite in place */
    memcpy(STAGING_SEG:0, ds_si, cx * 0x30);        /* 0x2C3E rep movsb */
    const u8 far *src = STAGING_SEG:0;
    do { dst = pack_cell(&src, dst); } while (--cx);
}

/* ======================================================================== */
/* Vector table (0x2000..0x2044) for reference                              */
/* ======================================================================== */
#if 0
0x2000 vid_window            0x2046
0x2002 vid_clear_playfield   0x2106
0x2004 vid_gauge_bar         0x2195
0x2006 vid_life_bar_max      0x2227
0x2008 vid_life_bar_cur      0x2256
0x200A vid_enemy_bar_max     0x2231
0x200C vid_enemy_bar_cur     0x2260
0x200E vid_label_hud         0x22BF
0x2010 vid_label_text        0x22CD
0x2012 vid_enemy_gauge_trough 0x2385
0x2014 vid_num_almas         0x238F
0x2016 vid_num_gold          0x23AC
0x2018 vid_num_item_count    0x23CC
0x201A vid_num_magic         0x23F5
0x201C vid_icon_sword        0x254C
0x201E vid_icon_item         0x25E2
0x2020 vid_icon_magic        0x25FC
0x2022 vid_putchar           0x27E9
0x2024 vid_scroll_up_1       0x2857
0x2026 vid_save_rect         0x289A
0x2028 vid_restore_rect      0x28D9
0x202A vid_puts              0x291A
0x202C vid_copy_rect         0x296F
0x202E vid_cursor_frame      0x29C3
0x2030 vid_draw_digits       0x24A3
0x2032 vid_to_decimal        0x243A
0x2034 vid_icon_sec6         0x2616
0x2036 vid_icon_sec5         0x2637
0x2038 vid_label_asciiz      0x22DB
0x203A vid_icon_sec4         0x2718
0x203C vid_icon_sec2         0x2730
0x203E vid_tear_icon         0x2A1C
0x2040 vid_dissolve_playfield 0x2130
0x2042 vid_clear_screen      0x2C01
0x2044 vid_convert_cells     0x2C2A
#endif
