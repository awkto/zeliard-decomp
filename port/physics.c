/* physics.c — port of the hero/camera part of src/fight.c.  Function names and
 * the hex tags match fight.c; the ring is addressed with linear indices and the
 * same single wrap the original applies to pointers (6D82/6D8E). */
#include "physics.h"
#include "boss.h"
#include "enemy.h"
#include "status.h"
#include "audio.h"
#include <stdio.h>
#include <string.h>

static void check_status_menu(Game *g);   /* 7202, below */

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
uint8_t game_bg_cell(const Game *g, int sc, int sr) { return g->bg_ring[wrap_down(game_win(g) + sr * RING_W + sc + 4)]; }

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
int game_cell_hazard(const Game *g, uint8_t v) { return is_hazard(g, v); }
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
    /* a map load (or a boss refill) replaces the whole ring; keep bg_ring in
     * step so a render before the next frame's snapshot (the Tear cutscene,
     * tear.c, draws right after shell_enter_cavern) is not blank/stale */
    memcpy(g->bg_ring, g->ring, RING_SIZE);
}
/* 6771 / 68FF: after a scroll, every live record standing in the map column
 * that just came in gets its marker back in that ring column (the column
 * itself was refilled from the map).  The cell under it is not saved; FF4A
 * is left at the record count. */
static void remark_column(Game *g, int map_col, uint8_t ring_col)
{
    g->obj_index = 0;
    for (int i = 0; i < g->nobj; i++, g->obj_index++) {
        const MapObj *o = &g->obj[i];
        if ((o->col >> 8) == 0xFF || o->col != (uint16_t)map_col) continue;
        g->ring[game_ring_index(g, o->row, ring_col)] = (uint8_t)(0x80 | i);
    }
}
void game_ring_refill(Game *g) { ring_fill(g); }
static void scroll_left(Game *g)                                                                        /* 66F8: world right, hero left */
{
    if (--g->scroll_col < 0) g->scroll_col = g->map->width - 1;
    memmove(g->ring + 1, g->ring, RING_SIZE - 1);
    ring_put_column(g, 0, g->scroll_col);
    shots_shift(g, 1);                                                  /* 864E */
    remark_column(g, g->scroll_col, 0);                                 /* 676D */
    memcpy(g->bg_ring, g->ring, RING_SIZE);
}
static void scroll_right(Game *g)                                                                       /* 68A0 */
{
    /* 68A0 bumps [80] *first* (`inc word [0x80]`) and only then fetches the
     * column it writes into ring column 0x23 (`mov ax,[0x80]; add ax,0x23`),
     * so the new column is scroll_col+0x23 with scroll_col already advanced.
     * Doing it the other way round leaves the whole ring one map column to
     * the left of what [80] says, and because every later scroll shifts that
     * column inwards the error spreads across the ring: the hero's collisions
     * then read the cell west of the one he is standing in. */
    if (++g->scroll_col == g->map->width) g->scroll_col = 0;
    memmove(g->ring, g->ring + 1, RING_SIZE - 1);
    int c = g->scroll_col + 0x23; if (c >= g->map->width) c -= g->map->width;
    ring_put_column(g, 0x23, c);
    shots_shift(g, 0);                                                  /* 8639 */
    remark_column(g, c, 0x23);                                          /* 68FF */
    memcpy(g->bg_ring, g->ring, RING_SIZE);
}
/* 6621 `dec byte [0x82]` / 6B2E `inc byte [0x82]`: the byte is never masked --
 * every reader takes `& 0x3F` -- so it runs on past 63 (seen as 0x40 by the
 * oracle after a fall through the ring's bottom row) */
static void scroll_up(Game *g)   { g->scroll_row = (uint8_t)(g->scroll_row - 1); }                      /* 6621 */
static void scroll_down(Game *g) { g->scroll_row = (uint8_t)(g->scroll_row + 1); }                      /* 6B2E */

/* ---------------------------------------------------------- forward decls */
static void frame(Game *g);
static void frame_tail(Game *g);
static void frame_7094(Game *g);
static void death_begin(Game *g);
static void walk_left(Game *g);
static void walk_right(Game *g);
static int  try_move_left(Game *g);
static int  try_move_right(Game *g);
static int  floor_under_hero(const Game *g);
static int  elevator_up(Game *g);
static int  elevator_down(Game *g);
static int  fixture_ride(Game *g);
static void message_tick(Game *g);
static void walk_in_step(Game *g);

static void stop_rising(Game *g) { g->conveyor = 0; g->vstate = V_FALL; }                              /* 65BA */
static void turn_around(Game *g) { g->hero_flags ^= FACE_LEFT; if (!g->on_ladder) g->hero_anim = 0x80; }   /* 6824 */
static void stop_walking(Game *g) { g->hero_flags &= (uint8_t)~WALKING; if (!g->on_ladder && g->vstate == V_GROUND) g->hero_anim = 0x80; }  /* 6837 */

/* 67A3 / 6942: a current cell is a one-way wall against the current --
 * everywhere *but* cavern 7: `cmp byte [C012],7 / clc / jnz` runs the 76F6
 * test only when the cavern is not 7, and returns "not blocked" (CF clear) in
 * cavern 7 (#87: MP72's floor gaps, where the port refused the step) */
static int current_blocks_left(const Game *g, uint8_t v)  { return g->map->cavern != 7 && special_tile(g, v) == 2; }
static int current_blocks_right(const Game *g, uint8_t v) { return g->map->cavern != 7 && special_tile(g, v) == 1; }

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
            if (g->hero_dead) return;                                   /* 718C jumped to 98FC */
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
    /* 7E15 writes the byte into the map image at C00A; the parsed Door is a
     * copy, so mirror it into raw (a 6BFC re-parse or a save-state reload that
     * stores raw would otherwise re-lock the door, #110). */
    if (g->map) map_door_letter_set((Map *)(void *)g->map, (int)(d - g->map->doors), d->letter);
    if (d->flag_ptr != 0xFFFF) g->page[d->flag_ptr & 0xFF] |= d->flag_mask;   /* 7E39 */
    /* no message box: 7E26..7E3B is the whole of it (the "You get a Key."
     * the port printed here is the pickup's, 90D3) -- key_door f43 */
    return 1;
}

/* 0x7E5B  the hero's state reset (door 7B3E, town entry 7A01) */
static void hero_state_reset(Game *g)
{
    g->attacking = 0; g->casting = 0; g->vstate = V_GROUND;             /* 7E5D..7E66 [FF43] [FF3C] [FF3D] */
    g->crouching = 0; g->hero_hit_flash = 0; g->msg_box = 0;            /* 7E69..7E6F [FF38] [FF36] [9EEF] */
    g->magic_active = 0; g->menu_result = 0; g->hero_anim = 0;          /* 7E72..7E7B [FF3E] [FF4B] [E7] */
    g->boss_state = 0xFF; g->hero_entering = 0xFF; g->menu_debounce = 0xFF;   /* 7E84 / 7E8A / 7E8D */
}

/* 0x79DC  fight.bin entry 2 ([6002]), the way in from town.bin 6FF8: before
 * the map is loaded it zeroes fight.bin's locals 9EED..9F2C (79E7..79F4, 0x40
 * bytes), sets 9EF5/9EFE/9EFF to FF, runs 7E5B, frees the four orb slots and
 * clears [FF3A].  The locals live in the overlay image that is parked, not
 * reloaded, across the town visit, so without this the port carried the last
 * cavern's crouch_release [9F0A], prev_facing [9F24] and regen_tick [9F18]
 * back in (gate_walking f99).  The door entry (6042) does none of it. */
void game_town_entry_reset(Game *g)
{
    g->msg_timer = 0; g->msg_box = 0; g->hero_home_row = 0; g->boss_knock_left = 0;      /* 9EED..9F01 */
    g->fixture_anim = 0; g->fall_rows = 0; g->rise_rows = 0; g->crouch_release = 0;      /* 9F07..9F0A */
    g->gf_frame = 0;            /* gfmcga is loaded afresh over town.bin's driver: its [4FFF] image byte is 0 */
    g->diag_jump = 0; g->conveyor_kick = 0; g->max_rise = 0;                             /* 9F0B..9F0D */
    memset(g->hit_side, 0, sizeof g->hit_side); g->contact_damage = 0; g->hero_hit = 0;  /* 9F0E..9F14 */
    g->on_updraft = 0; g->conveyor_phase = 0; g->on_hazard = 0; g->regen_tick = 0;       /* 9F15..9F18 */
    g->door_msg_latch = 0; g->post_boss_pending = 0; g->projectile_count = 0;            /* 9F19..9F1F */
    g->ice_slide = 0; g->ice_steps = 0; g->walk_dir = 0; g->slide_dir = 0;               /* 9F20..9F23 */
    g->prev_facing = 0; g->heat_timer = 0; g->boss_intro = 0;                            /* 9F24..9F26 */
    g->death_anim = 0; g->death_anim_b = 0; g->magic_hit_any = 0; g->cast_timer = 0;     /* 9F28..9F2B */
    g->menu_debounce = 0xFF;                                            /* 79F8 */
    hero_state_reset(g);                                                /* 7A01 */
    for (int i = 0; i < 4; i++) g->orbs[i].phase = 0xFF;                /* 7A04..7A0F */
    g->hero_entering = 0;                                               /* 7A12 [FF3A] */
}

/* 7A83: returns 1 when it consumed the "up" (door entered, message, or side step) */
static int door_check(Game *g)
{
    /* A door one column to the side, facing it: 7AB2/7AA6 `pop ax / jmp 66A5`
     * (`684C`), the bare scroll, not the walk at 663E/67C6 -- no walk_dir, no
     * walk frame, no WALKING bit.  The port called walk_left/right and drew
     * the next walk frame (gate_walking f51: hero_anim 1 -> 2, [9F22] 2). */
    int s = wrap_up(game_hero_cell(g) - 0x25);                          /* row-1, col-1 */
    if (g->ring[s] == DOOR_CELL) { if (g->hero_flags & FACE_LEFT) { try_move_left(g); return 1; } return 0; }    /* 7AAA */
    if (g->ring[wrap_down(s + 1)] != DOOR_CELL) {                       /* 7A92: col before col+1 */
        if (g->ring[wrap_down(s + 2)] != DOOR_CELL) return 0;
        if (!(g->hero_flags & FACE_LEFT)) { try_move_right(g); return 1; }   /* 7A9E */
        return 0;
    }
    int col = game_hero_map_col(g);
    int row = (g->hero_scr_row - 1 + g->scroll_row) & 0x3F;
    Door *d = NULL;
    for (int i = 0; i < g->map->ndoors; i++)
        if (g->map->doors[i].col == col && g->map->doors[i].row == row) { d = &((Map *)g->map)->doors[i]; break; }
    if (!d) return 0;
    if (!(d->letter & 0x80)) {                                          /* 7AF7: locked */
        /* 7E15 returns CF clear after a key is spent (the `or` at 7E39), and
         * 7B02's `ret` leaves jump_up at once (7AF6 popped its return): the
         * 7B03 pose/ice reset is the no-key path's only. */
        if (door_unlock(g, (Door *)d)) return 1;                        /* 7AFD..7B02 */
        g->hero_anim = 0x80; g->ice_steps = 0;
        if (!g->door_msg_latch) {                                       /* 7B0D */
            g->door_msg_latch = 0xFF;
            g->sfx_request = 0x16;
            game_message_idx(g, MSG_DOOR_LOCKED);                      /* 9AC5 */
        }
        return 1;
    }
    if (g->on_door) fprintf(stderr, "[door] (%d,%d) letter %02x -> %s map %02x at (%d,%d) flags %02x%s\n",
            d->col, d->row, d->letter, d->dest_row == 0xFF ? "TOWN" : "cavern", d->dest_map,
            d->dest_col, d->dest_row, d->dflags, d->flag_ptr != 0xFFFF ? " (sets a story flag)" : "");
    if (d->flag_ptr != 0xFFFF) g->page[d->flag_ptr & 0xFF] |= d->flag_mask;   /* 7B25 */
    /* 7B33..7B41: before the next map is loaded the cavern runs one more
     * frame: shots dropped (83DB), the tile cache invalidated (73B3; the port
     * redraws everything anyway), the hero's state reset (7E5B), then `call
     * 708F` -- the frame tail with its draw and a 717F boundary.  The oracle
     * shows it: Up at the town gate is still a cavern frame, the town's first
     * 68ED is the next one (jumps f44). */
    shots_clear(g);                                                     /* 83DB */
    hero_state_reset(g);                                                /* 7E5B */
    frame_tail(g);                                                      /* 7B41 */
    if (g->hero_dead) return 1;                                         /* 718C jumped to 98FC */
    g->page[0xC3] = (uint8_t)(d->letter & 0x40);                        /* 7B5F..7B64 */
    g->gf_frame = (uint8_t)(d->letter & 7);     /* 7B4D push (letter & 7) -> 7BBF/7BF5 gfmcga [301E] 4614: [4FFF] = AL */
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
            if (g->hero_dead) return;                                   /* 718C jumped to 98FC */
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
static int floor_under_hero(const Game *g)
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

/* 695A's own "is there anything under his feet" test, for the navigator: the
 * frame a walk carries the hero off the end of something he is still V_GROUND,
 * because game_step runs `gravity` before `hero_input`. */
int game_hero_has_floor(const Game *g) { return floor_under_hero(g); }

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
 * 76C2's two `pop ax` drop 76B8's return address and the loop's `push cx`
 * (76AA), so the handler's `ret` goes back to 6FAC: the frame carries on and
 * renders.  It is NOT aborted (#60: treating it as aborted spun forever,
 * unrendered and uncounted, with the hero pushed against a wall).
 * Updraft 76D4 = 6621 twice, currents 76F0/76EA = 66A5/684C twice. */
static void special_tiles_check(Game *g)
{
    g->on_updraft = 0;
    int s = wrap_down(game_hero_cell(g) + 0x49);
    for (int n = 3; n; n--, s = wrap_up(s - RING_W)) {
        int k = special_tile(g, g->ring[s]);
        if (k < 0) continue;
        switch (k) {
        case 0: scroll_up(g); scroll_up(g); g->on_updraft = 0xFF; g->vstate = V_GROUND; g->hero_anim = 0x80; return;
        case 1: try_move_left(g);  try_move_left(g);  return;
        default: try_move_right(g); try_move_right(g); return;
        }
    }
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
static void fixture_draw_one(Game *g, Fixture *f)
{
    int rc = fixture_rcol(g, f->col);
    if (rc < 0) return;
    for (int k = 0; k < 3 && rc + k < RING_W; k++)
        fixture_put(g, game_ring_index(g, f->row, (uint8_t)(rc + k)), (uint8_t)(f->cell + k));
    f->drawn = 1; f->drawn_col = f->col; f->drawn_row = f->row;
}

/* 0x82B4  is the hero standing on this fixture?  Grounded and off the ladder
 * (82B4: [FF3D] | [FF39]), its row is the row under his feet (82C0), it is on
 * screen (82DC -> 82F8), and one of its three cells is the cell under his body
 * column.  That last test is written the long way round: 82E2 puts his own ring
 * column [83]+4 in DL and 82EC compares DL, DL+1 and DL+2 against
 * AL = 0x21 - (the platform's ring column), which 82F8 leaves behind.  With the
 * camera holding [83] at 0x0C -- 6FF9 does that on every frame outside a boss
 * room, and no boss room carries a C fixture -- DL is 0x10 and the three
 * matches are platform ring columns 0x11, 0x10 and 0x0F, i.e. exactly the three
 * positions in which one of its cells is ring column [83]+5, the cell 6D6E+0x6D
 * tests for ground under his body.  The port had `rc <= hc <= rc+2` here, one
 * column to the left of that, so a platform moving right stopped carrying him a
 * column *before* it stopped holding him up and walked him off its leading
 * end -- which is what lost the ride on MP10's fix[5] every time. */
static int hero_on_fixture(const Game *g, const Fixture *f)
{
    if (g->vstate || g->on_ladder) return 0;
    if ((uint8_t)((g->hero_scr_row + g->scroll_row + 3) & 0x3F) != (f->row & 0x3F)) return 0;
    int rc = fixture_rcol(g, f->col);
    if (rc < 0) return 0;
    int hc = g->hero_scr_col + 4;                                       /* 82E2 */
    for (int k = 0; k < 3; k++) if (hc + k == 0x21 - rc) return 1;      /* 82EC */
    return 0;
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
    /* [9F07] fixture_anim is not reset with the map: only 79E7 (the way in
     * from town, game_town_entry_reset) clears it, so a door carries the
     * count over (cave_door f61: 57 in MP1D) */
}

/* 0x81BE..0x8208  erase a C platform where it stands, before it moves.  831F
 * takes its *rightmost* cell's ring column d = (col+2 - [80]) mod width, off
 * the ring when d > 0x25; so unlike the draw (82F8, fully on the ring) it also
 * clears a platform hanging over either edge: d 0/1 -> d+1 cells from ring
 * column 0 (81CE), leftmost 0x22/0x23 -> the 2/1 cells up to 0x23 (81DA).
 * It writes 0 (8202 `xor dl,dl` -> 8352, under a sprite marker too), not the
 * map cell.  The port used to erase only fully visible platforms, so one that
 * scrolled half off the left edge left its cells in the ring (crouch f55). */
static void fixture_c_erase(Game *g, Fixture *f)
{
    int w = g->map->width;
    int r = (int)f->col + 2; if (r >= w) r -= w;                        /* 831F..832B */
    int d = r - g->scroll_col; if (d < 0) d += w;                       /* 832D..834B */
    if (d > 0x25) return;
    int left = d - 2, c0 = left, n = 3;
    if (left < 0) { c0 = 0; n = d + 1; }                                /* 81CA..81D8 */
    else if (left >= 0x22) n = 2 - (left - 0x22);                       /* 81DA..81F4 */
    for (int k = 0; k < n; k++)
        fixture_put(g, game_ring_index(g, f->row, (uint8_t)(c0 + k)), 0);
    f->drawn = 0;
}

/* 7016 0x81AE (list [C008], the C platforms: erase, move, draw, one record at
 * a time), then 7019 0x7FB1 ([C004], A elevators) and 701C 0x8163 ([C006], B
 * gates): those two are only *drawn* here (82F8 visible -> cells 40/43..),
 * never erased -- a move (8024/80AF) erases its own old row. */
static void fixtures_draw(Game *g)
{
    g->fixture_anim++;                                                  /* 81AE */
    for (int i = 0; i < g->nfix; i++) {
        Fixture *f = &g->fix[i];
        if (f->kind != 2) continue;
        fixture_c_erase(g, f);
        fixture_c_move(g, f);                                           /* 8220 */
        fixture_draw_one(g, f);                                         /* 8224 */
    }
    for (int kind = 0; kind < 2; kind++)
        for (int i = 0; i < g->nfix; i++)
            if (g->fix[i].kind == kind) fixture_draw_one(g, &g->fix[i]);
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

/* 0x8024 (down) / 0x80AF (up)  move a platform one row if the three cells it
 * would occupy are empty and nothing is standing in the way.  Returns 1 when it
 * moved.  The two directions do *not* test the same thing, and the port ran the
 * down test both ways:
 *   down (8024) `add si,0x23 / test [si],0x80` -- the single cell one row below
 *     and one column left must be free of sprites -- then `mov cx,3 / inc si /
 *     test [si],0xff` over the three cells one row below;
 *   up (80A9..80C8) `sub si,0x24` twice, then three times `test [si],0x80` on
 *     the row *two* above and `test [bx],0xff` on the row one above.
 * So a rising platform ignored a sprite over its head and stalled on one beside
 * its feet. */
static int fixture_shift_row(Game *g, Fixture *f, int dr, int *block)
{
    int rc = fixture_rcol(g, f->col);
    if (rc < 0) return 0;
    uint8_t nrow = (uint8_t)((f->row + dr) & 0x3F);
    int probe = game_ring_index(g, nrow, (uint8_t)rc);
    if (block) *block = -1;
    if (dr > 0) {                                                       /* 8024 */
        int p = game_ring_add(probe, -1);
        if (g->ring[p] & 0x80) { if (block) *block = p; return 0; }     /* 802B..8037 */
        for (int k = 0; k < 3; k++) {
            p = game_ring_add(probe, k);
            if (g->ring[p] != 0) { if (block) *block = p; return 0; }   /* 803C..8041: SI stays on it */
        }
    } else {                                                            /* 80AF */
        int above = game_ring_index(g, (uint8_t)((f->row - 2) & 0x3F), (uint8_t)rc);
        for (int k = 0; k < 3; k++) {
            if (g->ring[game_ring_add(above, k)] & 0x80) return 0;      /* 80BA */
            if (g->ring[game_ring_add(probe, k)] != 0) return 0;        /* 80C0 */
        }
    }
    /* 804C..8068 / 80D4..80F0: per cell, the new row gets the fixture cell
     * and the old row a **0** (`xor dl,dl` -> 8352, which writes under a
     * sprite marker instead), not the map's cell as the port had it.  The
     * maps hold 0 under every A/B fixture's start cells (all 31 checked) and
     * 8024/80AF only move into empty rows, so the two agreed in practice;
     * the elevator scenario covers it. */
    int old = game_ring_index(g, f->row, (uint8_t)rc);
    for (int k = 0; k < 3 && rc + k < RING_W; k++) {
        fixture_put(g, game_ring_add(probe, k), (uint8_t)(f->cell + k));   /* 8054 */
        fixture_put(g, game_ring_add(old, k), 0);                          /* 805D */
    }
    f->row = nrow;
    f->drawn = 1; f->drawn_col = f->col; f->drawn_row = f->row;
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
    int block;
    if (!fixture_shift_row(g, f, 1, &block)) {
        /* 8009..8023: what stopped it, if a live enemy (6DCB: a sprite whose
         * type & 0x60 == 0) not already stunned, takes a hit with source 0
         * (+5 = (+5 | 0x40) & 0xE0: pending, the stomp's damage 9851).  An
         * elevator pressed down onto a frog kills it (elevator f106). */
        if (block >= 0 && (g->ring[block] & 0x80)) {
            int oi = g->ring[block] & 0x7F;
            if (oi < g->nobj) {
                MapObj *o = &g->obj[oi];
                if (!(o->type & 0x60) && !(o->hit & 0x20))
                    o->hit = (uint8_t)((o->hit | 0x40) & 0xE0);
            }
        }
        return 0;
    }
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
    if (!fixture_shift_row(g, f, -1, NULL)) return 0;
    g->hero_anim = 0x80;
    g->vstate = V_GROUND;                                               /* 80FC..8101: [E7] = 80, [FF3D] = 0 */
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
    if (!fixture_shift_row(g, f, 1, NULL)) return 0;                    /* 81A8: no 8009 here */
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
    special_tiles_check(g);                                             /* 7699 (returns to 6FAC) */
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
    /* gfmcga 3E34 at 7047 saves the blade's 32x32 block (3F42) relative to
     * [4FED], which still holds where the hero was drawn *last* frame (70D4
     * has not run yet); render.c restores it over the death sparks (7136). */
    g->sword_blk_ok = g->hero_drawn_ok;
    g->sword_blk_row = g->hero_drawn_row; g->sword_blk_col = g->hero_drawn_col;
    sword_advance(g);                                                   /* 7047: [3006], first swing step */
    hazard_check(g);
    if (g->map->cavern == 7 && g->shoes != 5 && ((++g->heat_timer & 0x3F) == 0)) {   /* 704F */
        g->hero_hit_flash = 0xFF; g->sfx_request = 9; hero_damage(g, 0x0F);
        game_message_idx(g, MSG_TOO_HOT);                              /* 9BB9 */
    }
    message_tick(g);                                                    /* 7210 */
    if (g->hero_dead) { g->hero_hit_flash = 0; frame_7094(g); }          /* 7081..708D */
    else frame_tail(g);                                                 /* 708F */
}

/* 0x7094..0x7202  the second half of 6F9B: draw, wait, the 717F boundary and
 * the per-frame bookkeeping after it. */
static void frame_7094(Game *g)
{
    g->frame_no++;
    if (!g->harness_rng) g->rng += 20;                                  /* FF1B: ticks per frame at speed 5 */
    sound_request(g);                                                   /* FF75 -> the sound stub */
    /* gfmcga 302C draws each background cell at 3159 from the ring *before*
     * 315C animates that cell, so the frame's picture holds the pre-animation
     * cells while the ring holds the post-animation ones.  render.c draws the
     * background from bg_ring, so snapshot the ring here -- after 6F9B's
     * simulation (scroll/doors/fixtures/pickups have all written the ring) and
     * before this frame's cell_anim below -- making bg_ring the ring with only
     * the 315C step undone.  The snapshot precedes the draw, so the normal
     * 70D4 presentation and the 717F trace PNG both see the same cells 302C's
     * 3159 did. */
    memcpy(g->bg_ring, g->ring, RING_SIZE);
    if (g->present) g->present(g);                                      /* draw + wait 4*speed ticks + poll input */
    g->hero_drawn_ok = 1;                                               /* 70D4 drew the hero: [4FED] */
    g->hero_drawn_row = g->hero_scr_row; g->hero_drawn_col = g->hero_scr_col;
    /* 70D9: the potion's +8 LIFE a frame, right after the 70D4 draw and before
     * the 717F boundary (it used to run after it, one frame late: #63) */
    if (!g->hero_dead && g->hp_regen_pending) {
        g->hp_regen_pending--; g->hp += 8;                              /* 70E7 / 70EB */
        if (g->hp > g->max_hp) { g->hp = g->max_hp; g->hp_regen_pending = 0; }   /* 70F3..70FF */
        g->sfx_request = 0x13;                                          /* 7105 */
    }
    cell_anim(g);                                                       /* 710F [3000]: gfmcga 302C's 315C hook */
    boss_sparks(g);                                                     /* 710F [3000] / 7114 [3010] */
    orbs_update(g);                                                     /* 7133: the second orb pass */
    shots_draw_pass(g);                                                 /* 713B: 8366 */
    sword_apply(g);                                                     /* 7144: 6F07, second half of the frame */
    sword_advance(g);                                                   /* 7147: [3006], second swing step */
    sound_request(g);
    if (g->frame_end) g->frame_end(g);                                  /* 717F: the harness frame boundary */
    if (g->hero_dead) return;
    if (g->hp == 0) { death_begin(g); return; }                         /* 718C: jmp 98FC */
    if (++g->regen_tick >= 0x10) {                                      /* 719E */
        g->regen_tick = 0;
        if (g->hp < g->max_hp) g->hp += 2;     /* 71B1 cmp/jnc, 71B7 add ax,2: no clamp, max_hp-1 -> max_hp+1 */
    }
    if (g->post_boss_pending) { post_boss_transition(g); return; }       /* 71C2 -> 72F1 */
    boss_rewards(g);                                                     /* 71CC */
    check_status_menu(g);                                                /* 7202 */
}


/* 0x708F  the frame from its draw on: entered from 7081 and, on its own,
 * by the door path (7B41 `call 708F`). */
static void frame_tail(Game *g)
{
    g->hero_hidden = 0;                                                 /* 708F */
    frame_7094(g);
}

/* 0x7202  The menu key opens select.bin (status.c).  fight.bin swaps the
 * overlay in from arena:C000, calls [A000] and, when the screen leaves 8 in
 * menu_result [FF4B] (the Kioku Feather), jumps to 99E0 — the same "back to
 * town" tail as the death path but *without* the gold/almas penalty. */
static void check_status_menu(Game *g)
{
    if (!g->menu_key) { g->menu_debounce = 0; return; }                  /* 7202 */
    if (g->menu_debounce || g->casting || g->magic_active || g->boss_intro) return;  /* 7275 */
    g->sfx_request = 0x0B;                                               /* 728C */
    g->menu_result = (uint8_t)status_run_fight(g);
    if (g->menu_result == 8) {                                           /* 729C -> 99E0 */
        g->cur_map = g->town_map ? g->town_map : 0x81;
        if (g->on_town && g->on_town(g, g->cur_map & 0x7F, -1, 0)) return;
    }
    g->menu_debounce = 0xFF;                                             /* 72BA */
    g->btn1_edge = g->btn2_edge = 0;                                     /* 72C2/72C7 */
    g->msg_box = 0; g->msg_timer = 0;                                    /* 72CC/72D1 */
    frame(g);                                                            /* 72D6: jmp 6F9B, a whole frame more */
}

/* 62DB: main-loop variant while on a ladder */
static void ladder_step(Game *g)
{
    g->crouching = 0; g->vstate = V_GROUND; g->conveyor = 0; g->attacking = 0;
    frame(g);
    if (g->hero_dead) return;                                           /* 718C jumped to 98FC */
    if (g->loop_restart) { g->loop_restart = 0; return; }               /* 73B0 jmp 6042: 629C starts over */
    hero_knockback(g);
    hero_input(g);
    if (g->on_ladder == 0xFF) {
        int tl = game_hero_cell(g);
        if (is_ladder(g->ring[wrap_down(tl + 1)])) return;
        if (is_ladder(g->ring[wrap_down(tl + 1 + RING_W)])) return;
    }
    g->hero_flags &= (uint8_t)~WALKING; g->on_ladder = 0;               /* 631D / 6322 */
    g->btn1_edge = g->btn2_edge = 0;    /* 6327/632C: a Space pressed on the ladder does not slash on leaving it */
    g->ice_slide = g->ice_steps = 0; g->hero_anim = 0x7F;               /* 6331 / 6336 / 633B */
}

/* 60E6: the six flashes of the ENCNT.GRP encounter card, 0x41 ticks apart.
 * No simulation runs while it is up; the renderer draws the card on the odd
 * halves (VID_TEXTBOX_FILL(BX=0x0C28, CX=0x3828) clears it on the even ones). */
static void encounter_step(Game *g)
{
    g->encounter_frames--;
    g->frame_no++;
    if (!g->harness_rng) g->rng += 20;
    if (g->present) g->present(g);
}

/* 0x98FC  LIFE reached 0 at 718C (the frame's own 717F boundary has already
 * been taken).  The rest of that main-loop pass never runs: 718C *jumps* here.
 * Only the setup; the dying frames are death_step(). */
static void death_begin(Game *g)
{
    g->attacking = 0;                                                   /* 9901 [FF43] */
    g->vstate = V_GROUND;                                               /* 9906 [FF3D] */
    g->crouching = 0;                                                   /* 990B [FF38] */
    g->hero_hit_flash = 0;                                              /* 9910 [FF36] */
    g->hero_dead = 0xFF;                                                /* 9915 [E8] */
    g->death_anim = 0; g->death_anim_b = 0;                             /* 991A / 991F */
    g->death_phase = 1;                                                 /* 9929 */
}

/* 0x9929..0x99A1  One dying frame per game_step: each pass runs 6F9B (one 717F
 * boundary; with [E8] set it returns right after it, 7184) and the code up to
 * the next 6F9B call.  The original's frame count, measured with the oracle:
 * 1 (9938) + falling rows + 32 (9948: 16 to raise the pose [E7] 0 -> 2 every
 * 8 frames, then 16 blinking) + 30 (9993) = 63 on flat ground; then 99A3
 * (the penalties and the town, hero_die). */
static void death_step(Game *g)
{
    switch (g->death_phase) {
    case 1:                                                             /* 9929 */
        g->hero_anim = 0; g->on_ladder = 0; g->hero_hidden = 0;
        frame(g);                                                       /* 9938 */
        /* 993B: `push 9929; call 695A`: 695A's falling and landing exits
         * `pop ax` their own return address (698D, 6B49) and so return to
         * 9929 -- the body falls one row a frame and lands -- the others
         * (on the floor, rising) return to 9942. */
        if (!gravity(g)) return;
        g->hero_hidden = 0;                                             /* 9943 */
        g->death_phase = 2;
        return;
    case 2:                                                             /* 9948 */
        frame(g);
        g->hero_hidden = 0;                                             /* 994B */
        if (g->hero_anim == 2) {                                        /* 9950 */
            g->death_anim_b++;                                          /* 9972 */
            if (g->death_anim_b & 0x0F) {
                if (g->death_anim_b & 1) g->hero_hidden = 0xFF;         /* 997D / 9984 */
                return;
            }
            audio_music_fade(8);                                        /* 998B: [FF24] = 8 */
            g->death_blink = 30;                                        /* 9990: mov cx,1E */
            g->death_phase = 3;
            return;
        }
        if (++g->death_anim & 7) return;                                /* 9957 */
        {
            uint8_t a = (uint8_t)((g->hero_anim + 1) & 3);              /* 9962 */
            if (a != 3) g->hero_anim = a;                               /* 9969: 3 is never stored */
        }
        return;
    case 3:                                                             /* 9993 */
        frame(g);
        g->hero_hidden = (uint8_t)((g->death_blink & 1) - 1);           /* 9998: odd CX shows, even hides */
        if (--g->death_blink) return;                                   /* 99A1: loop */
        g->death_phase = 0;
        hero_die(g);                                                    /* 99A3 */
        return;
    }
}

void game_step(Game *g)
{
    if (g->step_begin) g->step_begin(g);
    if (g->encounter_frames) { encounter_step(g); return; }             /* 60E6 */
    if (g->hero_dead) { death_step(g); return; }                        /* 98FC's own loops */
    if (g->walk_in) { walk_in_step(g); return; }                        /* 7C6E */
    if (g->on_ladder) { ladder_step(g); return; }
    sword_input(g);                                                     /* 6E3B */
    ice_slide_step(g);
    frame(g);
    if (g->hero_dead) return;                                           /* 718C jumped to 98FC */
    if (g->loop_restart) { g->loop_restart = 0; return; }               /* 73B0 jmp 6042: 629C starts over */
    magic_input(g);                                                     /* 87B0 */
    if (g->magic_extra_frame) {                                         /* 8963..896B: `jmp 6F9B`, its ret returns to the 87B0 caller */
        g->magic_extra_frame = 0;
        frame(g);
        if (g->hero_dead) return;                                       /* 718C jumped to 98FC */
        if (g->loop_restart) { g->loop_restart = 0; return; }
    }
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
    /* fight.bin's locals start as the overlay image holds them (it is loaded
     * from disk at boot and after a restore, then parked -- not reloaded --
     * across town visits): 9EF5 menu_debounce is 0xFF there, so the first
     * cavern frame's 7202 ignores an Enter held across the entry */
    g->menu_debounce = 0xFF;
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
    /* [C2]: only bit 0 is the entry's; the rest is whatever the town (or the
     * map the hero left) had -- neither town.bin 6FF8 nor fight.bin 7DC1/7C6E
     * writes anything but bit 0 (7C75 `and [c2],FE` / 7CB4 `or [c2],1`).  The
     * port cleared bit 1 (WALKING) here, so a hero who left the cavern walking
     * came back in standing (#69, gate_walking). */
    g->hero_flags = (uint8_t)((g->hero_flags & ~FACE_LEFT) | (face_left ? FACE_LEFT : 0));
    g->hero_anim = 0x80; g->vstate = V_GROUND; g->on_ladder = 0; g->crouching = 0;
    g->fall_rows = g->rise_rows = 0; g->diag_jump = 0;
    g->attacking = 0; g->attack_var = 0; g->hero_dead = 0; g->death_anim = 0; g->death_anim_b = 0; g->death_phase = 0;
    g->casting = g->magic_active = 0;     /* [9F2B] cast_timer survives a door: only 87E1 (a new cast) and 79E7 (the town entry) clear it */
    shots_clear(g);                                                     /* 83DB */
    for (int i = 0; i < 4; i++) { g->magic[i].live = 0; g->orbs[i].phase = 0xFF; }
    g->entry_col = col; g->entry_row = row; g->entry_face = (uint8_t)(face_left ? 1 : 0);
    ring_fill(g);
    fixtures_load(g);
    c000_load(g);
    enemies_load(g);
}

/* The object records of the map being left are part of its image at
 * [C010] (fight.bin works on them in place); the kernel then loads the new
 * map raw to C000 over it (ARCHITECTURE.md "Maps").  Keep that memory so the
 * bytes past a short map's end -- which a boss overlay's part records reach,
 * see Game.c000 -- are the ones the original has there. */
void c000_load(Game *g)
{
    unsigned o = g->c000_objs;
    for (int i = 0; i < g->nobj && o + 16 <= sizeof g->c000; i++, o += 16) {
        const MapObj *e = &g->obj[i];
        uint8_t *p = g->c000 + o;
        p[0] = (uint8_t)e->col; p[1] = (uint8_t)(e->col >> 8); p[2] = e->row; p[3] = e->rcol;
        p[4] = e->type; p[5] = e->hit; p[6] = e->phase; p[7] = e->flags; p[8] = e->hp;
        p[9] = e->next; p[10] = e->link; p[11] = (uint8_t)e->home_col; p[12] = (uint8_t)(e->home_col >> 8);
        p[13] = e->home_row; p[14] = e->home_type; p[15] = e->timer;
    }
    if (g->nobj && o + 2 <= sizeof g->c000) g->c000[o] = g->c000[o + 1] = 0xFF;
    const Map *m = g->map;
    if (!m || !m->raw) return;
    size_t n = m->rawlen < sizeof g->c000 ? m->rawlen : sizeof g->c000;
    memcpy(g->c000, m->raw, n);
    unsigned t = n >= 0x12 ? (unsigned)(m->raw[0x10] | m->raw[0x11] << 8) : 0;
    g->c000_objs = (uint16_t)(t >= 0xC000 && t < 0xE000 ? t - 0xC000 : 0);
}

void game_enter(Game *g, const Map *m, const Tileset *t, int dest_col, int dest_row, int face_left)
{
    g->map = m; g->tiles = t;
    game_place(g, dest_col, dest_row + 1, face_left);                    /* 7DC1: scroll_row = dest_row+1-row_bias */
}

/* 61A8: a level whose record has bit6 ([E6]) is a "boss room" the hero walks
 * into from the left under the boss overlay's own control.  The main loop puts
 * the camera at column 0x29 with the hero five columns in and refills the ring
 * before it hands the frame to the boss AI, which clears [E6] when the intro
 * is over (61BE).  MP90 (system map 0x1D, Alguien) is the only one. */
void game_boss_room_intro(Game *g)
{
    g->boss_intro = 0xFF;
    g->scroll_col = 0x29;
    g->hero_scr_col = 5;
    g->walk_in = 0; g->hero_entering = 0;
    ring_fill(g);
}

/* ------------------------------------------------------------- messages */
/* fight.bin's message table at 9A1E: {u16 x, chars, 0xFF} records.  The strings
 * and their x offsets (the original's record word; 73E0 draws the text at
 * x + 0x3A), and the 32-frame lifetime of msg_box_a (7210: the box is erased
 * when the low 5 bits of 9EED wrap).  The strings carry a plain apostrophe
 * where the original stores 0x5C: font.grp's 0x5C glyph is identical to 0x27. */
static const struct { uint16_t x; const char *s; } MESSAGES[MSG_COUNT] = {
    { 38, "You get 50 golds." },            /* 9A1E */
    { 34, "You get 100 golds." },           /* 9A32 */
    { 34, "You get 500 golds." },           /* 9A47 */
    { 30, "You get 1000 golds." },          /* 9A5C */
    { 50, "You get a Key." },               /* 9A72 */
    { 28, "You have recovered." },          /* 9A83 */
    {  8, "You have recovered full." },     /* 9A99 */
    { 60, "Shield broken." },               /* 9AB4 */
    { 20, "Can't open this door." },        /* 9AC5 */
    { 28, "Nothing in the box." },          /* 9ADD */
    {  6, "You get the Hero's Crest." },    /* 9AF3 */
    {  0, "You get the Ruzeria shoes." },   /* 9B0F */
    {  8, "You get the Glory Crest." },     /* 9B2C */
    {  6, "You get the Pirika shoes." },    /* 9B47 */
    {  6, "You get the Feruza shoes." },    /* 9B63 */
    {  0, "You get the Silkarn shoes." },   /* 9B7F */
    {  0, "Get the Enchantment sword." },   /* 9B9C */
    { 48, "It's too hot !!" },              /* 9BB9 */
    {  8, "Get the lion's head Key." },     /* 9BCB */
};
const char *fight_message(int idx) { return (idx >= 0 && idx < MSG_COUNT) ? MESSAGES[idx].s : ""; }
int fight_message_x(int idx) { return (idx >= 0 && idx < MSG_COUNT) ? MESSAGES[idx].x + 0x3A : 0x3A; }

/* 0x73E0: draw the 208x16 box at (56,30) (video [2000] BX=0x0E1E CX=0x3410
 * AL=0xFF) and print the record's string at (x, 0x22) ([202A] BX=x CL=0x22);
 * then arm the 32-frame close timer.  The box itself is painted every frame by
 * render.c while msg_box is up (the original's 7210 writes 0xFC into the E900
 * screen copy so gfmcga 312E skips redrawing those cells). */
void game_message(Game *g, const char *text, int x)
{
    snprintf(g->message, sizeof g->message, "%s", text);
    g->msg_x = (uint16_t)x;
    g->msg_timer = 0; g->msg_box = 0xFF;
}
void game_message_idx(Game *g, int idx) { game_message(g, fight_message(idx), fight_message_x(idx)); }
/* 0x7210  the box is erased 32 frames later. */
static void message_tick(Game *g)
{
    if (!g->msg_box) return;
    g->msg_timer = (uint8_t)((g->msg_timer + 1) & 0x1F);
    if (!g->msg_timer) { g->msg_box = 0; g->message[0] = 0; }
}

void game_message_tick(Game *g) { message_tick(g); }                 /* 7210 from 738F (post-boss transition) */

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
    if (!g->harness_rng) g->rng += 20;
    if (g->present) g->present(g);
    g->walk_in_x += g->walk_in_dir;
    if (--g->walk_in <= 0) {
        g->walk_in = 0; g->hero_entering = 0;
        g->hero_anim = 0x80;                                            /* 7D36 */
        g->hero_scr_col = 0x0C;                                         /* 7D28 */
        g->hero_scr_row = g->map->row_bias; g->hero_home_row = g->map->row_bias;
    }
}
