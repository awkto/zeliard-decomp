/* shots.c — enemy projectiles (EB80), the hero's magic (EB15) and the
 * orbiting spheres (EB60).  Port of src/fight.c 8366..8C78 (docs/FIGHT.md §6);
 * the hex tags are fight.bin addresses.
 *
 * Everything here is cell-granular like the rest of the engine: a projectile
 * moves one ring cell per frame in one of eight directions (table 85C2), a
 * magic bolt two map columns per frame (8BD0). */
#include "enemy.h"
#include <string.h>

/* ====================================================================== */
/* Enemy projectiles                                                       */
/* ====================================================================== */

/* 0x6DEC  passable_shot: 0x49+ passes, otherwise the tileset's 24-entry list. */
static int passable_shot(const Game *g, uint8_t v)
{
    if (v >= 0x49) return 1;
    for (int i = 0; i < 24; i++) if (g->tiles->lists[i] == v) return 1;
    return (v & 0x80) != 0;
}

/* 0x83DB  vec 30: erase and drop every projectile. */
void shots_clear(Game *g)
{
    memset(g->shots, 0, sizeof g->shots);
    g->shots[0].col = 0xFF;
    g->projectile_count = 0;
}

/* 0x8611  vec 29: append the 13-byte template, at most 31 live. */
void shot_spawn(Game *g, const Shot *tmpl)
{
    if (g->projectile_count >= MAX_SHOTS) return;
    int i = 0;
    while (i < MAX_SHOTS && g->shots[i].col != 0xFF) i++;
    if (i >= MAX_SHOTS) return;
    g->shots[i] = *tmpl;
    g->shots[i].drawn = 0;
    g->shots[i + 1].col = 0xFF;
    g->projectile_count++;
    g->shots_fired++;
}

/* 0x85F2  scripted path: script[age] & 7 becomes the direction; 0xFF ends it. */
static int shot_script(Shot *s)
{
    if (!s->script) return 1;
    uint8_t d = s->script[s->age];
    if (d == 0xFF) return 1;
    s->flags = (uint8_t)((s->flags & 0xF8) | (d & 7));
    return 0;
}

/* 0x85A5  one cell in the shot's direction (table 85C2: 0 R, then anticlockwise). */
static void shot_step(Shot *s)
{
    if ((s->flags & 0x40) && shot_script(s)) { s->col = 0; return; }
    switch (s->flags & 7) {
    case 0: s->col++;             break;                                /* 85D5 right */
    case 1: s->row--; s->col++;   break;                                /* 85D2 right-up */
    case 2: s->row--;             break;                                /* 85EE up */
    case 3: s->row--; s->col--;   break;                                /* 85DE left-up */
    case 4: s->col--;             break;                                /* 85E1 left */
    case 5: s->row++; s->col--;   break;                                /* 85E4 left-down */
    case 6: s->row++;             break;                                /* 85EA down */
    default: s->row++; s->col++;  break;                                /* 85D8 right-down */
    }
    s->row &= 0x3F;                                                     /* 85BD */
}

/* 0x8556..0x85A3  the shield blocks a shot whose row matches the hero's middle
 * row (directions 0/4), middle-1 (1..3) or middle+1 (5..7). */
static int shield_row_match(const Game *g, int d, uint8_t row)
{
    uint8_t mid = (uint8_t)((g->hero_scr_row + g->scroll_row + 1 + (g->crouching ? 1 : 0)) & 0x3F);
    if (d == 0 || d == 4) return row == mid;
    if (d >= 1 && d <= 3) return row == (uint8_t)((mid - 1) & 0x3F);
    return row == (uint8_t)((mid + 1) & 0x3F);
}

/* 0x846F  step, wall test, then the hero hit test. */
static void shot_move_and_hit(Game *g, Shot *s)
{
    shot_step(s);                                                       /* 85A5 */
    if (!(s->flags & 8)) {
        if (!s->col) return;
        if (!passable_shot(g, g->ring[game_ring_index(g, s->row, s->col)])) { s->col = 0; return; }
    }
    uint8_t r = (uint8_t)((g->scroll_row + g->hero_scr_row) & 0x3F);
    int hit_row = 0;
    if (!g->crouching && r == s->row) hit_row = 1;                      /* 8490 */
    for (int n = 2; n && !hit_row; n--) { r = (uint8_t)((r + 1) & 0x3F); if (r == s->row) hit_row = 1; }
    if (!hit_row) return;
    uint8_t c = (uint8_t)(g->hero_scr_col + 4 + ((g->hero_flags & FACE_LEFT) ? 1 : 0));   /* 84B4 */
    if (s->col != c && s->col != (uint8_t)(c + 1)) return;              /* 84C2 */
    s->col = 0;                                                         /* 84CD: consumed */
    int d = s->flags & 7;
    if (g->shield && !g->attacking && !g->on_ladder && d != 2 && d != 6) {   /* 84D0 */
        int from_left = (d == 0 || d == 1 || d == 7);
        if (from_left == ((g->hero_flags & FACE_LEFT) != 0)) {
            if (g->shield >= 4) { g->sfx_request = 0x0A; return; }       /* 854F */
            if (shield_row_match(g, d, s->row)) { g->sfx_request = 0x0A; return; }
        }
    }
    hero_damage(g, s->damage);                                          /* 850E */
    g->sfx_request = 9;
    g->hero_hit = g->hero_hit_flash = 0xFF;
    if (d == 2 || d == 6)                { g->hit_side[0] = g->hit_side[1] = g->hit_side[2] = g->hit_side[3] = 0xFF; }
    else if (d == 0 || d == 1 || d == 7) { g->hit_side[0] = g->hit_side[1] = 0xFF; g->hit_side[2] = g->hit_side[3] = 0; }
    else                                 { g->hit_side[0] = g->hit_side[1] = 0; g->hit_side[2] = g->hit_side[3] = 0xFF; }
}

/* 0x8422  move every live shot, compacting the list as it goes. */
void shots_update(Game *g)
{
    g->projectile_count = 0;
    int dst = 0;
    for (int i = 0; i <= MAX_SHOTS; i++) {
        Shot *s = &g->shots[i];
        if (s->col == 0xFF) break;
        if (!s->col && !(s->drawn & 0x8000)) continue;                  /* 842F: dead and erased */
        s->age++;
        shot_move_and_hit(g, s);
        if (dst != i) g->shots[dst] = *s;
        if (!(s->flags & 0x40) && s->age >= s->life) g->shots[dst].col = 0;   /* 8455 */
        dst++;
        g->projectile_count++;
    }
    g->shots[dst].col = 0xFF;
}

/* 0x8639 / 0x864E  the ring scrolled, so every live shot changes ring column. */
void shots_shift(Game *g, int right)
{
    for (int i = 0; i <= MAX_SHOTS && g->shots[i].col != 0xFF; i++)
        if (g->shots[i].col) g->shots[i].col = (uint8_t)(g->shots[i].col + (right ? 1 : -1));
}

/* 0x83D7  animation mask by the cell's top two bits. */
uint8_t shot_draw_cell(const Shot *s)
{
    static const uint8_t mask[4] = {0, 1, 3, 7};
    return (uint8_t)((s->cell + (s->age & mask[(s->cell >> 6) & 3])) & 0x3F);
}

/* ====================================================================== */
/* Magic                                                                   */
/* ====================================================================== */

/* 0x8C4F  one cell of a spell's 3x3 hit block. */
static void magic_hit_cell(Game *g, int p)
{
    uint8_t v = g->ring[p];
    if (!(v & 0x80)) return;
    int i = v & 0x7F;
    if (i >= g->nobj) return;
    MapObj *o = &g->obj[i];
    if (o->type & 0x20) return;                                         /* 8C55: sword/magic immune */
    if (o->hit & 0x20) return;                                          /* 8C5A: already stunned */
    o->hit = (uint8_t)((o->hit & 0xE0) | 0x40 | (g->magic_sel + 1));    /* 8C61 */
    g->magic_hit_any = 0xFF;
}

/* 0x8BF7  hit every sprite in the 3x3 block centred on the sprite's top-left.
 * Returns 1 when something was hit (the original's CF=0). */
static int magic_hit(Game *g, Magic *m)
{
    if (g->boss_map && g->boss_cutscene) return 0;                      /* 8BF7 */
    uint8_t rcol;
    if (ai_map_col_to_ring(g, m->col, &rcol)) return 0;
    if ((uint8_t)(rcol - 2) >= 0x20) return 0;                          /* 8C11: ring col 2..0x21 */
    int p = game_ring_add(game_ring_index(g, m->row, rcol), -(RING_W + 1));
    g->magic_hit_any = 0;
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) magic_hit_cell(g, game_ring_add(p, c));
        p = game_ring_add(p, RING_W);
    }
    return g->magic_hit_any != 0;
}

/* 0x8BD0  two map columns per frame in the record's facing. */
static void magic_move(Game *g, Magic *m)
{
    int c = (int)m->col + (((m->dir & 1) * 4) - 2);
    if (c < 0) c += g->map->width;
    else if (c >= g->map->width) c -= g->map->width;
    m->col = (uint16_t)c;
}
/* 0x8BC2  anim = (anim + 1) % 3, then move. */
static void magic_anim_move(Game *g, Magic *m)
{
    m->anim = (uint8_t)(m->anim + 1 >= 3 ? 0 : m->anim + 1);
    magic_move(g, m);
}

/* 0x8BB5  the spell is over: every record is retired and FF3E cleared. */
static void magic_end(Game *g)
{
    for (int i = 0; i < 4; i++) g->magic[i].live = 0;
    g->magic_active = 0;
}

/* 0x884D  one bolt starting at the hero, moving in his facing. */
static void spell_spawn_bolt(Game *g, Magic *m)
{
    memset(m, 0, sizeof *m);
    m->live = 1;
    m->dir = (uint8_t)(!(g->hero_flags & FACE_LEFT));                   /* 884C: 1 = right */
    m->row = (uint8_t)(((g->crouching & 1) + g->hero_scr_row + g->scroll_row) & 0x3F);   /* 8857 */
    int c = g->hero_scr_col + 4 + (m->dir ? 0 : 1) + g->scroll_col;     /* 8869 */
    if (c >= g->map->width) c -= g->map->width;
    m->col = (uint16_t)c;
}

/* 0x88A8  spell 5: four sprites raining from three rows above the window. */
static void spell_spawn_rain(Game *g)
{
    for (int k = 4; k >= 1; k--) {
        Magic *m = &g->magic[4 - k];
        memset(m, 0, sizeof *m);
        m->live = 1;
        int c = 6 * k + 2 + g->scroll_col;
        if (c >= g->map->width) c -= g->map->width;
        m->col = (uint16_t)c;
        m->row = (uint8_t)((g->scroll_row - 3 - (krn_random(g) & 3)) & 0x3F);   /* 88C3 */
    }
}

/* 0x88F8  spell 6: three bolts, the first two rows up, the second two down. */
static void spell_spawn_spread(Game *g)
{
    for (int i = 0; i < 3; i++) spell_spawn_bolt(g, &g->magic[i]);
    g->magic[0].row = (uint8_t)((g->magic[0].row - 2) & 0x3F);           /* 8907 */
    g->magic[1].row = (uint8_t)((g->magic[1].row + 2) & 0x3F);           /* 890F */
}

/* 0x8918  spell 7: hit every sprite in the window at once, then stop. */
static void spell_screen_wide(Game *g)
{
    if (!(g->boss_map && g->boss_cutscene)) {
        int p = game_ring_add(game_win(g), -RING_W);
        for (int r = 0; r < 0x13; r++) {
            for (int c = 0; c < RING_W; c++) magic_hit_cell(g, game_ring_add(p, c));
            p = game_ring_add(p, RING_W);
        }
    }
    g->magic_active = 0;                                                /* 8954 */
    g->sfx_request = 0x19;
}

/* 0x87B0  the magic button: a 6-frame cast, the charge spent on frame 4. */
void magic_input(Game *g)
{
    if (!g->magic_sel) return;
    if (g->casting) {
        g->cast_timer = (uint8_t)(g->cast_timer + 2);                   /* 87F1 */
        if (g->cast_timer != 4) { if (g->cast_timer >= 6) g->casting = 0; return; }
        int sel = g->magic_sel;
        if (!g->magic_count[sel - 1]) return;                           /* 880B */
        g->magic_count[sel - 1]--;
        g->sfx_request = 0x18;
        g->magic_active = 0xFF;
        g->magic_casts++;
        for (int i = 0; i < 4; i++) g->magic[i].live = 0;
        if (sel <= 4)      spell_spawn_bolt(g, &g->magic[0]);           /* 883F table */
        else if (sel == 5) spell_spawn_rain(g);
        else if (sel == 6) spell_spawn_spread(g);
        else               spell_screen_wide(g);
        return;
    }
    if (!g->btn2_edge) return;
    g->btn1_edge = g->btn2_edge = 0;                                    /* 87C7 */
    if (g->attacking || g->magic_active) return;
    g->cast_timer = 0; g->casting = 0xFF; g->sfx_request = 0x17;
}

/* 0x8AAD  the per-frame spell effect (table 8AC6 by magic_sel). */
void magic_update(Game *g)
{
    if (!g->magic_active) return;
    Magic *m = &g->magic[0];
    switch (g->magic_sel) {
    case 1:                                                             /* 8AD4 */
        if (m->dir & 0x80) { magic_end(g); return; }
        if (++m->age >= 5) { magic_end(g); return; }
        magic_anim_move(g, m);
        if (magic_hit(g, m)) m->dir |= 0x80;                            /* 8AF2 */
        return;
    case 2: case 4:                                                     /* 8AF7 */
        if (++m->age >= 10) { magic_end(g); return; }
        magic_anim_move(g, m);
        magic_hit(g, m);
        return;
    case 3:                                                             /* 8B09: flies 4 cells, then falls */
        if (++m->age >= 12) { magic_end(g); return; }
        if (m->age < 4) { magic_move(g, m); magic_hit(g, m); return; }
        m->anim = (uint8_t)((m->anim & 3) + 1);
        if (m->age != 3) {
            uint8_t rcol;
            if (!ai_map_col_to_ring(g, m->col, &rcol) && rcol < 0x21) {
                int p = game_ring_add(game_ring_index(g, m->row, rcol), 2 * RING_W);
                if (game_passable_wall(g, g->ring[p]) && game_passable_wall(g, g->ring[game_ring_add(p, 1)]))
                    m->row = (uint8_t)((m->row + 1) & 0x3F);            /* 8B5A */
            }
        }
        magic_hit(g, m);
        return;
    case 5:                                                             /* 8B64: the rain falls 2 rows a frame */
        if (++m->age >= 12) { magic_end(g); return; }
        for (int i = 0; i < 4; i++) {
            g->magic[i].row = (uint8_t)((g->magic[i].row + 2) & 0x3F);
            magic_hit(g, &g->magic[i]);
        }
        return;
    case 6:                                                             /* 8B83 */
        if (++m->age >= 10) { magic_end(g); return; }
        for (int i = 0; i < 3; i++) { magic_anim_move(g, &g->magic[i]); magic_hit(g, &g->magic[i]); }
        return;
    default:                                                            /* 8B9C: spell 7 acts once, at cast time */
        return;
    }
}

/* 0x896E  the 2x2 sprite cells of a live record.  The pointer tables 8C81
 * (facing right) / 8C8D (facing left) select a list of 3 animation frames x 4
 * cells; the cells are slots 0x67..0x7E of the tile bank, which GF_LOAD_HERO_ANIM
 * (gfmcga 44CE) refills per spell from the parked segment — the port draws them
 * from the tileset bank instead, so the shapes are right but the artwork is the
 * cavern's DCHR tail (see README "Stubbed"). */
static const uint8_t MAGIC_CELLS[2][7][12] = {
  { /* 8C8D, facing left */
    {0x67,0x68,0x69,0x6A, 0x6B,0x6C,0x6D,0x6E, 0x6F,0x70,0x71,0x72},
    {0x73,0x74,0x75,0x76, 0x77,0x78,0x79,0x7A, 0x7B,0x7C,0x7D,0x7E},
    {0x6B,0x6C,0x6D,0x6E, 0x6F,0x70,0x71,0x72, 0x73,0x74,0x75,0x76},
    {0x73,0x74,0x75,0x76, 0x77,0x78,0x79,0x7A, 0x7B,0x7C,0x7D,0x7E},
    {0x73,0x74,0x75,0x76, 0x67,0x68,0x69,0x6A, 0x6B,0x6C,0x6D,0x6E},
    {0x73,0x74,0x75,0x76, 0x77,0x78,0x79,0x7A, 0x7B,0x7C,0x7D,0x7E},
    {0x67,0x68,0x69,0x6A, 0x6B,0x6C,0x6D,0x6E, 0x6F,0x70,0x71,0x72},  /* spell 7 never draws */
  },
  { /* 8C81, facing right */
    {0x67,0x68,0x69,0x6A, 0x6B,0x6C,0x6D,0x6E, 0x6F,0x70,0x71,0x72},
    {0x67,0x68,0x69,0x6A, 0x6B,0x6C,0x6D,0x6E, 0x6F,0x70,0x71,0x72},
    {0x67,0x68,0x69,0x6A, 0x6F,0x70,0x71,0x72, 0x73,0x74,0x75,0x76},
    {0x67,0x68,0x69,0x6A, 0x6B,0x6C,0x6D,0x6E, 0x6F,0x70,0x71,0x72},
    {0x73,0x74,0x75,0x76, 0x67,0x68,0x69,0x6A, 0x6B,0x6C,0x6D,0x6E},
    {0x67,0x68,0x69,0x6A, 0x6B,0x6C,0x6D,0x6E, 0x6F,0x70,0x71,0x72},
    {0x67,0x68,0x69,0x6A, 0x6B,0x6C,0x6D,0x6E, 0x6F,0x70,0x71,0x72},
  },
};

/* out[0..3] = TL, TR, BL, BR (offset table 8C79: (0,0)(1,0)(0,1)(1,1)) */
int magic_sprite_cells(const Game *g, const Magic *m, uint8_t out[4])
{
    if (!m->live || !g->magic_sel || g->magic_sel > 7) return 0;
    const uint8_t *l = MAGIC_CELLS[(m->dir & 0xFF) != 0][g->magic_sel - 1] + 4 * (m->anim % 3);
    for (int i = 0; i < 4; i++) out[i] = l[i];
    return 1;
}

/* ====================================================================== */
/* Orbs (EB60)                                                             */
/* ====================================================================== */

/* 0x8790  16 orbit offsets (dx, dy) around the hero's top-left. */
static const int8_t ORBIT[16][2] = {
    {2, 1}, {2, 0}, {3, -1}, {4, -2}, {5, -2}, {6, -2}, {7, -1}, {8, 0},
    {8, 1}, {8, 2}, {7, 3},  {6, 4},  {5, 4},  {4, 4},  {3, 3},  {2, 2}};

/* 0x8765  one cell under the orb: hit source 9, one charge spent. */
static void orb_hit_cell(Game *g, Orb *o, int p)
{
    if (!o->hits) return;
    uint8_t v = g->ring[p];
    if (!(v & 0x80)) return;
    int i = v & 0x7F;
    if (i >= g->nobj) return;
    MapObj *e = &g->obj[i];
    if (e->type & 0x20) return;
    if (e->hit & 0x20) return;
    e->hit = (uint8_t)((e->hit & 0xE0) | 0x49);                         /* 8784: pending, source 9 */
    o->hits--;
}

/* 0x86FC  advance the phase and hit the 2x2 block the orb covers. */
void orbs_update(Game *g)
{
    for (int i = 0; i < 4; i++) {
        Orb *o = &g->orbs[i];
        if (o->phase == 0xFF) continue;
        o->phase = (uint8_t)((o->phase + o->speed) & 0xF);
        if (g->boss_map && g->boss_defeated) continue;                  /* 8741 */
        int col = g->hero_scr_col + ORBIT[o->phase][0];
        int row = g->hero_scr_row + ORBIT[o->phase][1] + g->scroll_row;
        int p = game_ring_add(game_ring_index(g, (uint8_t)row, (uint8_t)(col & 0xFF)), -(RING_W + 1));
        orb_hit_cell(g, o, p);
        orb_hit_cell(g, o, game_ring_add(p, 1));
        orb_hit_cell(g, o, game_ring_add(p, RING_W));
        orb_hit_cell(g, o, game_ring_add(p, RING_W + 1));
    }
}

/* the port's way of arming the spheres (the original's item menu does it) */
void orbs_arm(Game *g, int n, uint8_t speed, uint8_t hits)
{
    for (int i = 0; i < 4; i++) {
        g->orbs[i].phase = 0xFF;
        g->orbs[i].speed = 0; g->orbs[i].hits = 0;
    }
    for (int i = 0; i < n && i < 4; i++) {
        g->orbs[i].phase = (uint8_t)(i * 4);
        g->orbs[i].speed = speed; g->orbs[i].hits = hits;
    }
}
