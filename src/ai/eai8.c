/*
 * eai8.c — cavern 8 enemy AI (EAI8.BIN = ZELRES3[8], 1939 bytes @ BASE:A000).
 * Sprites ENP8.GRP (ZELRES3[63]).  Maps mp80..mp84: classes 0+1 (tall pairs),
 * 2, 3, 4.  Every class gives 255 EXP.
 *
 * Header:  [A000] = A2B9 entry   [A006] = A29F drop lists
 *   A008 exp[]     = { 255, 255, 255, 255, 255 }
 *   A010 contact[] = { 160, 160, 60, 80, 80 }
 *   drops: 0/1 A2A9 {11,11,11,11}, 2 A2AD {5,5,0,0}, 3 A2B1 {11,11,5,5}, 4 A2B5 {11,5,0,0}
 *   Frames: 0 A0B0/A0CE (6), 1 A0FB/A119 (6, lower), 2 A146/A16E (8), 3 A1A5/A1BE (5),
 *   4 A1E6 (4); dying 8 A0EC, 9 A137, A A196, B A1D7, C A1FA.
 * Dispatch A2C7: { A2D2, A2D1 (ret), A483, A538, A68F }.
 */
#include "ai_common.h"

/* 0xA75D  hero within 5 rows?  AL = 0xFF / facing bit, AH = |0x11 - rcol|, CF=1 facing him */
static u8 hero_dir5x(struct enemy *e, bool *facing, u8 *dx);
/* 0xA72B  same with range 8 rows and AH = 0x10 - rcol */
static u8 hero_dir8x(struct enemy *e, bool *facing, u8 *dx);
/* 0xA379 / 0xA3FE  tall step right / left (as eai2).  There is NO tall step-down: class 0 ignores gravity. */
/* 0xA352  tall_sync (lower.phase = phase, lower facing = ours);  0xA365 tall hit via own source */

/* ======================================================================== */
/* Class 0 (+1) — tall charger.  HP 100, contact 160, EXP 255.  Never      */
/* falls.  Idles (frame advancing every 2nd frame) facing the hero; when     */
/* facing him within 5 rows and 14 columns it charges 16 cells at 1 cell     */
/* per frame (next&1, link counts), stopping early at a wall.                */
/* ======================================================================== */
static void tallcharger_update(struct enemy *e)         /* 0xA2D2 */
{
    bool facing; u8 dx;
    if (!e->hp) e->hp = 100;
    if (e->hit & HIT_STUN) { u8 a = (e->hit & 0xBF) | 0x20; e->hit = a; e[1].hit = a | 0x60; fight_take_damage(e); return; }   /* A365 */
    e[1].hit &= ~0x40;
    if (e->next & 1) {                                              /* A319 */
        if (++e->link == 16) { e->next &= ~1; tall_sync(e); return; }
        if ((e->hit & FACING_RIGHT) ? tall_step_right(e) : tall_step_left(e)) e->next &= ~1;   /* wall: stop */
        else anim6(e);
        tall_sync(e); return;
    }
    if ((e->phase += 0x80) carried) anim6(e);                       /* A2EF */
    e->link = 0;
    u8 f = hero_dir5x(e, &facing, &dx);                             /* A2FC */
    if (facing) { if (dx < 15) e->next |= 1; }
    else if (f != 0xFF) e->hit = (e->hit & 0x7F) | f;
    tall_sync(e);
}
/* 0xA343 */ static void anim6(struct enemy *e) { if (++e->phase >= 6) e->phase = 0; }

/* ======================================================================== */
/* Class 2 — walker.  HP 48, contact 60, EXP 255.  Walks in its facing      */
/* direction one cell every 2nd frame, turning at walls; when it faces a     */
/* hero within 5 rows it moves every frame (next = 1), re-checking every     */
/* 4th frame; a wall then just stops the rush.                               */
/* ======================================================================== */
static void walker_update(struct enemy *e)              /* 0xA483 */
{
    bool facing; u8 dx;
    if (!e->hp) e->hp = 0x30;
    if (e->hit & HIT_STUN) { fight_take_damage(e); return; }
    if (!fight_step_down(e)) return;                                /* A498 */
    if (!(e->next & 1)) {
        u8 f = hero_dir5x(e, &facing, &dx); e->next = facing;      /* A4A6 */
        if (f != 0xFF) e->hit = (e->hit & 0x7F) | f;
        if ((e->phase += 0x80) did not carry) return;               /* A4BB */
        e->phase = (e->phase + 1) & 7;
        if ((e->hit & FACING_RIGHT) ? fight_step_right(e) : fight_step_left(e)) { e->next = 0; e->hit ^= FACING_RIGHT; }   /* A4E7 */
        return;
    }
    if (!(--e->link & 3)) {                                         /* A4F0 */
        u8 f = hero_dir5x(e, &facing, &dx); e->next = facing;
        if (f != 0xFF) e->hit = (e->hit & 0x7F) | f;
    }
    e->phase = (e->phase + 1) & 7;                                  /* A50E */
    if ((e->hit & FACING_RIGHT) ? fight_step_right(e) : fight_step_left(e)) e->next = 0;
}

/* ======================================================================== */
/* Class 3 — sentry gunner.  HP 64, contact 80, EXP 255.  Stands (3-frame   */
/* idle every 2nd frame); faces a hero within 5 rows and, 1/8 per frame,     */
/* fires (next 4: frame 3, after 3 frames frame 4 + shot, then next 2 ->     */
/* idle).  Every 6 frames 25% to shuffle: 8 animation frames then one step,  */
/* away from a ledge (A5A7, as the cavern-2 slime).  Note the left shot's    */
/* damage byte is 1 (A679) where the right one has 80 — original data bug.   */
/* ======================================================================== */
static struct shot gun_r = { 0, 0, 0x2A, 0, 18, 0, 80 };   /* A666 */
static struct shot gun_l = { 0, 0, 0x2B, 0, 18, 4, 1  };   /* A673 (sic) */

static void gunner_update(struct enemy *e)              /* 0xA538 */
{
    bool facing; u8 dx;
    if (!e->hp) e->hp = 0x40;
    if (e->hit & HIT_STUN) { fight_take_damage(e); return; }
    if (!fight_step_down(e)) return;
    if (e->next & 4) {                                              /* A620 */
        e->phase = 3;
        if (++e->link != 3) return;
        e->phase = 4;
        gun_l.col = e->rcol; gun_r.col = e->rcol + 1; gun_l.row = gun_r.row = e->row & 0x3F;
        fight_shot_spawn((e->hit & FACING_RIGHT) ? &gun_r : &gun_l);
        e->next = (e->next & ~4) | 2; e->link = 0; return;
    }
    if (!(e->next & 1)) {                                           /* A564 */
        u8 f = hero_dir5x(e, &facing, &dx);                         /* A5FE */
        if (f != 0xFF) { e->hit = (e->hit & 0x7F) | f; if (!(KRN_RANDOM() & 7)) { e->next |= 4; e->link = 0; } }
        if ((e->phase += 0x80) did not carry) return;
        if (anim3(e) && !(KRN_RANDOM() & 3)) { e->next = 1; e->link = 0; }   /* A56E */
        return;
    }
    if (e->next & 2) { e->next &= ~1; e->phase = 0; return; }       /* A5F5 */
    anim3(e);
    if (++e->link != 8) return;                                     /* A590 */
    e->next |= 2;
    u8 *p = ring_addr(e->row, e->rcol);
    if (!(KRN_RANDOM() & 0x80)) { if (cell_passable_ai(*ring_wrap_down(p + 0x4A))) fight_step_left(e);  else fight_step_right(e); }
    else                        { if (cell_passable_ai(*ring_wrap_down(p + 0x47))) fight_step_right(e); else fight_step_left(e);  }
}
/* 0xA680  phase = (phase+1) mod 3; returns true when it wrapped to 0 */
static bool anim3(struct enemy *e);

/* ======================================================================== */
/* Class 4 — flying chaser.  HP 96, contact 80, EXP 255.  The cavern-6     */
/* fish's roaming logic (path tables A71B/A723, vertical tracking, re-face   */
/* every 8 steps) run every 2nd frame (link bit 7 toggles); no flee state.  */
/* ======================================================================== */
static const u8 path_r[8] = { 0, 0, 1, 0, 0, 0, 7, 0 }, path_l[8] = { 4, 4, 3, 4, 4, 4, 5, 4 };   /* A71B / A723 */
static void flyer_update(struct enemy *e)               /* 0xA68F */
{
    bool facing; u8 dx;
    if (!e->hp) e->hp = 0x60;
    if (e->hit & HIT_STUN) { fight_take_damage(e); return; }
    e->phase = (e->phase + 1) & 3;
    if ((e->link += 0x80) did not carry) return;                    /* A6AB */
    u8 f = hero_dir8x(e, &facing, &dx);
    if (!facing) {
        if (e->next & 0x70) goto move;
        e->hit = (e->hit & 0x7F) | (f != 0xFF ? f : ((KRN_RANDOM() << 1) & 0x80));
    }
    if ((s8)(hero_map_row - e->row) < 0) fight_step_up(e); else fight_step_down(e);   /* A6DC */
move:
    e->next += 0x10;
    if (fight_step_dir(((e->hit & FACING_RIGHT) ? path_r : path_l)[(e->next >> 4) & 7])) e->hit ^= FACING_RIGHT;
}

/* 0xA2B9 */
void ai_entry(struct enemy *e)
{
    static void (*const fn[5])(struct enemy *) = { tallcharger_update, tall_lower_noop, walker_update, gunner_update, flyer_update };   /* A2C7 */
    fn[e->type & 0xF](e);
}
static void tall_lower_noop(struct enemy *e) { }        /* 0xA2D1 */
