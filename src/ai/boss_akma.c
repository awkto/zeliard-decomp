/*
 * boss_akma.c — cavern 8 boss "Alguien" (AKMA.BIN = ZELRES3[17], 2810 bytes @ A000).
 * Map mp8d (70 cells wide, level record ai = 15).  Sprites AKMA.GRP.
 * Boss info [A002] = AA06:
 *   col 0x2A (AA06), row 0x00 (AA08), HP 800 (AA09), EXP 30000, camera column
 *   12, knockback 0, name record AA13 {0x10, 0x2BB, 7 "Alguien"}, gold 3800.
 * Contact A010: 40 for every class except 6 (the beam) = 80.  Frame tables
 * A030: classes 0..6 (A07E..).  No drops.
 *
 * A winged demon that flies back and forth across the top of the room and
 * fires a diagonal beam toward the floor at each turn.
 *
 * Image: 13 columns x 16 rows, cell granularity, buffer AA2A (column-major,
 * stride 16, FF = empty).  A7CC pastes the body: 13 columns x 2 bitmap bytes
 * (16 rows) from list [A7F4 + 2*(anim&3)] / bitmap [A876 + ..] when flying
 * right (facing = FF) or [A7EE]/[A870] when flying left; anim = 0..2 (the
 * three wing positions: 25/16/18 parts, classes 0/1/2 + 5).  Then two
 * patches: the wing tips, 5 columns x 2 rows at buffer (3, 13) [right] /
 * (5, 13) [left] from A92C / A918 (+10 on odd frames: A53C), and the face,
 * 1 column x 2 rows at (10, 9) [right] / (0, 9) [left] from A94A / A940 +
 * 2*face (face 0 normal, 2/3 attacking, 1 death).
 * Part records (A57E..A613): {col = boss_col + c, row = boss_row + r, type =
 * class (not solid), hit = FACING_RIGHT while flying right (so the A070
 * mirrored tables are used) | 0x20 on a hit, phase = byte}.
 *
 * Damage (A38C): FULL damage_for_source(), no scaling, sound 0x22.  The
 * read-back marks a hit on a class-5 part with 0x80 but nothing uses it.
 *
 * Private state: AA1E part counter, AA1F hit source, AA20 anim (0..2), AA21
 * direction (0 left, FF right; initial FF), AA22 rcol temp, AA23 frame
 * counter, AA24 face, AA25 attacking, AA26 beam length, AA27 beam retracting,
 * AA28 beam variant (1 = steep), AA29 death counter, AA2A.. buffer.
 */
#include "ai_common.h"

static u16 *const boss_col = (u16 *)0xAA06;  static u8 *const boss_row = (u8 *)0xAA08;  static u16 *const boss_hp = (u16 *)0xAA09;
static u8 anim, dir = 0xFF, frame, face, attacking, beam_len, retracting, steep, death_cnt;

/* flight path: row as a function of column, index (col - 10) / 2 for columns 10..50 */
static const u8 path_right[21] = { 60,60,61,62,63,63, 0,0,0, 1,1,1,1,1,1,1,1,1,1,1,1 };   /* A954: high at the left, swooping down to row 1 */
static const u8 path_left[21]  = { 1,1,1,1,1,1,1,1,1,1,1,1, 0,0,0, 63,63,62,61,60,60 };   /* A969: mirror image */

/* 0xA32B  Frame entry. */
void boss_entry(void)
{
    u8 hit = readback_parts();                                      /* A32F..A382: first pending hit; class 5 -> |0x80 (unused) */
    MAP_OBJECTS[0].col = 0xFFFF;
    if (hit) { sfx_request = 0x22; boss_damage(damage_for_source(hit & 0x1F)); }   /* A38C..A3A8 */
    if (boss_cutscene) { death_step(); return; }                    /* A3AB */

    face = 0;                                                       /* A3B5 */
    if (++anim == 3) anim = 0;                                      /* A3BA: wing beat, sound 0x2B on frame 1 */
    if (anim == 1) sfx_request = 0x2B;
    frame++;

    /* 2 cells per frame along the path; at the end of the room climb 2 rows
     * per frame until row 0x3D (3 rows above the top row), then turn and
     * fire.  The beam variant depends on where the hero is. */
    if (!dir) {                                                     /* A3D5: flying left */
        if (!step_left2()) { *boss_row = path_left[(*boss_col - 10) / 2]; }   /* A47A */
        else if ((*boss_row = (*boss_row - 2) & 0x3F) == 0x3D) {
            dir = 0xFF; start_beam(); steep = hero_col() < 40;      /* A3F5..A429: hero on the left half -> steep */
        }
    } else {                                                        /* A42E: flying right */
        if (!step_right2()) { *boss_row = path_right[(*boss_col - 10) / 2]; }
        else if ((*boss_row = (*boss_row - 2) & 0x3F) == 0x3D) {
            dir = 0; start_beam(); steep = hero_col() >= 20;        /* A441..A477 */
        }
    }

    if (attacking) {                                                /* A492: beam grows 1 segment/frame to 8 (7 if steep), then shrinks */
        face = steep + 2;
        if (!retracting) { if (++beam_len >= 7 + !steep) retracting = 0xFF; }
        else if (--beam_len == 0) attacking = 0;
    }
    draw();                                                         /* A4F7 */
    draw_beam();                                                    /* A617 */
}

/* 0xA4D4 / 0xA4E6  two cells at a time, columns 0x0C..0x33; true = blocked */
static bool step_left2(void)  { if (*boss_col - 2 <= 9)    return true; *boss_col -= 2; return false; }
static bool step_right2(void) { if (*boss_col + 2 >  0x33) return true; *boss_col += 2; return false; }
static u16  hero_col(void)    { u16 c = scroll_col + hero_scr_col; return c >= MAP_WIDTH ? c - MAP_WIDTH : c; }   /* A40E..A421 */
static void start_beam(void)  { retracting = 0; beam_len = 0; attacking = 0xFF; sfx_request = 0x34; }

/* 0xA617  The beam: beam_len parts of type 0x26 (class 6, sword-immune,
 * contact 80) hanging off the body toward the floor in the flight
 * direction; segment k (k = 1..len-1) uses frame 3 (shallow) / 7 (steep),
 * the tip frame 2 / 6:
 *   flying left,  shallow: (col - 2k,      row + 9 + k)      (A638)
 *   flying left,  steep:   (col + 1 - 2k,  row + 9 + 2k)     (A6E0)
 *   flying right, shallow: (col + 11 + 2k, row + 9 + k)      (A687)
 *   flying right, steep:   (col + 10 + 2k, row + 9 + 2k)     (A734)
 * Each part is a fresh record (A78A) with hit = FACING_RIGHT when flying right. */
static void draw_beam(void);

/* 0xA97E  HP -= d (floor 0), bar [200C]; at 0 (and not already in the
 * cutscene): death counter 0, beam off, boss_cutscene. */
static void boss_damage(u16 d)
{
    *boss_hp = (*boss_hp > d) ? *boss_hp - d : 0;
    VID_200C(BX = *boss_hp);
    if (*boss_hp == 0 && !boss_cutscene) { death_cnt = 0; attacking = 0; boss_cutscene = 0xFF; }
}

/* 0xA9B0  Death: 40 frames of boss_dying, hovering in place; the first 30
 * keep the wings beating with the face toggling 0/1 and sound 0x37 every
 * 4th frame, then wings frame 1 / face 1; then boss_defeated (30000 EXP,
 * 3800 gold). */
static void death_step(void)
{
    if (death_cnt >= 0x28) { boss_defeated = 0xFF; return; }
    boss_dying = 0xFF; death_cnt++;
    if (death_cnt - 1 < 0x1E) { if (++anim == 3) anim = 0; frame++; face = (face + 1) & 1; if (!(frame & 3)) sfx_request = 0x37; }
    else                      { anim = 1; face = 1; }
    draw();
}

/* 0xA4F7  Compose body + wing tips + face into AA2A and emit the records. */
static void draw(void);
