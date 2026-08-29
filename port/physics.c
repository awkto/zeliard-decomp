/* physics.c — port of the hero/camera part of src/fight.c.  Function names and
 * the hex tags match fight.c; the ring is addressed with linear indices and the
 * same single wrap the original applies to pointers (6D82/6D8E). */
#include "physics.h"
#include "boss.h"
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
    shots_shift(g, 1);                                                  /* 864E */
}
static void scroll_right(Game *g)                                                                       /* 68A0 */
{
    memmove(g->ring, g->ring + 1, RING_SIZE - 1);
    int c = g->scroll_col + 0x23; if (c >= g->map->width) c -= g->map->width;
    ring_put_column(g, 0x23, c);
    if (++g->scroll_col == g->map->width) g->scroll_col = 0;
    shots_shift(g, 0);                                                  /* 8639 */
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
static int  elevator_up(Game *g);
static int  elevator_down(Game *g);
static int  fixture_ride(Game *g);
static void message_tick(Game *g);
static void walk_in_step(Game *g);

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

/* 0x7E15  spend a key (dflags bit0 = lion key) on a locked door: the letter's
 * bit 7 is set so the next "up" opens it, and the door's story flag is OR-ed
 * with its mask.  Returns 1 when the door was unlocked. */
static int door_unlock(Game *g, Door *d)
{
    uint8_t *n = (d->dflags & 1) ? &g->lion_keys : &g->keys;
    if (!*n) return 0;
    (*n)--;
    g->sfx_request = 0x15;
    d->letter |= 0x80;
    if (d->flag_ptr != 0xFFFF) g->page[d->flag_ptr & 0xFF] |= d->flag_mask;   /* 7E39 */
    game_message(g, (d->dflags & 1) ? fight_message(MSG_LION_KEY) : fight_message(MSG_KEY));
    return 1;
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
    Door *d = NULL;
    for (int i = 0; i < g->map->ndoors; i++)
        if (g->map->doors[i].col == col && g->map->doors[i].row == row) { d = &((Map *)g->map)->doors[i]; break; }
    if (!d) return 0;
    if (!(d->letter & 0x80)) {                                          /* 7AF7: locked */
        if (door_unlock(g, (Door *)d)) {                                /* 7E15: a key opens it */
            g->hero_anim = 0x80; g->ice_steps = 0;                      /* 7B03 */
            return 1;
        }
        g->hero_anim = 0x80; g->ice_steps = 0;
        if (!g->door_msg_latch) {                                       /* 7B0D */
            g->door_msg_latch = 0xFF;
            g->sfx_request = 0x16;
            game_message(g, fight_message(MSG_DOOR_LOCKED));            /* 9AC5 */
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
    if (elevator_up(g)) return;                                         /* 8074 */
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
    if (elevator_down(g)) return;                                        /* 7FDC */
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
    if (fixture_ride(g)) return 1;                                      /* 818E */
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

/* ------------------------------------------------------------- fixtures */
/* Fixture lists A/B/C (docs/FIGHT.md §5, src/fight.c 7FB1 / 8163 / 81AE).
 * The cells live only in the ring, so every frame the previous three cells are
 * restored from the map grid and the new ones written (8352 writes into
 * under_sprite[] when a sprite marker covers the cell). */

/* 0x82F8  ring column of a fixture's leftmost cell; -1 when off the ring. */
static int fixture_rcol(const Game *g, uint16_t col)
{
    int d = (int)col - g->scroll_col;
    if (d < 0) d += g->map->width;
    return (d < 0 || d > 0x21) ? -1 : d;
}

/* 0x8352  write a cell into the ring, or under a sprite marker. */
static void fixture_put(Game *g, int p, uint8_t v)
{
    uint8_t cur = g->ring[p];
    if (cur & 0x80) { int i = cur & 0x7F; if (i < MAX_OBJS) g->under_sprite[i] = v; return; }
    g->ring[p] = v;
}
static uint8_t map_cell(const Game *g, int col, int row)
{
    while (col < 0) col += g->map->width;
    while (col >= g->map->width) col -= g->map->width;
    return g->map->grid[col][row & 0x3F];
}
static void fixture_erase(Game *g, Fixture *f)
{
    if (!f->drawn) return;
    f->drawn = 0;
    int rc = fixture_rcol(g, f->drawn_col);
    if (rc < 0) return;
    for (int k = 0; k < 3 && rc + k < RING_W; k++)
        fixture_put(g, game_ring_index(g, f->drawn_row, (uint8_t)(rc + k)),
                    map_cell(g, (int)f->drawn_col + k, f->drawn_row));
}
static void fixture_draw_one(Game *g, Fixture *f)
{
    int rc = fixture_rcol(g, f->col);
    if (rc < 0) return;
    for (int k = 0; k < 3 && rc + k < RING_W; k++)
        fixture_put(g, game_ring_index(g, f->row, (uint8_t)(rc + k)), (uint8_t)(f->cell + k));
    f->drawn = 1; f->drawn_col = f->col; f->drawn_row = f->row;
}

/* 0x82B4  is the hero standing on this fixture?  (grounded, its row is the row
 * under his feet, and one of its three columns is under his body). */
static int hero_on_fixture(const Game *g, const Fixture *f)
{
    if (g->vstate || g->on_ladder) return 0;
    if ((uint8_t)((g->hero_scr_row + g->scroll_row + 3) & 0x3F) != (f->row & 0x3F)) return 0;
    int rc = fixture_rcol(g, f->col);
    if (rc < 0) return 0;
    int hc = g->hero_scr_col + 4;
    return hc >= rc && hc < rc + 3;
}

/* 0x8244/0x8252  fixture C: patrol between lim_l and lim_r, carrying the hero. */
static void fixture_c_move(Game *g, Fixture *f)
{
    if (!f->var) return;
    if (f->var == 1 && !(g->fixture_anim & 1)) return;                  /* 824A: half speed */
    uint8_t paused = (uint8_t)(f->state & 0x40);
    f->state &= (uint8_t)~0x40;                                         /* 8255 */
    if (paused) return;
    int carry = hero_on_fixture(g, f);
    int col;
    if (!(f->state & 0x80)) {                                           /* 8265: moving right */
        col = f->col + 1;
        if (col >= g->map->width) col -= g->map->width;
        if (carry) try_move_right(g);                                   /* 8276: 684C */
    } else {                                                            /* 8280: moving left */
        col = (int)f->col - 1;
        if (col < 0) col += g->map->width;
        if (carry) try_move_left(g);                                    /* 8291: 66A5 */
    }
    f->col = (uint16_t)col;
    uint16_t lim = (f->state & 0x80) ? f->lim_l : f->lim_r;             /* 827B / 8296 */
    if (col == (int)lim) { f->state ^= 0x80; f->state |= 0x40; }        /* 82AB */
}

/* the live copy of the map's fixture lists (they move, the map must not) */
static void fixtures_load(Game *g)
{
    g->nfix = g->map->nfix;
    if (g->nfix > 256) g->nfix = 256;
    for (int i = 0; i < g->nfix; i++) { g->fix[i] = g->map->fix[i]; g->fix[i].drawn = 0; }
    g->fixture_anim = 0;
}

static void fixtures_draw(Game *g)
{
    g->fixture_anim++;                                                  /* 81AE */
    for (int i = 0; i < g->nfix; i++) fixture_erase(g, &g->fix[i]);
    for (int i = 0; i < g->nfix; i++) {
        Fixture *f = &g->fix[i];
        if (f->kind == 2) fixture_c_move(g, f);
        fixture_draw_one(g, f);
    }
}

/* 0x814C  which of the three cells of a `first`-based fixture is `v`?
 * Returns the offset 0..2, or -1. */
static int fixture_cell_index(uint8_t v, uint8_t first) { return (uint8_t)(v - first) < 3 ? v - first : -1; }

/* 0x8109  the record whose leftmost cell sits `idx` cells left of the hero's
 * body column, on the row under his feet. */
static Fixture *fixture_under_hero(Game *g, int kind, int idx)
{
    int col = g->scroll_col + g->hero_scr_col + 4 + 1 - idx;
    while (col >= g->map->width) col -= g->map->width;
    while (col < 0) col += g->map->width;
    uint8_t row = (uint8_t)((g->scroll_row + g->hero_scr_row + 3) & 0x3F);
    for (int i = 0; i < g->nfix; i++)
        if (g->fix[i].kind == kind && g->fix[i].col == (uint16_t)col && (g->fix[i].row & 0x3F) == row)
            return &g->fix[i];
    return NULL;
}

/* 0x8024  move a platform one row (dr = +1 down, -1 up) if the three cells it
 * would occupy are empty and free of sprites.  Returns 1 when it moved. */
static int fixture_shift_row(Game *g, Fixture *f, int dr)
{
    int rc = fixture_rcol(g, f->col);
    if (rc < 0) return 0;
    uint8_t nrow = (uint8_t)((f->row + dr) & 0x3F);
    int probe = game_ring_index(g, nrow, (uint8_t)rc);
    for (int k = -1; k < 3; k++) {                                      /* 802B: also the cell left of it */
        int p = game_ring_add(probe, k);
        if (k < 0) { if (g->ring[p] & 0x80) return 0; continue; }
        if (g->ring[p] != 0) return 0;                                  /* 803C: must be empty */
    }
    fixture_erase(g, f);
    f->row = nrow;
    fixture_draw_one(g, f);
    return 1;
}

/* 0x7FDC  "down" on an elevator (fixture A): the platform and the hero sink
 * one row.  Returns 1 when the rest of down_pressed must be skipped (8000). */
static int elevator_down(Game *g)
{
    if (g->on_ladder) return 0;
    uint8_t v = g->ring[wrap_down(game_hero_cell(g) + 0x6D)];            /* row+3, col+1 */
    int idx = fixture_cell_index(v, 0x40);
    if (idx < 0) return 0;
    Fixture *f = fixture_under_hero(g, 0, idx);
    if (!f) return 0;
    if (!fixture_shift_row(g, f, 1)) return 0;
    g->hero_anim = 0x80;
    scroll_down(g);                                                     /* 8006 */
    return 1;
}

/* 0x8074  "up" on an elevator: the platform and the hero rise one row. */
static int elevator_up(Game *g)
{
    if (g->on_ladder) return 0;
    if (!passable_wall(g->ring[wrap_up(game_hero_cell(g) - 0x23)])) return 0;   /* 8085: head room */
    uint8_t v = g->ring[wrap_down(game_hero_cell(g) + 0x6D)];
    int idx = fixture_cell_index(v, 0x40);
    if (idx < 0) return 0;
    Fixture *f = fixture_under_hero(g, 0, idx);
    if (!f) return 0;
    if (!fixture_shift_row(g, f, -1)) return 0;
    g->hero_anim = 0x80;
    scroll_up(g);
    return 1;
}

/* 0x818E  a fixture-B gate sinks one row per frame while it carries the hero. */
static int fixture_ride(Game *g)
{
    uint8_t v = g->ring[wrap_down(game_hero_cell(g) + 0x6D)];
    int idx = fixture_cell_index(v, 0x43);
    if (idx < 0) return 0;
    Fixture *f = fixture_under_hero(g, 1, idx);
    if (!f) return 0;
    if (!fixture_shift_row(g, f, 1)) return 0;
    scroll_down(g);                                                     /* 81AB */
    return 1;
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
    if (g->boss_map || g->boss_room) {                                  /* 6FD3: the boss AI owns the camera column */
        if (g->hero_scr_col != g->boss.cam_col) { try_move_right(g); g->hero_scr_col--; }
    } else if (g->hero_scr_col != 0x0C) { try_move_left(g); g->hero_scr_col++; }  /* 6FF9 */
    g->hero_map_row = (uint8_t)((g->hero_scr_row + g->scroll_row) & 0x3F);
    fixtures_draw(g);
    signs_draw(g);
    magic_update(g);                                                    /* 8AAD */
    if (!g->boss_defeated) enemies_update(g);                           /* 8D19 */
    g->hero_hit_flash = 0; g->hero_hit = 0;
    hero_enemy_contact(g);                                              /* 751F */
    shots_update(g);                                                    /* 8422 */
    orbs_update(g);                                                     /* 86FC */
    hazard_check(g);
    if (g->map->cavern == 7 && g->shoes != 5 && ((++g->heat_timer & 0x3F) == 0)) {   /* 704F */
        g->hero_hit_flash = 0xFF; g->sfx_request = 9; hero_damage(g, 0x0F);
        game_message(g, fight_message(MSG_TOO_HOT));                    /* 9BB9 */
    }
    message_tick(g);                                                    /* 7210 */
    if (g->hero_dead) g->hero_hit_flash = 0; else g->hero_hidden = 0;
    g->frame_no++;
    g->rng += 20;                                                       /* FF1B: ticks per frame at speed 5 */
    sound_request(g);                                                   /* FF75 -> the sound stub */
    if (g->present) g->present(g);                                      /* draw + wait 4*speed ticks + poll input */
    orbs_update(g);                                                     /* 7133: the second orb pass */
    sword_apply(g);                                                     /* 7147: second half of the frame */
    sound_request(g);
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
    if (g->post_boss_pending) { post_boss_transition(g); return; }       /* 71C2 -> 72F1 */
    boss_rewards(g);                                                     /* 71CC */
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

/* 60E6: the six flashes of the ENCNT.GRP encounter card, 0x41 ticks apart.
 * No simulation runs while it is up; the renderer draws the card on the odd
 * halves (VID_TEXTBOX_FILL(BX=0x0C28, CX=0x3828) clears it on the even ones). */
static void encounter_step(Game *g)
{
    g->encounter_frames--;
    g->frame_no++;
    g->rng += 20;
    if (g->present) g->present(g);
}

void game_step(Game *g)
{
    if (g->encounter_frames) { encounter_step(g); return; }             /* 60E6 */
    if (g->walk_in) { walk_in_step(g); return; }                        /* 7C6E */
    if (g->on_ladder) { ladder_step(g); return; }
    sword_input(g);                                                     /* 6E3B */
    ice_slide_step(g);
    frame(g);
    magic_input(g);                                                     /* 87B0 */
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
    g->town_map = 0x81;                                                 /* [C5] Muralla Town */
    g->max_rise = 2; g->hero_scr_col = 0x0C; g->hero_scr_row = m->row_bias; g->hero_home_row = m->row_bias;
    shots_clear(g);
    for (int i = 0; i < 4; i++) g->orbs[i].phase = 0xFF;                /* 7A04 */
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
    g->casting = g->magic_active = g->cast_timer = 0;
    shots_clear(g);                                                     /* 83DB */
    for (int i = 0; i < 4; i++) { g->magic[i].live = 0; g->orbs[i].phase = 0xFF; }
    g->entry_col = col; g->entry_row = row; g->entry_face = (uint8_t)(face_left ? 1 : 0);
    ring_fill(g);
    fixtures_load(g);
    enemies_load(g);
}

void game_enter(Game *g, const Map *m, const Tileset *t, int dest_col, int dest_row, int face_left)
{
    g->map = m; g->tiles = t;
    game_place(g, dest_col, dest_row + 1, face_left);                    /* 7DC1: scroll_row = dest_row+1-row_bias */
}

/* ------------------------------------------------------------- messages */
/* fight.bin's message table at 9A1E: {u16 x, chars, 0xFF} records.  The port
 * keeps the strings (the box renderer 740E/7210 is not ported) and the
 * 32-frame lifetime of msg_box_a (7210: the box is erased when the low 5 bits
 * of 9EED wrap). */
static const char *const MESSAGES[MSG_COUNT] = {
    "You get 50 golds.",            /* 9A1E */
    "You get 100 golds.",           /* 9A32 */
    "You get 500 golds.",           /* 9A47 */
    "You get 1000 golds.",          /* 9A5C */
    "You get a Key.",               /* 9A72 */
    "You have recovered.",          /* 9A83 */
    "You have recovered full.",     /* 9A99 */
    "Shield broken.",               /* 9AB4 */
    "Can't open this door.",        /* 9AC5 */
    "Nothing in the box.",          /* 9ADD */
    "You get the Hero's Crest.",    /* 9AF3 */
    "You get the Ruzeria shoes.",   /* 9B0F */
    "You get the Glory Crest.",     /* 9B2C */
    "You get the Pirika shoes.",    /* 9B47 */
    "You get the Feruza shoes.",    /* 9B63 */
    "You get the Silkarn shoes.",   /* 9B7F */
    "Get the Enchantment sword.",   /* 9B9C */
    "It's too hot !!",              /* 9BB9 */
    "Get the lion's head Key.",     /* 9BCB */
};
const char *fight_message(int idx) { return (idx >= 0 && idx < MSG_COUNT) ? MESSAGES[idx] : ""; }

/* 0x73E0 */
void game_message(Game *g, const char *text)
{
    snprintf(g->message, sizeof g->message, "%s", text);
    g->msg_timer = 0; g->msg_box = 0xFF;
}
/* 0x7210  the box is erased 32 frames later. */
static void message_tick(Game *g)
{
    if (!g->msg_box) return;
    g->msg_timer = (uint8_t)((g->msg_timer + 1) & 0x1F);
    if (!g->msg_timer) { g->msg_box = 0; g->message[0] = 0; }
}

/* ------------------------------------------------------------- walk-in */
/* 0x7C6E  after a map transition the hero walks in over 26 frames on a blank
 * playfield: the sprite starts at x = 40 (or 256 when entering facing left)
 * and moves 8 px per frame, hero_anim advancing every frame. */
void game_start_walk_in(Game *g, int face_left)
{
    g->walk_in = 0x1A;
    g->walk_in_x = face_left ? 0x40 * 4 : 0x0A * 4;                     /* 7C7A / 7CB9 */
    g->walk_in_dir = face_left ? -8 : 8;
    if (face_left) g->hero_flags |= FACE_LEFT; else g->hero_flags &= (uint8_t)~FACE_LEFT;
    g->hero_entering = 0xFF;
}
static void walk_in_step(Game *g)
{
    g->hero_anim++;                                                     /* 7C82 */
    g->frame_no++;
    g->rng += 20;
    if (g->present) g->present(g);
    g->walk_in_x += g->walk_in_dir;
    if (--g->walk_in <= 0) {
        g->walk_in = 0; g->hero_entering = 0;
        g->hero_anim = 0x80;                                            /* 7D36 */
        g->hero_scr_col = 0x0C;                                         /* 7D28 */
        g->hero_scr_row = g->map->row_bias; g->hero_home_row = g->map->row_bias;
    }
}
