/*
 * eai1.c — cavern 1 enemy AI (EAI1.BIN = ZELRES3[1], 1847 bytes, raw at BASE:A000).
 * Hand-cleaned from `ndisasm -b16 -o 0xA000`; every routine carries its address.
 * Sprites: ENP1.GRP (ZELRES3[56]); frame lists are the 5-byte records described
 * in ai_common.h.  Used by maps mp10 (types 0..3 appear: 14/15/16/5 objects).
 *
 * Header (A000..A02F):
 *   [A000] = A254 entry      [A002] = 0        [A006] = A240 drop lists
 *   A008 exp[]     = { 3, 2, 5, 3 }                 (classes 0..3)
 *   A010 contact[] = { 5, 5, 15, 8 }                per frame of overlap
 *   A240 drop ptrs = { A24C, A250, A250, A248 }:
 *        class 0 -> {5,4,4,0}  10G / 1G / 1G / nothing
 *        class 1 -> {4,0,4,0}   1G / nothing / 1G / nothing
 *        class 2 -> {4,0,4,0}
 *        class 3 -> {5,0,0,0}  10G / nothing x3        (down-thrust kill: entry 0)
 *   Frame tables (A030 left / A070 right): class 0 A0B0/A0D3 (7 frames), class 1
 *   A0F6/A10A (4), class 2 A11E/A141 (7), class 3 A164/A182 (6); dying 8..B at
 *   A1A0/A1AF/A1BE/A1CD (3 each); items 10..19 at A22C/A1DC/A213/A1EB/A1FF/A21D/A222/A227.
 *
 * Common per-class prologue (every class): killed outright on a hazard tile
 * (vec 24 -> vec 25, no EXP), hp initialised on the first update (hp is 0 at
 * spawn), and a landed hit (hit & 0x20) is handed to vec 26 instead of moving.
 * Initial HP: class 0 = 2, class 1 = 2, class 2 = 1, class 3 = 1.
 */
#include "ai_common.h"

/* direction tables (vec 2 codes: 0 R,1 RU,2 U,3 LU,4 L,5 LD,6 D,7 RD) */
static const u8 hop_r[4]      = { 1, 0, 0, 7 };                 /* A71F  RU, R, R, RD   */
static const u8 hop_l[4]      = { 3, 4, 4, 5 };                 /* A723  LU, L, L, LD   */
static const u8 jump_r[8]     = { 2, 1, 1, 0, 0, 7, 7, 6 };     /* A727  U,RU,RU,R,R,RD,RD,D */
static const u8 jump_l[8]     = { 2, 3, 3, 4, 4, 5, 5, 6 };     /* A72F */

/* 0xA254  Entry: dispatch on the class nibble through the table at A262. */
void ai_entry(struct enemy *e)
{
    static void (*const class_fn[4])(struct enemy *) = { bat_update, snail_update, frog_update, hog_update };  /* A262 */
    class_fn[e->type & 0xF](e);
}

/* ======================================================================== */
/* Class 0 — "bat": red flying creature, 7 frames.  HP 2, contact 5, EXP 3.  */
/* State = next >> 6 (bits 7-6): 0 idle, 1 wake-up, 2 chase, 3 retreat.      */
/* ======================================================================== */

/* 0xA26A */
static void bat_update(struct enemy *e)
{
    static void (*const st[4])(struct enemy *) = { bat_idle, bat_wake, bat_chase, bat_retreat };   /* A29D */
    if (fight_on_hazard(e)) { fight_enemy_killed(e); return; }     /* A26A */
    if (!e->hp) e->hp = 2;                                          /* A276 */
    if (e->hit & HIT_STUN) { fight_take_damage(e); return; }        /* A280 */
    st[e->next >> 6](e);                                            /* A28B: rol,rol,and 3 */
}

/* 0xA2A5  Idle (frame 0).  phase counts down 0x10 per frame (spawn sets 0x10,
 * a retreat sets 0x70); at 0 the bat wakes when its ring column is within
 * 0x0B..0x1A, i.e. from 6 cells left to 9 cells right of the hero's body
 * column (0x11). */
static void bat_idle(struct enemy *e)
{
    if (e->phase) { e->phase -= 0x10; return; }                                     /* A2AA */
    if ((u8)(e->rcol - 0x11) < 10 || (u8)(0x11 - e->rcol) < 7) e->next = 0x40;     /* A2B5..A2C7 */
    e->phase = 0;                                                                   /* A2CB */
}

/* 0xA2D0  Wake-up: frames 1,2,3 (one per frame), then chase. */
static void bat_wake(struct enemy *e)
{
    e->phase = (e->phase + 1) & 7;
    if (e->phase == 3) e->next = 0x80;
}

/* 0xA3D4  Flying animation: frames 3,4,5,6 cycling. */
static void bat_anim(struct enemy *e)
{
    e->phase = (e->phase + 1) & 7;
    if (e->phase >= 7) e->phase = 3;
}

/* 0xA2E3  Chase: one cell per frame, diagonally toward the hero; no gravity
 * except when it is directly above/below him.  Vertical bands from
 * d = (hero_map_row - row + 21) & 63: d < 18 hero is >3 rows above,
 * 18..23 within 3 rows, >= 24 hero is >= 3 rows below.  When the hero was hurt
 * this frame (by anything) the bat backs off (state 3). */
static void bat_chase(struct enemy *e)
{
    bat_anim(e);                                                    /* A2E3 */
    if (hero_hit_flash) { e->next = 0xC0; return; }                 /* A2E6 */
    u8 d = (hero_map_row - e->row + 0x15) & 0x3F;                   /* A2F2 */
    if (e->rcol == 0x11 || e->rcol == 0x10) goto drop;              /* over/under the hero */
    if (d < 0x12) {                                                 /* hero above */
        if (e->rcol < 0x10) { if (!fight_step_right_up(e)) { e->hit |= FACING_RIGHT;  return; } goto right; }   /* A35E */
        else                { if (!fight_step_left_up(e))  { e->hit &= ~FACING_RIGHT; return; } goto left;  }   /* A36A */
    } else if (d >= 0x18) {                                         /* hero below */
        if (e->rcol < 0x10) { if (!fight_step_right_down(e)) { e->hit |= FACING_RIGHT;  return; } goto right; } /* A312 */
        else                { if (!fight_step_left_down(e))  { e->hit &= ~FACING_RIGHT; return; } goto left;  } /* A31E */
    } else if (e->rcol < 0x10) goto right; else goto left;          /* A32A: level */
right: if (!fight_step_right(e)) { e->hit |= FACING_RIGHT;  return; } goto drop;   /* A338 */
left:  if (!fight_step_left(e))  { e->hit &= ~FACING_RIGHT; return; } goto drop;   /* A344 */
drop:  if (fight_step_down(e)) e->next = 0xC0;                      /* A376: blocked below -> retreat */
}

/* 0xA383  Retreat: one diagonal-up step in the facing direction (flipping the
 * facing so it now looks back at the hero) plus one step up in the same frame,
 * then a 2-frame pause, then idle for 7 frames (phase 0x70) before re-arming. */
static void bat_retreat(struct enemy *e)
{
    if (e->next & 0x20) {                                           /* A383 */
        e->phase = (e->phase - 1) & 7;                              /* A3BD */
        if (e->phase) return;
        e->phase = 0x70; e->next = 0;                               /* A3CB */
        return;
    }
    bat_anim(e);                                                    /* A389 */
    if (e->hit & FACING_RIGHT) { if (fight_step_right_up(e)) return; e->hit &= ~FACING_RIGHT; }   /* A392 */
    else                       { if (fight_step_left_up(e))  return; e->hit |= FACING_RIGHT;  }   /* A3A0 */
    if (fight_step_up(e)) return;                                   /* A3AC */
    e->next |= 0x20; e->phase = 2;                                  /* A3B4 */
}

/* ======================================================================== */
/* Class 1 — "snail": ground crawler, 4 frames.  HP 2, contact 5, EXP 2.     */
/* ======================================================================== */

/* 0xA3E7  Falls 1 row/frame; on the ground it steps one cell toward the hero
 * every 4th frame (phase bits 6-7 count, bits 0-1 animate), never jumps; a
 * blocked step just leaves it in place. */
static void snail_update(struct enemy *e)
{
    if (fight_on_hazard(e)) { fight_enemy_killed(e); return; }     /* A3E7 */
    if (!e->hp) e->hp = 2;                                          /* A3F3 */
    if (e->hit & HIT_STUN) { fight_take_damage(e); return; }        /* A3FD */
    if (!fight_step_down(e)) return;                                /* A408: falling */
    e->phase = (e->phase + 0x41) & 0xC3;                            /* A410 */
    if (e->phase & 0xF0) return;                                    /* A418: move on every 4th frame */
    if (e->rcol < 0x11) { if (!fight_step_right(e)) e->hit |= FACING_RIGHT;  }   /* A41F */
    else                { if (!fight_step_left(e))  e->hit &= ~FACING_RIGHT; }   /* A432 */
}

/* ======================================================================== */
/* Class 2 — "frog": hopper, 7 frames (0-1 sit, 2-6 jump).  HP 1, contact   */
/* 15, EXP 5.                                                                */
/* ======================================================================== */

/* 0xA4E8  Where is the hero?  Returns 0xFF when he is 8 or more rows away
 * (CF=0); otherwise the facing bit that would point at him (0x80 when his
 * body column 0x11 is right of the sprite) with CF=1 when the enemy already
 * faces him. */
static u8 hero_dir8(struct enemy *e, bool *facing_hero)
{
    u8 d = hero_map_row - e->row; if ((s8)d < 0) d = -d;            /* A4E8 */
    *facing_hero = false;
    if (d >= 8) return 0xFF;                                        /* A4F2 */
    if (e->rcol < 0x11) { *facing_hero = (e->hit & FACING_RIGHT) != 0; return FACING_RIGHT; }   /* A4F9 */
    else                { *facing_hero = (e->hit & FACING_RIGHT) == 0; return 0; }              /* A50B */
}

/* 0xA43F  On the ground: hop immediately whenever it faces a hero who is
 * within 7 rows; otherwise every 8th frame (phase bits 5-7) turn toward him
 * (if within 7 rows) and hop.  Idle animation toggles frame 0/1. */
static void frog_update(struct enemy *e)
{
    bool facing;
    if (fight_on_hazard(e)) { fight_enemy_killed(e); return; }     /* A43F */
    if (!e->hp) e->hp = 1;                                          /* A44B */
    if (e->hit & HIT_STUN) { fight_take_damage(e); return; }        /* A455 */
    if (e->next & 8) { frog_hop(e); return; }                       /* A460 */
    e->phase = (e->phase + 0x21) & 0xE1;                            /* A466 */
    if (!fight_step_down(e)) return;                                /* A46E: airborne */
    u8 f = hero_dir8(e, &facing);                                   /* A476 */
    if (!facing) {
        if (e->phase & 0xE0) return;                                /* A47B */
        f = hero_dir8(e, &facing);                                  /* A483 */
        if (f != 0xFF) e->hit = (e->hit & 0x7F) | f;                /* A48A: face the hero */
    }
    e->phase = 2; e->next |= 8;                                     /* A491 / A49A */
}

/* 0xA4A2  Hop: phase 2..5 -> steps RU, R, R, RD (hop_r) or LU, L, L, LD
 * (hop_l), one per frame; blocked -> if not facing the hero, turn around;
 * the hop ends and gravity takes over. */
static void frog_hop(struct enemy *e)
{
    bool facing;
    u8 old = e->phase, n = (old + 1) & 7;                           /* A4A2 */
    if (n < 7) {
        e->phase = (old & 0xF0) | n;                                /* A4AF */
        u8 dir = ((e->hit & FACING_RIGHT) ? hop_r : hop_l)[old - 2];/* A4B9 */
        if (!fight_step_dir(dir)) return;                           /* A4CA */
        hero_dir8(e, &facing);                                      /* A4D2 */
        if (!facing) e->hit ^= FACING_RIGHT;                        /* A4D7 */
    }
    e->next &= ~8; e->phase = 0; fight_step_down(e);                /* A4DB */
}

/* ======================================================================== */
/* Class 3 — "hedgehog": walker that jumps over walls and gaps, 6 frames     */
/* (0-3 walk, 4-5 curled/resting).  HP 1, contact 8, EXP 3.                  */
/* next: 0x02 chasing, 0x04 resting, 0x08 gap jump, 0x10 wall jump,          */
/* bits 5-7 jump step counter.  link: wander step counter.                   */
/* ======================================================================== */

/* 0xA6F0  Same as hero_dir8 but the range is 6 rows. */
static u8 hero_dir6(struct enemy *e, bool *facing_hero);

/* 0xA517 */
static void hog_update(struct enemy *e)
{
    bool facing;
    if (fight_on_hazard(e)) { fight_enemy_killed(e); return; }     /* A517 */
    if (!e->hp) e->hp = 1;                                          /* A523 */
    if (e->hit & HIT_STUN) { fight_take_damage(e); return; }        /* A52D */
    if (e->next & 0x08) { hog_gap_jump(e);  return; }               /* A538 */
    if (e->next & 0x10) { hog_wall_jump(e); return; }               /* A541 */
    if (!fight_step_down(e)) return;                                /* A54A: falling */
    if (e->next & 0x04) { hog_rest(e); return; }                    /* A552 */

    /* walking (A5C5): if the cell under the leading foot is open, jump the gap */
    u8 *p = ring_addr(e->row, e->rcol);
    p = ring_wrap_down(p + 0x48 + ((e->hit & FACING_RIGHT) ? 1 : 0));   /* (row+2, rcol or rcol+1) */
    if (cell_passable_ai(*p)) { e->phase = 0; e->next |= 8; return; }   /* A5EB */
    e->phase = (e->phase + 1) & 3;                                  /* A5F4 */
    if (!(e->next & 2) && (e->link += 0x10) == 0) { e->next |= 4; return; }   /* A5FB: wander 16 steps, then rest */
    hero_dir6(e, &facing);                                          /* A60C */
    if (facing) { e->hit &= ~0x02; e->link = 0; }                   /* A611: keep walking (sic: clears hit bit 1, not next) */
    if (e->hit & FACING_RIGHT) { if (!fight_step_right(e)) return; }/* A619 */
    else                       { if (!fight_step_left(e))  return; }/* A634 */
    e->phase = 0; e->next = (e->next | 0x10) & 0x1F;                /* A627/A63C: wall ahead -> jump over it */
}

/* 0xA558  Resting (frames 4/5 toggling every 4 frames).  If the hero comes
 * within 6 rows: face him, start chasing (next |= 2).  Otherwise after 16
 * frames pick a random facing (away from a wall) and walk again. */
static void hog_rest(struct enemy *e)
{
    bool facing;
    e->phase = (e->phase & 0xF1) | 4;                               /* A558 */
    u8 f = hero_dir6(e, &facing);                                   /* A560 */
    if (f != 0xFF) { e->hit = (e->hit & 0x7F) | f; e->phase = 0; e->next = (e->next | 2) & ~4; return; }   /* A567 */
    if ((e->phase += 0x40) did not carry) return;                   /* A57B */
    e->phase = ((e->phase + 1) & 1) + 4;                            /* A582 */
    if ((e->next += 0x40) did not carry) return;                    /* A58E */
    e->next &= ~4;                                                  /* A595 */
    e->hit = (e->hit & 0x7F) | (KRN_RANDOM() & 0x80);               /* A599 */
    if (e->hit & FACING_RIGHT) { if (fight_probe_right(e)) e->hit &= ~FACING_RIGHT; }   /* A5AB */
    else                       { if (fight_probe_left(e))  e->hit |= FACING_RIGHT;  }   /* A5B8 */
}

/* 0xA649  Gap jump: phase 1,2,3 -> steps R, R, RD (hop_r[1..3]) or L, L, LD;
 * a blocked probe ends it in the rest state; after 3 steps gravity resumes. */
static void hog_gap_jump(struct enemy *e)
{
    u8 n = (e->phase + 1) & 3;                                      /* A649 */
    if (n == 0) { e->next &= ~8; e->phase = 3; fight_step_down(e); return; }   /* A683 */
    e->phase = (e->phase & 0xF0) | n;
    u8 dir = ((e->hit & FACING_RIGHT) ? hop_r : hop_l)[n];         /* A65C */
    if (fight_probe_dir(dir)) { e->next = (e->next & ~8) | 4; return; }   /* A67A */
    fight_step_dir(dir);                                            /* A675 */
}

/* 0xA690  Wall jump: 8 sub-steps indexed by (next>>5)-1 through jump_r/jump_l
 * (U, RU, RU, R, R, RD, RD, D), one per frame; the walk frame advances every
 * second frame and the jump ends when it wraps (after 7 steps, before the D).
 * Blocked -> rest. */
static void hog_wall_jump(struct enemy *e)
{
    e->next += 0x20;                                                /* A690 */
    if (!(e->next & 0x20)) {
        u8 n = (e->phase + 1) & 3;                                  /* A69A */
        if (n == 0) { e->next &= ~0x10; e->phase = 3; fight_step_down(e); return; }   /* A6E3 */
        e->phase = (e->phase & 0xF0) | n;
    }
    u8 dir = ((e->hit & FACING_RIGHT) ? jump_r : jump_l)[((e->next >> 5) - 1) & 7];   /* A6AD */
    if (!fight_step_dir(dir)) return;                               /* A6C7 */
    e->next = (e->next & ~0x10) | 4;                                /* A6CF */
    if (e->phase) e->phase = 3;                                     /* A6D7 */
}

/* 0xA6F0 */
static u8 hero_dir6(struct enemy *e, bool *facing_hero)
{
    u8 d = hero_map_row - e->row; if ((s8)d < 0) d = -d;
    *facing_hero = false;
    if (d >= 6) return 0xFF;                                        /* A6FA */
    if (e->rcol < 0x11) { *facing_hero = (e->hit & FACING_RIGHT) != 0; return FACING_RIGHT; }
    else                { *facing_hero = (e->hit & FACING_RIGHT) == 0; return 0; }
}
