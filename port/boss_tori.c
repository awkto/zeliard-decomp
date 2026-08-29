/* boss_tori.c — cavern 3 boss "Pollo" (TORI.BIN = ZELRES3[11]).  Ported from
 * src/ai/boss_tori.c.
 *
 *   [A002] = A773: col 0x2E, row 0x12, HP 500, EXP 500, camera column 8,
 *   knockback always left, name "Pollo", gold 500.  Contact A010: 56 for the
 *   head (class 0), 18 for everything else.
 *   Image: a 9 x 8 pose buffer (0xFF = empty) composed from up to four
 *   layers.  Layer n = the 9-byte column bitmap at [A6CB + 2n] (bit 7 =
 *   column 0) plus the byte list at [A64D + 2n], one byte {type<<4 | frame}
 *   per set bit.  Layers: 0/1 body (1 = flinch), 2..5 head, 6..9 wings,
 *   0xA..0xC legs, 0xD..0x10 diving, 0x11/0x12 rising and fallen.
 *   (The decompilation labels A64D "bitmaps" and A6CB "lists"; the popcount
 *   of every A6CB entry matches the length of the matching A64D run exactly
 *   the other way round, so the roles are swapped here — a correction to
 *   docs/ENEMIES.md §3 / src/ai/boss_tori.c.)
 */
#include "boss.h"
#include <string.h>

#define A64D 0xA64D                     /* u16[19] layer -> {type<<4|frame} bytes */
#define A6CB 0xA6CB                     /* u16[19] layer -> 9-byte column bitmap */

enum { S_WINGS, S_BODY, S_DIVING, S_DIVEFR, S_RISING, S_LEGS, S_HEAD,
       S_FLINCH, S_PARITY, S_FLAPPING, S_FLAPCNT, S_DEADPOSE, S_LAYING, S_DIVETIMER };
#define ST(n) (g->boss.st[n])

static int tori_weak(uint8_t type) { return (type & 0x1F) == 0; }          /* the head */

/* A5AB step_right (col + 1 while < 0x30); A58F / A59D step left with the
 * two different limits the AI uses. */
static int step_right(Game *g)   { if (g->boss.col >= 0x30) return 1; g->boss.col++; return 0; }
static int step_left_0D(Game *g) { if (g->boss.col <= 0x0D) return 1; g->boss.col--; return 0; }
static int step_left_11(Game *g) { if (g->boss.col <= 0x11) return 1; g->boss.col--; return 0; }

/* 0xA57B  the shared legs/flap timer: 0,1,2 and true when it wraps */
static int legs_tick(Game *g)
{
    ST(S_LEGS) = (uint8_t)(ST(S_LEGS) + 1);
    if (ST(S_LEGS) >= 3) { ST(S_LEGS) = 0; return 1; }
    return 0;
}

/* 0xA552  paint one layer into the 9x8 pose buffer */
static void layer(const Game *g, uint8_t *buf, int n, int row_shift)
{
    unsigned ls = boss_img16(g, A64D + 2u * (unsigned)n);
    unsigned bm = boss_img16(g, A6CB + 2u * (unsigned)n);
    if (!ls || !bm) return;
    unsigned k = 0;
    for (int r = 0; r < 9; r++) {
        uint8_t bits = boss_img8(g, bm + (unsigned)r);
        for (int c = 0; c < 8; c++) {
            if (!(bits & (0x80 >> c))) continue;
            uint8_t v = boss_img8(g, ls + k++);
            int rr = r + row_shift;
            if (rr >= 0 && rr < 9) buf[rr * 8 + c] = v;
        }
    }
}

/* 0xA455/0xA4C1  compose the layers and place the parts */
static void tori_draw(Game *g)
{
    Boss *b = &g->boss;
    uint8_t buf[72];
    memset(buf, 0xFF, sizeof buf);
    if (ST(S_DIVING)) {                                                    /* A467 */
        layer(g, buf, 0x0D + (ST(S_DIVEFR) & 3), 0);
    } else if (ST(S_RISING) || ST(S_DEADPOSE)) {
        layer(g, buf, 0x11 + (ST(S_DEADPOSE) & 1), 0);
    } else {
        layer(g, buf, ST(S_BODY) ? 1 : 0, 0);
        layer(g, buf, 6 + (ST(S_WINGS) & 3), 0);
        layer(g, buf, 0x0A + (ST(S_LEGS) % 3), 0);
        layer(g, buf, 2 + (ST(S_HEAD) & 3), 0);
    }
    boss_parts_begin(g);
    for (int r = 0; r < 9; r++)
        for (int c = 0; c < 8; c++) {
            uint8_t v = buf[r * 8 + c];
            if (v == 0xFF) continue;
            boss_part(g, (uint16_t)(b->col + c), (uint8_t)(b->row + r),
                      (uint8_t)(v >> 4), (uint8_t)(v & 0xF));
        }
    boss_parts_end(g);
}

/* 0xA60A  Death: 40 frames; the first 20 animate with sound 0x2C, then the
 * fallen pose. */
static void tori_death(Game *g)
{
    uint8_t t = g->boss.death_cnt;
    if (boss_death_tick(g) >= 0x28) { boss_parts_begin(g); boss_parts_end(g); return; }
    ST(S_BODY) = 1;
    if (t < 0x14) {
        if (!(t & 3)) g->sfx_request = 0x2C;
        ST(S_HEAD) = (uint8_t)((ST(S_HEAD) + 1) & 3);
        legs_tick(g);
        ST(S_DEADPOSE) = 0;
    } else ST(S_DEADPOSE) = (uint8_t)(1 + (t & 1));
    tori_draw(g);
}

/* 0xA1D4  Frame entry. */
void boss_tori_entry(Game *g)
{
    Boss *b = &g->boss;
    uint8_t hit = boss_readback(g, tori_weak);                             /* A1E2 */

    if (hit) {                                                             /* A235 */
        unsigned d = (unsigned)damage_for_source(g, (uint8_t)(hit & 0x1F)) * 2u;
        if (hit & 0x80) d *= 4;                                            /* the head: x8 */
        g->sfx_request = 0x29;
        boss_damage(g, d);                                                 /* A5BA */
        if (ST(S_DIVING)) { ST(S_DIVING) = 0; ST(S_DIVEFR) = 0; ST(S_RISING) = 0xFF; }
        else step_right(g);                                                /* A5AB */
        ST(S_FLINCH) = 4;                                                  /* A276 */
    }
    ST(S_BODY) = 0;                                                        /* A27B */
    if (ST(S_FLINCH)) { ST(S_FLINCH)--; ST(S_BODY) = 1; }

    if (ST(S_DIVING)) {                                                    /* A290 */
        if (b->row != 0x0E) b->row--;
        ST(S_DIVEFR) = (uint8_t)((ST(S_DIVEFR) + 1) & 3);
        if (ST(S_DIVEFR) == 2) g->sfx_request = 0x2B;
        int done = step_left_11(g);
        if (ST(S_DIVETIMER) == 0) done = 1; else ST(S_DIVETIMER)--;
        if (done || hit) { ST(S_DIVING) = 0; ST(S_DIVEFR) = 0; ST(S_RISING) = 0xFF; g->sfx_request = 0x2A; }
    } else if (ST(S_RISING)) {                                             /* A2E5 */
        if (ST(S_DIVEFR) == 1) ST(S_RISING) = 0;
        else { ST(S_DIVEFR) = 1; if (b->row != 0x12) { b->row++; ST(S_DIVEFR) = 0; step_left_0D(g); } }
    } else if (ST(S_FLAPPING)) {                                           /* A316 */
        ST(S_HEAD) = (uint8_t)((ST(S_HEAD) + 1) & 3);
        if (legs_tick(g)) {
            if (ST(S_FLAPCNT) < 4) { ST(S_FLAPCNT)++; g->sfx_request = 0x2A; ST(S_FLINCH) = 4; }
            else { ST(S_FLAPPING) = 0; ST(S_DIVEFR) = 0; ST(S_DIVING) = 0xFF; ST(S_DIVETIMER) = 15; }
        }
    } else if (ST(S_LAYING)) {                                             /* A35D */
        if (legs_tick(g)) {
            if (ST(S_FLAPCNT) < 2) { ST(S_FLAPCNT)++; g->sfx_request = 0x2A; ST(S_FLINCH) = 2; }
            else {
                Shot egg;                                                  /* A766 */
                boss_shot_template(g, 0xA766, &egg);
                if (!ai_map_col_to_ring(g, (uint16_t)(b->col + 4), &egg.col)) {
                    egg.row = (uint8_t)((b->row + 4) & 0x3F);
                    shot_spawn(g, &egg);
                }
                ST(S_LAYING) = 0;
            }
        }
    } else if (g->boss_cutscene) { tori_death(g); return; }                /* A60A */
    else {                                                                 /* A3B7: hover over the hero */
        ST(S_HEAD) = (uint8_t)((ST(S_HEAD) + 1) & 3);
        if (hit && b->col >= 0x14) { ST(S_FLAPPING) = 0xFF; ST(S_FLAPCNT) = 0; }       /* A3C0 */
        if (!ST(S_FLAPPING) && !(krn_random(g) & 0xF)) { ST(S_LAYING) = 0xFF; ST(S_FLAPCNT) = 0; }  /* A3DF */
        if (!(++ST(S_PARITY) & 1)) {                                       /* A3FD: every 2nd frame */
            unsigned width = g->map ? (unsigned)g->map->width : 1;
            uint8_t d = (uint8_t)(b->col - (unsigned)g->scroll_col % width);
            if (d < 0x0C) { ST(S_WINGS) = (uint8_t)((ST(S_WINGS) - 1) & 3);
                            if (step_right(g)) { ST(S_FLAPPING) = 0xFF; ST(S_FLAPCNT) = 0; } }
            else if (d > 0x0C) { ST(S_WINGS) = (uint8_t)((ST(S_WINGS) + 1) & 3); step_left_0D(g); }
            if (d >= 0x0C && !(krn_random(g) & 0x1F)) { ST(S_FLAPPING) = 0xFF; ST(S_FLAPCNT) = 0; }
        }
    }
    tori_draw(g);                                                          /* A455 */
}
