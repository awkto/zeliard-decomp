/* boss_mao2.c — the final boss "Jashiin" (MAO2.BIN = ZELRES3[19], 3183 bytes),
 * map mpa0.  Ported from src/ai/boss_mao2.c and the image's own listing.
 *
 *   [A002] = AC03: col 0x30, row 9, HP 800, EXP 10000, camera column 12,
 *   knockback free, name "Jashiin", gold 0.  Contact A010 = 80 for every
 *   class, the two projectiles (4 and 5) included.
 *
 * Image (A6BC): a 6-column x 9-row buffer, column-major; list [A9E4 + 2*pose]
 * / bitmap [AAE1 + ..] facing right, [A957]/[AA71] facing left (one bitmap
 * byte per column, bit 7 = row 0; 7..9 parts).  Poses 0..3 walk, 4 crouch,
 * 5 crouch with two extra cells patched into row 8 (A6EA: 0x23/0x1F facing
 * right, 0x1F/0x21 left), 6 jump, 7..9 throw (9 releases), 10..13 cast (12
 * releases).  Parts (A70E): {col + c, row + r, type = class | [AC22] (0x60 =
 * sword-immune + harmless while materialising), hit = 0x80 facing right,
 * phase = the byte}.
 *
 * Damage (A362): sword /2, everything else /4, sound 0x39.  Below 200 HP the
 * fight enters phase 2.
 *
 * Phase 1 (A3BF) — teleport and strike: Jashiin is invisible until no
 * projectile of his is in flight, then picks a side of the hero (scroll_col +
 * 4 facing right, + 24 facing left; the other side when that column is outside
 * 0x10..0x34), row 9, and runs a 17-step sequence: 5 frames flickering in
 * (odd steps not drawn, even steps immune, sound 0x3B), 5 solid frames of
 * {0,0,7,7,9} (throw) or {10,10,11,11,12} (cast) — the only frames he can be
 * hurt in — then 6 frames flickering out.
 *
 * Phase 2 (A4C9) — HP < 200: he stays visible, faces the hero and keeps
 * exactly 8 columns from him (1 cell on odd walk poses, 2 on even, columns
 * 0x10..0x35); a wall starts the 14-frame jump arc A666 ({fwd, drow, pose},
 * 0x80 ends it).  At range for two consecutive frames, 1/16 per frame, he
 * throws.  Every 32 frames he REGENERATES 80 HP (sound 0x3C) and at 800 he
 * drops back into phase 1 at step 10.
 *
 * Projectiles are marker PARTS, not fight.bin shots, so they do contact
 * damage 80 and the shield does not apply.  1 (A8E5, sound 0x3A): type 0x24,
 * from (col + 5 / col, row + 4), falling one row for ages 0..2 and moving one
 * column for ages 0..8, frame 0 then (age & 3) + 1, gone at age 11.  2
 * (A90E): type 0x25, from (col + 7 / col - 1, row + 4), falls three rows
 * (frame 2) then flies horizontally (frame 0) until its column leaves
 * 0x10..0x38.
 *
 * `started` (A3A0): nothing is drawn until [FF21] is non-zero.  fight.bin
 * never writes that byte: it is the score's F1 (mmao, 1625 driver ticks in), so
 * the shell steps a silent copy of the score by frames (audio_clock_*).
 */
#include "boss.h"
#include <string.h>

enum { S_INIT, S_POSE, S_FACING, S_STARTED, S_TYPEBITS, S_TELE, S_VARIANT, S_STEP,
       S_S1ON, S_S1ROW, S_S1DIR, S_S1AGE, S_S2ON, S_S2ROW, S_S2DIR, S_S2AGE,
       S_PHASE2, S_JUMP, S_JUMPI, S_ATRANGE, S_THROW, S_THROWI, S_FRAME };
#define ST(n) (g->boss.st[n])
#define S1COL (g->boss.sw[0])
#define S2COL (g->boss.sw[1])

#define M2_LIST_R 0xA9E4                /* u16[14] */
#define M2_BM_R   0xAAE1
#define M2_LIST_L 0xA957
#define M2_BM_L   0xAA71
#define M2_STRIKE 0xA46F                /* {0,0,7,7,9, 10,10,11,11,12} */
#define M2_DEATH  0xABF9                /* {8,8,8,12,12,12,13,13,11,11} */
#define M2_JUMP   0xA666                /* {fwd, s8 drow, pose} x 14, then 0x80 */

#define M2_W 6
#define M2_H 9

static void part_facing(Game *g, uint16_t col, uint8_t row, uint8_t type, uint8_t phase, uint8_t hit)
{
    int before = g->nobj;
    boss_part(g, col, row, type, phase);
    if (g->nobj > before) g->obj[g->nobj - 1].hit = hit;
}

/* A6BC */
static void mao2_draw(Game *g)
{
    Boss *b = &g->boss;
    uint8_t buf[M2_W * M2_H];
    int right = ST(S_FACING) != 0;
    unsigned pose = ST(S_POSE) > 13 ? 13 : ST(S_POSE);
    memset(buf, 0xFF, sizeof buf);
    boss_paste(g, buf, M2_W, M2_H, 0, 0, 6, 1,
               boss_img16(g, (right ? M2_LIST_R : M2_LIST_L) + 2 * pose),
               boss_img16(g, (right ? M2_BM_R   : M2_BM_L)   + 2 * pose));
    if (pose == 5) {                                    /* A6EA: the crouch patch */
        /* A6F1..A709: right: [AC41] (column 0) = 0x23, [AC4A] (column 1) = 0x1F; left: [AC65]
         * (column 4) = 0x1F, [AC6E] (column 5) = 0x21 -- row 8 of the 9-byte columns at AC39 */
        if (right) { buf[0 * M2_H + 8] = 0x23; buf[1 * M2_H + 8] = 0x1F; }
        else       { buf[4 * M2_H + 8] = 0x1F; buf[5 * M2_H + 8] = 0x21; }
    }
    for (int c = 0; c < M2_W; c++)
        for (int r = 0; r < M2_H; r++) {
            uint8_t v = buf[c * M2_H + r];
            if (v == 0xFF) continue;
            part_facing(g, (uint16_t)(b->col + c), (uint8_t)((b->row + r) & 0x3F),
                        (uint8_t)(((v >> 4) & 0x0F) | ST(S_TYPEBITS)), v,
                        /* A75B..A76A: +5 = facing bit 7, ORed with bit 5 while a hit was read back
                         * this frame ([AC1D]); the shots' own writes (A7EC, A8A9) carry the facing only */
                        (uint8_t)((right ? 0x80 : 0) | g->boss.part_hit));
        }
}

/* A7AE / A7B8 / A85B: the two projectiles, advanced and drawn together. */
static void mao2_shots(Game *g)
{
    if (ST(S_S1ON)) {                                   /* A7B8 */
        uint8_t age = ST(S_S1AGE);
        if (age < 3) ST(S_S1ROW) = (uint8_t)((ST(S_S1ROW) + 1) & 0x3F);
        if (age < 9) S1COL = (uint8_t)(S1COL + (ST(S_S1DIR) ? 1 : -1));   /* ac29 is a byte */
        /* A805..A815: frame 0 below age 3 (`cmp ah,3 / jc`), then (age & 3) + 1.
         * A7EC: vec 27 CF just skips the part; the age below ticks anyway (A83F). */
        part_facing(g, S1COL, ST(S_S1ROW), 0x24,
                    (uint8_t)(age < 3 ? 0 : (age & 3) + 1), (uint8_t)(ST(S_S1DIR) ? 0x80 : 0));
        if (++ST(S_S1AGE) >= 11) ST(S_S1ON) = 0;
    }
    if (ST(S_S2ON)) {                                   /* A85B */
        /* A862: the first 3 frames also fall one row (frame 2); the column steps
         * EVERY frame (A86F..A87F), then vec 27 (A886: CF skips the part, A8C8),
         * and only after the draw the age ticks and the 0x10..0x38 test ends it
         * (A8CC..A8DF): the last step is still drawn. */
        uint8_t age = ST(S_S2AGE);
        uint8_t frame = 0;
        if (age < 3) { ST(S_S2ROW) = (uint8_t)((ST(S_S2ROW) + 1) & 0x3F); frame = 2; }
        S2COL = (uint8_t)(S2COL + (ST(S_S2DIR) ? 1 : -1));       /* ac2e is a byte */
        part_facing(g, S2COL, ST(S_S2ROW), 0x25, frame, (uint8_t)(ST(S_S2DIR) ? 0x80 : 0));
        ST(S_S2AGE)++;
        if ((uint8_t)S2COL < 0x10 || (uint8_t)S2COL >= 0x39) ST(S_S2ON) = 0;
    }
    boss_parts_end(g);
}

static void launch_shot1(Game *g)                       /* A8E5 */
{
    Boss *b = &g->boss;
    ST(S_S1ON) = 0xFF; ST(S_S1AGE) = 0; ST(S_S1DIR) = ST(S_FACING);
    S1COL = (uint16_t)(ST(S_FACING) ? b->col + 5 : b->col);
    ST(S_S1ROW) = (uint8_t)((b->row + 4) & 0x3F);
    g->sfx_request = 0x3A;
}

static void launch_shot2(Game *g)                       /* A90E */
{
    Boss *b = &g->boss;
    ST(S_S2ON) = 0xFF; ST(S_S2AGE) = 0; ST(S_S2DIR) = ST(S_FACING);
    S2COL = (uint16_t)(ST(S_FACING) ? b->col + 7 : b->col - 1);
    ST(S_S2ROW) = (uint8_t)((b->row + 4) & 0x3F);
}

/* A691 / A6A7: one cell, columns 0x10..0x35; a successful step clears the
 * at-range latch.  true = blocked. */
static int step_left(Game *g)  { if ((int)g->boss.col - 1 <= 0x0E) return 1; g->boss.col--; ST(S_ATRANGE) = 0; return 0; }
static int step_right(Game *g) { if ((int)g->boss.col + 1 > 0x35)  return 1; g->boss.col++; ST(S_ATRANGE) = 0; return 0; }

/* A4F5: the hero's map column (+3, the middle of the sprite) */
static uint8_t mao2_hero_col(const Game *g)
{
    unsigned c = (unsigned)g->scroll_col + (unsigned)g->hero_scr_col + 3;
    if (g->map && c >= (unsigned)g->map->width) c -= (unsigned)g->map->width;
    return (uint8_t)c;
}

/* A479: one random draw, bit 0 (shr al,1 / sbb al,al -> [AC1E]): set = face
 * right and appear four columns right of scroll_col, clear = face left at
 * scroll_col + 24 (8-bit sums, wrapped once at the map width [C002]).  If that
 * column is outside 0x10..0x34 (A4A1) the side flips and the other column is
 * taken as it is, unchecked (A4AA..A4C8).  Row 9 (A479: [AC05]). */
static uint8_t mao2_side_col(Game *g, int right)
{
    unsigned width = g->map ? (unsigned)g->map->width : 256u;
    uint8_t c = (uint8_t)(g->scroll_col + (right ? 4u : 0x18u));
    if (c >= width) c = (uint8_t)(c - width);
    return c;
}

static void mao2_teleport(Game *g)
{
    int right = (krn_random(g) & 1) != 0;
    uint8_t c = mao2_side_col(g, right);
    if (c < 0x10 || c >= 0x35) { right = !right; c = mao2_side_col(g, right); }
    g->boss.col = c;
    ST(S_FACING) = (uint8_t)(right ? 0xFF : 0);
    g->boss.row = 9;
}

/* AB88: +80 HP every 32 frames; back to 800 resumes phase 1 at the flicker-out */
static void mao2_regenerate(Game *g)
{
    Boss *b = &g->boss;
    if (b->hp >= b->hp0) return;
    unsigned hp = (unsigned)b->hp + 80;
    if (hp >= b->hp0) {
        hp = b->hp0;
        ST(S_PHASE2) = 0; ST(S_STEP) = 10; ST(S_TELE) = 0xFF; ST(S_TYPEBITS) = 0x60;
    }
    b->hp = (uint16_t)hp;
    g->sfx_request = 0x3C;
}

/* A617: one A666 entry per frame */
static void mao2_jump_step(Game *g)
{
    Boss *b = &g->boss;
    unsigned e = M2_JUMP + 3u * ST(S_JUMPI);
    uint8_t fwd = boss_img8(g, e);
    if (fwd == 0x80) { ST(S_JUMP) = 0; return; }
    int8_t drow = (int8_t)boss_img8(g, e + 1);
    ST(S_POSE) = boss_img8(g, e + 2);
    if (fwd) { if (ST(S_FACING)) { step_right(g); step_right(g); } else { step_left(g); step_left(g); } }
    b->row = (uint8_t)((b->row + drow) & 0x3F);
    if (++ST(S_JUMPI) >= 14) ST(S_JUMP) = 0;
}

/* A4C9 */
static void mao2_phase2(Game *g)
{
    Boss *b = &g->boss;
    if (!(++ST(S_FRAME) & 0x1F)) mao2_regenerate(g);
    if (ST(S_JUMP))  { mao2_jump_step(g); mao2_draw(g); mao2_shots(g); return; }
    if (ST(S_THROW)) {                                          /* A5F7 */
        ST(S_POSE) = boss_img8(g, M2_STRIKE + ST(S_THROWI));
        ST(S_THROWI)++;
        if (ST(S_POSE) == 9) { ST(S_THROW) = 0; launch_shot1(g); }
        mao2_draw(g); mao2_shots(g); return;
    }
    if (ST(S_S1ON)) { mao2_draw(g); mao2_shots(g); return; }    /* A4EB */

    uint8_t hero = mao2_hero_col(g);
    ST(S_FACING) = (uint8_t)((uint8_t)b->col < hero ? 0xFF : 0);
    uint8_t d = (uint8_t)((ST(S_FACING) ? hero - (uint8_t)b->col : (uint8_t)b->col - hero) & 0xFE);
    if (d != 8) {
        int toward = d > 8;
        ST(S_POSE) = (uint8_t)(toward ? (ST(S_POSE) + 1) & 3 : (ST(S_POSE) - 1) & 3);
        int go_right = (toward == (ST(S_FACING) != 0));
        int blocked;
        if (!(ST(S_POSE) & 1)) (go_right ? step_right : step_left)(g);   /* even: 2 cells */
        blocked = (go_right ? step_right : step_left)(g);
        if (blocked) { ST(S_JUMPI) = 0; ST(S_JUMP) = 0xFF; }
        else { mao2_draw(g); mao2_shots(g); return; }
    }
    /* A5CD: at range (or just blocked) — the second consecutive frame may throw */
    uint8_t was = ST(S_ATRANGE);
    ST(S_ATRANGE) = 0xFF;
    if (was) {
        ST(S_POSE) &= (uint8_t)~1u;
        if (!(krn_random(g) & 0xF)) { ST(S_THROWI) = 0; ST(S_THROW) = 0xFF; }
    }
    mao2_draw(g); mao2_shots(g);
}

/* ABC4: 40 frames, ABF9's poses one per two frames, sound 0x23 every 8th. */
static void mao2_death(Game *g)
{
    uint8_t t = g->boss.death_cnt;
    if (boss_death_tick(g) >= 0x28) { boss_parts_begin(g); boss_parts_end(g); return; }
    if (!(t & 7)) g->sfx_request = 0x23;
    if (t < 0x14) ST(S_POSE) = boss_img8(g, M2_DEATH + (unsigned)(t / 2));
    ST(S_TYPEBITS) = 0;
    boss_parts_begin(g);
    mao2_draw(g);
    boss_parts_end(g);
}

/* 0xA2F2  Frame entry. */
void boss_mao2_entry(Game *g)
{
    Boss *b = &g->boss;
    if (!ST(S_INIT)) { ST(S_INIT) = 1; ST(S_FACING) = 0xFF; }
    uint8_t hit = boss_readback(g, NULL);                       /* A2F6 */
    boss_hit_flash(g, hit != 0);   /* A763: every part this frame carries hit bit 5 */

    if (hit) {                                                  /* A362 */
        unsigned d = (unsigned)damage_for_source(g, (uint8_t)(hit & 0x1F)) / 2;
        if ((hit & 0x1F) != 1) d /= 2;
        boss_damage(g, d);
        g->sfx_request = 0x39;
        if (b->hp == 0) { ST(S_S1ON) = 0; ST(S_S2ON) = 0; }     /* AB51 */
        else if (b->hp < 200) ST(S_PHASE2) = 0xFF;              /* A389 */
    }
    if (g->boss_cutscene) { mao2_death(g); return; }            /* A396 */
    /* A3A0..A3AF: nothing is drawn until [FF21] is non-zero -- the score's F1, which the port
     * follows with audio_clock_* (the shell starts it with MPA0's music).  Without a clock
     * (--map 0x1E, unit tests) the gate is open. */
    if (!ST(S_STARTED)) {
        boss_parts_begin(g); boss_parts_end(g);
        if (!g->score_clock || g->ff21) ST(S_STARTED) = 1;
        return;
    }

    boss_parts_begin(g);
    if (ST(S_PHASE2)) { mao2_phase2(g); return; }               /* A4C9 */

    if (!ST(S_TELE)) {                                          /* A3BF */
        if (ST(S_S1ON) || ST(S_S2ON)) { mao2_shots(g); return; }
        mao2_teleport(g);
        ST(S_STEP) = 0; ST(S_TELE) = 0xFF;
        ST(S_VARIANT) = (uint8_t)((krn_random(g) >> 7) & 1);   /* A3E7: rol al,1 / and al,1 */
    }
    ST(S_STEP)++;                                               /* A3F3 */
    if (ST(S_STEP) < 6 || (ST(S_STEP) >= 11 && ST(S_STEP) < 17)) {
        if (ST(S_STEP) & 1) { mao2_shots(g); return; }          /* not drawn */
        g->sfx_request = 0x3B;
        ST(S_TYPEBITS) = 0x60;
        if (ST(S_STEP) < 6) ST(S_POSE) = (uint8_t)(ST(S_VARIANT) * 10);
    } else if (ST(S_STEP) < 11) {                               /* A41C: solid */
        ST(S_POSE) = boss_img8(g, M2_STRIKE + (unsigned)(ST(S_VARIANT) * 5 + ST(S_STEP) - 6));
        ST(S_TYPEBITS) = 0;
        if (ST(S_POSE) == 9)  launch_shot1(g);
        if (ST(S_POSE) == 12) launch_shot2(g);
    } else { ST(S_TELE) = 0; mao2_shots(g); return; }           /* A467 */
    mao2_draw(g);
    mao2_shots(g);
}
