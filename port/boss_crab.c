/* boss_crab.c — cavern 1 boss "Cangrejo" (CRAB.BIN = ZELRES3[9], A000).
 * Ported line by line from src/ai/boss_crab.c; the pose matrices, the frame
 * lists and the [A002] block are read straight out of the overlay image, so
 * every number is the original's.  Map mp1d, sprites CRAB.GRP.
 *
 *   [A002] = A7C3: col 0x2B, row 0x0C, HP 150, EXP 120, camera column 12,
 *   knockback free, name "Cangrejo", gold 150.  Contact A010 = 6 everywhere.
 *   Pose matrices: pointer table A70A[pose] -> 6 rows x 10 columns of part
 *   classes (0xFF = empty); poses 0..8 share A71E, the jump pose 9 uses A75A.
 */
#include "boss.h"
#include <string.h>

#define A70A 0xA70A                     /* u16[10] pose -> matrix */

/* private state (A7DC..A7ED) */
enum { S_PREP_ON, S_PREP_IDX, S_LAND_ON, S_LAND_IDX, S_JUMP_ON, S_JUMP_IDX,
       S_SHOT_ON, S_SHOT_IDX, S_SHOT_ROW };
#define ST(n) (g->boss.st[n])

static const uint8_t prep_poses[8]  = { 7, 7, 8, 8, 8, 8, 8, 6 };          /* A481 */
static const uint8_t jump_script[]  = { 0xF1,0xF1,0xF1,0xF1,0xF1, 0xF8,0xF8,0xF8,
                                        0xF2,0xF2,0xF2,0xF2,0xF2, 0xFF };  /* A5F9 */
static const uint8_t land_poses[4]  = { 7, 8, 8, 0 };                      /* A5F5 */
static const uint8_t drop_script[]  = { 0x80,0x80,0x80,0x80,0x80,0x81,0x82,0x03,0x04,0xFF };  /* A5B6 */

/* the three lower parts 0x10..0x12 (and the jump pose's 0x90/0x14) are the
 * weak points: damage x8 instead of x4 (A351) */
static int crab_weak(uint8_t type) { return (type & 0x10) != 0; }

/* 0xA43E / 0xA44D: the crab may occupy map columns 0x10..0x31 */
static int step_left(Game *g)  { if (g->boss.col == 0x10) return 1; g->boss.col--; return 0; }
static int step_right(Game *g) { if (g->boss.col == 0x31) return 1; g->boss.col++; return 0; }

/* 0xA671  Build the part records for the current pose. */
static void crab_draw(Game *g)
{
    Boss *b = &g->boss;
    boss_parts_begin(g);
    unsigned m = boss_img16(g, A70A + 2u * (b->pose <= 9 ? b->pose : 0));
    if (m) {
        for (int r = 0; r < 6; r++)
            for (int c = 0; c < 10; c++) {
                uint8_t v = boss_img8(g, m + (unsigned)(r * 10 + c));
                if (v == 0xFF) continue;
                boss_part(g, (uint16_t)(b->col + c), (uint8_t)(b->row + r), v, b->pose);
            }
    }
    /* 0xA501: the projectile the jump drops, falling one row per frame */
    if (ST(S_SHOT_ON)) {
        uint8_t s = drop_script[ST(S_SHOT_IDX)];
        if (s == 0xFF) ST(S_SHOT_ON) = 0;
        else {
            if (s & 0x80) ST(S_SHOT_ROW) = (uint8_t)((ST(S_SHOT_ROW) + 1) & 0x3F);
            boss_part(g, b->sw[0], ST(S_SHOT_ROW), 0x35, (uint8_t)(s & 0xF));   /* A55C */
            ST(S_SHOT_IDX)++;
        }
    }
    boss_parts_end(g);
}

static void land_step(Game *g)                                             /* A5D3 */
{
    g->boss.pose = land_poses[ST(S_LAND_IDX) & 3];
    if (++ST(S_LAND_IDX) == 4) ST(S_LAND_ON) = 0;
}

/* 0xA4B9  Jump toward the hero: 5 rows up, 3 across, 5 rows down, one
 * horizontal step per frame; at script step 4 a projectile is dropped. */
static void jump_step(Game *g)
{
    Boss *b = &g->boss;
    b->pose = 9;
    uint8_t s = jump_script[ST(S_JUMP_IDX)];
    if (s == 0xFF) { ST(S_JUMP_ON) = 0; ST(S_LAND_IDX) = 0; ST(S_LAND_ON) = 0xFF; land_step(g); return; }
    if ((s & 0xF) != 8) b->row = (uint8_t)((b->row + ((s & 0xF) >> 1) - (s & 1)) & 0x3F);   /* A4D7 */
    if (s & 0xF0) { if (!b->walk_dir) step_left(g); else step_right(g); }
    if (ST(S_JUMP_IDX) == 4 && !ST(S_SHOT_ON)) {                           /* A501 */
        ST(S_SHOT_ON) = 0xFF; ST(S_SHOT_IDX) = 0;
        b->sw[0] = (uint16_t)(b->col + 4); ST(S_SHOT_ROW) = (uint8_t)((b->row + 3) & 0x3F);
    }
    ST(S_JUMP_IDX)++;
}

/* 0xA466  8 frames of crouch poses, then turn toward the hero and jump */
static void prepare_step(Game *g)
{
    Boss *b = &g->boss;
    if (++ST(S_PREP_IDX) == 8) {                                           /* A489 */
        uint16_t hero = boss_hero_col(g, 12);
        b->walk_dir = (uint8_t)((b->col + 5 < hero) ? 0xFF : 0);
        ST(S_PREP_ON) = 0; ST(S_JUMP_IDX) = 0; ST(S_JUMP_ON) = 0xFF;
        jump_step(g);
        return;
    }
    b->pose = prep_poses[ST(S_PREP_IDX) & 7];
}

/* 0xA607  Death: 40 frames, sound 0x23 every other frame for the first 30;
 * the poses swing 0..5 for 20 frames then settle on 8. */
static int death_step(Game *g)
{
    Boss *b = &g->boss;
    uint8_t t = b->death_cnt;
    if (t < 0x1E && !(t & 1)) g->sfx_request = 0x23;
    if (boss_death_tick(g) >= 0x28) return 0;
    if (t < 0x14) {
        if (!b->walk_dir) { if (++b->pose >= 6) { b->pose = 5; b->walk_dir = 0xFF; } }
        else              { if (--b->pose == 0xFF) { b->pose = 0; b->walk_dir = 0; } }
    } else b->pose = 8;
    return 1;
}

/* 0xA2F0  Frame entry. */
void boss_crab_entry(Game *g)
{
    Boss *b = &g->boss;
    uint8_t hit = boss_readback(g, crab_weak);                             /* A2FE..A349 */
    boss_hit_flash(g, hit != 0);   /* A6BC: every part this frame carries hit bit 5 */

    if (!g->boss_cutscene && hit) {                                        /* A351 */
        unsigned d = (unsigned)damage_for_source(g, (uint8_t)(hit & 0x1F)) * 4u;
        if (hit & 0x80) d *= 2;
        boss_damage(g, d);                                                 /* A796 */
        g->sfx_request = 0x22;
        uint16_t hero = boss_hero_col(g, 12);                              /* A37E */
        if (b->col + 5 < hero) { step_left(g); step_left(g); }
        else                   { step_right(g); step_right(g); }
    }

    if (ST(S_JUMP_ON))            jump_step(g);                            /* A4B9 */
    else if (ST(S_LAND_ON))       land_step(g);                            /* A5D3 */
    else if (g->boss_cutscene)  { if (!death_step(g)) { boss_parts_begin(g); boss_parts_end(g); return; } }
    else if (ST(S_PREP_ON))       prepare_step(g);                         /* A466 */
    else if (!(krn_random(g) & 7)) { ST(S_PREP_IDX) = 0; ST(S_PREP_ON) = 0xFF; prepare_step(g); }  /* A45C */
    else {                                                                 /* A3F0: walk one cell every 2nd frame */
        b->parity++;
        if (b->parity & 1) { crab_draw(g); return; }
        if (!b->walk_dir) { if (step_left(g))  b->walk_dir = 0xFF; if (++b->pose >= 6) b->pose = 0; }
        else              { if (step_right(g)) b->walk_dir = 0;    if (--b->pose == 0xFF) b->pose = 5; }
    }
    crab_draw(g);                                                          /* A671 */
}
