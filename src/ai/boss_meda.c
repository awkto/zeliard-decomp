/*
 * boss_meda.c — cavern 5 boss "Vista" (jellyfish/medusa) (MEDA.BIN = ZELRES3[13], 2184 bytes).
 * Map mp5d.  Sprites MEDA.GRP (ZELRES3[68]).  Boss info [A002] = A716:
 *   col 0x30, row 0x0B, HP 700, EXP 3000, camera column 12, knockback 0,
 *   name record A723 {0x11, 0xBB, 5 "Vista"}, gold 800.
 * Contact A010: 30 for every part.  Frame tables A030: classes 0..3
 * (A050..A140), 0xE/0xF (A18B/A1DB: the tentacle/attack overlays).
 *
 * Image: 14 rows x 12 columns of {type, frame} words in A738 (0x150 bytes),
 * composed each frame (A438) from layered bitmaps (A539: 8 bits per row,
 * word list consumed per set bit):
 *   body       A5DC/A606 at (0,0), 13 rows
 *   right side A613/A623 at (1,8), 11 rows
 *   tentacles  [A62E + 2*A72F] / A682 at (4,3), 5 rows   (A72F = 0..5 by hero position, 5 = dead)
 *   attack     [A687 + 2*A730] / [A6C7 + 2*A730] at (4,7), 5 rows  (A730 = 0..4)
 * Part row = boss_row + r, col = boss_col + c (A4AA..A537).
 *
 * State: A716 col, A718 row, A719 HP, A72F tentacle pose, A730 attack
 * frame, A732 hit source (|0x80 if the part had type bit 3), A733 death
 * counter, A734 vertical state (0 / FF down / 7F up), A735 delay, A736
 * horizontal direction, A737 attack pause.
 */
#include "ai_common.h"

static struct shot drip = { 0, 0, 0x30, 0, 50, 6, 80 };            /* A6E0: straight DOWN, cell 0x30, 50 cells, damage 80 */

/* 0xA1EA  Frame entry. */
void boss_entry(void)
{
    u8 hit = readback_parts();                                      /* A1F8 */
    MAP_OBJECTS[0].col = 0xFFFF;
    if (hit & 0x1F) {                                               /* A24B */
        u16 d = damage_for_source(hit & 0x1F) / 8;                  /* one eighth ... */
        if ((hit & 0x1F) == 1 && sword >= 4) { d *= 32; sfx_request = 0x2D; }   /* ... unless sword 4+: x4 */
        else sfx_request = 0x2E;
        boss_damage(d);                                             /* A575: HP A719; at 0 cutscene + vec 30 */
    }
    if (boss_cutscene) { death_step(); return; }                    /* A5A6 */
    if (!vstate) {                                                  /* A291: cruising at row 7 */
        if (*boss_row == 7 && hero_col(16) > *boss_col + 4 && hero_col(16) <= *boss_col + 6) { delay = 3; vstate = 0xFF; }   /* A298: hero right below -> dive */
        if (!hdir) { if (step_left_0A())  hdir = 0xFF; }           /* A2D3: 1 cell/frame, col 0x0A..0x31 */
        else       { if (step_right_31()) hdir = 0; }
        tentacle_pose_by_hero();                                    /* A317: A72F = A70D[boss_col - 9] ... (row table) */
    } else if (delay) delay--;
    else if (vstate & 0x80) { if (row_down_to_0B()) vstate = 0x7F; }   /* A2FF: dive 4 rows */
    else                    { if (row_up_to_07())   vstate = 0; }      /* A30B: back up */
    tentacles_by_hero();                                            /* A358: A72F from the hero's column relative to boss_col+1..+10 / -6..+17 */
    if (pause) pause--;                                             /* A328 */
    else {
        if (++attack == 5) { pause = 3; attack = 0; }               /* A336: 5-frame attack cycle, 3-frame pause */
        if (attack == 4) {                                          /* A3C1: two drips from (col+6,row+12) and (col+7,row+10) */
            if (!map_col_to_ring(*boss_col + 6, &drip.col)) { drip.row = (*boss_row + 12) & 0x3F; fight_shot_spawn(&drip); }
            if (!map_col_to_ring(*boss_col + 7, &drip.col)) { drip.row = (*boss_row + 10) & 0x3F; fight_shot_spawn(&drip); }
        }
    }
    compose_and_draw();                                             /* A438 */
}
/* 0xA5A6  Death: 40 frames boss_dying; first 20 frames attack frame 0, tentacles
 * follow the hero, sound 0x23 every frame; then tentacle pose 5; then boss_defeated. */
