/* boss_tori.c — cavern 3 boss "Pollo" (TORI.BIN = ZELRES3[11]).  Ported from
 * src/ai/boss_tori.c.
 *
 *   [A002] = A773: col 0x2E, row 0x12, HP 500, EXP 500, camera column 8,
 *   knockback always left, name "Pollo", gold 500.  Contact A010: 56 for the
 *   head (class 0), 18 for everything else.
 *   Image: a pose buffer of 9 columns x 8 rows, column-major (0xFF =
 *   empty), composed from up to four layers.  Layer n = the 9-byte bitmap
 *   at [A6CB + 2n], one byte per column (bit 7 = row 0), plus the byte list
 *   at [A64D + 2n], one byte {type<<4 | frame} per set bit (#75).  Layers: 0/1 body (1 = flinch), 2..5 head, 6..9 wings,
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

/* A5AB step_right: refused (CF) at col >= 0x30.  A58F / A59D step left,
 * refused below 0x0D / 0x11 (`cmp [A773],n; jnc dec`: col == n still
 * steps -- #75, the port refused one column early). */
static int step_right(Game *g)   { if (g->boss.col >= 0x30) return 1; g->boss.col++; return 0; }
static int step_left_0D(Game *g) { if (g->boss.col < 0x0D) return 1; g->boss.col--; return 0; }
static int step_left_11(Game *g) { if (g->boss.col < 0x11) return 1; g->boss.col--; return 0; }

/* 0xA57B  the shared legs/flap timer [A78F]: 0,1,2; true (CF clear) when it
 * wraps */
static int legs_tick(Game *g)
{
    ST(S_LEGS) = (uint8_t)(ST(S_LEGS) + 1);
    if (ST(S_LEGS) >= 3) { ST(S_LEGS) = 0; return 1; }
    return 0;
}

/* 0xA552  paint one layer into the pose buffer [A79C]: 9 columns of 8 rows,
 * column-major; the column's bitmap byte is rotated left, bit 7 first =
 * row 0, one list byte per set bit */
static void layer(const Game *g, uint8_t *buf, int n)
{
    unsigned ls = boss_img16(g, A64D + 2u * (unsigned)n);
    unsigned bm = boss_img16(g, A6CB + 2u * (unsigned)n);
    if (!ls || !bm) return;
    unsigned k = 0;
    for (int c = 0; c < 9; c++) {
        uint8_t bits = boss_img8(g, bm + (unsigned)c);
        for (int r = 0; r < 8; r++)
            if (bits & (0x80 >> r)) buf[c * 8 + r] = boss_img8(g, ls + k++);
    }
}

/* 0xA455  compose the layers, then A4BC place the parts */
static void tori_draw(Game *g)
{
    Boss *b = &g->boss;
    uint8_t buf[72];
    uint8_t row = b->row;                                                  /* A455: [A793] = [A775] */
    memset(buf, 0xFF, sizeof buf);                                         /* A45D */
    if (ST(S_DEADPOSE) || ST(S_RISING)) {                                  /* A467 [A799] / A46E [A78E] */
        layer(g, buf, 0x11 + (ST(S_DIVEFR) & 1));                          /* A475 */
    } else if (ST(S_DIVING)) {                                             /* A481 */
        layer(g, buf, 0x0D + (ST(S_DIVEFR) & 3));
        row = (uint8_t)(row + (ST(S_DIVEFR) & 1));                         /* A495 shr / adc */
    } else {                                                               /* A49E */
        layer(g, buf, ST(S_BODY));
        layer(g, buf, 6 + ST(S_WINGS));
        layer(g, buf, 0x0A + ST(S_LEGS));
        layer(g, buf, 2 + ST(S_HEAD));
    }
    /* A4C1: the OUTER loop is the column (`inc ax` at A545 on [A773]), the
     * inner the row ([A793] + cl, A4E8): each record is {col, row, ring col,
     * type = byte >> 4 (A506), hit = 0 | 20 while [A791] (A510), phase = the
     * whole byte (A509)}.  A column outside the ring is skipped whole (A4D7).
     * (#75: the port read the buffer rows-first and kept only the low nibble
     * as the phase -- pollo_fight f86.) */
    boss_parts_begin(g);
    for (int c = 0; c < 9; c++)
        for (int r = 0; r < 8; r++) {
            uint8_t v = buf[c * 8 + r];
            if (v == 0xFF) continue;
            boss_part(g, (uint16_t)(b->col + c), (uint8_t)(row + r), (uint8_t)((v >> 4) & 0xF), v);
        }
    boss_parts_end(g);
}

/* 0xA60A  Death: [A794] counts 40 frames.  The first 20 (A626) tick the legs,
 * turn the head and request sound 0x2C every frame; then [A799] = FF and
 * [A78D] = 1, the fallen pose (layer 0x12).  At 0x28 A647 sets FF30 and
 * returns without drawing. */
static void tori_death(Game *g, int prev_n)
{
    uint8_t t = g->boss.death_cnt;
    if (boss_death_tick(g) >= 0x28) {
        /* A647 returns without drawing, and TORI's readback ends the list
         * with A231 `mov word [si],0FFF` -- not FFFF -- so last frame's
         * records stay listed behind a 0x0FFF first column (#75,
         * pollo_fight f196: obj.n 10 in the original, 0 here) */
        g->nobj = prev_n;
        if (prev_n) g->obj[0].col = 0x0FFF;
        return;
    }
    ST(S_BODY) = 1;                                                        /* A616 */
    if (t < 0x14) {                                                        /* A622 */
        legs_tick(g);                                                      /* A626 */
        ST(S_HEAD) = (uint8_t)((ST(S_HEAD) + 1) & 3);
        g->sfx_request = 0x2C;                                             /* A632 */
    } else { ST(S_DEADPOSE) = 0xFF; ST(S_DIVEFR) = 1; }                    /* A63A */
    tori_draw(g);
}

/* 0xA1D4  Frame entry. */
void boss_tori_entry(Game *g)
{
    Boss *b = &g->boss;
    int prev_n = g->nobj;
    uint8_t hit = boss_readback(g, tori_weak);                             /* A1E2 */
    boss_hit_flash(g, hit != 0);   /* A510: every part this frame carries hit bit 5 */

    if (hit) {                                                             /* A235 */
        unsigned d = (unsigned)damage_for_source(g, (uint8_t)(hit & 0x1F)) * 2u;
        if (hit & 0x80) d *= 4;                                            /* the head: x8 */
        g->sfx_request = 0x29;
        int alive = b->hp != 0;
        boss_damage(g, d);                                                 /* A5BA */
        if (alive && b->hp == 0) {                                         /* A5D4: the kill */
            ST(S_FLAPPING) = 0; ST(S_LAYING) = 0; ST(S_FLAPCNT) = 0;       /* A5DE..A5E8 */
            if (ST(S_DIVING)) { b->death_cnt = 0; ST(S_DIVING) = 0; ST(S_DIVEFR) = 0; ST(S_RISING) = 0xFF; }
        }
        if (ST(S_DIVING)) { ST(S_DIVING) = 0; ST(S_DIVEFR) = 0; ST(S_RISING) = 0xFF; }   /* A25B */
        else step_right(g);                                                /* A5AB */
        ST(S_FLINCH) = 4;                                                  /* A276 */
    }
    ST(S_BODY) = 0;                                                        /* A27B */
    if (ST(S_FLINCH)) { ST(S_FLINCH)--; ST(S_BODY) = 1; }

    if (ST(S_DIVING)) {                                                    /* A290 */
        if (b->row != 0x0E) b->row--;
        ST(S_DIVEFR) = (uint8_t)((ST(S_DIVEFR) + 1) & 3);
        if (ST(S_DIVEFR) == 2) g->sfx_request = 0x2B;
        /* A2B7: a refused step ends the dive; so does the timer [A79B]
         * running out, or a hit -- the timer only counts while it moves */
        int done = step_left_11(g);
        if (!done) {
            if (ST(S_DIVETIMER) == 0) done = 1;
            else { ST(S_DIVETIMER)--; if (hit) done = 1; }
        }
        if (done) { ST(S_DIVING) = 0; ST(S_DIVEFR) = 0; ST(S_RISING) = 0xFF; g->sfx_request = 0x2A; }
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
    } else if (g->boss_cutscene) { tori_death(g, prev_n); return; }                /* A60A */
    else {                                                                 /* A3B7: hover over the hero */
        ST(S_HEAD) = (uint8_t)((ST(S_HEAD) + 1) & 3);
        if (hit && b->col >= 0x14) { ST(S_FLAPPING) = 0xFF; ST(S_FLAPCNT) = 0; }       /* A3C0 */
        if (!ST(S_FLAPPING) && !(krn_random(g) & 0xF)) { ST(S_LAYING) = 0xFF; ST(S_FLAPCNT) = 0; }  /* A3DF */
        if (!(++ST(S_PARITY) & 1)) {                                       /* A3FD: every 2nd frame */
            /* A3FD..A40F: the HERO's map column, (byte)[80] + [83], less the
             * width once if past it -- not the scroll column (#75) */
            unsigned hc = (unsigned)(((unsigned)g->scroll_col + g->hero_scr_col) & 0xFF);
            if (g->map && hc >= (unsigned)g->map->width) hc -= (unsigned)g->map->width;
            uint8_t d = (uint8_t)(b->col - hc);
            if (d < 0x0C) { ST(S_WINGS) = (uint8_t)((ST(S_WINGS) - 1) & 3);
                            if (step_right(g)) { ST(S_FLAPPING) = 0xFF; ST(S_FLAPCNT) = 0; } }
            else {
                if (d > 0x0C) { ST(S_WINGS) = (uint8_t)((ST(S_WINGS) + 1) & 3); step_left_0D(g); }
                if (!(krn_random(g) & 0x1F)) { ST(S_FLAPPING) = 0xFF; ST(S_FLAPCNT) = 0; }   /* A442 */
            }
        }
    }
    tori_draw(g);                                                          /* A455 */
}
