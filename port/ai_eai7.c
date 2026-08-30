/* ai_eai7.c — cavern 7 (heat) AI (EAI7.BIN), ported from src/ai/eai7.c.
 * class 0/1 tall ranged walker HP 16 contact 80 EXP 80  (= the eai2 plant)
 * class 2/3 tall spitter       HP 64 contact 80 EXP 200 (= the eai5 spitter)
 * class 4   fast hedgehog      HP 8  contact 40 EXP 50  (eai1 hog, 2 cells/frame) */
#include "enemy.h"
#include <string.h>

#define STEP(o, d) ai_step(g, (o), (d))
#define TALL(o, d) ai_tall_step(g, (o), (d))

static uint8_t dist_r = 8, dist_l = 8;                                  /* A491 / A492 */

/* 0xA41E  the walker's attack: frames 4..7, shot at phase 6 */
static void tallwalker_attack(Game *g, MapObj *e)
{
    e->phase++;
    if (e->phase == 8) { e->next &= (uint8_t)~3; e->phase = 0; }
    else if (e->phase == 6) {                                           /* A437 */
        int right = (e->hit & FACING_RIGHT) != 0;
        Shot s; memset(&s, 0, sizeof s);
        s.cell = (uint8_t)(right ? 0x30 : 0x2F); s.life = 20; s.damage = 40;
        s.flags = (uint8_t)(right ? 0 : 4);
        s.col = (uint8_t)(e->rcol + (right ? 1 : 0));
        s.row = (uint8_t)((e->row + 1) & 0x3F);
        shot_spawn(g, &s);
    }
    ai_tall_sync(g, e);
}

/* 0xA30A  = the eai2 plant with the straight fire shot above. */
static void tallwalker_update(Game *g, MapObj *e)
{
    int facing;
    if (!e->hp) e->hp = 16;
    if ((e->hit & HIT_STUN) || (e[1].hit & 0x40)) { ai_tall_take_hit(g, e); return; }   /* A5F5 */
    if (TALL(e, 6)) return;
    if (e->next & 1) { tallwalker_attack(g, e); return; }
    uint8_t f = ai_hero_dir(g, e, 5, &facing);
    if (!facing) {
        if (f != 0xFF) e->hit ^= FACING_RIGHT;
        unsigned t = (unsigned)e->phase + 0x80; e->phase = (uint8_t)t;
        if (!(t & 0x100)) { ai_tall_sync(g, e); return; }
        e->phase = (uint8_t)((e->phase + 1) & 3);
        if (e->hit & FACING_RIGHT) { if (TALL(e, 0)) e->hit &= (uint8_t)~FACING_RIGHT; }
        else                       { if (TALL(e, 4)) e->hit |= FACING_RIGHT; }
        ai_tall_sync(g, e); return;
    }
    e->hit &= (uint8_t)~FACING_RIGHT;
    if (e->rcol <= 0x11) e->hit |= FACING_RIGHT;
    int at_range = 0, cornered = 0;
    if (e->hit & FACING_RIGHT) {
        uint8_t d = (uint8_t)(0x11 - e->rcol);
        if (d == dist_r) at_range = 1;
        else if (d < dist_r) { if (TALL(e, 4)) cornered = 1; else e->phase = (uint8_t)((e->phase - 1) & 3); }
        else                 { if (TALL(e, 0)) at_range = 1; else e->phase = (uint8_t)((e->phase + 1) & 3); }
    } else {
        uint8_t d = (uint8_t)(e->rcol - 0x11);
        if (d == dist_l) at_range = 1;
        else if (d < dist_l) { if (TALL(e, 0)) cornered = 1; else e->phase = (uint8_t)((e->phase - 1) & 3); }
        else                 { if (TALL(e, 4)) at_range = 1; else e->phase = (uint8_t)((e->phase + 1) & 3); }
    }
    if (at_range) {
        dist_r = (uint8_t)((krn_random(g) & 3) + 7);
        dist_l = (uint8_t)((krn_random(g) & 3) + 7);
        ai_hero_dir(g, e, 5, &facing);
        if (facing) { e->next |= 1; e->phase = 4; }
        ai_tall_sync(g, e); return;
    }
    if (cornered) {
        if (krn_random(g) & 1) return;
        e->next |= 3; e->phase = 4;
    }
    ai_tall_sync(g, e);
}

/* 0xA639  = the eai5 tall spitter with HP 64 and its own shot. */
static void tallspit_update(Game *g, MapObj *e)
{
    int facing;
    if (!e->hp) e->hp = 0x40;
    if (e->hit & HIT_STUN) { ai_tall_take_hit_own(g, e); return; }      /* A71E */
    e[1].hit &= (uint8_t)~0x40;
    if (TALL(e, 6)) return;
    if (e->next & 1) {                                                  /* A6BB */
        unsigned t = (unsigned)e->phase + 0x80; e->phase = (uint8_t)t;
        if (t & 0x100) {
            e->phase++;
            if ((e->phase & 7) == 6) {
                int right = (e->hit & FACING_RIGHT) != 0;
                Shot s; memset(&s, 0, sizeof s);
                s.cell = (uint8_t)(right ? 0x32 : 0x31); s.life = 20; s.damage = 40;
                s.flags = (uint8_t)(right ? 0 : 4);
                s.col = (uint8_t)(e->rcol + (right ? 1 : 0));
                s.row = (uint8_t)((e->row + 1) & 0x3F);
                shot_spawn(g, &s);
            } else if (!(e->phase & 7)) { e->next &= (uint8_t)~1; e->phase = 3; }
        }
        ai_tall_sync(g, e); return;
    }
    ai_hero_dir(g, e, 5, &facing);
    if (facing) {
        if (!(krn_random(g) & 0xC0) && (e->phase & 1)) {
            e->next |= 1; e->phase = 4; ai_tall_sync(g, e); return;
        }
    } else {
        unsigned t = (unsigned)e->phase + 0x80; e->phase = (uint8_t)t;
        if (!(t & 0x100)) { ai_tall_sync(g, e); return; }
    }
    e->phase = (uint8_t)((e->phase + 1) & 3);
    if (!(e->phase & 1)) {
        if (e->rcol <= 0x10) { if (!TALL(e, 0)) e->hit |= FACING_RIGHT; }
        else                 { if (!TALL(e, 4)) e->hit &= (uint8_t)~FACING_RIGHT; }
    }
    ai_tall_sync(g, e);
}

/* 0xA818 */
static const uint8_t gap_r[8]  = {1, 1, 0, 0, 0, 7, 7, 7};              /* A8B1 */
static const uint8_t gap_l[8]  = {3, 3, 4, 4, 4, 5, 5, 5};              /* A8B8 */
static const uint8_t wall_r[8] = {2, 1, 1, 0, 0, 7, 7, 6};              /* A8BF */
static const uint8_t wall_l[8] = {2, 3, 3, 4, 4, 5, 5, 6};              /* A8C7 */

static void runner_jump(Game *g, MapObj *e)
{
    e->next = (uint8_t)(e->next + 0x20);
    if (!(e->next & 0x20)) {
        uint8_t n = (uint8_t)((e->phase + 1) & 3);
        if (!n) { e->next = 0; e->phase = 3; STEP(e, 6); return; }      /* A875 */
        e->phase = (uint8_t)((e->phase & 0xF0) | n);
    }
    const uint8_t *t = (e->hit & FACING_RIGHT) ? ((e->next & 0x10) ? wall_r : gap_r)
                                               : ((e->next & 0x10) ? wall_l : gap_l);
    if (!ai_step_dir(g, e, t[((e->next >> 5) - 1) & 7])) return;
    e->next = 0;                                                        /* A865 */
    if (e->phase) e->phase = 3;
}

/* 0xA749  the eai1 hedgehog at 2 cells per frame, no resting state */
static void runner_update(Game *g, MapObj *e)
{
    int facing;
    if (ai_on_hazard(g, e)) { enemy_killed(g, e); return; }
    if (!e->hp) e->hp = 8;
    if (e->hit & HIT_STUN) { enemy_take_damage(g, e); return; }
    if (e->next & 0x18) { runner_jump(g, e); return; }                  /* A76A */
    if (!STEP(e, 6)) return;
    if (!(e->next & 2)) {                                               /* A77B */
        uint8_t f = ai_hero_dir(g, e, 6, &facing);
        if (!facing && f != 0xFF) { e->hit = (uint8_t)((e->hit & 0x7F) | f); e->next |= 2; return; }
    }
    int p = game_ring_index(g, e->row, e->rcol);
    p = game_ring_add(p, 2 * RING_W + ((e->hit & FACING_RIGHT) ? 1 : 0));   /* A796 */
    if (ai_cell_passable(g, g->ring[p])) { e->phase = 0; e->next |= 8; return; }
    e->phase = (uint8_t)((e->phase + 1) & 3);
    if (!(e->next & 2)) {
        unsigned t = (unsigned)e->link + 0x10; e->link = (uint8_t)t;    /* A7D2 */
        if (t & 0x100) { e->next ^= 0x80; return; }
    }
    ai_hero_dir(g, e, 6, &facing);
    if (!facing) e->next &= (uint8_t)~2;                                /* A7DD */
    if (e->hit & FACING_RIGHT) { STEP(e, 0); if (!STEP(e, 0)) return; } /* A7EC */
    else                       { STEP(e, 4); if (!STEP(e, 4)) return; }
    e->phase = 0; e->next |= 0x10;                                      /* A7F9 */
}

/* 0xA2F1 (dispatch A2FF) */
void eai7_entry(Game *g, MapObj *e)
{
    switch (e->type & 0xF) {
    case 0: tallwalker_update(g, e); break;
    case 1: case 3: break;                                              /* A309 / A638: ret */
    case 2: tallspit_update(g, e);   break;
    case 4: runner_update(g, e);     break;
    default: break;
    }
}
