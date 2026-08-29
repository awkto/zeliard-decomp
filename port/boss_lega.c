/* boss_lega.c — cavern 6 boss "Tarso" (LEGA.BIN = ZELRES3[14]).  Ported from
 * src/ai/boss_lega.c.
 *
 *   [A002] = A7A0: col 0x26, row 0x07, HP 640, EXP 6000, camera column 8,
 *   knockback always left, name "Tarso", gold 1500.  Contact A010: 160 for
 *   classes 0..5, 80 for class 6 (its projectile), 10 for 7..15.
 *
 * Image: 8 rows x 8 columns.  Pose p (0..8) selects the 8-byte column bitmap
 * `[A744 + 2p]` and the byte list `[A6C8 + 2p]`; every set bit takes one byte
 * whose bit 7 means "immune and harmless" (`type |= 0x60`), whose bits 4-6 are
 * the part class and which is itself the frame.  (The two run lengths match
 * the bitmap popcounts 7,9,10,11,11,12,10,12,11 exactly.)  The face is patched
 * into buffer slots 0x28/0x29 (one column right on poses 6 and 8) and 0x3C
 * with frames 2*face and 2*face+1.
 *
 * Damage: sword x2, orb x1, everything else /8.  Walks LEFT on poses 1,2,3,7
 * of an 8-frame cycle down to column 0x0E; a hit before column 0x2F makes it
 * back off right for 20 frames.  On pose 6, 1/2, HP >= 20 and no shot out it
 * launches a projectile PART (class 6, immune, contact 80) from (col+4, row)
 * along the 17-step path at A5D8, which explodes below column 0x12.
 */
#include "boss.h"
#include <string.h>

enum { S_POSE, S_FACE, S_FACET, S_RETREAT, S_RTIMER, S_ATTACK, S_ASTEP,
       S_SHOT, S_SHOTROW, S_SHOTFR, S_SHOTIDX, S_EXPLODE };
#define ST(n) (g->boss.st[n])
#define SHOTCOL (g->boss.sw[0])

#define LEGA_BM   0xA744                /* u16[9] pose -> 8-byte column bitmap */
#define LEGA_LIST 0xA6C8                /* u16[9] pose -> byte list */
#define LEGA_PATH 0xA5D8                /* 17 x {s8 dcol, s8 drow} */
#define LEGA_FACE 0xA41B                /* the 8-entry face cycle */

static int walks(uint8_t pose) { return pose == 1 || pose == 2 || pose == 3 || pose == 7; }

static int step_left_0E(Game *g)  { if (g->boss.col <= 0x0E) return 1; g->boss.col--; return 0; }
static int step_right_32(Game *g) { if (g->boss.col >= 0x32) return 1; g->boss.col++; return 0; }

/* A44C: compose the pose into an 8x8 buffer and place the parts */
static void lega_draw(Game *g)
{
    Boss *b = &g->boss;
    uint8_t buf[64];
    memset(buf, 0xFF, sizeof buf);
    unsigned bm = boss_img16(g, LEGA_BM + 2u * (b->pose % 9u));
    unsigned ls = boss_img16(g, LEGA_LIST + 2u * (b->pose % 9u));
    unsigned k = 0;
    for (int r = 0; r < 8; r++) {
        uint8_t bits = boss_img8(g, bm + (unsigned)r);
        for (int c = 0; c < 8; c++)
            if (bits & (0x80 >> c)) buf[r * 8 + c] = boss_img8(g, ls + k++);
    }
    /* A483: the face, two parts, frames 2*face and +1 */
    int fslot = 0x28 + ((b->pose == 6 || b->pose >= 8) ? 1 : 0);
    buf[fslot] = (uint8_t)(2 * ST(S_FACE));
    if (fslot + 1 < 64) buf[fslot + 1] = (uint8_t)(2 * ST(S_FACE) + 1);
    buf[0x3C] = (uint8_t)(2 * ST(S_FACE));

    boss_parts_begin(g);
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++) {
            uint8_t v = buf[r * 8 + c];
            if (v == 0xFF) continue;
            uint8_t type = (uint8_t)((v >> 4) & 7);
            if (v & 0x80) type |= 0x60;                                 /* immune + harmless */
            boss_part(g, (uint16_t)(b->col + c), (uint8_t)(b->row + r), type, v);
        }
    /* A5FA: the projectile is a part of its own (class 6, immune, contact 80) */
    if (ST(S_SHOT))
        boss_part(g, SHOTCOL, ST(S_SHOTROW), 0x26, ST(S_SHOTFR));
    boss_parts_end(g);
}

/* A544: the projectile follows the 17-step path and explodes below column 0x12 */
static void lega_shot_step(Game *g)
{
    if (!ST(S_SHOT)) return;
    if (ST(S_EXPLODE)) {
        ST(S_SHOTFR) = (uint8_t)(3 + (ST(S_EXPLODE) - 1) / 4);
        if (++ST(S_EXPLODE) > 12) { ST(S_SHOT) = 0; ST(S_EXPLODE) = 0; }
        return;
    }
    if (SHOTCOL < 0x12) { ST(S_EXPLODE) = 1; g->sfx_request = 0x32; return; }
    unsigned i = ST(S_SHOTIDX) < 16 ? ST(S_SHOTIDX) : 16;
    int dc = (int8_t)boss_img8(g, LEGA_PATH + 2 * i);
    int dr = (int8_t)boss_img8(g, LEGA_PATH + 2 * i + 1);
    SHOTCOL = (uint16_t)(SHOTCOL + dc);
    ST(S_SHOTROW) = (uint8_t)((ST(S_SHOTROW) + dr) & 0x3F);
    ST(S_SHOTFR) = (uint8_t)((ST(S_SHOTFR) + 1) % 3);
    if (i == 9 || i == 12 || i == 15) g->sfx_request = 0x31;
    if (ST(S_SHOTIDX) < 16) ST(S_SHOTIDX)++;
}

/* A3B5/A3C7: the three-step launch sequence */
static void lega_attack_step(Game *g)
{
    Boss *b = &g->boss;
    switch (ST(S_ASTEP)++) {
    case 0:
        ST(S_FACE) = 6; b->pose = 8;
        ST(S_SHOT) = 0xFF; ST(S_SHOTIDX) = 0; ST(S_SHOTFR) = 0; ST(S_EXPLODE) = 0;
        SHOTCOL = (uint16_t)(b->col + 4); ST(S_SHOTROW) = b->row;
        return;
    case 1: ST(S_FACE) = 7; b->pose = 6; return;
    default: ST(S_FACE) = 0; ST(S_ATTACK) = 0; b->pose = 6; return;
    }
}

/* A66E: 40 frames; the first 10 run the poses at A69B with sound 0x33 */
static void lega_death(Game *g)
{
    uint8_t t = g->boss.death_cnt;
    if (boss_death_tick(g) >= 0x28) { boss_parts_begin(g); boss_parts_end(g); return; }
    if (t < 10) {
        g->boss.pose = boss_img8(g, 0xA69B + t);
        if (g->boss.pose >= 3) g->sfx_request = 0x33;
    } else ST(S_FACE) = boss_img8(g, 0xA6BC + (unsigned)((t - 10) % 6));
    lega_draw(g);
}

/* 0xA223  Frame entry. */
void boss_lega_entry(Game *g)
{
    Boss *b = &g->boss;
    uint8_t hit = boss_readback(g, NULL);                               /* A231 */
    boss_hit_flash(g, hit != 0);   /* A501: every part this frame carries hit bit 5 */

    if (hit) {                                                          /* A275 */
        unsigned d = damage_for_source(g, (uint8_t)(hit & 0x1F));
        if ((hit & 0x1F) == 1) d *= 2;
        else if ((hit & 0x1F) != 9) d /= 8;
        boss_damage(g, d);                                              /* A644 */
        g->sfx_request = 0x2F;
        if (b->col < 0x2F) { ST(S_RTIMER) = 20; ST(S_RETREAT) = 0xFF; } /* A2A4 */
    }
    if (g->boss_cutscene) { lega_death(g); return; }                    /* A66E */

    if (ST(S_ATTACK)) lega_attack_step(g);                              /* A3B5 */
    else if (!(ST(S_SHOT) && ST(S_SHOTIDX) < 13)) {                     /* A2C9: no walk while the shot flies */
        if (!ST(S_RETREAT)) {                                           /* A2DA: walk left */
            ST(S_RTIMER) = 60;
            b->pose = (uint8_t)((b->pose + 1) & 7);
            if (walks(b->pose)) {
                ST(S_RETREAT) = (uint8_t)(step_left_0E(g) ? 0xFF : 0);
                if (b->pose == 7 && !ST(S_RETREAT)) ST(S_RETREAT) = (uint8_t)(step_left_0E(g) ? 0xFF : 0);
            }
        } else if (!--ST(S_RTIMER)) ST(S_RETREAT) = 0;                  /* A316 */
        else {                                                          /* A323: poses backwards, stepping right */
            b->pose = (uint8_t)(b->pose ? b->pose - 1 : 8);
            if (walks(b->pose)) step_right_32(g);
            if (b->pose == 6 || b->pose == 3) step_right_32(g);
        }
    }
    if (!ST(S_RETREAT) && b->pose == 6 && !(krn_random(g) & 1) && !ST(S_SHOT) && b->hp >= 20) {  /* A35F */
        ST(S_ATTACK) = 0xFF; ST(S_ASTEP) = 0; ST(S_FACET) = 0;
        b->pose = 8; g->sfx_request = 0x30;
    }
    ST(S_FACET) = (uint8_t)((ST(S_FACET) + 1) & 3);                     /* A39F */
    if (!ST(S_ATTACK)) ST(S_FACE) = boss_img8(g, LEGA_FACE + ST(S_FACET));
    lega_shot_step(g);                                                  /* A544 */
    lega_draw(g);                                                       /* A44C */
}
