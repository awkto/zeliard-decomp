/* boss_meda.c — cavern 5 boss "Vista" (MEDA.BIN = ZELRES3[13]).  Ported from
 * src/ai/boss_meda.c.
 *
 *   [A002] = A716: col 0x30, row 0x0B, HP 700, EXP 3000, camera column 12,
 *   knockback free, name "Vista", gold 800.  Contact A010 = 30 everywhere.
 *
 * Image: a 14 x 12 buffer of {type, frame} pairs composed from four layers.
 * Every layer is a list of `{u8 type, u8 frame}` words plus an 8-bit-per-row
 * column bitmap; the popcount of each bitmap matches the length of its list
 * exactly, which is how the two were told apart:
 *   body        list A5DC, bitmap A606 (13 rows) at (0,0)   — 21 parts
 *   right side  list A613, bitmap A623 (11 rows) at (1,8)   —  8 parts
 *   tentacles   list [A62E + 2p], bitmap A682 (5 rows) at (4,3)
 *   attack      list [A687 + 2f], bitmap [A6C7 + 2f] (5 rows) at (4,7)
 * Part cell = (boss_col + col, boss_row + row).
 *
 * Damage: one eighth of `damage_for_source`, except a sword hit with sword >= 4,
 * which is x32 of the eighth (= x4).  Death: 40 frames, sound 0x23.
 */
#include "boss.h"
#include <string.h>

enum { S_TENT, S_ATTACK, S_VSTATE, S_DELAY, S_HDIR, S_PAUSE };
#define ST(n) (g->boss.st[n])
#define MEDA_TENT 0xA6ED                /* 41-entry ramp 0x0C..0x07..0x0C */

static int meda_weak(uint8_t type) { return (type & 0x08) != 0; }

static int step_left_0A(Game *g)  { if (g->boss.col <= 0x0A) return 1; g->boss.col--; return 0; }
static int step_right_31(Game *g) { if (g->boss.col >= 0x31) return 1; g->boss.col++; return 0; }

/* A539: paint one layer of {type, frame} words into the 14x12 buffer */
static void meda_layer(const Game *g, uint8_t *buf, unsigned list, unsigned bm, int rows,
                       int row0, int col0)
{
    if (!list || !bm) return;
    unsigned k = 0;
    for (int r = 0; r < rows; r++) {
        uint8_t bits = boss_img8(g, bm + (unsigned)r);
        for (int c = 0; c < 8; c++) {
            if (!(bits & (0x80 >> c))) continue;
            uint8_t type = boss_img8(g, list + 2 * k);
            uint8_t frame = boss_img8(g, list + 2 * k + 1);
            k++;
            int rr = row0 + r, cc = col0 + c;
            if (rr < 0 || rr >= 14 || cc < 0 || cc >= 12) continue;
            buf[(rr * 12 + cc) * 2] = type;
            buf[(rr * 12 + cc) * 2 + 1] = frame;
        }
    }
}

/* A438 + A4AA: compose and place */
static void meda_draw(Game *g)
{
    Boss *b = &g->boss;
    uint8_t buf[14 * 12 * 2];
    memset(buf, 0xFF, sizeof buf);
    meda_layer(g, buf, 0xA5DC, 0xA606, 13, 0, 0);                       /* body */
    meda_layer(g, buf, 0xA613, 0xA623, 11, 1, 8);                       /* right side */
    meda_layer(g, buf, boss_img16(g, 0xA62E + 2u * (ST(S_TENT) % 6u)), 0xA682, 5, 4, 3);
    meda_layer(g, buf, boss_img16(g, 0xA687 + 2u * (ST(S_ATTACK) % 5u)),
                       boss_img16(g, 0xA6C7 + 2u * (ST(S_ATTACK) % 5u)), 5, 4, 7);
    boss_parts_begin(g);
    for (int r = 0; r < 14; r++)
        for (int c = 0; c < 12; c++) {
            uint8_t t = buf[(r * 12 + c) * 2], f = buf[(r * 12 + c) * 2 + 1];
            if (t == 0xFF) continue;
            boss_part(g, (uint16_t)(b->col + c), (uint8_t)(b->row + r), t, f);
        }
    boss_parts_end(g);
}

/* A317/A358: the tentacles lean toward the hero.  The 41-entry ramp at A6ED
 * runs 0x0C..0x07..0x0C and the pose is `entry - 7`; the *index* is inferred
 * (src/ai/boss_meda.c says "A70D[boss_col - 9]", which would run off the end
 * of the table into the [A002] block), so the port uses the hero's offset from
 * the boss's column biased by 6, the range the same note gives. */
static void meda_tentacles(Game *g)
{
    int idx = (int)boss_hero_col(g, 16) - (int)g->boss.col + 6;
    if (idx < 0) idx = 0;
    if (idx > 40) idx = 40;
    uint8_t v = boss_img8(g, MEDA_TENT + (unsigned)idx);
    ST(S_TENT) = (uint8_t)(v >= 7 ? v - 7 : 0);
}

/* A5A6: 40 frames; the first 20 keep attack frame 0 and follow the hero with
 * sound 0x23, then the tentacles settle on pose 5. */
static void meda_death(Game *g)
{
    uint8_t t = g->boss.death_cnt;
    if (boss_death_tick(g) >= 0x28) { boss_parts_begin(g); boss_parts_end(g); return; }
    if (t < 0x14) { g->sfx_request = 0x23; ST(S_ATTACK) = 0; meda_tentacles(g); }
    else ST(S_TENT) = 5;
    meda_draw(g);
}

/* 0xA1EA  Frame entry. */
void boss_meda_entry(Game *g)
{
    Boss *b = &g->boss;
    uint8_t hit = boss_readback(g, meda_weak);                          /* A1F8 */
    boss_hit_flash(g, hit != 0);   /* A4F5: every part this frame carries hit bit 5 */

    if (hit & 0x1F) {                                                   /* A24B */
        unsigned d = (unsigned)damage_for_source(g, (uint8_t)(hit & 0x1F)) / 8u;
        if ((hit & 0x1F) == 1 && g->sword >= 4) { d *= 32; g->sfx_request = 0x2D; }
        else g->sfx_request = 0x2E;
        boss_damage(g, d);                                              /* A575 */
        if (g->boss_cutscene) shots_clear(g);
    }
    if (g->boss_cutscene) { meda_death(g); return; }                    /* A5A6 */

    if (!ST(S_VSTATE)) {                                                /* A291: cruise at row 7 */
        uint16_t hero = boss_hero_col(g, 16);
        if (b->row == 7 && hero > b->col + 4 && hero <= b->col + 6) { ST(S_DELAY) = 3; ST(S_VSTATE) = 0xFF; }
        if (!ST(S_HDIR)) { if (step_left_0A(g))  ST(S_HDIR) = 0xFF; }
        else             { if (step_right_31(g)) ST(S_HDIR) = 0; }
        meda_tentacles(g);                                              /* A317 */
    } else if (ST(S_DELAY)) ST(S_DELAY)--;
    else if (ST(S_VSTATE) & 0x80) { if (b->row >= 0x0B) ST(S_VSTATE) = 0x7F; else b->row++; }   /* A2FF: dive 4 rows */
    else                          { if (b->row <= 0x07) ST(S_VSTATE) = 0;    else b->row--; }   /* A30B */
    meda_tentacles(g);                                                  /* A358 */

    if (ST(S_PAUSE)) ST(S_PAUSE)--;                                     /* A328 */
    else {
        if (++ST(S_ATTACK) == 5) { ST(S_PAUSE) = 3; ST(S_ATTACK) = 0; } /* A336 */
        if (ST(S_ATTACK) == 4) {                                        /* A3C1: two drips */
            Shot drip;                                                  /* A6E0: cell 0x30, 50 cells
                                                                         * straight down, damage 80 */
            boss_shot_template(g, 0xA6E0, &drip);
            if (!ai_map_col_to_ring(g, (uint16_t)(b->col + 6), &drip.col)) {
                drip.row = (uint8_t)((b->row + 12) & 0x3F); shot_spawn(g, &drip);
            }
            if (!ai_map_col_to_ring(g, (uint16_t)(b->col + 7), &drip.col)) {
                drip.row = (uint8_t)((b->row + 10) & 0x3F); shot_spawn(g, &drip);
            }
        }
    }
    meda_draw(g);                                                       /* A438 */
}
