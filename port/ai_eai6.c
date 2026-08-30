/* ai_eai6.c — cavern 6 AI (EAI6.BIN), ported from src/ai/eai6.c.
 * class 0/1 tall ghost    HP 48 contact 80 EXP 100 (only the sword hurts it)
 * class 2   flying fish   HP 16 contact 40 EXP 50
 * class 3   charging beast HP 8 contact 40 EXP 50
 * class 4   falling rock  sword-immune, contact 80, EXP 0 */
#include "enemy.h"
#include <string.h>

#define STEP(o, d) ai_step(g, (o), (d))
#define TALL(o, d) ai_tall_step(g, (o), (d))

static const uint8_t path_r[8] = {0, 0, 1, 0, 0, 0, 7, 0};              /* A75E */
static const uint8_t path_l[8] = {4, 4, 3, 4, 4, 4, 5, 4};              /* A766 */

/* 0xA508  the lower record follows the upper one's frame, facing and type bits */
static void ghost_sync(Game *g, MapObj *e)
{
    (void)g;
    e[1].phase = e->phase;
    e[1].type = (uint8_t)((e[1].type & 0x9F) | (e->type & 0x60));
    e[1].hit = (uint8_t)((e->hit & 0x80) | (e[1].hit & 0x7F));
}

/* 0xA412 */
static void ghost_update(Game *g, MapObj *e)
{
    int facing;
    if (!e->hp) e->hp = 0x30;
    if (e->hit & HIT_STUN) {
        if ((e->hit & 0x1F) == 1) {                                     /* A4F7: only the sword */
            e->hit = 0x21; e[1].hit = 0x61;
            enemy_take_damage(g, e);
            return;
        }
        e->hit &= 0x9F;                                                 /* A42E: magic is ignored */
    }
    e[1].hit &= (uint8_t)~0x40;                                         /* A432 */
    if (TALL(e, 6)) return;                                             /* A436 */
    if (e->next & 1) {                                                  /* A48D */
        e->phase = (uint8_t)((e->phase + 1) & 0xF);
        if (!e->phase) { e->next &= (uint8_t)~1; e->phase = 1; e->type |= 0x60; }   /* A496 */
        else if (e->phase >= 4) {
            e->type &= 0x1F;                                            /* A4AA */
            if (e->phase == 8) {                                        /* A4AE */
                int right = (e->hit & FACING_RIGHT) != 0;
                Shot s; memset(&s, 0, sizeof s);
                s.cell = 0x63; s.life = 20; s.damage = 20; s.flags = (uint8_t)(right ? 0 : 4);
                s.col = (uint8_t)(e->rcol + (right ? 1 : 0));
                s.row = (uint8_t)((e->row + 1) & 0x3F);
                shot_spawn(g, &s);
            }
        }
        ghost_sync(g, e); return;
    }
    ai_hero_dir(g, e, 4, &facing);                                      /* A442 */
    if (facing && (e->link & 0xF0)) {                                   /* A479 */
        e->link = 0; e->phase = 0; e->next |= 1; ghost_sync(g, e); return;
    }
    e->link++; e->phase = 1; e->type |= 0x60;                           /* A447 */
    if (krn_random(g) & 1) { if (!TALL(e, 4)) e->hit &= (uint8_t)~FACING_RIGHT; }   /* A46A */
    else                   { if (!TALL(e, 0)) e->hit |= FACING_RIGHT; }             /* A45B */
    ghost_sync(g, e);
}

/* 0xA78D  Flee for 15 frames, away from the hero, then recover. */
static void fish_flee(Game *g, MapObj *e)
{
    int facing;
    if (++e->link >= 15) { e->phase = 0xC; e->next = 4; return; }       /* A790 */
    uint8_t f = ai_hero_dir(g, e, 8, &facing);
    int move_only = 0;
    if (facing) {
        if (!(e->next & 0x70))
            e->hit = (uint8_t)((e->hit & 0x7F) | (f == 0xFF ? ((krn_random(g) << 1) & 0x80) : (uint8_t)(f ^ 0x80)));   /* A7A5 */
        else move_only = 1;
    }
    if (!move_only) {
        if ((int8_t)(uint8_t)(g->hero_map_row - e->row) < 0) STEP(e, 6); else STEP(e, 2);   /* A7C2 */
    }
    e->phase = (uint8_t)(((e->phase + 1) & 3) | 8);
    e->next = (uint8_t)(e->next + 0x10);
    if (ai_step_dir(g, e, ((e->hit & FACING_RIGHT) ? path_r : path_l)[(e->next >> 4) & 7])) e->hit ^= FACING_RIGHT;
}

/* 0xA6B8  Flying fish: no gravity, roams on the 8-entry path table. */
static void fish_update(Game *g, MapObj *e)
{
    int facing;
    if (!e->hp) e->hp = 16;
    if (e->hit & HIT_STUN) { e->phase = 3; e->next = 1; enemy_take_damage(g, e); return; }   /* A6C8 */
    if (e->next & 2) { fish_flee(g, e); return; }                       /* A78D */
    if (e->next & 1) {                                                  /* A76E */
        e->type |= 0x60;
        e->phase = (uint8_t)((e->phase + 1) & 7);
        if (e->phase >= 7) { e->phase = 8; e->link = 0; e->next = 2; }
        return;
    }
    if (e->next & 4) {                                                  /* A815 */
        e->phase = (uint8_t)((e->phase + 1) & 0xF);
        if (!e->phase) { e->next = 0; e->type &= 0x1F; }
        return;
    }
    uint8_t f = ai_hero_dir(g, e, 8, &facing);                          /* A6F0 */
    int move_only = 0;
    if (!facing) {
        if (e->next & 0x70) move_only = 1;
        else {
            if (f != 0xFF) e->hit = (uint8_t)((e->hit & 0x7F) | f);
            else e->hit = (uint8_t)((e->hit & 0x7F) | ((krn_random(g) << 1) & 0x80));
        }
    }
    if (!move_only) {
        if ((int8_t)(uint8_t)(g->hero_map_row - e->row) < 0) STEP(e, 2); else STEP(e, 6);   /* A718 */
    }
    e->phase = (uint8_t)((e->phase + 1) & 3);                           /* A72C */
    e->next = (uint8_t)(e->next + 0x10);
    if (ai_step_dir(g, e, ((e->hit & FACING_RIGHT) ? path_r : path_l)[(e->next >> 4) & 7])) e->hit ^= FACING_RIGHT;
}

/* 0xA927 */
static void beast_bounce(MapObj *e) { e->next |= 2; e->hit ^= FACING_RIGHT; e->phase = 5; }
/* 0xA947 */
static int beast_climb(Game *g, MapObj *e)
{
    if (!(e->next & 4)) return !STEP(e, 6);
    if (STEP(e, 2)) return 0;
    e->next |= 4;
    return 1;
}

/* 0xA857  Charging beast. */
static void beast_update(Game *g, MapObj *e)
{
    int facing;
    if (!e->hp) e->hp = 8;
    if (e->hit & HIT_STUN) { enemy_take_damage(g, e); return; }
    if (!(e->next & 1)) {                                               /* A872 */
        if (!STEP(e, 6)) return;
        ai_hero_dir(g, e, 8, &facing);
        if (facing) { e->next = 1; e->link = 0; return; }               /* A8BA */
        unsigned t = (unsigned)e->phase + 0x80; e->phase = (uint8_t)t;
        if (!(t & 0x100)) return;
        e->phase = (uint8_t)((e->phase + 1) & 0xF3);
        if ((e->hit & FACING_RIGHT) ? STEP(e, 0) : STEP(e, 4)) e->hit ^= FACING_RIGHT;   /* A893 */
        if (!(--e->link & 0xF)) e->hit ^= FACING_RIGHT;                 /* A8AB */
        return;
    }
    if (e->next & 2) {                                                  /* A934 */
        if (!(++e->phase & 7)) { e->next &= (uint8_t)~2; e->phase = 4; }
        return;
    }
    if (ai_hero_dir(g, e, 8, &facing) == 0xFF) goto recheck;            /* A8C9 */
    e->phase = 4;
    if (e->hit & FACING_RIGHT) { STEP(e, 0); if (STEP(e, 0) && beast_climb(g, e)) { beast_bounce(e); return; } }
    else                       { STEP(e, 4); if (STEP(e, 4) && beast_climb(g, e)) { beast_bounce(e); return; } }
    e->link++;                                                          /* A8FE */
    if ((e->link & 0xF) == 0xF) beast_bounce(e);
    if (e->link & 0x1F) return;
recheck:
    ai_hero_dir(g, e, 8, &facing);                                      /* A914 */
    if (!facing) { e->phase = 0; e->next = 0; e->link = 0; }
}

/* 0xA95F  Falling rock: the cavern-4 icicle, with sound 0x21 and a crumble. */
static void rock_update(Game *g, MapObj *e)
{
    e->type |= 0x20;
    if (e->next & 2) {                                                  /* A9B4 */
        unsigned t = (unsigned)e->phase + 0x80; e->phase = (uint8_t)t;
        if (!(t & 0x100)) return;
        e->phase = (uint8_t)((e->phase + 1) & 3);
        if (!e->phase) { e->flags = (uint8_t)((e->flags & 0xF0) | 1); enemy_killed(g, e); }
        return;
    }
    if (e->next & 1) {                                                  /* A98C */
        if (!STEP(e, 6)) return;
        e->next |= 2; e->phase = 1;
        if ((uint8_t)((e->row - (g->scroll_row - 1)) & 0x3F) < 0x13) g->sfx_request = 0x21;   /* A99C */
        return;
    }
    if (e->rcol < 8 || e->rcol >= 0x13) return;                         /* A96F */
    if (!(krn_random(g) & 3)) e->next |= 1;                             /* A97D */
}

/* 0xA3F9 (dispatch A407) */
void eai6_entry(Game *g, MapObj *e)
{
    switch (e->type & 0xF) {
    case 0: ghost_update(g, e); break;
    case 1: break;                                                      /* A411: ret */
    case 2: fish_update(g, e);  break;
    case 3: beast_update(g, e); break;
    case 4: rock_update(g, e);  break;
    default: break;
    }
}
