/* boss_drgn.c — cavern 7 boss "Dragon" (DRGN.BIN = ZELRES3[16], 2985 bytes).
 * Ported from src/ai/boss_drgn.c (the Ghidra output for this overlay is
 * unusable, so the decompilation was read out of the ndisasm listing).
 *
 *   [A002] = AA3C: col 0x1E, row 0x08, HP 800, EXP 12000, camera column 5,
 *   knockback free, name "Dragon", gold 2500.  Contact A010: 40 for class 0
 *   (the head) and 8/9 (the flame), 30 for the body classes 1..7.
 *
 * Image (A758): the dragon is composed at CELL granularity into a 29-column x
 * 10-row buffer (column-major, 0xFF = empty).  A layer is pasted at (column,
 * row) as `cx` bitmap bytes, one per column, bit 7 = the layer's first row,
 * and one `class << 4 | frame` byte per set bit.  Five layers (A542):
 *
 *   | layer      | at      | cols | list             | bitmap           |
 *   | pose       | (0, 1)  | 12   | [A783 + 2*pose]  | [A810 + 2*pose]  |
 *   | body       | (12, 0) | 11   | [A8DE + 2*(f&1)] | [A8FD + 2*(f&1)] |
 *   | front legs | (9, 6)  |  7   | [A881 + 2*(w&3)] | [A89A + 2*(w&3)] |
 *   | hind legs  | (17, 6) |  7   | [A8B7 + 2*(w&3)] | A8D7 (fixed)     |
 *   | tail       | (25, 8) |  4   | A87A (3 bytes)   | A87D             |
 *
 * so the dragon is up to 29 cells (232 px) wide — wider than the 28-column
 * window; columns outside the ring are simply skipped (A606).  Every buffer
 * cell becomes one part {col + c, row + r, type = class | 0x80 (SOLID while
 * alive), phase = the whole byte}.  The flame (A68B) is a second 13x8 image at
 * (col - 10, row + 4) — (col - 6) on pose 5 — with lists [A917 + 2*(f&3)] /
 * bitmaps [A930 + ..] for poses < 6 (class 8) and [A96C]/[A985] otherwise
 * (class 9); its parts are `class | 0x20` (sword-immune), not solid.
 *
 * The list/bitmap tables are read out of the image at run time, so the shapes
 * are the original's; the byte counts match the bitmap popcounts exactly
 * (11,11,11,11,10,11,10,11,11,11,11 for the poses, 15/12 body, ...).
 */
#include "boss.h"
#include <string.h>

enum { S_BREATH, S_FLAMEFR, S_POSE, S_FRAME, S_WALK, S_HALF, S_RETREAT, S_RSTEPS,
       S_WIND, S_WBASE, S_WCNT, S_FLAMECNT, S_HEADHIT, S_RTBL, S_RIDX, S_REACT };
#define ST(n) (g->boss.st[n])

#define DR_POSE_LIST  0xA783            /* u16[11] */
#define DR_POSE_BM    0xA810
#define DR_BODY_LIST  0xA8DE            /* u16[2] */
#define DR_BODY_BM    0xA8FD
#define DR_FLEG_LIST  0xA881            /* u16[4] */
#define DR_FLEG_BM    0xA89A
#define DR_HLEG_LIST  0xA8B7            /* u16[4] */
#define DR_HLEG_BM    0xA8D7            /* one fixed 7-byte bitmap */
#define DR_TAIL_LIST  0xA87A            /* 3 bytes */
#define DR_TAIL_BM    0xA87D            /* 4 bytes */
#define DR_FLAME_LIST 0xA917            /* u16[4], class 8 (poses < 6) */
#define DR_FLAME_BM   0xA930
#define DR_FLAME2_LIST 0xA96C           /* u16[4], class 9 */
#define DR_FLAME2_BM  0xA985
#define DR_REACT_LOW  0xA4B4            /* 7 bytes, the last has bit7 = end */
#define DR_REACT_HIGH 0xA4BB

#define DR_W 29
#define DR_H 10

/* A542: the five body layers, then the flame image if it is burning. */
static void drgn_draw(Game *g)
{
    Boss *b = &g->boss;
    uint8_t buf[DR_W * DR_H];
    unsigned pose = ST(S_POSE) > 10 ? 10 : ST(S_POSE);
    unsigned w = ST(S_WALK) & 3, f = ST(S_FRAME) & 1;
    memset(buf, 0xFF, sizeof buf);
    boss_paste(g, buf, DR_W, DR_H,  0, 1, 12, 1, boss_img16(g, DR_POSE_LIST + 2 * pose),
                                          boss_img16(g, DR_POSE_BM   + 2 * pose));
    boss_paste(g, buf, DR_W, DR_H, 12, 0, 11, 1, boss_img16(g, DR_BODY_LIST + 2 * f),
                                          boss_img16(g, DR_BODY_BM   + 2 * f));
    boss_paste(g, buf, DR_W, DR_H,  9, 6,  7, 1, boss_img16(g, DR_FLEG_LIST + 2 * w),
                                          boss_img16(g, DR_FLEG_BM   + 2 * w));
    boss_paste(g, buf, DR_W, DR_H, 17, 6,  7, 1, boss_img16(g, DR_HLEG_LIST + 2 * w), DR_HLEG_BM);
    boss_paste(g, buf, DR_W, DR_H, 25, 8,  4, 1, DR_TAIL_LIST, DR_TAIL_BM);

    boss_parts_begin(g);
    int solid = g->boss_cutscene ? 0 : 0x80;                        /* A62F */
    for (int c = 0; c < DR_W; c++)
        for (int r = 0; r < DR_H; r++) {
            uint8_t v = buf[c * DR_H + r];
            if (v == 0xFF) continue;
            boss_part(g, (uint16_t)(b->col + c), (uint8_t)(b->row + r),
                      (uint8_t)(((v >> 4) & 0x0F) | solid), v);
        }

    if (ST(S_BREATH)) {                                             /* A68B */
        uint8_t fb[13 * 8];
        unsigned k = ST(S_FLAMEFR) & 3;
        int high = pose >= 6;
        memset(fb, 0xFF, sizeof fb);
        boss_paste(g, fb, 13, 8, 0, 0, 13, 1,
              boss_img16(g, (high ? DR_FLAME2_LIST : DR_FLAME_LIST) + 2 * k),
              boss_img16(g, (high ? DR_FLAME2_BM   : DR_FLAME_BM)   + 2 * k));
        int fx = (int)b->col - 10 + (pose == 5 ? 4 : 0);
        for (int c = 0; c < 13; c++)
            for (int r = 0; r < 8; r++) {
                uint8_t v = fb[c * 8 + r];
                if (v == 0xFF) continue;
                boss_part(g, (uint16_t)(fx + c), (uint8_t)(b->row + 4 + r),
                          (uint8_t)(((v >> 4) & 0x0F) | 0x20), v);
            }
    }
    boss_parts_end(g);
}

/* A521 / A532: forward is LEFT, down to column 0x10; the retreat is RIGHT up
 * to the start column 0x1E.  true = blocked. */
static int step_left(Game *g)  { if ((int)g->boss.col - 1 <= 0x0E) return 1; g->boss.col--; return 0; }
static int step_right(Game *g) { if ((int)g->boss.col + 1 > 0x1E)  return 1; g->boss.col++; return 0; }

/* A9B4: HP -= d, and at zero cancel everything the state machine was doing. */
static void drgn_damage(Game *g, unsigned d)
{
    boss_damage(g, d);
    if (g->boss.hp == 0) {
        ST(S_HEADHIT) = 0; ST(S_RIDX) = 0; ST(S_BREATH) = 0; ST(S_WIND) = 0;
    }
}

/* A4C2: 6 frames alternating base / base+1, then the flame. */
static void windup_step(Game *g)
{
    ST(S_WCNT)++;
    ST(S_POSE) = (uint8_t)(ST(S_WBASE) + (ST(S_WCNT) & 1));
    if (ST(S_WCNT) >= 6) {
        ST(S_POSE) = (uint8_t)(ST(S_WBASE) + 1);
        ST(S_FLAMEFR) = 0; ST(S_FLAMECNT) = 0; ST(S_WIND) = 0; ST(S_BREATH) = 0xFF;
        g->sfx_request = 0x36;
    }
}

/* A4FC: frames 0,1,2,3,2,3,... for 10 frames, sound 0x36 every frame. */
static void flame_step(Game *g)
{
    g->sfx_request = 0x36;
    if (++ST(S_FLAMEFR) >= 4) ST(S_FLAMEFR) = 2;
    if (++ST(S_FLAMECNT) >= 10) ST(S_BREATH) = 0;
}

/* A9F2: 40 frames; the first 30 thrash between poses 2 and 3 with sound 0x37
 * every 4th frame, then pose 0x0A (collapsed). */
static void drgn_death(Game *g)
{
    uint8_t t = g->boss.death_cnt;
    if (boss_death_tick(g) >= 0x28) { boss_parts_begin(g); boss_parts_end(g); return; }
    if (t < 0x1E) {
        ST(S_FRAME)++;
        ST(S_POSE) = (uint8_t)(2 + (ST(S_FRAME) & 1));
        if (!(ST(S_FRAME) & 3)) g->sfx_request = 0x37;
    } else { ST(S_FRAME) = 1; ST(S_POSE) = 0x0A; }
    drgn_draw(g);
}

/* the head is class 0 (A2E1's weak-point test) */
static int drgn_weak(uint8_t type) { return (type & 0x1F) == 0; }

/* 0xA2DD  Frame entry. */
void boss_drgn_entry(Game *g)
{
    uint8_t hit = boss_readback(g, drgn_weak);                      /* A2E1 */
    boss_hit_flash(g, hit != 0);   /* A644: every part this frame carries hit bit 5 */

    if (hit) {                                                      /* A33E */
        unsigned d = (unsigned)damage_for_source(g, (uint8_t)(hit & 0x1F)) / 2;
        if ((hit & 0x1F) >= 2) d /= 4;                              /* magic and orbs */
        if (hit & 0x80) { ST(S_HEADHIT) = 0xFF; g->sfx_request = 0x34; d *= 2; }
        else            { ST(S_RETREAT) = 0xFF; g->sfx_request = 0x35; }
        drgn_damage(g, d);
        if (ST(S_HEADHIT)) {                                        /* A384: rear up, back off 8 */
            ST(S_RTBL) = (uint8_t)(ST(S_POSE) < 6);
            ST(S_RIDX) = 0; ST(S_BREATH) = 0; ST(S_WIND) = 0;
            ST(S_RETREAT) = 0xFF; ST(S_REACT) = 0xFF; ST(S_RSTEPS) = 8;
        }
        ST(S_HEADHIT) = 0;
    }
    if (g->boss_cutscene) { drgn_death(g); return; }                /* A3BA */
    ST(S_FRAME)++;                                                  /* A3C4 */
    if (ST(S_BREATH)) { flame_step(g);  drgn_draw(g); return; }     /* A4FC */
    if (ST(S_WIND))   { windup_step(g); drgn_draw(g); return; }     /* A4C2 */

    /* A3DC: one cell every second frame (the half-frame accumulator wraps) */
    ST(S_HALF) = (uint8_t)(ST(S_HALF) + 0x80);
    if (ST(S_HALF) == 0) {
        if (!ST(S_RETREAT)) { if (!step_left(g)) ST(S_WALK)++; }
        else if (--ST(S_RSTEPS) == 0) ST(S_RETREAT) = 0;
        else { ST(S_RETREAT) = (uint8_t)(step_right(g) ? 0 : 0xFF); ST(S_WALK)--; }
    }

    if (ST(S_REACT)) {                                              /* A48E */
        unsigned t = ST(S_RTBL) ? DR_REACT_LOW : DR_REACT_HIGH;
        uint8_t v = boss_img8(g, t + (ST(S_RIDX) > 6 ? 6 : ST(S_RIDX)));
        ST(S_POSE) = (uint8_t)(v & 0x7F);
        if (v & 0x80) ST(S_REACT) = 0;                              /* the table's own end bit */
        ST(S_RIDX)++;
    } else if (!(krn_random(g) & 0xC0) &&                           /* A417: 1/4 from a rest pose */
               (ST(S_POSE) == 0 || ST(S_POSE) == 4 || ST(S_POSE) == 7)) {
        ST(S_WBASE) = ST(S_POSE); ST(S_WCNT) = 0; ST(S_WIND) = 0xFF;
    } else {                                                        /* A448: pose by distance */
        uint8_t left = (uint8_t)g->scroll_col;
        uint8_t bc = (uint8_t)g->boss.col;
        if ((uint8_t)(left + 16) < bc)      ST(S_POSE) = (uint8_t)(ST(S_POSE) < 6 ? 6 : 7);
        else if ((uint8_t)(left + 11) < bc) ST(S_POSE) = (uint8_t)(ST(S_POSE) >= 7 ? 6 : 0);
        else                                ST(S_POSE) = (uint8_t)(ST(S_POSE) >= 7 ? 6 : 4);
    }
    drgn_draw(g);                                                   /* A542 */
}
