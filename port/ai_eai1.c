/* ai_eai1.c — the cavern-1 enemy AI (EAI1.BIN = ZELRES3[1], loaded at
 * BASE:A000).  A line-by-line port of src/ai/eai1.c; the hex tags are the
 * addresses inside the overlay image.  The data tables (EXP, contact damage,
 * drop lists, sprite frames) are *not* duplicated here: enemy.c reads them out
 * of the original overlay (docs/ENEMIES.md §2, cavern 1).
 *
 * class 0 bat      HP 2  contact 5   EXP 3
 * class 1 snail    HP 2  contact 5   EXP 2
 * class 2 frog     HP 1  contact 15  EXP 5
 * class 3 hedgehog HP 1  contact 8   EXP 3 */
#include "enemy.h"

#define FACING_RIGHT 0x80
#define HIT_STUN     0x20

/* vec-2 direction codes: 0 R, 1 RU, 2 U, 3 LU, 4 L, 5 LD, 6 D, 7 RD */
static const uint8_t hop_r[4]  = {1, 0, 0, 7};                 /* A71F  RU, R, R, RD */
static const uint8_t hop_l[4]  = {3, 4, 4, 5};                 /* A723  LU, L, L, LD */
static const uint8_t jump_r[8] = {2, 1, 1, 0, 0, 7, 7, 6};     /* A727  U,RU,RU,R,R,RD,RD,D */
static const uint8_t jump_l[8] = {2, 3, 3, 4, 4, 5, 5, 6};     /* A72F */

#define STEP_R(o)  ai_step(g, (o), 0)
#define STEP_RU(o) ai_step(g, (o), 1)
#define STEP_U(o)  ai_step(g, (o), 2)
#define STEP_LU(o) ai_step(g, (o), 3)
#define STEP_L(o)  ai_step(g, (o), 4)
#define STEP_LD(o) ai_step(g, (o), 5)
#define STEP_D(o)  ai_step(g, (o), 6)
#define STEP_RD(o) ai_step(g, (o), 7)

/* ===================================================================== bat */
/* State = next >> 6: 0 idle, 1 wake-up, 2 chase, 3 retreat. */

/* 0xA2A5  Idle (frame 0): phase counts down 0x10 per frame; at 0 it wakes when
 * its ring column is 0x0B..0x1A (6 cells left .. 9 right of the hero's body
 * column 0x11). */
static void bat_idle(Game *g, MapObj *e)
{
    (void)g;
    if (e->phase) { e->phase = (uint8_t)(e->phase - 0x10); return; }
    if ((uint8_t)(e->rcol - 0x11) < 10 || (uint8_t)(0x11 - e->rcol) < 7) e->next = 0x40;
    e->phase = 0;
}

/* 0xA2D0  Wake-up: frames 1, 2, 3, then chase. */
static void bat_wake(Game *g, MapObj *e)
{
    (void)g;
    e->phase = (uint8_t)((e->phase + 1) & 7);
    if (e->phase == 3) e->next = 0x80;
}

/* 0xA3D4  Flying animation: frames 3,4,5,6 cycling. */
static void bat_anim(MapObj *e)
{
    e->phase = (uint8_t)((e->phase + 1) & 7);
    if (e->phase >= 7) e->phase = 3;
}

/* 0xA2E3  Chase: one cell per frame diagonally toward the hero, no gravity
 * except directly above/below him; backs off when the hero was hurt this
 * frame or it is blocked below. */
static void bat_chase(Game *g, MapObj *e)
{
    bat_anim(e);                                                        /* A2E3 */
    if (g->hero_hit_flash) { e->next = 0xC0; return; }                  /* A2E6 */
    uint8_t d = (uint8_t)((g->hero_map_row - e->row + 0x15) & 0x3F);    /* A2F2 */
    int go_right = 0, go_left = 0;
    if (e->rcol == 0x11 || e->rcol == 0x10) goto drop;                  /* over/under the hero */
    if (d < 0x12) {                                                     /* hero above */
        if (e->rcol < 0x10) { if (!STEP_RU(e)) { e->hit |= FACING_RIGHT;  return; } go_right = 1; }   /* A35E */
        else                { if (!STEP_LU(e)) { e->hit &= (uint8_t)~FACING_RIGHT; return; } go_left = 1; }  /* A36A */
    } else if (d >= 0x18) {                                             /* hero below */
        if (e->rcol < 0x10) { if (!STEP_RD(e)) { e->hit |= FACING_RIGHT;  return; } go_right = 1; }   /* A312 */
        else                { if (!STEP_LD(e)) { e->hit &= (uint8_t)~FACING_RIGHT; return; } go_left = 1; }  /* A31E */
    } else if (e->rcol < 0x10) go_right = 1; else go_left = 1;          /* A32A: level */
    if (go_right) { if (!STEP_R(e)) { e->hit |= FACING_RIGHT; return; } }               /* A338 */
    else if (go_left) { if (!STEP_L(e)) { e->hit &= (uint8_t)~FACING_RIGHT; return; } } /* A344 */
drop:
    if (STEP_D(e)) e->next = 0xC0;                                      /* A376: blocked below -> retreat */
}

/* 0xA383  Retreat: a diagonal-up step in the facing direction (flipping the
 * facing) plus one step up, a 2-frame pause, then 7 idle frames. */
static void bat_retreat(Game *g, MapObj *e)
{
    if (e->next & 0x20) {                                               /* A383 */
        e->phase = (uint8_t)((e->phase - 1) & 7);                       /* A3BD */
        if (e->phase) return;
        e->phase = 0x70; e->next = 0;                                   /* A3CB */
        return;
    }
    bat_anim(e);                                                        /* A389 */
    if (e->hit & FACING_RIGHT) { if (STEP_RU(e)) return; e->hit &= (uint8_t)~FACING_RIGHT; }   /* A392 */
    else                       { if (STEP_LU(e)) return; e->hit |= FACING_RIGHT; }             /* A3A0 */
    if (STEP_U(e)) return;                                              /* A3AC */
    e->next |= 0x20; e->phase = 2;                                      /* A3B4 */
}

/* 0xA26A */
static void bat_update(Game *g, MapObj *e)
{
    if (ai_on_hazard(g, e)) { enemy_killed(g, e); return; }             /* A26A */
    if (!e->hp) e->hp = 2;                                              /* A276 */
    if (e->hit & HIT_STUN) { enemy_take_damage(g, e); return; }         /* A280 */
    switch ((e->next >> 6) & 3) {                                       /* A28B, table A29D */
    case 0: bat_idle(g, e); break;
    case 1: bat_wake(g, e); break;
    case 2: bat_chase(g, e); break;
    default: bat_retreat(g, e); break;
    }
}

/* =================================================================== snail */
/* 0xA3E7  Falls 1 row/frame; on the ground one step toward the hero every 4th
 * frame; a blocked step just stops it. */
static void snail_update(Game *g, MapObj *e)
{
    if (ai_on_hazard(g, e)) { enemy_killed(g, e); return; }             /* A3E7 */
    if (!e->hp) e->hp = 2;                                              /* A3F3 */
    if (e->hit & HIT_STUN) { enemy_take_damage(g, e); return; }         /* A3FD */
    if (!STEP_D(e)) return;                                             /* A408: falling */
    e->phase = (uint8_t)((e->phase + 0x41) & 0xC3);                     /* A410 */
    if (e->phase & 0xF0) return;                                        /* A418 */
    if (e->rcol < 0x11) { if (STEP_R(e)) e->hit |= FACING_RIGHT; }      /* A41F */
    else                { if (STEP_L(e)) e->hit &= (uint8_t)~FACING_RIGHT; }   /* A432 */
}

/* ==================================================================== frog */

/* 0xA4E8 / 0xA6F0  Where is the hero?  0xFF when he is `range` or more rows
 * away; otherwise the facing bit that points at him, with *facing_hero set
 * when the enemy already faces him. */
static uint8_t hero_dir(const Game *g, const MapObj *e, int range, int *facing_hero)
{
    int d = (int)(int8_t)(uint8_t)(g->hero_map_row - e->row);
    if (d < 0) d = -d;
    *facing_hero = 0;
    if (d >= range) return 0xFF;
    if (e->rcol < 0x11) { *facing_hero = (e->hit & FACING_RIGHT) != 0; return FACING_RIGHT; }
    *facing_hero = (e->hit & FACING_RIGHT) == 0;
    return 0;
}

/* 0xA4A2  Hop: phase 2..5 -> RU, R, R, RD (or the mirrored list), one step per
 * frame; blocked and not facing the hero -> turn around. */
static void frog_hop(Game *g, MapObj *e)
{
    int facing;
    uint8_t old = e->phase, n = (uint8_t)((old + 1) & 7);               /* A4A2 */
    if (n < 7) {
        e->phase = (uint8_t)((old & 0xF0) | n);                         /* A4AF */
        int k = (old & 7) - 2; if (k < 0) k = 0; if (k > 3) k = 3;
        uint8_t dir = ((e->hit & FACING_RIGHT) ? hop_r : hop_l)[k];     /* A4B9 */
        if (!ai_step_dir(g, e, dir)) return;                            /* A4CA */
        hero_dir(g, e, 8, &facing);                                     /* A4D2 */
        if (!facing) e->hit ^= FACING_RIGHT;                            /* A4D7 */
    }
    e->next &= (uint8_t)~8; e->phase = 0; STEP_D(e);                    /* A4DB */
}

/* 0xA43F  On the ground: hop at once when it faces a hero within 7 rows,
 * otherwise every 8th frame turn toward him and hop. */
static void frog_update(Game *g, MapObj *e)
{
    int facing;
    if (ai_on_hazard(g, e)) { enemy_killed(g, e); return; }             /* A43F */
    if (!e->hp) e->hp = 1;                                              /* A44B */
    if (e->hit & HIT_STUN) { enemy_take_damage(g, e); return; }         /* A455 */
    if (e->next & 8) { frog_hop(g, e); return; }                        /* A460 */
    e->phase = (uint8_t)((e->phase + 0x21) & 0xE1);                     /* A466 */
    if (!STEP_D(e)) return;                                             /* A46E: airborne */
    uint8_t f = hero_dir(g, e, 8, &facing);                             /* A476 */
    if (!facing) {
        if (e->phase & 0xE0) return;                                    /* A47B */
        f = hero_dir(g, e, 8, &facing);                                 /* A483 */
        if (f != 0xFF) e->hit = (uint8_t)((e->hit & 0x7F) | f);         /* A48A */
    }
    e->phase = 2; e->next |= 8;                                         /* A491 / A49A */
}

/* =============================================================== hedgehog */
/* next: 0x02 chasing, 0x04 resting, 0x08 gap jump, 0x10 wall jump, bits 5-7
 * the jump step counter; link counts wander steps. */

/* 0xA558  Resting (frames 4/5).  The hero within 6 rows -> chase; otherwise
 * after 16 frames pick a random facing away from a wall and walk again. */
static void hog_rest(Game *g, MapObj *e)
{
    int facing;
    e->phase = (uint8_t)((e->phase & 0xF1) | 4);                        /* A558 */
    uint8_t f = hero_dir(g, e, 6, &facing);                             /* A560 */
    if (f != 0xFF) {                                                    /* A567 */
        e->hit = (uint8_t)((e->hit & 0x7F) | f); e->phase = 0;
        e->next = (uint8_t)((e->next | 2) & ~4);
        return;
    }
    unsigned t = (unsigned)e->phase + 0x40; e->phase = (uint8_t)t;      /* A57B */
    if (!(t & 0x100)) return;
    e->phase = (uint8_t)(((e->phase + 1) & 1) + 4);                     /* A582 */
    t = (unsigned)e->next + 0x40; e->next = (uint8_t)t;                 /* A58E */
    if (!(t & 0x100)) return;
    e->next &= (uint8_t)~4;                                             /* A595 */
    e->hit = (uint8_t)((e->hit & 0x7F) | (krn_random(g) & 0x80));       /* A599 */
    if (e->hit & FACING_RIGHT) { if (ai_probe(g, e, 0)) e->hit &= (uint8_t)~FACING_RIGHT; }   /* A5AB */
    else                       { if (ai_probe(g, e, 4)) e->hit |= FACING_RIGHT; }             /* A5B8 */
}

/* 0xA649  Gap jump: phase 1,2,3 -> R, R, RD (or mirrored); a blocked probe
 * ends it in the rest state. */
static void hog_gap_jump(Game *g, MapObj *e)
{
    uint8_t n = (uint8_t)((e->phase + 1) & 3);                          /* A649 */
    if (n == 0) { e->next &= (uint8_t)~8; e->phase = 3; STEP_D(e); return; }   /* A683 */
    e->phase = (uint8_t)((e->phase & 0xF0) | n);
    uint8_t dir = ((e->hit & FACING_RIGHT) ? hop_r : hop_l)[n];         /* A65C */
    if (ai_probe_dir(g, e, dir)) { e->next = (uint8_t)((e->next & ~8) | 4); return; }   /* A67A */
    ai_step_dir(g, e, dir);                                             /* A675 */
}

/* 0xA690  Wall jump: 8 sub-steps through jump_r/jump_l, one per frame. */
static void hog_wall_jump(Game *g, MapObj *e)
{
    e->next = (uint8_t)(e->next + 0x20);                                /* A690 */
    if (!(e->next & 0x20)) {
        uint8_t n = (uint8_t)((e->phase + 1) & 3);                      /* A69A */
        if (n == 0) { e->next &= (uint8_t)~0x10; e->phase = 3; STEP_D(e); return; }   /* A6E3 */
        e->phase = (uint8_t)((e->phase & 0xF0) | n);
    }
    uint8_t dir = ((e->hit & FACING_RIGHT) ? jump_r : jump_l)[((e->next >> 5) - 1) & 7];   /* A6AD */
    if (!ai_step_dir(g, e, dir)) return;                                /* A6C7 */
    e->next = (uint8_t)((e->next & ~0x10) | 4);                         /* A6CF */
    if (e->phase) e->phase = 3;                                         /* A6D7 */
}

/* 0xA517 */
static void hog_update(Game *g, MapObj *e)
{
    int facing;
    if (ai_on_hazard(g, e)) { enemy_killed(g, e); return; }             /* A517 */
    if (!e->hp) e->hp = 1;                                              /* A523 */
    if (e->hit & HIT_STUN) { enemy_take_damage(g, e); return; }         /* A52D */
    if (e->next & 0x08) { hog_gap_jump(g, e);  return; }                /* A538 */
    if (e->next & 0x10) { hog_wall_jump(g, e); return; }                /* A541 */
    if (!STEP_D(e)) return;                                             /* A54A: falling */
    if (e->next & 0x04) { hog_rest(g, e); return; }                     /* A552 */

    /* A5C5: an open cell under the leading foot -> jump the gap */
    int p = game_ring_index(g, e->row, e->rcol);
    p = game_ring_add(p, 2 * RING_W + ((e->hit & FACING_RIGHT) ? 1 : 0));
    if (ai_cell_passable(g, g->ring[p])) { e->phase = 0; e->next |= 8; return; }   /* A5EB */
    e->phase = (uint8_t)((e->phase + 1) & 3);                           /* A5F4 */
    if (!(e->next & 2)) {                                               /* A5FB: 16 wander steps, then rest */
        e->link = (uint8_t)(e->link + 0x10);
        if (e->link == 0) { e->next |= 4; return; }
    }
    hero_dir(g, e, 6, &facing);                                         /* A60C */
    if (facing) { e->hit &= (uint8_t)~0x02; e->link = 0; }              /* A611 (sic: clears hit bit 1) */
    if (e->hit & FACING_RIGHT) { if (!STEP_R(e)) return; }              /* A619 */
    else                       { if (!STEP_L(e)) return; }              /* A634 */
    e->phase = 0; e->next = (uint8_t)((e->next | 0x10) & 0x1F);         /* A627/A63C: wall ahead -> jump it */
}

/* 0xA254  Entry: dispatch on the class nibble (table A262). */
void eai1_entry(Game *g, MapObj *e)
{
    switch (e->type & 0xF) {
    case 0: bat_update(g, e);   break;
    case 1: snail_update(g, e); break;
    case 2: frog_update(g, e);  break;
    case 3: hog_update(g, e);   break;
    default: break;
    }
}
