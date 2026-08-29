/*
 * boss_crab.c — cavern 1 boss "Cangrejo" (CRAB.BIN = ZELRES3[9], 2030 bytes @ A000).
 * Map mp1d (73 cells wide, row bias 12).  Sprites CRAB.GRP (ZELRES3[64]).
 *
 * Boss overlays work differently from eai*: fight.bin calls [A000] ONCE per
 * frame (8D1D) and the overlay rebuilds the whole C010 object list itself:
 * the boss is a matrix of 2x2 parts, each part a fresh 16-byte record with
 * type = part class (its A030 frame table), phase = current pose, and a
 * marker in the ring.  fight.bin only sees those records for the sword/
 * contact tests; next frame the overlay reads the pending-hit bits back
 * (A323) before rebuilding.  Vector 27 (map col -> ring col) and 20 (ring
 * address) are the only fight services used besides 28 (damage lookup).
 *
 * Boss info block [A002] = A7C3:
 *   +0 u16 boss map column (0x2B)   +2 u8 boss top row (0x0C)
 *   +3 u16 HP = 150 (also the initial HP-bar value passed to video [200A]/[200C])
 *   +5 u16 EXP = 120   +7 hero camera column 12   +8 knockback-left = 0
 *   +9 u16 -> A7D0 name record passed to video [2010]: {x 0x10, y 0x00BB, len 8, "Cangrejo"}
 *   +B u16 gold = 150
 * Contact table A010: 6 for every part.  No drops, no per-class EXP.
 * Frame tables A030: parts 0..8 (A05C..A1E2, 10 poses each), 0x10..0x12
 * (A214/A246/A273: the three lower weak-point parts), 0x14 (A2A5), 0x15
 * (A2D7: the dropped projectile).
 * Pose matrices (6 rows x 10 columns of part classes, FF = empty), pointer
 * table A70A: poses 0..8 -> A71E, pose 9 (jump) -> A75A:
 *   A71E row0: -- -- -- 00 -- 01 -- -- -- --      (eye stalks)
 *        row2: 02 -- 03 -- 04 -- 05 -- 06 --      (body)
 *        row4: 07 -- 10 -- 11 -- 12 -- 08 --      (claws + weak points 10..12)
 *   A75A: the same parts rearranged for the jump (00 at col 4; 03/05 row 1;
 *         02/14/06 row 2; 90/12 row 3; 07/08 row 4).  0x90 = 0x80|0x10 solid.
 *
 * Private state (A7DC..A7ED): part counter, hit source|0x80 (weak point),
 * pose A7DE, draw row A7DF, walk direction A7E0 (0 left / FF right), frame
 * parity A7E1, prepare-seq flag/idx A7E2/A7E3, landing flag/idx A7E4/A7E5,
 * jump flag/idx A7E6/A7E7, projectile flag/idx/col/row A7E8..A7EC, death
 * counter A7ED.  HP at A7C6 (the +3 word).
 */
#include "ai_common.h"

static u16 *const boss_col = (u16 *)0xA7C3;  static u8 *const boss_row = (u8 *)0xA7C5;  static u16 *const boss_hp = (u16 *)0xA7C6;
static u8 pose, walk_dir, parity, prep_on, prep_idx, land_on, land_idx, jump_on, jump_idx, shot_on, shot_idx, death_cnt;
static u16 shot_col; static u8 shot_row;

static const u8 prep_poses[8]  = { 7, 7, 8, 8, 8, 8, 8, 6 };            /* A481: crouch before the jump */
static const u8 jump_script[]  = { 0xF1,0xF1,0xF1,0xF1,0xF1, 0xF8,0xF8,0xF8, 0xF2,0xF2,0xF2,0xF2,0xF2, 0xFF };   /* A5F9 */
                                  /* high nibble != 0: one horizontal step in walk_dir; low nibble: 8 none, 1 up, 2 down */
static const u8 land_poses[4]  = { 7, 8, 8, 0 };                        /* A5F5 */
static const u8 drop_script[]  = { 0x80,0x80,0x80,0x80,0x80,0x81,0x82,0x03,0x04,0xFF };   /* A5B6: bit7 = fall one row, low = frame */

/* 0xA2F0  Frame entry. */
void boss_entry(void)
{
    /* 1. read back last frame's parts: restore covered cells, note a pending hit (A2FE..A347) */
    u8 hit = 0, n = 0;
    for (struct enemy *o = MAP_OBJECTS; o->col != 0xFFFF; o++, n++) {
        if (map_col_to_ring(o->col, &o->rcol)) continue;
        *ring_addr(o->row, o->rcol) = under_sprite[n];
        if ((o->hit & 0x40) && !(hit & 0x80)) hit = (o->hit & 0x1F) | ((o->type & 0x10) ? 0x80 : 0);
    }
    MAP_OBJECTS[0].col = 0xFFFF;                                    /* A349 */

    /* 2. apply the hit: damage x4, x8 on a weak point (type & 0x10); sound 0x22; hop 2 cells away */
    if (!boss_cutscene && hit) {                                    /* A351 */
        u16 d = damage_for_source(hit & 0x1F) * 4; if (hit & 0x80) d *= 2;
        boss_damage(d);                                             /* A796 */
        sfx_request = 0x22;
        u16 hero = min(scroll_col + 12, MAP_WIDTH);                 /* A37E */
        if (*boss_col + 5 < hero) { step_left(); step_left(); } else { step_right(); step_right(); }
    }

    /* 3. behaviour */
    if (jump_on)            jump_step();                            /* A4B9 */
    else if (land_on)       { pose = land_poses[land_idx++]; if (land_idx == 4) land_on = 0; }   /* A5D3 */
    else if (boss_cutscene) death_step();                           /* A607 */
    else if (prep_on)       prepare_step();                         /* A466 */
    else if (!(KRN_RANDOM() & 7)) { prep_idx = 0; prep_on = 0xFF; prepare_step(); }   /* A45C */
    else {                                                          /* walk: one cell every 2nd frame, poses 0..5 */
        parity++;
        if (parity & 1) { draw(); return; }
        if (!walk_dir) { if (step_left())  walk_dir = 0xFF; if (++pose >= 6) pose = 0; }          /* A3F0 */
        else           { if (step_right()) walk_dir = 0;    if (--pose == 0xFF) pose = 5; }       /* A41E */
    }
    draw();                                                         /* A671 */
}

/* 0xA43E / 0xA44D  the crab may occupy map columns 0x10..0x31 */
static bool step_left(void)  { if (*boss_col == 0x10) return true; (*boss_col)--; return false; }
static bool step_right(void) { if (*boss_col == 0x31) return true; (*boss_col)++; return false; }

/* 0xA796  HP -= d (floor 0), redraw the bar; at 0 start the death cutscene */
static void boss_damage(u16 d)
{
    *boss_hp = (*boss_hp > d) ? *boss_hp - d : 0;
    VID_200C(BX = *boss_hp);
    if (*boss_hp == 0 && !boss_cutscene) { death_cnt = 0; boss_cutscene = 0xFF; }
}

/* 0xA466  8 frames of crouch poses, then turn toward the hero and jump */
static void prepare_step(void)
{
    if (++prep_idx == 8) {                                          /* A489 */
        u16 hero = min(scroll_col + 12, MAP_WIDTH);
        walk_dir = (*boss_col + 5 < hero) ? 0xFF : 0;
        prep_on = 0; jump_idx = 0; jump_on = 0xFF; jump_step(); return;
    }
    pose = prep_poses[prep_idx];
}

/* 0xA4B9  Jump toward the hero: pose 9, 5 rows up / 3 across / 5 rows down,
 * one horizontal step per frame; at script index 4 a projectile is dropped
 * from (col+4, row+3) (A501..A548) which falls one row per frame following
 * drop_script and vanishes at the end (A55C: part type 0x35, frame table
 * 0x15, contact 6). */
static void jump_step(void)
{
    pose = 9;
    u8 s = jump_script[jump_idx];
    if (s == 0xFF) { jump_on = 0; land_idx = 0; land_on = 0xFF; land_step(); return; }   /* A5C0 */
    if ((s & 0xF) != 8) *boss_row = (*boss_row + ((s & 0xF) >> 1) - (s & 1)) & 0x3F;    /* A4D7 */
    if (s & 0xF0) { if (!walk_dir) step_left(); else step_right(); }
    draw(); jump_idx++;
}

/* 0xA607  Death: 40 frames; sound 0x23 every other frame during the first 30,
 * boss_dying set (fight.bin plays the explosion fx), poses swing 0..5 for
 * the first 20 frames then pose 8; then boss_defeated (EXP/gold awarded by
 * fight.bin 71DA). */
static void death_step(void)
{
    u8 t = death_cnt;
    if (t >= 0x28) { boss_defeated = 0xFF; return; }
    if (t < 0x1E && !(t & 1)) sfx_request = 0x23;
    boss_dying = 0xFF;
    if (t < 0x14) { death_cnt++; if (!walk_dir) { if (++pose >= 6) { pose = 5; walk_dir = 0xFF; } } else { if (--pose == 0xFF) { pose = 0; walk_dir = 0; } } }
    else { death_cnt++; pose = 8; }
    draw();
}

/* 0xA671  Build the part records for the current pose (matrix A70A[pose]):
 * for each of 6 rows x 10 columns with class != FF and inside the ring:
 * {col, row, rcol, type = class, hit = 0 (0x20 if flashing), phase = pose};
 * marker 0x80|i in the ring, old cell saved.  Then 0xA501: the falling
 * projectile part, if active. */
static void draw(void);
