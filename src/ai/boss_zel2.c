/*
 * boss_zel2.c — cavern 7 mid-boss "Paguro" (ZEL2.BIN = ZELRES3[15], 1563 bytes).
 * Map mp73 (level record ai = 18, tileset MPPB, cavern byte 1).  It is the
 * cavern-4 boss "Agar" (ZELA.BIN, boss_zela.c) recompiled with a different
 * name, palette and numbers — the code is byte-for-byte the same apart from
 * the 13 bytes listed below, and the frame lists use palette 0 instead of 2
 * (so it is drawn from the same ZELA.GRP cells in the other 4-colour set).
 * Boss info [A002] = A5DF:
 *   col 0x30, row 0x0C, HP 600, EXP 3000, camera column 12, knockback 0,
 *   name record A5EC {0x11, 0xBB, 6 "Paguro"}, gold 1600.
 * Contact A010: 30 for every part.  Frame tables A030[0..4] = A03A/A08A/
 * A0D0/A116/A166 (16 entries each, palette 0).
 *
 * Differences from ZELA (everything else: see boss_zela.c, addresses -0x0F
 * from A22D on):
 *   * damage (A216): d = damage_for_source(src) / 2 for EVERY source; the
 *     "magic 3 (source 4) counts x4" branch and its sound are gone, the hit
 *     sound is always 0x24.
 *   * the two bolts (A543 left / A550 right) are cell 0x05 / 0x04, 50 cells,
 *     damage 120 (Agar: cells 0x15/0x12, damage 80).
 *   * HP 600 / EXP 3000 / gold 1600 / name.
 *
 * Private state (A5DF..A602): A5DF col, A5E1 row, A5E2 HP, A5F6 anim, A5F7
 * firing (1 left, 2 right), A5F8 hopping, A5F9 hop direction, A5FA pause,
 * A5FB hop step, A5FC pause count, A5FD part counter, A5FE anim divider,
 * A5FF hit source, A600 rcol temp, A601 death counter, A602 blocked latch;
 * A603 = 12 x {pose, part} buffer.
 */
#include "ai_common.h"

static struct shot bolt_l = { 0, 0, 0x05, 0, 50, 4, 120 };         /* A543: left, 50 cells, damage 120 */
static struct shot bolt_r = { 0, 0, 0x04, 0, 50, 0, 120 };         /* A550: right */
static const u8 pose_of_anim[8] = { 1, 2, 3, 0, 3, 2, 1, 0 };     /* A4DB */

/* 0xA1B6  Frame entry — identical to boss_zela.c boss_entry() except: */
void boss_entry(void)
{
    u8 hit = readback_parts();                                      /* A1C4 */
    MAP_OBJECTS[0].col = 0xFFFF;
    if (hit) {                                                      /* A20F */
        u16 d = damage_for_source(hit) / 2;                         /* A224: half damage, all sources */
        sfx_request = 0x24;
        boss_damage(d);                                             /* A55D: HP A5E2; at 0 -> cutscene, vec 30 (shots cleared), firing off */
        if (*boss_col < hero_col(15)) { step_left(); step_left(); } else { step_right(); step_right(); }   /* A230: hop 2 cells away */
    }
    /* A254 on: hop (1/16 per frame, A265), hop table A2F8, attack_step A362,
     * part build A3B9, fire A4E3 (bolt from (col+1,row+3) left / (col+7,row+3)
     * right, spawned with vec 29), death A58B — as boss_zela.c. */
}

/* 0xA525 step_right (col+1 while < 0x32)   0xA534 step_left (col-1 while > 0x11)  — CF=1 at the limit */
/* 0xA58B  Death: 40 frames boss_dying; 21 frames of animation with sound 0x28 every 4th frame, then pose 2; then boss_defeated (3000 EXP, 1600 gold). */
