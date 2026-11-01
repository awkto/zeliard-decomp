/* boss_meda.c — cavern 5 boss "Vista" (MEDA.BIN = ZELRES3[13]).
 *
 *   [A002] = A716: col 0x30, row 0x0B, HP 700, EXP 3000, camera column 12,
 *   knockback free, name "Vista", almas 800.  Contact A010 = 30 everywhere.
 *   State: A716 col, A718 row, A719 HP, A72F tentacle pose, A730 attack
 *   frame, A732 the hit, A733 death counter, A734 vertical state, A735 dive
 *   delay, A736 heading (0 left), A737 attack pause.
 *
 * Image (A438): a buffer A738 of 14 COLUMNS x 12 rows of {type, frame} words,
 * column-major (24 bytes a column), 0xFF = empty.  A539 pastes a layer: one
 * bitmap byte per column (`rol`, bit 7 first = the layer's first row), one
 * list word per set bit, starting at column [A72C], row [A72D] -- di runs on
 * linearly, so a layer's rows past 11 spill into the next column's top:
 *   body        list A5DC, bitmap A606, 13 columns at (0,0)
 *   right side  list A613, bitmap A623, 11 columns at (1,8)
 *   tentacles   list [A62E + 2*pose], bitmap A682, 5 columns at (4,3)
 *   attack      list [A687 + 2*frame], bitmap [A6C7 + 2*frame], 5 at (4,7)
 * A4AA emits it column by column (`inc ax`, `add si,18h`), row = A718 + r.
 * (The port had the buffer as 14 rows x 12 columns -- transposed, like TORI,
 * TAKO and ZELA -- and A6ED as a tentacle ramp: A317 indexes it by col - 9 and
 * it is the cruising ROW, 0x0C down to 7 mid-room and back up.)
 */
#include "boss.h"
#include <string.h>

enum { S_TENT, S_ATTACK, S_VSTATE, S_DELAY, S_HDIR, S_PAUSE };
#define ST(n) (g->boss.st[n])
#define MEDA_ROWS 0xA6ED                /* A317: row by column - 9 */

static int meda_weak(uint8_t type) { return (type & 0x08) != 0; }       /* A22F */

/* A42B: refused only below 0x0A (`cmp byte [A716],0Ah / jnc dec`: CF from
 * the cmp, so col 0x0A still steps, to 9).  A41D: refused at >= 0x31. */
static int step_left(Game *g)  { if ((uint8_t)g->boss.col < 0x0A) return 1; g->boss.col--; return 0; }
static int step_right(Game *g) { if ((uint8_t)g->boss.col >= 0x31) return 1; g->boss.col++; return 0; }

/* A29F / A358: scroll + 16 wrapped once by the width */
static uint16_t hero_col16(const Game *g)
{
    unsigned c = (unsigned)g->scroll_col + 16;
    if (g->map && c >= (unsigned)g->map->width) c -= (unsigned)g->map->width;
    return (uint16_t)c;
}

/* A539 */
static void meda_layer(const Game *g, uint8_t *buf, unsigned list, unsigned bm, int cols,
                       int col0, int row0)
{
    int di = col0 * 12 + row0;                                          /* A53B..A558 */
    unsigned k = 0;
    for (int c = 0; c < cols; c++, di += 12 - 8) {
        uint8_t bits = boss_img8(g, bm + (unsigned)c);
        for (int i = 0; i < 8; i++, di++) {
            if (!(bits & (0x80 >> i))) continue;
            if (di >= 0 && di < 14 * 12) {
                buf[di * 2]     = boss_img8(g, list + 2 * k);
                buf[di * 2 + 1] = boss_img8(g, list + 2 * k + 1);
            }
            k++;
        }
    }
}

/* A438 + A4AA: compose and place */
static void meda_draw(Game *g)
{
    Boss *b = &g->boss;
    uint8_t buf[14 * 12 * 2];
    memset(buf, 0xFF, sizeof buf);                                      /* A43F */
    meda_layer(g, buf, 0xA5DC, 0xA606, 13, 0, 0);                       /* body */
    meda_layer(g, buf, 0xA613, 0xA623, 11, 1, 8);                       /* right side */
    meda_layer(g, buf, boss_img16(g, 0xA62E + 2u * ST(S_TENT)), 0xA682, 5, 4, 3);
    meda_layer(g, buf, boss_img16(g, 0xA687 + 2u * ST(S_ATTACK)),
                       boss_img16(g, 0xA6C7 + 2u * ST(S_ATTACK)), 5, 4, 7);
    boss_parts_begin(g);
    for (int c = 0; c < 14; c++)
        for (int r = 0; r < 12; r++) {
            const uint8_t *e = buf + (c * 12 + r) * 2;
            if (e[0] == 0xFF) continue;                                 /* A4CF */
            boss_part(g, (uint16_t)(b->col + c), (uint8_t)(b->row + r), e[0], e[1]);
        }
    boss_parts_end(g);
}

/* A358: the tentacle pose from where the hero (scroll + 16) is */
static void meda_tentacles(Game *g)
{
    unsigned col = g->boss.col, hc = hero_col16(g);
    uint8_t p;
    if (col + 1 < hc && col + 10 >= hc) p = 2;                          /* A36B / A375: below */
    else if (col - 6 < hc && col + 17 >= hc) p = (col + 7 < hc + 1) ? 3 : 1;   /* A37F..A3A4 */
    else p = (col + 7 < hc + 1) ? 4 : 0;                                /* A3AA */
    ST(S_TENT) = p;
}

/* A3C1  two drips; each is skipped when its column is off the ring (A3CC
 * `jc`, A3EF `jnc`) -- unlike ZELA/TORI, MEDA does test vec 27's CF (#84) */
static void meda_drips(Game *g)
{
    Boss *b = &g->boss;
    Shot drip;                                                          /* A6E0: cell 0x30, down */
    boss_shot_template(g, 0xA6E0, &drip);
    if (!boss_ring_col(g, (uint16_t)(b->col + 6), &drip.col)) {
        drip.row = (uint8_t)((b->row + 12) & 0x3F); shot_spawn(g, &drip);
    }
    if (!boss_ring_col(g, (uint16_t)(b->col + 7), &drip.col)) {
        drip.row = (uint8_t)((b->row + 10) & 0x3F); shot_spawn(g, &drip);
    }
}

/* A5A6: 40 frames (then FF30 and nothing drawn); while the new count is
 * below 0x14 the attack frame is 0, the tentacles follow the hero and sound
 * 0x23 plays; after that pose 5. */
static void meda_death(Game *g)
{
    uint8_t n = (uint8_t)(boss_death_tick(g) + 1);
    if (n > 0x28) return;                                               /* A5D6 */
    if (n < 0x14) { ST(S_ATTACK) = 0; meda_tentacles(g); meda_draw(g); g->sfx_request = 0x23; }
    else { ST(S_TENT) = 5; meda_draw(g); }                              /* A5CE */
}

/* 0xA1EA  Frame entry. */
void boss_meda_entry(Game *g)
{
    Boss *b = &g->boss;
    uint8_t hit = boss_readback(g, meda_weak);                          /* A1F8 */
    boss_hit_flash(g, hit != 0);   /* A4F5: every part this frame carries hit bit 5 */

    if (hit & 0x1F) {                                                   /* A24B */
        uint8_t src = (uint8_t)(hit & 0x1F);
        unsigned d = (unsigned)(damage_for_source(g, src) >> 3);        /* A25B */
        if (src == 1 && g->sword >= 4) { d <<= 5; g->sfx_request = 0x2D; }   /* A263 */
        else g->sfx_request = 0x2E;
        int alive = !g->boss_cutscene;
        boss_damage(g, d);                                              /* A575 */
        if (alive && g->boss_cutscene) shots_clear(g);                  /* A5A1: vec 30, once */
    }
    if (g->boss_cutscene) { meda_death(g); return; }                    /* A287 */

    int row_from_col = 0;
    if (!ST(S_VSTATE)) {                                                /* A291: cruise */
        if (b->row == 7) {                                              /* A298 */
            unsigned hc = hero_col16(g);
            if (b->col + 4u < hc && b->col + 6u >= hc) { ST(S_DELAY) = 3; ST(S_VSTATE) = 0xFF; }
        }
        if (!ST(S_HDIR)) { if (step_left(g))  ST(S_HDIR) = 0xFF; }      /* A2D3 */
        else             { if (step_right(g)) ST(S_HDIR) = 0; }         /* A2DF */
        row_from_col = 1;
    } else if (ST(S_DELAY)) { ST(S_DELAY)--; row_from_col = 1; }       /* A2EB */
    else if (ST(S_VSTATE) & 0x80) {                                     /* A2FF: down to 0x0B */
        b->row++;
        if (b->row >= 0x0B) ST(S_VSTATE) = 0x7F;
    } else {                                                            /* A30B: up until below 7 */
        b->row--;
        if (b->row < 7) ST(S_VSTATE) = 0;
    }
    if (row_from_col) b->row = boss_img8(g, MEDA_ROWS + (unsigned)(b->col - 9));   /* A317 */
    meda_tentacles(g);                                                  /* A325 -> A358 */

    if (ST(S_PAUSE)) ST(S_PAUSE)--;                                     /* A328 */
    else {
        if (++ST(S_ATTACK) == 5) { ST(S_PAUSE) = 3; ST(S_ATTACK) = 0; } /* A336 */
        if (ST(S_ATTACK) == 4) meda_drips(g);                           /* A34B */
    }
    meda_draw(g);                                                       /* A438 */
}
