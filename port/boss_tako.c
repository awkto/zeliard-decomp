/* boss_tako.c — cavern 2 boss "Pulpo" (TAKO.BIN = ZELRES3[10]).  Ported from
 * src/ai/boss_tako.c; the pose images come out of the overlay itself.
 *
 *   [A002] = AA80: col 0x24, row 0x10, HP 250, EXP 200, camera column 7,
 *   knockback always left, name "Pulpo", gold 200.  Contact A010 = 10.
 *   Pose p (0..0x1F): 7-byte bitmap at [A9AF + 2p], one byte per part
 *   COLUMN (bit 7 = the top row) plus a list of {type, frame} words at
 *   [A57D + 2p] consumed for every set bit, top to bottom, left to right.
 *   Part cell = (boss_col + 2*byte, boss_row + 2*bit) (A401..A487, #75).
 *   Pose = stage (0 / 8 / 0x10, one step per clean hit) + frame (0..7); the
 *   final stage adds 8 for the attack pose.
 */
#include "boss.h"
#include <string.h>

#define A9AF 0xA9AF                     /* u16[32] pose -> 7-byte bitmap */
#define A57D 0xA57D                     /* u16[32] pose -> {type, frame} words */
#define AA20 0xAA20                     /* 24 x 4 ink-cloud frames (A492) */

enum { S_FRAME, S_STAGE, S_REACT, S_ATTACK, S_INKROW };
#define ST(n) (g->boss.st[n])
#define INKCOL (g->boss.sw[0])

static int tako_weak(uint8_t type) { return (type & 0x1F) >= 0x0E; }        /* A2DE */

/* 0xA3E3  Compose the pose and, when it is out, the ink cloud. */
static void tako_draw(Game *g, uint8_t pose)
{
    Boss *b = &g->boss;
    boss_parts_begin(g);
    unsigned bm = boss_img16(g, A9AF + 2u * (pose & 0x1F));
    unsigned ls = boss_img16(g, A57D + 2u * (pose & 0x1F));
    /* A401: the OUTER loop is 7 part columns (`add ax,2` at A487 on the map
     * column, one bitmap byte each, A486), the inner 8 rows (row + 2*cl,
     * A42B): {type, phase} pairs off the list, one per set bit; a column
     * outside the ring (A412) skips its pairs.  (#75: the port read the
     * bytes as rows -- pulpo_fight f120.) */
    if (bm && ls) {
        unsigned k = 0;
        for (int c = 0; c < 7; c++) {
            uint8_t bits = boss_img8(g, bm + (unsigned)c);
            for (int r = 0; r < 8; r++) {
                if (!(bits & (0x80 >> r))) continue;
                uint8_t type  = boss_img8(g, ls + 2 * k);
                uint8_t frame = boss_img8(g, ls + 2 * k + 1);
                k++;
                boss_part(g, (uint16_t)(b->col + 2 * c), (uint8_t)(b->row + 2 * r), type, frame);
            }
        }
    }
    /* 0xA492: four ink parts (class 0x10 = sword-immune 0x30, contact 10).
     * A4D4 writes their hit byte as 0 -- no A44D bit 5 on the ink. */
    if (ST(S_ATTACK) & 0x80) {
        unsigned n = (unsigned)(ST(S_ATTACK) & 0x1F);
        uint8_t ph = g->boss.part_hit;
        g->boss.part_hit = 0;
        if (n >= 1 && n <= 24)
            for (int i = 0; i < 4; i++) {
                uint8_t f = boss_img8(g, AA20 + (n - 1) * 4 + (unsigned)i);
                if (!f) continue;
                boss_part(g, (uint16_t)(INKCOL + i), ST(S_INKROW), 0x30, (uint8_t)(f - 1));
            }
        g->boss.part_hit = ph;
    }
    boss_parts_end(g);
}

/* 0xA530  Death: [AA99] (the attack) is dropped at once; [AA9E] counts 40
 * frames.  The first 20 (A549) advance the pose frame [AA96], flicker
 * stage / stage + 8 on the counter's parity *after* its increment and
 * request sound 0x28 every frame; then (A56B) stage + 8 with the frame
 * held.  At 0x28 A577 sets FF30 and returns without drawing. */
static void tako_death(Game *g)
{
    ST(S_ATTACK) = 0;                                                      /* A530 */
    uint8_t t = g->boss.death_cnt;
    if (boss_death_tick(g) >= 0x28) { boss_parts_begin(g); boss_parts_end(g); return; }
    uint8_t p;
    if (t < 0x14) {
        ST(S_FRAME) = (uint8_t)((ST(S_FRAME) + 1) & 7);                    /* A550 */
        p = (uint8_t)(ST(S_STAGE) + (((t + 1) & 1) ? 8 : 0));              /* A555..A55D */
        g->sfx_request = 0x28;                                             /* A563 */
    } else p = (uint8_t)(ST(S_STAGE) + 8);                                 /* A56D */
    tako_draw(g, (uint8_t)(p + ST(S_FRAME)));
}

/* 0xA27D  Frame entry. */
void boss_tako_entry(Game *g)
{
    Boss *b = &g->boss;
    uint8_t hit = boss_readback(g, tako_weak);                             /* A28B..A2D4 */
    boss_hit_flash(g, hit != 0);   /* A44D: every part this frame carries hit bit 5 */

    if (hit) {                                                             /* A2DE */
        unsigned d = (unsigned)damage_for_source(g, (uint8_t)(hit & 0x1F)) * 2u;
        if (hit & 0x80) { d *= 2; g->sfx_request = 0x24; } else g->sfx_request = 0x25;
        boss_damage(g, d);                                                 /* A503 */
        if (!(ST(S_REACT) & 0x10) && ST(S_STAGE) != 0x10) {                /* A309 */
            ST(S_STAGE) = (uint8_t)(ST(S_STAGE) + 8);
            ST(S_REACT) = 0x10; ST(S_ATTACK) |= 0x20; g->sfx_request = 0x26;
        }
    }
    if (g->boss_cutscene) { tako_death(g); return; }                       /* A530 */

    ST(S_FRAME) = (uint8_t)((ST(S_FRAME) + 1) & 7);                        /* A334 */
    uint8_t p = ST(S_STAGE);
    if (ST(S_REACT) & 0x10) {                                              /* A344: 16-frame flicker */
        ST(S_REACT) ^= 0x20;
        if (!(ST(S_REACT) & 0x20)) p = (uint8_t)(p - 8);
        ST(S_REACT) = (uint8_t)((ST(S_REACT) & 0xF0) | ((ST(S_REACT) + 1) & 0xF));
        if (!(ST(S_REACT) & 0xF)) { ST(S_REACT) &= (uint8_t)~0x10; ST(S_ATTACK) &= (uint8_t)~0x20; }
    }
    if (p == 0x10) {                                                       /* A36F: the final stage attacks */
        if (ST(S_ATTACK) & 0x40) {                                         /* A377: 4-frame wind-up */
            uint8_t a = (uint8_t)(ST(S_ATTACK) ^ 0x20), n = (uint8_t)((a + 1) & 3);
            ST(S_ATTACK) = (uint8_t)((a & 0xE0) | n);
            if (!n) {                                                      /* A391: launch the ink cloud */
                ST(S_ATTACK) = 0xA0;
                INKCOL = (uint16_t)(b->col + 4); ST(S_INKROW) = (uint8_t)((b->row + 4) & 0x3F);
                g->sfx_request = 0x27;
            }
        }
        if (!(ST(S_ATTACK) & 0xA0) && !(ST(S_REACT) & 0x10)) ST(S_ATTACK) |= 0x40;   /* A3AC */
        if (!(ST(S_ATTACK) & 0x20)) p = (uint8_t)(p + 8);                  /* A3BB */
        if (ST(S_ATTACK) & 0x80) {                                         /* A3C3: 24 frames drifting left */
            ST(S_ATTACK) = (uint8_t)((ST(S_ATTACK) & 0xE0) | ((ST(S_ATTACK) + 1) & 0x1F));
            INKCOL--;
            if ((ST(S_ATTACK) & 0x1F) == 0x19) ST(S_ATTACK) = 0;
        }
    }
    tako_draw(g, (uint8_t)(p + ST(S_FRAME)));                              /* A3E3 */
}
