/* test_video.c — the five video drivers as output stages (issue #32,
 * port/video.h, docs/VIDEO_DRIVERS.md).
 *
 * Two halves.  The first needs nothing but the port: the geometry of each
 * mode, the Hercules row map, and the pair-buffer -> screen conversions on a
 * synthetic framebuffer.  The second re-reads the colour tables out of
 * zeliard/GM{CGA,HGC,TGA}.BIN and zeliard/GAME.BIN and checks that the ones
 * compiled into port/video_*.c are byte-for-byte what the drivers carry — the
 * tables are the only thing in those files that could silently drift from the
 * originals.
 *
 * usage: test_video [GAMEDIR]                                              */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "video.h"
#include "sar.h"
#include "render.h"
#include "gfx.h"

static int checks, failures;
static void ck(int ok, const char *fmt, ...)
{
    va_list ap; checks++;
    if (ok) return;
    failures++;
    printf("  FAIL ");
    va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    printf("\n");
}

static uint8_t fb[FB_W * FB_H];
static uint8_t rgb[VIDEO_MAX_W * VIDEO_MAX_H * 3];

static const uint8_t *px(int w, int x, int y) { return rgb + ((size_t)y * w + x) * 3; }
static int is(const uint8_t *p, int r, int g, int b) { return p[0] == r && p[1] == g && p[2] == b; }

/* ---------------------------------------------------------- the mode table */
static void t_modes(void)
{
    printf("modes           ");
    ck(video_mode_by_name("mcga") == VID_MCGA && video_mode_by_name("MCGA") == VID_MCGA,
       "--video mcga (either case)");
    ck(video_mode_by_name("cga") == VID_CGA && video_mode_by_name("cga2") == VID_CGA2,
       "cga and cga2 are separate modes of the one driver file");
    ck(video_mode_by_name("ega") == VID_EGA && video_mode_by_name("hgc") == VID_HGC,
       "ega, hgc");
    ck(video_mode_by_name("tandy") == VID_TANDY && video_mode_by_name("tga") == VID_TANDY,
       "tandy, and the RESOURCE.CFG spelling TGA");
    ck(video_mode_by_name("vga") < 0 && video_mode_by_name(NULL) < 0, "an unknown name is rejected");
    ck(!strcmp(video_mode_cfg(VID_HGC), "HGC") && !strcmp(video_mode_cfg(VID_TANDY), "TGA"),
       "the RESOURCE.CFG videoDrv spellings");

    /* docs/VIDEO_DRIVERS.md §1: the mode each driver's INT 10h call sets */
    struct { int m, w, h; const char *what; } geo[] = {
        { VID_MCGA,  320, 200, "mode 13h, 320x200x256" },
        { VID_CGA,   320, 200, "mode 5, 320x200x4" },
        { VID_CGA2,  640, 200, "mode 6, 640x200x2" },
        { VID_EGA,   640, 200, "mode Eh, 640x200x16" },
        { VID_HGC,   720, 348, "Hercules, 720x348 mono" },
        { VID_TANDY, 320, 200, "mode 9, Tandy 320x200x16" },
    };
    for (unsigned i = 0; i < sizeof geo / sizeof *geo; i++) {
        int w = 0, h = 0;
        video_size(geo[i].m, &w, &h);
        ck(w == geo[i].w && h == geo[i].h, "%s is %dx%d (got %dx%d)",
           geo[i].what, geo[i].w, geo[i].h, w, h);
    }
}

/* -------------------------------------------------- the conversions proper */
/* A pair index is l*8+r.  Fill the framebuffer with every one of the 64 and
 * check what each driver makes of it. */
static void fill_pairs(void)
{
    for (int y = 0; y < FB_H; y++)
        for (int x = 0; x < FB_W; x++)
            fb[y * FB_W + x] = (uint8_t)((y * FB_W + x) & 63);
}

static void t_mcga(void)
{
    printf("mcga            ");
    memset(fb, 0, sizeof fb);
    fb[0] = 0x09;                       /* (1,1) = plain white */
    fb[1] = 0x12;                       /* (2,2) = plain red   */
    fb[2] = 0x05;                       /* (0,5) = black + blue, the HUD digit box */
    video_to_rgb(VID_MCGA, fb, rgb);
    ck(is(px(320, 0, 0), PAL_RGB[9][0], PAL_RGB[9][1], PAL_RGB[9][2]),
       "MCGA is the identity: the byte in A000 indexes the A41B blend DAC");
    ck(is(px(320, 3, 0), 0, 0, 0), "pair 0 is black");
    ck(!is(px(320, 2, 0), 0, 0, 0), "pair 05 (blk+blu) is not black");
}

static void t_cga(void)
{
    printf("cga             ");
    /* the four mode-5 burst-off colours, from the four table entries that
     * produce them: (blk,blk)=0, (cyn,cyn)=1, (red,red)=2, (wht,wht)=3 */
    memset(fb, 0, sizeof fb);
    fb[0] = 0 * 8 + 0; fb[1] = 4 * 8 + 4; fb[2] = 2 * 8 + 2; fb[3] = 1 * 8 + 1;
    video_to_rgb(VID_CGA, fb, rgb);
    ck(is(px(320, 0, 0), 0x00, 0x00, 0x00), "CGA 0 is black");
    ck(is(px(320, 1, 0), 0x55, 0xFF, 0xFF), "CGA 1 is light cyan (burst off, intensity on)");
    ck(is(px(320, 2, 0), 0xFF, 0x55, 0x55), "CGA 2 is light red");
    ck(is(px(320, 3, 0), 0xFF, 0xFF, 0xFF), "CGA 3 is white");
    ck(VID_CGA_PAIR[0 * 8 + 5] == 0 && VID_CGA_PAIR[5 * 8 + 0] == 0,
       "blue blends to black on CGA (@290B row/column blu)");
    ck(VID_CGA_PAIR[1 * 8 + 1] == 3 && VID_CGA_PAIR[3 * 8 + 3] == 3,
       "white and green both come out white");

    /* mode 6: the same 2-bit value as two mono pixels, 640 wide */
    fill_pairs();
    video_to_rgb(VID_CGA2, fb, rgb);
    int bad = 0;
    for (int x = 0; x < FB_W; x++) {
        unsigned v = VID_CGA_PAIR[fb[x] & 63];
        if (!is(px(640, x * 2, 0), (v >> 1) & 1 ? 255 : 0, (v >> 1) & 1 ? 255 : 0, (v >> 1) & 1 ? 255 : 0)) bad++;
        if (!is(px(640, x * 2 + 1, 0), v & 1 ? 255 : 0, v & 1 ? 255 : 0, v & 1 ? 255 : 0)) bad++;
    }
    ck(bad == 0, "cga2 paints each 2-bit value as its own two mono pixels (%d wrong)", bad);
}

static void t_ega(void)
{
    printf("ega             ");
    /* [0x2044] is a `ret`, so the two halves of the pair stay two pixels */
    memset(fb, 0, sizeof fb);
    fb[0] = 2 * 8 + 3;                  /* left red, right green */
    video_to_rgb(VID_EGA, fb, rgb);
    ck(is(px(640, 0, 0), 0xAA, 0, 0), "EGA keeps the pair's left half: red");
    ck(is(px(640, 1, 0), 0x55, 0xFF, 0x55), "and its right half: green");
    /* the A409 block, decoded */
    struct { int c, r, g, b; const char *n; } want[] = {
        { 0, 0x00, 0x00, 0x00, "black (0)" },     { 1, 0xFF, 0xFF, 0xFF, "white (15)" },
        { 2, 0xAA, 0x00, 0x00, "red (4)" },       { 3, 0x55, 0xFF, 0x55, "light green (10)" },
        { 4, 0x55, 0xFF, 0xFF, "light cyan (11)" }, { 5, 0x00, 0x00, 0xAA, "blue (1)" },
        { 6, 0xFF, 0xFF, 0x55, "yellow (14)" },   { 7, 0xAA, 0x00, 0xAA, "magenta (5)" },
    };
    memset(fb, 0, sizeof fb);
    for (int c = 0; c < 8; c++) fb[c] = (uint8_t)(c * 9);
    video_to_rgb(VID_EGA, fb, rgb);
    for (unsigned i = 0; i < sizeof want / sizeof *want; i++)
        ck(is(px(640, want[i].c * 2, 0), want[i].r, want[i].g, want[i].b),
           "PC-88 colour %d is %s (GAME.BIN A409 palette register %02X)",
           want[i].c, want[i].n, VID_EGA_PALETTE[want[i].c]);
    ck(VID_EGA_PALETTE[0] == 0x00 && VID_EGA_PALETTE[1] == 0x3F && VID_EGA_PALETTE[7] == 0x2D,
       "the block is in PC-88 order, not IBM order");
}

static void t_hgc(void)
{
    printf("hgc             ");
    /* @2E11: line = 4*((y+28)/3) + (y+28)%3 */
    ck(video_hgc_line(0) == 37, "game row 0 lands on Hercules line 37 (got %d)", video_hgc_line(0));
    ck(video_hgc_line(1) == 38 && video_hgc_line(2) == 40 && video_hgc_line(3) == 41,
       "rows 1,2,3 -> lines 38,40,41");
    ck(video_hgc_line(199) == 302, "row 199 -> line 302 (got %d)", video_hgc_line(199));
    ck(video_hgc_line(199) - video_hgc_line(0) + 2 == 267,
       "200 game rows cover 267 Hercules lines");
    int b3 = 0;
    for (int y = 0; y < FB_H; y++) if (video_hgc_line(y) % 4 == 3) b3++;
    ck(b3 == 0, "the row helper never addresses bank 3 itself (%d rows did)", b3);

    memset(fb, 0, sizeof fb);
    for (int y = 0; y < FB_H; y++) fb[y * FB_W] = 1 * 8 + 1;     /* white: 2 bits = 11 */
    video_to_rgb(VID_HGC, fb, rgb);
    ck(is(px(720, 40, 37), 0xAA, 0xAA, 0xAA) && is(px(720, 41, 37), 0xAA, 0xAA, 0xAA),
       "a white pixel lights both mono pixels at the 40 px left margin");
    ck(is(px(720, 39, 37), 0, 0, 0), "and nothing inside the margin");
    ck(is(px(720, 40, 36), 0, 0, 0), "line 36 is above the game area");
    /* the bank-3 repeat: row 1 (line 38, b = 2) is written again on line 39 */
    ck(is(px(720, 40, 39), 0xAA, 0xAA, 0xAA) && !memcmp(px(720, 0, 38), px(720, 0, 39), 720 * 3),
       "line 39 repeats the b = 2 row above it (the bank-3 wrap)");
    ck(is(px(720, 40, 303), 0xAA, 0xAA, 0xAA), "the last repeat is line 303");
    ck(is(px(720, 40, 304), 0, 0, 0), "and nothing below it");
}

static void t_tandy(void)
{
    printf("tandy           ");
    memset(fb, 0, sizeof fb);
    fb[0] = 1 * 8 + 1; fb[1] = 3 * 8 + 3; fb[2] = 0;
    video_to_rgb(VID_TANDY, fb, rgb);
    ck(is(px(320, 0, 0), 0xFF, 0xFF, 0xFF), "white+white is colour F");
    ck(is(px(320, 1, 0), 0x55, 0xFF, 0x55), "green+green is colour A (light green)");
    ck(is(px(320, 2, 0), 0x00, 0x00, 0x00), "black+black is colour 0");
    ck(VID_TGA_PAIR[1 * 8 + 1] == 0xF && VID_TGA_PAIR[3 * 8 + 3] == 0xA
       && VID_TGA_PAIR[0] == 0x0, "the @2999 blend approximation on the fixed 16 colours");
}

/* ------------------------------- the tables, against the drivers themselves */
/* The drivers are loose files next to the .SAR archives, and a copy of the game
 * may hold them upper- or lower-cased; try both. */
static uint8_t *slurp(const char *dir, const char *name, size_t *len)
{
    char path[512], lower[64];
    size_t i = 0;
    for (; name[i] && i < sizeof lower - 1; i++)
        lower[i] = (char)(name[i] >= 'A' && name[i] <= 'Z' ? name[i] + 32 : name[i]);
    lower[i] = 0;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(path, sizeof path, "%s/%s", dir, lower);
        f = fopen(path, "rb");
    }
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = n > 0 ? malloc((size_t)n) : NULL;
    if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
    fclose(f);
    *len = (size_t)n;
    return b;
}

static void t_tables(const char *dir)
{
    printf("driver tables   ");
    size_t n = 0;
    /* every driver is loaded raw at BASE:2000, so file offset = addr - 0x2000 */
    struct { const char *file; unsigned addr; const uint8_t *have; const char *what; } t[] = {
        { "GMCGA.BIN", 0x290B, VID_CGA_PAIR, "GMCGA.BIN @290B pair -> 2 bpp" },
        { "GMHGC.BIN", 0x2994, VID_CGA_PAIR, "GMHGC.BIN @2994 (byte-identical to CGA's)" },
        { "GMTGA.BIN", 0x2999, VID_TGA_PAIR, "GMTGA.BIN @2999 pair -> 4 bpp" },
    };
    int any = 0;
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) {
        uint8_t *b = slurp(dir, t[i].file, &n);
        if (!b) continue;
        any = 1;
        size_t off = t[i].addr - 0x2000;
        ck(off + 64 <= n && !memcmp(b + off, t[i].have, 64), "%s", t[i].what);
        free(b);
    }
    /* GAME.BIN is loaded at BASE:A000 (docs/ARCHITECTURE.md) */
    uint8_t *g = slurp(dir, "GAME.BIN", &n);
    if (g) {
        any = 1;
        size_t off = 0xA409 - 0xA000;
        ck(off + 17 <= n && !memcmp(g + off, VID_EGA_PALETTE, 17),
           "GAME.BIN @A409, the INT 10h AX=1002h palette block");
        free(g);
    }
    if (!any) printf("(GM*.BIN not available in %s: skipping the table check) ", dir);
}


/* The town's own tables (issue #34): gt*.bin's 64-entry tile/sprite pair tables
 * and the 16-entry tables inside ympd.bin / ckpd.bin, read back out of the
 * overlays themselves.  gt*.bin is ZELRES1[7..11] loaded at 3000; ympd/ckpd are
 * ZELRES2[8..9] loaded at 3300. */
static void t_town_tables(const char *dir)
{
    printf("town tables     ");
    struct { int arch, idx; unsigned org, addr; const uint8_t *have; size_t n; const char *what; } t[] = {
        { 0, 8,  0x3000, 0x3D0B, VID_GT_CGA, 64, "gtcga.bin @3D0B tile/sprite pair table" },
        { 0, 9,  0x3000, 0x3C24, VID_GT_CGA, 64, "gthgc.bin @3C24 (byte-identical to gtcga's)" },
        { 0, 10, 0x3000, 0x3DCB, VID_GT_TGA, 64, "gttga.bin @3DCB tile/sprite pair table" },
        { 1, 8,  0x3300, 0x3432, VID_YM_MTN_C, 16, "ympd.bin @3432 mountains, CGA/HGC" },
        { 1, 8,  0x3300, 0x357D, VID_YM_MTN_T, 16, "ympd.bin @357D mountains, Tandy" },
        { 1, 8,  0x3300, 0x36B6, VID_YM_GND_C, 16, "ympd.bin @36B6 ground strips, CGA/HGC" },
        { 1, 8,  0x3300, 0x38C7, VID_YM_GND_T0, 16, "ympd.bin @38C7 first ground strip, Tandy" },
        { 1, 8,  0x3300, 0x38D7, VID_YM_GND_T1, 16, "ympd.bin @38D7 second ground strip, Tandy" },
        { 1, 9,  0x3300, 0x3753, VID_CK_MTN_C, 16, "ckpd.bin @3753 ceiling, CGA/HGC" },
        { 1, 9,  0x3300, 0x38D0, VID_CK_MTN_T, 16, "ckpd.bin @38D0 ceiling, Tandy" },
        { 1, 9,  0x3300, 0x3497, VID_CK_GND_C, 16, "ckpd.bin @3497 ground strips, CGA/HGC" },
        { 1, 9,  0x3300, 0x3654, VID_CK_GND_T, 16, "ckpd.bin @3654 ground strips, Tandy" },
    };
    int any = 0;
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) {
        size_t len = 0;
        uint8_t *b = sar_load(dir, t[i].arch, t[i].idx, 1, &len);
        if (!b) continue;
        any = 1;
        size_t off = t[i].addr - t[i].org;
        ck(off + t[i].n <= len && !memcmp(b + off, t[i].have, t[i].n), "%s", t[i].what);
        free(b);
    }
    if (!any) printf("(ZELRES not available in %s: skipping the table check) ", dir);

    /* what a tagged pixel becomes: gtcga's table, not GMCGA's, for a tile pixel,
     * and the painter's own table for a backdrop pixel */
    uint8_t o = 0;
    ck(VID_CGA_PAIR[0 * 8 + 2] == 2 && VID_GT_CGA[0 * 8 + 2] == 0, "blk+red is red in GMCGA's table but black in gtcga's");
    ck(video_town_pixel(VID_CGA, 100, FB_GT | 2, &o) && o == 0, "a tagged town tile pixel (blk+red) reads gtcga's table: black");
    ck(!video_town_pixel(VID_CGA, 100, 2, &o), "an untagged one is left to the driver's own table");
    ck(!video_town_pixel(VID_MCGA, 100, FB_GT | 2, &o) && !video_town_pixel(VID_EGA, 100, FB_BD_YM | 2, &o),
       "MCGA and EGA never recolour (the tag is masked away)");
    /* ympd mountains: pair (p2a p1a p2b p1b) = bits 5 3 2 0, the CGA table looks at the right half only */
    ck(video_town_pixel(VID_CGA, 50, FB_BD_YM | (1 << 5) | 1, &o) && o == VID_YM_MTN_C[9], "ympd mountain: index 1001");
    ck(video_town_pixel(VID_TANDY, 50, FB_BD_YM | (1 << 3) | (1 << 2), &o) && o == VID_YM_MTN_T[6], "ympd mountain (Tandy): index 0110");
    /* the two ground strips pack the same four bits differently and take different Tandy tables */
    ck(video_town_pixel(VID_TANDY, 142, FB_BD_YM | (1 << 5) | (1 << 1), &o) && o == VID_YM_GND_T0[4 | 2], "ympd first ground strip: bits 5 and 1 are w1a and w2b");
    ck(video_town_pixel(VID_TANDY, 150, FB_BD_YM | (1 << 4) | 1, &o) && o == VID_YM_GND_T1[8 | 1], "ympd second ground strip: bits 4 and 0 are w2a and w1b");
    ck(video_town_pixel(VID_CGA, 150, FB_BD_CK | (1 << 4) | (1 << 1), &o) && o == VID_CK_GND_C[4 | 1], "ckpd ground strips: bits 5 4 2 1");
}

/* The drivers all drew for a 4:3 monitor, so the presentation shape is not the
 * framebuffer shape (issue #43).  This is the only place the distinction is
 * asserted -- `make verify` deliberately never goes through it. */
static void t_display_aspect(void)
{
    printf("display aspect  ");
    static const struct { int mode; int fw, fh, dw, dh; } CASES[] = {
        {VID_MCGA,  320, 200, 320, 240},
        {VID_CGA,   320, 200, 320, 240},
        {VID_TANDY, 320, 200, 320, 240},
        {VID_CGA2,  640, 200, 640, 480},
        {VID_EGA,   640, 200, 640, 480},
        {VID_HGC,   720, 348, 720, 540},
    };
    for (unsigned i = 0; i < sizeof CASES / sizeof CASES[0]; i++) {
        int fw = 0, fh = 0, dw = 0, dh = 0;
        video_size(CASES[i].mode, &fw, &fh);
        video_display_size(CASES[i].mode, &dw, &dh);
        ck(fw == CASES[i].fw && fh == CASES[i].fh, "%s framebuffer %dx%d", video_mode_name(CASES[i].mode), fw, fh);
        ck(dw == CASES[i].dw && dh == CASES[i].dh, "%s presents at %dx%d (want %dx%d, 4:3)",
              video_mode_name(CASES[i].mode), dw, dh, CASES[i].dw, CASES[i].dh);
        ck(dw * 3 == dh * 4, "%s presentation is 4:3", video_mode_name(CASES[i].mode));
    }
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../zeliard";
    t_modes();   printf("ok\n");
    t_mcga();    printf("ok\n");
    t_cga();     printf("ok\n");
    t_ega();     printf("ok\n");
    t_hgc();     printf("ok\n");
    t_tandy();   printf("ok\n");
    t_display_aspect(); printf("ok\n");
    t_tables(dir); printf("ok\n");
    t_town_tables(dir); printf("ok\n");
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
