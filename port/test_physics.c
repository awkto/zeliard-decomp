/* test_physics.c — frame-by-frame assertions against docs/FIGHT.md §4-§5 on
 * synthetic maps (plus two checks on the real MP10 when the game files exist). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "map.h"
#include "gfx.h"
#include "physics.h"

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; fprintf(stderr, "  FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

static Map tmap;
static Tileset ttiles;
static unsigned presented;
static void present(Game *g) { presented++; }

/* synthetic tileset: passable {0,1,2,8}, conveyor L = 0xB, R = 0xC, hazard = 0xD */
static void test_tiles(void)
{
    memset(&ttiles, 0, sizeof ttiles);
    static const uint8_t pass[24] = {0, 1, 2, 8, 0x0B, 0x0C};
    memcpy(ttiles.lists, pass, 24);
    ttiles.lists[0x18] = 0x0B; ttiles.lists[0x1C] = 0x0C; ttiles.lists[0x20] = 0x0D;
}

/* 96-wide map, thick ground from row 13 down */
static void flat_map(void)
{
    static char rows[MAP_ROWS][MAP_MAX_WIDTH + 1];
    static const char *ptr[MAP_ROWS];
    for (int r = 0; r < MAP_ROWS; r++) { memset(rows[r], r >= 13 ? '6' : '.', 96); rows[r][96] = 0; ptr[r] = rows[r]; }
    map_from_text(&tmap, 96, ptr);
}
static void set(int col, int row, int v) { tmap.grid[col][row] = (uint8_t)v; }

static Game G;
static void start(int col, int row, int face_left)
{
    game_init(&G, &tmap, &ttiles);
    G.present = present;
    game_place(&G, col, row, face_left);
    game_first_frame(&G);
}
static void step(uint8_t dirs) { G.dirs = dirs; game_step(&G); }
static int hcol(void) { return game_hero_map_col(&G); }
static int hrow(void) { return game_hero_map_row(&G); }

static void t_idle(void)
{
    flat_map(); start(30, 10, 0);
    for (int i = 0; i < 5; i++) { step(0); CHECK(hrow() == 10 && hcol() == 30 && G.vstate == V_GROUND, "idle hero moved: (%d,%d) v=%02x", hcol(), hrow(), G.vstate); }
    CHECK(G.hero_anim == 0x80, "idle pose expected");
}

static void t_walk(void)
{
    flat_map(); start(30, 10, 0);
    for (int i = 1; i <= 5; i++) { step(DIR_RIGHT); CHECK(hcol() == 30 + i, "walk right frame %d: col %d", i, hcol()); }
    CHECK(G.hero_scr_col == 12 && G.hero_scr_row == 10, "hero must stay on screen cell (12,10): (%d,%d)", G.hero_scr_col, G.hero_scr_row);
    CHECK(G.scroll_col == 35 - 16, "scroll_col %d", G.scroll_col);
    step(DIR_LEFT); CHECK(hcol() == 35 && (G.hero_flags & FACE_LEFT), "first Left turns without moving: col %d flags %02x", hcol(), G.hero_flags);
    step(DIR_LEFT); CHECK(hcol() == 34, "second Left moves: col %d", hcol());
    step(0); CHECK(G.hero_anim == 0x80, "release -> idle pose (the WALKING flag itself only clears on jump/down/block)");
    CHECK(G.hero_scr_col == 12 && G.hero_scr_row == 10, "camera");
}

static void t_wall(void)
{
    flat_map();
    for (int r = 10; r <= 12; r++) set(40, r, 6);        /* 3-high wall at col 40 */
    start(30, 10, 0);
    for (int i = 0; i < 15; i++) step(DIR_RIGHT);
    CHECK(hcol() == 38, "right column (col+2) stops before the wall: hero col %d (want 38)", hcol());
    /* from the other side, walking left: body/left column stops at 41 */
    start(50, 10, 1);
    for (int i = 0; i < 15; i++) step(DIR_LEFT);
    CHECK(hcol() == 40, "walking left the hero's own left column may overlap the wall (body stays adjacent): hero col %d (want 40)", hcol());
    /* a 1-high obstacle at head height blocks; crouch is not a way through while walking */
    flat_map(); set(40, 10, 6); start(30, 10, 0);
    for (int i = 0; i < 15; i++) step(DIR_RIGHT);
    CHECK(hcol() == 38, "head-row cell blocks walking: col %d", hcol());
    flat_map(); set(40, 12, 6); start(30, 10, 0);
    for (int i = 0; i < 15; i++) step(DIR_RIGHT);
    CHECK(hcol() == 38, "feet-row cell blocks walking: col %d", hcol());
    /* DCHR item cells (>= 0x49) are passable, gate cells 0x43 are solid to the body */
    flat_map(); set(40, 11, 0x49); start(30, 10, 0);
    for (int i = 0; i < 15; i++) step(DIR_RIGHT);
    CHECK(hcol() == 45, "item cell 0x49 is passable: col %d", hcol());
    flat_map(); set(40, 11, 0x43); start(30, 10, 0);
    for (int i = 0; i < 15; i++) step(DIR_RIGHT);
    CHECK(hcol() == 38, "gate cell 0x43 is solid to the body: col %d", hcol());
}

static void t_jump(void)
{
    flat_map(); start(30, 10, 0);
    /* hold Up: rows 10 -> 9 -> 8 -> 8 (stop) -> 9 -> 10 (land, crouch) */
    static const int want[] = {9, 8, 8, 9, 10, 10};
    static const uint8_t wv[] = {V_RISE, V_RISE, V_FALL, V_FALL, V_FALL, V_GROUND};
    for (int i = 0; i < 6; i++) {
        step(DIR_UP);
        CHECK(hrow() == want[i] && G.vstate == wv[i], "jump frame %d: row %d v=%02x (want %d %02x)", i + 1, hrow(), G.vstate, want[i], wv[i]);
    }
    CHECK(G.crouching == 0xFF, "landing after a 2-row fall crouches");
    CHECK(G.rise_rows == 0 && G.fall_rows == 0, "counters reset");
    CHECK(G.hero_scr_row == 10, "hero back on home row: %d", G.hero_scr_row);
    /* rise moves the hero on screen (row >= 7), not the world */
    start(30, 10, 0); step(DIR_UP);
    CHECK(G.hero_scr_row == 9 && G.scroll_row == 0, "rise moves the sprite up on screen: scr_row %d scroll_row %d", G.hero_scr_row, G.scroll_row);
    /* variable height: tap Up once -> 1 row up, 1 row down, no crouch */
    start(30, 10, 0);
    step(DIR_UP); CHECK(hrow() == 9 && G.vstate == V_RISE, "tap: rise 1");
    step(0);      CHECK(hrow() == 9 && G.vstate == V_FALL, "release ends the rise: row %d v=%02x", hrow(), G.vstate);
    step(0);      CHECK(hrow() == 10 && G.vstate == V_FALL, "fall 1 row: row %d v=%02x", hrow(), G.vstate);
    step(0);      CHECK(hrow() == 10 && G.vstate == V_GROUND && G.crouching == 0, "land without crouch: v=%02x crouch=%02x", G.vstate, G.crouching);
    /* crouch is released 2 frames after landing */
    start(30, 10, 0);
    for (int i = 0; i < 6; i++) step(DIR_UP);
    CHECK(G.crouching == 0xFF, "crouched on landing");
    step(0); CHECK(G.crouching == 0xFF, "still crouched after 1 frame");
    step(0); CHECK(G.crouching == 0, "crouch released after 2 frames");
    /* Feruza shoes: 4 rows */
    start(30, 10, 0); G.shoes = 1;
    int top = 10;
    for (int i = 0; i < 6; i++) { step(DIR_UP); if (hrow() < top) top = hrow(); }
    CHECK(top == 6, "Feruza shoes rise 4 rows: top row %d", top);
}

static void t_ceiling(void)
{
    flat_map();
    for (int c = 28; c <= 34; c++) set(c, 8, 6);        /* ceiling 2 rows above the head (head row 10 -> probe row 9 ok, row 8 solid) */
    start(30, 10, 0);
    step(DIR_UP); CHECK(hrow() == 9 && G.vstate == V_RISE, "first row under a 2-high gap: row %d", hrow());
    step(DIR_UP); CHECK(hrow() == 9 && G.vstate == V_FALL, "second row blocked by the ceiling: row %d v=%02x", hrow(), G.vstate);
    step(DIR_UP); CHECK(hrow() == 10, "falls back");
    flat_map();
    for (int c = 28; c <= 34; c++) set(c, 9, 6);        /* no headroom at all */
    start(30, 10, 0);
    step(DIR_UP); CHECK(hrow() == 10 && G.vstate == V_GROUND && G.hero_anim == 0x80, "no rise under a solid ceiling: row %d v=%02x", hrow(), G.vstate);
    /* only the centre column (col+1) is probed above the head */
    flat_map(); set(30, 9, 6); set(32, 9, 6); start(30, 10, 0);
    step(DIR_UP); CHECK(hrow() == 9, "side columns above the head do not block the jump: row %d", hrow());
}

static void t_diagonal_jump(void)
{
    flat_map(); start(30, 10, 0);
    step(DIR_UP | DIR_RIGHT); CHECK(hrow() == 9 && hcol() == 31, "diag frame 1: (%d,%d)", hcol(), hrow());
    step(DIR_UP | DIR_RIGHT); CHECK(hrow() == 8 && hcol() == 32, "diag frame 2: (%d,%d)", hcol(), hrow());
    step(DIR_UP | DIR_RIGHT); CHECK(hrow() == 8 && hcol() == 33 && G.vstate == V_FALL, "diag frame 3 (top, extra step): (%d,%d) v=%02x", hcol(), hrow(), G.vstate);
    step(DIR_UP | DIR_RIGHT); CHECK(hrow() == 9 && hcol() == 34, "air control keeps walking: (%d,%d)", hcol(), hrow());
    step(DIR_UP | DIR_RIGHT); CHECK(hrow() == 10 && hcol() == 35, "(%d,%d)", hcol(), hrow());
    step(0); CHECK(G.vstate == V_GROUND && hcol() == 35, "landed at %d", hcol());
}

static void t_gap(void)
{
    /* 1-cell gap: a walking hero crosses it, an idle hero drops in */
    flat_map();
    for (int r = 13; r < MAP_ROWS; r++) set(40, r, 0);
    start(34, 10, 0);
    for (int i = 0; i < 10; i++) { step(DIR_RIGHT); CHECK(hrow() == 10 && G.vstate == V_GROUND, "walking over a 1-cell gap: frame %d (%d,%d) v=%02x", i, hcol(), hrow(), G.vstate); }
    CHECK(hcol() == 44, "col %d", hcol());
    start(39, 10, 0);                                    /* body column 40 = over the hole, idle */
    step(0); CHECK(hrow() == 11 && G.vstate == V_FALL, "idle hero falls into a 1-cell hole: row %d v=%02x", hrow(), G.vstate);
    /* 2-cell gap: falls even while walking */
    flat_map();
    for (int r = 13; r < MAP_ROWS; r++) { set(40, r, 0); set(41, r, 0); }
    start(34, 10, 0);
    int fell_at = -1;
    for (int i = 0; i < 10; i++) { step(DIR_RIGHT); if (G.vstate == V_FALL && fell_at < 0) fell_at = hcol(); }
    CHECK(fell_at == 40, "2-cell gap: fall starts with the body over it + one extra step (69CB): first falling col %d (want 40)", fell_at);
    CHECK(hrow() > 10, "hero is falling: row %d", hrow());
}

static void t_edge_fall(void)
{
    flat_map();
    for (int c = 40; c < 96; c++) for (int r = 13; r < 17; r++) set(c, r, 0);   /* ground drops to row 17 after col 39 */
    start(34, 10, 0);
    /* walk right: at hero col 38 the body (39) is on the last floor cell; next step body 40 -> fall */
    int rows[12], cols[12];
    for (int i = 0; i < 12; i++) { step(DIR_RIGHT); rows[i] = hrow(); cols[i] = hcol(); }
    /* frames: 35,36,37,38,39 (body 40 over the void), then 1 row/frame + the single extra step of 69CB;
     * a walk-off clears WALKING, so holding Right in the air does not keep him moving (air control only
     * continues a jump that started while walking) */
    CHECK(rows[3] == 10 && cols[3] == 38, "frame 4: (%d,%d)", cols[3], rows[3]);
    CHECK(rows[4] == 10 && cols[4] == 39, "frame 5: body steps over the edge: (%d,%d)", cols[4], rows[4]);
    CHECK(rows[5] == 11 && cols[5] == 40, "walked off the edge: 1 row down + extra step: (%d,%d)", cols[5], rows[5]);
    CHECK(rows[6] == 12 && cols[6] == 40, "fall 1 row/frame, no acceleration: (%d,%d)", cols[6], rows[6]);
    CHECK(rows[7] == 13 && cols[7] == 40, "(%d,%d)", cols[7], rows[7]);
    CHECK(rows[8] == 14 && cols[8] == 40, "(%d,%d)", cols[8], rows[8]);
    CHECK(rows[9] == 14 && cols[9] == 40, "landed on row 14 (floor 17): (%d,%d)", cols[9], rows[9]);
    CHECK(G.crouching == 0xFF || G.crouch_release >= 1, "landing crouch after a 4-row fall (crouch %02x)", G.crouching);
    /* falling scrolls the world: the hero stays on his home row on screen */
    CHECK(G.hero_scr_row == 10 && G.scroll_row == 4, "world scrolled 4 rows: scr_row %d scroll_row %d", G.hero_scr_row, G.scroll_row);
    for (int i = 0; i < 6; i++) step(DIR_RIGHT);
    CHECK(hrow() == 14 && hcol() > 42 && G.vstate == V_GROUND, "walks on after landing: (%d,%d)", hcol(), hrow());
}

static void t_ladder(void)
{
    flat_map();
    for (int r = 4; r <= 12; r++) set(31, r, 1);        /* ladder in the hero's body column, rows 4..12 (floor 13 stays solid) */
    start(30, 10, 0);
    /* one row per rendered frame (each climbed row runs frame()); a step renders its own frame first,
     * then hero_input climbs until hero_anim is odd (65F9..661F): 1 row on mounting, then 2 per step */
    presented = 0; step(DIR_UP); CHECK(hrow() == 9 && G.on_ladder == 0xFF && presented == 2, "mount + climb 1 row: row %d ladder %02x frames %u", hrow(), G.on_ladder, presented);
    presented = 0; step(DIR_UP); CHECK(hrow() == 7 && presented == 3, "climb 2 rows in 2 frames: row %d frames %u", hrow(), presented);
    presented = 0; step(DIR_UP); CHECK(hrow() == 5 && presented == 3, "climb 2 rows in 2 frames: row %d frames %u", hrow(), presented);
    presented = 0; step(DIR_UP); CHECK(hrow() == 4 && presented == 2, "top reached: row %d frames %u", hrow(), presented);
    step(DIR_UP); CHECK(hrow() == 4, "top of the ladder: row %d", hrow());
    CHECK(G.scroll_row == (uint8_t)((4 - 10) & 63) && G.hero_scr_row == 10, "climbing scrolls the world: scroll_row %d scr_row %d", G.scroll_row, G.hero_scr_row);
    /* descend: Down climbs back, 1 row per rendered frame */
    presented = 0; step(DIR_DOWN); CHECK(hrow() == 4 + (int)presented - 1 && presented >= 2, "descend: row %d frames %u", hrow(), presented);
    int r0 = hrow(); presented = 0; step(DIR_DOWN); CHECK(hrow() == r0 + (int)presented - 1 && presented >= 2, "descend: row %d frames %u", hrow(), presented);
    /* letting go: Down with no ladder below the feet -> drop */
    flat_map();
    for (int r = 4; r <= 9; r++) set(31, r, 1);          /* ladder ends at row 9 (floor at 13) */
    start(30, 10, 0);
    step(DIR_UP); step(DIR_UP); step(DIR_UP); step(DIR_UP);
    CHECK(hrow() == 4 && G.on_ladder == 0xFF, "at the top: row %d", hrow());
    step(DIR_DOWN); step(DIR_DOWN); step(DIR_DOWN); step(DIR_DOWN);
    CHECK(hrow() >= 7 && hrow() <= 10, "climbed down to the ladder end and let go: row %d", hrow());
    for (int i = 0; i < 6; i++) step(0);
    CHECK(hrow() == 10 && G.vstate == V_GROUND && G.on_ladder == 0, "back on the ground: row %d v=%02x ladder %02x", hrow(), G.vstate, G.on_ladder);
    /* falling onto a ladder grabs it (699D) */
    flat_map();
    for (int r = 6; r <= 13; r++) set(31, r, 1);
    for (int c = 0; c < 96; c++) for (int r = 13; r < 20; r++) if (c != 31) set(c, r, 0);   /* nothing but the ladder below row 13 */
    for (int c = 0; c < 96; c++) set(c, 20, 6);
    start(30, 10, 0);
    step(0); CHECK(G.on_ladder == 0xFF, "idle hero over a ladder cell grabs it while falling: ladder %02x row %d", G.on_ladder, hrow());
}

static void t_conveyor(void)
{
    flat_map();
    for (int c = 20; c < 60; c++) set(c, 12, 0x0C);     /* belt cells at feet height, pushing right */
    start(30, 10, 0);
    int c0 = hcol();
    for (int i = 0; i < 8; i++) step(0);
    CHECK(hcol() == c0 + 2, "belt pushes 1 cell every 4 frames: moved %d in 8 frames", hcol() - c0);
    CHECK(G.conveyor == 1, "conveyor flag 1 (right)");
    for (int i = 0; i < 4; i++) step(DIR_LEFT);          /* walking against the belt cancels the push */
    CHECK(hcol() == c0 + 2, "walking against the belt holds position: %d", hcol() - c0);
}

static void t_hazard(void)
{
    flat_map();
    for (int c = 30; c < 40; c++) set(c, 13, 0x0D);
    start(30, 10, 0);
    step(0); step(0);
    CHECK(G.on_hazard == 0xFF && G.hazard_frames >= 1, "hazard cell below the feet is detected every frame");
}

static const Door *taken;
static int door_cb(Game *g, const Door *d) { taken = d; return 0; }

static void t_mp10(const char *dir)
{
    static Map m; static Tileset t;
    if (map_load_system(&m, dir, 0)) { fprintf(stderr, "  (MP10 not available in %s: skipping the real-map checks)\n", dir); return; }
    if (gfx_load_tileset(&t, dir, m.tileset)) { fprintf(stderr, "  (tileset not available)\n"); return; }
    CHECK(m.width == 240 && m.cavern == 1 && m.row_bias == 10, "MP10 header: width %d cavern %d bias %d", m.width, m.cavern, m.row_bias);
    CHECK(t.lists[0] == 0 && t.lists[3] == 8 && t.lists[0x18] == 0x0B && t.lists[0x1C] == 0x0C && t.lists[0x20] == 0x0F, "MPP1 cell-0 lists");
    game_init(&G, &m, &t); G.present = present;
    game_place(&G, 61, 7, 0);
    CHECK(G.scroll_col == 45 && G.scroll_row == 61 && G.hero_scr_col == 12 && G.hero_scr_row == 10, "MURALLA door entry: scroll (%d,%d)", G.scroll_col, G.scroll_row);
    game_first_frame(&G);
    CHECK(game_ring_cell(&G, 12, 9) == DOOR_CELL, "door cell 0x4A above the hero after signs_draw: %02x", game_ring_cell(&G, 12, 9));
    for (int i = 0; i < 3; i++) { step(0); CHECK(hrow() == 7 && hcol() == 61 && G.vstate == V_GROUND, "stands in the door: (%d,%d)", hcol(), hrow()); }
    /* Up in the door: the C00A record (61,6) -> town MRMP col 205 */
    taken = NULL;
    G.on_door = door_cb;
    step(DIR_UP);
    CHECK(taken && taken->dest_map == 1 && taken->dest_row == 0xFF && taken->dest_col == 205, "door record found: %s", taken ? "yes" : "no");
    CHECK(hrow() == 7, "door press does not jump: row %d", hrow());
    /* walk right along the platform (row 10 is solid from col 48 to 80) */
    G.on_door = NULL;
    for (int i = 0; i < 15; i++) step(DIR_RIGHT);
    CHECK(hcol() == 76 && hrow() == 7, "walked 15 cells right on the MURALLA platform: (%d,%d)", hcol(), hrow());
    /* horizontal wrap: walk left across column 0 */
    game_place(&G, 3, 7, 1); game_first_frame(&G);
    for (int i = 0; i < 6; i++) step(DIR_LEFT);
    CHECK(hcol() >= 230 || hrow() != 7 || G.vstate != V_GROUND || hcol() < 3, "wrapped/moved: col %d row %d", hcol(), hrow());
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../zeliard";
    test_tiles();
    struct { const char *name; void (*fn)(void); } tests[] = {
        {"idle", t_idle}, {"walk", t_walk}, {"walls", t_wall}, {"jump", t_jump}, {"ceiling", t_ceiling},
        {"diagonal jump", t_diagonal_jump}, {"gaps", t_gap}, {"edge fall", t_edge_fall}, {"ladder", t_ladder},
        {"conveyor", t_conveyor}, {"hazard", t_hazard},
    };
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; i++) {
        int before = fails;
        tests[i].fn();
        fprintf(stderr, "%-14s %s\n", tests[i].name, fails == before ? "ok" : "FAILED");
    }
    int before = fails; t_mp10(dir); fprintf(stderr, "%-14s %s\n", "mp10", fails == before ? "ok" : "FAILED");
    fprintf(stderr, "%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
