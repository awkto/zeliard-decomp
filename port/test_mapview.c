/* test_mapview.c — the map viewer's pure half (port/mapview.c): layout, wrap,
 * pan, the map-cell <-> window-pixel mapping and the tile raster.  SDL-free,
 * needs no game files, part of `make test`. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mapview.h"

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

static void t_wrap(void)
{
    printf("wrap            ");
    ck(mapview_wrap(0, 10) == 0 && mapview_wrap(9, 10) == 9 && mapview_wrap(10, 10) == 0, "in range / at n");
    ck(mapview_wrap(-1, 10) == 9 && mapview_wrap(-10, 10) == 0 && mapview_wrap(-11, 10) == 9, "negative");
    ck(mapview_wrap(25, 10) == 5 && mapview_wrap(7, 0) == 0, "multiple turns / n == 0");
    MapViewSeg s[2];
    ck(mapview_segments(2, 5, 10, s) == 1 && s[0].src == 2 && s[0].len == 5 && s[0].dst == 0, "one run inside the map");
    ck(mapview_segments(8, 5, 10, s) == 2 && s[0].src == 8 && s[0].len == 2 && s[0].dst == 0 &&
       s[1].src == 0 && s[1].len == 3 && s[1].dst == 2, "a run across the seam splits in two");
    ck(mapview_segments(5, 5, 10, s) == 1 && s[0].len == 5, "ending exactly at the edge needs no second run");
    ck(mapview_segments(-3, 4, 10, s) == 2 && s[0].src == 7 && s[0].len == 3 && s[1].src == 0 && s[1].len == 1,
       "a negative origin wraps");
    ck(mapview_segments(3, 99, 10, s) >= 1 && s[0].len + (mapview_segments(3, 99, 10, s) == 2 ? s[1].len : 0) == 10,
       "a view wider than the map shows each cell once");
}

static void t_layout(void)
{
    printf("layout          ");
    MapViewLayout L;
    mapview_layout(&L, 0, 0, 1280, 720, 60, 64);          /* a small map: fits */
    ck(L.cs == 11 && L.vw == 60 && L.vh == 64 && !L.pan_x && !L.pan_y, "small map fits (cs %d vw %d)", L.cs, L.vw);
    ck(L.x0 == (1280 - 660) / 2 && L.y0 == (720 - 704) / 2, "and is centred (%d,%d)", L.x0, L.y0);
    mapview_layout(&L, 0, 0, 1280, 800, 320, 64);          /* 4 px per cell fills the width */
    ck(L.cs == 4 && L.vw == 320 && !L.pan_x && !L.pan_y, "a 320-wide map fits at the minimum size");
    mapview_layout(&L, 0, 0, 800, 600, 320, 64);           /* too narrow for 4 px/cell */
    ck(L.cs == MAPVIEW_CS_MIN && L.vw == 200 && L.pan_x && !L.pan_y, "a narrow window pins the minimum and pans (vw %d)", L.vw);
    mapview_layout(&L, 0, 0, 800, 200, 100, 64);           /* too short */
    ck(L.cs == MAPVIEW_CS_MIN && L.vh == 50 && L.pan_y, "a short window pans vertically");
    mapview_layout(&L, 10, 20, 100, 100, 3, 3);
    ck(L.cs == MAPVIEW_CS_MAX && L.vw == 3, "tiny maps stop at the largest cell size");
    mapview_layout(&L, 0, 0, 0, 0, 0, 0);
    ck(L.vw >= 1 && L.vh >= 1 && L.cs >= 1, "degenerate sizes stay sane");
}

static void t_mapping(void)
{
    printf("mapping         ");
    MapViewLayout L;
    mapview_layout(&L, 0, 0, 800, 600, 320, 64);           /* pans in x: vw 200, cs 4 */
    int ox = 0, oy = 0, px, py, c, r;
    mapview_center(&L, 160, 32, &ox, &oy);
    ck(ox == 60 && oy == 0, "centre on column 160 -> origin 60 (got %d,%d)", ox, oy);
    ck(mapview_cell_to_px(&L, ox, oy, 160, 32, &px, &py) && px == L.x0 + 100 * 4 && py == L.y0 + 32 * 4, "centre cell lands mid-view");
    ck(!mapview_cell_to_px(&L, ox, oy, 10, 32, &px, &py), "a column outside the view is not mapped");
    mapview_center(&L, 5, 0, &ox, &oy);                     /* near the left edge: the view wraps */
    ck(ox == 5 - 100 + 320, "origin wraps negative (got %d)", ox);
    ck(mapview_cell_to_px(&L, ox, oy, 315, 0, &px, &py) && px == L.x0 + (315 - ox) * 4, "column 315 is left of the seam");
    ck(mapview_cell_to_px(&L, ox, oy, 2, 0, &px, &py) && px == L.x0 + (2 + 320 - ox) * 4, "column 2 is right of the seam");
    ck(mapview_cell_to_px(&L, ox, oy, 319, 0, &px, &py) && mapview_cell_to_px(&L, ox, oy, 0, 0, &px, &py), "both sides of the seam are in view");
    /* round trip */
    int ok = 1;
    for (int col = 0; col < 320; col += 7)
        if (mapview_cell_to_px(&L, ox, oy, col, 9, &px, &py)) {
            if (!mapview_px_to_cell(&L, ox, oy, px + 1, py + 1, &c, &r) || c != col || r != 9) ok = 0;
        }
    ck(ok, "px -> cell inverts cell -> px across the seam");
    ck(!mapview_px_to_cell(&L, ox, oy, -5, 5, &c, &r), "outside the view maps to nothing");
    /* panning */
    mapview_pan(&L, &ox, &oy, 400, 3);
    ck(ox == mapview_wrap(5 - 100 + 320 + 400, 320) && oy == 0, "panning wraps in x and ignores a fitting axis");
    MapViewLayout F;
    mapview_layout(&F, 0, 0, 1280, 800, 60, 64);
    ox = 7; oy = 7;
    mapview_pan(&F, &ox, &oy, 5, 5);
    ck(ox == 0 && oy == 0, "a map that fits cannot be panned");
    mapview_center(&F, 30, 30, &ox, &oy);
    ck(ox == 0 && oy == 0, "and is not re-centred");
    MapViewLayout T;
    mapview_layout(&T, 0, 0, 1280, 200, 100, 64);          /* pans vertically */
    oy = 0; mapview_pan(&T, &ox, &oy, 0, -1);
    ck(oy == 63, "vertical pan wraps below zero (got %d)", oy);
    mapview_center(&T, 0, 2, &ox, &oy);
    ck(mapview_cell_to_px(&T, ox, oy, 0, 62, &px, &py) && mapview_cell_to_px(&T, ox, oy, 0, 2, &px, &py),
       "the vertical seam shows rows 62 and 2 together");
}

static void t_raster(void)
{
    printf("raster          ");
    static Tileset t;
    static Map m;
    memset(&t, 0, sizeof t); memset(&m, 0, sizeof m);
    t.present[1] = 1; memset(t.cell[1].px, 9, sizeof t.cell[1].px);          /* solid index 9 */
    t.present[2] = 1; t.cell[2].px[0][0] = 0; t.cell[2].px[0][1] = 9;        /* black pixel + one lit */
    for (int k = 0; k < 3; k++) { t.present[0x40 + k] = 1; memset(t.cell[0x40 + k].px, 12, sizeof t.cell[0x40 + k].px); }
    m.width = 12;
    m.grid[0][0] = 1; m.grid[11][63] = 1; m.grid[3][5] = 2; m.grid[4][4] = 0x70;   /* 0x70: not in the bank */
    m.nfix = 1; m.fix[0].col = 10; m.fix[0].row = 20; m.fix[0].cell = 0x40;       /* wraps: 10, 11, 0 */
    size_t n = (size_t)12 * 8 * 64 * 8;
    uint32_t *px = malloc(n * 4);
    ck(px && mapview_raster(px, &m, &t) == 0, "raster succeeds");
    if (!px) return;
    int stride = 96;
    gfx_pal_init();
    uint32_t want = 0xFF000000u | (uint32_t)PAL_RGB[9][0] << 16 | (uint32_t)PAL_RGB[9][1] << 8 | PAL_RGB[9][2];
    ck(px[0] == want && px[7 * stride + 7] == want, "cell (0,0) is painted from the bank");
    ck(px[(63 * 8 + 7) * stride + 11 * 8 + 7] == want, "the last cell is painted (bottom-right)");
    ck(px[8] == MAPVIEW_BG, "an empty cell is the background");
    ck(px[(5 * 8) * stride + 3 * 8] == MAPVIEW_BG && px[(5 * 8) * stride + 3 * 8 + 1] == want, "colour 0 pixels are background, others painted");
    ck(px[(4 * 8) * stride + 4 * 8] == MAPVIEW_BG, "a value outside the bank is skipped");
    uint32_t f = 0xFF000000u | (uint32_t)PAL_RGB[12][0] << 16 | (uint32_t)PAL_RGB[12][1] << 8 | PAL_RGB[12][2];
    ck(px[(20 * 8) * stride + 10 * 8] == f && px[(20 * 8) * stride + 11 * 8] == f && px[(20 * 8) * stride] == f,
       "a fixture draws its three cells, wrapping at the map edge");
    m.width = 0;
    ck(mapview_raster(px, &m, &t) == -1 && mapview_raster(NULL, &m, &t) == -1, "bad arguments are refused");
    free(px);
}

int main(void)
{
    t_wrap();    printf("ok\n");
    t_layout();  printf("ok\n");
    t_mapping(); printf("ok\n");
    t_raster();  printf("ok\n");
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
