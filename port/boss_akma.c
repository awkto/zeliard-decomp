/* boss_akma.c — cavern 8 boss "Alguien" (AKMA.BIN = ZELRES3[17], 2810 bytes).
 * Ported from src/ai/boss_akma.c and the ndisasm listing of the image.
 *
 *   [A002] = AA06: col 0x2A, row 0x00, HP 800, EXP 30000, camera column 12,
 *   knockback free, name "Alguien", gold 3800.  Contact A010: 40 for every
 *   class except 6 (the beam) = 80.
 *
 * A winged demon that flies back and forth across the top of the room and
 * fires a diagonal beam at the floor at each turn.
 *
 * Image (A4F7): a 13-column x 16-row buffer, column-major (A7CC pastes 13
 * columns of *two* bitmap bytes each, so one column's 16 rows are contiguous).
 * Body list [A7F4 + 2*(anim&3)] / bitmap [A876 + ..] when flying right,
 * [A7EE]/[A870] flying left; anim 0..2 are the three wing positions (25 / 16 /
 * 18 parts).  Two patches are then copied in verbatim (0xFF included):
 *   - the wing tips, 5 columns x 2 rows at buffer (3, 13) right / (5, 13)
 *     left, from A92C / A918 (+10 on odd frames, A53C);
 *   - the face, *2 columns x 1 row* at (10, 9) right / (0, 9) left, from
 *     A94A / A940 + 2*face (A570 writes the second byte at di + 0x10, i.e.
 *     the next column — src/ai/boss_akma.c calls this "1 column x 2 rows").
 * Parts (A59A): {col + c, row + r, type = class, hit = 0x80 while flying
 * right (so the A070 mirrored frame tables are used), phase = the byte}.
 *
 * Damage (A38C): the FULL damage_for_source(), no scaling and no weak point,
 * sound 0x22.
 */
#include "boss.h"
#include <string.h>

enum { S_INIT, S_ANIM, S_DIR, S_FRAME, S_FACE, S_ATTACK, S_BEAM, S_RETRACT, S_STEEP };
#define ST(n) (g->boss.st[n])

#define AK_LIST_R 0xA7F4                /* u16[3] body lists, flying right */
#define AK_BM_R   0xA876
#define AK_LIST_L 0xA7EE
#define AK_BM_L   0xA870
#define AK_TIP_R  0xA92C                /* 10 bytes per frame parity, 5 cols x 2 rows */
#define AK_TIP_L  0xA918
#define AK_FACE_R 0xA94A                /* 2 bytes per face */
#define AK_FACE_L 0xA940
#define AK_PATH_R 0xA954                /* u8[21]: row for column 10 + 2*i */
#define AK_PATH_L 0xA969

#define AK_W 13
#define AK_H 16

/* the original stores the facing in the record's `hit` byte (A5C9), so the
 * mirrored A070 frame tables are used while it flies right. */
static void part_facing(Game *g, uint16_t col, uint8_t row, uint8_t type, uint8_t phase, uint8_t hit)
{
    int before = g->nobj;
    boss_part(g, col, row, type, phase);
    if (g->nobj > before) g->obj[g->nobj - 1].hit = hit;
}

/* A4F7 */
static void akma_draw(Game *g)
{
    Boss *b = &g->boss;
    uint8_t buf[AK_W * AK_H];
    int right = ST(S_DIR) != 0;
    unsigned a = ST(S_ANIM) & 3; if (a > 2) a = 2;
    memset(buf, 0xFF, sizeof buf);
    boss_paste(g, buf, AK_W, AK_H, 0, 0, 13, 2,
               boss_img16(g, (right ? AK_LIST_R : AK_LIST_L) + 2 * a),
               boss_img16(g, (right ? AK_BM_R   : AK_BM_L)   + 2 * a));
    /* A529: the wing tips, copied verbatim (0xFF clears a cell) */
    unsigned tip = (right ? AK_TIP_R : AK_TIP_L) + ((ST(S_FRAME) & 1) ? 10u : 0u);
    int tx = right ? 3 : 5;
    for (int c = 0; c < 5; c++) {
        buf[(tx + c) * AK_H + 13] = boss_img8(g, tip + 2u * (unsigned)c);
        buf[(tx + c) * AK_H + 14] = boss_img8(g, tip + 2u * (unsigned)c + 1);
    }
    /* A553: the face, two horizontally adjacent cells on row 9 */
    unsigned fc = (right ? AK_FACE_R : AK_FACE_L) + 2u * ST(S_FACE);
    int fx = right ? 10 : 0;
    buf[fx * AK_H + 9] = boss_img8(g, fc);
    buf[(fx + 1) * AK_H + 9] = boss_img8(g, fc + 1);

    boss_parts_begin(g);
    for (int c = 0; c < AK_W; c++)
        for (int r = 0; r < AK_H; r++) {
            uint8_t v = buf[c * AK_H + r];
            if (v == 0xFF) continue;
            part_facing(g, (uint16_t)(b->col + c), (uint8_t)(b->row + r),
                        (uint8_t)((v >> 4) & 0x0F), v, (uint8_t)(right ? 0x80 : 0));
        }
}

/* A617: the beam — beam_len parts of type 0x26 (class 6, sword-immune,
 * contact 80) hanging forward-down from the body.  Segment k = 1..len-1 uses
 * frame 3 (shallow) / 7 (steep), the tip (k = len) frame 2 / 6. */
static void akma_beam(Game *g)
{
    Boss *b = &g->boss;
    if (!ST(S_ATTACK) || !ST(S_BEAM)) { boss_parts_end(g); return; }
    int right = ST(S_DIR) != 0, steep = ST(S_STEEP) != 0;
    int n = ST(S_BEAM);
    for (int k = 1; k <= n; k++) {
        int col, row;
        if (!right) col = steep ? (int)b->col + 1 - 2 * k : (int)b->col - 2 * k;
        else        col = steep ? (int)b->col + 10 + 2 * k : (int)b->col + 11 + 2 * k;
        row = b->row + 9 + (steep ? 2 * k : k);
        uint8_t frame = (uint8_t)(k == n ? (steep ? 6 : 2) : (steep ? 7 : 3));
        part_facing(g, (uint16_t)col, (uint8_t)(row & 0x3F), 0x26, frame, (uint8_t)(right ? 0x80 : 0));
    }
    boss_parts_end(g);
}

/* A4D4 / A4E6: two cells at a time between columns 10 and 0x33 */
static int step_left2(Game *g)  { if ((int)g->boss.col - 2 <= 9)   return 1; g->boss.col -= 2; return 0; }
static int step_right2(Game *g) { if ((int)g->boss.col + 2 > 0x33) return 1; g->boss.col += 2; return 0; }

/* A40E: the hero's map column */
static uint16_t akma_hero_col(const Game *g)
{
    unsigned c = (unsigned)g->scroll_col + (unsigned)g->hero_scr_col;
    if (g->map && c >= (unsigned)g->map->width) c -= (unsigned)g->map->width;
    return (uint16_t)c;
}

static void start_beam(Game *g)
{
    ST(S_RETRACT) = 0; ST(S_BEAM) = 0; ST(S_ATTACK) = 0xFF;
    g->sfx_request = 0x34;
}

static uint8_t path_row(const Game *g, int right)
{
    int i = ((int)g->boss.col - 10) / 2;
    if (i < 0) i = 0; else if (i > 20) i = 20;
    return boss_img8(g, (unsigned)((right ? AK_PATH_R : AK_PATH_L) + i));
}

/* A9B0: 40 frames hovering; the first 30 keep the wings beating with the face
 * toggling and sound 0x37 every 4th frame, then wings 1 / face 1. */
static void akma_death(Game *g)
{
    uint8_t t = g->boss.death_cnt;
    if (boss_death_tick(g) >= 0x28) { boss_parts_begin(g); boss_parts_end(g); return; }
    if (t < 0x1E) {
        if (++ST(S_ANIM) == 3) ST(S_ANIM) = 0;
        ST(S_FRAME)++;
        ST(S_FACE) = (uint8_t)((ST(S_FACE) + 1) & 1);
        if (!(ST(S_FRAME) & 3)) g->sfx_request = 0x37;
    } else { ST(S_ANIM) = 1; ST(S_FACE) = 1; }
    akma_draw(g);
    boss_parts_end(g);
}

/* 0xA32B  Frame entry. */
void boss_akma_entry(Game *g)
{
    if (!ST(S_INIT)) { ST(S_INIT) = 1; ST(S_DIR) = 0xFF; }          /* AA21 starts 0xFF */
    uint8_t hit = boss_readback(g, NULL);                           /* A32F */

    if (hit) {                                                      /* A38C: no scaling */
        g->sfx_request = 0x22;
        boss_damage(g, damage_for_source(g, (uint8_t)(hit & 0x1F)));
        if (g->boss.hp == 0) ST(S_ATTACK) = 0;                      /* A97E */
    }
    if (g->boss_cutscene) { akma_death(g); return; }                /* A3AB */

    ST(S_FACE) = 0;                                                 /* A3B5 */
    if (++ST(S_ANIM) == 3) ST(S_ANIM) = 0;
    if (ST(S_ANIM) == 1) g->sfx_request = 0x2B;
    ST(S_FRAME)++;

    if (!ST(S_DIR)) {                                               /* A3D5: flying left */
        if (!step_left2(g)) g->boss.row = path_row(g, 0);
        else if ((g->boss.row = (uint8_t)((g->boss.row - 2) & 0x3F)) == 0x3D) {
            ST(S_DIR) = 0xFF; start_beam(g); ST(S_STEEP) = (uint8_t)(akma_hero_col(g) < 40);
        }
    } else {                                                        /* A42E: flying right */
        if (!step_right2(g)) g->boss.row = path_row(g, 1);
        else if ((g->boss.row = (uint8_t)((g->boss.row - 2) & 0x3F)) == 0x3D) {
            ST(S_DIR) = 0; start_beam(g); ST(S_STEEP) = (uint8_t)(akma_hero_col(g) >= 20);
        }
    }

    if (ST(S_ATTACK)) {                                             /* A492 */
        ST(S_FACE) = (uint8_t)(ST(S_STEEP) + 2);
        if (!ST(S_RETRACT)) { if (++ST(S_BEAM) >= 7 + !ST(S_STEEP)) ST(S_RETRACT) = 0xFF; }
        else if (--ST(S_BEAM) == 0) ST(S_ATTACK) = 0;
    }
    akma_draw(g);                                                   /* A4F7 */
    akma_beam(g);                                                   /* A617 */
}
