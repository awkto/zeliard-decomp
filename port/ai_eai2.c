/* ai_eai2.c — cavern 2 AI (EAI2.BIN), ported from src/ai/eai2.c.  Hex tags
 * are offsets inside the overlay image; the data tables (EXP, contact damage,
 * drops, sprite frames) are read from the original by enemy.c.
 *
 * class 0/1 tall plant  HP 8   contact 10  EXP 10
 * class 2   blue slime  HP 4   contact 8   EXP 4
 * class 3   red frog    HP 2   contact 10  EXP 10
 * class 4/5 bird        HP 3   contact 8/40 EXP 4/255 */
#include "enemy.h"
#include <string.h>

#define STEP(o, d) ai_step(g, (o), (d))
#define TALL(o, d) ai_tall_step(g, (o), (d))

/* A531 / A53D: the arc shot's scripted path (RU x3, R x2, RD x6) */
static const uint8_t arc_r[] = {1,1,1, 0,0, 7,7,7,7,7,7, 0xFF};
static const uint8_t arc_l[] = {3,3,3, 4,4, 5,5,5,5,5,5, 0xFF};

static uint8_t dist_r = 8, dist_l = 8;                                  /* A6D6 / A6D7 */

/* A4FD..A524: the four 13-byte shot templates */
static Shot shot_of(int right, int straight)
{
    Shot s; memset(&s, 0, sizeof s);
    s.cell = 0x9A;
    if (straight) { s.life = 7; s.damage = 20; s.flags = (uint8_t)(right ? 0 : 4); }
    else          { s.life = 0xFF; s.damage = 8; s.flags = 0x40; s.script = right ? arc_r : arc_l; }
    return s;
}

/* 0xA49D  Attack: frames 4..7, the shot leaves at phase 6, done at 8. */
static void plant_attack(Game *g, MapObj *e)
{
    e->phase++;
    if (e->phase == 8) { e->next &= (uint8_t)~3; e->phase = 0; }        /* A4AF */
    else if (e->phase == 6) {                                           /* A4BA */
        int right = (e->hit & FACING_RIGHT) != 0;
        Shot t = shot_of(right, e->next & 2);
        t.col = (uint8_t)(e->rcol + (right ? 1 : 0));
        t.row = (uint8_t)((e->row + 2) & 0x3F);
        shot_spawn(g, &t);                                              /* A4F5 */
    }
    ai_tall_sync(g, e);
}

/* 0xA384  The tall plant: keeps a random distance 7..10 from the hero and
 * fires an arc (or, cornered, a straight shot). */
static void plant_update(Game *g, MapObj *e)
{
    int facing;
    if (!e->hp) e->hp = 8;                                              /* A384 */
    if ((e->hit & HIT_STUN) || (e[1].hit & 0x40)) { ai_tall_take_hit(g, e); return; }   /* A38E */
    if (TALL(e, 6)) return;                                             /* A3A0: falling */
    if (e->next & 1) { plant_attack(g, e); return; }                    /* A3A6 */
    uint8_t f = ai_hero_dir(g, e, 5, &facing);                          /* A3AF */
    if (!facing) {
        if (f != 0xFF) e->hit ^= FACING_RIGHT;                          /* A3B8 */
        unsigned t = (unsigned)e->phase + 0x80; e->phase = (uint8_t)t;  /* A3BC: every 2nd frame */
        if (!(t & 0x100)) { ai_tall_sync(g, e); return; }
        e->phase = (uint8_t)((e->phase + 1) & 3);
        if (e->hit & FACING_RIGHT) { if (TALL(e, 0)) e->hit &= (uint8_t)~FACING_RIGHT; }
        else                       { if (TALL(e, 4)) e->hit |= FACING_RIGHT; }
        ai_tall_sync(g, e); return;
    }
    e->hit &= (uint8_t)~FACING_RIGHT;                                   /* A3F0 */
    if (e->rcol <= 0x11) e->hit |= FACING_RIGHT;
    int at_range = 0, cornered = 0;
    if (e->hit & FACING_RIGHT) {
        uint8_t dist = (uint8_t)(0x11 - e->rcol);                       /* A405 */
        if (dist == dist_r) at_range = 1;
        else if (dist < dist_r) { if (TALL(e, 4)) cornered = 1; else e->phase = (uint8_t)((e->phase - 1) & 3); }
        else                    { if (TALL(e, 0)) at_range = 1; else e->phase = (uint8_t)((e->phase + 1) & 3); }
    } else {
        uint8_t dist = (uint8_t)(e->rcol - 0x11);                       /* A42E */
        if (dist == dist_l) at_range = 1;
        else if (dist < dist_l) { if (TALL(e, 0)) cornered = 1; else e->phase = (uint8_t)((e->phase - 1) & 3); }
        else                    { if (TALL(e, 4)) at_range = 1; else e->phase = (uint8_t)((e->phase + 1) & 3); }
    }
    if (at_range) {                                                     /* A459 */
        dist_r = (uint8_t)((krn_random(g) & 3) + 7);
        dist_l = (uint8_t)((krn_random(g) & 3) + 7);
        ai_hero_dir(g, e, 5, &facing);
        if (facing) { e->next |= 1; e->phase = 4; }                     /* A47D */
        ai_tall_sync(g, e); return;
    }
    if (cornered) {                                                     /* A488 */
        if (krn_random(g) & 1) return;
        e->next |= 3; e->phase = 4;
    }
    ai_tall_sync(g, e);
}

/* 0xA6D8  Blue slime: 8 idle frames, then 8 move frames with one step on the
 * 8th (never off a ledge), then 4 more — 1 cell per 20 frames. */
static void slime_update(Game *g, MapObj *e)
{
    if (!e->hp) e->hp = 4;                                              /* A6D8 */
    if (e->hit & HIT_STUN) { enemy_take_damage(g, e); return; }         /* A6E2 */
    if (!STEP(e, 6)) return;                                             /* A6ED */
    if (!(e->next & 1)) {                                               /* A6F5 */
        e->phase = (uint8_t)((e->phase + 1) & 7);
        if (!e->phase) { e->next = (uint8_t)((e->next | 1) & ~2); e->link = 0; }   /* A705 */
        return;
    }
    e->phase = (uint8_t)((e->link & 3) + 8);                            /* A718 */
    e->link++;
    if (!(e->next & 2)) {
        if (e->link != 8) return;                                       /* A725 */
        e->next |= 2;
        int p = game_ring_index(g, e->row, e->rcol);
        if (!(krn_random(g) & 0x80)) {                                  /* A730 */
            uint8_t v = g->ring[game_ring_add(p, 2 * RING_W + 2)];      /* (row+2, rcol+2) */
            if (ai_cell_passable(g, v)) STEP(e, 4); else STEP(e, 0);
        } else {
            uint8_t v = g->ring[game_ring_add(p, 2 * RING_W - 1)];      /* (row+2, rcol-1) */
            if (ai_cell_passable(g, v)) STEP(e, 0); else STEP(e, 4);
        }
        return;
    }
    if (e->link == 12) { e->next &= (uint8_t)~1; e->phase = 0; }        /* A794 */
}

/* 0xA871 */
static void redfrog_spit(Game *g, MapObj *e)
{
    e->link++;
    e->phase = (uint8_t)((e->phase + 1) & 1);
    if (e->link != 4) return;
    e->phase = 7;                                                       /* A882 */
    int right = (e->hit & FACING_RIGHT) != 0;
    Shot t; memset(&t, 0, sizeof t);
    t.cell = 0x9E; t.life = 6; t.damage = 20; t.flags = (uint8_t)(right ? 0 : 4);
    t.col = (uint8_t)(e->rcol + (right ? 1 : 0));
    t.row = (uint8_t)((e->row + 1) & 0x3F);
    shot_spawn(g, &t);                                                  /* A8AA */
    e->next = (uint8_t)((e->next & ~4) | 2); e->link = 0;
}

/* 0xA82B  the eai1 frog hop */
static void redfrog_hop(Game *g, MapObj *e)
{
    static const uint8_t hop_r[4] = {1, 0, 0, 7}, hop_l[4] = {3, 4, 4, 5};   /* A8EC / A8F0 */
    int facing;
    uint8_t old = e->phase, n = (uint8_t)((old + 1) & 7);
    if (n < 7) {
        e->phase = (uint8_t)((old & 0xF0) | n);
        int k = (old & 7) - 2; if (k < 0) k = 0; if (k > 3) k = 3;
        if (!ai_step_dir(g, e, ((e->hit & FACING_RIGHT) ? hop_r : hop_l)[k])) return;
        ai_hero_dir(g, e, 5, &facing);
        if (!facing) e->hit ^= FACING_RIGHT;                            /* A85B */
    }
    e->next &= (uint8_t)~8; e->phase = 0; STEP(e, 6);                   /* A864 */
}

/* 0xA7A4  Red frog: hop or spit, decided by a coin whenever it would hop. */
static void redfrog_update(Game *g, MapObj *e)
{
    int facing;
    if (ai_on_hazard(g, e)) { enemy_killed(g, e); return; }             /* A7A4 */
    if (!e->hp) e->hp = 2;
    if (e->hit & HIT_STUN) { enemy_take_damage(g, e); return; }         /* A7BA */
    if (e->next & 2) {                                                  /* A8BC: recovering */
        e->link++;
        e->phase = (uint8_t)((e->phase + 1) & 1);
        if (e->link == 6) e->next &= (uint8_t)~2;
        return;
    }
    if (e->next & 4) { redfrog_spit(g, e); return; }                    /* A871 */
    if (e->next & 8) { redfrog_hop(g, e); return; }                     /* A82B */
    e->phase = (uint8_t)((e->phase + 0x21) & 0xE1);                     /* A7DD */
    if (!STEP(e, 6)) return;                                             /* A7E5 */
    uint8_t f = ai_hero_dir(g, e, 5, &facing);                          /* A7ED */
    if (!facing) {
        if (e->phase & 0xE0) return;                                    /* A7F2 */
        f = ai_hero_dir(g, e, 5, &facing);
        if (f != 0xFF) { e->hit = (uint8_t)((e->hit & 0x7F) | f); e->phase = 2; e->next |= 8; }
        return;
    }
    if (krn_random(g) & 1) { e->phase = 2; e->next |= 8; }              /* A811 */
    else                   { e->next |= 4; e->link = 0; }               /* A81A */
}

/* 0xA923  the eai1 bat, but an idle bird first tries one step up each frame. */
static void bird_update(Game *g, MapObj *e)
{
    if (ai_on_hazard(g, e)) { enemy_killed(g, e); return; }             /* A923 */
    if (!e->hp) e->hp = 3;                                              /* A92F */
    if (e->hit & HIT_STUN) { enemy_take_damage(g, e); return; }         /* A939 */
    switch ((e->next >> 6) & 3) {                                       /* A944 */
    case 0: STEP(e, 2); eai1_bat_idle(g, e); break;                     /* A95E */
    case 1: eai1_bat_wake(g, e);    break;
    case 2: eai1_bat_chase(g, e);   break;
    default: eai1_bat_retreat(g, e); break;
    }
}

/* 0xA369 (dispatch A377); class 1 is the lower half of the tall plant. */
void eai2_entry(Game *g, MapObj *e)
{
    switch (e->type & 0xF) {
    case 0: plant_update(g, e);   break;
    case 1: break;                                                      /* A383: ret */
    case 2: slime_update(g, e);   break;
    case 3: redfrog_update(g, e); break;
    case 4: case 5: bird_update(g, e); break;
    default: break;
    }
}
