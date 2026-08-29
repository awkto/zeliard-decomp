/*
 * eai3.c — cavern 3 enemy AI (EAI3.BIN = ZELRES3[3], 1847 bytes @ BASE:A000).
 * Sprites ENP3.GRP (ZELRES3[58]).  Maps mp30/mp31: classes 0..3.
 *
 * Header:  [A000] = A2B2 entry   [A006] = A29A drop lists
 *   A008 exp[]     = { 20, 10, 10, 20 }
 *   A010 contact[] = { 40, 40, 16, 40 }
 *   drops: 0 A2A2 {4,4,0,0}, 1 A2A6 {5,5,0,0}, 2 A2AA {4,4,4,4}, 3 A2AE {5,5,5,5}
 *   Frames: 0 A0B0/A0EC (12: 0-7 crawl, 8-9 falling/landing, 10-11 jump),
 *   1 A137/A155 (6), 2 A182/A1A5 (7: 0-3 buried shell, 4-6 raised), 3 A1D7/A1F5 (6);
 *   dying 8..B at A128/A173/A1C8/A213.
 * Dispatch A2C0: { A2C8, A44D, A4F0, A66E }.
 */
#include "ai_common.h"

/* ======================================================================== */
/* Class 0 — ceiling spider.  HP 2, contact 40, EXP 20.  State = next & 7   */
/* (table A2E9): 0 crawl on the ceiling, 1 drop, 2 landed, 3-4 hop up/across,*/
/* 5 hop down, 6 landed, 7 climb back to the ceiling.  link = step counter. */
/* ======================================================================== */
static void spider_update(struct enemy *e)          /* 0xA2C8 */
{
    if (!e->hp) e->hp = 2;                                          /* A2C8 */
    if (e->hit & HIT_STUN) { fight_take_damage(e); return; }        /* A2D2 */
    switch (e->next & 7) {                                          /* A2DD */
    case 0: spider_crawl(e); break;                                 /* A2F9 */
    case 1: if (fight_step_down(e)) { e->next = 2; e->phase = 9; } break;          /* A356: fall until floor */
    case 2: e->next = 3; e->phase = 0xA; e->link = 0; break;        /* A367 */
    case 3:                                                         /* A374: two diagonal-up steps */
        if (e->link == 1) { e->next = 4; e->link = 0xFF; }
        e->phase = 0xB; e->link++;
        if ((e->hit & FACING_RIGHT) ? fight_step_right_up(e) : fight_step_left_up(e)) e->hit ^= FACING_RIGHT;
        break;
    case 4:                                                         /* A3AC: two horizontal steps */
        if (e->link == 1) e->next = 5;
        e->phase = 8; e->link++;
        if ((e->hit & FACING_RIGHT) ? fight_step_right(e) : fight_step_left(e)) e->hit ^= FACING_RIGHT;
        break;
    case 5:                                                         /* A3E0: diagonal down until blocked */
        e->phase = 8;
        if ((e->hit & FACING_RIGHT) ? fight_step_right_down(e) : fight_step_left_down(e)) { e->phase = 9; e->next = 6; }
        break;
    case 6: e->phase = 0xA; e->next = 7; break;                     /* A405 */
    case 7: spider_climb(e); break;                                 /* A40E */
    }
}

/* 0xA2F9  On the ceiling: frames 0-7, one step every 2nd frame in the facing
 * direction.  Drops (state 1, frame 8) when the cells above it become free
 * (vec 14 not blocked), when its column is 0x12..0x14 (just right of the
 * hero's columns 0x10..0x12), or on the second wall bump (link parity: it
 * turns at the first wall, drops at the next). */
static void spider_crawl(struct enemy *e)
{
    e->phase = (e->phase + 1) & 7;                                  /* A2F9 */
    if (!fight_probe_up(e)) goto drop;                              /* A300: nothing above -> fall */
    if (!(e->phase & 1)) return;                                    /* A307 */
    if (e->rcol >= 0x12 && e->rcol < 0x15) goto drop;               /* A30E */
    if ((e->hit & FACING_RIGHT) ? !fight_step_right(e) : !fight_step_left(e)) return;   /* A31F/A337 */
    u8 n = e->link; e->link = 0; e->hit ^= FACING_RIGHT;            /* A327: wall: turn */
    if (n & 1) return;
drop: e->next = 1; e->phase = 8;                                    /* A34D */
}

/* 0xA40E  Climb: one diagonal-up step per frame, flipping direction every
 * frame (zig-zag straight up) until a ceiling is directly above, then
 * re-attach (state 0, link = 1). */
static void spider_climb(struct enemy *e)
{
    e->phase = 8;
    if (!(e->hit & FACING_RIGHT)) {
        if (fight_step_left_up(e)) return;                          /* A418 */
        if (fight_probe_up(e)) goto attach;                         /* A420 */
    } else {
        if (fight_step_right_up(e)) return;                         /* A439 */
        if (fight_step_up(e)) goto attach;                          /* A441 (sic: a real step, not a probe) */
    }
    e->hit ^= FACING_RIGHT; return;
attach: e->next = 0; e->phase = 0; e->link = 1;                     /* A42C */
}

/* ======================================================================== */
/* Class 1 — red hopper ("crab").  HP 2, contact 40, EXP 10.  Frames 0-5.   */
/* Hops toward the hero without pause: arc RU,RU,R,R,RD,RD (A4E4) or        */
/* LU,LU,L,L,LD,LD (A4EA), one cell per frame, re-facing the hero at every  */
/* landing; if the very first step of a hop is blocked it hops away once.   */
/* ======================================================================== */
static const u8 hop_r[6] = { 1, 1, 0, 0, 7, 7 }, hop_l[6] = { 3, 3, 4, 4, 5, 5 };   /* A4E4 / A4EA */
static void crab_update(struct enemy *e)            /* 0xA44D */
{
    if (!e->hp) e->hp = 2;
    if (e->hit & HIT_STUN) { fight_take_damage(e); return; }        /* A457 */
    if (e->next & 8) { crab_hop(e); return; }                       /* A462 */
    if (!(e->next & 4)) { e->hit |= FACING_RIGHT; if (e->rcol >= 0x11) e->hit ^= FACING_RIGHT; }   /* A46E: face the hero */
    if (!fight_step_down(e)) return;                                /* A47C */
    e->phase &= 0xF0;
    if ((e->phase += 0x80) carried) { e->phase = 0; e->next |= 8; } /* A488: every 2nd grounded frame */
}
static void crab_hop(struct enemy *e)               /* 0xA498 */
{
    e->next &= ~4;
    u8 old = e->phase;
    e->phase = (e->phase + 1) & 7;
    if (e->phase >= 6) { e->phase = 0; e->next &= ~8; }             /* A4AC */
    if (!fight_step_dir(((e->hit & FACING_RIGHT) ? hop_r : hop_l)[old])) return;   /* A4C1 */
    e->next &= ~8;                                                  /* A4C9: blocked */
    if (e->phase == 1) { e->next |= 4; e->hit ^= FACING_RIGHT; }    /* first step blocked: turn away */
    e->phase = 0; fight_step_down(e);
}

/* ======================================================================== */
/* Class 2 — burrowing snake.  HP 4, contact 16, EXP 10.  State = next & 3  */
/* (A519): 0 crawling buried (immune, harmless), 1 rising, 2 up, 3 sinking. */
/* ======================================================================== */
static struct shot venom_r = { 0, 0, 0x2B, 0, 15, 0, 40 };          /* A654: right, 15 cells, damage 40 */
static struct shot venom_l = { 0, 0, 0x2B, 0, 15, 4, 40 };          /* A661 */

static void snake_update(struct enemy *e)           /* 0xA4F0 */
{
    bool facing;
    if (!e->hp) e->hp = 4;
    if (e->hit & HIT_STUN) { fight_take_damage(e); return; }        /* A4FA */
    if (!fight_step_down(e)) return;                                /* A505 */
    switch (e->next & 3) {
    case 0:                                                         /* A521 */
        e->type |= 0x60;                                            /* sword-immune, no contact */
        if ((e->phase += 0x80) did not carry) return;               /* every 2nd frame */
        e->phase = (e->phase + 1) & 1; if (e->phase) return;        /* every 4th: */
        if (++e->link >= 7) { e->next = 1; e->phase = 2; }          /* A536: surface after 7 steps */
        if (e->hit & FACING_RIGHT) {                                /* A547: one step, turning at ledges */
            if (cell_passable_ai(*ring_wrap_down(ring_addr(e->row, e->rcol) + 0x4A))) { e->hit &= ~FACING_RIGHT; fight_step_left(e); }
            else fight_step_right(e);
        } else {
            if (cell_passable_ai(*ring_wrap_down(ring_addr(e->row, e->rcol) + 0x47))) { e->hit |= FACING_RIGHT; fight_step_right(e); }
            else fight_step_left(e);
        }
        return;
    case 1:                                                         /* A5A3: rise, frames 2..5 */
        e->type &= 0x1F;
        if (++e->phase == 5) { e->next = 2; e->link = 0; }
        return;
    case 2:                                                         /* A5BA */
        if (e->next & 0x80) { e->next = 3; e->phase = 5; return; }  /* fired: sink */
        if ((e->phase += 0x40) did not carry) return;               /* every 4th frame */
        e->hit ^= FACING_RIGHT;                                     /* A5C7: look the other way */
        hero_dir5(e, &facing);
        if (!facing) { if (++e->link == 3) { e->next = 3; e->phase = 5; } return; }   /* A5D0 */
        e->phase = 6; e->next |= 0x80;                              /* A5E3: spit */
        venom_l.col = e->rcol; venom_r.col = e->rcol + 1; venom_l.row = venom_r.row = e->row & 0x3F;
        fight_shot_spawn((e->hit & FACING_RIGHT) ? &venom_r : &venom_l);
        return;
    case 3:                                                         /* A612: sink, frames 5..1 */
        if (--e->phase == 1) { e->next = 0; e->link = 0; }
        return;
    }
}
/* 0xA625  = eai2 hero_dir5 (range 5 rows) */

/* ======================================================================== */
/* Class 3 — charging beetle.  HP 4, contact 40, EXP 20.  Frame 0 while     */
/* airborne, 1 idle, 1-5 running.  next bit0 = charging, link = frames run. */
/* ======================================================================== */
static void beetle_update(struct enemy *e)          /* 0xA66E */
{
    bool facing; u8 dx;
    if (!e->hp) e->hp = 4;
    if (e->hit & HIT_STUN) { fight_take_damage(e); return; }        /* A678 */
    u8 ph = e->phase; e->phase = 0;
    if (!fight_step_down(e)) return;                                /* A68B: falling shows frame 0 */
    e->phase = ph;
    if (!(e->next & 1)) {                                           /* A697: idle */
        e->phase = 1; e->link = 0;
        u8 f = hero_dir6x(e, &facing, &dx);                         /* A701: |drow| < 6, dx = |0x11 - rcol| */
        if (!facing) { if (f != 0xFF) e->hit = (e->hit & 0x7F) | f; return; }   /* turn toward the hero */
        if (dx < 10) e->next |= 1;                                  /* A6B7: charge when within 10 columns */
        return;
    }
    if (++e->link == 20) { e->next &= ~1; return; }                 /* A6C2: run 20 frames */
    if (e->hit & FACING_RIGHT) { if (fight_step_right(e) && fight_step_right_up(e)) { e->next &= ~1; return; } }   /* A6E4: climbs 1-cell steps */
    else                       { if (fight_step_left(e)  && fight_step_left_up(e))  { e->next &= ~1; return; } }   /* A6D1 */
    if (++e->phase >= 6) e->phase = 1;                              /* A6F2 */
}

/* 0xA701  like hero_dir5 with range 6 rows; also returns AH = |0x11 - rcol| */
static u8 hero_dir6x(struct enemy *e, bool *facing_hero, u8 *dx);

/* 0xA2B2 */
void ai_entry(struct enemy *e)
{
    static void (*const fn[4])(struct enemy *) = { spider_update, crab_update, snake_update, beetle_update };   /* A2C0 */
    fn[e->type & 0xF](e);
}
