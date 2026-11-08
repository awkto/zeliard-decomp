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


/* ------------------------------------------------------------ annotations */
static MapViewWorld W;

/* map_from_text reads past the end of a short row, so give every missing row a full blank one */
static void text_map(Map *m, int width, const char *rows[MAP_ROWS])
{
    static const char BLANK[] = "........................................................................";
    for (int r = 0; r < MAP_ROWS; r++) if (!rows[r]) rows[r] = BLANK;
    map_from_text(m, width, rows);
}

static void add_door(MapViewInfo *in, int col, int row, int dest_map, int dcol, int drow, int letter)
{
    Door *d = &in->doors[in->ndoors++];
    memset(d, 0, sizeof *d);
    d->col = (uint16_t)col; d->row = (uint8_t)row; d->letter = (uint8_t)letter;
    d->dest_map = (uint8_t)dest_map; d->dest_col = (uint16_t)dcol; d->dest_row = (uint8_t)drow;
}
static void init_info(MapViewInfo *in, const char *name, const char *place, int width)
{
    memset(in, 0, sizeof *in);
    in->ok = 1; in->width = width;
    snprintf(in->name, sizeof in->name, "%s", name);
    snprintf(in->place, sizeof in->place, "%s", place);
}

static void t_doors(void)
{
    printf("doors           ");
    memset(&W, 0, sizeof W);
    MapViewInfo *a = &W.m[0], *b = &W.m[1], *c = &W.m[2], *e = &W.m[3];
    init_info(a, "MP10", "ALFA", 200); init_info(b, "MP11", "BETA", 200);
    init_info(c, "MP12", "ALFA", 200); init_info(e, "MP1D", "ALFA", 70);
    e->boss_room = 1; e->ai = 13;
    add_door(a, 10, 5, 1, 20, 6, 0xC0);            /* 0: <-> b.0 */
    add_door(a, 30, 5, 1, 40, 6, 0xC0);            /* 1: <-> b.1 */
    add_door(a, 50, 5, 0, 70, 5, 0xC0);            /* 2: <-> 3, the same map */
    add_door(a, 70, 5, 0, 50, 5, 0xC0);            /* 3 */
    add_door(a, 90, 5, 0x81, 4, 0xFF, 0xC0);       /* 4: Muralla, west end */
    add_door(a, 100, 5, 0x81, 205, 0xFF, 0xC0);    /* 5: Muralla, east end */
    add_door(a, 110, 5, 3, 5, 5, 0x01);            /* 6: the boss room */
    add_door(a, 120, 5, 2, 7, 7, 0xC0);            /* 7: leads to c, but c's door goes elsewhere */
    add_door(a, 130, 5, 1, 141, 9, 0xC0);          /* 8: b.2 stands one column off */
    add_door(a, 140, 5, 2, 7, 7, 0xC0);            /* 9: <-> c.0 */
    add_door(a, 150, 5, 0, 160, 5, 0xC0);          /* 10: <-> 11 */
    add_door(a, 160, 5, 0, 150, 5, 0xC0);          /* 11 */
    add_door(a, 170, 5, 0x85, 264, 0xFF, 0xC0);    /* 12: Tumba, east end */
    add_door(b, 20, 6, 0, 10, 5, 0xC0);
    add_door(b, 40, 6, 0, 30, 5, 0xC0);
    add_door(b, 140, 9, 0, 130, 5, 0xC0);
    add_door(c, 7, 7, 0, 140, 5, 0xC0);
    MapViewDoorLabel L[MAPVIEW_MAX_DOORS];
    mapview_door_labels(&W, 0, L);
    ck(L[0].kind == MVD_NUM && L[0].num == 1 && !strcmp(L[0].text, "1 BETA"), "first pair a<->b is 1 (%s)", L[0].text);
    ck(L[1].num == 2 && !strcmp(L[1].text, "2 BETA"), "second pair is 2 (%s)", L[1].text);
    ck(L[8].kind == MVD_NUM && L[8].num == 3 && L[8].pdoor == 2, "a partner one column off still pairs, as 3 (%s)", L[8].text);
    mapview_door_labels(&W, 1, L);
    ck(L[0].num == 1 && !strcmp(L[0].text, "1 ALFA") && L[1].num == 2 && L[2].num == 3, "the other end reads the same numbers (%s %s %s)", L[0].text, L[1].text, L[2].text);
    mapview_door_labels(&W, 0, L);
    ck(L[9].kind == MVD_NUM && L[9].num == 1 && !strcmp(L[9].text, "1 MP12"), "pairs are numbered per map pair; the same place name falls back to the map (%s)", L[9].text);
    mapview_door_labels(&W, 2, L);
    ck(L[0].num == 1 && !strcmp(L[0].text, "1 MP10"), "and c reads 1 as well (%s)", L[0].text);
    mapview_door_labels(&W, 0, L);
    ck(L[2].kind == MVD_LETTER && L[3].kind == MVD_LETTER && !strcmp(L[2].text, "A") && !strcmp(L[3].text, "A"), "a pair inside one map takes a letter at both ends (%s %s)", L[2].text, L[3].text);
    ck(!strcmp(L[10].text, "B") && !strcmp(L[11].text, "B"), "the next pair is B (%s %s)", L[10].text, L[11].text);
    ck(L[4].kind == MVD_TOWN && !strcmp(L[4].text, "MURALLA W"), "a cave mouth names its town and end (%s)", L[4].text);
    ck(!strcmp(L[5].text, "MURALLA E") && !strcmp(L[12].text, "TUMBA E"), "east ends (%s, %s)", L[5].text, L[12].text);
    ck(L[6].kind == MVD_BOSS && !strcmp(L[6].text, "BOSS DRAGON"), "a boss-room door names the boss (%s)", L[6].text);
    ck(L[7].kind == MVD_LINK && !strcmp(L[7].text, "TO MP12") && L[7].pmap == -1, "no partner record: a plain link (%s)", L[7].text);
    int pm = -1, pd = -1;
    ck(mapview_door_partner(&W, 0, 0, &pm, &pd) && pm == 1 && pd == 0, "partner lookup");
    ck(!mapview_door_partner(&W, 0, 4, &pm, &pd) && !mapview_door_partner(&W, 0, 7, &pm, &pd) && !mapview_door_partner(&W, 9, 0, &pm, &pd),
       "towns, one-way doors and absent maps have no partner");
    add_door(b, 60, 6, 0, 120, 5, 0xC0);           /* b.3 leads at a.7, which leads at c: not mutual */
    ck(!mapview_door_partner(&W, 1, 3, &pm, &pd) && !mapview_door_partner(&W, 0, 7, &pm, &pd), "a pair must point at each other");
    ck(mapview_town_name(9) && !strcmp(mapview_town_name(9), "ESCO") && mapview_town_name(10) == NULL && mapview_town_width(7) == 280, "town table");
    ck(!strcmp(mapview_boss_name(15), "ALGUIEN") && !strcmp(mapview_boss_name(17), "JASHIIN") && mapview_boss_name(14) == NULL, "boss table");
    ck(mapview_world_find(&W, "MP12") == 2 && mapview_world_find(&W, "MP99") == -1, "world lookup by name");
}

static void set_lists(Tileset *t)
{
    memset(t, 0, sizeof *t);
    t->lists[0x24] = 9; t->lists[0x28] = 7; t->lists[0x2C] = 8;      /* updraft, left, right */
    t->lists[0x18] = 6; t->lists[0x1C] = 5;                          /* belt left, belt right */
}

static void t_runs(void)
{
    printf("currents        ");
    static Map m; static Tileset t;
    const char *rows[MAP_ROWS] = {0};
    rows[3] = "..8888......"; rows[4] = "..8888......"; rows[5] = "..888.......";
    rows[7] = "77......77.."; rows[8] = "77..........";
    rows[10] = ".....99....."; rows[11] = ".....99....."; rows[12] = ".....99.9..."; rows[13] = ".....99.9...";
    rows[20] = "5555........"; rows[21] = "6666........";
    rows[30] = "1111111111.."; rows[31] = "8...........";
    text_map(&m, 12, rows);
    set_lists(&t);
    ck(mapview_cell_class(&t, 9) == MVC_UP && mapview_cell_class(&t, 7) == MVC_LEFT && mapview_cell_class(&t, 8) == MVC_RIGHT &&
       mapview_cell_class(&t, 6) == MVC_BELT_L && mapview_cell_class(&t, 5) == MVC_BELT_R && mapview_cell_class(&t, 1) == MVC_NONE &&
       mapview_cell_class(&t, 0) == MVC_NONE && mapview_cell_class(NULL, 9) == MVC_NONE, "the cell classes come from the bank's lists");
    MapViewRun R[64];
    int n = mapview_runs(&m, &t, R, 64);
    int right = -1, left0 = -1, left1 = -1, up = -1, up2 = -1, bl = -1, br = -1, r31 = -1;
    for (int i = 0; i < n; i++) {
        if (R[i].kind == MVC_RIGHT && R[i].r0 == 3) right = i;
        if (R[i].kind == MVC_RIGHT && R[i].r0 == 5) r31 = i;
        if (R[i].kind == MVC_LEFT && R[i].c0 == 0) left0 = i;
        if (R[i].kind == MVC_LEFT && R[i].c0 == 8) left1 = i;
        if (R[i].kind == MVC_UP && R[i].c0 == 5) up = i;
        if (R[i].kind == MVC_UP && R[i].c0 == 8) up2 = i;
        if (R[i].kind == MVC_BELT_L) bl = i;
        if (R[i].kind == MVC_BELT_R) br = i;
    }
    ck(right >= 0 && R[right].c0 == 2 && R[right].c1 == 5 && R[right].r1 == 4, "equal rows of a current merge into one block");
    ck(r31 >= 0 && R[r31].c1 == 4 && R[r31].r1 == 5, "a shorter row does not join the block above it");
    ck(left0 >= 0 && R[left0].c1 == 1 && R[left0].r0 == 7 && R[left0].r1 == 8, "a left current, two rows");
    ck(left1 >= 0 && R[left1].c1 == 9 && R[left1].r1 == 7, "a second run in the same row stays apart");
    ck(up >= 0 && R[up].c1 == 6 && R[up].r0 == 10 && R[up].r1 == 13, "updrafts run down columns and merge across (%d..%d)", up >= 0 ? R[up].c0 : -1, up >= 0 ? R[up].c1 : -1);
    ck(up2 >= 0 && R[up2].c1 == 8 && R[up2].r0 == 12 && R[up2].r1 == 13, "a column of a different extent stays its own run");
    ck(bl >= 0 && br >= 0 && R[bl].r0 == 21 && R[br].r0 == 20, "belts are kept apart by direction");
    ck(n == 9, "nothing else is reported (%d runs)", n);
    ck(mapview_runs(&m, &t, R, 3) == 3 && mapview_runs(NULL, &t, R, 3) == 0 && mapview_runs(&m, &t, R, 0) == 0, "the output limit and bad arguments");
    int seam = 1;
    memset(&m, 0, sizeof m);
    {   const char *r2[MAP_ROWS] = {0}; r2[2] = "9..........9"; text_map(&m, 12, r2); }
    n = mapview_runs(&m, &t, R, 64);
    for (int i = 0; i < n; i++) if (R[i].c0 != R[i].c1) seam = 0;
    ck(n == 2 && seam, "nothing joins across the wrap seam");
}

static void t_spans(void)
{
    printf("lifts           ");
    static Map m;
    const char *rows[MAP_ROWS] = {0};
    rows[15] = ".....1......"; rows[23] = "....1.......";
    rows[40] = "1...........";  rows[41] = ".1..........";
    text_map(&m, 12, rows);
    Fixture f; MapViewSpan s;
    memset(&f, 0, sizeof f); f.kind = 0; f.col = 4; f.row = 20;
    ck(mapview_fixture_span(&m, &f, &s) && s.vertical && !s.oneway && s.col == 4 && s.w == 3 && s.row == 16 && s.h == 7,
       "an elevator sweeps the rows its three cells can move into (row %d, %d tall)", s.row, s.h);
    f.kind = 1;
    ck(mapview_fixture_span(&m, &f, &s) && s.oneway && s.row == 20 && s.h == 3, "a gate only sinks: from its row down to the floor (%d tall)", s.h);
    {   const char *r4[MAP_ROWS] = {0}; static Map w; r4[38] = "1..........."; r4[45] = "1..........."; text_map(&w, 12, r4);
        memset(&f, 0, sizeof f); f.kind = 0; f.col = 10; f.row = 40;               /* its cells are 10, 11 and 0 */
        ck(mapview_fixture_span(&w, &f, &s) && s.row == 39 && s.h == 6 && s.col == 10, "the cells it moves into wrap with the map (row %d, %d tall)", s.row, s.h); }
    {   const char *r3[MAP_ROWS] = {0}; r3[9] = "111111111111"; r3[11] = "111111111111"; static Map b; text_map(&b, 12, r3);
        memset(&f, 0, sizeof f); f.kind = 0; f.col = 2; f.row = 10;
        ck(!mapview_fixture_span(&b, &f, &s), "a lift between two floors cannot move: no line"); }
    memset(&f, 0, sizeof f); f.kind = 2; f.row = 30; f.col = 5; f.var = 1; f.lim_l = 3; f.lim_r = 8;
    ck(mapview_fixture_span(&m, &f, &s) && !s.vertical && s.col == 3 && s.w == 8 && s.h == 1 && s.row == 30, "a platform covers lim_l .. lim_r + 2 (%d wide)", s.w);
    f.lim_l = 10; f.lim_r = 1;                                        /* patrols across the seam */
    ck(mapview_fixture_span(&m, &f, &s) && s.col == 10 && s.w == 6, "a platform may patrol across the seam (%d wide)", s.w);
    f.var = 0;
    ck(!mapview_fixture_span(&m, &f, &s), "a static platform draws nothing");
}

static void t_items(void)
{
    printf("items           ");
    static Map m;
    const char *rows[MAP_ROWS] = {0};
    text_map(&m, 20, rows);
    m.cavern = 5; snprintf(m.name, sizeof m.name, "MPX");
    m.nobj = 6;
    m.objs[0].type = 0x05; m.objs[0].col = 3;                                         /* an enemy: not an item */
    m.objs[1].type = 0x73; m.objs[1].col = 5; m.objs[1].row = 9; m.objs[1].phase = 3;
    m.objs[1].flags = 0x20; m.objs[1].home_col = 0x12; m.objs[1].home_row = 0x04;     /* event pair */
    m.objs[2].type = 0x76; m.objs[2].col = 0xFF00;                                    /* already gone */
    m.objs[3].type = 0xD0; m.objs[3].col = 7; m.objs[3].row = 4; m.objs[3].next = 0x19; m.objs[3].flags = 0x20; m.objs[3].home_col = 0x12; m.objs[3].home_row = 0x08;
    m.objs[4].type = 0x73; m.objs[4].col = 9; m.objs[4].row = 4; m.objs[4].phase = 0; m.objs[4].next = 0x18; m.objs[4].home_col = 0xFFFF;
    m.objs[5].type = 0x7A; m.objs[5].col = 11; m.objs[5].row = 4; m.objs[5].home_col = 0xFFFF;
    static MapViewInfo in;
    mapview_info_set(&in, &m);
    ck(in.ok && in.nitems == 4 && in.items[0].idx == 1 && in.items[1].idx == 3, "only item states are kept (%d)", in.nitems);
    ck(in.items[0].fbyte == 0x12 && in.items[0].fmask == 0x04 && in.items[2].fmask == 0, "the event pair is kept only on event objects");
    MapViewItem it[16];
    uint8_t page[256]; memset(page, 0, sizeof page);
    int n = mapview_items(&in, NULL, 0, page, it, 16);
    ck(n == 4 && it[0].kind == MVI_CHEST && !strcmp(it[0].label, "EMPTY") && !it[0].taken, "a chest's prize comes from its digit (%s)", it[0].label);
    ck(it[1].kind == MVI_ROCK && !strcmp(it[1].label, "ROCK FULL POTION"), "a rock shows what it hides (%s)", it[1].label);
    ck(it[2].kind == MVI_CHEST && !strcmp(it[2].label, "POTION"), "digit 0 turns the chest into its `next` (%s)", it[2].label);
    ck(it[3].kind == MVI_SHOES && !strcmp(it[3].label, "PIRIKA SHOES"), "the key item is named by cavern (%s)", it[3].label);
    page[0x12] = 0x04;
    mapview_items(&in, NULL, 0, page, it, 16);
    ck(it[0].taken && !it[1].taken, "the event flag marks an item taken (it survives leaving the cavern)");
    page[0x12] = 0x0C;
    mapview_items(&in, NULL, 0, page, it, 16);
    ck(it[0].taken && it[1].taken && !it[2].taken, "each item has its own bit");
    memset(page, 0, sizeof page);
    MapObj live[8]; memset(live, 0, sizeof live);
    for (int i = 0; i < 6; i++) live[i] = m.objs[i];
    live[4].col = 0xFF00;                                                  /* picked up on this visit, no flag */
    mapview_items(&in, live, 6, page, it, 16);
    ck(it[2].taken && !it[0].taken, "a removed live record counts as taken");
    live[3].type = 0x79; live[3].next = 0;                                  /* the rock broke and left a full potion */
    mapview_items(&in, live, 6, page, it, 16);
    ck(it[1].kind == MVI_FULL && !strcmp(it[1].label, "FULL POTION") && !it[1].taken, "a broken rock shows what it dropped (%s)", it[1].label);
    ck(mapview_items(&in, NULL, 0, page, it, 2) == 2, "the output limit");
    MapViewItem one; MapViewInfo q; memset(&q, 0, sizeof q); q.ok = 1; q.nitems = 4; q.cavern = 1;
    q.items[0].type = 0x73; q.items[0].phase = 4; q.items[1].type = 0x73; q.items[1].phase = 6;
    q.items[2].type = 0x7C; q.items[3].type = 0xF1;
    MapViewItem all[4];
    mapview_items(&q, NULL, 0, NULL, all, 4);
    ck(!strcmp(all[0].label, "500G") && !strcmp(all[1].label, "GLORY CREST") && all[2].kind == MVI_MSG && all[3].kind == MVI_DROP && !all[3].label[0],
       "500G, the Glory Crest, a message chest, a floor that gives way");
    (void)one;
}

static void t_legend(void)
{
    printf("legend          ");
    int w[4] = { 50, 40, 30, 20 }, line[4];
    ck(mapview_legend_fit(w, 4, 100, 5, 2, line) == 4 && line[0] == 0 && line[1] == 0 && line[2] == 1 && line[3] == 1, "entries wrap onto a second line");
    ck(mapview_legend_fit(w, 4, 100, 5, 1, line) == 2, "with one line the tail is dropped");
    ck(mapview_legend_fit(w, 4, 45, 5, 2, line) == 0, "an entry wider than the window stops the fill");
    ck(mapview_legend_fit(w, 4, 1000, 5, 2, line) == 4 && line[3] == 0, "wide windows keep one line");
    ck(mapview_legend_fit(w, 0, 100, 5, 2, line) == 0, "an empty legend");
}

static int data_present(const char *dir)
{
    static Map m;
    memset(&m, 0, sizeof m);
    int r = map_load_system(&m, dir, 0);
    map_free(&m);
    return r == 0;
}

static void t_real(const char *dir)
{
    printf("real maps       ");
    if (!data_present(dir)) { printf("(no game files: skipped) "); return; }
    static MapViewWorld RW;
    ck(mapview_world_load(&RW, dir) == 31, "all 31 system maps load");
    ck(!strcmp(RW.m[0].place, "MALICIA") && !strcmp(RW.m[26].place, "FALTER") && !strcmp(RW.m[29].place, "JASHIIN'S ROOM"), "the HUD place names (%s, %s, %s)", RW.m[0].place, RW.m[26].place, RW.m[29].place);
    /* every door in the game is a town, a boss room or half of a mutual pair */
    int links = 0, doors = 0, pairs = 0;
    MapViewDoorLabel L[MAPVIEW_MAX_DOORS];
    for (int i = 0; i < MAPVIEW_NMAPS; i++) {
        mapview_door_labels(&RW, i, L);
        for (int d = 0; d < RW.m[i].ndoors; d++) {
            doors++;
            if (L[d].kind == MVD_LINK) { links++; printf("\n    unpaired %s door %d -> %s ", RW.m[i].name, d, L[d].text); }
            if (L[d].kind == MVD_NUM || L[d].kind == MVD_LETTER) {
                pairs++;
                MapViewDoorLabel P[MAPVIEW_MAX_DOORS];
                mapview_door_labels(&RW, L[d].pmap, P);
                if (P[L[d].pdoor].kind == MVD_BOSS) continue;      /* the boss room's way back: the cavern side names the boss */
                if (P[L[d].pdoor].pmap != i || P[L[d].pdoor].pdoor != d || P[L[d].pdoor].num != L[d].num) { pairs = -9999; printf("\n    mismatch %s door %d (%s) vs %s door %d (%s)", RW.m[i].name, d, L[d].text, RW.m[L[d].pmap].name, L[d].pdoor, P[L[d].pdoor].text); }
            }
        }
    }
    ck(doors > 150 && links == 3, "%d doors, %d without a partner, a town or a boss (the MP81/MP82 one-way triangle)", doors, links);
    ck(pairs > 100, "every pair is mutual and numbered the same at both ends (%d ends)", pairs);
    mapview_door_labels(&RW, 5, L);                                    /* MP30 (Madera) */
    ck(!strcmp(L[0].text, "1 RIZA") && !strcmp(L[11].text, "10 RIZA") && !strcmp(L[10].text, "BOSQUE E") && !strcmp(L[1].text, "1 PELIGRO"),
       "MP30: Riza 1..10, Bosque east, Peligro (%s, %s, %s, %s)", L[0].text, L[11].text, L[10].text, L[1].text);
    mapview_door_labels(&RW, 6, L);                                    /* MP31 (Riza) */
    ck(!strcmp(L[11].text, "10 MADERA") && !strcmp(L[8].text, "BOSQUE W") && !strcmp(L[12].text, "BOSS POLLO"), "MP31: the same numbers back, Bosque west, Pollo (%s, %s, %s)", L[11].text, L[8].text, L[12].text);
    mapview_door_labels(&RW, 0, L);
    ck(!strcmp(L[0].text, "BOSS CANGREJO") && !strcmp(L[1].text, "MURALLA E"), "MP10: Cangrejo, Muralla (%s, %s)", L[0].text, L[1].text);
    ck(!strcmp(L[4].text, "EXIT CANGREJO"), "MP10: the door that lands on the boss room's exit door is the Exit (%s)", L[4].text);
    mapview_door_labels(&RW, 15, L);                                   /* MP61: pairs inside one map */
    ck(L[4].kind == MVD_LETTER && L[9].kind == MVD_LETTER && L[4].num == L[9].num, "MP61: doors 4 and 9 are a letter pair (%s)", L[4].text);
    mapview_door_labels(&RW, 18, L);                                   /* MP70 Caliente */
    ck(!strcmp(L[11].text, "BOSS DRAGON") && !strcmp(L[0].text, "LLAMA E") && !strcmp(L[8].text, "LLAMA W"), "MP70: Dragon, Llama east and west (%s, %s, %s)", L[11].text, L[0].text, L[8].text);
    mapview_door_labels(&RW, 24, L);                                   /* MP81 Milagro */
    ck(!strcmp(L[8].text, "BOSS ALGUIEN"), "MP81: Alguien (%s)", L[8].text);
    /* the after-the-boss state: exit doors and the reward */
    ck(RW.m[1].boss_room && RW.m[1].ndoors == 2 && RW.m[1].nitems == 1, "MP1D read after the boss: 2 doors, the reward (%d, %d)", RW.m[1].ndoors, RW.m[1].nitems);
    MapViewItem it[8];
    ck(mapview_items(&RW.m[1], NULL, 0, NULL, it, 8) == 1 && it[0].kind == MVI_KEY, "and the reward is a Key");
    /* MP83: the wind corridor */
    static Map m; static Tileset t;
    memset(&m, 0, sizeof m);
    if (map_load_system(&m, dir, 26) == 0 && gfx_load_tileset(&t, dir, m.tileset) == 0) {
        MapViewRun R[512];
        int n = mapview_runs(&m, &t, R, 512), left = 0, right = 0, up = 0;
        for (int i = 0; i < n; i++) {
            if (R[i].kind == MVC_LEFT && R[i].c0 == 2 && R[i].c1 == 38 && R[i].r0 == 6 && R[i].r1 == 9) left = 1;
            if (R[i].kind == MVC_RIGHT && R[i].c0 == 93 && R[i].c1 == 125 && R[i].r0 == 6 && R[i].r1 == 9) right = 1;
            if (R[i].kind == MVC_UP) up++;
        }
        ck(left && right && up > 0, "MP83: left current cols 2..38, right 93..125 (rows 6..9, the MP83 survey), %d updraft runs", up);
        int lifts = 0, plats = 0, gates = 0;
        for (int i = 0; i < m.nfix; i++) {
            MapViewSpan s;
            if (!mapview_fixture_span(&m, &m.fix[i], &s)) continue;
            lifts += s.kind == MVF_ELEVATOR; plats += s.kind == MVF_PLATFORM; gates += s.kind == MVF_GATE;
        }
        ck(plats >= 4 && lifts >= 1 && gates >= 4, "MP83 fixtures: %d lifts, %d gates, %d platforms", lifts, gates, plats);
        map_free(&m);
    } else ck(0, "MP83 loads");
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../zeliard";
    t_wrap();    printf("ok\n");
    t_layout();  printf("ok\n");
    t_mapping(); printf("ok\n");
    t_raster();  printf("ok\n");
    t_doors();   printf("ok\n");
    t_runs();    printf("ok\n");
    t_spans();   printf("ok\n");
    t_items();   printf("ok\n");
    t_legend();  printf("ok\n");
    t_real(dir); printf("ok\n");
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
