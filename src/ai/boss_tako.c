/*
 * boss_tako.c — cavern 2 boss "Pulpo" (octopus) (TAKO.BIN = ZELRES3[10], 2722 bytes).
 * Map mp2d.  Sprites TAKO.GRP (ZELRES3[65]).  Boss info [A002] = AA80:
 *   col 0x24, row 0x10, HP 250, EXP 200, camera column 7, knockback always
 *   left, name record AA8D {0x12, 0xBB, 5 "Pulpo"}, gold 200.
 * Contact A010: 10 for every part.  Frame tables A030: parts 0..5
 * (A052..A1E2, 16 poses each), 0x0E/0x0F/0x10 (A255/A205/A25F).
 *
 * Image format: the octopus is 8 parts wide x 7 rows; pose p (0..0x1F) has
 * an 8-bit-per-row bitmap at A9AF[p] and a list of {type, frame} words at
 * A57D[p] consumed left to right, top to bottom for every set bit (A3E3..
 * A48B).  Part row = boss_row + 2*bitrow, col = boss_col + 2*bit.
 * Pose = stage (AA97: 0, 8, 0x10) + animation frame (AA96 & 7); the
 * attack pose adds 8 (0x18) and the hit flicker subtracts 8.
 *
 * State: AA96 frame, AA97 stage, AA98 hit-reaction (0x10 | 4-bit timer,
 * 0x20 flicker), AA99 attack state (0x40 wind-up + 2-bit count, 0x80 ink
 * cloud out + 5-bit frame, 0x20 pose toggle), AA9E death counter, AA9F/AAA1
 * ink cloud col/row.  HP at AA83.
 */
#include "ai_common.h"

/* 0xA27D  Frame entry. */
void boss_entry(void)
{
    /* read back the parts (A28B..A2D4): weak point = part type >= 0x0E */
    u8 hit = 0;  /* ... as crab, with (type >= 0x0E) -> |0x80 */
    MAP_OBJECTS[0].col = 0xFFFF;
    if (hit) {                                                      /* A2DE (no cutscene test here) */
        u16 d = damage_for_source(hit & 0x1F) * 2;
        if (hit & 0x80) { d *= 2; sfx_request = 0x24; } else sfx_request = 0x25;
        boss_damage(d);                                             /* A503: HP at AA83, bar [200C], death at 0 */
        if (!(react & 0x10) && stage != 0x10) {                     /* A309: each clean hit advances the stage */
            stage += 8; react = 0x10; attack |= 0x20; sfx_request = 0x26;
        }
    }
    if (boss_cutscene) { death_step(); return; }                    /* A530 */
    frame = (frame + 1) & 7;                                        /* A334 */
    u8 p = stage;
    if (react & 0x10) {                                             /* A344: 16-frame flicker between stage and stage-8 */
        react ^= 0x20; if (!(react & 0x20)) p -= 8;
        react = (react & 0xF0) | ((react + 1) & 0xF);
        if (!(react & 0xF)) { react &= ~0x10; attack &= ~0x20; }
    }
    if (p == 0x10) {                                                /* A36F: final stage attacks */
        if (attack & 0x40) {                                        /* A377: wind-up, 4 frames */
            u8 a = attack ^ 0x20, n = (a + 1) & 3; attack = (a & 0xE0) | n;
            if (!n) { attack = 0xA0; ink_col = *boss_col + 4; ink_row = (*boss_row + 4) & 0x3F; sfx_request = 0x27; }   /* A391 */
        }
        if (!(attack & 0xA0) && !(react & 0x10)) attack |= 0x40;    /* A3AC: start the next wind-up */
        if (!(attack & 0x20)) p += 8;                               /* A3BB: attack pose 0x18 */
        if (attack & 0x80) {                                        /* A3C3: ink cloud: 24 frames, one cell left per frame */
            attack = (attack & 0xE0) | ((attack + 1) & 0x1F); ink_col--;
            if ((attack & 0x1F) == 0x19) attack = 0;
        }
    }
    draw(p + frame);                                                /* A3E3 */
}

/* 0xA492  The ink cloud: 4 parts at (ink_col + i, ink_row), type 0x30
 * (frame table 0x10 = A25F, sword-immune), frame = AA20[(n-1)*4 + i] - 1
 * (0 = no part), contact 10. */
/* 0xA530  Death: 40 frames, boss_dying; first 20 frames flicker stage/stage+8
 * with sound 0x28, then pose stage+8; then boss_defeated. */
/* 0xA503  boss_damage: HP (AA83) -= d, bar update, at 0 -> cutscene, AA9E = 0 */
