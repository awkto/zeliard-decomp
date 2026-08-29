/*
 * eai4.c — cavern 4 (ice) enemy AI (EAI4.BIN = ZELRES3[4], 2118 bytes @ BASE:A000).
 * Sprites ENP4.GRP (ZELRES3[59]).  Maps mp40/mp41: classes 0..4.
 *
 * Header:  [A000] = A269 entry   [A006] = A24F drop lists
 *   A008 exp[]     = { 10, 10, 0, 0, 20 }
 *   A010 contact[] = { 20, 4, 80, 80, 80 }
 *   drops: 0 A259 {5,4,4,5}, 1 A25D {4,4,4,4}, 2/3 A261 {1,1,1,1} (vanish), 4 A265 {5,5,5,4}
 *   Frames: 0 A0B0/A100 (16: 0-7 walk, 8-C curl up, D ball, E-F spinning),
 *   1 A15F (8, both facings), 2 A196 (2), 3 A1A0 (2), 4 A1B9 (4);
 *   dying 8 A150, 9 A187, A/B A1AA, C A1CD.
 * Dispatch A277: { A281, A466, A6B1, A6B1, A6F0 }.
 */
#include "ai_common.h"

/* ======================================================================== */
/* Class 0 — shell crawler that curls into a ball and dashes.  HP 8,        */
/* contact 20, EXP 10.  next: 1 uncurl-up anim, 2 uncurl-down anim,          */
/* 4 dash, 8 wall-wait+jump (bits 4-7 timers), link bit0 wait done.          */
/* ======================================================================== */
static const u8 jump_r[8] = { 2, 1, 1, 0, 0, 7, 7, 6 };   /* A456 */
static const u8 jump_l[8] = { 2, 3, 3, 4, 4, 5, 5, 6 };   /* A45E */

static void shell_update(struct enemy *e)               /* 0xA281 */
{
    if (!e->hp) e->hp = 8;
    if (e->hit & HIT_STUN) { fight_take_damage(e); return; }        /* A28B */
    if (e->next & 8) { shell_wall_jump(e); return; }                /* A296 */
    if (e->next & 4) { shell_dash(e); return; }                     /* A29F */
    if (!fight_step_down(e)) return;                                /* A2A8 */
    if (e->next & 1) {                                              /* A324: curl frames 8..B then next&=~1 */
        if ((e->phase & 0xF) < 8) { e->phase = 8; return; }
        if (++e->phase == 0xB) { e->phase |= 0x10; e->next &= ~1; }
        return;
    }
    if (e->next & 2) {                                              /* A346: uncurl B..8 then next&=~2 */
        if ((e->phase & 0xF) >= 0xC) { e->phase = 0xB; return; }
        if (--e->phase == 8) { e->phase |= 0x10; e->next &= ~2; }
        return;
    }
    /* walking: frame advances every frame, one step every 2nd frame (bit 7 toggles) */
    e->phase = (e->phase & 0xF0) | ((e->phase + 1) & 7);            /* A2BF */
    if ((e->phase += 0x80) did not carry) return;
    bool level = (hero_map_row == e->row) || (((hero_map_row + 1) & 0x3F) == e->row);   /* A2D5 */
    if (level) {
        if (!(KRN_RANDOM() & 3)) e->next = 5;                       /* A2EF: 25%: curl and dash */
        if (e->rcol < 0x11) goto right; else goto left;             /* A2FC: face the hero */
    }
    if (e->hit & FACING_RIGHT) goto right;
left:  e->hit &= ~FACING_RIGHT; if (fight_step_left(e))  e->next = 9; return;   /* A302: wall -> wait + jump */
right: e->hit |= FACING_RIGHT;  if (fight_step_right(e)) e->next = 9; return;   /* A313 */
}

/* 0xA368  Dash: frames count up to E/F (spinning ball), then 2 cells per
 * frame diagonally down (RD,RD) or, if that is blocked, straight (R,R);
 * when both are blocked turn around and uncurl (phase 0x1D, next = 2). */
static void shell_dash(struct enemy *e)
{
    u8 f = (e->phase & 0xF) + 1;
    if (f < 0xF) { e->phase = f; return; }
    e->phase = (f >= 0x10) ? 0xE : 0xF;                             /* A377 */
    if (e->hit & FACING_RIGHT) {
        fight_step_right_down(e); if (!fight_step_right_down(e)) return;       /* A386 */
        fight_step_right(e);      if (!fight_step_right(e))      return;
        e->hit &= ~FACING_RIGHT;
    } else {
        fight_step_left_down(e);  if (!fight_step_left_down(e))  return;       /* A3A6 */
        fight_step_left(e);       if (!fight_step_left(e))       return;
        e->hit |= FACING_RIGHT;
    }
    e->phase = 0x1D; e->next = 2;                                   /* A3C4 */
}

/* 0xA3CD  Blocked by a wall while walking: frames B/C alternate; press
 * down (falls if nothing below) for 16 frames (next bits 4-7), then one step
 * right (sic: tests link bit 3, which is never set), then an 8-step jump arc
 * (jump_r/jump_l indexed by next bits 5-7).  A blocked step on the way down
 * (index >= 6) flips the facing.  Ends with the uncurl animation. */
static void shell_wall_jump(struct enemy *e)
{
    u8 f = (e->phase + 1) & 0xF; if (f >= 0xD) f = 0xB; e->phase = f;   /* A3CD */
    if (!(e->link & 1)) {
        fight_step_down(e);                                         /* A3E3 */
        e->next += 0x10; if (e->next & 0xF0) return;
        e->link |= 1; return;                                       /* A3F3 */
    }
    if (!(e->link & 4)) { e->link |= 4; if (e->link & 8) fight_step_left(e); else fight_step_right(e); return; }   /* A3FE */
    u8 idx = (e->next >> 5) & 7;                                    /* A41E */
    e->next += 0x20;
    if (!(e->next & 0xE0)) { e->link = 0; e->next = 2; }            /* A433: arc finished */
    if (!fight_step_dir(((e->hit & FACING_RIGHT) ? jump_r : jump_l)[idx])) return;
    if ((e->next & 0xE0) >= 0xC0) e->hit ^= FACING_RIGHT;           /* A444..A451 */
}

/* ======================================================================== */
/* Class 1 — dividing green slime.  HP 16, contact 4, EXP 10.  Only magic  */
/* 3, 4, 7 (sources 4, 5, 8), the Enchantment sword (sword == 6) or a hit    */
/* landing on an odd animation frame damages it; any other hit is ignored   */
/* and instead makes it split: it claims a spare object (vec 31) and, when   */
/* its walk frame reaches 6, spawns a copy 2 cells ahead (hp 0 -> 16).      */
/* next bit0 = skip this update (fresh copy); flags 0x40 = spare claimed,    */
/* link = spare index.                                                       */
/* ======================================================================== */
static void slime_update(struct enemy *e)               /* 0xA466 */
{
    if (!e->hp) e->hp = 16;
    if (e->hit & HIT_STUN) {                                        /* A470 */
        u8 src = e->hit & 0x1F;
        if (src == 4 || src == 5 || src == 8 || (src == 1 && sword == 6) || (e->phase & 1)) { fight_take_damage(e); return; }   /* A47B..A4AC */
        e->hit &= ~HIT_STUN;                                        /* A4B1: shrug it off */
        if (!(e->flags & 0x40)) {
            struct enemy *s; u8 idx;
            find_spare_object(&s, &idx);                            /* A4BB: vec 31 */
            s->col = 0xFF00;                                        /* A4C2 */
            if (s->flags & 0x40) { s->flags &= ~0x40; MAP_OBJECTS[s->link].row = 0; }   /* A4C6 */
            s->row = 0x7F; e->link = idx; e->flags |= 0x40;         /* A4E1 */
        }
    }
    bool skip = e->next & 1; e->next &= ~1; if (skip) return;       /* A4EC */
    if (e->flags & 0x40) {                                          /* A56C: about to split */
        e->phase = (e->phase & 0xF0) | ((e->phase + 1) & 7);
        if ((e->phase & 7) == 6) { slime_split(e); return; }
    } else
        e->phase = (e->phase & 0xF0) | ((e->phase + 1) & 3);        /* A4FF */
    if (!fight_step_down(e)) return;                                /* A514 */
    e->phase -= 0x10; if (e->phase & 0xF0) return;                  /* A51C: one step every 4th frame */
    e->phase |= 0x40;
    bool level = (hero_map_row == e->row) || (((hero_map_row + 1) & 0x3F) == e->row);   /* A52B */
    if (level ? (e->rcol > 0x10) : !(e->hit & FACING_RIGHT)) {
        e->hit &= ~FACING_RIGHT; if (!fight_step_left(e)) return;   /* A54B */
    }
    e->hit |= FACING_RIGHT; if (!fight_step_right(e)) return;       /* A557 */
    e->hit &= ~FACING_RIGHT; fight_step_left(e);                    /* A563 */
}

/* 0xA584  Spawn the copy in the claimed record 2 cells ahead (behind when
 * facing right: col-2) if the 3x3 block of cells around that spot is
 * passable (A679); marker written directly into the ring, under_sprite[]
 * updated.  Parent frame 0x16, copy 0x17 (or vice versa).  If the copy's
 * index is above ours it gets next bit0 so it skips the update fight.bin
 * will still give it this frame. */
static void slime_split(struct enemy *e)
{
    struct enemy *s = &MAP_OBJECTS[e->link];
    u8 *p = ring_addr(e->row, e->rcol);
    if (!(e->hit & FACING_RIGHT)) {
        if ((s8)e->rcol < 0 || e->rcol >= 0x21 || !block3x3_free(p - 0x23)) return;   /* A5A5: (row-1, rcol+1).. */
        p += 2; s->col = wrap(e->col + 2); s->rcol = e->rcol + 2; e->phase = 0x16; s->phase = 0x17;   /* A5C1..A5F6 */
    } else {
        if ((s8)e->rcol < 0 || e->rcol < 3 || !block3x3_free(p - 0x27)) return;       /* A5FC: (row-1, rcol-3).. */
        p -= 2; s->col = wrap(e->col - 2); s->rcol = e->rcol - 2; e->phase = 0x17; s->phase = 0x16;
    }
    u8 old = *p; *p = 0x80 | e->link; under_sprite[e->link] = old;  /* A5C3 / A657 */
    s->type = e->type & 0x1F; s->row = e->row; s->flags = 0; s->hp = 0; s->next = 0;   /* A5CE..A663 */
    e->flags &= ~0x40;
    if (obj_index < e->link) s->next |= 1;                          /* A66B */
}
/* 0xA679  three rows x three cells from SI (after ring_wrap_up) all passable -> CF=0 */

/* ======================================================================== */
/* Classes 2, 3 — icicles.  Sword-immune, contact 80, EXP 0, drop "vanish". */
/* ======================================================================== */
/* 0xA6B1  Hang (frame 0) until the hero is roughly below (rcol 8..0x12,
 * i.e. up to 9 cells left of his body column), then with 25% chance per
 * frame start falling (frame 1); fall 1 row/frame; on landing set drop id 1
 * and die through vec 25 (no EXP). */
static void icicle_update(struct enemy *e)
{
    e->type |= 0x20;
    if (!(e->next & 1)) {
        if (e->rcol < 8 || e->rcol >= 0x13) return;                 /* A6BB */
        if (KRN_RANDOM() & 3) return;                               /* A6C8 */
        e->phase = 1; e->next |= 1; return;
    }
    if (!fight_step_down(e)) return;                                /* A6DB */
    e->flags = (e->flags & 0xF0) | 1; fight_enemy_killed(e);        /* A6E3 */
}

/* ======================================================================== */
/* Class 4 — spinning blade that crawls along surfaces.  HP 2, contact 80,  */
/* EXP 20.  next = heading state 0..7; two 8x5 tables of {dir, next state,   */
/* frame} (A756 for facing-left = wall on one side, A7CE = the other side). */
/* Each frame the 5 candidates are tried in order (turn toward the wall     */
/* first, then straight, then away); the first free one is taken.  A move   */
/* on the middle ("straight") candidate is immediately repeated -> 2 cells/  */
/* frame on straights.  If all 5 are blocked the facing bit flips (switches  */
/* table).  Only runs while rcol is 3..0x20.                                 */
/* ======================================================================== */
struct crawl_entry { u8 dir, next, frame; };
static const struct crawl_entry crawl_l[8][5] = /* A756 */ { {{6,2,1},{7,1,2},{0,0,0},{1,7,3},{2,6,1}}, /* ... state s: dirs (s+6..s+2)&7, next = (-dir)&7 */ };
static const struct crawl_entry crawl_r[8][5] = /* A7CE */ { {{6,6,0},{5,7,3},{4,0,0},{3,1,2},{2,2,0}}, /* ... state s: dirs (6-s..2-s)&7, next = (dir+4)&7 */ };

static void blade_update(struct enemy *e)               /* 0xA6F0 */
{
    if (!e->hp) e->hp = 2;
    if (e->hit & HIT_STUN) { fight_take_damage(e); return; }        /* A6FA */
    if (e->rcol < 3 || e->rcol >= 0x21) return;                     /* A705 */
    if (blade_step(e) == 2) blade_step(e);                          /* A713: straight -> second step */
}
/* 0xA71C  returns the candidate index taken (via CL = 5 - index); 0xFF = none */
static u8 blade_step(struct enemy *e)
{
    const struct crawl_entry *t = ((e->hit & FACING_RIGHT) ? crawl_r : crawl_l)[e->next];
    for (u8 i = 0; i < 5; i++)
        if (!fight_step_dir(t[i].dir)) { e->next = t[i].next; e->phase = t[i].frame; return i; }   /* A749 */
    e->hit ^= FACING_RIGHT; return 0xFF;                            /* A744 */
}

/* 0xA269 */
void ai_entry(struct enemy *e)
{
    static void (*const fn[5])(struct enemy *) = { shell_update, slime_update, icicle_update, icicle_update, blade_update };   /* A277 */
    fn[e->type & 0xF](e);
}
