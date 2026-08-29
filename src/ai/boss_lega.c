/*
 * boss_lega.c — cavern 6 boss "Tarso" (LEGA.BIN = ZELRES3[14], 2073 bytes).
 * Map mp6d.  Sprites LEGA.GRP (ZELRES3[69]).  Boss info [A002] = A7A0:
 *   col 0x26, row 0x07, HP 640, EXP 6000, camera column 8, knockback always
 *   left, name record A7AD {0x11, 0xBB, 5 "Tarso"}, gold 1500.
 * Contact A010: 160 for classes 0..5, 80 for class 6 (its projectile), 10
 * for 7..15.  Frame tables A030: classes 0..6 (A03E..A205).
 *
 * Image: 8 rows x 8 columns; pose A7B9 (0..8) selects a bitmap A744[pose]
 * and byte list A6C8[pose] (A44C..A483); each byte: bit 7 -> type |= 0x60
 * (immune, harmless), bits 4-6 -> class, whole byte -> frame.  The face
 * (two parts, frames 2*A7BA and +1) is patched in at buffer offset 0x28/0x29
 * (+1 column on poses 6 and 8+) and 0x3C.  Part row = boss_row + r.
 *
 * State: A7A0 col, A7A2 row, A7A3 HP, A7B8 death counter, A7B9 pose, A7BA
 * face frame, A7BB face timer, A7BD retreating, A7BF retreat timer, A7C0
 * attack sequence, A7C1 its step, A7C2 projectile live, A7C3/A7C5
 * projectile col/row, A7C6 its frame, A7C7 its path index, A7C8 exploding.
 */
#include "ai_common.h"

static const u8 walk_left_poses[5]  = { 1, 2, 3, 7, 7 };           /* A41F: poses that step left */
static const u8 walk_right_poses[5] = { 1, 2, 3, 7, 7 };           /* A424 */
static const s8 path[17][2] = { {-1,0},{-1,0},{-1,1},{0,2},{-1,2},{0,2},{-1,2},{-1,-2},{-1,0},{-1,2},{-1,-1},{-1,0},{-1,1},{-1,0},{-1,0},{-1,0},{-1,0} };   /* A5D8: {dcol, drow} per frame; index sticks at 16 */

/* 0xA223  Frame entry. */
void boss_entry(void)
{
    u8 hit = readback_parts();                                      /* A231 (no weak points; last hit wins) */
    MAP_OBJECTS[0].col = 0xFFFF;
    if (hit) {                                                      /* A275 */
        u16 d = damage_for_source(hit);
        if (hit == 1) d *= 2; else if (hit != 9) d /= 8;            /* sword x2, orb x1, magic /8 */
        boss_damage(d); sfx_request = 0x2F;                         /* A644: HP A7A3 */
        if (*boss_col < 0x2F) { retreat_timer = 20; retreating = 0xFF; }   /* A2A4 */
    }
    if (boss_cutscene) { death_step(); return; }                    /* A66E */
    if (attacking) attack_step();                                   /* A3B5: 3-step sequence A3C7 */
    else if (!(shot_live && path_idx < 13)) {                       /* A2C9: no movement while the shot is on its way */
        if (!retreating) {                                          /* A2DA: walk left, 8-frame cycle, 1 cell on poses 1,2,3,7 */
            retreat_timer = 60; pose = (pose + 1) & 7;
            if (in(pose, walk_left_poses)) { retreating = step_left_0E(); if (pose == 7) retreating = step_left_0E(); }
        } else if (!--retreat_timer) retreating = 0;                /* A316 */
        else { /* A323: poses backwards (0->8, 6->4), stepping right (max col 0x32) on poses 1,2,3,7; double on 6 and 3 */ }
    }
    if (!retreating && pose == 6 && !(KRN_RANDOM() & 1) && !shot_live && *boss_hp >= 20) {   /* A35F */
        attacking = 0xFF; attack_step_idx = 0; face_timer = 0; pose = 8; sfx_request = 0x30;
    }
    face_timer = (face_timer + 1) & 3; face = face_tbl[face_timer];   /* A39F: A41B */
    draw_parts(); projectile_part(); shot_update();                  /* A44C / A5FA / A544 */
}
/* 0xA3B5  attack sequence: step 1 (A3CD) face 6, pose 8, launch the
 * projectile from (col+4, row): frame 0, path index 0; step 2 (A3FE) face
 * 7, pose 6; step 3 (A40A) face 0, attacking = 0, pose 6. */
/* 0xA544  projectile: when its col < 0x12 it explodes (frames 3..5, sound
 * 0x32, then gone); else moves by path[idx] per frame, frames 0..2, sound
 * 0x31 at path steps 9, 12, 15.  Drawn as a part of type 0x26 (immune,
 * class 6 -> contact 80). */
/* 0xA66E  Death: 40 frames boss_dying; first 10 frames poses A69B[t]
 * = {0,1,2,3,7,7,7,3,...} with sound 0x33 from pose 3, then face frames
 * from A6BC; then boss_defeated. */
