/*
 * eai7.c — cavern 7 (heat) enemy AI (EAI7.BIN = ZELRES3[7], 2255 bytes @ BASE:A000).
 * Sprites ENP7.GRP (ZELRES3[62]).  Maps mp70/mp71/mp72: classes 0+1 and 2+3
 * are tall (2x4) pairs, class 4 is a normal 2x2 sprite.
 *
 * Header:  [A000] = A2F1 entry   [A006] = A2DB drop lists
 *   A008 exp[]     = { 80, 80, 200, 200, 50 }
 *   A010 contact[] = { 80, 80, 80, 80, 40 }
 *   drops: 0/1 A2E5 {11,11,11,5}, 2/3 A2E9 {11,11,11,5}, 4 A2ED {11,5,5,0}
 *   Frames: 0 A0B0/A0D8 (8), 1 A10F/A137 (8, lower), 2 A16E/A196 (8), 3 A1CD/A1F5
 *   (8, lower), 4 A22C/A240 (4); dying 8 A100, 9 A15F, A A1BE, B A21D, C A254.
 * Dispatch A2FF: { A30A, A309 (ret), A639, A638 (ret), A749 }.
 */
#include "ai_common.h"

/* 0xA609  hero within 5 rows? (eai2 hero_dir5)   0xA882  within 6 rows? (eai1 hero_dir6) */
/* 0xA59D / 0xA493 / 0xA518  tall step down / right / left (as eai2)        */
/* 0xA47A / 0xA732  tall_sync (lower.phase = phase, lower facing = ours)     */
/* 0xA5F5  tall hit via the lower record's source bits (as eai2 tall_take_hit) */
/* 0xA71E  tall hit via own source bits (as eai5)                            */

/* ======================================================================== */
/* Class 0 (+1) — tall ranged walker: the cavern-2 "plant" (eai2 A384)     */
/* transplanted: keeps a random distance 7..10 ([A491]/[A492], initially    */
/* 8/8) from a hero within 5 rows, walks every 2nd frame otherwise, turns    */
/* when he is behind, fires when at range (next |= 1, frames 4..7, shot at   */
/* phase 6, done at 8) or, 50%, when cornered (next |= 3).  Both cases use   */
/* the same straight shot here.  HP 16, contact 80, EXP 80.                  */
/* ======================================================================== */
static struct shot fire_r = { 0, 0, 0x30, 0, 20, 0, 40 };   /* A460: right, 20 cells, damage 40 */
static struct shot fire_l = { 0, 0, 0x2F, 0, 20, 4, 40 };   /* A46D */
static void tallwalker_update(struct enemy *e);         /* 0xA30A  = eai2 plant_update with the shot below */
static void tallwalker_attack(struct enemy *e)          /* 0xA41E */
{
    e->phase++;
    if (e->phase == 8) { e->next &= ~3; e->phase = 0; }
    else if (e->phase == 6) {                                       /* A437: from (rcol(+1), row+1) */
        fire_l.col = e->rcol; fire_r.col = e->rcol + 1; fire_l.row = fire_r.row = e->row + 1;
        fight_shot_spawn((e->hit & FACING_RIGHT) ? &fire_r : &fire_l);
    }
    tall_sync(e);
}

/* ======================================================================== */
/* Class 2 (+3) — tall spitter: the cavern-5 tall spitter (eai5 A350) with  */
/* HP 64, contact 80, EXP 200.  Walks toward the hero's column one cell per  */
/* 4 animation frames (one animation frame per 2 game frames); when facing   */
/* a hero within 5 rows, 25% per odd frame to attack: frames 4.. every 2nd   */
/* frame, shot at phase&7 == 6, back to walking at 0.  Only the upper half   */
/* takes hits (A64C clears the lower half's pending bit).                    */
/* ======================================================================== */
static struct shot spit_r = { 0, 0, 0x32, 0, 20, 0, 40 };   /* A704 */
static struct shot spit_l = { 0, 0, 0x31, 0, 20, 4, 40 };   /* A711 */
static void tallspit_update(struct enemy *e)            /* 0xA639 */
{
    bool facing;
    if (!e->hp) e->hp = 0x40;
    if (e->hit & HIT_STUN) { u8 a = (e->hit & 0xBF) | 0x20; e->hit = a; e[1].hit = a | 0x60; fight_take_damage(e); return; }   /* A71E */
    e[1].hit &= ~0x40;
    if (!tall_step_down(e)) return;
    if (e->next & 1) {                                              /* A6BB */
        if ((e->phase += 0x80) carried) {
            e->phase++;
            if ((e->phase & 7) == 6) {
                spit_l.col = e->rcol; spit_r.col = e->rcol + 1; spit_l.row = spit_r.row = e->row + 1;
                fight_shot_spawn((e->hit & FACING_RIGHT) ? &spit_r : &spit_l);
            } else if (!(e->phase & 7)) { e->next &= ~1; e->phase = 3; }
        }
        tall_sync(e); return;
    }
    hero_dir5(e, &facing);
    if (facing) { if (!(KRN_RANDOM() & 0xC0) && (e->phase & 1)) { e->next |= 1; e->phase = 4; tall_sync(e); return; } }
    else if ((e->phase += 0x80) did not carry) { tall_sync(e); return; }
    e->phase = (e->phase + 1) & 3;
    if (!(e->phase & 1)) {
        if (e->rcol <= 0x10) { if (!tall_step_right(e)) e->hit |= FACING_RIGHT; }
        else                 { if (!tall_step_left(e))  e->hit &= ~FACING_RIGHT; }
    }
    tall_sync(e);
}

/* ======================================================================== */
/* Class 4 — fast hedgehog: the cavern-1 hedgehog (eai1 A517) at 2 cells   */
/* per frame, without the resting state.  HP 8, contact 40, EXP 50.         */
/* next: 2 chasing, 8 gap jump, 0x10 wall jump, bits 5-7 jump step.          */
/* ======================================================================== */
static const u8 gap_r[]  = { 1, 1, 0, 0, 0, 7, 7 };          /* A8B1 (7 entries; the 8th is never reached) */
static const u8 gap_l[]  = { 3, 3, 4, 4, 4, 5, 5 };          /* A8B8 */
static const u8 wall_r[] = { 2, 1, 1, 0, 0, 7, 7, 6 };       /* A8BF */
static const u8 wall_l[] = { 2, 3, 3, 4, 4, 5, 5, 6 };       /* A8C7 */

static void runner_update(struct enemy *e)              /* 0xA749 */
{
    bool facing;
    if (fight_on_hazard(e)) { fight_enemy_killed(e); return; }
    if (!e->hp) e->hp = 8;
    if (e->hit & HIT_STUN) { fight_take_damage(e); return; }
    if (e->next & 0x18) { runner_jump(e); return; }                 /* A76A */
    if (!fight_step_down(e)) return;
    if (!(e->next & 2)) {                                           /* A77B */
        u8 f = hero_dir6(e, &facing);
        if (!facing && f != 0xFF) { e->hit = (e->hit & 0x7F) | f; e->next |= 2; return; }   /* A78A: turn, chase */
    }
    u8 *p = ring_wrap_down(ring_addr(e->row, e->rcol) + 0x48 + ((e->hit & FACING_RIGHT) ? 1 : 0));   /* A796 */
    if (cell_passable_ai(*p)) { e->phase = 0; e->next |= 8; return; }   /* gap ahead */
    e->phase = (e->phase + 1) & 3;
    if (!(e->next & 2) && (e->link += 0x10) carried) { e->next ^= 0x80; return; }   /* A7D2 (sic: flips next bit 7, not the facing) */
    hero_dir6(e, &facing); if (!facing) e->next &= ~2;              /* A7DD */
    if (e->hit & FACING_RIGHT) { fight_step_right(e); if (!fight_step_right(e)) return; }   /* A7EC: 2 cells/frame */
    else                       { fight_step_left(e);  if (!fight_step_left(e))  return; }
    e->phase = 0; e->next |= 0x10;                                  /* A7F9: wall -> jump */
}
/* 0xA818  as eai1 hog_wall_jump, table chosen by next&0x10 (wall) / &8 (gap) */
static void runner_jump(struct enemy *e)
{
    e->next += 0x20;
    if (!(e->next & 0x20)) {
        u8 n = (e->phase + 1) & 3;
        if (!n) { e->next = 0; e->phase = 3; fight_step_down(e); return; }   /* A875 */
        e->phase = (e->phase & 0xF0) | n;
    }
    const u8 *t = (e->hit & FACING_RIGHT) ? ((e->next & 0x10) ? wall_r : gap_r) : ((e->next & 0x10) ? wall_l : gap_l);
    if (!fight_step_dir(t[((e->next >> 5) - 1) & 7])) return;
    e->next = 0; if (e->phase) e->phase = 3;                        /* A865 */
}

/* 0xA2F1 */
void ai_entry(struct enemy *e)
{
    static void (*const fn[5])(struct enemy *) = { tallwalker_update, tall_lower_noop, tallspit_update, tall_lower_noop, runner_update };   /* A2FF */
    fn[e->type & 0xF](e);
}
static void tall_lower_noop(struct enemy *e) { }        /* 0xA309 / 0xA638 */
