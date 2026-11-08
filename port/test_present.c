/* test_present.c — the display filters (issue #53, port/present.h).
 *
 * The filters only exist on the SDL path, so this is built and run separately
 * from `make test` (which stays headless and SDL-free).  It drives the SDL
 * "offscreen" (or "dummy") video driver and a software renderer, blits one
 * frame through each filter into a deliberately non-integer window (1000x700
 * for a 320x240 display), and checks the letterbox geometry, that integer
 * scaling really produces uniform n x n blocks, and that sharp-bilinear and
 * smooth disagree.  With no SDL, or no working video driver, it prints that it
 * is skipping and passes.
 *
 * usage: test_present                                                        */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "present.h"
static float fabsf_(float v) { return v < 0 ? -v : v; }

static int checks, failures;
static void ck(int ok, const char *fmt, ...)
{
    va_list ap;
    checks++;
    if (ok) return;
    failures++;
    printf("  FAIL ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

/* ------------------------------------------------------ the CRT mask maths */
static void t_crt(void)
{
    printf("crt mask        ");
    /* 200 source lines into 800 rows: 4 rows per line, centre bright, seam dark */
    int g[4];
    for (int i = 0; i < 4; i++) g[i] = present_crt_row_gain(i, 800, 200);
    ck(g[2] == 255, "line centre (row 2 of 4) is full brightness (got %d)", g[2]);
    ck(g[0] == 255 - PRESENT_CRT_SCAN_DARK, "the seam row is darkest (got %d)", g[0]);
    ck(g[1] > g[0] && g[1] < g[2] && g[3] > g[0] && g[3] < g[2], "the profile rises to the centre and falls");
    int periodic = 1, inrange = 1;
    for (int y = 0; y < 800; y++) {
        int v = present_crt_row_gain(y, 800, 200);
        if (v != present_crt_row_gain(y % 4, 800, 200)) periodic = 0;
        if (v < 255 - PRESENT_CRT_SCAN_DARK || v > 255) inrange = 0;
    }
    ck(periodic, "the pattern repeats every source line");
    ck(inrange, "every row gain is within [255-dark, 255]");
    ck(present_crt_row_gain(0, 500, 200) == 255 && present_crt_row_gain(1, 500, 200) == 255,
       "under 3 rows per source line there is no scanline gap");
    ck(present_crt_row_gain(5, 0, 200) == 255 && present_crt_row_gain(5, 800, 0) == 255, "degenerate sizes are flat");
    int own = 0, other = 0;
    for (int x = 0; x < 9; x++)
        for (int c = 0; c < 3; c++) {
            int v = present_crt_col_gain(x, c, 960, 320);
            if (x % 3 == c) own += (v == 255); else other += (v == 255 - PRESENT_CRT_MASK_DARK);
        }
    ck(own == 9 && other == 18, "the aperture grille lights one channel per column in R,G,B order");
    ck(present_crt_col_gain(1, 0, 640, 320) == 255, "under 3 columns per source pixel the grille is off");
    uint32_t px = present_crt_mask(0, 2, 960, 800, 320, 200);   /* centre row, R stripe */
    ck(px == 0xFFFFD7D7u, "mask pixel (0,2): R full, G and B dimmed by the grille (got %08X)", (unsigned)px);
    ck((present_crt_mask(7, 0, 960, 800, 320, 200) >> 24) == 0xFF, "the mask is opaque");
}

/* ------------------------------------------------------------- the names */
static void t_names(void)
{
    printf("filter names    ");
    ck(present_filter_by_name("integer") == FILTER_INTEGER, "integer");
    ck(present_filter_by_name("sharp") == FILTER_SHARP, "sharp");
    ck(present_filter_by_name("sharp-bilinear") == FILTER_SHARP, "sharp-bilinear");
    ck(present_filter_by_name("smooth") == FILTER_SMOOTH, "smooth");
    ck(present_filter_by_name("crt") == FILTER_CRT, "crt");
    ck(!strcmp(present_filter_name(FILTER_CRT), "crt"), "crt name");
    ck(present_filter_by_name(present_filter_name(FILTER_CRT)) == FILTER_CRT, "crt round-trips");
    for (int k = 0; k < FILTER_COUNT; k++)
        ck(present_filter_by_name(present_filter_name(k)) == k, "filter %d round-trips by name", k);
    ck(present_filter_by_name("nonsense") < 0, "an unknown name is rejected");
    ck(present_filter_by_name(NULL) < 0, "NULL is rejected");
    ck(!strcmp(present_filter_name(FILTER_INTEGER), "integer"), "integer name");
    ck(!strcmp(present_filter_name(FILTER_SHARP), "sharp-bilinear"), "sharp name");
    ck(!strcmp(present_filter_name(FILTER_SMOOTH), "smooth"), "smooth name");
    /* the six-plus set, in cycle order (F3 / V walk it) */
    static const char *const order[] = { "integer", "sharp-bilinear", "smooth", "scanlines",
                                         "soft-scanlines", "crt", "crt-curved" };
    ck(FILTER_COUNT == 7, "seven filters (got %d)", FILTER_COUNT);
    for (int k = 0; k < 7; k++) {
        ck(!strcmp(present_filter_name(k), order[k]), "cycle slot %d is %s", k, order[k]);
        ck(present_filter_by_name(order[k]) == k, "%s parses to slot %d", order[k], k);
    }
    ck(present_filter_by_name("scanlines") == FILTER_SCANLINES, "scanlines");
    ck(present_filter_by_name("soft-scanlines") == FILTER_SOFT_SCANLINES, "soft-scanlines");
    ck(present_filter_by_name("crt-curved") == FILTER_CRT_CURVED, "crt-curved");
    ck(present_filter_by_name("curved") == FILTER_CRT_CURVED, "curved alias");
    ck(present_filter_by_name("soft") == FILTER_SMOOTH, "soft alias");
    ck(present_filter_by_name("CRT-Curved") == FILTER_CRT_CURVED, "names are case-insensitive");
    ck(present_filter_by_name("crt") == FILTER_CRT, "crt is still plain crt");
    {   int k = FILTER_INTEGER, seen = 0;
        for (int s = 0; s < FILTER_COUNT; s++) { seen |= 1 << k; k = (k + 1) % FILTER_COUNT; }
        ck(seen == (1 << FILTER_COUNT) - 1 && k == FILTER_INTEGER, "the (k+1)%%COUNT cycle visits every filter and wraps");
    }
}

/* ------------------------------------------------- the curve and the vignette */
static void t_curve(void)
{
    printf("filter curve    ");
    float x, y;
    present_curve_map(0.5f, 0.5f, &x, &y);
    ck(fabsf_(x - 0.5f) < 1e-5f && fabsf_(y - 0.5f) < 1e-5f, "the centre stays put");
    present_curve_map(1.0f, 0.5f, &x, &y);
    ck(fabsf_(x - 1.0f) < 1e-5f && fabsf_(y - 0.5f) < 1e-5f, "the right edge midpoint stays on the border");
    present_curve_map(0.5f, 0.0f, &x, &y);
    ck(fabsf_(y) < 1e-5f, "the top edge midpoint stays on the border");
    present_curve_map(0.0f, 0.0f, &x, &y);
    ck(x > 0.02f && y > 0.02f && x < 0.2f, "the corner is pulled in (%.3f,%.3f)", x, y);
    present_curve_map(1.0f, 1.0f, &x, &y);
    ck(x < 0.98f && y < 0.98f, "the far corner is pulled in too");
    int mono = 1;
    float last = -1;
    for (int i = 0; i <= 40; i++) {      /* along a row the mapped x never goes backwards */
        present_curve_map(i / 40.0f, 0.1f, &x, &y);
        if (x < last) mono = 0;
        last = x;
    }
    ck(mono, "the map is monotonic along a row (no fold-over)");
    ck(present_vignette_alpha(0.5f, 0.5f) == 0, "no vignette at the centre");
    ck(present_vignette_alpha(0.0f, 0.0f) > 100, "the corner is darkened (%d)", present_vignette_alpha(0.0f, 0.0f));
    ck(present_vignette_alpha(0.5f, 0.0f) < present_vignette_alpha(0.0f, 0.0f), "an edge midpoint is lighter than a corner");
    uint32_t a = present_scan_mask(5, 2, 960, 800, 320, 200), b = present_scan_mask(5, 0, 960, 800, 320, 200);
    ck(a == 0xFFFFFFFFu && (b & 255) == 255 - PRESENT_CRT_SCAN_DARK && ((b >> 8) & 255) == (b & 255) && ((b >> 16) & 255) == (b & 255),
       "the scan-only mask is grey: centre white, seam dark, no grille (got %08X)", (unsigned)b);
}

/* ---------------------------------------------------------- the geometry */
static void t_geometry(void)
{
    printf("filter geometry ");
    int x, y, w, h;
    /* 1000x700 window, a 320x200 source shown at MCGA's 320x240 display shape */
    present_content_rect(FILTER_SMOOTH, 1000, 700, 320, 200, 320, 240, &x, &y, &w, &h);
    ck(w == 933 && h == 700 && x == 33 && y == 0, "smooth fits 4:3 (933x700 at 33,0)");
    present_content_rect(FILTER_SHARP, 1000, 700, 320, 200, 320, 240, &x, &y, &w, &h);
    ck(w == 933 && h == 700 && x == 33 && y == 0, "sharp shares smooth's content rect");
    present_content_rect(FILTER_INTEGER, 1000, 700, 320, 200, 320, 240, &x, &y, &w, &h);
    ck(w == 960 && h == 600 && x == 20 && y == 50, "integer picks 3x of the source (960x600 centred)");
    ck(w % 320 == 0 && h % 200 == 0, "integer content is an integer multiple of the source");
    ck(present_prescale(933, 700, 320, 200) == 2, "sharp pre-scales 2x to 933x700");
    ck(present_prescale(320, 240, 320, 200) == 1, "a 1:1 fit pre-scales 1x");
    ck(present_prescale(1000, 700, 720, 348) == 1, "HGC 720x348 in 1000x700 is 1x");
    /* --aspect 1:1: the display is the framebuffer itself */
    present_content_rect(FILTER_SMOOTH, 1000, 700, 320, 200, 320, 200, &x, &y, &w, &h);
    ck(w == 1000 && h == 625 && x == 0 && y == 37, "1:1 smooth fits 320:200 (1000x625)");
    /* a window smaller than one source pixel must not blow up */
    present_content_rect(FILTER_INTEGER, 100, 100, 320, 200, 320, 240, &x, &y, &w, &h);
    ck(w <= 100 && h <= 100 && w > 0 && h > 0, "a tiny window falls back to a fit");
}

#ifdef HAVE_SDL
#include <SDL.h>

#define WIN_W 1000
#define WIN_H 700
#define SRC_W 320
#define SRC_H 200
#define DISP_W 320
#define DISP_H 240

static int start_sdl(SDL_Window **win, SDL_Renderer **ren)
{
    /* the offscreen driver (2.0.22+) renders without a display; dummy is the
     * older fallback.  Both work with the software renderer. */
    SDL_SetHint(SDL_HINT_VIDEODRIVER, "offscreen");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
        if (SDL_Init(SDL_INIT_VIDEO) != 0) return -1;
    }
    *win = SDL_CreateWindow("test_present", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                            WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    if (!*win) { SDL_Quit(); return -1; }
    *ren = SDL_CreateRenderer(*win, -1, SDL_RENDERER_SOFTWARE);
    if (!*ren) *ren = SDL_CreateRenderer(*win, -1, 0);
    if (!*ren) { SDL_DestroyWindow(*win); SDL_Quit(); return -1; }
    return 0;
}

/* every pixel outside the content rect must be black (the letterbox bars) */
static int letterbox_clean(const unsigned *px, int ow, int oh, int x, int y, int w, int h)
{
    for (int j = 0; j < oh; j++)
        for (int i = 0; i < ow; i++) {
            int in = i >= x && i < x + w && j >= y && j < y + h;
            if (!in && (px[(size_t)j * ow + i] & 0xFFFFFFu)) return 0;
        }
    return 1;
}

static void render_read(SDL_Renderer *ren, SDL_Texture *src, int filter, unsigned *out, int ow, int oh)
{
    memset(out, 0xAA, (size_t)ow * oh * 4);
    if (present_render(ren, src, SRC_W, SRC_H, DISP_W, DISP_H, filter) != 0)
        printf("  present_render(%s): %s\n", present_filter_name(filter), SDL_GetError());
    SDL_RenderPresent(ren);
    SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, out, ow * 4);
}

static void t_render(void)
{
    printf("filter render   ");
    SDL_Window *win = NULL;
    SDL_Renderer *ren = NULL;
    if (start_sdl(&win, &ren) != 0) {
        printf("(no usable SDL video driver: skipping the render half) ");
        return;
    }
    int ow = 0, oh = 0;
    SDL_GetRendererOutputSize(ren, &ow, &oh);
    ck(ow == WIN_W && oh == WIN_H, "the offscreen window is %dx%d (got %dx%d)", WIN_W, WIN_H, ow, oh);

    /* a 1-pixel checkerboard: linear and sharp-bilinear must disagree on it */
    SDL_Texture *src = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING, SRC_W, SRC_H);
    unsigned *sbuf = malloc((size_t)SRC_W * SRC_H * 4);
    for (int j = 0; j < SRC_H; j++)
        for (int i = 0; i < SRC_W; i++)
            sbuf[(size_t)j * SRC_W + i] = ((i + j) & 1) ? 0xFFFFFFFFu : 0xFF000000u;
    SDL_UpdateTexture(src, NULL, sbuf, SRC_W * 4);

    unsigned *f[FILTER_COUNT];
    for (int k = 0; k < FILTER_COUNT; k++) {
        f[k] = malloc((size_t)ow * oh * 4);
        render_read(ren, src, k, f[k], ow, oh);
    }

    for (int k = 0; k < FILTER_COUNT; k++) {
        int x, y, w, h;
        present_content_rect(k, ow, oh, SRC_W, SRC_H, DISP_W, DISP_H, &x, &y, &w, &h);
        ck(letterbox_clean(f[k], ow, oh, x, y, w, h),
           "%s: the letterbox outside %dx%d at (%d,%d) is black", present_filter_name(k), w, h, x, y);
        /* at least one pixel inside the content is lit */
        int lit = 0;
        for (int j = y; j < y + h && !lit; j++)
            for (int i = x; i < x + w && !lit; i++)
                if (f[k][(size_t)j * ow + i] & 0xFFFFFFu) lit = 1;
        ck(lit, "%s: the content rect has a lit pixel", present_filter_name(k));
    }

    /* integer: 3x of a checkerboard is uniform 3x3 blocks */
    {
        int x, y, w, h, m;
        present_content_rect(FILTER_INTEGER, ow, oh, SRC_W, SRC_H, DISP_W, DISP_H, &x, &y, &w, &h);
        m = w / SRC_W;
        ck(m == 3, "integer scale factor is 3 in a 1000x700 window (got %d)", m);
        /* the checkerboard is white where (sx+sy) is odd: (101,50) is white,
         * and its 3x3 block must be uniformly white */
        unsigned want = 0xFFFFFFFFu;
        int ok = 1;
        for (int b = 0; b < m; b++)
            for (int a = 0; a < m; a++)
                if (f[FILTER_INTEGER][(size_t)(y + 50 * m + b) * ow + (x + 101 * m + a)] != want) ok = 0;
        ck(ok, "integer: source pixel (101,50) becomes a uniform 3x3 white block");
    }

    /* sharp-bilinear is not plain smooth, and not plain nearest either */
    ck(memcmp(f[FILTER_SHARP], f[FILTER_SMOOTH], (size_t)ow * oh * 4) != 0,
       "sharp-bilinear differs from smooth");
    ck(memcmp(f[FILTER_SHARP], f[FILTER_INTEGER], (size_t)ow * oh * 4) != 0,
       "sharp-bilinear differs from integer");
    ck(memcmp(f[FILTER_SMOOTH], f[FILTER_INTEGER], (size_t)ow * oh * 4) != 0,
       "smooth differs from integer");

    /* crt: the multiply mask only ever darkens sharp-bilinear's picture, and
     * does darken it (the checkerboard is half white), with a scanline gap */
    {
        int x, y, w, h, dark = 0, bright = 0, seam = 1;
        present_content_rect(FILTER_CRT, ow, oh, SRC_W, SRC_H, DISP_W, DISP_H, &x, &y, &w, &h);
        for (int j = y; j < y + h; j++)
            for (int i = x; i < x + w; i++) {
                unsigned a = f[FILTER_SHARP][(size_t)j * ow + i], b = f[FILTER_CRT][(size_t)j * ow + i];
                for (int s = 0; s < 24; s += 8)
                    if (((b >> s) & 255) > ((a >> s) & 255)) bright++;
                if (a != b) dark++;
            }
        ck(bright == 0, "crt never brightens sharp-bilinear's pixel (%d did)", bright);
        ck(dark > w * h / 2, "crt darkens most of the picture (%d of %d)", dark, w * h);
        /* row mean luminance must vary with the scanline profile */
        long best = 0, worst = 1L << 40;
        for (int j = y; j < y + h; j++) {
            long s = 0;
            for (int i = x; i < x + w; i++) s += f[FILTER_CRT][(size_t)j * ow + i] & 255;
            if (s > best) best = s;
            if (s < worst) worst = s;
        }
        seam = best > worst * 5 / 4;
        ck(seam, "crt rows differ in brightness (scanlines): max %ld vs min %ld", best, worst);
    }

    /* the scanline family: scanlines = sharp + bands (never brighter, rows vary),
     * soft-scanlines = smooth + bands, curved differs from flat crt and keeps a
     * black bowed corner */
    {
        int x, y, w, h, bright = 0, bright2 = 0;
        present_content_rect(FILTER_SCANLINES, ow, oh, SRC_W, SRC_H, DISP_W, DISP_H, &x, &y, &w, &h);
        for (int j = y; j < y + h; j++)
            for (int i = x; i < x + w; i++) {
                unsigned s0 = f[FILTER_SHARP][(size_t)j * ow + i], s1 = f[FILTER_SCANLINES][(size_t)j * ow + i];
                unsigned m0 = f[FILTER_SMOOTH][(size_t)j * ow + i], m1 = f[FILTER_SOFT_SCANLINES][(size_t)j * ow + i];
                for (int s = 0; s < 24; s += 8) {
                    if (((s1 >> s) & 255) > ((s0 >> s) & 255)) bright++;
                    if (((m1 >> s) & 255) > ((m0 >> s) & 255)) bright2++;
                }
            }
        ck(bright == 0, "scanlines never brighten sharp-bilinear (%d did)", bright);
        ck(bright2 == 0, "soft-scanlines never brighten smooth (%d did)", bright2);
        ck(memcmp(f[FILTER_SCANLINES], f[FILTER_SHARP], (size_t)ow * oh * 4) != 0, "scanlines differ from sharp");
        ck(memcmp(f[FILTER_SOFT_SCANLINES], f[FILTER_SMOOTH], (size_t)ow * oh * 4) != 0, "soft-scanlines differ from smooth");
        ck(memcmp(f[FILTER_CRT_CURVED], f[FILTER_CRT], (size_t)ow * oh * 4) != 0, "crt-curved differs from crt");
        /* the curved picture's top-left corner is outside the bowed edge: black,
         * where flat crt shows the checkerboard (the picture's corner pixel row) */
        unsigned corner = 0;
        for (int j = y; j < y + 6; j++)
            for (int i = x; i < x + 6; i++) corner |= f[FILTER_CRT_CURVED][(size_t)j * ow + i] & 0xFFFFFF;
        ck(corner == 0, "crt-curved: the corner of the content rect is black (%06X)", corner);
        unsigned mid = 0;
        for (int i = x + w / 2 - 20; i < x + w / 2 + 20; i++) mid |= f[FILTER_CRT_CURVED][(size_t)(y + h / 2) * ow + i] & 0xFFFFFF;
        ck(mid != 0, "crt-curved: the middle is lit");
    }

    /* the screenshots: one per filter of a fixed synthetic frame */
    {
        const char *dir = getenv("PRESENT_SHOTS");
        if (dir) {
            for (int j = 0; j < SRC_H; j++)
                for (int i = 0; i < SRC_W; i++) {
                    unsigned r = i * 255 / SRC_W, g = j * 255 / SRC_H, b = 255 - r;
                    if (((i / 20) + (j / 20)) & 1) { r /= 2; g /= 2; b /= 2; }
                    if (i > 100 && i < 220 && j > 60 && j < 140) { r = g = b = (i % 8 < 4) ? 255 : 40; }
                    if (j % 50 == 0 || i % 64 == 0) r = g = b = 255;
                    sbuf[(size_t)j * SRC_W + i] = 0xFF000000u | r << 16 | g << 8 | b;
                }
            SDL_UpdateTexture(src, NULL, sbuf, SRC_W * 4);
            for (int k = 0; k < FILTER_COUNT; k++) {
                render_read(ren, src, k, f[k], ow, oh);
                char path[512];
                snprintf(path, sizeof path, "%s/%s.ppm", dir, present_filter_name(k));
                FILE *fp = fopen(path, "wb");
                if (!fp) continue;
                fprintf(fp, "P6\n%d %d\n255\n", ow, oh);
                for (int p = 0; p < ow * oh; p++) {
                    unsigned v = f[k][p];
                    fputc((v >> 16) & 255, fp); fputc((v >> 8) & 255, fp); fputc(v & 255, fp);
                }
                fclose(fp);
            }
        }
    }

    for (int k = 0; k < FILTER_COUNT; k++) free(f[k]);
    free(sbuf);
    SDL_DestroyTexture(src);
    present_shutdown();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
}
#endif

int main(void)
{
    t_crt();        printf("ok\n");
    t_names();      printf("ok\n");
    t_curve();      printf("ok\n");
    t_geometry();   printf("ok\n");
#ifdef HAVE_SDL
    t_render();     printf("ok\n");
#else
    printf("filter render   (built without SDL2: skipping the render half) ok\n");
#endif
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
