/* ai_eai5.c — cavern 5 (water/currents) AI (EAI5.BIN), from src/ai/eai5.c.
 * class 0/1 tall spitter    HP 24 contact 40 EXP 50
 * class 2   dividing slime  HP 16 contact 20 EXP 20
 * class 3   turning charger HP 8  contact 20 EXP 10
 * class 4   diving floater  HP 8  contact 10 EXP 10
 * Classes 2-4 call vec 32 every update, so currents sweep them 2 cells/frame. */
#include "enemy.h"
#include <string.h>

#define STEP(o, d) ai_step(g, (o), (d))
#define TALL(o, d) ai_tall_step(g, (o), (d))

/* 0xA350  The tall spitter.  Only the upper record takes hits (A363). */
static void tallspit_update(Game *g, MapObj *e)
{
    int facing;
    if (!e->hp) e->hp = 0x18;
    if (e->hit & HIT_STUN) { ai_tall_take_hit_own(g, e); return; }      /* A35A -> A435 */
    e[1].hit &= (uint8_t)~0x40;                                         /* A363 */
    if (TALL(e, 6)) return;                                             /* A367 */
    if (e->next & 1) {                                                  /* A3D2 */
        unsigned t = (unsigned)e->phase + 0x80; e->phase = (uint8_t)t;
        if (t & 0x100) {
            e->phase++;
            if ((e->phase & 0xF) == 0xB) {                              /* A3F2 */
                int right = (e->hit & FACING_RIGHT) != 0;
                Shot s; memset(&s, 0, sizeof s);
                s.cell = 0xB1; s.life = (uint8_t)(right ? 18 : 20); s.damage = 40;
                s.flags = (uint8_t)(right ? 0 : 4);
                s.col = (uint8_t)(e->rcol + (right ? 1 : 0));
                s.row = (uint8_t)((e->row + 1) & 0x3F);
                shot_spawn(g, &s);
            } else if ((e->phase & 0xF) == 0xC) { e->next &= (uint8_t)~1; e->phase = 3; }
        }
        ai_tall_sync(g, e); return;
    }
    ai_hero_dir(g, e, 4, &facing);                                      /* A373 */
    if (facing) {
        if (!(krn_random(g) & 0xC0) && (e->phase & 3) == 3) {           /* A3B6 */
            e->next |= 1; e->phase = 8; ai_tall_sync(g, e); return;
        }
    } else {
        unsigned t = (unsigned)e->phase + 0x80; e->phase = (uint8_t)t;  /* A378 */
        if (!(t & 0x100)) { ai_tall_sync(g, e); return; }
    }
    e->phase = (uint8_t)((e->phase + 1) & 7);                           /* A381 */
    if (!(e->phase & 3)) {
        if (e->rcol <= 0x10) { if (!TALL(e, 0)) e->hit |= FACING_RIGHT; }   /* A391 */
        else                 { if (!TALL(e, 4)) e->hit &= (uint8_t)~FACING_RIGHT; }
    }
    ai_tall_sync(g, e);
}

/* 0xA641  The copy starts immune and harmless and grows through frames 4..7. */
static void slime_split(Game *g, MapObj *e)
{
    int idx = ai_find_spare(g);
    if (idx < 0) return;
    MapObj *s = &g->obj[idx];
    int i = (int)(e - g->obj);
    int p = game_ring_index(g, e->row, e->rcol);
    int dst;
    if (!(e->hit & FACING_RIGHT)) {
        if (e->rcol >= 0x20) return;
        for (int r = -1; r <= 1; r++)
            for (int c = 2; c <= 3; c++)
                if (!ai_cell_passable(g, g->ring[game_ring_add(p, r * RING_W + c)])) return;
        dst = game_ring_add(p, 2);
        s->col = (uint16_t)((e->col + 2) % g->map->width); s->rcol = (uint8_t)(e->rcol + 2);
    } else {
        if (e->rcol < 4) return;
        for (int r = -1; r <= 1; r++)
            for (int c = -2; c >= -1; c--)
                if (!ai_cell_passable(g, g->ring[game_ring_add(p, r * RING_W + c)])) return;
        dst = game_ring_add(p, -2);
        s->col = (uint16_t)((e->col + g->map->width - 2) % g->map->width); s->rcol = (uint8_t)(e->rcol - 2);
    }
    g->under_sprite[idx] = g->ring[dst];
    g->ring[dst] = (uint8_t)(0x80 | idx);
    s->type = (uint8_t)((e->type & 0x1F) | 0x60);
    s->hit = (uint8_t)(e->hit & 0x80);
    s->row = e->row; s->phase = 4; s->flags = e->flags; s->hp = 0; s->link = 0;
    s->next = (uint8_t)(2 | (i < idx ? 1 : 0));
}

/* 0xA5F1 */
static void slime_update(Game *g, MapObj *e)
{
    if (!e->hp) e->hp = 16;
    if (e->hit & HIT_STUN) {
        uint8_t src = (uint8_t)(e->hit & 0x1F);
        if (src == 4 || src == 5 || src == 8 || (src == 1 && g->sword == 6)) { enemy_take_damage(g, e); return; }   /* A604 */
        e->hit &= (uint8_t)~HIT_STUN;
        if (!(e->next & 2)) slime_split(g, e);                          /* A638 */
    }
    if (ai_ride_current(g, e)) return;                                  /* A780: vec 32 */
    int skip = e->next & 1;
    e->next &= (uint8_t)~1;
    if (skip) return;
    if (e->next & 2) {                                                  /* A7FF */
        e->phase = (uint8_t)((e->phase + 1) & 7);
        if (!e->phase) { e->next &= (uint8_t)~2; e->type &= 0x9F; }
        return;
    }
    e->phase = (uint8_t)((e->phase + 1) & 0xF3);                        /* A797 */
    if (!STEP(e, 6)) return;
    e->phase = (uint8_t)(e->phase - 0x10);
    if (e->phase & 0xF0) return;                                        /* A7A9 */
    e->phase |= 0x40;
    int level = (g->hero_map_row == e->row) || (((g->hero_map_row + 1) & 0x3F) == e->row);
    if (level ? (e->rcol > 0x11) : !(e->hit & FACING_RIGHT)) {
        e->hit &= (uint8_t)~FACING_RIGHT;
        if (!STEP(e, 4)) return;
    }
    e->hit |= FACING_RIGHT;
    if (!STEP(e, 0)) return;
    e->hit &= (uint8_t)~FACING_RIGHT; STEP(e, 4);
}

/* 0xA812  Turning charger: walks, looks around, then charges 2 cells/frame. */
static void charger_update(Game *g, MapObj *e)
{
    int facing;
    if (!e->hp) e->hp = 8;
    if (e->hit & HIT_STUN) { enemy_take_damage(g, e); return; }
    if (ai_ride_current(g, e)) return;                                  /* A827 */
    int walk_step = 0;
    if (e->next & 4) {                                                  /* A8DB */
        if (++e->link < 5) walk_step = 1;
        else {
            e->phase = 5;
            if (e->hit & FACING_RIGHT) { STEP(e, 0); if (!STEP(e, 0)) return; e->next = 2; e->phase = 4; }
            else                       { STEP(e, 4); if (!STEP(e, 4)) return; e->next = 2; e->phase = 0; }
            return;
        }
    }
    if (!walk_step) {
        if (!STEP(e, 6)) return;                                         /* A835 */
        if (e->next & 2) {                                              /* A843 */
            uint8_t f = (uint8_t)(e->phase & 7);
            if (f == 0) e->next &= (uint8_t)~1;
            if (f == 4) e->next |= 1;
            if (e->next & 1) e->phase--; else e->phase++;
            f = (uint8_t)(e->phase & 7);
            if (f == 0) e->hit &= (uint8_t)~FACING_RIGHT;
            else if (f == 4) e->hit |= FACING_RIGHT;
            else return;
            ai_hero_dir(g, e, 4, &facing);                              /* A87A */
            if (facing) { e->next = 4; e->link = 0; return; }
            if (krn_random(g) & 0x80) { e->next = 0; e->link = 0; }
            return;
        }
        uint8_t r = ai_hero_dir(g, e, 4, &facing);                      /* A89B */
        if (facing) { e->next = 4; e->link = 0; return; }
        e->link++;
        if (!(r & 7)) e->next = 2;
        unsigned t = (unsigned)e->phase + 0x80; e->phase = (uint8_t)t;  /* A8B4 */
        if (!(t & 0x100)) return;
    }
    if (e->hit & FACING_RIGHT) { if (STEP(e, 0)) e->next = 2; }         /* A8CE */
    else                       { if (STEP(e, 4)) e->next = 2; }
}

/* 0xA91A  Diving floater: floats up, dives over the hero, rises again. */
static void diver_update(Game *g, MapObj *e)
{
    if (!e->hp) e->hp = 8;
    if (e->hit & HIT_STUN) { enemy_take_damage(g, e); return; }
    if (ai_ride_current(g, e)) return;                                  /* A92F */
    if (e->next & 1) {                                                  /* A993 */
        if ((e->phase & 7) < 5) { e->phase++; return; }
        STEP(e, 6); if (!STEP(e, 6)) return;
        e->phase = (uint8_t)((e->phase + 1) & 7);
        if (!e->phase) e->next = 2;                                     /* A9AD */
        return;
    }
    if (e->next & 2) {                                                  /* A9BC */
        STEP(e, 2);
        if (!((e->rcol <= 0x10) ? STEP(e, 1) : STEP(e, 3))) return;
        if (!STEP(e, 2)) return;
        e->next &= (uint8_t)~2; return;
    }
    if (e->rcol > 0xF && e->rcol <= 0x12) { e->next |= 1; e->phase = 4; }   /* A940 */
    else e->phase = (uint8_t)((e->phase & 0xF0) | ((e->phase + 1) & 3));    /* A958 */
    STEP(e, 2);                                                         /* A966 */
    unsigned t = (unsigned)e->phase + 0x80; e->phase = (uint8_t)t;
    if (!(t & 0x100)) return;
    if (e->rcol <= 0x10) { if (STEP(e, 0)) STEP(e, 4); }                /* A972 */
    else                 { if (STEP(e, 4)) STEP(e, 0); }
}

/* 0xA337 (dispatch A345) */
void eai5_entry(Game *g, MapObj *e)
{
    switch (e->type & 0xF) {
    case 0: tallspit_update(g, e); break;
    case 1: break;                                                      /* A34F: ret */
    case 2: slime_update(g, e);    break;
    case 3: charger_update(g, e);  break;
    case 4: diver_update(g, e);    break;
    default: break;
    }
}
