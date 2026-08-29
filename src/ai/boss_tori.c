/*
 * boss_tori.c — cavern 3 boss "Pollo" (bird) (TORI.BIN = ZELRES3[11], 2020 bytes).
 * Map mp3d.  Sprites TORI.GRP (ZELRES3[66]).  Boss info [A002] = A773:
 *   col 0x2E, row 0x12, HP 500, EXP 500, camera column 8, knockback always
 *   left, name record A780 {0x12, 0xBB, 5 "Pollo"}, gold 500.
 * Contact A010: 56 for part class 0 (the beak/head), 18 for all others.
 * Frame tables A030[0..0xE] = A04E..A1C5 (part classes 0..14).
 *
 * Image: 9 rows x 8 columns of 2x2 parts; the pose buffer A79C (72 bytes,
 * FF = empty) is composed from up to four layers (A552: layer n = bitmap
 * A6CB[n] (9 bytes, one bit per column) + byte list A64D[n]; each set bit
 * takes a byte {type<<4 | frame}).  Layers drawn each frame (A467..A4B9):
 *   normal: body A78B (0, or 1 = flinch), wings 6+A78A (4 frames), legs
 *           0xA+A78F (3), head 2+A790 (4);
 *   diving: 0xD + (A78D&3), with the body shifted down one row on odd frames;
 *   rising/dead: 0x11 + (A78D&1).
 * Part row = boss_row + row, col = boss_col + col (A4C1..A54B).
 *
 * State: A773 col, A775 row, A776 HP, A78A wings, A78B body, A78C diving,
 * A78D dive frame, A78E rising, A78F legs (0..2 = 3-frame timer, A57B),
 * A790 head, A791 hit source, A794 death counter, A795 flinch frames,
 * A796 parity, A797 flap-up, A798 flap count, A799 dead pose, A79A egg,
 * A79B dive timer.
 */
#include "ai_common.h"

static struct shot egg = { 0, 0, 0xA7, 0, 50, 4, 40 };            /* A766: cell A7, 50 cells LEFT, damage 40 */

/* 0xA1D4  Frame entry. */
void boss_entry(void)
{
    u8 hit = readback_parts();                                      /* A1E2: weak point = part type 0 -> |0x80 */
    MAP_OBJECTS[0].col = 0xFFFF;
    if (hit) {                                                      /* A235 */
        u16 d = damage_for_source(hit & 0x1F) * 2; if (hit & 0x80) d *= 4;   /* head: x8 */
        sfx_request = 0x29; boss_damage(d);                         /* A5BA */
        if (diving) { diving = 0; dive_frame = 0; rising = 0xFF; } /* A25B: a hit ends the dive */
        else step_right();                                          /* A5AB: col+1 (max 0x30) */
        flinch = 4;                                                 /* A276 */
    }
    body = 0; if (flinch) { flinch--; body = 1; }                   /* A27B */
    if (diving) {                                                   /* A290: charge left 1 cell/frame, rising to row 0xE, poses D..10, sound 0x2B */
        if (*boss_row != 0xE) (*boss_row)--;
        dive_frame = (dive_frame + 1) & 3; if (dive_frame == 2) sfx_request = 0x2B;
        if (step_left_11() || !dive_timer-- || hit) { diving = 0; dive_frame = 0; rising = 0xFF; sfx_request = 0x2A; }   /* A2B7..A2DD */
    } else if (rising) {                                            /* A2E5: back down to row 0x12, one row per 2 frames, drifting left (min col 0xD) */
        if (dive_frame == 1) rising = 0;
        else { dive_frame = 1; if (*boss_row != 0x12) { (*boss_row)++; dive_frame = 0; step_left_0D(); } }
    } else if (flapping) {                                          /* A316: 4 flaps (3 frames each, sound 0x2A, flinch 4) then dive for 15 frames */
        head = (head + 1) & 3;
        if (legs_tick()) { if (flap_cnt < 4) { flap_cnt++; sfx_request = 0x2A; flinch = 4; } else { flapping = 0; dive_frame = 0; diving = 0xFF; dive_timer = 15; } }
    } else if (laying) {                                            /* A35D: 2 flaps then drop an egg from (col+4, row+4) */
        if (legs_tick()) {
            if (flap_cnt < 2) { flap_cnt++; sfx_request = 0x2A; flinch = 2; }
            else { map_col_to_ring(*boss_col + 4, &egg.col); egg.row = (*boss_row + 4) & 0x3F; fight_shot_spawn(&egg); laying = 0; }
        }
    } else if (boss_cutscene) { death_step(); return; }             /* A60A */
    else {                                                          /* A3B7: hover over the hero */
        head = (head + 1) & 3;
        if (hit && *boss_col >= 0x14) { flapping = 0xFF; flap_cnt = 0; }          /* A3C0: hit -> flap-up + dive */
        if (!flapping && !(KRN_RANDOM() & 0xF)) { laying = 0xFF; flap_cnt = 0; }  /* A3DF: 1/16 per frame: egg */
        if (!(++parity & 1)) {                                      /* every 2nd frame */
            u8 d = (u8)*boss_col - (u8)(scroll_col % MAP_WIDTH);    /* A3FD: distance from the window's left edge */
            if (d < 0xC)      { wings = (wings - 1) & 3; if (step_right()) { flapping = 0xFF; flap_cnt = 0; } }   /* A41C */
            else if (d > 0xC) { wings = (wings + 1) & 3; step_left_0D(); }                                        /* A436 */
            if (d >= 0xC && !(KRN_RANDOM() & 0x1F)) { flapping = 0xFF; flap_cnt = 0; }                          /* A442 */
        }
    }
    compose_and_draw();                                             /* A455 */
}
/* 0xA57B  legs/flap timer: legs = (legs+1) mod 3, true when it wraps */
/* 0xA58F step_left_0D (col-1 if >= 0xD)  0xA59D step_left_11 (col-1 if >= 0x11, CF=1 at the limit)  0xA5AB step_right (col+1 if < 0x30) */
/* 0xA5BA  boss_damage: HP (A776) -= d, bar; at 0: cutscene, shots cleared (vec 30), flap/egg cleared; if diving: death counter 0, rising */
/* 0xA60A  Death: 40 frames of boss_dying with the flinch body; first 20 frames legs+head animate with sound 0x2C, then the fallen pose (0x11/0x12); then boss_defeated */
