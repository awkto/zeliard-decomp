/* test_smooth.c -- smooth scrolling's pure half (port/smooth.c + render.c's offset hooks, #127):
 * the easing, the wrap arithmetic, the per-step offsets (world, hero, enemy, shot, spell) and
 * that the renderer draws a slide from the ring's spare cells while leaving the normal render
 * byte-identical.  SDL-free, needs no game files, part of `make test`. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smooth.h"
#include "physics.h"
#include "town.h"
#include "enemy.h"

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

/* vsync-locked pacing (#131) */
static void t_vsync(void)
{
    printf("vsync pacing    ");
    SmoothVsync v; smooth_vsync_init(&v);
    ck(v.period > 16.6 && v.period < 16.7, "assumes 60 Hz until measured");
    for (int i = 0; i < 40; i++) smooth_vsync_update(&v, 1000 + i * 8.3333);
    ck(v.period > 8.2 && v.period < 8.5, "a 120 Hz display is learned (%.2f)", v.period);
    smooth_vsync_init(&v);
    for (int i = 0; i < 60; i++) smooth_vsync_update(&v, 500 + i * 16.667 + (i % 3 ? 0.4 : -0.3));
    ck(v.period > 16.5 && v.period < 16.85, "60 Hz with a little jitter (%.3f)", v.period);
    double p = v.period;
    smooth_vsync_update(&v, v.last + 3 * 16.667);       /* two frames skipped: counts as 3 frames, not a 50 ms period */
    ck(v.period > 16.4 && v.period < 16.9, "a skipped frame does not change the period (%.3f)", v.period);
    smooth_vsync_update(&v, v.last + 700);               /* a stall (a tab hidden) is ignored */
    ck(fabs(v.period - p) < 0.5, "a long stall is ignored");
    smooth_vsync_update(&v, v.last - 5);                 /* time never goes back */
    ck(fabs(v.period - p) < 0.5, "a frame time that goes back is ignored");

    /* speed 6 is 67.6 ms a step: 4.056 frames.  The step is begun by the first frame at or past the deadline,
     * so it lasts 4 frames, and one in about 18 lasts 5; never 3 or 6. */
    const double V = 1000.0 / 60, P = 67.6, E = 2.0;
    int hist[8] = {0}, agree = 1;
    for (int ph = 0; ph < 100; ph++) {                    /* the deadline's phase against the frame grid, every 1/100 of a frame */
        double ts0 = 100;                                 /* a step begun by frame ts0 ... */
        double deadline = ts0 + E - V * ph / 100.0 + P;   /* ... whose own deadline fell up to a frame (less eps) before it */
        int k = smooth_frames_to(deadline, ts0, V, E);
        hist[k < 8 ? k : 7]++;
        if (!smooth_frame_due(deadline, ts0 + k * V, E) || smooth_frame_due(deadline, ts0 + (k - 1) * V, E)) agree = 0;
    }
    ck(agree, "K frames: the step is due at the Kth frame and not the one before, at every phase");
    ck(hist[4] + hist[5] == 100 && hist[5] >= 4 && hist[5] <= 8, "a 67.6 ms step is 4 frames, sometimes 5 (4:%d 5:%d)", hist[4], hist[5]);
    ck(smooth_frames_to(0, 100, V, E) == 1 && smooth_frames_to(100, 100, V, E) == 1, "a step already due lasts one frame, never zero");
    ck(smooth_frames_to(100 + V, 100, V, E) == 1 && smooth_frames_to(100 + V + 2.5, 100, V, E) == 2, "the deadline just past a frame waits for the next");
    ck(smooth_frame_due(100, 98.5, E) && !smooth_frame_due(100, 97.5, E), "due 2 ms early counts");

    /* the slide: even, and the last picture is one frame's worth from the target, so the next step's first picture
     * (the previous target) is where the slide was going: no jump, no doubled picture */
    double prev = 0; int even = 1, px[8];
    for (int f = 0; f < 4; f++) {
        double t = smooth_frame_progress(100 + f * V, 100, 4, V);
        px[f] = smooth_remaining(8, t);
        if (f && fabs((t - prev) - 0.25) > 1e-9) even = 0;
        prev = t;
    }
    ck(even && px[0] == 8 && px[1] == 6 && px[2] == 4 && px[3] == 2, "4 frames: 8, 6, 4, 2 px to go, then the next step");
    ck(smooth_frame_progress(100 + 4 * V, 100, 4, V) == 1 && smooth_frame_progress(100 + 9 * V, 100, 4, V) == 1, "the slide is done at frame K, and stays done");
    ck(smooth_remaining(8, smooth_frame_progress(100 + 3 * V, 100, 5, V)) == 3 && smooth_remaining(8, smooth_frame_progress(100 + 4 * V, 100, 5, V)) == 2, "5 frames: ..., 3, 2");
    ck(smooth_remaining(24, smooth_frame_progress(100 + 3 * V, 100, 4, V)) == 6, "a 3-cell camera move leaves 6 of 24 px at the last frame");
    ck(smooth_frame_progress(90, 100, 4, V) == 0 && smooth_frame_progress(100, 100, 0, V) == 1 && smooth_frame_progress(100, 100, 4, 0) == 1, "before the start 0; no frames or period: settled");
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
    ck(s.start.active && s.start.wx == 24, "a 3-cell scroll (the camera re-centring on a turn) is a slide, not a jump (#131)");
    g->scroll_col = 11; smooth_step(&s, g);
    ck(s.start.wx == -16, "a 2-cell scroll back slides too");
    g->scroll_col = 15; smooth_step(&s, g);
    ck(!s.start.active && s.start.wx == 0, "a 4-cell scroll is a jump (door, teleport), not a slide");
    g->scroll_col = 16; smooth_step(&s, g);
    ck(s.start.wx == 8, "and the next step slides again");
    g->scroll_row = 10; smooth_step(&s, g);
    g->scroll_row = 13; smooth_step(&s, g);
    ck(s.start.wy == 24 && s.start.wx == 0, "a 3-row scroll (a fast fall) slides");
    g->scroll_row = 18; smooth_step(&s, g);
    ck(s.start.wy == 0, "a 5-row scroll is a jump");
    g->scroll_row = 10; g->scroll_col = 16; smooth_step(&s, g);
    g->scroll_col = 15;
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

/* ---------------------------------------------------------------- fixtures */
static void put_fix(Game *g, int i, int col, int row, int cell, int kind)
{
    Fixture *f = &g->fix[i];
    memset(f, 0, sizeof *f);
    f->col = (uint16_t)col; f->row = (uint8_t)row; f->cell = (uint8_t)cell; f->kind = (uint8_t)kind;
    if (g->nfix <= i) g->nfix = i + 1;
}

static void t_fixtures(void)
{
    printf("fixtures        ");
    static Map m; static Tileset t; Game *g = new_game(&m, &t);
    ck(smooth_fixture_rcol(60, 50, 100) == 10 && smooth_fixture_rcol(5, 90, 100) == 15 && smooth_fixture_rcol(84, 50, 100) == -1 &&
       smooth_fixture_rcol(83, 50, 100) == 33 && smooth_fixture_rcol(49, 50, 100) == -1, "the ring column of a fixture, over the seam, and off the ring");

    /* an elevator the hero rides: it and the scroll both move a row, so it stays put on screen */
    Smooth s; smooth_reset(&s);
    g->scroll_col = 50; g->scroll_row = 10; g->hero_scr_col = 12; g->hero_scr_row = 6;
    put_fix(g, 0, 60, 19, 0x40, 0);      /* A elevator under the hero */
    put_fix(g, 1, 70, 19, 0x43, 1);      /* a B gate nobody rides */
    put_fix(g, 2, 55, 18, 0x46, 2);      /* a C platform that moves right a column a step */
    put_fix(g, 3, 140, 18, 0x46, 2);     /* a platform off the ring */
    smooth_step(&s, g);
    ck(!s.start.active, "the first step has no history");
    g->scroll_row = 11; g->fix[0].row = 20; g->fix[1].row = 19; g->fix[2].col = 56;
    smooth_step(&s, g);
    ck(s.start.wy == 8 && s.start.hy == 0, "the world scrolls down a row, the hero holds his screen row");
    ck(s.start.kx[0] == 0 && s.start.ky[0] == 0, "an elevator sinking with the scroll keeps its screen place (%d,%d)", s.start.kx[0], s.start.ky[0]);
    ck(s.start.kx[1] == 0 && s.start.ky[1] == 8, "a gate that did not move slides with the world (%d,%d)", s.start.kx[1], s.start.ky[1]);
    ck(s.start.kx[2] == -8 && s.start.ky[2] == 8, "a platform moving right while the scroll holds slides on its own (%d,%d)", s.start.kx[2], s.start.ky[2]);
    ck(s.start.kx[3] == 0 && s.start.ky[3] == 8, "an off-ring platform has no position: it rides the world");
    ck(s.start.active, "a slide is active");
    SmoothOfs o;
    smooth_at(&s, 0.5, &o);
    ck(o.ky[0] == 0 && o.ky[1] == 4 && o.kx[2] == -4, "halfway: the lift holds, the gate is half way");
    smooth_at(&s, 1, &o);
    ck(!o.active && !o.ky[1], "settled");
    /* the way up, and the ring's wrap: the scroll row crosses 0 */
    g->scroll_row = 0; g->fix[0].row = 9;
    smooth_step(&s, g);
    g->scroll_row = 63; g->fix[0].row = 8;
    smooth_step(&s, g);
    ck(s.start.wy == -8 && s.start.ky[0] == 0, "a lift rising across the ring's wrap keeps its place (wy %d, ky %d)", s.start.wy, s.start.ky[0]);
    /* a fixture changing record index space (a new map) forgets */
    static Map m2; m2.width = 100; g->map = &m2;
    smooth_step(&s, g);
    ck(!s.start.active && s.start.ky[0] == 0, "a new map has no history");

    /* rendering: the fixture's cells are drawn at their own offset, the tiles at the world's */
    memset(&t, 0, sizeof t); g->map = &m;
    for (int v = 1; v < 0x49; v++) {
        if (v > 1 && v < 0x40) continue;
        t.present[v] = 1;
        for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) t.cell[v].px[r][c] = (uint8_t)(v == 1 ? 3 : 0x80 + v + (c == 0 ? 1 : 0) + (r == 0 ? 2 : 0));
    }
    g->scroll_col = 50; g->scroll_row = 5;
    memset(g->ring, 0, sizeof g->ring);
    for (int i = 0; i < RING_SIZE; i++) g->bg_ring[i] = (i / RING_W) % 2 ? 1 : 0;   /* striped rows: every second tile row */
    g->nfix = 0;
    put_fix(g, 0, 60, 10, 0x40, 0);                                   /* ring col 10..12, screen cell 6..8, row 5 */
    for (int k = 0; k < 3; k++) g->bg_ring[10 * RING_W + 10 + k] = (uint8_t)(0x40 + k);
    static uint8_t a[FB_W * FB_H], b[FB_W * FB_H];
    render_frame(a, g, NULL);
    int fy = PF_Y + 5 * 8, fx = PF_X + 6 * 8;
    ck(a[fy * FB_W + fx] != 3 && a[fy * FB_W + fx] != 0, "the plain frame draws the elevator");
    SmoothOfs w; memset(&w, 0, sizeof w); w.active = 1; w.wy = 8; w.ky[0] = 0;
    render_set_smooth(&w); render_frame(b, g, NULL); render_set_smooth(NULL);
    int ok = 1;
    for (int y = 0; y < 8; y++) for (int x = 0; x < 24; x++) if (b[(fy + y) * FB_W + fx + x] != a[(fy + y) * FB_W + fx + x]) ok = 0;
    ck(ok, "wy = +8 with the lift's own ky = 0: the lift stays where it was");
    int ok2 = 1;
    for (int y = 16; y < PF_H; y++) for (int x = 0; x < PF_W; x++) {
        int yy = PF_Y + y, xx = PF_X + x;
        if (yy >= fy && yy < fy + 8 && x >= 6 * 8 && x < 9 * 8) continue;
        if (b[yy * FB_W + xx] != a[(yy - 8) * FB_W + xx] && !(yy - 8 >= fy && yy - 8 < fy + 8 && x >= 6 * 8 && x < 9 * 8)) ok2 = 0;
    }
    ck(ok2, "...and the scenery moves 8 px, the lift's old cells left empty");
    memset(&w, 0, sizeof w); w.active = 1; w.wy = 8; w.ky[0] = 8;
    render_set_smooth(&w); render_frame(b, g, NULL); render_set_smooth(NULL);
    ok = 1;
    for (int y = 0; y < 8; y++) for (int x = 0; x < 24; x++) if (b[(fy + 8 + y) * FB_W + fx + x] != a[(fy + y) * FB_W + fx + x]) ok = 0;
    ck(ok, "ky = +8: the lift moves with the world");
    memset(&w, 0, sizeof w); w.active = 1; w.wx = 8; w.kx[0] = 0;
    render_set_smooth(&w); render_frame(b, g, NULL); render_set_smooth(NULL);
    ok = 1;
    for (int y = 0; y < 8; y++) for (int x = 0; x < 24; x++) if (b[(fy + y) * FB_W + fx + x] != a[(fy + y) * FB_W + fx + x]) ok = 0;
    ck(ok, "a platform keeping its screen column while the world slides sideways");
    /* a lift half hidden under a sprite marker still draws */
    g->bg_ring[10 * RING_W + 11] = 0x80 | 3; g->nobj = 4; g->under_sprite[3] = 0x41;
    render_frame(a, g, NULL);
    memset(&w, 0, sizeof w); w.active = 1; w.wy = 8;
    render_set_smooth(&w); render_frame(b, g, NULL); render_set_smooth(NULL);
    ok = 1;
    for (int y = 0; y < 8; y++) for (int x = 0; x < 24; x++) if (b[(fy + y) * FB_W + fx + x] != a[(fy + y) * FB_W + fx + x]) ok = 0;
    ck(ok, "a lift cell under a sprite marker is still drawn at the lift's offset");
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

/* ------------------------------------------------------------- the wide view (#126) */
static ScreenFrame SF;

static void t_wide(void)
{
    printf("wide view       ");
    static Map m; static Tileset t; Game *g = new_game(&m, &t);
    static uint8_t a[FB_W * FB_H], w[RENDER_WIDE_MAX_W * RENDER_WIDE_MAX_H];
    const RenderView rv = RENDER_VIEW_WIDE;
    const int W = render_view_width(&rv);
    ck(W == 384 && render_view_width(NULL) == 320, "the full-ring view is 384 wide, the stock one 320");
    for (int v = 1; v < 8; v++) {
        t.present[v] = 1;
        for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) t.cell[v].px[r][c] = (uint8_t)(v * 16 + (c == 0 ? 1 : 2));
    }
    g->scroll_row = 0;
    for (int r = 0; r < RING_H; r++) for (int c = 0; c < RING_W; c++) g->bg_ring[r * RING_W + c] = (uint8_t)(1 + (c % 7));

    /* the playfield: ring column c is at x 48 + 8c of the wide screen; the stock 28 columns are 4..31 */
    render_frame(a, g, NULL);
    ck(render_frame_view(w, g, NULL, &rv) == 1, "a plain cavern frame is drawn wide");
    int ok = 1;
    for (int y = PF_Y; y < PF_Y + PF_H; y++)
        for (int x = 0; x < 28 * 8; x++) if (w[y * W + PF_X + 32 + x] != a[y * FB_W + PF_X + x]) ok = 0;
    ck(ok, "the stock 28 columns are the middle of the wide playfield, 32 px in");
    int margins = 1;
    for (int y = PF_Y; y < PF_Y + PF_H; y += 8)
        for (int c = 0; c < 36; c++) {
            int v = 1 + (c % 7);
            if (w[y * W + PF_X + c * 8 + 3] != (uint8_t)(v * 16 + 2) || w[y * W + PF_X + c * 8] != (uint8_t)(v * 16 + 1)) margins = 0;
        }
    ck(margins, "all 36 ring columns are drawn, margins 0..3 and 32..35 included, each at its own cell");
    int leak = 0;
    for (int y = 0; y < FB_H; y++) { for (int x = 0; x < PF_X; x++) leak |= w[y * W + x]; for (int x = PF_X + 288; x < W; x++) leak |= w[y * W + x]; }
    ck(!leak, "nothing leaks out of the playfield into the stone columns");

    /* a plain 0/0 view is the stock render */
    const RenderView rs = { 0, 0, 0, 0 };
    static uint8_t b[FB_W * FB_H];
    ck(render_frame_view(b, g, NULL, &rs) == 1 && !memcmp(a, b, sizeof a), "a 0/0 view is render_frame, byte for byte");
    const RenderView bad = { RENDER_VIEW_MAX_COLS + 1, 0, 0, 0 };
    ck(render_frame_view(b, g, NULL, &bad) == 0, "more columns than the view limit are refused");

    /* which frames are wide */
    ck(render_wide_ok(g), "a cavern frame is wide");
    g->boss_map = 1; ck(!render_wide_ok(g) && render_frame_view(w, g, NULL, &rv) == 0, "a boss room is not (stays 28 columns)"); g->boss_map = 0;
    g->walk_in = 1; ck(!render_wide_ok(g), "nor the walk-in"); g->walk_in = 0;
    g->encounter_frames = 3; ck(!render_wide_ok(g), "nor the encounter card"); g->encounter_frames = 0;
    g->dissolve = 2; ck(!render_wide_ok(g), "nor the death dissolve"); g->dissolve = 0;

    /* a shot in a margin column is drawn there (the stock view clips it) */
    memset(g->shots, 0, sizeof g->shots);
    g->shots[0].col = 1; g->shots[0].row = 3; g->shots[0].cell = 9; g->shots[1].col = 0xFF;
    t.present[9] = 1;
    for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) t.cell[9].px[r][c] = 0xEE;
    render_frame_view(w, g, NULL, &rv);
    ck(w[(PF_Y + 3 * 8 + 2) * W + PF_X + 1 * 8 + 2] == 0xEE, "a shot at ring column 1 is drawn in the left margin");
    render_frame(a, g, NULL);
    int seen = 0;
    for (int y = 0; y < FB_H; y++) for (int x = 0; x < FB_W; x++) seen |= a[y * FB_W + x] == 0xEE;
    ck(!seen, "and the stock view does not draw it");
    g->shots[0].col = 34;
    render_frame_view(w, g, NULL, &rv);
    ck(w[(PF_Y + 3 * 8 + 2) * W + PF_X + 34 * 8 + 2] == 0xEE, "a shot at ring column 34 in the right margin");
    memset(g->shots, 0, sizeof g->shots); g->shots[0].col = 0xFF;

    /* a smooth slide at the edge of a view that is the whole ring shows no wrapped cell from the next row */
    SmoothOfs o; memset(&o, 0, sizeof o); o.active = 1; o.wx = 8;
    for (int c = 0; c < 100; c++) for (int r = 0; r < MAP_ROWS; r++) m.grid[c][r] = (uint8_t)(1 + (c % 7));   /* the map's own columns */
    g->scroll_col = 5;
    render_frame_view(w, g, NULL, &rv);
    static uint8_t w0[RENDER_WIDE_MAX_W * RENDER_WIDE_MAX_H];
    memcpy(w0, w, sizeof w0);
    render_set_smooth(&o); render_frame_view(w, g, NULL, &rv); render_set_smooth(NULL);
    ok = 1; int rev = 1;
    for (int y = PF_Y; y < PF_Y + PF_H; y++) {
        for (int x = PF_X + 8; x < PF_X + 288; x++) if (w[y * W + x] != w0[y * W + x - 8]) ok = 0;
        for (int x = PF_X; x < PF_X + 8; x++) if (w[y * W + x] != t.cell[1 + (4 % 7)].px[0][x - PF_X]) rev = 0;   /* map col scroll_col - 1 = 4 */
    }
    ck(ok, "wx = +8 in the wide view: the picture moved 8 px right");
    ck(rev, "...and the column beyond the ring is the map's own column (scroll_col - 1), not the neighbouring row's cell");
    g->scroll_row = 0;
    leak = 0;
    for (int y = 0; y < FB_H; y++) for (int x = PF_X + 288; x < W; x++) leak |= w[y * W + x];
    ck(!leak, "...nothing spills past the right edge");
    memset(&o, 0, sizeof o); o.active = 1; o.wx = -8;
    render_set_smooth(&o); render_frame_view(w, g, NULL, &rv); render_set_smooth(NULL);
    rev = 1;
    for (int y = PF_Y; y < PF_Y + PF_H; y++) for (int x = PF_X + 280; x < PF_X + 288; x++)
        if (w[y * W + x] != t.cell[1 + (41 % 7)].px[0][x - PF_X - 280]) rev = 0;                  /* map col scroll_col + 36 = 41 */
    ck(rev, "wx = -8: the right edge shows the map's column scroll_col + 36");

    /* the message box is centred on the playfield: its rows are the only ones that change */
    render_frame_view(w0, g, NULL, &rv);
    g->msg_box = 1; snprintf(g->message, sizeof g->message, "HELLO");
    render_frame_view(w, g, NULL, &rv);
    int inside = 1, changed = 0;
    for (int y = 0; y < FB_H; y++) for (int x = 0; x < W; x++)
        if (w[y * W + x] != w0[y * W + x]) {
            changed++;
            if (y < 30 || y >= 46 || x < 56 + 32 || x >= 56 + 32 + 208) inside = 0;
        }
    ck(changed > 100 && inside, "the 208x16 message box is centred on the wide playfield (x 88..295), nothing else changes");
    g->msg_box = 0;

    /* the furniture: stone sides at the edges, the strip above tiled, the HUD panel centred */
    memset(&SF, 0, sizeof SF); SF.loaded = 1;
    for (int y = 0; y < FB_H; y++) for (int x = 0; x < FB_W; x++) {
        int on = x < 48 || x >= 272 || (x >= 48 && x < 272 && (y < 13 || y >= 158));
        if (on) { SF.on[y * FB_W + x] = 1; SF.px[y * FB_W + x] = (uint8_t)(1 + (x * 7 + y) % 60); }
    }
    const uint8_t *fp, *fo;
    render_wide_frame(&SF, 384, FB_H, &fp, &fo);
    ok = 1;
    for (int y = 0; y < FB_H; y++) for (int x = 0; x < 48; x++)
        if (fo[y * 384 + x] != 1 || fp[y * 384 + x] != SF.px[y * FB_W + x]) ok = 0;
    ck(ok, "the left stone is the original 48 px, unchanged");
    ok = 1;
    for (int y = 0; y < FB_H; y++) for (int x = 0; x < 48; x++)
        if (fo[y * 384 + 336 + x] != 1 || fp[y * 384 + 336 + x] != SF.px[y * FB_W + 272 + x]) ok = 0;
    ck(ok, "the right stone is the original 48 px at the new right edge");
    ok = 1;
    for (int y = PF_Y; y < 158; y++) for (int x = 48; x < 336; x++) if (fo[y * 384 + x]) ok = 0;
    ck(ok, "the playfield rows carry no furniture");
    ok = 1;
    for (int y = 158; y < FB_H; y++) for (int x = 0; x < 224; x++)
        if (fp[y * 384 + 80 + x] != SF.px[y * FB_W + 48 + x]) ok = 0;
    ck(ok, "the HUD panel keeps its size, centred (x 80..303)");
    ok = 1;
    for (int y = 158; y < FB_H; y++) for (int x = 48; x < 80; x++)
        if (!fo[y * 384 + x] || fp[y * 384 + x] != SF.px[y * FB_W + 48]) ok = 0;
    for (int y = 158; y < FB_H; y++) for (int x = 304; x < 336; x++)
        if (!fo[y * 384 + x] || fp[y * 384 + x] != SF.px[y * FB_W + 271]) ok = 0;
    ck(ok, "...its edge columns repeated out to the stone");
    ok = 1;
    for (int y = 0; y < 13; y++) for (int x = 48; x < 336; x++)
        if (!fo[y * 384 + x] || fp[y * 384 + x] != SF.px[y * FB_W + 48 + ((x - 48 - 32) % 224 + 224) % 224]) ok = 0;
    ck(ok, "the strip above is the 224 px strip tiled, in phase with the centred copy");
    ok = 1;
    render_wide_frame(&SF, 320, FB_H, &fp, &fo);                              /* a 320 layout is the original */
    for (int i = 0; i < FB_W * FB_H; i++)
        if (fo[i] != SF.on[i] || (fo[i] && fp[i] != SF.px[i])) ok = 0;
    ck(ok, "width 320 reproduces the original furniture");
    /* the HUD band composition */
    static uint8_t h320[FB_W * FB_H]; memset(h320, 0x33, sizeof h320);
    memset(w, 0, sizeof w);
    render_wide_hud(w, 384, FB_H, h320);
    ck(w[160 * 384 + 80] == 0x33 && w[199 * 384 + 303] == 0x33 && w[160 * 384 + 79] == 0 && w[160 * 384 + 304] == 0 && w[157 * 384 + 100] == 0,
       "the 320-wide HUD band is copied to x 80..303, rows 158..199");
    ck(w[5 * 384 + 80] == 0x33 && w[12 * 384 + 303] == 0x33 && w[5 * 384 + 79] == 0 && w[14 * 384 + 100] == 0, "...and so is the strip above (rows 0..12, the Tear icons)");
    free(g);
}

/* ------------------------------------------------------------- the zoom-out levels (#126) */
static void t_zoomout(void)
{
    printf("zoom-out levels ");
    static Map m; static Tileset t; Game *g = new_game(&m, &t);
    static uint8_t w[RENDER_WIDE_MAX_W * RENDER_WIDE_MAX_H], a[RENDER_WIDE_MAX_W * RENDER_WIDE_MAX_H];
    /* the layout table: columns x rows of cells, pixels, and that every shape fits the buffers and the 640 limit */
    static const struct { int cols, rows, w, h; } want[ZOOM_LEVELS] = {
        [ZOOM_NORMAL] = { 36, 18, 384, 200 }, [ZOOM_CROP] = { 36, 18, 384, 200 },
        [ZOOM_OUT1] = { 36, 24, 384, 248 }, [ZOOM_OUT2] = { 62, 28, 592, 280 } };
    int ok = 1;
    for (int l = 0; l < ZOOM_LEVELS; l++) {
        RenderView v = render_zoom_view(l);
        int cols = SCREEN_COLS + v.lo + v.hi, rows = 18 + v.rt + v.rb;
        if (cols != want[l].cols || rows != want[l].rows || render_view_width(&v) != want[l].w || render_view_height(&v) != want[l].h) ok = 0;
        if (v.lo != v.hi || v.rt != v.rb || render_view_width(&v) > RENDER_WIDE_MAX_W || render_view_height(&v) > RENDER_WIDE_MAX_H) ok = 0;
    }
    ck(ok, "the four levels: 36x18, 36x18 cropped, 36x24 (384x248), 62x28 (592x280); symmetric, inside the buffers");
    ck(!strcmp(render_zoom_name(ZOOM_OUT2, 1), "OUT 2") && !strcmp(render_zoom_name(ZOOM_CROP, 1), "CROPPED") &&
       !strcmp(render_zoom_name(ZOOM_CROP, 0), "ON") && !strcmp(render_zoom_name(ZOOM_OUT1, 0), "OFF"), "OSD names: the level in the wide view, ON / OFF outside it");
    {   /* aspect: the display shape is w : 1.2h, so OUT1 is near 4:3 and OUT2 near 16:9 */
        RenderView v1 = render_zoom_view(ZOOM_OUT1), v2 = render_zoom_view(ZOOM_OUT2);
        double a1 = render_view_width(&v1) / (1.2 * render_view_height(&v1)), a2 = render_view_width(&v2) / (1.2 * render_view_height(&v2));
        ck(a1 > 1.25 && a1 < 1.35 && a2 > 1.7 && a2 < 1.85, "display aspects %.2f (OUT1) and %.2f (OUT2, about 16:9)", a1, a2);
    }

    for (int v = 1; v < 8; v++) {
        t.present[v] = 1;
        for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) t.cell[v].px[r][c] = (uint8_t)(v * 16 + (c == 0 ? 1 : 2));
    }
    /* each ring row has its own tile (1 + row % 7), so a row's place shows in the picture; the map the same by column */
    for (int c = 0; c < 100; c++) for (int r = 0; r < MAP_ROWS; r++) m.grid[c][r] = (uint8_t)(1 + (c % 7));
    for (int r = 0; r < RING_H; r++) for (int c = 0; c < RING_W; c++) g->bg_ring[r * RING_W + c] = g->ring[r * RING_W + c] = (uint8_t)(1 + (r % 7));
    g->scroll_col = 10;

    /* zoom-out 1: the window's 18 rows sit 3 rows (24 px) down, and the 3 rows above and below are the ring's own rows */
    RenderView v1 = render_zoom_view(ZOOM_OUT1);
    const int W1 = render_view_width(&v1);
    for (int sr0 = 0; sr0 < 2; sr0++) {
        g->scroll_row = sr0 ? 62 : 20;       /* the second wraps: rows 59..61 above, then 62, 63, 0, 1 ... */
        ck(render_frame_view(w, g, NULL, &v1) == 1, "zoom-out 1 draws a plain cavern frame");
        ok = 1;
        for (int sr = -3; sr < 21; sr++) {
            int ringrow = (g->scroll_row + sr) & 0x3F;
            int y = PF_Y + (sr + 3) * 8 + 3;
            for (int c = 0; c < 36; c++)
                if (w[y * W1 + PF_X + c * 8 + 3] != (uint8_t)((1 + ringrow % 7) * 16 + 2)) ok = 0;
        }
        ck(ok, "zoom-out 1 (scroll_row %d): rows -3..20 of the window are the ring's rows, wrapping at 64", g->scroll_row);
    }
    g->scroll_row = 20;
    render_frame_view(w, g, NULL, &v1);
    const RenderView v0 = RENDER_VIEW_WIDE;
    render_frame_view(a, g, NULL, &v0);
    ok = 1;
    for (int y = 0; y < 18 * 8; y++) for (int x = 0; x < W1; x++)
        if (w[(PF_Y + 24 + y) * W1 + x] != a[(PF_Y + y) * W1 + x]) ok = 0;
    ck(ok, "...and the middle 18 rows are exactly the normal wide view");
    int leak = 0;
    for (int y = PF_Y + 24 * 8; y < 248; y++) for (int x = PF_X; x < W1 - PF_X; x++) leak |= w[y * W1 + x];
    ck(!leak, "nothing is drawn in the HUD band's rows (the HUD is composed apart)");

    /* a shot in an extra row, above and below the window, is drawn where it is */
    memset(g->shots, 0, sizeof g->shots); g->shots[1].col = 0xFF;
    t.present[9] = 1;
    for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) t.cell[9].px[r][c] = 0xEE;
    g->shots[0].col = 12; g->shots[0].row = (uint8_t)((g->scroll_row - 2) & 0x3F); g->shots[0].cell = 9;
    render_frame_view(w, g, NULL, &v1);
    ck(w[(PF_Y + 1 * 8 + 2) * W1 + PF_X + 12 * 8 + 2] == 0xEE, "a shot two rows above the window is drawn in the top extra rows");
    g->shots[0].row = (uint8_t)(g->scroll_row + 20);
    render_frame_view(w, g, NULL, &v1);
    ck(w[(PF_Y + 23 * 8 + 2) * W1 + PF_X + 12 * 8 + 2] == 0xEE, "...and one two rows under it in the bottom extra rows");
    g->shots[0].row = (uint8_t)(g->scroll_row + 23);
    render_frame_view(w, g, NULL, &v1);
    int seen = 0;
    for (int y = 0; y < 248; y++) for (int x = 0; x < W1; x++) seen |= w[y * W1 + x] == 0xEE;
    ck(!seen, "...but not one beyond the view");
    g->shots[0].row = (uint8_t)(g->scroll_row + 19 + 64 - 64);
    memset(g->shots, 0, sizeof g->shots); g->shots[0].col = 0xFF;

    /* an enemy in an extra row: a 2x2 sprite from a made-up overlay and bank */
    {
        static AiOverlay ai; static EnemyGfx eg; static uint8_t img[0x400];
        ai.img = img; ai.len = sizeof img; ai.loaded = 1; ai.frame_l[0] = 0xA100;
        img[0x100] = 0; img[0x101] = 1; img[0x102] = 2; img[0x103] = 3; img[0x104] = 4;
        eg.ncells = 5;
        for (int k = 1; k < 5; k++) for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) { eg.cell[k].mask[r][c] = 1; eg.cell[k].idx[r][c] = 1; }
        g->ai = &ai; g->egfx = &eg; g->nobj = 1;
        MapObj *o = &g->obj[0];
        o->col = 0; o->row = (uint8_t)((g->scroll_row - 3) & 0x3F); o->rcol = 20; o->type = 0; o->hit = 0; o->phase = 0;
        g->ring[game_ring_index(g, o->row, o->rcol)] = 0x80;
        memset(w, 0, sizeof w);
        render_frame_view(w, g, NULL, &v1);
        uint8_t px = w[(PF_Y + 1) * W1 + PF_X + 20 * 8 + 1];
        ck(px != 0 && px != (uint8_t)(1 * 16 + 2), "an enemy in the top extra row (3 rows above the window) is drawn");
        render_frame_view(a, g, NULL, &v0);
        seen = 0;
        for (int y = PF_Y; y < PF_Y + PF_H; y++) for (int x = PF_X; x < 384 - PF_X; x++) seen |= a[y * 384 + x] == px;
        ck(!seen, "...and the normal wide view does not show it");
        o->row = (uint8_t)((g->scroll_row + 19) & 0x3F);              /* the window's hidden row 18+1: inside the view now */
        g->ring[game_ring_index(g, o->row, o->rcol)] = 0x80;
        memset(w, 0, sizeof w);
        render_frame_view(w, g, NULL, &v1);
        ck(w[(PF_Y + 22 * 8 + 1) * W1 + PF_X + 20 * 8 + 1] == px && w[(PF_Y + 23 * 8 + 1) * W1 + PF_X + 20 * 8 + 1] == px,
           "an enemy on row 19 of the window (hidden under the HUD when stock) is drawn whole");
        o->row = (uint8_t)((g->scroll_row + 20) & 0x3F);              /* on the view's last row: its lower pair is cut by the view */
        g->ring[game_ring_index(g, o->row, o->rcol)] = 0x80;
        memset(w, 0, sizeof w);
        render_frame_view(w, g, NULL, &v1);
        seen = 0;
        for (int y = PF_Y + 24 * 8; y < 248; y++) for (int x = PF_X; x < W1 - PF_X; x++) seen |= w[y * W1 + x];
        ck(w[(PF_Y + 23 * 8 + 1) * W1 + PF_X + 20 * 8 + 1] == px && !seen, "...one on the view's last row draws its top pair and does not spill into the HUD rows");
        g->nobj = 0; g->ai = NULL; g->egfx = NULL;
        memset(g->ring, 0, sizeof g->ring);
        for (int r = 0; r < RING_H; r++) for (int c = 0; c < RING_W; c++) g->bg_ring[r * RING_W + c] = g->ring[r * RING_W + c] = (uint8_t)(1 + (r % 7));
    }

    /* zoom-out 2: 62 columns.  The 36 ring columns are in the middle (x 48 + 13*8), the 13 each side are the map */
    RenderView v2 = render_zoom_view(ZOOM_OUT2);
    const int W2 = render_view_width(&v2);
    ck(render_frame_view(w, g, NULL, &v2) == 1, "zoom-out 2 draws a plain cavern frame");
    ok = 1;
    for (int sr = -5; sr < 23; sr++) {
        int y = PF_Y + (sr + 5) * 8 + 3, ringrow = (g->scroll_row + sr) & 0x3F;
        for (int c = 0; c < 36; c++)
            if (w[y * W2 + PF_X + (13 + c) * 8 + 3] != (uint8_t)((1 + ringrow % 7) * 16 + 2)) ok = 0;
        for (int k = -13; k < 0; k++) {        /* scenery on the left: map column scroll_col + k */
            int mc = ((g->scroll_col + k) % 100 + 100) % 100;
            if (w[y * W2 + PF_X + (13 + k) * 8 + 3] != (uint8_t)((1 + mc % 7) * 16 + 2)) ok = 0;
        }
        for (int k = 36; k < 49; k++) {        /* ...and on the right */
            int mc = (g->scroll_col + k) % 100;
            if (w[y * W2 + PF_X + (13 + k) * 8 + 3] != (uint8_t)((1 + mc % 7) * 16 + 2)) ok = 0;
        }
    }
    ck(ok, "zoom-out 2: ring columns in the middle, 13 map columns of scenery each side (from the map, row for row)");
    render_frame_view(a, g, NULL, &v0);
    ok = 1;
    for (int y = 0; y < 18 * 8; y++) for (int x = 0; x < 288; x++)
        if (w[(PF_Y + 40 + y) * W2 + PF_X + 13 * 8 + x] != a[(PF_Y + y) * 384 + PF_X + x]) ok = 0;
    ck(ok, "...and the ring's 36 columns x the window's 18 rows are exactly the normal wide view");
    leak = 0;
    for (int y = 0; y < 280; y++) { for (int x = 0; x < PF_X; x++) leak |= w[y * W2 + x]; for (int x = W2 - PF_X; x < W2; x++) leak |= w[y * W2 + x]; }
    ck(!leak, "nothing leaks into the stone columns");
    {   /* the live span the shading uses */
        int x0, x1, y0, y1;
        render_live_span(&v2, &x0, &x1, &y0, &y1);
        ck(x0 == PF_X + 13 * 8 && x1 == x0 + 288 && y0 == PF_Y && y1 == PF_Y + 28 * 8, "the live span is the 36 ring columns x all rows (x %d..%d)", x0, x1);
        render_live_span(&v0, &x0, &x1, &y0, &y1);
        ck(x0 == PF_X && x1 == PF_X + 288, "...and the whole playfield in the normal wide view");
    }
    {   /* a lift outside the ring is drawn as the map has it; one inside is the ring's */
        for (int k = 0; k < 3; k++) { t.present[0x40 + k] = 1; for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) t.cell[0x40 + k].px[r][c] = (uint8_t)(0xD0 + k); }
        g->nfix = 1; g->fix[0].col = (uint16_t)(g->scroll_col + 40); g->fix[0].row = (uint8_t)(g->scroll_row + 5); g->fix[0].cell = 0x40;
        render_frame_view(w, g, NULL, &v2);
        int y = PF_Y + (5 + 5) * 8 + 3;
        ck(w[y * W2 + PF_X + (13 + 40) * 8 + 3] == 0xD0 && w[y * W2 + PF_X + (13 + 42) * 8 + 3] == 0xD2, "a fixture at ring column 40 is drawn in the scenery columns");
        g->fix[0].col = (uint16_t)(g->scroll_col + 34);        /* cells 34, 35 in the ring, 36 outside it */
        render_frame_view(w, g, NULL, &v2);
        ck(w[y * W2 + PF_X + (13 + 36) * 8 + 3] == 0xD2 && w[y * W2 + PF_X + (13 + 34) * 8 + 3] != 0xD0, "...only its cells past the ring (the ring's own are the ring's)");
        render_frame_view(w, g, NULL, &v1);
        ck(1, "...the zoom-out 1 view has no scenery columns");
        g->nfix = 0;
    }
    /* a map too narrow for the view is refused */
    m.width = 60;
    ck(render_frame_view(w, g, NULL, &v2) == 0 && render_frame_view(w, g, NULL, &v1) == 1, "zoom-out 2 on a 60-column map is refused (it would repeat itself), zoom-out 1 is not");
    m.width = 100;

    /* the furniture at 592x280 and 384x248: stone sides, HUD at the bottom */
    memset(&SF, 0, sizeof SF); SF.loaded = 1;
    for (int y = 0; y < FB_H; y++) for (int x = 0; x < FB_W; x++) {
        int on = x < 48 || x >= 272 || (x >= 48 && x < 272 && (y < 13 || y >= 158));
        if (on) { SF.on[y * FB_W + x] = 1; SF.px[y * FB_W + x] = (uint8_t)(1 + (x * 7 + y) % 60); }
    }
    for (int k = 0; k < 2; k++) {
        const int fw = k ? 592 : 384, fh = k ? 280 : 248, ex = fh - 200;
        const uint8_t *fp, *fo;
        render_wide_frame(&SF, fw, fh, &fp, &fo);
        ok = 1;
        for (int y = 0; y < fh; y++)
            for (int x = 0; x < fw; x++) {
                int in_play = x >= 48 && x < fw - 48 && y >= 13 && y < 158 + ex;
                if (in_play && fo[y * fw + x]) ok = 0;
                if (x < 48 && !fo[y * fw + x]) ok = 0;
                if (x >= fw - 48 && !fo[y * fw + x]) ok = 0;
            }
        ck(ok, "%dx%d furniture: stone down both sides over the full height, nothing over the playfield", fw, fh);
        ok = 1;
        for (int y = 0; y < 60; y++) for (int x = 0; x < 48; x++) if (fp[y * fw + x] != SF.px[y * FB_W + x]) ok = 0;
        for (int y = 140 + ex; y < fh; y++) for (int x = 0; x < 48; x++) if (fp[y * fw + x] != SF.px[(y - ex) * FB_W + x]) ok = 0;
        ck(ok, "...its top 60 and bottom 60 rows are the original stone's, the extra rows repeat the middle");
        ok = 1;
        const int ho = (fw - FB_W) / 2;
        for (int y = 158 + ex; y < fh; y++) for (int x = 0; x < 224; x++)
            if (fp[y * fw + ho + 48 + x] != SF.px[(y - ex) * FB_W + 48 + x]) ok = 0;
        ck(ok, "...the HUD panel keeps its size, centred, on the bottom 42 rows");
        static uint8_t hb[RENDER_WIDE_MAX_W * RENDER_WIDE_MAX_H], h320[FB_W * FB_H];
        memset(h320, 0x33, sizeof h320); memset(hb, 0, sizeof hb);
        render_wide_hud(hb, fw, fh, h320);
        ck(hb[(158 + ex) * fw + ho + 48] == 0x33 && hb[(fh - 1) * fw + ho + 48 + 223] == 0x33 && hb[(157 + ex) * fw + ho + 100] == 0 && hb[5 * fw + ho + 100] == 0x33 && hb[20 * fw + ho + 100] == 0,
           "...and render_wide_hud copies the HUD band there and the strip above");
    }
    free(g);
}

int main(void)
{
    t_math();    printf("ok\n");
    t_vsync();   printf("ok\n");
    t_step();    printf("ok\n");
    t_objects(); printf("ok\n");
    t_render();  printf("ok\n");
    t_wide();    printf("ok\n");
    t_zoomout(); printf("ok\n");
    t_fixtures(); printf("ok\n");
    t_town_steps();  printf("ok\n");
    t_town_render(); printf("ok\n");
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
