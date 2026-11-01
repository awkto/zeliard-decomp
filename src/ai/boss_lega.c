/*
 * boss_lega.c — cavern 6 boss "Tarso" (LEGA.BIN = ZELRES3[14], 2073 bytes).
 * Map mp6d.  Sprites LEGA.GRP (ZELRES3[69]).  Boss info [A002] = A7A0:
 *   col 0x26, row 0x07, HP 640, EXP 6000, camera column 8, knockback always
 *   left, name record A7AD {0x11, 0xBB, 5 "Tarso"}, gold 1500.
 * Contact A010: 160 for classes 0..5, 80 for class 6 (its projectile), 10
 * for 7..15.  Frame tables A030: classes 0..6 (A03E..A205).
 *
 * Image (A44C): buffer A7C9 of 8 COLUMNS x 10 rows of part bytes, column-
 * major (10 bytes a column, 0xFF empty).  Pose A7B9 (0..8) pastes bitmap
 * [A744 + 2*pose] (one byte per column, `rol`, bit 7 first) and byte list
 * [A6C8 + 2*pose] into rows 2..9 (di starts at A7CB, +2 after each column).
 * The face goes in at A7F1 = column 4 row 0 (row 1 on pose 6 and poses >= 8),
 * frame 2*A7BA, and 0x14 bytes on = column 6, frame 2*A7BA+1.  A4AA emits
 * column by column (part col = boss_col + c, row = boss_row + r); a column
 * off the ring is skipped whole.  Each byte: bit 7 -> type |= 0x60 (immune,
 * harmless), bits 4-6 the class, the byte itself the frame; [si+5] = 0x20
 * while A7B7 (the hit) is set (A501).  port/boss_lega.c is the line-by-line
 * version (tarso_fight).
 *
 * State: A7A0 col (word), A7A2 row, A7A3 HP, A7B6 part count, A7B7 hit
 * source, A7B8 death counter, A7B9 pose, A7BA face, A7BB face timer, A7BC
 * ring column, A7BD retreating, A7BE (written at A390, never read), A7BF
 * retreat timer, A7C0 attacking, A7C1 its step, A7C2 projectile live,
 * A7C3/A7C5 projectile col (word)/row, A7C6 its frame, A7C7 its path index,
 * A7C8 exploding.
 */
#include "ai_common.h"

static const u8 face_tbl[4]         = { 0, 1, 2, 1 };              /* A41B */
static const u8 walk_left_poses[5]  = { 2, 5, 6, 7, 0 };           /* A41F: poses that step left */
static const u8 walk_right_poses[5] = { 1, 3, 6, 7, 7 };           /* A424: poses that step right */
static const u8 death_poses[10]     = { 0, 1, 2, 3, 6, 7, 6, 3, 2, 1 };   /* A69B, by the new count */
static const s8 path[17][2] = { {-1,0},{-1,0},{-1,1},{0,2},{-1,2},{0,2},{-1,2},{-1,-2},{-1,0},{-1,2},{-1,-1},{-1,0},{-1,1},{-1,0},{-1,0},{-1,0},{-1,0} };   /* A5D8: {dcol, drow} per frame; index sticks at 16 */

/* A429: col - 1 is stored whatever happens; CF = the new col <= 0x0E */
static int step_left_0E(void)  { *boss_col -= 1; return *boss_col <= 0x0E; }
/* A43B: col + 1 only up to 0x32; CF = refused */
static int step_right_32(void) { if (*boss_col + 1 > 0x32) return 1; *boss_col += 1; return 0; }

/* 0xA223  Frame entry. */
void boss_entry(void)
{
    u8 hit = readback_parts();                                      /* A231: [si+5]&1F of the LAST part with bit 6 */
    MAP_OBJECTS[0].col = 0xFFFF;                                    /* A26D */
    if (hit) {                                                      /* A275 */
        u16 d = damage_for_source(hit);                             /* vec 28: AH */
        if (hit == 1) d *= 2; else if (hit != 9) d /= 8;            /* sword x2, orb x1, the rest /8 */
        boss_damage(d);                                             /* A644: HP A7A3, bar 200C; at 0 --
                                                                       on every hit at 0 -- A7B8 = 0, A7C2 = 0, FF2E = FF */
        sfx_request = 0x2F;
        if ((u8)*boss_col < 0x2F) { retreat_timer = 0x14; retreating = 0xFF; }   /* A2A4 */
    }
    if (boss_cutscene) { death_step(); return; }                    /* A2B5 -> A66E */
    if (attacking) { attack_step(); draw_parts(); return; }         /* A2BF -> A3B5, straight to A44C */
    if (shot_live && path_idx < 0x0D) goto attack_check;            /* A2C9: no movement while the shot is on its way */
    if (!retreating) {                                              /* A2DA: walk left */
        retreat_timer = 0x3C; pose = (pose + 1) & 7;
        if (in(pose, walk_left_poses)) { retreating = step_left_0E() ? 0xFF : 0; if (pose == 7) retreating = step_left_0E() ? 0xFF : 0; }
    } else if (!--retreat_timer) retreating = 0;                    /* A316 */
    else for (;;) {                                                 /* A323: poses backwards (0 -> 7, 6 -> 3) */
        pose = (pose == 0 ? 8 : pose == 6 ? 4 : pose) - 1;
        if (!in(pose, walk_right_poses)) break;
        retreating = step_right_32() ? 0 : 0xFF;                    /* A344: cmc / sbb -- set while it can step */
        if (pose == 6 || pose == 3) { retreating = step_right_32() ? 0 : 0xFF; break; }
        /* A354: poses 1 and 7 go round again, so one frame can step 3 columns */
    }
attack_check:
    if (!retreating && pose == 6 && !(KRN_RANDOM() & 1) && !shot_live && *boss_col >= 0x14) {   /* A35F: the COLUMN (A37D), not HP */
        attacking = 0xFF; attack_step_idx = 0; unused_a7be = 0; pose = 8; sfx_request = 0x30;
    }
    face_timer = (face_timer + 1) & 3; face = face_tbl[face_timer];   /* A39F */
    draw_parts();                                                   /* A44C, then A5FA / A54B */
}
/* 0xA3B5  attack sequence, table A3C7 by ++A7C1: step 1 (A3CD) face 6, pose
 * 8, launch the projectile from (col+4, row & 3F): frame 0, path index 0,
 * not exploding; step 2 (A3FE) face 7, pose 6; step 3 (A40A) face 0,
 * attacking = 0, pose 6.  No face timer on these frames. */
/* 0xA5FA  the projectile part, after the body: type 0x26 (class 6, immune,
 * contact 80), [si+5] = 0 (no hit bit), frame A7C6; not placed off the ring. */
/* 0xA54B  the projectile moves AFTER it is drawn: exploding -> frame + 1,
 * gone at 6 (frames 3, 4, 5 drawn); else when the col's low byte < 0x12 it
 * starts exploding (frame 3, sound 0x32); else col low byte += path[idx].dcol,
 * row += path[idx].drow, idx + 1 below 0x10, frame (frame + 1) % 3, sound
 * 0x31 when the new idx is 9, 12 or 15. */
/* 0xA66E  Death: at 0x28 FF30 = FF and nothing is drawn; else FF2F = FF and
 * ++A7B8; below 10 the pose is A69B[count] (sound 0x33 from pose 3) and the
 * face timer runs on (A39F); from 10 face 6 (A6BC is never reached). */
