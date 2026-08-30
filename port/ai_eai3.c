/* ai_eai3.c — cavern 3 AI (EAI3.BIN), ported from src/ai/eai3.c.
 * class 0 ceiling spider HP 2 contact 40 EXP 20
 * class 1 red hopper     HP 2 contact 40 EXP 10
 * class 2 burrowing snake HP 4 contact 16 EXP 10
 * class 3 charging beetle HP 4 contact 40 EXP 20 */
#include "enemy.h"
#include <string.h>

#define STEP(o, d) ai_step(g, (o), (d))
#define PROBE(o, d) ai_probe(g, (o), (d))

/* 0xA2F9  Ceiling crawl: one step every 2nd frame; drops when nothing is
 * above, when its column is 0x12..0x14, or on the second wall bump. */
static void spider_crawl(Game *g, MapObj *e)
{
    e->phase = (uint8_t)((e->phase + 1) & 7);                           /* A2F9 */
    int drop = 0;
    if (PROBE(e, 2)) drop = 1;                                          /* A300: nothing above */
    else if (e->phase & 1) {
        if (e->rcol >= 0x12 && e->rcol < 0x15) drop = 1;                /* A30E */
        else if ((e->hit & FACING_RIGHT) ? STEP(e, 0) : STEP(e, 4)) {   /* A31F */
            uint8_t n = e->link; e->link = 0; e->hit ^= FACING_RIGHT;   /* A327 */
            if (!(n & 1)) drop = 1;
        }
    }
    if (drop) { e->next = 1; e->phase = 8; }                            /* A34D */
}

/* 0xA40E  Zig-zag straight up until a ceiling is directly above. */
static void spider_climb(Game *g, MapObj *e)
{
    e->phase = 8;
    if (!(e->hit & FACING_RIGHT)) {
        if (!STEP(e, 3)) return;                                        /* A418 */
        if (!PROBE(e, 2)) { e->next = 0; e->phase = 0; e->link = 1; return; }   /* A420 */
    } else {
        if (!STEP(e, 1)) return;                                        /* A439 */
        if (!STEP(e, 2)) { e->next = 0; e->phase = 0; e->link = 1; return; }    /* A441 */
    }
    e->hit ^= FACING_RIGHT;
}

/* 0xA2C8 */
static void spider_update(Game *g, MapObj *e)
{
    if (!e->hp) e->hp = 2;
    if (e->hit & HIT_STUN) { enemy_take_damage(g, e); return; }         /* A2D2 */
    switch (e->next & 7) {                                              /* A2DD */
    case 0: spider_crawl(g, e); break;
    case 1: if (!STEP(e, 6)) break; e->next = 2; e->phase = 9; break;   /* A356 */
    case 2: e->next = 3; e->phase = 0xA; e->link = 0; break;            /* A367 */
    case 3:                                                             /* A374 */
        if (e->link == 1) { e->next = 4; e->link = 0xFF; }
        e->phase = 0xB; e->link++;
        if ((e->hit & FACING_RIGHT) ? STEP(e, 1) : STEP(e, 3)) e->hit ^= FACING_RIGHT;
        break;
    case 4:                                                             /* A3AC */
        if (e->link == 1) e->next = 5;
        e->phase = 8; e->link++;
        if ((e->hit & FACING_RIGHT) ? STEP(e, 0) : STEP(e, 4)) e->hit ^= FACING_RIGHT;
        break;
    case 5:                                                             /* A3E0 */
        e->phase = 8;
        if ((e->hit & FACING_RIGHT) ? STEP(e, 7) : STEP(e, 5)) { e->phase = 9; e->next = 6; }
        break;
    case 6: e->phase = 0xA; e->next = 7; break;                         /* A405 */
    default: spider_climb(g, e); break;                                 /* A40E */
    }
}

/* 0xA498 */
static void crab_hop(Game *g, MapObj *e)
{
    static const uint8_t hop_r[6] = {1,1,0,0,7,7}, hop_l[6] = {3,3,4,4,5,5};   /* A4E4 / A4EA */
    e->next &= (uint8_t)~4;
    uint8_t old = e->phase;
    e->phase = (uint8_t)((e->phase + 1) & 7);
    if (e->phase >= 6) { e->phase = 0; e->next &= (uint8_t)~8; }        /* A4AC */
    int k = old & 7; if (k > 5) k = 5;
    if (!ai_step_dir(g, e, ((e->hit & FACING_RIGHT) ? hop_r : hop_l)[k])) return;   /* A4C1 */
    e->next &= (uint8_t)~8;                                             /* A4C9 */
    if (e->phase == 1) { e->next |= 4; e->hit ^= FACING_RIGHT; }
    e->phase = 0; STEP(e, 6);
}

/* 0xA44D  Red hopper: an unbroken arc toward the hero. */
static void crab_update(Game *g, MapObj *e)
{
    if (!e->hp) e->hp = 2;
    if (e->hit & HIT_STUN) { enemy_take_damage(g, e); return; }         /* A457 */
    if (e->next & 8) { crab_hop(g, e); return; }                        /* A462 */
    if (!(e->next & 4)) {                                               /* A46E */
        e->hit |= FACING_RIGHT;
        if (e->rcol >= 0x11) e->hit ^= FACING_RIGHT;
    }
    if (!STEP(e, 6)) return;                                             /* A47C */
    e->phase &= 0xF0;
    unsigned t = (unsigned)e->phase + 0x80; e->phase = (uint8_t)t;      /* A488 */
    if (t & 0x100) { e->phase = 0; e->next |= 8; }
}

/* 0xA4F0  Burrowing snake: crawls buried (immune), surfaces, spits, sinks. */
static void snake_update(Game *g, MapObj *e)
{
    int facing;
    if (!e->hp) e->hp = 4;
    if (e->hit & HIT_STUN) { enemy_take_damage(g, e); return; }         /* A4FA */
    if (!STEP(e, 6)) return;                                             /* A505 */
    int p = game_ring_index(g, e->row, e->rcol);
    switch (e->next & 3) {
    case 0: {                                                           /* A521 */
        e->type |= 0x60;
        unsigned t = (unsigned)e->phase + 0x80; e->phase = (uint8_t)t;
        if (!(t & 0x100)) return;
        e->phase = (uint8_t)((e->phase + 1) & 1);
        if (e->phase) return;
        if (++e->link >= 7) { e->next = 1; e->phase = 2; }              /* A536 */
        if (e->hit & FACING_RIGHT) {                                    /* A547 */
            if (ai_cell_passable(g, g->ring[game_ring_add(p, 2 * RING_W + 2)])) { e->hit &= (uint8_t)~FACING_RIGHT; STEP(e, 4); }
            else STEP(e, 0);
        } else {
            if (ai_cell_passable(g, g->ring[game_ring_add(p, 2 * RING_W - 1)])) { e->hit |= FACING_RIGHT; STEP(e, 0); }
            else STEP(e, 4);
        }
        return; }
    case 1:                                                             /* A5A3 */
        e->type &= 0x1F;
        if (++e->phase == 5) { e->next = 2; e->link = 0; }
        return;
    case 2: {                                                           /* A5BA */
        if (e->next & 0x80) { e->next = 3; e->phase = 5; return; }
        unsigned t = (unsigned)e->phase + 0x40; e->phase = (uint8_t)t;
        if (!(t & 0x100)) return;
        e->hit ^= FACING_RIGHT;                                         /* A5C7 */
        ai_hero_dir(g, e, 5, &facing);
        if (!facing) { if (++e->link == 3) { e->next = 3; e->phase = 5; } return; }
        e->phase = 6; e->next |= 0x80;                                  /* A5E3 */
        int right = (e->hit & FACING_RIGHT) != 0;
        Shot s; memset(&s, 0, sizeof s);
        s.cell = 0x2B; s.life = 15; s.damage = 40; s.flags = (uint8_t)(right ? 0 : 4);
        s.col = (uint8_t)(e->rcol + (right ? 1 : 0));
        s.row = (uint8_t)(e->row & 0x3F);
        shot_spawn(g, &s);
        return; }
    default:                                                            /* A612 */
        if (--e->phase == 1) { e->next = 0; e->link = 0; }
        return;
    }
}

/* 0xA66E  Charging beetle: idles until the hero is within 6 rows and 10
 * columns, then runs for 20 frames, climbing 1-cell steps. */
static void beetle_update(Game *g, MapObj *e)
{
    int facing; uint8_t dx;
    if (!e->hp) e->hp = 4;
    if (e->hit & HIT_STUN) { enemy_take_damage(g, e); return; }         /* A678 */
    uint8_t ph = e->phase; e->phase = 0;
    if (!STEP(e, 6)) return;                                             /* A68B */
    e->phase = ph;
    if (!(e->next & 1)) {                                               /* A697 */
        e->phase = 1; e->link = 0;
        uint8_t f = ai_hero_dirx(g, e, 6, &facing, &dx);                /* A701 */
        if (!facing) { if (f != 0xFF) e->hit = (uint8_t)((e->hit & 0x7F) | f); return; }
        if (dx < 10) e->next |= 1;                                      /* A6B7 */
        return;
    }
    if (++e->link == 20) { e->next &= (uint8_t)~1; return; }            /* A6C2 */
    if (e->hit & FACING_RIGHT) { if (STEP(e, 0) && STEP(e, 1)) { e->next &= (uint8_t)~1; return; } }
    else                       { if (STEP(e, 4) && STEP(e, 3)) { e->next &= (uint8_t)~1; return; } }
    if (++e->phase >= 6) e->phase = 1;                                  /* A6F2 */
}

/* 0xA2B2 (dispatch A2C0) */
void eai3_entry(Game *g, MapObj *e)
{
    switch (e->type & 0xF) {
    case 0: spider_update(g, e); break;
    case 1: crab_update(g, e);   break;
    case 2: snake_update(g, e);  break;
    case 3: beetle_update(g, e); break;
    default: break;
    }
}
