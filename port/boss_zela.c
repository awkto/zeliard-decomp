/* boss_zela.c — cavern 4 boss "Agar" (ZELA.BIN = ZELRES3[12]) and the cavern-7
 * mid-boss "Paguro" (ZEL2.BIN = ZELRES3[15]), which is the same code with
 * different numbers (src/ai/boss_zel2.c: 13 bytes apart).
 *
 *   ZELA [A002] = A5EE: col 0x30, row 0x0C, HP 500, EXP 1000, camera 12,
 *        knockback free, "Agar", gold 600; bolts cell 0x15/0x12, damage 80;
 *        magic 3 (source 4) counts x4, otherwise half damage.
 *   ZEL2 [A002] = A5DF: HP 600, EXP 3000, "Paguro", gold 1600; bolts cell
 *        0x05/0x04, damage 120; always half damage, hit sound 0x24.
 *   Contact A010 = 30 for every part.
 *
 * Image: 4 columns x 3 rows of 2x2 parts with the roles of the record fields
 * swapped — `type` is the POSE (frame table A030[0..4]) and `phase` is the
 * PART index 0..11, which doubles as the frame index; an attack patches two
 * parts to frames 0xC..0xF.  Poses come from A4EA[anim & 7].
 *
 * The hop sequence (A307) and the attack walk (A371) are only sketched in
 * src/ai/boss_zela.c; the shape below (2 rows up, a horizontal step, 2 rows
 * down, a 4-frame pause) follows that sketch.
 */
#include "boss.h"
#include <string.h>

enum { S_ANIM, S_FIRING, S_HOPPING, S_HOPDIR, S_PAUSE, S_HOPSTEP, S_PAUSECNT, S_BLOCKED };
#define ST(n) (g->boss.st[n])

static const uint8_t pose_of_anim[8] = { 1, 2, 3, 0, 3, 2, 1, 0 };         /* A4EA */

static int step_left(Game *g)  { if (g->boss.col <= 0x11) return 1; g->boss.col--; return 0; }
static int step_right(Game *g) { if (g->boss.col >= 0x32) return 1; g->boss.col++; return 0; }

/* 0xA4F2 / 0xA4E3  the bolt */
static void zela_fire(Game *g)
{
    Boss *b = &g->boss;
    int zel2 = (b->index == BOSS_ZEL2);
    /* the two 13-byte templates: ZELA A552/A55F (cells 0x15/0x14, damage 80),
     * ZEL2 A543/A550 (cells 0x05/0x04, damage 120).  src/ai/boss_zela.c calls
     * the right-hand cell 0x12; the image says 0x14. */
    unsigned tpl = ST(S_FIRING) == 1 ? (zel2 ? 0xA543u : 0xA552u)
                                     : (zel2 ? 0xA550u : 0xA55Fu);
    Shot s;
    boss_shot_template(g, tpl, &s);
    uint16_t col = (uint16_t)(ST(S_FIRING) == 1 ? b->col + 1 : b->col + 7);
    if (!ai_map_col_to_ring(g, col, &s.col)) {
        s.row = (uint8_t)((b->row + 3) & 0x3F);
        shot_spawn(g, &s);
    }
    ST(S_FIRING) = 0;
}

/* 0xA467  the 12 parts; `pose` is the record type, the part index the frame */
static void zela_draw(Game *g, uint8_t pose, const uint8_t frames[12])
{
    Boss *b = &g->boss;
    boss_parts_begin(g);
    for (int i = 0; i < 12; i++)
        boss_part(g, (uint16_t)(b->col + 2 * (i & 3)), (uint8_t)(b->row + 2 * (i >> 2)),
                  pose, frames[i]);
    boss_parts_end(g);
}

/* 0xA59A / 0xA58B  Death: 40 frames; 21 frames of animation with sound 0x28
 * every 4th frame, then pose 2. */
static int zela_death(Game *g, uint8_t *pose)
{
    uint8_t t = g->boss.death_cnt;
    if (boss_death_tick(g) >= 0x28) return 0;
    if (t < 21) { if (!(t & 3)) g->sfx_request = 0x28; *pose = pose_of_anim[(t * 2) & 7]; }
    else *pose = 2;
    return 1;
}

/* 0xA1B6  Frame entry (both overlays). */
void boss_zela_entry(Game *g)
{
    Boss *b = &g->boss;
    int zel2 = (b->index == BOSS_ZEL2);
    uint8_t hit = boss_readback(g, NULL);                                  /* A1C4: no weak points */

    if (hit) {                                                             /* A20F */
        unsigned d = (unsigned)damage_for_source(g, (uint8_t)(hit & 0x1F)) / 2u;
        if (!zel2 && (hit & 0x1F) == 4) { d *= 4; g->sfx_request = 0x24; }  /* magic 3 */
        else g->sfx_request = (uint8_t)(zel2 ? 0x24 : 0x25);
        boss_damage(g, d);                                                 /* A56C / A55D */
        if (g->boss_cutscene) { shots_clear(g); ST(S_FIRING) = 0; }
        if (b->col < boss_hero_col(g, 15)) { step_left(g); step_left(g); }  /* A23F */
        else                               { step_right(g); step_right(g); }
    }

    uint8_t pose;
    if (g->boss_cutscene) {
        if (!zela_death(g, &pose)) { boss_parts_begin(g); boss_parts_end(g); return; }
        uint8_t frames[12];
        for (int i = 0; i < 12; i++) frames[i] = (uint8_t)i;
        zela_draw(g, pose, frames);
        return;
    }

    if (ST(S_FIRING)) {                                                    /* A371: walk toward the hero at half speed */
        if (!(b->parity++ & 1)) {
            if (b->col > boss_hero_col(g, 18)) ST(S_BLOCKED) = (uint8_t)step_left(g);
            else                               ST(S_BLOCKED) = (uint8_t)step_right(g);
        }
    } else {
        if (!ST(S_HOPPING) && !(krn_random(g) & 0xF)) {                     /* A274 */
            ST(S_HOPPING) = ST(S_HOPDIR) = ST(S_PAUSE) = 0xFF;
            ST(S_HOPSTEP) = ST(S_PAUSECNT) = 0;
            if (b->col < boss_hero_col(g, 14)) ST(S_HOPDIR) = 0;            /* A2A3 */
        }
        ST(S_ANIM) = (uint8_t)((ST(S_ANIM) + 2) & 6);                       /* A2BE */
        if (ST(S_PAUSE)) {                                                  /* A2C8 */
            ST(S_PAUSECNT) = (uint8_t)((ST(S_PAUSECNT) + 1) & 3);
            if (!ST(S_PAUSECNT)) { ST(S_PAUSE) = 0; if (!(ST(S_HOPPING) & 0x80)) ST(S_HOPPING) = 0; }
        } else {                                                            /* A2F4: the hop sequence A307 */
            switch (ST(S_HOPSTEP)) {
            case 0: case 1: b->row = (uint8_t)((b->row - 1) & 0x3F); break;
            case 2: if (ST(S_HOPDIR)) { if (step_right(g)) ST(S_BLOCKED) = 0xFF; }
                    else              { if (step_left(g))  ST(S_BLOCKED) = 0xFF; }
                    break;
            case 3: case 4: b->row = (uint8_t)((b->row + 1) & 0x3F); break;
            default: ST(S_PAUSE) = 0x7F; ST(S_BLOCKED) = 0; ST(S_HOPPING) = 0; ST(S_HOPSTEP) = 0xFF; break;
            }
            ST(S_HOPSTEP)++;
        }
    }

    pose = pose_of_anim[ST(S_ANIM) & 7];
    uint8_t frames[12];
    for (int i = 0; i < 12; i++) frames[i] = (uint8_t)i;
    if (!ST(S_HOPPING)) {                                                   /* A3E3 */
        if (ST(S_FIRING) == 1)      { frames[1] = 0xE; frames[4] = 0xF; if (ST(S_ANIM) == 4) zela_fire(g); }
        else if (ST(S_FIRING) == 2) { frames[7] = 0xC; frames[10] = 0xD; if (ST(S_ANIM) == 0) zela_fire(g); }
        else if (!(krn_random(g) & 1)) {                                    /* A3FA */
            if (b->col >= boss_hero_col(g, 18)) { if (ST(S_ANIM) == 2) ST(S_FIRING) = 1; }
            else if (b->col + 7 >= boss_hero_col(g, 16) && ST(S_ANIM) == 6) ST(S_FIRING) = 2;
        }
    }
    zela_draw(g, pose, frames);                                             /* A467 */
}
