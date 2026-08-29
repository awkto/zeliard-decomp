/*
 * boss_drgn.c — cavern 7 boss "Dragon" (DRGN.BIN = ZELRES3[16], 2985 bytes @ A000).
 * Map mp7d (70 cells wide, level record ai = 13).  Sprites DRGN.GRP.
 * Boss info [A002] = AA3C:
 *   col 0x1E (AA3C), row 0x08 (AA3E), HP 800 (AA3F), EXP 12000, camera column 5
 *   (the hero is kept at the LEFT edge of the window), knockback 0, name record
 *   AA49 {0x11, 0xBB, 6 "Dragon"}, gold 2500.
 * Contact A010: 40 for class 0 (head) and classes 8/9 (the flame), 30 for the
 * body classes 1..7.  Frame tables A030: classes 0..9 (A044..).  No drops.
 *
 * Image: the dragon is composed at CELL granularity into a 29-column x 10-row
 * buffer AA69 (column-major, stride 10; FF = empty): part (c, r) is a 2x2
 * sprite whose top-left is map cell (boss_col + c, boss_row + r).  A758
 * pastes a layer at (AA53 = column, AA54 = row): for CX columns one bitmap
 * byte per column (bit 7 = first row) and one {class<<4 | frame} byte per set
 * bit.  Layers, in order (A542..A5E4):
 *   1. pose:       (0, 1)  list [A783 + 2*pose], bitmap [A810 + 2*pose], 12 columns
 *                  (neck/head; poses 0..0xA, 10-11 parts, class 0 = head)
 *   2. body:       (12, 0) list [A8DE + 2*(frame&1)], bitmap [A8FD + ..], 11 columns
 *   3. front legs: (9, 6)  list [A881 + 2*(walk&3)], bitmap [A89A + ..], 7 columns
 *   4. hind legs:  (17, 6) list [A8B7 + 2*(walk&3)], bitmap A8D7, 7 columns
 *   5. tail:       (25, 8) list A87A {2B,2C,2D}, bitmap A87D, 4 columns
 * so the dragon is up to 29 cells (232 px) wide and 10 cells tall — wider
 * than the 28-column window; columns off the ring are skipped (A606).
 * Each part record: {col, row, rcol, type = class | 0x80 (SOLID to the hero
 * while not in the death cutscene, A62F), hit = 0 / 0x20 when a hit landed
 * this frame, phase = whole byte}.
 * The flame (A68B) is a second image: 13 columns x 8 rows at
 * (boss_col - 10 (+4 when pose == 5), boss_row + 4), lists [A917 + 2*(f&3)]
 * / bitmaps [A930 + ..] for poses < 6 (class 8) or [A96C]/[A985] for poses
 * >= 6 (class 9); type = class | 0x20 (sword-immune), not solid.
 *
 * Poses (pose layer): 0 = standing (mid range), 4 = head lowered (close),
 * 6/7 = far / walking upright, 1/5/8 = the breathing frames of 0/4/7,
 * 2/3 = death thrash, 9/0xA = rearing (head-hit reaction, 0xA also final
 * death pose).
 *
 * Private state: AA53/AA54 compose column/row, AA55 rcol temp, AA56 breathing,
 * AA57 flame frame, AA58 death counter, AA59 part counter, AA5A hit source
 * (|0x80 head), AA5B pose, AA5C frame counter, AA5D walk frame, AA5E half-
 * frame accumulator (+0x80 per frame), AA5F retreating, AA60 retreat steps,
 * AA61 winding up, AA62 wind-up base pose, AA63 wind-up counter, AA64 flame
 * counter, AA65 head-hit latch, AA66 reaction table select, AA67 reaction
 * index, AA68 reacting.
 */
#include "ai_common.h"

static u16 *const boss_col = (u16 *)0xAA3C;  static u8 *const boss_row = (u8 *)0xAA3E;  static u16 *const boss_hp = (u16 *)0xAA3F;
static u8 breathing, flame_frame, death_cnt, pose, frame, walk, half, retreating, retreat_steps,
          winding, windup_base, windup_cnt, flame_cnt, head_hit, react_tbl, react_idx, reacting;

static const u8 react_low[7]  = { 0x0A, 0x09, 0x06, 0x03, 0x02, 0x03, 0x02 };   /* A4B4: head hit while pose < 6 (last entry 0x82 = 2 | end) */
static const u8 react_high[7] = { 0x03, 0x02, 0x03, 0x02, 0x01, 0x03, 0x02 };   /* A4BB: head hit while pose >= 6 */

/* 0xA2DD  Frame entry. */
void boss_entry(void)
{
    /* 1. read back last frame's parts (A2E1..A334): restore the covered cells
     *    (ED20[n]), take the FIRST pending hit; a part of class 0 (head) marks
     *    it |0x80.  Flame parts have type & 0x20 but their class is 8/9. */
    u8 hit = 0, n = 0;
    for (struct enemy *o = MAP_OBJECTS; o->col != 0xFFFF; o++, n++) {
        if (map_col_to_ring(o->col, &o->rcol)) continue;
        *ring_addr(o->row, o->rcol) = under_sprite[n];
        if ((o->hit & 0x40) && !(hit & 0x80)) hit = (o->hit & 0x1F) | ((o->type & 0x1F) == 0 ? 0x80 : 0);
    }
    MAP_OBJECTS[0].col = 0xFFFF;                                    /* A336 */

    /* 2. damage (A33E): sword/stomp d = dmg/2, magic and orb d = dmg/8; the
     *    head doubles it (sword x1, magic x1/4).  Any hit starts a retreat. */
    if (hit) {
        u16 d = damage_for_source(hit & 0x1F) / 2;
        if ((hit & 0x7F) >= 2) d /= 4;                              /* A35C..A363 */
        if (hit & 0x80) { head_hit = 0xFF; sfx_request = 0x34; d *= 2; }   /* A369 */
        else            { retreating = 0xFF; sfx_request = 0x35; }         /* A377: retreat_steps NOT reset: 0 -> 255 steps, i.e. until column 0x1E */
        boss_damage(d);                                             /* A9B4 */
        if (head_hit) {                                             /* A384: rear up, back off 8 cells */
            react_tbl = pose < 6; react_idx = 0; breathing = 0; winding = 0;
            retreating = 0xFF; reacting = 0xFF; retreat_steps = 8;
        }
        head_hit = 0;
    }
    if (boss_cutscene) { death_step(); return; }                    /* A3BA */
    frame++;                                                        /* A3C4 */
    if (breathing)    { flame_step();  draw(); return; }            /* A4FC */
    if (winding)      { windup_step(); draw(); return; }            /* A4C2 */

    /* 3. walk (A3DC): one cell every 2nd frame.  Forward = LEFT (toward the
     *    hero) down to column 0x10 (A521); retreat = RIGHT up to column 0x1E
     *    (A532) for retreat_steps half-frames or until blocked. */
    if ((half += 0x80) == 0) {
        if (!retreating) { if (!step_left()) walk++; }              /* A3EA */
        else if (--retreat_steps == 0) retreating = 0;              /* A3F5 */
        else { retreating = step_right() ? 0 : 0xFF; walk--; }      /* A402 */
    }

    /* 4. pose (A410) */
    if (reacting) {                                                 /* A48E: 7-frame reaction sequence */
        const u8 *t = react_tbl ? react_low : react_high;
        pose = t[react_idx]; if (react_idx == 6) reacting = 0; react_idx++;   /* entry 6 carries bit 7 = end */
    } else if (!(KRN_RANDOM() & 0xC0) && (pose == 0 || pose == 4 || pose == 7)) {   /* A417: 1/4 per frame from a rest pose */
        windup_base = pose; windup_cnt = 0; winding = 0xFF;         /* A435 */
    } else {                                                        /* A448: pose by distance from the window's left edge */
        u8 left = scroll_col_lo;                                    /* [0x80] */
        if ((u8)(left + 16) < (u8)*boss_col)      pose = (pose < 6)  ? 6 : 7;   /* far: > 16 columns */
        else if ((u8)(left + 11) < (u8)*boss_col) pose = (pose >= 7) ? 6 : 0;   /* 12..16 columns */
        else                                      pose = (pose >= 7) ? 6 : 4;   /* close: <= 11 columns (hero is at column 5) */
    }
    draw();                                                         /* A542 */
}

/* 0xA521 / 0xA532  column limits 0x10..0x1E; true = blocked (CF=1) */
static bool step_left(void)  { if (*boss_col - 1 <= 0x0E) return true; (*boss_col)--; return false; }
static bool step_right(void) { if (*boss_col + 1 >  0x1E) return true; (*boss_col)++; return false; }

/* 0xA4C2  Wind-up: 6 frames alternating base/base+1, then 10 frames of flame. */
static void windup_step(void)
{
    windup_cnt++; pose = windup_base + (windup_cnt & 1);
    if (windup_cnt >= 6) { pose = windup_base + 1; flame_frame = 0; flame_cnt = 0; winding = 0; breathing = 0xFF; sfx_request = 0x36; }   /* A4D9 */
}

/* 0xA4FC  Breathing: sound 0x36 every frame, flame frames 0,1,2,3,2,3,...
 * (A501: +1, wrapping 4 -> 2), 10 frames, then back to the pose logic. */
static void flame_step(void)
{
    sfx_request = 0x36;
    if (++flame_frame >= 4) flame_frame = 2;
    if (++flame_cnt >= 10) breathing = 0;
}

/* 0xA9B4  HP -= d (floor 0), bar [200C]; at 0: death cutscene, everything
 * else cancelled (react, breathing, wind-up). */
static void boss_damage(u16 d)
{
    *boss_hp = (*boss_hp > d) ? *boss_hp - d : 0;
    VID_200C(BX = *boss_hp);
    if (*boss_hp == 0) { death_cnt = 0; boss_cutscene = 0xFF; head_hit = 0; react_idx = 0; breathing = 0; winding = 0; }
}

/* 0xA9F2  Death: 40 frames of boss_dying; the first 30 thrash between poses
 * 2 and 3 with sound 0x37 every 4th frame, then pose 0xA (collapsed); then
 * boss_defeated (fight.bin awards 12000 EXP / 2500 gold). */
static void death_step(void)
{
    if (death_cnt >= 0x28) { boss_defeated = 0xFF; return; }
    boss_dying = 0xFF; death_cnt++;
    if (death_cnt < 0x1E) { frame++; pose = 2 + (frame & 1); if (!(frame & 3)) sfx_request = 0x37; }
    else                  { frame = 1; pose = 0x0A; }
    draw();
}

/* 0xA542  Compose the 5 layers into AA69 (see header), then emit one record
 * per non-FF buffer cell (A5E7..A687), then the flame image if breathing
 * (A68B..A757).  Records get hit |= 0x20 when a hit was read back this
 * frame (flash). */
static void draw(void);
