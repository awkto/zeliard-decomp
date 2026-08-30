/* ai_eai8.c — cavern 8 AI (EAI8.BIN), ported from src/ai/eai8.c.  Every class
 * gives 255 EXP.
 * class 0/1 tall charger  HP 100 contact 160 (never falls)
 * class 2   walker        HP 48  contact 60
 * class 3   sentry gunner HP 64  contact 80
 * class 4   flying chaser HP 96  contact 80 */
#include "enemy.h"
#include <string.h>

#define STEP(o, d) ai_step(g, (o), (d))
#define TALL(o, d) ai_tall_step(g, (o), (d))

/* 0xA343 */
static void anim6(MapObj *e) { if (++e->phase >= 6) e->phase = 0; }
/* 0xA680 */
static int anim3(MapObj *e) { e->phase = (uint8_t)((e->phase + 1) % 3); return e->phase == 0; }

/* 0xA2D2  Tall charger: never falls; charges 16 cells when the hero is within
 * 5 rows and 14 columns. */
static void tallcharger_update(Game *g, MapObj *e)
{
    int facing; uint8_t dx;
    if (!e->hp) e->hp = 100;
    if (e->hit & HIT_STUN) { ai_tall_take_hit_own(g, e); return; }      /* A365 */
    e[1].hit &= (uint8_t)~0x40;
    if (e->next & 1) {                                                  /* A319 */
        if (++e->link == 16) { e->next &= (uint8_t)~1; ai_tall_sync(g, e); return; }
        if ((e->hit & FACING_RIGHT) ? TALL(e, 0) : TALL(e, 4)) e->next &= (uint8_t)~1;
        else anim6(e);
        ai_tall_sync(g, e); return;
    }
    unsigned t = (unsigned)e->phase + 0x80; e->phase = (uint8_t)t;      /* A2EF */
    if (t & 0x100) anim6(e);
    e->link = 0;
    uint8_t f = ai_hero_dirx(g, e, 5, &facing, &dx);                    /* A2FC */
    if (facing) { if (dx < 15) e->next |= 1; }
    else if (f != 0xFF) e->hit = (uint8_t)((e->hit & 0x7F) | f);
    ai_tall_sync(g, e);
}

/* 0xA483 */
static void walker_update(Game *g, MapObj *e)
{
    int facing; uint8_t dx;
    if (!e->hp) e->hp = 0x30;
    if (e->hit & HIT_STUN) { enemy_take_damage(g, e); return; }
    if (!STEP(e, 6)) return;                                             /* A498 */
    if (!(e->next & 1)) {
        uint8_t f = ai_hero_dirx(g, e, 5, &facing, &dx);                /* A4A6 */
        e->next = (uint8_t)(facing ? 1 : 0);
        if (f != 0xFF) e->hit = (uint8_t)((e->hit & 0x7F) | f);
        unsigned t = (unsigned)e->phase + 0x80; e->phase = (uint8_t)t;  /* A4BB */
        if (!(t & 0x100)) return;
        e->phase = (uint8_t)((e->phase + 1) & 7);
        if ((e->hit & FACING_RIGHT) ? STEP(e, 0) : STEP(e, 4)) { e->next = 0; e->hit ^= FACING_RIGHT; }   /* A4E7 */
        return;
    }
    if (!(--e->link & 3)) {                                             /* A4F0 */
        uint8_t f = ai_hero_dirx(g, e, 5, &facing, &dx);
        e->next = (uint8_t)(facing ? 1 : 0);
        if (f != 0xFF) e->hit = (uint8_t)((e->hit & 0x7F) | f);
    }
    e->phase = (uint8_t)((e->phase + 1) & 7);                           /* A50E */
    if ((e->hit & FACING_RIGHT) ? STEP(e, 0) : STEP(e, 4)) e->next = 0;
}

/* 0xA538  Sentry gunner.  The left shot's damage byte really is 1 (A673). */
static void gunner_update(Game *g, MapObj *e)
{
    int facing; uint8_t dx;
    if (!e->hp) e->hp = 0x40;
    if (e->hit & HIT_STUN) { enemy_take_damage(g, e); return; }
    if (!STEP(e, 6)) return;
    if (e->next & 4) {                                                  /* A620 */
        e->phase = 3;
        if (++e->link != 3) return;
        e->phase = 4;
        int right = (e->hit & FACING_RIGHT) != 0;
        Shot s; memset(&s, 0, sizeof s);
        s.cell = (uint8_t)(right ? 0x2A : 0x2B); s.life = 18;
        s.damage = (uint8_t)(right ? 80 : 1);
        s.flags = (uint8_t)(right ? 0 : 4);
        s.col = (uint8_t)(e->rcol + (right ? 1 : 0));
        s.row = (uint8_t)(e->row & 0x3F);
        shot_spawn(g, &s);
        e->next = (uint8_t)((e->next & ~4) | 2); e->link = 0;
        return;
    }
    if (!(e->next & 1)) {                                               /* A564 */
        uint8_t f = ai_hero_dirx(g, e, 5, &facing, &dx);                /* A5FE */
        if (f != 0xFF) {
            e->hit = (uint8_t)((e->hit & 0x7F) | f);
            if (!(krn_random(g) & 7)) { e->next |= 4; e->link = 0; }
        }
        unsigned t = (unsigned)e->phase + 0x80; e->phase = (uint8_t)t;
        if (!(t & 0x100)) return;
        if (anim3(e) && !(krn_random(g) & 3)) { e->next = 1; e->link = 0; }   /* A56E */
        return;
    }
    if (e->next & 2) { e->next &= (uint8_t)~1; e->phase = 0; return; }  /* A5F5 */
    anim3(e);
    if (++e->link != 8) return;                                         /* A590 */
    e->next |= 2;
    int p = game_ring_index(g, e->row, e->rcol);
    if (!(krn_random(g) & 0x80)) {
        if (ai_cell_passable(g, g->ring[game_ring_add(p, 2 * RING_W + 2)])) STEP(e, 4); else STEP(e, 0);
    } else {
        if (ai_cell_passable(g, g->ring[game_ring_add(p, 2 * RING_W - 1)])) STEP(e, 0); else STEP(e, 4);
    }
}

/* 0xA68F  The eai6 fish's roaming logic at half speed, no flee state. */
static void flyer_update(Game *g, MapObj *e)
{
    static const uint8_t path_r[8] = {0, 0, 1, 0, 0, 0, 7, 0};          /* A71B */
    static const uint8_t path_l[8] = {4, 4, 3, 4, 4, 4, 5, 4};          /* A723 */
    int facing; uint8_t dx;
    if (!e->hp) e->hp = 0x60;
    if (e->hit & HIT_STUN) { enemy_take_damage(g, e); return; }
    e->phase = (uint8_t)((e->phase + 1) & 3);
    unsigned t = (unsigned)e->link + 0x80; e->link = (uint8_t)t;        /* A6AB */
    if (!(t & 0x100)) return;
    uint8_t f = ai_hero_dirx(g, e, 8, &facing, &dx);
    int move_only = 0;
    if (!facing) {
        if (e->next & 0x70) move_only = 1;
        else e->hit = (uint8_t)((e->hit & 0x7F) | (f != 0xFF ? f : ((krn_random(g) << 1) & 0x80)));
    }
    if (!move_only) {
        if ((int8_t)(uint8_t)(g->hero_map_row - e->row) < 0) STEP(e, 2); else STEP(e, 6);   /* A6DC */
    }
    e->next = (uint8_t)(e->next + 0x10);
    if (ai_step_dir(g, e, ((e->hit & FACING_RIGHT) ? path_r : path_l)[(e->next >> 4) & 7])) e->hit ^= FACING_RIGHT;
}

/* 0xA2B9 (dispatch A2C7) */
void eai8_entry(Game *g, MapObj *e)
{
    switch (e->type & 0xF) {
    case 0: tallcharger_update(g, e); break;
    case 1: break;                                                      /* A2D1: ret */
    case 2: walker_update(g, e);      break;
    case 3: gunner_update(g, e);      break;
    case 4: flyer_update(g, e);       break;
    default: break;
    }
}
