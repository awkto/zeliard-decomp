/*
 * eai5.c — cavern 5 (water/currents) enemy AI (EAI5.BIN = ZELRES3[5], 2551 bytes).
 * Sprites ENP5.GRP (ZELRES3[60]).  Maps mp50/mp51: classes 0+1 (tall pairs),
 * 0 alone, 2, 3, 4.  Classes 2, 3, 4 call vec 32 every update, so they are
 * swept two cells per frame by current/updraft tiles.
 *
 * Header:  [A000] = A337 entry   [A006] = A321 drop lists
 *   A008 exp[]     = { 50, 50, 20, 10, 10 }
 *   A010 contact[] = { 40, 40, 20, 20, 10 }
 *   drops: 0/1 A32B {11,5,5,5}, 2 A32F {5,4,5,4}, 3/4 A333 {5,0,5,0}
 *   Frames: 0 A0B0/A0EC (12: 0-7 walk, 8-B spit), 1 A137/A173 (12, lower half),
 *   2 A1BE (8: 0-3 slime, 4-7 growing), 3 A1F5/A213 (6: 0-4 turn, 5 charge),
 *   4 A240 (8: 0-3 float, 4-5 dive); dying 8 A128, 9 A1AF, A A1E6, B A231, C A268.
 * Dispatch A345: { A350, A34F (ret), A5F1, A812, A91A }.
 */
#include "ai_common.h"

/* 0xA5C2  hero within 4 rows?  AL = 0xFF / facing bit; CF=1 facing him */
static u8 hero_dir4(struct enemy *e, bool *facing_hero);
/* 0xA449  lower.phase = phase; lower facing = ours (as eai2 tall_sync) */
static void tall_sync(struct enemy *e);
/* 0xA56A / 0xA460 / 0xA4E5  tall (2x4) step down / right / left, as eai2 A653/A549/A5CE */

/* ======================================================================== */
/* Class 0 (+1) — tall 2x4 spitter.  HP 24, contact 40, EXP 50.  Only the   */
/* upper record takes hits (A363 clears the lower half's pending bit before  */
/* fight.bin can convert it, so hits on the lower half are lost).            */
/* ======================================================================== */
static struct shot spit_r = { 0, 0, 0xB1, 0, 18, 0, 40 };   /* A41B */
static struct shot spit_l = { 0, 0, 0xB1, 0, 20, 4, 40 };   /* A428 */

static void tallspit_update(struct enemy *e)            /* 0xA350 */
{
    bool facing;
    if (!e->hp) e->hp = 0x18;
    if (e->hit & HIT_STUN) {                                        /* A35A -> A435 */
        u8 a = (e->hit & 0xBF) | 0x20; e->hit = a; e[1].hit = a | 0x60;
        fight_take_damage(e); return;
    }
    e[1].hit &= ~0x40;                                              /* A363 */
    if (!tall_step_down(e)) return;                                 /* A367 */
    if (e->next & 1) {                                              /* A3D2: spitting, frames 8..B every 2nd frame */
        if ((e->phase += 0x80) carried) {
            e->phase++;
            if ((e->phase & 0xF) == 0xB) {                          /* A3F2: fire from (rcol(+1), row+1) */
                spit_l.col = e->rcol; spit_r.col = e->rcol + 1; spit_l.row = spit_r.row = e->row + 1;
                fight_shot_spawn((e->hit & FACING_RIGHT) ? &spit_r : &spit_l);
            } else if ((e->phase & 0xF) == 0xC) { e->next &= ~1; e->phase = 3; }
        }
        tall_sync(e); return;
    }
    hero_dir4(e, &facing);                                          /* A373 */
    if (facing) {
        if (!(KRN_RANDOM() & 0xC0) && (e->phase & 3) == 3) { e->next |= 1; e->phase = 8; tall_sync(e); return; }   /* A3B6: 25% per matching frame */
    } else if ((e->phase += 0x80) did not carry) { tall_sync(e); return; }   /* A378: animate every 2nd frame */
    e->phase = (e->phase + 1) & 7;                                  /* A381 */
    if (!(e->phase & 3)) {                                          /* one step per 4 animation frames */
        if (e->rcol <= 0x10) { if (!tall_step_right(e)) e->hit |= FACING_RIGHT; }   /* A391 */
        else                 { if (!tall_step_left(e))  e->hit &= ~FACING_RIGHT; }
    }
    tall_sync(e);
}

/* ======================================================================== */
/* Class 2 — dividing slime (as eai4 class 1, HP 16, contact 20, EXP 20).   */
/* Damaged only by magic 3/4/7 or the Enchantment sword; any other hit       */
/* spawns a copy at once (A641): the copy starts immune and harmless         */
/* (type | 0x60) with frames 4..7, then becomes a normal slime (A7FF).       */
/* ======================================================================== */
static void slime_update(struct enemy *e)               /* 0xA5F1 */
{
    if (!e->hp) e->hp = 16;
    if (e->hit & HIT_STUN) {
        u8 src = e->hit & 0x1F;
        if (src == 4 || src == 5 || src == 8 || (src == 1 && sword == 6)) { fight_take_damage(e); return; }   /* A604 */
        e->hit &= ~HIT_STUN;
        if (!(e->next & 2)) slime_split(e);                         /* A638 */
    }
    fight_ride_current(e);                                          /* A780: vec 32 */
    bool skip = e->next & 1; e->next &= ~1; if (skip) return;
    if (e->next & 2) {                                              /* A7FF: growing copy */
        e->phase = (e->phase + 1) & 7;
        if (!e->phase) { e->next &= ~2; e->type &= 0x9F; }
        return;
    }
    e->phase = (e->phase + 1) & 0xF3;                               /* A797 */
    if (!fight_step_down(e)) return;
    e->phase -= 0x10; if (e->phase & 0xF0) return;                  /* A7A9: one step every 4th frame */
    e->phase |= 0x40;
    bool level = (hero_map_row == e->row) || (((hero_map_row + 1) & 0x3F) == e->row);   /* A7BE */
    if (level ? (e->rcol > 0x11) : !(e->hit & FACING_RIGHT)) { e->hit &= ~FACING_RIGHT; if (!fight_step_left(e)) return; }
    e->hit |= FACING_RIGHT; if (!fight_step_right(e)) return;
    e->hit &= ~FACING_RIGHT; fight_step_left(e);
}
/* 0xA641  spare = vec 31; facing left: needs rcol < 0x20 and cells
 * (row-1..row+1, rcol+2..rcol+3) passable, copy at col+2; facing right:
 * rcol >= 4, cells (row-1..row+1, rcol-2..rcol-1), copy at col-2.  Marker
 * 0x80|idx written at (row, rcol+-2), old cell -> under_sprite[idx].
 * Copy: type | 0x60, hit = our facing, phase 4, flags = ours, hp 0, next 2
 * (| 1 when its index is above ours), link 0. */
static void slime_split(struct enemy *e);

/* ======================================================================== */
/* Class 3 — turning charger.  HP 8, contact 20, EXP 10.  next: 0 walk,     */
/* 2 look around, 4 charge.  Frames 0 (facing left) .. 4 (facing right)     */
/* form a turn animation, 5 = charging.                                      */
/* ======================================================================== */
static void charger_update(struct enemy *e)             /* 0xA812 */
{
    bool facing;
    if (!e->hp) e->hp = 8;
    if (e->hit & HIT_STUN) { fight_take_damage(e); return; }
    fight_ride_current(e);                                          /* A827 */
    if (e->next & 4) {                                              /* A8DB: charge */
        if (++e->link < 5) goto walk_step;                          /* first 4 frames at 1 cell/frame */
        e->phase = 5;
        if (e->hit & FACING_RIGHT) { fight_step_right(e); if (!fight_step_right(e)) return; e->next = 2; e->phase = 4; }
        else                       { fight_step_left(e);  if (!fight_step_left(e))  return; e->next = 2; e->phase = 0; }
        return;                                                     /* 2 cells/frame until a wall */
    }
    if (!fight_step_down(e)) return;                                /* A835 */
    if (e->next & 2) {                                              /* A843: look around: phase ping-pongs 0..4 */
        u8 f = e->phase & 7;
        if (f == 0) e->next &= ~1; if (f == 4) e->next |= 1;
        if (e->next & 1) e->phase--; else e->phase++;
        f = e->phase & 7;
        if (f == 0) e->hit &= ~FACING_RIGHT; else if (f == 4) e->hit |= FACING_RIGHT; else return;
        hero_dir4(e, &facing);                                      /* A87A */
        if (facing) { e->next = 4; e->link = 0; return; }
        if (KRN_RANDOM() & 0x80) { e->next = 0; e->link = 0; }
        return;
    }
    u8 r = hero_dir4(e, &facing);                                   /* A89B */
    if (facing) { e->next = 4; e->link = 0; return; }
    e->link++;
    if (!(r & 7)) e->next = 2;                                      /* hero near but behind: look around */
    if ((e->phase += 0x80) did not carry) return;                   /* A8B4: one step every 2nd frame */
walk_step:
    if (e->hit & FACING_RIGHT) { if (fight_step_right(e)) e->next = 2; }   /* A8CE */
    else                       { if (fight_step_left(e))  e->next = 2; }   /* A8C1 */
}

/* ======================================================================== */
/* Class 4 — diving floater.  HP 8, contact 10, EXP 10.  Frames 0-3 float,  */
/* 4-5 dive.  next: 1 diving, 2 rising.                                      */
/* ======================================================================== */
/* 0xA91A  Floats up (one step up every frame) and drifts toward the hero's
 * column one cell every 2nd frame; when its column is 0x10..0x12 (over the
 * hero) it dives 2 rows/frame until it lands, then rises 2 rows/frame
 * (U + RU/LU toward the hero, or U + U) until blocked, then floats again. */
static void diver_update(struct enemy *e)
{
    if (!e->hp) e->hp = 8;
    if (e->hit & HIT_STUN) { fight_take_damage(e); return; }
    fight_ride_current(e);                                          /* A92F */
    if (e->next & 1) {                                              /* A993: dive */
        if ((e->phase & 7) < 5) { e->phase++; return; }
        fight_step_down(e); if (!fight_step_down(e)) return;
        e->phase = (e->phase + 1) & 7; if (!e->phase) e->next = 2;  /* A9AD */
        return;
    }
    if (e->next & 2) {                                              /* A9BC: rise */
        fight_step_up(e);
        if (!((e->rcol <= 0x10) ? fight_step_right_up(e) : fight_step_left_up(e))) return;
        if (!fight_step_up(e)) return;
        e->next &= ~2; return;
    }
    if (e->rcol > 0xF && e->rcol <= 0x12) { e->next |= 1; e->phase = 4; }   /* A940 */
    else e->phase = (e->phase & 0xF0) | ((e->phase + 1) & 3);        /* A958 */
    fight_step_up(e);                                               /* A966 */
    if ((e->phase += 0x80) did not carry) return;
    if (e->rcol <= 0x10) { if (fight_step_right(e)) fight_step_left(e); }   /* A972 */
    else                 { if (fight_step_left(e))  fight_step_right(e); }
}

/* 0xA337 */
void ai_entry(struct enemy *e)
{
    static void (*const fn[5])(struct enemy *) = { tallspit_update, tall_lower_noop, slime_update, charger_update, diver_update };   /* A345 */
    fn[e->type & 0xF](e);
}
static void tall_lower_noop(struct enemy *e) { }        /* 0xA34F */
