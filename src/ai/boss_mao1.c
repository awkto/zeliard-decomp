/*
 * boss_mao1.c — cavern 9 "Jashiin" APPEARANCE (MAO1.BIN = ZELRES3[18], 1437 bytes).
 * Map mp90 (42 cells wide, level record ai = 16, enemies = 16 = MAO1.GRP,
 * music mmao, flags 0x5B: [E6] boss_room set, FF34 clear).
 * Boss info [A002] = A581:
 *   col (A581, written by the script: 0x10 for poses 0..2, 0x0D from pose 3),
 *   row 0x01 (A583), HP 250 (A584), EXP 200, camera column 5, knockback FF,
 *   name record A58E {0x11, 0x2BB, 7 "Jashiin"}, gold 0.
 * Contact A010: 0 for every class.  Frame tables A030: classes 0..6.
 *
 * This is NOT a fight: the overlay is a scripted cutscene.  The entry never
 * looks at the hit bits and nothing decrements HP; the HP/EXP words are only
 * there because fight.bin reads the block.  Each frame (A23C):
 *   1. read back the parts (restore the covered cells only, A240..A271),
 *      terminate the list;
 *   2. A59C++ and fetch script[A59C] from A3BB:
 *        0x00..0x0A  pose: boss_col = pose < 3 ? 0x10 : 0x0D; draw pose
 *        0x80|n      show text n (A376): clear the text box with video [2000]
 *                    (BX = 0x0E1E, CX = 0x3410, AL = 0xFF), then [202A] with
 *                    SI = string, BX = string.x + 0x3A, CL = 0x22
 *        0xC0        clear two rows of the E939 screen copy (2 x 26 cells of
 *                    0xFE = force redraw of the text area) (A3A2)
 *        0xE0        sound 0x38 (A370)
 *        0xFF        [E6] = 0: leave boss mode, the map continues as a normal
 *                    level (A36A; fight.bin then runs its ordinary enemy pass)
 *      after a command the pose logic (A290) redraws the current pose.
 *
 * Script (A3BB, one byte per frame):
 *   10 x pose 0, text 0, 30 x pose 0, clear, 0 1 1 2 2 3 3 3 3 3, text 1,
 *   29 x pose 3, clear, 3 3 3 4 4 5, text 2, 30 x pose 5, clear,
 *   5 5 6 6 7, sound 0x38, 8 8 9 9 A A A, end.
 * Texts (A448.., {u16 x, chars, 0xFF}):
 *   0 (x 8):  "Finally, you reached me."
 *   1 (x 24): "I enjoyed your show."
 *   2 (x 8):  "Come on!  I\ll kill you."      ('\' is the font's apostrophe)
 *
 * Image (A290..A34F): 6 columns x 8 rows of 2x2 parts at TWO-cell spacing
 * (col = boss_col + 2c, row = boss_row + 2r), list [A495 + 2*pose], bitmap
 * [A52F + 2*pose] (one byte per column, bit 7 = row 0).  Part records:
 * {type = class, phase = frame, hit = 0}; no solid bit, no contact damage.
 * Poses 0..2 use class 0 only (6-10 parts: the small human form), 3..6
 * classes 1-2 (11-13 parts), 7..0xA add classes 3..6 (12-17 parts): Jashiin
 * grows from the human figure into the demon.
 */
#include "ai_common.h"

static u16 *const boss_col = (u16 *)0xA581;  static u8 *const boss_row = (u8 *)0xA583;
static u8 script_pos, pose;                                         /* A59C, A59B */
static const u8 script[] = { /* A3BB */
    0,0,0,0,0,0,0,0,0,0, 0x80, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0xC0,
    0,1,1,2,2,3,3,3,3,3, 0x81, 3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3, 0xC0,
    3,3,3,4,4,5, 0x82, 5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5, 0xC0,
    5,5,6,6,7, 0xE0, 8,8,9,9,10,10,10, 0xFF };
static const char *const text[3] = { "Finally, you reached me.", "I enjoyed your show.", "Come on!  I\\ll kill you." };   /* A442 -> A448/A463/A47A */

/* 0xA23C  Frame entry. */
void boss_entry(void)
{
    readback_parts_restore_only();                                  /* A240 */
    MAP_OBJECTS[0].col = 0xFFFF;                                    /* A273 */
    u8 c = script[++script_pos];                                    /* A27B */
    if (!(c & 0x80)) pose = c;                                      /* A28D */
    else switch (c & 0xF0) {                                        /* A350 */
        case 0x80: show_text(text[c & 0xF]); break;                 /* A376 */
        case 0xC0: clear_text_rows(); break;                        /* A3A2 */
        case 0xE0: sfx_request = 0x38; break;                       /* A370 */
        default:   if (c == 0xFF) { boss_room = 0; return; } return; /* A364: [E6] = 0 */
    }
    *boss_col = (pose < 3) ? 0x10 : 0x0D;                           /* A290 */
    draw_pose(pose);                                                /* A2A1..A34F */
}
