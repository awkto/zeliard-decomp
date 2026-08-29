/*
 * eai6.c — cavern 6 enemy AI (EAI6.BIN = ZELRES3[6], 2514 bytes @ BASE:A000).
 * Sprites ENP6.GRP (ZELRES3[61]).  Maps mp60/mp61/mp62: classes 0+1 (tall
 * pairs), 2, 3, 4.
 *
 * Header:  [A000] = A3F9 entry   [A006] = A3DF drop lists
 *   A008 exp[]     = { 100, 100, 50, 50, 0 }
 *   A010 contact[] = { 80, 80, 40, 40, 80 }
 *   drops: 0/1 A3E9 {11,11,11,11}, 2 A3ED {5,5,5,5}, 3 A3F1 {5,5,0,0}, 4 A3F5 {0,0,0,0}
 *   Frames: 0 A0B0/A100 (16; 1,3,12,14 are empty = invisible; 5-11 attack), 1 A15F/A1AF
 *   (16, lower half), 2 A20E/A25E (16: 0-3 fly, 8-11 flee, 12-15 recover),
 *   3 A2BD/A2E5 (8: 0-3 walk, 4 charge, 5 bounce), 4 A31C (4: 0 rock, 1-3 crumble);
 *   dying 8 A150, 9 A1FF, A A2AE, B A30D, C A330.
 * Dispatch A407: { A412, A411 (ret), A6B8, A857, A95F }.
 */
#include "ai_common.h"

/* 0xA527  hero within 4 rows? (as eai5 hero_dir4)    0xA828  within 8 rows? */
static u8 hero_dir4(struct enemy *e, bool *f);
static u8 hero_dir8(struct enemy *e, bool *f);
/* 0xA660 / 0xA556 / 0xA5DB  tall (2x4) step down / right / left, as eai2 */

/* ======================================================================== */
/* Class 0 (+1) — tall ghost.  HP 48, contact 80, EXP 100.  Only the sword */
/* (source 1) hurts it; magic hits are discarded (A42E).  While walking it  */
/* is invisible (frame 1 is empty), sword-immune and harmless (type|0x60);   */
/* it materialises to shoot.  link counts walk frames.                       */
/* ======================================================================== */
static struct shot ghost_shot_r = { 0, 0, 0x63, 0, 20, 0, 20 };   /* A4DD: right, 20 cells, damage 20 */
static struct shot ghost_shot_l = { 0, 0, 0x63, 0, 20, 4, 20 };   /* A4EA */

static void ghost_update(struct enemy *e)               /* 0xA412 */
{
    bool facing;
    if (!e->hp) e->hp = 0x30;
    if (e->hit & HIT_STUN) {
        if ((e->hit & 0x1F) == 1) { e->hit = 0x21; e[1].hit = 0x61; fight_take_damage(e); return; }   /* A4F7 */
        e->hit &= 0x9F;                                             /* A42E: ignore magic */
    }
    e[1].hit &= ~0x40;                                              /* A432 */
    if (!tall_step_down(e)) return;                                 /* A436 */
    if (e->next & 1) {                                              /* A48D: attack, frames 1..15 */
        e->phase = (e->phase + 1) & 0xF;
        if (!e->phase) { e->next &= ~1; e->phase = 1; e->type |= 0x60; }   /* A496: vanish again */
        else if (e->phase >= 4) {
            e->type &= 0x1F;                                        /* A4AA: visible and vulnerable */
            if (e->phase == 8) {                                    /* A4AE: fire from (rcol(+1), row+1) */
                ghost_shot_l.col = e->rcol; ghost_shot_r.col = e->rcol + 1;
                ghost_shot_l.row = ghost_shot_r.row = e->row + 1;
                fight_shot_spawn((e->hit & FACING_RIGHT) ? &ghost_shot_r : &ghost_shot_l);
            }
        }
        ghost_sync(e); return;
    }
    hero_dir4(e, &facing);                                          /* A442 */
    if (facing && (e->link & 0xF0)) { e->link = 0; e->phase = 0; e->next |= 1; ghost_sync(e); return; }   /* A479: after >= 16 walk frames */
    e->link++; e->phase = 1; e->type |= 0x60;                       /* A447 */
    if (KRN_RANDOM() & 1) { if (!tall_step_left(e))  e->hit &= ~FACING_RIGHT; }   /* A46A: random walk, 1 cell/frame */
    else                  { if (!tall_step_right(e)) e->hit |= FACING_RIGHT;  }   /* A45B */
    ghost_sync(e);
}
/* 0xA508  lower.phase = phase; lower.type bits 5-6 = ours; lower facing = ours */
static void ghost_sync(struct enemy *e);

/* ======================================================================== */
/* Class 2 — flying fish.  HP 16, contact 40, EXP 50.  No gravity.          */
/* next: bits 4-6 step counter into the 8-entry path tables; 1 = just hit,   */
/* 2 = fleeing, 4 = recovering.                                              */
/* ======================================================================== */
static const u8 path_r[8] = { 0, 0, 1, 0, 0, 0, 7, 0 };   /* A75E: R,R,RU,R,R,R,RD,R */
static const u8 path_l[8] = { 4, 4, 3, 4, 4, 4, 5, 4 };   /* A766 */

static void fish_update(struct enemy *e)                /* 0xA6B8 */
{
    bool facing;
    if (!e->hp) e->hp = 16;
    if (e->hit & HIT_STUN) { e->phase = 3; e->next = 1; fight_take_damage(e); return; }   /* A6C8 */
    if (e->next & 2) { fish_flee(e); return; }                      /* A78D */
    if (e->next & 1) {                                              /* A76E: hit reaction, immune, frames 3..7 */
        e->type |= 0x60;
        e->phase = (e->phase + 1) & 7;
        if (e->phase >= 7) { e->phase = 8; e->link = 0; e->next = 2; }
        return;
    }
    if (e->next & 4) {                                              /* A815: recover, frames C..F */
        e->phase = (e->phase + 1) & 0xF;
        if (!e->phase) { e->next = 0; e->type &= 0x1F; }
        return;
    }
    u8 f = hero_dir8(e, &facing);                                   /* A6F0 */
    if (!facing) {
        if (e->next & 0x70) goto move;                              /* only every 8th step: */
        if (f != 0xFF) e->hit = (e->hit & 0x7F) | f;                /* face the hero */
        else e->hit = (e->hit & 0x7F) | ((KRN_RANDOM() << 1) & 0x80);   /* or a random way */
    }
    if ((s8)(hero_map_row - e->row) < 0) fight_step_up(e); else fight_step_down(e);   /* A718: track vertically */
move:
    e->phase = (e->phase + 1) & 3;                                  /* A72C */
    e->next += 0x10;
    if (fight_step_dir(((e->hit & FACING_RIGHT) ? path_r : path_l)[(e->next >> 4) & 7])) e->hit ^= FACING_RIGHT;   /* A751 */
}

/* 0xA78D  Flee for 15 frames (frames 8..B): same as the roaming move but
 * the facing and the vertical step are AWAY from the hero; then recover. */
static void fish_flee(struct enemy *e)
{
    bool facing;
    if (++e->link >= 15) { e->phase = 0xC; e->next = 4; return; }   /* A790 */
    u8 f = hero_dir8(e, &facing);
    if (facing) {
        if (!(e->next & 0x70)) e->hit = (e->hit & 0x7F) | (f == 0xFF ? ((KRN_RANDOM() << 1) & 0x80) : (f ^ 0x80));   /* A7A5 */
        else goto move;
    }
    if ((s8)(hero_map_row - e->row) < 0) fight_step_down(e); else fight_step_up(e);   /* A7C2 */
move:
    e->phase = ((e->phase + 1) & 3) | 8;
    e->next += 0x10;
    if (fight_step_dir(((e->hit & FACING_RIGHT) ? path_r : path_l)[(e->next >> 4) & 7])) e->hit ^= FACING_RIGHT;
}

/* ======================================================================== */
/* Class 3 — charging beast.  HP 8, contact 40, EXP 50.  next: 1 charging, */
/* 2 bouncing off a wall, 4 may climb.  link: walk step counter / charge    */
/* frame counter.                                                            */
/* ======================================================================== */
static void beast_update(struct enemy *e)               /* 0xA857 */
{
    bool facing;
    if (!e->hp) e->hp = 8;
    if (e->hit & HIT_STUN) { fight_take_damage(e); return; }
    if (!(e->next & 1)) {                                           /* A872: walking */
        if (!fight_step_down(e)) return;
        hero_dir8(e, &facing);
        if (facing) { e->next = 1; e->link = 0; return; }           /* A8BA: charge */
        if ((e->phase += 0x80) did not carry) return;               /* one step every 2nd frame */
        e->phase = (e->phase + 1) & 0xF3;
        if ((e->hit & FACING_RIGHT) ? fight_step_right(e) : fight_step_left(e)) e->hit ^= FACING_RIGHT;   /* A893: wall: turn */
        if (!(--e->link & 0xF)) e->hit ^= FACING_RIGHT;             /* A8AB: turn every 16 steps */
        return;
    }
    if (e->next & 2) {                                              /* A934: bounce, frame 5 for 8 frames */
        if (!(++e->phase & 7)) { e->next &= ~2; e->phase = 4; }
        return;
    }
    if (hero_dir8(e, &facing) == 0xFF) goto recheck;                /* A8C9 */
    e->phase = 4;                                                   /* charge: 2 cells per frame */
    if (e->hit & FACING_RIGHT) { fight_step_right(e); if (fight_step_right(e) && beast_climb(e)) goto bounce; }   /* A8ED */
    else                       { fight_step_left(e);  if (fight_step_left(e)  && beast_climb(e)) goto bounce; }   /* A8DA */
    e->link++;                                                      /* A8FE */
    if ((e->link & 0xF) == 0xF) beast_bounce(e);                    /* after 15 charge frames */
    if (e->link & 0x1F) return;
recheck:
    hero_dir8(e, &facing);                                          /* A914 */
    if (!facing) { e->phase = 0; e->next = 0; e->link = 0; }        /* A91A: back to walking */
    return;
bounce: beast_bounce(e);
}
/* 0xA947  blocked ahead: without next&4 try to drop a row (CF=1 = floor);
 * with it try to step up, and set next&4 when that fails too. */
static bool beast_climb(struct enemy *e)
{
    if (!(e->next & 4)) return fight_step_down(e);
    if (!fight_step_up(e)) return false;
    e->next |= 4; return true;
}
/* 0xA927 */ static void beast_bounce(struct enemy *e) { e->next |= 2; e->hit ^= FACING_RIGHT; e->phase = 5; }

/* ======================================================================== */
/* Class 4 — falling rock.  Sword-immune, contact 80, EXP 0.  Like the      */
/* cavern-4 icicle (trigger rcol 8..0x12, 25%/frame) but plays sound 0x21   */
/* on landing (if within 19 rows of the window) and crumbles through frames */
/* 1..3 for 8 frames before vanishing (drop id 1, vec 25).                   */
/* ======================================================================== */
static void rock_update(struct enemy *e)                /* 0xA95F */
{
    e->type |= 0x20;
    if (e->next & 2) {                                              /* A9B4 */
        if ((e->phase += 0x80) did not carry) return;
        if (!(e->phase = (e->phase + 1) & 3)) { e->flags = (e->flags & 0xF0) | 1; fight_enemy_killed(e); }
        return;
    }
    if (e->next & 1) {                                              /* A98C */
        if (!fight_step_down(e)) return;
        e->next |= 2; e->phase = 1;
        if (((e->row - (scroll_row - 1)) & 0x3F) < 0x13) sfx_request = 0x21;   /* A99C */
        return;
    }
    if (e->rcol < 8 || e->rcol >= 0x13) return;                     /* A96F */
    if (!(KRN_RANDOM() & 3)) e->next |= 1;                          /* A97D */
}

/* 0xA3F9 */
void ai_entry(struct enemy *e)
{
    static void (*const fn[5])(struct enemy *) = { ghost_update, tall_lower_noop, fish_update, beast_update, rock_update };   /* A407 */
    fn[e->type & 0xF](e);
}
static void tall_lower_noop(struct enemy *e) { }        /* 0xA411 */
