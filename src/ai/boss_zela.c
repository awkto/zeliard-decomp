/*
 * boss_zela.c — cavern 4 boss "Agar" (ZELA.BIN = ZELRES3[12], 1576 bytes).
 * Map mp4d.  Sprites ZELA.GRP (ZELRES3[67]).  Boss info [A002] = A5EE:
 *   col 0x30, row 0x0C, HP 500, EXP 1000, camera column 12, knockback 0,
 *   name record A5FB {0x12, 0xBB, 4 "Agar"}, gold 600.
 * Contact A010: 30 for every part.
 *
 * Image: 4 columns x 3 rows of 2x2 parts (A467).  Here the roles of type
 * and phase are swapped: record.type = POSE (0..4, frame tables A030[0..4]
 * = A03A/A08A/A0D0/A116/A166 with 16 entries each) and record.phase = PART
 * index 0..11 (A3C8: buffer A610 of {pose, part}).  Pose comes from
 * A4EA[A603 & 7] = { 1,2,3,0,3,2,1,0 }; attacks patch single parts to
 * frames 0xC..0xF (A431/A453: open mouth / raised arm).
 * Part row = boss_row + 2*r, col = boss_col + 2*c.
 *
 * State: A5EE col, A5F0 row, A5F1 HP, A603 anim, A604 firing (1 = left
 * shot, 2 = right shot), A605 hopping, A606 hop direction, A607 pause,
 * A608 hop step, A609 pause count, A60B anim divider, A60C hit source,
 * A60E death counter, A60F "blocked" latch.
 */
#include "ai_common.h"

static struct shot bolt_l = { 0, 0, 0x15, 0, 50, 4, 80 };          /* A552: left, 50 cells, damage 80 */
static struct shot bolt_r = { 0, 0, 0x12, 0, 50, 0, 80 };          /* A55F: right */
static const u8 pose_of_anim[8] = { 1, 2, 3, 0, 3, 2, 1, 0 };     /* A4EA */

/* 0xA1B6  Frame entry. */
void boss_entry(void)
{
    u8 hit = readback_parts();                                      /* A1C4 (no weak points) */
    MAP_OBJECTS[0].col = 0xFFFF;
    if (hit) {                                                      /* A20F */
        u16 d = damage_for_source(hit) / 2;                         /* half sword damage */
        if (hit == 4) { d *= 4; sfx_request = 0x24; } else sfx_request = 0x25;   /* magic 3 (source 4) counts double */
        boss_damage(d);                                             /* A56C: HP A5F1; at 0 -> cutscene, vec 30, firing off */
        if (*boss_col < hero_col(15)) { step_left(); step_left(); } else { step_right(); step_right(); }   /* A23F: hop 2 cells away */
    }
    if (firing) { attack_step(); }                                  /* A371 */
    else {
        if (!hopping && !(KRN_RANDOM() & 0xF) && !boss_cutscene) {  /* A274: 1/16 per frame start a hop */
            hopping = hop_dir = pause = 0xFF; hop_step = pause_cnt = 0;
            if (*boss_col < hero_col(14)) hop_dir = 0;             /* A2A3: toward the hero */
        }
        anim = (anim + 2) & 6;                                      /* A2BE */
        if (pause) { if (!(pause_cnt = (pause_cnt + 1) & 3)) { pause = 0; if (!(hopping & 0x80)) hopping = 0; } }   /* A2C8 */
        else hop_table[hop_step++]();                               /* A2F4: A307 sequence: row-1, row-1, (check), row+1, row+1, ... end (A320: pause = 0x7F, blocked = 0) */
        /* hop steps that "check" (A348) abort the sequence and take a horizontal step toward the hero (A3AB/A3BE) unless boss_col == hero_col(12) */
    }
    build_parts();                                                  /* A3C8 */
    if (!hopping) {                                                 /* A3E3 */
        if (firing == 1)      { part[1].frame = 0xE; part[4].frame = 0xF; if (pose == 4) fire(); }        /* A453 */
        else if (firing == 2) { part[7].frame = 0xC; part[10].frame = 0xD; if (pose == 0) fire(); }       /* A431 */
        else if (!(KRN_RANDOM() & 1)) {                             /* A3FA */
            if (*boss_col >= hero_col(18)) { if (pose == 2) firing = 1; }                                  /* A447: hero left -> left shot */
            else if (*boss_col + 7 >= hero_col(16) && pose == 6) firing = 2;                               /* A419: hero right -> right shot */
        }
    }
    draw_parts();                                                   /* A467 */
}

/* 0xA371  While an attack is pending the animation runs at half speed and
 * Agar walks toward the hero (left if boss_col > scroll+18 on frame 0,
 * right on frame 4), setting the blocked latch at the column limits
 * (0x11..0x32). */
static void attack_step(void);
/* 0xA4F2  fire: shot from (boss_col+1, row+3) going left (firing 1) or
 * (boss_col+7, row+3) going right (firing 2); firing = 0 */
static void fire(void);
/* 0xA59A  Death: 40 frames boss_dying; 21 frames of animation with sound
 * 0x28 every 4th frame, then pose 2; then boss_defeated. */
