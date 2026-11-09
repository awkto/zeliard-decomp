/* boss_lega.c — cavern 6 boss "Tarso" (LEGA.BIN = ZELRES3[14], 2073 bytes).
 *
 *   [A002] = A7A0: col 0x26, row 0x07, HP 640, EXP 6000, camera column 8,
 *   knockback always left, name "Tarso", almas 1500.  Contact A010: 160 for
 *   classes 0..5, 80 for class 6 (its projectile), 10 for 7..15.
 *   State: A7A0 col (word), A7A2 row, A7A3 HP, A7B6 part count, A7B7 the
 *   hit, A7B8 death counter, A7B9 pose, A7BA face, A7BB face timer, A7BC
 *   the ring column, A7BD retreating, A7BE (written, never read), A7BF
 *   retreat timer, A7C0 attacking, A7C1 its step, A7C2 shot live, A7C3 shot
 *   col (word), A7C5 shot row, A7C6 shot frame, A7C7 path index, A7C8
 *   exploding.
 *
 * Image (A44C): a buffer A7C9 of 8 COLUMNS x 10 rows of part bytes,
 * column-major (10 bytes a column), 0xFF = empty.  Pose p (0..8) pastes
 * bitmap [A744 + 2p] (one byte per column, `rol`, bit 7 first) and byte list
 * [A6C8 + 2p] into rows 2..9 (di starts at A7CB); the face goes in at A7F1 =
 * column 4 row 0 (row 1 on pose 6 and poses >= 8) with frame 2*face, and 0x14
 * bytes on = column 6, same row, with 2*face+1.  A4AA emits column by column
 * (`inc ax`, `add di,0Ah`); a column off the ring (vec 27 CF) is skipped
 * whole.  Each byte: bit 7 -> type |= 0x60, bits 4-6 the class, the byte
 * itself the frame.  (The port had an 8x8 row-major buffer, the face at
 * 0x28/0x29/0x3C -- transposed, like TORI, TAKO, ZELA and MEDA.)
 */
#include "boss.h"
#include "audio.h"
#include <string.h>

enum { S_FACE, S_FACET, S_RETREAT, S_RTIMER, S_ATTACK, S_ASTEP, S_UNUSED_BE,
       S_SHOT, S_SHOTROW, S_SHOTFR, S_SHOTIDX, S_EXPLODE };
#define ST(n) (g->boss.st[n])
#define SHOTCOL (g->boss.sw[0])                 /* A7C3 */

#define LEGA_BM   0xA744                /* u16[] pose -> 8 bitmap bytes, one a column */
#define LEGA_LIST 0xA6C8                /* u16[] pose -> byte list */
#define LEGA_PATH 0xA5D8                /* 17 x {s8 dcol, s8 drow} */
#define LEGA_FACE 0xA41B                /* the 4-entry face cycle {0,1,2,1} */
#define LEGA_WALKL 0xA41F               /* {2,5,6,7,0}: poses that step left */
#define LEGA_WALKR 0xA424               /* {1,3,6,7,7}: poses that step right */
#define LEGA_DEATH 0xA69B               /* death poses by the new count */

/* A2F4 / A339: `repne scasb` over 5 bytes */
static int in5(const Game *g, unsigned tbl, uint8_t v)
{
    for (unsigned i = 0; i < 5; i++) if (boss_img8(g, tbl + i) == v) return 1;
    return 0;
}

/* A429: col - 1 is stored whatever happens; CF = the new col <= 0x0E */
static int step_left(Game *g) { g->boss.col--; return g->boss.col <= 0x0E; }
/* A43B: col + 1 only up to 0x32; CF = refused */
static int step_right(Game *g)
{
    if (g->boss.col + 1u > 0x32) return 1;
    g->boss.col++;
    return 0;
}

/* A644: HP -= d floored at 0, the bar (video 200C); at 0 -- on every hit that
 * finds it at 0, so a blow during the death restarts it -- the death counter
 * A7B8 = 0, the shot dropped, FF2E = FF. */
static void lega_damage(Game *g, unsigned d)
{
    Boss *b = &g->boss;
    b->hp = (uint16_t)(b->hp > d ? b->hp - d : 0);
    if (b->hp) return;
    b->death_cnt = 0;
    ST(S_SHOT) = 0;
    g->boss_cutscene = 0xFF;
}

/* A5FA: the projectile is a part of its own -- type 0x26, [si+5] = 0 (no
 * hit bit even on a hit frame), frame A7C6; off the ring it is not placed. */
static void lega_shot_part(Game *g)
{
    if (!ST(S_SHOT)) return;
    uint8_t ph = g->boss.part_hit;
    g->boss.part_hit = 0;
    boss_part(g, SHOTCOL, ST(S_SHOTROW), 0x26, ST(S_SHOTFR));
    g->boss.part_hit = ph;
}

/* A54B: after the parts are placed, the shot moves */
static void lega_shot_step(Game *g)
{
    if (!ST(S_SHOT)) return;
    if (ST(S_EXPLODE)) {                                                /* A5C6: frames 3,4,5 */
        if (++ST(S_SHOTFR) >= 6) ST(S_SHOT) = 0;
        return;
    }
    if ((uint8_t)SHOTCOL < 0x12) {                                      /* A55A: byte compare */
        ST(S_EXPLODE) = 0xFF; ST(S_SHOTFR) = 3; SFX_SET(g->sfx_request, 0x32);
        return;
    }
    unsigned i = ST(S_SHOTIDX);                                         /* A571 */
    uint8_t lo = (uint8_t)(SHOTCOL + boss_img8(g, LEGA_PATH + 2 * i));  /* `add [A7C3],al`: low byte only */
    SHOTCOL = (uint16_t)((SHOTCOL & 0xFF00) | lo);
    ST(S_SHOTROW) = (uint8_t)(ST(S_SHOTROW) + boss_img8(g, LEGA_PATH + 2 * i + 1));
    if (ST(S_SHOTIDX) < 0x10) ST(S_SHOTIDX)++;                          /* A589: cmp 10h / adc 0 */
    ST(S_SHOTFR) = (uint8_t)(ST(S_SHOTFR) + 1 >= 3 ? 0 : ST(S_SHOTFR) + 1);
    if (ST(S_SHOTIDX) == 9 || ST(S_SHOTIDX) == 0x0C || ST(S_SHOTIDX) == 0x0F) SFX_SET(g->sfx_request, 0x31);
}

/* A44C: compose, place the parts, the shot part, then move the shot */
static void lega_draw(Game *g)
{
    Boss *b = &g->boss;
    uint8_t buf[8 * 10];
    memset(buf, 0xFF, sizeof buf);                                      /* A44E: 0x28 words */
    unsigned list = boss_img16(g, LEGA_LIST + 2u * b->pose);            /* A461 */
    unsigned bm = boss_img16(g, LEGA_BM + 2u * b->pose);                /* A465 */
    unsigned di = 2;                                                    /* A469: A7CB */
    for (int c = 0; c < 8; c++, di += 2) {
        uint8_t bits = boss_img8(g, bm + (unsigned)c);
        for (int i = 0; i < 8; i++, di++)
            if (bits & (0x80 >> i)) buf[di] = boss_img8(g, list++);     /* A473 rol / movsb */
    }
    unsigned f = 0x28 + ((b->pose == 6 || b->pose >= 8) ? 1u : 0u);     /* A484: A7F1 */
    buf[f] = (uint8_t)(2 * ST(S_FACE));
    buf[f + 0x14] = (uint8_t)(2 * ST(S_FACE) + 1);                      /* A49C: +1 +13h */

    boss_parts_begin(g);                                                /* A4A2 */
    for (int c = 0; c < 8; c++) {
        /* A4C1: a column off the ring (vec 27 CF) is skipped -- boss_part
         * makes the same test for each of its parts */
        for (int k = 0; k < 10; k++) {
            uint8_t v = buf[c * 10 + k];
            if (v == 0xFF) continue;                                    /* A4C7 */
            uint8_t type = (uint8_t)(((v & 0x80) ? 0x60 : 0) | ((v >> 4) & 7));   /* A4E3 */
            boss_part(g, (uint16_t)(b->col + c), (uint8_t)((b->row + k) & 0x3F), type, v);
        }
    }
    lega_shot_part(g);                                                  /* A544 -> A5FA */
    boss_parts_end(g);                                                  /* A547 */
    lega_shot_step(g);                                                  /* A54B */
}

/* A39F: the face timer, then A44C */
static void lega_face_draw(Game *g)
{
    ST(S_FACET) = (uint8_t)((ST(S_FACET) + 1) & 3);
    ST(S_FACE) = boss_img8(g, LEGA_FACE + ST(S_FACET));
    lega_draw(g);
}

/* A3B5: the three-step launch, table A3C7; every step jumps straight to A44C */
static void lega_attack_step(Game *g)
{
    Boss *b = &g->boss;
    switch (++ST(S_ASTEP)) {
    case 1:                                                             /* A3CD */
        ST(S_FACE) = 6; b->pose = 8;
        ST(S_SHOT) = 0xFF;
        SHOTCOL = (uint16_t)(b->col + 4); ST(S_SHOTROW) = (uint8_t)(b->row & 0x3F);
        ST(S_SHOTFR) = 0; ST(S_SHOTIDX) = 0; ST(S_EXPLODE) = 0;
        break;
    case 2: ST(S_FACE) = 7; b->pose = 6; break;                         /* A3FE */
    default: ST(S_FACE) = 0; ST(S_ATTACK) = 0; b->pose = 6; break;      /* A40A */
    }
    lega_draw(g);
}

/* A66E: 40 frames; while the new count is below 10 the pose is A69B[n]
 * (sound 0x33 from pose 3) and the face runs on (A39F); then face 6.  At
 * 0x28 A6C2 sets FF30 and returns without drawing. */
static void lega_death(Game *g)
{
    if (boss_death_tick(g) >= 0x28) return;
    uint8_t n = g->boss.death_cnt;
    if (n < 10) {
        g->boss.pose = boss_img8(g, LEGA_DEATH + n);
        if (g->boss.pose >= 3) SFX_SET(g->sfx_request, 0x33);
        lega_face_draw(g);
    } else { ST(S_FACE) = 6; lega_draw(g); }                            /* A6A5: `cmp ah,6` never below */
}

/* 0xA223  Frame entry. */
void boss_lega_entry(Game *g)
{
    Boss *b = &g->boss;
    uint8_t hit = boss_readback(g, NULL);                               /* A231: last hit wins */
    boss_hit_flash(g, hit != 0);   /* A501: every body part this frame carries hit bit 5 */

    if (hit) {                                                          /* A275 */
        uint8_t src = (uint8_t)(hit & 0x1F);
        unsigned d = damage_for_source(g, src);                         /* A280: AH */
        if (src == 1) d *= 2;                                           /* A292 */
        else if (src != 9) d >>= 3;                                     /* A296 */
        lega_damage(g, d);                                              /* A644 */
        SFX_SET(g->sfx_request, 0x2F);
        if ((uint8_t)b->col < 0x2F) { ST(S_RTIMER) = 0x14; ST(S_RETREAT) = 0xFF; }   /* A2A4 */
    }
    if (g->boss_cutscene) { lega_death(g); return; }                    /* A2B5 */
    if (ST(S_ATTACK)) { lega_attack_step(g); return; }                  /* A2BF */

    if (ST(S_SHOT) && ST(S_SHOTIDX) < 0x0D) {
        /* A2C9: no movement while the shot is on its way */
    } else if (!ST(S_RETREAT)) {                                        /* A2DA: walk left */
        ST(S_RTIMER) = 0x3C;
        b->pose = (uint8_t)((b->pose + 1) & 7);
        if (in5(g, LEGA_WALKL, b->pose)) {
            ST(S_RETREAT) = (uint8_t)(step_left(g) ? 0xFF : 0);
            if (b->pose == 7) ST(S_RETREAT) = (uint8_t)(step_left(g) ? 0xFF : 0);
        }
    } else if (!--ST(S_RTIMER)) ST(S_RETREAT) = 0;                      /* A316 */
    else for (;;) {                                                     /* A323: poses backwards */
        uint8_t p = b->pose;
        if (p == 0) p = 8;
        if (p == 6) p -= 2;
        b->pose = --p;
        if (!in5(g, LEGA_WALKR, p)) break;
        ST(S_RETREAT) = (uint8_t)(step_right(g) ? 0 : 0xFF);            /* A344: cmc / sbb */
        if (p == 6 || p == 3) { ST(S_RETREAT) = (uint8_t)(step_right(g) ? 0 : 0xFF); break; }
        /* A354: poses 1 and 7 go round again */
    }
    if (!ST(S_RETREAT) && b->pose == 6 && !(krn_random(g) & 1) && !ST(S_SHOT)
        && b->col >= 0x14) {                                            /* A35F: the COLUMN, A37D */
        ST(S_ATTACK) = 0xFF; ST(S_ASTEP) = 0; ST(S_UNUSED_BE) = 0;
        b->pose = 8; SFX_SET(g->sfx_request, 0x30);
    }
    lega_face_draw(g);                                                  /* A39F */
}
