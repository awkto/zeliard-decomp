/* ai_eai4.c — cavern 4 (ice) AI (EAI4.BIN), ported from src/ai/eai4.c.
 * class 0   shell crawler   HP 8  contact 20 EXP 10
 * class 1   dividing slime  HP 16 contact 4  EXP 10
 * class 2/3 icicle          sword-immune, contact 80, EXP 0
 * class 4   spinning blade  HP 2  contact 80 EXP 20 */
#include "enemy.h"
#include <string.h>

#define STEP(o, d) ai_step(g, (o), (d))

static const uint8_t jump_r[8] = {2, 1, 1, 0, 0, 7, 7, 6};              /* A456 */
static const uint8_t jump_l[8] = {2, 3, 3, 4, 4, 5, 5, 6};              /* A45E */

/* 0xA368  Dash: spin frames E/F, then 2 cells per frame diagonally down or
 * straight; both blocked -> turn around and uncurl. */
static void shell_dash(Game *g, MapObj *e)
{
    uint8_t f = (uint8_t)((e->phase & 0xF) + 1);
    if (f < 0xF) { e->phase = f; return; }
    e->phase = (uint8_t)(f >= 0x10 ? 0xE : 0xF);                        /* A377 */
    if (e->hit & FACING_RIGHT) {
        STEP(e, 7); if (!STEP(e, 7)) return;                            /* A386 */
        STEP(e, 0); if (!STEP(e, 0)) return;
        e->hit &= (uint8_t)~FACING_RIGHT;
    } else {
        STEP(e, 5); if (!STEP(e, 5)) return;                            /* A3A6 */
        STEP(e, 4); if (!STEP(e, 4)) return;
        e->hit |= FACING_RIGHT;
    }
    e->phase = 0x1D; e->next = 2;                                       /* A3C4 */
}

/* 0xA3CD  Blocked by a wall: 16 frames of "press down", one sideways step,
 * then an 8-step jump arc. */
static void shell_wall_jump(Game *g, MapObj *e)
{
    uint8_t f = (uint8_t)((e->phase + 1) & 0xF);
    if (f >= 0xD) f = 0xB;
    e->phase = f;                                                       /* A3CD */
    if (!(e->link & 1)) {
        STEP(e, 6);                                                     /* A3E3 */
        e->next = (uint8_t)(e->next + 0x10);
        if (e->next & 0xF0) return;
        e->link |= 1; return;                                           /* A3F3 */
    }
    if (!(e->link & 4)) {                                               /* A3FE */
        e->link |= 4;
        if (e->link & 8) STEP(e, 4); else STEP(e, 0);
        return;
    }
    uint8_t idx = (uint8_t)((e->next >> 5) & 7);                        /* A41E */
    e->next = (uint8_t)(e->next + 0x20);
    if (!(e->next & 0xE0)) { e->link = 0; e->next = 2; }                /* A433 */
    if (!ai_step_dir(g, e, ((e->hit & FACING_RIGHT) ? jump_r : jump_l)[idx])) return;
    if ((e->next & 0xE0) >= 0xC0) e->hit ^= FACING_RIGHT;               /* A444 */
}

/* 0xA281 */
static void shell_update(Game *g, MapObj *e)
{
    if (!e->hp) e->hp = 8;
    if (e->hit & HIT_STUN) { enemy_take_damage(g, e); return; }         /* A28B */
    if (e->next & 8) { shell_wall_jump(g, e); return; }                 /* A296 */
    if (e->next & 4) { shell_dash(g, e); return; }                      /* A29F */
    if (STEP(e, 6)) return;                                             /* A2A8 */
    if (e->next & 1) {                                                  /* A324: curl 8..B */
        if ((e->phase & 0xF) < 8) { e->phase = 8; return; }
        if (++e->phase == 0xB) { e->phase |= 0x10; e->next &= (uint8_t)~1; }
        return;
    }
    if (e->next & 2) {                                                  /* A346: uncurl B..8 */
        if ((e->phase & 0xF) >= 0xC) { e->phase = 0xB; return; }
        if (--e->phase == 8) { e->phase |= 0x10; e->next &= (uint8_t)~2; }
        return;
    }
    e->phase = (uint8_t)((e->phase & 0xF0) | ((e->phase + 1) & 7));     /* A2BF */
    unsigned t = (unsigned)e->phase + 0x80; e->phase = (uint8_t)t;
    if (!(t & 0x100)) return;
    int level = (g->hero_map_row == e->row) || (((g->hero_map_row + 1) & 0x3F) == e->row);   /* A2D5 */
    int right;
    if (level) {
        if (!(krn_random(g) & 3)) e->next = 5;                          /* A2EF */
        right = e->rcol < 0x11;
    } else right = (e->hit & FACING_RIGHT) != 0;
    if (right) { e->hit |= FACING_RIGHT;  if (STEP(e, 0)) e->next = 9; }   /* A313 */
    else       { e->hit &= (uint8_t)~FACING_RIGHT; if (STEP(e, 4)) e->next = 9; }   /* A302 */
}

/* 0xA679  three rows x three cells from `p` all ai-passable */
static int block3x3_free(Game *g, int p)
{
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            if (!ai_cell_passable(g, g->ring[game_ring_add(p, r * RING_W + c)])) return 0;
    return 1;
}

/* 0xA584  Spawn the copy two cells ahead into the claimed spare record. */
static void slime_split(Game *g, MapObj *e)
{
    int i = (int)(e - g->obj);
    if (e->link >= g->nobj) { e->flags &= (uint8_t)~0x40; return; }
    MapObj *s = &g->obj[e->link];
    int p = game_ring_index(g, e->row, e->rcol);
    int dst;
    if (!(e->hit & FACING_RIGHT)) {
        if (e->rcol >= 0x21) return;                                    /* A5A5 */
        if (!block3x3_free(g, game_ring_add(p, -RING_W + 1))) return;
        dst = game_ring_add(p, 2);
        s->col = (uint16_t)((e->col + 2) % g->map->width); s->rcol = (uint8_t)(e->rcol + 2);
        e->phase = 0x16; s->phase = 0x17;
    } else {
        if (e->rcol < 3) return;                                        /* A5FC */
        if (!block3x3_free(g, game_ring_add(p, -RING_W - 3))) return;
        dst = game_ring_add(p, -2);
        s->col = (uint16_t)((e->col + g->map->width - 2) % g->map->width); s->rcol = (uint8_t)(e->rcol - 2);
        e->phase = 0x17; s->phase = 0x16;
    }
    g->under_sprite[e->link] = g->ring[dst];                            /* A5C3 */
    g->ring[dst] = (uint8_t)(0x80 | e->link);
    s->type = (uint8_t)(e->type & 0x1F); s->row = e->row; s->flags = 0;
    s->hp = 0; s->next = 0; s->link = 0; s->hit = 0;
    e->flags &= (uint8_t)~0x40;
    if (i < (int)e->link) s->next |= 1;                                 /* A66B */
}

/* 0xA466  Dividing slime: only magic 3/4/7 or the Enchantment sword hurts it;
 * any other hit makes it split instead. */
static void slime_update(Game *g, MapObj *e)
{
    if (!e->hp) e->hp = 16;
    if (e->hit & HIT_STUN) {                                            /* A470 */
        uint8_t src = (uint8_t)(e->hit & 0x1F);
        if (src == 4 || src == 5 || src == 8 || (src == 1 && g->sword == 6) || (e->phase & 1)) {
            enemy_take_damage(g, e); return;                            /* A47B */
        }
        e->hit &= (uint8_t)~HIT_STUN;                                   /* A4B1 */
        if (!(e->flags & 0x40)) {
            int idx = ai_find_spare(g);                                 /* A4BB: vec 31 */
            if (idx >= 0) {
                MapObj *s = &g->obj[idx];
                s->col = 0xFF00;                                        /* A4C2 */
                if (s->flags & 0x40) { s->flags &= (uint8_t)~0x40; if (s->link < g->nobj) g->obj[s->link].row = 0; }
                s->row = 0x7F;
                e->link = (uint8_t)idx; e->flags |= 0x40;               /* A4E1 */
            }
        }
    }
    int skip = e->next & 1;
    e->next &= (uint8_t)~1;
    if (skip) return;                                                   /* A4EC */
    if (e->flags & 0x40) {                                              /* A56C */
        e->phase = (uint8_t)((e->phase & 0xF0) | ((e->phase + 1) & 7));
        if ((e->phase & 7) == 6) { slime_split(g, e); return; }
    } else e->phase = (uint8_t)((e->phase & 0xF0) | ((e->phase + 1) & 3));   /* A4FF */
    if (STEP(e, 6)) return;                                             /* A514 */
    e->phase = (uint8_t)(e->phase - 0x10);
    if (e->phase & 0xF0) return;                                        /* A51C */
    e->phase |= 0x40;
    int level = (g->hero_map_row == e->row) || (((g->hero_map_row + 1) & 0x3F) == e->row);   /* A52B */
    if (level ? (e->rcol > 0x10) : !(e->hit & FACING_RIGHT)) {
        e->hit &= (uint8_t)~FACING_RIGHT;
        if (!STEP(e, 4)) return;                                        /* A54B */
    }
    e->hit |= FACING_RIGHT;
    if (!STEP(e, 0)) return;                                            /* A557 */
    e->hit &= (uint8_t)~FACING_RIGHT; STEP(e, 4);                       /* A563 */
}

/* 0xA6B1  Icicle: hangs until the hero is roughly below, then falls and dies. */
static void icicle_update(Game *g, MapObj *e)
{
    e->type |= 0x20;
    if (!(e->next & 1)) {
        if (e->rcol < 8 || e->rcol >= 0x13) return;                     /* A6BB */
        if (krn_random(g) & 3) return;                                  /* A6C8 */
        e->phase = 1; e->next |= 1; return;
    }
    if (STEP(e, 6)) return;                                             /* A6DB */
    e->flags = (uint8_t)((e->flags & 0xF0) | 1);
    enemy_killed(g, e);                                                 /* A6E3 */
}

/* 0xA71C  One crawl candidate.  Direction sequence and next-state formula are
 * the A756 / A7CE tables:
 *   facing left  (A756): dir = (state + 6 + i) & 7, next = (8 - dir) & 7
 *   facing right (A7CE): dir = (6 - state - i) & 7, next = (4 - dir) & 7
 * The tables' third column (the sprite frame) is not reconstructed: the port
 * spins the 4-frame animation instead. */
static int blade_step(Game *g, MapObj *e)
{
    int right = (e->hit & FACING_RIGHT) != 0;
    for (int i = 0; i < 5; i++) {
        int dir = right ? ((6 - e->next - i) & 7) : ((e->next + 6 + i) & 7);
        if (ai_step_dir(g, e, (uint8_t)dir)) continue;
        e->next = (uint8_t)(right ? ((4 - dir) & 7) : ((8 - dir) & 7));  /* A749 */
        e->phase = (uint8_t)((e->phase + 1) & 3);
        return i;
    }
    e->hit ^= FACING_RIGHT;                                             /* A744 */
    return -1;
}

/* 0xA6F0  Spinning blade: crawls along surfaces, 2 cells on straights. */
static void blade_update(Game *g, MapObj *e)
{
    if (!e->hp) e->hp = 2;
    if (e->hit & HIT_STUN) { enemy_take_damage(g, e); return; }         /* A6FA */
    if (e->rcol < 3 || e->rcol >= 0x21) return;                         /* A705 */
    if (blade_step(g, e) == 2) blade_step(g, e);                        /* A713 */
}

/* 0xA269 (dispatch A277) */
void eai4_entry(Game *g, MapObj *e)
{
    switch (e->type & 0xF) {
    case 0: shell_update(g, e);  break;
    case 1: slime_update(g, e);  break;
    case 2: case 3: icicle_update(g, e); break;
    case 4: blade_update(g, e);  break;
    default: break;
    }
}
