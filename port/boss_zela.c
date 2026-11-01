/* boss_zela.c — cavern 4 boss "Agar" (ZELA.BIN = ZELRES3[12]) and the cavern-7
 * mid-boss "Paguro" (ZEL2.BIN = ZELRES3[15]): the same code, ZEL2 15 bytes
 * shorter in its hit handler (no magic-3 branch) and so shifted down by 15
 * from A228 on.  Addresses below are ZELA's (ZEL2 = ZELA - 0xF past A228).
 *
 *   ZELA [A002] = A5EE: col 0x30, row 0x0C, HP 500, EXP 1000, camera 12,
 *        knockback free, "Agar", almas 600; bolts A552/A55F (cells 0x15/0x14,
 *        damage 80); magic 3 (source 4) counts x2 (half, then x4), sound 0x24,
 *        otherwise half damage, sound 0x25.
 *   ZEL2 [A002] = A5DF: HP 600, EXP 3000, "Paguro", almas 1600; bolts
 *        A543/A550 (cells 0x05/0x04, damage 120); always half damage, 0x24.
 *   Contact A010 = 30 for every part.  No `or [si+5],0x20`: hit stays 0.
 *
 * Image (A3C8 / A467): 12 words {pose, frame} at A610, frame = the index;
 * the emitter walks them COLUMN-major -- 4 columns (`loop A479`, col += 2)
 * of 3 rows (`loop A493`, row += 2) -- so entry i is part (2*(i/3), 2*(i%3)).
 * (The port had them row-major, i -> (2*(i&3), 2*(i>>2)): the same transposed
 * buffer TORI A4C1 and TAKO A401 had.)  `type` = the pose A4EA[anim],
 * `phase` = the frame.  An attack patches the middle row's frames: A453 parts
 * 1/4 -> 0xE/0xF (left), A431 parts 7/10 -> 0xC/0xD (right).
 *
 * State (ZELA): A5EE col, A5F0 row, A5F1 HP, A603 anim, A604 firing, A605
 * hopping, A606 hop direction (FF left, 0 right), A607 pause, A608 hop step,
 * A609 pause count, A60B anim divider (image value 2), A60E death counter,
 * A60F blocked latch.
 */
#include "boss.h"
#include <string.h>

enum { S_ANIM, S_FIRING, S_HOPPING, S_HOPDIR, S_PAUSE, S_HOPSTEP, S_PAUSECNT, S_BLOCKED,
       S_DIV, S_INIT };
#define ST(n) (g->boss.st[n])

/* A23F / A2A3 / A350 / A38F / A403: the hero's map column, scroll + n
 * wrapped once by the map width [C002] (`sub ax,[C002]` / `xchg`). */
static uint16_t hero_col(const Game *g, unsigned n)
{
    unsigned c = (unsigned)g->scroll_col + n;
    if (g->map && c >= (unsigned)g->map->width) c -= (unsigned)g->map->width;
    return (uint16_t)c;
}

/* A543 / A534: refused (CF) only AT the limit (`cmp byte [A5EE],n; jnz`) */
static int step_left(Game *g)  { if ((uint8_t)g->boss.col == 0x11) return 1; g->boss.col--; return 0; }
static int step_right(Game *g) { if ((uint8_t)g->boss.col == 0x32) return 1; g->boss.col++; return 0; }

/* A56C (ZEL2 A55D): HP -= d (floored at 0), the bar; at 0 -- on every hit
 * that finds it at 0, so a blow during the death restarts it -- FF2E = FF,
 * the death counter A60E = 0, firing off and `jmp [603C]` (vec 30, every
 * projectile dropped). */
static void zela_damage(Game *g, unsigned d)
{
    Boss *b = &g->boss;
    b->hp = (uint16_t)(b->hp > d ? b->hp - d : 0);
    if (b->hp) return;
    g->boss_cutscene = 0xFF;
    b->death_cnt = 0;
    ST(S_FIRING) = 0;
    shots_clear(g);
}

/* A4F2 (ZEL2 A4E3)  the bolt: both templates get row+3 and the ring columns
 * of col+1 / col+7 from vec 27 -- whatever BL it returns, CF is not looked at
 * (#84) -- and template (firing-1)*13 goes to vec 29. */
static void zela_fire(Game *g)
{
    Boss *b = &g->boss;
    int zel2 = (b->index == BOSS_ZEL2);
    unsigned tpl = (zel2 ? 0xA543u : 0xA552u) + (ST(S_FIRING) == 1 ? 0u : 13u);
    Shot s;
    boss_shot_template(g, tpl, &s);
    s.row = (uint8_t)((b->row + 3) & 0x3F);
    boss_ring_col(g, (uint16_t)(ST(S_FIRING) == 1 ? b->col + 1 : b->col + 7), &s.col);
    shot_spawn(g, &s);
    ST(S_FIRING) = 0;
}

/* A3C8 / A5C6: the 12 {pose, frame} words */
static void zela_buffer(Game *g, uint8_t frames[12], uint8_t *pose)
{
    unsigned tbl = g->boss.index == BOSS_ZEL2 ? 0xA4DBu : 0xA4EAu;        /* A3CE xlat */
    *pose = boss_img8(g, tbl + (ST(S_ANIM) & 7));
    for (int i = 0; i < 12; i++) frames[i] = (uint8_t)i;
}

/* A467  the emitter: 4 columns of 3 rows; a column off the ring is skipped
 * whole (A485).  The column is not wrapped (`inc ax` x2 at A4E0). */
static void zela_draw(Game *g, uint8_t pose, const uint8_t frames[12])
{
    Boss *b = &g->boss;
    boss_parts_begin(g);
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 3; r++)
            boss_part(g, (uint16_t)(b->col + 2 * c), (uint8_t)(b->row + 2 * r),
                      pose, frames[c * 3 + r]);
    boss_parts_end(g);
}

/* A59A  Death: A60E counts to 0x28 (then FF30, nothing drawn: A5E8); while
 * below 0x15 sound 0x28 when the new count is a multiple of 4 and anim+1,
 * after that anim = 2 (A5E1). */
static void zela_death(Game *g)
{
    uint8_t n = (uint8_t)(boss_death_tick(g) + 1);
    if (n > 0x28) return;                                                  /* A5E8 */
    if (n >= 0x15) ST(S_ANIM) = 2;                                         /* A5E1 */
    else {
        if (!(n & 3)) g->sfx_request = 0x28;                               /* A5B1 */
        ST(S_ANIM) = (uint8_t)((ST(S_ANIM) + 1) & 7);                      /* A5BD */
    }
    uint8_t frames[12], pose;
    zela_buffer(g, frames, &pose);
    zela_draw(g, pose, frames);
}

/* A348: the hop's horizontal step toward A606, unless blocked or Agar is on
 * column scroll+12 (then the step only changed the row) */
static void hop_check(Game *g)
{
    if (ST(S_BLOCKED)) return;
    if (g->boss.col == hero_col(g, 12)) return;                            /* A350 */
    if (ST(S_HOPDIR)) { if (step_left(g)) ST(S_BLOCKED) = 0xFF; }          /* A3AB */
    else              { if (step_right(g)) ST(S_BLOCKED) = 0xFF; }         /* A3BE */
}

static void row_up(Game *g)   { g->boss.row = (uint8_t)((g->boss.row - 1) & 0x3F); }   /* A334 */
static void row_down(Game *g) { g->boss.row = (uint8_t)((g->boss.row + 1) & 0x3F); }   /* A32A */

/* A2F4: the hop table A307 {A334, A33E x3, A348 x2, A343 x3, A31B} */
static void hop_step(Game *g)
{
    switch (ST(S_HOPSTEP)++) {
    case 0: row_up(g); break;
    case 1: case 2: case 3: row_up(g); hop_check(g); break;                /* A33E */
    case 4: case 5: hop_check(g); break;                                   /* A348 */
    case 6: case 7: case 8: row_down(g); hop_check(g); break;              /* A343 */
    case 9: ST(S_HOPPING) = 0x7F; ST(S_PAUSE) = 0x7F; ST(S_BLOCKED) = 0;   /* A31B */
            row_down(g); break;
    default: break;            /* not reached: A608 is reset whenever a hop starts */
    }
}

/* A1B6  Frame entry (both overlays). */
void boss_zela_entry(Game *g)
{
    Boss *b = &g->boss;
    int zel2 = (b->index == BOSS_ZEL2);
    if (!ST(S_INIT)) { ST(S_INIT) = 1; ST(S_DIV) = 2; }                    /* A60B's image value */
    uint8_t hit = boss_readback(g, NULL);                                  /* A1C4: no weak points */

    if (hit) {                                                             /* A20F */
        uint8_t src = (uint8_t)(hit & 0x1F);
        unsigned d = (unsigned)(damage_for_source(g, src) >> 1);           /* A224 shr bl,1 */
        if (!zel2 && src == 4) { d *= 4; g->sfx_request = 0x24; }          /* A228: magic 3 */
        else g->sfx_request = (uint8_t)(zel2 ? 0x24 : 0x25);
        zela_damage(g, d);
        if (b->col < hero_col(g, 15)) { step_left(g); step_left(g); }     /* A23F */
        else                          { step_right(g); step_right(g); }
    }

    int walk = 1;
    if (!ST(S_FIRING)) {
        if (ST(S_HOPPING)) walk = 0;                                       /* A26D */
        else if (!(krn_random(g) & 0xF) && !g->boss_cutscene) {            /* A274 / A280 */
            ST(S_HOPPING) = ST(S_PAUSE) = ST(S_HOPDIR) = 0xFF;             /* A28A */
            ST(S_HOPSTEP) = ST(S_PAUSECNT) = 0;
            if (b->col < hero_col(g, 14)) ST(S_HOPDIR) = 0;                /* A2A3 */
            walk = 0;
        }
    }

    if (walk) {                                                            /* A371 */
        if (g->boss_cutscene) { zela_death(g); return; }                   /* A378 */
        if (--ST(S_DIV) == 0) { ST(S_DIV) = 2; ST(S_ANIM) = (uint8_t)((ST(S_ANIM) + 1) & 7); }
        if (hero_col(g, 18) < b->col) {                                    /* A38F..A3A2 */
            if (ST(S_ANIM) == 0 && step_left(g)) ST(S_BLOCKED) = 0xFF;     /* A3A4 */
        } else if (ST(S_ANIM) == 4 && step_right(g)) ST(S_BLOCKED) = 0xFF; /* A3B7 */
    } else {                                                               /* A2BE */
        ST(S_ANIM) = (uint8_t)((ST(S_ANIM) + 2) & 6);
        if (ST(S_PAUSE)) {                                                 /* A2C8 */
            ST(S_PAUSECNT) = (uint8_t)((ST(S_PAUSECNT) + 1) & 3);
            if (!ST(S_PAUSECNT)) { ST(S_PAUSE) = 0; if (!(ST(S_HOPPING) & 0x80)) ST(S_HOPPING) = 0; }
        } else hop_step(g);                                                /* A2F4 */
    }

    uint8_t frames[12], pose;
    zela_buffer(g, frames, &pose);                                         /* A3C8 */
    if (!ST(S_HOPPING)) {                                                  /* A3E3 */
        int f = ST(S_FIRING);
        if (!f && !(krn_random(g) & 1)) {                                  /* A3FA */
            uint16_t hc = hero_col(g, 18);                                 /* A403 */
            if (b->col >= hc) { if (ST(S_ANIM) == 2) f = ST(S_FIRING) = 1; }          /* A447 */
            else if (b->col + 7 < (uint16_t)(hc - 2) && ST(S_ANIM) == 6) f = ST(S_FIRING) = 2;   /* A419 */
        }
        if (f == 1)      { frames[1] = 0xE; frames[4] = 0xF;  if (ST(S_ANIM) == 4) zela_fire(g); }  /* A453 */
        else if (f == 2) { frames[7] = 0xC; frames[10] = 0xD; if (ST(S_ANIM) == 0) zela_fire(g); }  /* A431 */
    }
    zela_draw(g, pose, frames);                                            /* A467 */
}
