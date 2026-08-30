/* ai.c — the fight.bin services the AI overlays call through the vector table
 * at BASE:6000 (docs/FIGHT.md §7, src/ai/ai_common.h §3).  Every routine keeps
 * the original's *pointer* arithmetic over the 36x64 ring, so the quirks the
 * AI relies on (column -1 landing in the previous row, the ring-edge refusals)
 * survive.  "blocked" = the 8086 CF = 1 = we return 1. */
#include "enemy.h"
#include <string.h>

/* ring deltas from the sprite's top-left cell (row*0x24 + col) */
#define D(dr, dc) ((dr) * RING_W + (dc))

static inline uint8_t rcell(const Game *g, int p, int d) { return g->ring[game_ring_add(p, d)]; }

/* 0x94E1  vec 23.  cell < 0x49: passable only if it is in the tileset's
 * 24-entry list; 0x49..0x7F (DCHR items/doors): passable; >= 0x80 (a sprite
 * marker): never.  docs/ENEMIES.md §4 corrects FIGHT.md, which had this
 * reversed. */
int ai_cell_passable(const Game *g, uint8_t cell)
{
    if (cell & 0x80) return 0;
    if (cell >= 0x49) return 1;
    for (int i = 0; i < 24; i++) if (g->tiles->lists[i] == cell) return 1;
    return 0;
}

/* 0x97A0  vec 24: the cell under the sprite's left foot (row+2, rcol) */
int ai_on_hazard(const Game *g, const MapObj *o)
{
    uint8_t v = rcell(g, game_ring_index(g, o->row, o->rcol), D(2, 0));
    const uint8_t *q = g->tiles->lists + 0x20;                          /* 73C0 */
    for (int n = 0; n < 4 && q[n]; n++) if (v == q[n]) return 1;
    return 0;
}

/* 0x96A1  vec 27: map column -> ring column; 1 = outside the ring */
int ai_map_col_to_ring(const Game *g, uint16_t col, uint8_t *rcol)
{
    int d = (int)col - g->scroll_col;
    if (d < 0) d += g->map->width;
    if (d >= RING_W) return 1;
    *rcol = (uint8_t)d;
    return 0;
}

/* kernel [11A] KRN_RANDOM: fight.bin only ever uses the low bits.  The original
 * sums the free-running tick counter FF1B; the port keeps a counter that the
 * frame loop advances by the same 4*speed ticks and stirs it here. */
uint16_t krn_random(Game *g)
{
    g->rng = (uint16_t)(g->rng * 25173u + 13849u);
    return g->rng;
}

/* ------------------------------------------------------------------ probes */
/* vec 12..19 (92B4..949A).  "P" cells must be ai-passable, "S" cells only need
 * to be free of sprite markers.  The cell lists are ai_common.h §3. */
static int probe_cells(const Game *g, const MapObj *o, const int *P, int np, const int *S, int ns)
{
    int p = game_ring_index(g, o->row, o->rcol);
    for (int i = 0; i < np; i++) if (!ai_cell_passable(g, rcell(g, p, P[i]))) return 1;
    for (int i = 0; i < ns; i++) if (rcell(g, p, S[i]) & 0x80) return 1;
    return 0;
}

int ai_probe(Game *g, MapObj *o, int dir)
{
    switch (dir) {
    case 0: {   /* 92B4 right */
        static const int P[] = {D(0,2), D(1,2)}, S[] = {D(-1,2)};
        return probe_cells(g, o, P, 2, S, 1); }
    case 4: {   /* 930A left */
        static const int P[] = {D(0,-1), D(1,-1)}, S[] = {D(-1,-2), D(0,-2), D(1,-2)};
        return probe_cells(g, o, P, 2, S, 3); }
    case 2: {   /* 9362 up */
        static const int P[] = {D(-1,0), D(-1,1)}, S[] = {D(-2,-1), D(-2,0), D(-2,1)};
        return probe_cells(g, o, P, 2, S, 3); }
    case 6: {   /* 939A down */
        static const int P[] = {D(2,0), D(2,1)}, S[] = {D(2,-1)};
        return probe_cells(g, o, P, 2, S, 1); }
    case 1: {   /* 93C5 right-up */
        static const int P[] = {D(0,2), D(-1,2), D(-1,1)}, S[] = {D(-2,0), D(-2,1), D(-2,2)};
        return probe_cells(g, o, P, 3, S, 3); }
    case 7: {   /* 940C right-down */
        static const int P[] = {D(1,2), D(2,2), D(2,1)}, S[] = {D(0,2), D(2,0)};
        return probe_cells(g, o, P, 3, S, 2); }
    case 3: {   /* 9452 left-up */
        static const int P[] = {D(0,-1), D(-1,-1), D(-1,0)},
                         S[] = {D(0,-2), D(-1,-2), D(-2,-2), D(-2,-1), D(-2,0)};
        return probe_cells(g, o, P, 3, S, 5); }
    default: {  /* 5: 949A left-down */
        static const int P[] = {D(1,-1), D(2,-1), D(2,0)}, S[] = {D(0,-2), D(1,-2), D(2,-2)};
        return probe_cells(g, o, P, 3, S, 3); }
    }
}

/* ------------------------------------------------------------------- steps */
/* vec 4..11 (91E5..926C): the ring-edge refusal, then the probe, then move.
 * col wraps at the map width (927F/9293), row is masked & 0x3F (92A4/92AC).
 *
 * **Returns 1 when the step was REFUSED, 0 when the sprite moved** -- 925D /
 * 9263 / 9269 all `stc` before their `ret`, while the move at 92A4 ends in an
 * `and`, which clears carry.  So the idiom every ground AI opens with,
 *
 *     call [cs:0x6014]        ; step down
 *     jc   <keep going>       ; blocked = standing on something
 *     ret                     ; it moved = it is falling, do nothing else
 *
 * is `if (!ai_step(g, e, 6)) return;` in C.  ai_eai1.c had it that way and
 * every other overlay had it inverted, so from cavern 2 on **every enemy that
 * stands on the ground returned before doing anything** and the caverns were
 * populated with statues.  The `eai run` assertion in test_combat.c did not
 * catch it: its "something changed" metric was being satisfied by the item
 * animation that item_update used to run for every state (issue #39). */
int ai_step(Game *g, MapObj *o, int dir)
{
    int right = (dir == 0 || dir == 1 || dir == 7);
    int left  = (dir == 3 || dir == 4 || dir == 5);
    if (right && o->rcol >= 0x22) return 1;
    if (left  && o->rcol < 2) return 1;
    if (!right && !left && (o->rcol == 0 || o->rcol == 0x23)) return 1;
    if (ai_probe(g, o, dir)) return 1;
    if (right) { o->col++; if (o->col >= g->map->width) o->col = 0; o->rcol++; }
    if (left)  { o->col = (uint16_t)(o->col ? o->col - 1 : g->map->width - 1); o->rcol--; }
    if (dir == 1 || dir == 2 || dir == 3) o->row = (uint8_t)((o->row - 1) & 0x3F);
    if (dir == 5 || dir == 6 || dir == 7) o->row = (uint8_t)((o->row + 1) & 0x3F);
    return 0;
}

int ai_step_dir(Game *g, MapObj *o, uint8_t dir)  { return ai_step(g, o, dir & 7); }    /* vec 2  9723 */
int ai_probe_dir(Game *g, MapObj *o, uint8_t dir) { return ai_probe(g, o, dir & 7); }   /* vec 3  973F */

/* 0x98C5  vec 31: the first record with home_col == 0xFFFF that is either
 * inactive with row 0x7F or active, inside the ring and not an item. */
int ai_find_spare(Game *g)
{
    for (int i = 0; i < g->nobj; i++) {
        MapObj *o = &g->obj[i];
        if (o->home_col != 0xFFFF) continue;
        if ((o->col >> 8) == 0xFF) { if (o->row == 0x7F) return i; continue; }
        if (o->rcol == 0xFF) continue;
        if (o->type & 0x10) continue;
        return i;
    }
    return -1;
}

/* 0x975B  vec 32: an updraft / current tile under the sprite sweeps it two
 * cells (9788 table).  Returns 1 when it moved (the original pops the caller's
 * return address, so the AI must return immediately). */
int ai_ride_current(Game *g, MapObj *o)
{
    int p = game_ring_index(g, o->row, o->rcol);
    for (int k = 0; k < 2; k++) {
        uint8_t v = rcell(g, p, D(1, k));
        if (!v) continue;
        for (int t = 0; t < 3; t++) {                                   /* 76F6: 0 updraft, 1 cur-L, 2 cur-R */
            const uint8_t *q = g->tiles->lists + 0x24 + 4 * t;
            for (int n = 0; n < 4 && q[n]; n++) {
                if (v != q[n]) continue;
                int dir = t == 0 ? 2 : (t == 1 ? 4 : 0);
                ai_step(g, o, dir); ai_step(g, o, dir);
                return 1;
            }
        }
    }
    return 0;
}

/* ==================================================================== */
/* Helpers the AI overlays share (each overlay has its own copy of these  */
/* routines; the port keeps one).                                        */
/* ==================================================================== */

/* eai1 A4E8 / eai2 A8F4 / eai5 A5C2 */
uint8_t ai_hero_dirx(const Game *g, const MapObj *e, int range, int *facing_hero, uint8_t *dx)
{
    int d = (int)(int8_t)(uint8_t)(g->hero_map_row - e->row);
    if (d < 0) d = -d;
    if (dx) *dx = (uint8_t)(e->rcol < 0x11 ? 0x11 - e->rcol : e->rcol - 0x11);
    *facing_hero = 0;
    if (d >= range) return 0xFF;
    if (e->rcol < 0x11) { *facing_hero = (e->hit & 0x80) != 0; return 0x80; }
    *facing_hero = (e->hit & 0x80) == 0;
    return 0;
}
uint8_t ai_hero_dir(const Game *g, const MapObj *e, int range, int *facing_hero)
{
    return ai_hero_dirx(g, e, range, facing_hero, NULL);
}

/* eai2 A653 / A549 / A5CE: the 2x4 sprite occupies (row..row+3, rcol..rcol+1)
 * across two consecutive records.  A step needs the four cells of the column
 * (or row) it moves into to be ai-passable and free of sprite markers. */
int ai_tall_step(Game *g, MapObj *e, int dir)
{
    MapObj *lo = e + 1;
    int p = game_ring_index(g, e->row, e->rcol);
    if (dir == 6) {                                                     /* A653 down */
        if (e->rcol < 1 || e->rcol > 0x22) return 1;
        for (int c = 0; c < 2; c++)
            if (!ai_cell_passable(g, rcell(g, p, D(4, c)))) return 1;   /* A679 */
        e->row = (uint8_t)((e->row + 1) & 0x3F);
        lo->row = (uint8_t)((lo->row + 1) & 0x3F);
        return 0;
    }
    int right = (dir == 0);
    if (right) { if (e->rcol >= 0x22) return 1; } else { if (e->rcol < 2) return 1; }
    int dc = right ? 2 : -1;
    for (int r = 0; r < 4; r++)                                         /* A56F / A5F4 */
        if (!ai_cell_passable(g, rcell(g, p, D(r, dc)))) return 1;
    for (int r = 1; r <= 4; r++)                                        /* the column above must be sprite-free */
        if (rcell(g, p, D(-r, dc)) & 0x80) return 1;
    if (right) { if (++e->col >= g->map->width) e->col = 0; e->rcol++; }
    else       { e->col = (uint16_t)(e->col ? e->col - 1 : g->map->width - 1); e->rcol--; }
    lo->col = e->col; lo->rcol = e->rcol;
    return 0;
}

/* eai2 A6BF: the lower record follows the upper one's frame and facing. */
void ai_tall_sync(Game *g, MapObj *e)
{
    (void)g;
    e[1].phase = e->phase;
    e[1].hit = (uint8_t)((e->hit & 0x80) | (e[1].hit & 0x7F));
}

/* eai2 A6AB: either half was hit — the source bits come from the lower record
 * (the original quirk), both halves are marked stunned, the upper takes it. */
void ai_tall_take_hit(Game *g, MapObj *e)
{
    uint8_t a = (uint8_t)((e[1].hit & 0xBF) | 0x20);
    e->hit = a; e[1].hit = (uint8_t)(a | 0x60);
    enemy_take_damage(g, e);
}
/* eai5 A435 / eai7 A71E: the same but with the upper record's own source. */
void ai_tall_take_hit_own(Game *g, MapObj *e)
{
    uint8_t a = (uint8_t)((e->hit & 0xBF) | 0x20);
    e->hit = a; e[1].hit = (uint8_t)(a | 0x60);
    enemy_take_damage(g, e);
}
