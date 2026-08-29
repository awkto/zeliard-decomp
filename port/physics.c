/* physics.c — port of the hero/camera part of src/fight.c.  Function names and
 * the hex tags match fight.c; the ring is addressed with linear indices and the
 * same single wrap the original applies to pointers (6D82/6D8E). */
#include "physics.h"
#include "enemy.h"
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ ring */
static inline int wrap_down(int p) { return p >= RING_SIZE ? p - RING_SIZE : p; }
static inline int wrap_up(int p)   { return p < 0 ? p + RING_SIZE : p; }
int game_win(const Game *g) { return (g->scroll_row & 0x3F) * RING_W; }
int game_hero_cell(const Game *g) { return wrap_down(game_win(g) + g->hero_scr_row * RING_W + g->hero_scr_col + 4); }   /* 6DB1 */
int game_hero_map_col(const Game *g) { int c = g->scroll_col + g->hero_scr_col + 4; if (c >= g->map->width) c -= g->map->width; return c; }
int game_hero_map_row(const Game *g) { return (g->scroll_row + g->hero_scr_row) & 0x3F; }
int game_ring_index(const Game *g, uint8_t row, uint8_t col) { (void)g; return (row & 0x3F) * RING_W + col; }   /* 6D6E */
int game_ring_add(int p, int delta) { p += delta; while (p >= RING_SIZE) p -= RING_SIZE; while (p < 0) p += RING_SIZE; return p; }
uint8_t game_ring_cell(const Game *g, int sc, int sr) { return g->ring[wrap_down(game_win(g) + sr * RING_W + sc + 4)]; }

/* 6DCB: plain cell -> (value, is_sprite=0); marker -> object type */
static uint8_t cell_or_type(const Game *g, int p, int *is_sprite)
{
    uint8_t v = g->ring[p];
    if (!(v & 0x80)) { *is_sprite = 0; return v; }
    *is_sprite = 1;
    int i = v & 0x7F;
    return i < g->nobj ? g->obj[i].type : 0;
}

/* 6DF3 */
static int in_passable_list(const Game *g, uint8_t v)
{
    for (int i = 0; i < 24; i++) if (g->tiles->lists[i] == v) return 1;
    if ((v & 0x9F) == 0x90 || (v & 0x9F) == 0x91) return 0;
    return (v & 0x80) != 0;
}
int game_passable_wall(const Game *g, uint8_t v) { return v >= 0x40 ? 1 : in_passable_list(g, v); }   /* 6DE5 */
int game_passable_body(const Game *g, uint8_t v)                                                        /* 6E1B */
{
    if (v >= 0x49) return 1;
    for (int i = 0; i < 24; i++) if (g->tiles->lists[i] == v) return 1;
    return (v & 0x80) != 0;
}
#define passable_wall(v) game_passable_wall(g, (v))
#define passable_body(v) game_passable_body(g, (v))
static inline int is_ladder(uint8_t v) { return (uint8_t)(v - 1) < 2; }                                 /* 6BBD */

/* 6BC4: 2 = pushes left (8018 list), 1 = pushes right (801C), 0 none */
static int conveyor_kind(const Game *g, uint8_t v)
{
    const uint8_t *q = g->tiles->lists + 0x18;
    for (int n = 0; n < 4 && q[n]; n++) if (v == q[n]) return 2;
    q = g->tiles->lists + 0x1C;
    for (int n = 0; n < 4 && q[n]; n++) if (v == q[n]) return 1;
    return 0;
}
static int is_hazard(const Game *g, uint8_t v)                                                          /* 73C0 */
{
    const uint8_t *q = g->tiles->lists + 0x20;
    for (int n = 0; n < 4 && q[n]; n++) if (v == q[n]) return 1;
    return 0;
}
/* 76F6: 0 updraft, 1 current left, 2 current right, -1 none */
static int special_tile(const Game *g, uint8_t v)
{
    if (v == 0) return -1;
    for (int k = 0; k < 3; k++) {
        const uint8_t *q = g->tiles->lists + 0x24 + 4 * k;
        for (int n = 0; n < 4 && q[n]; n++) if (v == q[n]) return k;
    }
    return -1;
}
static int ice_physics(const Game *g) { return g->map->cavern == 4 && g->shoes != 4; }                /* 6D9A */

/* ------------------------------------------------------- ring fill/scroll */
static void ring_put_column(Game *g, int ring_col, int map_col)
{
    const uint8_t *src = g->map->grid[map_col];
    for (int r = 0; r < RING_H; r++) g->ring[r * RING_W + ring_col] = src[r];
}
static void ring_fill(Game *g)                                                                          /* 6C98 */
{
    int c = g->scroll_col;
    for (int i = 0; i < RING_W; i++) {
        ring_put_column(g, i, c);
        if (++c == g->map->width) c = 0;
    }
}
static void scroll_left(Game *g)                                                                        /* 66F8: world right, hero left */
{
    if (--g->scroll_col < 0) g->scroll_col = g->map->width - 1;
    memmove(g->ring + 1, g->ring, RING_SIZE - 1);
    ring_put_column(g, 0, g->scroll_col);
}
static void scroll_right(Game *g)                                                                       /* 68A0 */
{
    memmove(g->ring, g->ring + 1, RING_SIZE - 1);
    int c = g->scroll_col + 0x23; if (c >= g->map->width) c -= g->map->width;
    ring_put_column(g, 0x23, c);
    if (++g->scroll_col == g->map->width) g->scroll_col = 0;
}
static void scroll_up(Game *g)   { g->scroll_row = (uint8_t)((g->scroll_row - 1) & 0x3F); }             /* 6621 */
static void scroll_down(Game *g) { g->scroll_row = (uint8_t)((g->scroll_row + 1) & 0x3F); }             /* 6B2E */

/* ---------------------------------------------------------- forward decls */
static void frame(Game *g);
static void walk_left(Game *g);
static void walk_right(Game *g);
static int  try_move_left(Game *g);
static int  try_move_right(Game *g);
static int  floor_under_hero(Game *g);

static void stop_rising(Game *g) { g->conveyor = 0; g->vstate = V_FALL; }                              /* 65BA */
static void turn_around(Game *g) { g->hero_flags ^= FACE_LEFT; if (!g->on_ladder) g->hero_anim = 0x80; }   /* 6824 */
static void stop_walking(Game *g) { g->hero_flags &= (uint8_t)~WALKING; if (!g->on_ladder && g->vstate == V_GROUND) g->hero_anim = 0x80; }  /* 6837 */

/* 67A3 / 6942: cavern 7 one-way current walls */
static int current_blocks_left(const Game *g, uint8_t v)  { return g->map->cavern == 7 && special_tile(g, v) == 2; }
static int current_blocks_right(const Game *g, uint8_t v) { return g->map->cavern == 7 && special_tile(g, v) == 1; }

/* 66A5: returns 1 when blocked.  Destination = the hero's own left column. */
static int try_move_left(Game *g)
{
    int tl = game_hero_cell(g);
    int s = wrap_up(tl - RING_W) - 1;
    for (int n = 4; n; n--) {
        int spr; uint8_t t = cell_or_type(g, wrap_up(s), &spr);
        if (spr && (t & 0x80)) return 1;
        s = wrap_down(s + RING_W);
    }
    s = tl;
    if (!g->crouching) {
        if (!passable_wall(g->ring[s])) return 1;
        if (current_blocks_left(g, g->ring[s])) return 1;
    }
    for (int n = 2; n; n--) {
        s = wrap_down(s + RING_W);
        if (!passable_body(g->ring[s])) return 1;
        if (current_blocks_left(g, g->ring[s])) return 1;
    }
    scroll_left(g);
    return 0;
}
/* 684C: mirror, destination = the hero's own right column (col+2) */
static int try_move_right(Game *g)
{
    int tl = game_hero_cell(g) + 2;
    int s = wrap_up(tl - RING_W);
    for (int n = 4; n; n--) {
        int spr; uint8_t t = cell_or_type(g, wrap_down(s), &spr);
        if (spr && (t & 0x80)) return 1;
        s = wrap_down(s + RING_W);
    }
    s = tl;
    if (!g->crouching) {
        if (!passable_wall(g->ring[s])) return 1;
        if (current_blocks_right(g, g->ring[s])) return 1;
    }
    for (int n = 2; n; n--) {
        s = wrap_down(s + RING_W);
        if (!passable_body(g->ring[s])) return 1;
        if (current_blocks_right(g, g->ring[s])) return 1;
    }
    scroll_right(g);
    return 0;
}

int game_push_hero(Game *g, int left) { return left ? try_move_left(g) : try_move_right(g); }
void game_knock_fall(Game *g)                                                                           /* 64A2 */
{
    if (floor_under_hero(g)) return;
    if (g->rise_rows) { g->rise_rows--; g->hero_scr_row++; } else scroll_down(g);
}

static void ice_slide_start(Game *g)                                                                    /* 6508 */
{
    if (!ice_physics(g) || g->ice_slide || g->on_ladder) return;
    int n = g->ice_steps >> 1; if (!n) return; if (n > 9) n = 10;
    g->ice_slide = (uint8_t)n; g->ice_steps = 0;
}
static void ice_slide_step(Game *g)                                                                     /* 64BB */
{
    if (!ice_physics(g) || g->vstate != V_GROUND || !g->ice_slide) return;
    g->ice_slide--;
    uint8_t v = g->ring[wrap_down(game_hero_cell(g) + 0x6D)];
    if (v >= 0x40 && v < 0x49) { g->ice_slide = 0; return; }
    if (g->slide_dir & 1) { if (g->walk_dir != 1) try_move_right(g); }
    else                  { if (g->walk_dir != 2) try_move_left(g); }
}

/* 663E */
static void walk_left(Game *g)
{
    g->regen_tick = 0;
    if (!(g->hero_flags & FACE_LEFT)) { turn_around(g); return; }
    if (g->crouching) return;
    if (g->conveyor == 1) { stop_walking(g); return; }
    if (try_move_left(g)) { stop_walking(g); return; }
    g->walk_dir = 2;
    if (g->on_ladder) return;
    if (ice_physics(g) && !g->ice_slide) { g->slide_dir = 0; g->ice_steps++; }
    g->hero_flags |= WALKING;
    if (g->vstate == V_GROUND) { g->hero_anim = (uint8_t)((g->hero_anim + 1) & 0x7F); g->door_msg_latch = 0; }
}
/* 67C6 */
static void walk_right(Game *g)
{
    g->regen_tick = 0;
    if (g->hero_flags & FACE_LEFT) { turn_around(g); return; }
    if (g->crouching) return;
    if (g->conveyor == 2) { stop_walking(g); return; }
    if (try_move_right(g)) { stop_walking(g); return; }
    g->walk_dir = 1;
    if (g->on_ladder) return;
    if (ice_physics(g) && !g->ice_slide) { g->slide_dir = 1; g->ice_steps++; }
    g->hero_flags |= WALKING;
    if (g->vstate == V_GROUND) { g->hero_anim = (uint8_t)((g->hero_anim + 1) & 0x7F); g->door_msg_latch = 0; }
}

/* 6545: one row of rise while "up" is held, at most max_rise rows */
static void rise(Game *g)
{
    if (++g->ice_slide > 9) g->ice_slide = 10;
    if (g->on_ladder) return;
    g->crouching = 0;
    if (g->rise_rows < g->max_rise) {
        int s = wrap_up(game_hero_cell(g) - 0x23);                     /* row-1, col+1 */
        if (passable_wall(g->ring[s])) {
            g->hero_anim = 0; g->hero_flags &= (uint8_t)~WALKING; g->vstate = V_RISE;
            g->conveyor_kick = g->max_rise >> 1;
            g->rise_rows++;
            if (g->hero_scr_row < 7) scroll_up(g); else g->hero_scr_row--;
            return;
        }
        if (g->rise_rows == 0) { if (!g->on_ladder) g->hero_anim = 0x80; return; }
    }
    stop_rising(g);
}

/* 65C5 */
static void ladder_mount(Game *g)
{
    int tl = game_hero_cell(g);
    if (is_ladder(g->ring[wrap_down(tl + 1)])) {
        g->on_ladder = 0xFF; g->crouching = 0;
        for (;;) {
            int s = wrap_up(game_hero_cell(g) - 0x23);
            g->hero_anim--;
            if (!is_ladder(g->ring[s])) { g->hero_anim |= 1; return; }
            scroll_up(g); frame(g);
            if (g->hero_anim & 1) return;
        }
    }
    if (is_ladder(g->ring[tl])) { if (g->hero_flags & FACE_LEFT) walk_left(g); return; }
    if (is_ladder(g->ring[wrap_down(tl + 2)])) { if (!(g->hero_flags & FACE_LEFT)) walk_right(g); return; }
}

/* 7A83: returns 1 when it consumed the "up" (door entered, message, or side step) */
static int door_check(Game *g)
{
    int s = wrap_up(game_hero_cell(g) - 0x25);                          /* row-1, col-1 */
    if (g->ring[s] == DOOR_CELL) { if (g->hero_flags & FACE_LEFT) { walk_left(g); return 1; } return 0; }
    if (g->ring[wrap_down(s + 2)] == DOOR_CELL) { if (!(g->hero_flags & FACE_LEFT)) { walk_right(g); return 1; } return 0; }
    if (g->ring[wrap_down(s + 1)] != DOOR_CELL) return 0;
    int col = game_hero_map_col(g);
    int row = (g->hero_scr_row - 1 + g->scroll_row) & 0x3F;
    const Door *d = NULL;
    for (int i = 0; i < g->map->ndoors; i++)
        if (g->map->doors[i].col == col && g->map->doors[i].row == row) { d = &g->map->doors[i]; break; }
    if (!d) return 0;
    if (!(d->letter & 0x80)) {
        /* 7E15: would consume a key / lion key; keys are stubbed */
        if (!g->door_msg_latch) {
            g->door_msg_latch = 0xFF;
            snprintf(g->message, sizeof g->message, "Can't open this door.");
            fprintf(stderr, "[door] locked door at (%d,%d) -> map %02x (%d,%d): %s\n",
                    d->col, d->row, d->dest_map, d->dest_col, d->dest_row, g->message);
        }
        return 1;
    }
    fprintf(stderr, "[door] (%d,%d) letter %02x -> %s map %02x at (%d,%d) flags %02x%s\n",
            d->col, d->row, d->letter, d->dest_row == 0xFF ? "TOWN" : "cavern", d->dest_map,
            d->dest_col, d->dest_row, d->dflags, d->flag_ptr != 0xFFFF ? " (sets a story flag)" : "");
    if (g->on_door) g->on_door(g, d);
    return 1;
}

/* 6537 */
static void jump_up(Game *g)
{
    g->regen_tick = 0;
    if (door_check(g)) return;                                          /* 7AF6 pops jump_up's return */
    /* elevator_up (8074): elevators are not implemented yet */
    ladder_mount(g);
    rise(g);
}
static void jump_left(Game *g)  { g->diag_jump = 0xFF; rise(g); walk_left(g); }    /* 6634 */
static void jump_right(Game *g) { g->diag_jump = 0xFF; rise(g); walk_right(g); }   /* 67BC */

/* 6AC9 */
static void down_pressed(Game *g)
{
    g->regen_tick = 0;
    if (g->conveyor) return;
    /* elevator_down (7FDC): not implemented */
    int s = wrap_down(game_hero_cell(g) + 0x6D);                         /* row+3, col+1 */
    if (is_ladder(g->ring[s])) {
        for (;;) {
            s = wrap_down(game_hero_cell(g) + 0x6D);
            g->hero_anim++;
            if (!passable_wall(g->ring[s])) { g->hero_anim |= 1; return; }
            scroll_down(g); frame(g);
            if (g->hero_anim & 1) return;
        }
    }
    if (g->on_ladder) { g->on_ladder = V_KNOCK; g->vstate = V_KNOCK; return; }
    g->crouch_release = 0; g->crouching = 0xFF;
}

/* 63DA */
static void unstick_from_wall(Game *g)
{
    if (g->crouching || g->vstate != V_GROUND) return;
    int tl = game_hero_cell(g);
    if (passable_wall(g->ring[tl])) return;
    if (passable_wall(g->ring[wrap_down(tl + 2)])) return;
    int s = wrap_down(tl + 2 + RING_W);
    if (passable_wall(g->ring[s])) scroll_right(g); else scroll_left(g);
}

/* 6B76: 1 = standing */
static int floor_under_hero(Game *g)
{
    int c = wrap_down(game_hero_cell(g) + 0x6D);                         /* row+3, col+1 */
    int spr; uint8_t t;
    t = cell_or_type(g, c, &spr);           if (spr && (t & 0x80)) return 1;
    t = cell_or_type(g, wrap_up(c - 1), &spr); if (spr && (t & 0x80)) return 1;
    if (!passable_body(g->ring[c])) return 1;
    if (g->hero_anim == 0x80) return 0;
    if (passable_body(g->ring[wrap_up(c - 1)])) return 0;
    return !passable_body(g->ring[wrap_down(c + 1)]);
}

/* 6A1E / 6A4A */
static void ledge_step_right(Game *g)
{
    int s = wrap_down(game_hero_cell(g) + 0x6D);
    if (!passable_wall(g->ring[s])) return;
    if (passable_wall(g->ring[wrap_down(s + 1)])) return;
    try_move_right(g);
}
static void ledge_step_left(Game *g)
{
    int s = wrap_down(game_hero_cell(g) + 0x6D);
    if (!passable_wall(g->ring[s])) return;
    if (passable_wall(g->ring[wrap_up(s - 1)])) return;
    try_move_left(g);
}

/* 6B41: returns 0 when hero_input must be skipped this frame */
static int land(Game *g)
{
    if (g->vstate != V_FALL) return 1;
    uint8_t fell = g->fall_rows;
    g->vstate = V_GROUND; g->crouch_release = 0; g->fall_rows = 0; g->hero_anim = 0x80;
    if (g->conveyor) return 0;
    if (fell >= 2) g->crouching = 0xFF;
    return 0;
}

/* 6A67 */
static void conveyor_check(Game *g)
{
    g->conveyor = 0;
    int k = conveyor_kind(g, g->ring[wrap_down(game_hero_cell(g) + 0x49)]);   /* row+2, col+1 */
    if (!k) return;
    g->hero_flags &= (uint8_t)~WALKING; g->conveyor = (uint8_t)k;
    if (g->conveyor_kick) {
        if (g->shoes == 3) return;
        g->conveyor_kick--;
        if (k == 1) try_move_right(g); else try_move_left(g);
        return;
    }
    if ((g->conveyor_phase++ & 3) != 0) return;
    if (k == 1) { if (!(g->dirs & DIR_LEFT))  try_move_right(g); }
    else        { if (!(g->dirs & DIR_RIGHT)) try_move_left(g); }
}

/* 695A: returns 1 when hero_input should run afterwards */
static int gravity(Game *g)
{
    if (g->on_updraft) return 1;
    if (g->vstate & 0x80) return 1;
    /* elevator_ride (818E): not implemented */
    conveyor_check(g);
    if (floor_under_hero(g)) return land(g);

    g->fall_rows++;
    if (g->rise_rows) { g->rise_rows--; g->hero_scr_row++; } else scroll_down(g);
    if (!(g->hero_flags & WALKING)) {
        if (is_ladder(g->ring[wrap_down(game_hero_cell(g) + 0x49)])) { g->on_ladder = 0xFF; return 0; }
    }
    g->hero_anim = 0x80;
    uint8_t was = g->vstate; g->vstate = V_FALL;
    if (g->conveyor || g->hero_dead) return 0;
    if (was == V_GROUND) {                                              /* 69CB: walked off an edge */
        if (g->hero_flags & FACE_LEFT) walk_left(g); else walk_right(g);
        g->hero_flags &= (uint8_t)~WALKING;
        return 0;
    }
    uint8_t d = g->dirs & 0xC;                                          /* 69E6: air control */
    if (d == DIR_LEFT && !(g->hero_flags & FACE_LEFT)) { g->hero_flags &= (uint8_t)~WALKING; turn_around(g); ledge_step_right(g); return 0; }
    if (d == DIR_RIGHT && (g->hero_flags & FACE_LEFT)) { g->hero_flags &= (uint8_t)~WALKING; turn_around(g); ledge_step_left(g); return 0; }
    if (g->hero_flags & WALKING) { if (g->hero_flags & FACE_LEFT) walk_left(g); else walk_right(g); return 0; }
    if (d == DIR_LEFT)  ledge_step_left(g);
    if (d == DIR_RIGHT) ledge_step_right(g);
    return 0;
}

/* 6343 */
static void hero_input(Game *g)
{
    g->walk_dir = 0;
    uint8_t dirs = g->dirs;
    if (dirs == (DIR_UP | DIR_LEFT))  { jump_left(g);  return; }
    if (dirs == (DIR_UP | DIR_RIGHT)) { jump_right(g); return; }
    if (dirs == DIR_UP)               { jump_up(g);    return; }

    if (!g->on_ladder && g->vstate != V_GROUND) {
        if (!g->diag_jump) { stop_rising(g); return; }
        g->diag_jump = 0;
        if (!(g->hero_flags & WALKING)) { stop_rising(g); return; }
        if (g->hero_flags & FACE_LEFT) walk_left(g); else walk_right(g);
        stop_rising(g);
        return;
    }
    uint8_t f = g->hero_flags & FACE_LEFT;
    if (f != g->prev_facing) ice_slide_start(g);
    g->prev_facing = f;
    if (dirs == DIR_DOWN) down_pressed(g);
    if ((dirs & 0xC) == DIR_LEFT)  { walk_left(g);  return; }
    if ((dirs & 0xC) == DIR_RIGHT) { walk_right(g); return; }
    ice_slide_start(g);
    if (!g->on_ladder && !g->crouching) g->hero_anim = 0x80;
}

/* 7699 / 76F6: updraft and current tiles under the hero's centre column.
 * Returns 1 when the frame must be aborted before rendering (76C2). */
static int special_tiles_check(Game *g)
{
    g->on_updraft = 0;
    int s = wrap_down(game_hero_cell(g) + 0x49);
    for (int n = 3; n; n--, s = wrap_up(s - RING_W)) {
        int k = special_tile(g, g->ring[s]);
        if (k < 0) continue;
        switch (k) {
        case 0: scroll_up(g); g->on_updraft = 0xFF; g->vstate = V_GROUND; g->hero_anim = 0x80; return 1;
        case 1: try_move_left(g);  try_move_left(g);  return 1;
        default: try_move_right(g); try_move_right(g); return 1;
        }
    }
    return 0;
}

/* 7981: ring column of map column (col+3); returns -1 when off the ring */
static int door_view_col(const Game *g, int col)
{
    int c = col + 3; if (c >= g->map->width) c -= g->map->width;
    int d = c - g->scroll_col;
    if (d < 0) { if (c > 0x27) return -1; d = c + g->map->width - g->scroll_col; }
    return d > 0x27 ? -1 : d;
}

/* 78DD: write the 5x4 door arch (DCHR cells) into the ring for every door
 * record in view; the letter tile 0x61+(letter&7) sits in the top row. */
static void signs_draw(Game *g)
{
    static const uint8_t LOCKED[20]   = {0x49, 0x4A, 0x61, 0x4B, 0x4C, 0x4D, 0x4F, 0x50, 0x51, 0x4E, 0x5F, 0x52, 0x53, 0x54, 0x60, 0x5F, 0x55, 0x56, 0x57, 0x60};
    static const uint8_t UNLOCKED[20] = {0x49, 0x4A, 0x61, 0x4B, 0x4C, 0x4D, 0x58, 0x00, 0x59, 0x4E, 0x5F, 0x5A, 0x00, 0x5B, 0x60, 0x5F, 0x5C, 0x5D, 0x5E, 0x60};
    for (int i = 0; i < g->map->ndoors; i++) {
        const Door *d = &g->map->doors[i];
        int vc = door_view_col(g, d->col);
        if (vc < 0) continue;
        uint8_t tmpl[20];
        memcpy(tmpl, (d->letter & 0x80) ? UNLOCKED : LOCKED, 20);
        tmpl[2] = (uint8_t)(0x61 + (d->letter & 7));
        int ncols, first, ring_col;
        if (vc < 4) { ncols = vc + 1; first = 5 - ncols; ring_col = 0; }
        else { ncols = 40 - vc; if (ncols > 5) ncols = 5; first = 0; ring_col = vc - 4; }
        int di = (d->row & 0x3F) * RING_W + ring_col;
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < ncols; c++) {
                int p = wrap_down(di + c);
                if (!(g->ring[p] & 0x80)) g->ring[p] = tmpl[r * 5 + first + c];
            }
            di = wrap_down(di + RING_W);
        }
    }
}

/* 7FB1 / 8163 / 81AE: fixture cells written into the ring each frame (static; the
 * elevator motion and fixture-C variants are not implemented). */
static void fixtures_draw(Game *g)
{
    for (int i = 0; i < g->map->nfix; i++) {
        const Fixture *f = &g->map->fix[i];
        for (int k = 0; k < 3; k++) {
            int mc = f->col + k; if (mc >= g->map->width) mc -= g->map->width;
            int d = mc - g->scroll_col; if (d < 0) d += g->map->width;
            if (d < 0 || d >= RING_W) continue;
            int p = (f->row & 0x3F) * RING_W + d;
            if (!(g->ring[p] & 0x80)) g->ring[p] = (uint8_t)(f->cell + k);
        }
    }
}

/* 74A0: hazard tiles (damage is logged/counted only) */
static void hazard_check(Game *g)
{
    if (g->shoes == 2) return;
    g->on_hazard = 0;
    int s = game_hero_cell(g), rows = 3;
    if (g->crouching) { s = wrap_down(s + RING_W); rows--; }
    for (; rows; rows--) {
        for (int c = 0; c < 3; c++) if (is_hazard(g, g->ring[wrap_down(s + c)])) g->on_hazard = 0xFF;
        s = wrap_down(s + RING_W);
    }
    if (!g->on_ladder && is_hazard(g, g->ring[wrap_down(s + 1)])) g->on_hazard = 0xFF;
    if (g->on_hazard) {                                                 /* 7505 */
        static const uint8_t hazard_damage[9] = {1, 1, 4, 8, 20, 20, 20, 20, 20};
        int c = g->map->cavern; if (c < 1) c = 1; if (c > 9) c = 9;
        g->hero_hit_flash = 0xFF; g->sfx_request = 9; g->hazard_frames++;
        hero_damage(g, hazard_damage[c - 1]);
    }
}

/* 6F9B: the per-frame simulation of everything but the hero, then render + wait */
static void frame(Game *g)
{
    g->max_rise = g->shoes == 1 ? 4 : 2;
    if (special_tiles_check(g)) return;                                 /* 76C2: frame aborted */
    if (g->vstate == V_GROUND) {                                        /* 6FAC vertical re-centring */
        g->rise_rows = 0;
        if (g->hero_home_row != g->hero_scr_row) {
            if (g->hero_home_row < g->hero_scr_row) { scroll_down(g); g->hero_scr_row--; }
            else                                    { scroll_up(g);   g->hero_scr_row++; }
        }
    }
    if (g->hero_scr_col != 0x0C) { try_move_left(g); g->hero_scr_col++; }  /* 6FF9 */
    g->hero_map_row = (uint8_t)((g->hero_scr_row + g->scroll_row) & 0x3F);
    fixtures_draw(g);
    signs_draw(g);
    if (!g->boss_defeated) enemies_update(g);                           /* 8D19 */
    g->hero_hit_flash = 0; g->hero_hit = 0;
    hero_enemy_contact(g);                                              /* 751F */
    /* shots_update / orbs_update (8422/86FC): no projectiles yet */
    hazard_check(g);
    if (g->map->cavern == 7 && g->shoes != 5 && ((++g->heat_timer & 0x3F) == 0)) {   /* 704F */
        g->hero_hit_flash = 0xFF; g->sfx_request = 9; hero_damage(g, 0x0F);
        snprintf(g->message, sizeof g->message, "It's too hot !!");
    }
    if (g->hero_dead) g->hero_hit_flash = 0; else g->hero_hidden = 0;
    g->frame_no++;
    g->rng += 20;                                                       /* FF1B: ticks per frame at speed 5 */
    if (g->present) g->present(g);                                      /* draw + wait 4*speed ticks + poll input */
    sword_apply(g);                                                     /* 7147: second half of the frame */
    if (g->hero_dead) return;
    if (g->hp == 0) { hero_die(g); return; }                            /* 718C */
    if (++g->regen_tick >= 0x10) {                                      /* 719E */
        g->regen_tick = 0;
        if (g->hp < g->max_hp) { g->hp += 2; if (g->hp > g->max_hp) g->hp = g->max_hp; }
    }
    if (g->hp_regen_pending) {                                          /* 70D9: potion */
        g->hp_regen_pending--; g->hp += 8;
        if (g->hp > g->max_hp) { g->hp = g->max_hp; g->hp_regen_pending = 0; }
    }
}

/* 62DB: main-loop variant while on a ladder */
static void ladder_step(Game *g)
{
    g->crouching = 0; g->vstate = V_GROUND; g->conveyor = 0; g->attacking = 0;
    frame(g);
    hero_knockback(g);
    hero_input(g);
    if (g->on_ladder == 0xFF) {
        int tl = game_hero_cell(g);
        if (is_ladder(g->ring[wrap_down(tl + 1)])) return;
        if (is_ladder(g->ring[wrap_down(tl + 1 + RING_W)])) return;
    }
    g->hero_flags &= (uint8_t)~WALKING; g->on_ladder = 0;
    g->ice_slide = g->ice_steps = 0; g->hero_anim = 0x7F;
}

void game_step(Game *g)
{
    if (g->on_ladder) { ladder_step(g); return; }
    sword_input(g);                                                     /* 6E3B */
    ice_slide_step(g);
    frame(g);
    /* magic_input (87B0): stub */
    unstick_from_wall(g);
    hero_knockback(g);                                                  /* 6412 */
    if (++g->crouch_release == 2) g->crouching = 0;
    if (g->dirs & DIR_DOWN) g->hero_flags &= (uint8_t)~WALKING;
    if (gravity(g)) hero_input(g);
}

void game_first_frame(Game *g) { frame(g); }

void game_init(Game *g, const Map *m, const Tileset *t)
{
    memset(g, 0, sizeof *g);
    g->map = m; g->tiles = t;
    g->hero_anim = 0x80; g->hp = g->max_hp = 0x50;
    g->sword = 1;                                                       /* the training sword */
    g->rng = 0x1234;
    g->max_rise = 2; g->hero_scr_col = 0x0C; g->hero_scr_row = m->row_bias; g->hero_home_row = m->row_bias;
}

void game_place(Game *g, int col, int row, int face_left)
{
    const Map *m = g->map;
    g->scroll_col = col - 16; while (g->scroll_col < 0) g->scroll_col += m->width;
    g->scroll_col %= m->width;
    g->hero_home_row = m->row_bias; g->hero_scr_row = m->row_bias; g->hero_scr_col = 0x0C;
    g->scroll_row = (uint8_t)((row - m->row_bias) & 0x3F);
    g->hero_flags = face_left ? FACE_LEFT : 0;
    g->hero_anim = 0x80; g->vstate = V_GROUND; g->on_ladder = 0; g->crouching = 0;
    g->fall_rows = g->rise_rows = 0; g->diag_jump = 0;
    g->attacking = 0; g->attack_var = 0; g->hero_dead = 0; g->death_anim = 0;
    g->entry_col = col; g->entry_row = row; g->entry_face = (uint8_t)(face_left ? 1 : 0);
    ring_fill(g);
    enemies_load(g);
}

void game_enter(Game *g, const Map *m, const Tileset *t, int dest_col, int dest_row, int face_left)
{
    g->map = m; g->tiles = t;
    game_place(g, dest_col, dest_row + 1, face_left);                    /* 7DC1: scroll_row = dest_row+1-row_bias */
}
