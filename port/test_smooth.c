/* test_smooth.c -- smooth scrolling's pure half (port/smooth.c + render.c's offset hooks, #127):
 * the easing, the wrap arithmetic, the per-step offsets (world, hero, enemy, shot, spell) and
 * that the renderer draws a slide from the ring's spare cells while leaving the normal render
 * byte-identical.  SDL-free, needs no game files, part of `make test`. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smooth.h"
#include "physics.h"
#include "town.h"

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

static Game *new_game(Map *m, Tileset *t)
{
    Game *g = calloc(1, sizeof *g);
    memset(m, 0, sizeof *m); m->width = 100;
    g->map = m; g->tiles = t;
    return g;
}

static void t_math(void)
{
    printf("math            ");
    ck(smooth_wrap_delta(5, 5, 100) == 0 && smooth_wrap_delta(6, 5, 100) == 1 && smooth_wrap_delta(5, 6, 100) == -1,
       "plain deltas");
    ck(smooth_wrap_delta(0, 99, 100) == 1 && smooth_wrap_delta(99, 0, 100) == -1, "a column step across the map seam");
    ck(smooth_wrap_delta(0, 63, 64) == 1 && smooth_wrap_delta(63, 0, 64) == -1, "a row step across the 64-row ring");
    ck(smooth_wrap_delta(50, 0, 100) == 50 && smooth_wrap_delta(51, 0, 100) == -49, "half the ring is the longest signed delta");
    ck(smooth_remaining(8, 0) == 8 && smooth_remaining(8, 1) == 0 && smooth_remaining(8, 0.5) == 4, "8 px: start, settled, middle");
    ck(smooth_remaining(-8, 0.5) == -4 && smooth_remaining(-8, 0.25) == -6 && smooth_remaining(8, 0.25) == 6, "symmetric for both directions");
    ck(smooth_remaining(8, -3) == 8 && smooth_remaining(8, 7) == 0, "progress is clamped");
    int mono = 1, prev = 99;
    for (int i = 0; i <= 100; i++) { int r = smooth_remaining(8, i / 100.0); if (r > prev || r < 0) mono = 0; prev = r; }
    ck(mono && prev == 0, "the slide only ever shrinks, and ends at 0");
    ck(smooth_remaining(0, 0.3) == 0, "no move, no offset");
    ck(smooth_progress(0, 84.5) == 0 && smooth_progress(84.5, 84.5) == 1 && smooth_progress(42.25, 84.5) == 0.5, "progress through a 84.5 ms step");
    ck(smooth_progress(-5, 84.5) == 0 && smooth_progress(500, 84.5) == 1 && smooth_progress(5, 0) == 1, "clamped; a zero period is settled");
    ck(smooth_progress(101.4, 101.4) == 1 && smooth_progress(50.7, 101.4) == 0.5, "the same at speed 6 (101.4 ms)");
    ck(smooth_is_slide(8, 0) && smooth_is_slide(-24, 24) && !smooth_is_slide(25, 0) && !smooth_is_slide(0, -32), "slide or jump");
}

static void t_step(void)
{
    printf("steps           ");
    static Map m; static Tileset t; Game *g = new_game(&m, &t);
    Smooth s; smooth_reset(&s);
    g->scroll_col = 50; g->scroll_row = 10; g->hero_scr_col = 12; g->hero_scr_row = 6;
    smooth_step(&s, g);
    ck(!s.start.active, "the first step has no history: nothing slides");
    g->scroll_col = 51;                                   /* the world moves left one cell, the hero stays on column 12 */
    smooth_step(&s, g);
    ck(s.start.active && s.start.wx == 8 && s.start.wy == 0 && s.start.hx == 0, "scroll +1: the world starts 8 px right of its place");
    SmoothOfs o;
    smooth_at(&s, 0, &o);
    ck(o.active && o.wx == 8, "t = 0 is the previous position");
    smooth_at(&s, 0.5, &o);
    ck(o.active && o.wx == 4, "t = 0.5 is half way");
    smooth_at(&s, 1, &o);
    ck(!o.active && o.wx == 0, "t = 1 is settled and inactive");
    smooth_at(&s, 0.97, &o);
    ck(!o.active, "a slide that has quantised to 0 px is the plain picture");
    g->scroll_col = 50;
    smooth_step(&s, g);
    ck(s.start.wx == -8, "scroll -1 slides the other way");
    g->scroll_col = 50;
    smooth_step(&s, g);
    ck(!s.start.active, "a step with no movement does not slide");

    g->scroll_col = 99; smooth_step(&s, g);              /* the seam */
    g->scroll_col = 0;  smooth_step(&s, g);
    ck(s.start.wx == 8, "scroll_col 99 -> 0 wraps at the map seam (+8)");
    g->scroll_col = 99; smooth_step(&s, g);
    ck(s.start.wx == -8, "and 0 -> 99 (-8)");
    g->scroll_row = 0; smooth_step(&s, g);
    g->scroll_row = 63; smooth_step(&s, g);
    ck(s.start.wy == -8 && s.start.wx == 0, "scroll_row 0 -> 63 wraps the 64-row ring (-8)");

    g->scroll_col = 10; smooth_step(&s, g);
    g->scroll_col = 13; smooth_step(&s, g);
    ck(!s.start.active, "a 3-cell scroll is a jump (door, teleport), not a slide");
    g->scroll_col = 14; smooth_step(&s, g);
    ck(s.start.wx == 8, "and the next step slides again");
    Map m2; memset(&m2, 0, sizeof m2); m2.width = 100;
    g->map = &m2; g->scroll_col = 15; smooth_step(&s, g);
    ck(!s.start.active, "a different map never slides from the old one");

    /* the hero where the camera does not follow him (a map edge) */
    g->hero_scr_col = 12; g->scroll_col = 20; smooth_step(&s, g);
    g->hero_scr_col = 13; smooth_step(&s, g);
    ck(s.start.hx == -8 && s.start.wx == 0 && s.start.active, "the hero stepping right on a still camera starts 8 px left");
    g->hero_scr_row = 5; smooth_step(&s, g);
    ck(s.start.hy == 8 && s.start.hx == 0, "the hero stepping up starts 8 px lower");
    free(g);
}

static void put_obj(Game *g, int i, int rcol, int row, int type)
{
    MapObj *o = &g->obj[i];
    memset(o, 0, sizeof *o);
    o->col = 40; o->rcol = (uint8_t)rcol; o->row = (uint8_t)row; o->type = (uint8_t)type;
    g->ring[game_ring_index(g, (uint8_t)row, (uint8_t)rcol)] = (uint8_t)(0x80 | i);
    if (g->nobj <= i) g->nobj = i + 1;
}

static void t_objects(void)
{
    printf("objects         ");
    static Map m; static Tileset t; Game *g = new_game(&m, &t);
    Smooth s; smooth_reset(&s);
    g->scroll_col = 50; g->scroll_row = 0; g->hero_scr_col = 12; g->hero_scr_row = 6;
    memset(g->ring, 0, sizeof g->ring);
    put_obj(g, 0, 20, 5, 7);      /* stands still in the world */
    put_obj(g, 1, 24, 8, 3);      /* walks with the hero: same screen cell */
    put_obj(g, 2, 24, 9, 3);      /* walks away: two cells further */
    put_obj(g, 3, 30, 4, 2);      /* leaps 5 cells */
    g->shots[0].col = 18; g->shots[0].row = 7;
    g->shots[1].col = 0xFF;
    smooth_step(&s, g);

    g->scroll_col = 51;           /* the world scrolls by one: a still enemy is one screen cell left */
    memset(g->ring, 0, sizeof g->ring);
    put_obj(g, 0, 19, 5, 7);
    put_obj(g, 1, 24, 8, 3);
    put_obj(g, 2, 22, 9, 3);
    put_obj(g, 3, 35, 4, 2);
    g->shots[0].col = 17; g->shots[0].row = 7;   /* a still shot rides the world (shots_shift) */
    smooth_step(&s, g);
    ck(s.start.wx == 8 && s.start.ox[0] == 8 && s.start.oy[0] == 0, "a still enemy slides with the world");
    ck(s.start.ox[1] == 0, "an enemy keeping pace with the hero does not slide against the screen");
    ck(s.start.ox[2] == 16, "an enemy walking away from the hero slides by its own screen delta (%d)", s.start.ox[2]);
    ck(s.start.ox[3] == 0 && s.start.oy[3] == 0, "a leap of more than 24 px is a jump");
    ck(s.start.sx[0] == 8, "a shot slot is matched by index and slides with the world");
    ck(s.start.ox[100] == 8 && s.start.oy[100] == 0, "an object with no history rides the world");
    SmoothOfs o;
    smooth_at(&s, 0.5, &o);
    ck(o.ox[0] == 4 && o.ox[2] == 8 && o.sx[0] == 4, "all scale together (%d %d %d)", o.ox[0], o.ox[2], o.sx[0]);

    /* a type change in the same slot is a different object */
    g->scroll_col = 51;
    memset(g->ring, 0, sizeof g->ring);
    put_obj(g, 0, 17, 5, 9);
    smooth_step(&s, g);
    ck(!s.start.active && s.start.ox[0] == 0, "same slot, new type, still world: nothing slides");
    free(g);
}

static void t_render(void)
{
    printf("render          ");
    static Map m; static Tileset t; Game *g = new_game(&m, &t);
    static uint8_t a[FB_W * FB_H], b[FB_W * FB_H];
    for (int v = 1; v < 8; v++) {
        t.present[v] = 1;
        for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) t.cell[v].px[r][c] = (uint8_t)(v * 16 + (c == 0 ? 1 : 2));
    }
    g->scroll_row = 0;
    /* ring window columns -1 .. 28 carry tile ids 1..7 repeating by column */
    for (int r = 0; r < RING_H; r++) for (int c = 0; c < RING_W; c++) g->bg_ring[r * RING_W + c] = (uint8_t)(1 + (c % 7));
    render_frame(a, g, NULL);
    SmoothOfs z; memset(&z, 0, sizeof z);
    render_set_smooth(&z);                               /* inactive offsets are the engine path */
    render_frame(b, g, NULL);
    render_set_smooth(NULL);
    ck(memcmp(a, b, sizeof a) == 0, "inactive offsets render byte-identically");

    SmoothOfs o; memset(&o, 0, sizeof o); o.active = 1; o.wx = 8;
    render_set_smooth(&o); render_frame(b, g, NULL); render_set_smooth(NULL);
    int ok = 1;
    for (int y = PF_Y; y < PF_Y + PF_H; y++)
        for (int x = PF_X + 8; x < PF_X + PF_W; x++) if (b[y * FB_W + x] != a[y * FB_W + x - 8]) ok = 0;
    ck(ok, "wx = +8: the picture is the plain one moved 8 px right");
    int rev = 1;
    for (int y = PF_Y; y < PF_Y + PF_H; y++)
        for (int x = PF_X; x < PF_X + 8; x++)
            if (b[y * FB_W + x] != t.cell[1 + ((3) % 7)].px[0][x - PF_X]) rev = 0;   /* ring column 3 = screen column -1 */
    ck(rev, "the revealed column is the ring cell beyond the window");
    int leak = 0;
    for (int y = 0; y < FB_H; y++) { for (int x = 0; x < PF_X; x++) leak |= b[y * FB_W + x]; for (int x = PF_X + PF_W; x < FB_W; x++) leak |= b[y * FB_W + x]; }
    ck(!leak, "nothing leaks out of the playfield: the frame and HUD columns stay put");

    memset(&o, 0, sizeof o); o.active = 1; o.wx = -3;
    render_set_smooth(&o); render_frame(b, g, NULL); render_set_smooth(NULL);
    ok = 1;
    for (int y = PF_Y; y < PF_Y + PF_H; y++)
        for (int x = PF_X; x < PF_X + PF_W - 3; x++) if (b[y * FB_W + x] != a[y * FB_W + x + 3]) ok = 0;
    ck(ok, "wx = -3 (a partial slide the other way)");
    rev = 1;
    for (int y = PF_Y; y < PF_Y + PF_H; y++)
        for (int x = PF_X + PF_W - 3; x < PF_X + PF_W; x++)
            if (b[y * FB_W + x] != t.cell[1 + ((32) % 7)].px[0][x - (PF_X + PF_W - 3)]) rev = 0;   /* ring column 32 = screen column 28 */
    ck(rev, "...revealing ring column 32 on the right");

    memset(&o, 0, sizeof o); o.active = 1; o.wy = 8;
    render_set_smooth(&o); render_frame(b, g, NULL); render_set_smooth(NULL);
    ok = 1;
    for (int y = PF_Y + 8; y < PF_Y + PF_H; y++)
        for (int x = PF_X; x < PF_X + PF_W; x++) if (b[y * FB_W + x] != a[(y - 8) * FB_W + x]) ok = 0;
    ck(ok, "wy = +8: moved 8 px down, the row above comes from the wrapped ring");
    int above = 0;
    for (int y = 0; y < PF_Y; y++) for (int x = 0; x < FB_W; x++) above |= b[y * FB_W + x];
    ck(!above, "and nothing is drawn above the playfield");
    free(g);
}

/* ------------------------------------------------------------------ towns */
static TownMap TM; static TownTiles TT; static TownSprites TS; static TownHero TH; static TownBackdrop TB; static Town TW;

static void town_setup(void)
{
    memset(&TM, 0, sizeof TM); memset(&TT, 0, sizeof TT); memset(&TS, 0, sizeof TS); memset(&TH, 0, sizeof TH);
    memset(&TB, 0, sizeof TB); memset(&TW, 0, sizeof TW);
    TM.width = 100; TM.index = 1;
    for (int v = 1; v < 8; v++) {
        TT.present[v] = 1;
        for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) TT.cell[v].px[r][c] = (uint8_t)(v * 16 + (c == 0 ? 1 : 2));
    }
    for (int c = 0; c < TM.width; c++) for (int r = 0; r < TOWN_ROWS; r++) TM.grid[c][r] = (uint8_t)(1 + (c % 7));
    /* backdrop strips with a different value per column so their motion can be read off */
    TB.loaded = 1; TB.has_far = 1; TB.back_y0 = 14; TB.back_y1 = 78;
    for (int x = 0; x < 112; x++)
        for (int r = 0; r < 16; r++) {
            TB.far14[r][x] = (uint8_t)(x + 1);
            if (r < 8) { TB.near142[r][x] = (uint8_t)(x + 1); TB.near150[r][x] = (uint8_t)(x + 1); }
        }
    TM.nnpcs = 1;
    TM.npcs[0].col = 20; TM.npcs[0].sprite = 0x80; TM.npcs[0].saved = 1; TM.npcs[0].type = 1;
    TM.grid[20][TOWN_NPC_ROW] = TOWN_MARK;
    TS.ncells = 1; TS.frame[0][4][0] = 1; TS.frame[0][0][0] = 1;
    for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) { TS.cell[0].px[r][c] = 200; TS.cell[0].mask[r][c] = 1; }
    TW.map = &TM; TW.tiles = &TT; TW.spr = &TS; TW.hero = &TH; TW.back = &TB;
    TW.scroll_col = 10; TW.hero_scr_col = 12; TW.back_steps = 3;
}

static void t_town_steps(void)
{
    printf("town steps      ");
    town_setup();
    SmoothTown s; smooth_town_reset(&s);
    smooth_town_step(&s, &TW);
    ck(!s.start.active, "no history: nothing slides");
    TW.scroll_col = 11; TW.back_steps = 4;               /* walking right: the camera follows the hero */
    smooth_town_step(&s, &TW);
    ck(s.start.active && s.start.wx == 8 && s.start.hx == 0, "a scroll step: the tiles were 8 px to the right, the hero stays");
    ck(s.start.fx[0] == 4 && s.start.fx[1] == 8 && s.start.fx[2] == 16, "each parallax layer slides by its own step: 4 / 8 / 16 px");
    ck(s.start.nx[0] == 8, "a still NPC rides the map: it was 8 px to the right on screen");
    TW.scroll_col = 10; TW.back_steps = 3;               /* walking left */
    smooth_town_step(&s, &TW);
    ck(s.start.wx == -8 && s.start.fx[0] == -4 && s.start.fx[1] == -8 && s.start.fx[2] == -16 && s.start.nx[0] == -8, "and the other way");
    TW.scroll_col = 10; TW.hero_scr_col = 13;            /* the hero walks across the screen, no scroll */
    smooth_town_step(&s, &TW);
    ck(s.start.hx == -8 && s.start.wx == 0 && s.start.fx[1] == 0 && s.start.nx[0] == 0, "the hero alone: his own cell delta, the world and layers still");
    TW.hero_scr_col = 20;                                /* a door: a jump */
    smooth_town_step(&s, &TW);
    ck(s.start.hx == 0, "a jump of more than 3 cells is not slid");
    TW.scroll_col = 30; TW.back_steps = 40;              /* a whole new place */
    smooth_town_step(&s, &TW);
    ck(s.start.wx == 0 && s.start.fx[2] == 0 && s.start.nx[0] == 0, "a scroll or phase jump is not slid");
    TW.scroll_col = 10; TW.back_steps = 3;
    smooth_town_step(&s, &TW);
    TM.npcs[0].col = 21; TM.grid[20][TOWN_NPC_ROW] = 1; TM.grid[21][TOWN_NPC_ROW] = TOWN_MARK;   /* the NPC walks right a cell */
    smooth_town_step(&s, &TW);
    TM.npcs[0].col = 22; TM.grid[21][TOWN_NPC_ROW] = 1; TM.grid[22][TOWN_NPC_ROW] = TOWN_MARK;
    smooth_town_step(&s, &TW);
    ck(s.start.nx[0] == -8 && s.start.wx == 0, "an NPC that walks is matched by index and slides by its own move");
    TW.scroll_col = 11; TW.back_steps = 4; TM.npcs[0].col = 23; TM.grid[22][TOWN_NPC_ROW] = 1; TM.grid[23][TOWN_NPC_ROW] = TOWN_MARK;
    smooth_town_step(&s, &TW);
    ck(s.start.wx == 8 && s.start.nx[0] == 0, "walking with the scroll: the NPC's own and the map's slides cancel on screen (it kept its cell)");
    TM.grid[23][TOWN_NPC_ROW] = 1;                       /* the marker lifted (a door frame) */
    smooth_town_step(&s, &TW);
    ck(s.start.nx[0] == 0, "an NPC that is not drawn has no offset");
    TM.index = 2;
    smooth_town_step(&s, &TW);
    ck(!s.start.active, "a different town forgets the history");
    SmoothOfs h; smooth_ofs_at(&s.start, 0.5, &h);
    ck(!h.active, "an idle step stays idle");
    TW.scroll_col = 12; TW.back_steps = 5;
    smooth_town_step(&s, &TW);
    smooth_ofs_at(&s.start, 0.5, &h);
    ck(h.active && h.wx == 4 && h.fx[0] == 2 && h.fx[1] == 4 && h.fx[2] == 8, "halfway: every layer half way, each by its own amount");
    smooth_ofs_at(&s.start, 1, &h);
    ck(!h.active && h.fx[2] == 0, "settled at t = 1");
}

static int strip_phase(const uint8_t *fb, int y)       /* the value at the left edge of the window: x + 1 shifted */
{
    return fb[y * FB_W + 48];
}

static void t_town_render(void)
{
    printf("town render     ");
    town_setup();
    static uint8_t a[FB_W * FB_H], b[FB_W * FB_H];
    TW.scroll_col = 10; TW.back_steps = 3;
    town_render(a, &TW);
    SmoothOfs z; memset(&z, 0, sizeof z);
    town_set_smooth(&z); town_render(b, &TW); town_set_smooth(NULL);
    ck(memcmp(a, b, sizeof a) == 0, "inactive offsets render byte-identically");

    /* the plain picture at the next step, for comparing a slide's end to */
    SmoothOfs o; memset(&o, 0, sizeof o); o.active = 1; o.wx = 8; o.fx[0] = 4; o.fx[1] = 8; o.fx[2] = 16; o.nx[0] = 8;
    town_set_smooth(&o); town_render(b, &TW); town_set_smooth(NULL);
    int ok = 1;
    for (int y = 78; y < 142; y++) for (int x = 48 + 8; x < 48 + 224; x++) {
        if (y >= 78 + 5 * 8 && y < 78 + 8 * 8) continue;        /* the NPC row: its sprite moves too */
        if (b[y * FB_W + x] != a[y * FB_W + x - 8]) ok = 0;
    }
    ck(ok, "wx = +8: the map tiles are the plain picture moved 8 px right");
    int rev = 1;
    for (int y = 78; y < 78 + 40; y++) for (int x = 48; x < 56; x++)
        if (b[y * FB_W + x] != TT.cell[TM.grid[TW.scroll_col + 3][0]].px[0][x - 48 + 0] && y == 78) rev = 0;   /* map column scroll_col + 3 */
    ck(rev, "the revealed column is the map column just before the window");
    /* the layers: with fx the strip phase differs from the plain one by exactly that many px */
    int p0 = strip_phase(a, 14), p1 = strip_phase(b, 14);
    ck(((p0 - p1) % 112 + 112) % 112 == 4 || ((p1 - p0) % 112 + 112) % 112 == 4, "the far strip is displaced 4 px (%d vs %d)", p0, p1);
    p0 = strip_phase(a, 142); p1 = strip_phase(b, 142);
    ck(((p1 - p0) % 112 + 112) % 112 == 104 || ((p0 - p1) % 112 + 112) % 112 == 8, "the middle strip is displaced 8 px (%d vs %d)", p0, p1);
    p0 = strip_phase(a, 150); p1 = strip_phase(b, 150);
    ck(((p0 - p1) % 112 + 112) % 112 == 16 || ((p1 - p0) % 112 + 112) % 112 == 16, "the near strip is displaced 16 px (%d vs %d)", p0, p1);
    int leak = 0;
    for (int y = 0; y < FB_H; y++) { for (int x = 0; x < 48; x++) leak |= b[y * FB_W + x]; for (int x = 48 + 224; x < FB_W; x++) leak |= b[y * FB_W + x]; }
    ck(!leak, "nothing leaks out of the window: the frame and HUD columns stay put");
    /* NPC at screen col 20-(10+4)=6 -> x 48+48=96; +8 px */
    int sx = 48 + 6 * 8 + 8, sy = 78 + 5 * 8;
    ck(b[sy * FB_W + sx] == 200 && b[sy * FB_W + sx + 7] == 200 && b[sy * FB_W + sx - 1] != 200, "the NPC sprite is drawn at its offset");
    ok = 1;
    memset(&o, 0, sizeof o); o.active = 1; o.hx = 8;
    town_set_smooth(&o); town_render(b, &TW); town_set_smooth(NULL);
    for (int y = 78; y < 142; y++) for (int x = 48; x < 48 + 224; x++) {
        int hx0 = 48 + TW.hero_scr_col * 8;                    /* the hero's two columns: only these may differ */
        if (x >= hx0 - 0 && x < hx0 + 24) continue;
        if (b[y * FB_W + x] != a[y * FB_W + x]) ok = 0;
    }
    ck(ok, "a hero offset changes only the hero");
}

int main(void)
{
    t_math();    printf("ok\n");
    t_step();    printf("ok\n");
    t_objects(); printf("ok\n");
    t_render();  printf("ok\n");
    t_town_steps();  printf("ok\n");
    t_town_render(); printf("ok\n");
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
