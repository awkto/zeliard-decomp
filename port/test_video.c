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

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../zeliard";
    t_modes();   printf("ok\n");
    t_mcga();    printf("ok\n");
    t_cga();     printf("ok\n");
    t_ega();     printf("ok\n");
    t_hgc();     printf("ok\n");
    t_tandy();   printf("ok\n");
    t_tables(dir); printf("ok\n");
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
