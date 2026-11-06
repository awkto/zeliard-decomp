/* combat.c — the sword, contact damage, knockback, enemy damage/death and the
 * hero's HP.  Port of src/fight.c 6412 / 6E3B / 6F07 / 751F / 75E2 / 7685 /
 * 9851 / 96C1 / 96D5 / 97B5 (docs/FIGHT.md §6). */
#include "enemy.h"
#include "gfx.h"
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------ hero damage */

/* 0x7685 */
void hero_damage(Game *g, unsigned dmg)
{
    g->hp = (uint16_t)(g->hp > dmg ? g->hp - dmg : 0);
}

/* 0x75E2  dmg = (dmg/2) >> ((shield+1)/2); the shield loses the same and
 * breaks at 0 (761A). */
void hero_damage_shielded(Game *g, unsigned dmg)
{
    if (!g->shield) { hero_damage(g, dmg); g->sfx_request = 9; return; }
    dmg = (dmg >> 1) >> ((g->shield + 1) >> 1);
    if (g->shield_hp <= dmg) {
        g->shield = 0; g->shield_hp = 0;
        g->hud_panels &= (uint8_t)~2;   /* 761F: [2004] style 0xFF greys bh 0xC5 w 0x18 */
        game_message_idx(g, MSG_SHIELD_BROKEN);
    } else g->shield_hp = (uint16_t)(g->shield_hp - dmg);
    hero_damage(g, dmg); g->sfx_request = 8;
}

/* 0x9715 */
void exp_add(Game *g, unsigned n)  { unsigned v = g->exp + n; g->exp = (uint16_t)(v > 0xFFFF ? 0xFFFF : v); }

/* The engine has *two* purse adders and they are not the same purse:
 *
 *   916B  add [0x86],ax / adc byte [0x85],0 / call [cs:0x2016]   -> GOLD  [85..87]
 *   917C  add [0x8b],ax / jnc / mov word [0x8b],0xffff
 *                       / call [cs:0x2014]                       -> ALMAS [8B]
 *
 * (`[cs:2016]` is `vid_num_gold`, `[cs:2014]` is `vid_num_almas` — see
 * docs/VIDEO_DRIVERS.md §1.)  Only the treasure box pays gold in a cavern
 * (8F4A/8F56/8F68/8F74 all `jmp 0x916b`); every coin (8FCC/8FD9/8FE2) and the
 * boss award (71F2) `call 0x917c` and pay **almas**, which is the currency the
 * bank exchanges for gold in town (docs/TOWN.md §"Balance"). */
void gold_add(Game *g, unsigned n) { g->gold += n; if (g->gold > 0xFFFFFF) g->gold = 0xFFFFFF; }
void almas_add(Game *g, unsigned n)
{
    unsigned v = (unsigned)g->almas + n;                                /* 917C */
    g->almas = (uint16_t)(v > 0xFFFF ? 0xFFFF : v);                     /* 9182: the carry saturates at FFFF */
}

/* 0x98FC  Death: 30 blinking frames, then the penalties of 99AD and the
 * hand-off to the town engine (99E0: cur_map = town_map, the hero reappears at
 * the town map's own start column and the Sage's "While you were unconscious"
 * text runs).  With no town hooked up the port restarts at the map entry. */
void hero_die(Game *g)
{
    g->deaths++;
    if (!g->jashiin_defeated) {                                         /* 99AD */
        unsigned bonus = (unsigned)(2 * g->level) >= 127 ? 0 : 127 - 2 * (unsigned)g->level;
        exp_add(g, bonus);                                              /* 99C6: exp += 127 - 2*level */
        g->gold = 0;                                                    /* 99C9: the 24-bit GOLD is lost */
        g->almas >>= 1;                                                 /* 99D4: ALMAS halved */
    } else g->town_map = 0x80;                                          /* 99B4: the ending goes to the castle */
    g->hp = g->max_hp;                                                  /* 99D8 */
    g->hero_dead = 0; g->hero_hidden = 0; g->death_anim = 0;
    g->cur_map = g->town_map ? g->town_map : 0x81;
    if (g->on_town) fprintf(stderr, "[death] hero died at frame %u (death %u); gold lost, almas halved\n", g->frame_no, g->deaths);
    if (g->on_town && g->on_town(g, g->cur_map & 0x7F, -1, 1)) return;  /* 99E0 */
    /* port-only fallback when no town is hooked up; centre it in 73E0's box */
    game_message(g, "Garland died.", 56 + (208 - 8 * (int)strlen("Garland died.")) / 2);
    game_place(g, g->entry_col, g->entry_row, g->entry_face);
}

/* ------------------------------------------------------- damage to enemies */

/* 0x9851  vec 28 */
uint8_t damage_for_source(const Game *g, uint8_t src)
{
    static const uint8_t sword_base[6] = {1, 2, 4, 8, 0x20, 0x7F};      /* 98B8 */
    static const uint8_t magic_base[7] = {2, 4, 8, 0x10, 0x20, 0x40, 0xFF};  /* 98BE */
    unsigned half = g->level >> 1;
    if (src == 0) return (uint8_t)(half + 1);                           /* stomp */
    if (src != 1) {
        if (src == 9) { unsigned v = ((unsigned)g->level + 1) * 4; return (uint8_t)(v > 255 ? 255 : v); }
        return (src >= 2 && src <= 8) ? magic_base[src - 2] : 0;
    }
    unsigned sw = g->sword ? g->sword : 1;
    if (sw > 6) sw = 6;
    unsigned d = sword_base[sw - 1] + half;
    if (d > 255) return 255;
    d *= (unsigned)g->attack_bonus + 1;
    if (d > 255) d = 255;
    if (g->attack_type == 2) { d *= 2; if (d > 255) d = 255; }
    return (uint8_t)d;
}

/* 0x96D5  vec 25.  No EXP, no drop. */
void enemy_killed(Game *g, MapObj *o)
{
    int i = (int)(o - g->obj);
    o->phase = 0; o->type |= 0x68; o->hit &= 0x80;
    if ((o->flags & 0x10) && !(o->type & 1) && i + 1 < g->nobj) {       /* tall: the lower record too */
        o->phase = 0x80;
        o[1].phase = 0; o[1].type |= 0x68; o[1].hit &= 0x80;
    }
    if ((uint8_t)((o->row - (g->scroll_row - 1)) & 0x3F) < 0x13) g->sfx_request = 7;   /* 96FD */
}

/* 0x96C1 */
void kill_with_exp(Game *g, MapObj *o)
{
    if (!(o->type & 0x10) && g->ai) exp_add(g, g->ai->exp[o->type & 0xF]);
    enemy_killed(g, o);
}

/* 0x97B5  vec 26.  Every AI calls this when it sees hit & 0x20. */
void enemy_take_damage(Game *g, MapObj *o)
{
    uint8_t d = damage_for_source(g, (uint8_t)(o->hit & 0x1F));
    if (o->hp > d) { o->hp = (uint8_t)(o->hp - d); g->sfx_request = 6; return; }
    int i = (int)(o - g->obj);
    uint8_t *f = (!(o->type & 1) && (o->flags & 0x10) && i + 1 < g->nobj) ? &o[1].flags : &o->flags;   /* 97CD */
    if (!(*f & 0xF)) {
        const uint8_t *tbl = ai_drop_list(g->ai, o->type & 7);          /* 97E2 */
        if (tbl) {
            int r = krn_random(g) & 3;                                  /* 97F2: the draw is always made (#62) */
            if (g->attack_type == 2) r = 0;                             /* 97FC: a down-thrust takes entry 0 */
            *f = (uint8_t)((*f & 0xF0) | (tbl[r] & 0xF));
        }
    }
    kill_with_exp(g, o);
}

/* ------------------------------------------------------------- the sword */

/* Blade shapes: the hit lists of sword.grp (ZELRES2[26]), one per section
 * (kernel 0x0BA0: sword 1-3 -> section 0, 4-5 -> 1, 6 -> 2), which the game
 * relocates to arena:B000.  6F69 `mov di,[es:bx+B002]` picks a list by the
 * word index below; each list is a run of byte steps added to SI (6F79),
 * starting 4 rows above the hero's top-left (6F21: -0x90; -0x6C crouched),
 * up to an FF.  Decoded here to (row, col) cells relative to the hero's
 * top-left (the crouch origin is one row lower).  The left-facing lists
 * (word index | 0x10) are their own data, NOT a mirror of the right ones:
 * e.g. up0 left is up0 right shifted one column, and the thrust left has
 * 10 cells to the right one's 11 (an oracle run hit a frog behind the hero
 * with the mirrored up0, left_walk f74). */
typedef struct { int8_t dr, dc; } Cellofs;
#define SHAPE_MAX 31
#define END {127, 127}
static const Cellofs SWORD_SHAPE[3][12][SHAPE_MAX] = {
  {   /* section 0 (sword 1-3) */
    /* R slash0 @1A6 */ {{-2,-2},{-2,-1},{-1,-2},{-1,-1},{-1,0},{0,-2},{0,-1},{0,0},{0,1},{1,-2},{1,-1},{1,0},{1,1},END},
    /* R slash1 @1B4 */ {{0,-1},{0,0},{0,1},{0,2},{0,3},{0,4},{1,-1},{1,0},{1,1},{1,2},{1,3},{1,4},END},
    /* R slash2 @1C1 */ {{0,1},{0,2},{0,3},{0,4},{1,1},{1,2},{1,3},{1,4},END},
    /* R up0    @1CA */ {{-2,-1},{-2,0},{-2,1},{-1,-2},{-1,-1},{-1,0},{-1,1},{0,-2},{0,-1},{0,0},{0,1},{0,2},END},
    /* R up1    @1D7 */ {{-2,1},{-2,2},{-2,3},{-1,1},{-1,2},{-1,3},{-1,4},{0,1},{0,2},{0,3},{0,4},{1,1},{1,2},{1,3},{1,4},{2,1},{2,2},{2,3},{2,4},{3,2},{3,3},END},
    /* R thrust @1ED */ {{0,1},{0,2},{1,0},{1,1},{1,2},{2,0},{2,1},{2,2},{3,0},{3,1},{3,2},END},
    /* L slash0 @1F9 */ {{-2,2},{-2,3},{-1,1},{-1,2},{-1,3},{0,0},{0,1},{0,2},{0,3},{1,0},{1,1},{1,2},{1,3},END},
    /* L slash1 @207 */ {{0,-3},{0,-2},{0,-1},{0,0},{0,1},{0,2},{1,-3},{1,-2},{1,-1},{1,0},{1,1},{1,2},END},
    /* L slash2 @214 */ {{0,-3},{0,-2},{0,-1},{0,0},{1,-3},{1,-2},{1,-1},{1,0},END},
    /* L up0    @21D */ {{-2,0},{-2,1},{-2,2},{-1,-1},{-1,0},{-1,1},{-1,2},{0,-1},{0,0},{0,1},{0,2},{0,3},END},
    /* L up1    @22A */ {{-2,-2},{-2,-1},{-2,0},{-1,-3},{-1,-2},{-1,-1},{-1,0},{0,-3},{0,-2},{0,-1},{0,0},{1,-3},{1,-2},{1,-1},{1,0},{2,-3},{2,-2},{2,-1},{2,0},{3,-2},{3,-1},END},
    /* L thrust @240 */ {{0,-1},{0,0},{1,-1},{1,0},{2,-1},{2,0},{2,1},{3,-1},{3,0},{3,1},END},
  },
  {   /* section 1 (sword 4-5) */
    /* R slash0 @1A6 */ {{-3,-2},{-3,-1},{-2,-3},{-2,-2},{-2,-1},{-1,-3},{-1,-2},{-1,-1},{-1,0},{0,-3},{0,-2},{0,-1},{0,0},{1,-2},{1,-1},{1,0},{1,1},END},
    /* R slash1 @1B8 */ {{-1,-2},{-1,-1},{-1,0},{-1,1},{0,-2},{0,-1},{0,0},{0,1},{0,2},{0,3},{0,4},{0,5},{1,-1},{1,0},{1,1},{1,2},{1,3},{1,4},{1,5},END},
    /* R slash2 @1CC */ {{0,1},{0,2},{0,3},{0,4},{0,5},{1,1},{1,2},{1,3},{1,4},{1,5},END},
    /* R up0    @1D7 */ {{-3,-2},{-3,-1},{-3,0},{-3,1},{-2,-2},{-2,-1},{-2,0},{-2,1},{-1,-3},{-1,-2},{-1,-1},{-1,0},{-1,1},{0,-3},{0,-2},{0,-1},{0,0},{0,1},{0,2},END},
    /* R up1    @1EB */ {{-2,2},{-2,3},{-2,4},{-1,1},{-1,2},{-1,3},{-1,4},{0,0},{0,1},{0,2},{0,3},{0,4},{1,0},{1,1},{1,2},{1,3},{1,4},{2,0},{2,1},{2,2},{2,3},{2,4},{3,1},{3,2},{3,3},END},
    /* R thrust @205 */ {{0,0},{0,1},{0,2},{1,0},{1,1},{1,2},{2,0},{2,1},{2,2},{3,0},{3,1},{3,2},{4,0},{4,1},END},
    /* L slash0 @214 */ {{-3,2},{-3,3},{-2,2},{-2,3},{-2,4},{-1,1},{-1,2},{-1,3},{-1,4},{0,1},{0,2},{0,3},{0,4},{1,0},{1,1},{1,2},{1,3},END},
    /* L slash1 @226 */ {{-1,0},{-1,1},{-1,2},{-1,3},{0,-4},{0,-3},{0,-2},{0,-1},{0,0},{0,1},{0,2},{0,3},{1,-4},{1,-3},{1,-2},{1,-1},{1,0},{1,1},{1,2},END},
    /* L slash2 @23A */ {{0,-4},{0,-3},{0,-2},{0,-1},{0,0},{1,-4},{1,-3},{1,-2},{1,-1},{1,0},END},
    /* L up0    @245 */ {{-3,0},{-3,1},{-3,2},{-3,3},{-2,0},{-2,1},{-2,2},{-2,3},{-1,-1},{-1,0},{-1,1},{-1,2},{-1,3},{-1,4},{0,-1},{0,0},{0,1},{0,2},{0,3},{0,4},END},
    /* L up1    @25A */ {{-2,-3},{-2,-2},{-2,-1},{-1,-3},{-1,-2},{-1,-1},{0,-3},{0,-2},{0,-1},{0,0},{1,-3},{1,-2},{1,-1},{1,0},{1,1},{1,2},{2,-3},{2,-2},{2,-1},{2,0},{2,1},{3,-3},{3,-2},{3,-1},END},
    /* L thrust @273 */ {{0,-1},{0,0},{0,1},{1,-1},{1,0},{1,1},{2,-1},{2,0},{2,1},{3,-1},{3,0},{3,1},{4,0},{4,1},END},
  },
  {   /* section 2 (sword 6) */
    /* R slash0 @1A6 */ {{-3,-2},{-3,-1},{-2,-3},{-2,-2},{-2,-1},{-1,-3},{-1,-2},{-1,-1},{-1,0},{0,-3},{0,-2},{0,-1},{0,0},{1,-1},{1,0},END},
    /* R slash1 @1B6 */ {{-1,-2},{-1,-1},{-1,0},{0,-2},{0,-1},{0,0},{0,1},{0,2},{0,3},{0,4},{0,5},{1,-1},{1,0},{1,1},{1,2},{1,3},{1,4},{1,5},END},
    /* R slash2 @1C9 */ {{0,1},{0,2},{0,3},{0,4},{0,5},{1,1},{1,2},{1,3},{1,4},{1,5},END},
    /* R up0    @1D4 */ {{-4,0},{-4,1},{-3,-3},{-3,-2},{-3,-1},{-3,0},{-2,-4},{-2,-3},{-2,-2},{-2,-1},{-2,0},{-1,-4},{-1,-3},{-1,-2},{-1,-1},{-1,0},{-1,1},{0,-4},{0,-3},{0,-2},{0,-1},{0,0},{0,1},END},
    /* R up1    @1EC */ {{-3,1},{-3,2},{-3,3},{-2,1},{-2,2},{-2,3},{-2,4},{-1,1},{-1,2},{-1,3},{-1,4},{0,1},{0,2},{0,3},{0,4},{1,1},{1,2},{1,3},{1,4},{1,5},{2,0},{2,1},{2,2},{2,3},{2,4},{2,5},{3,2},{3,3},{3,4},END},
    /* R thrust @20A */ {{1,0},{1,1},{1,2},{2,0},{2,1},{2,2},{3,0},{3,1},{3,2},{4,0},{4,1},{5,0},{5,1},END},
    /* L slash0 @218 */ {{-3,2},{-3,3},{-2,2},{-2,3},{-2,4},{-1,1},{-1,2},{-1,3},{-1,4},{0,1},{0,2},{0,3},{0,4},{1,1},{1,2},END},
    /* L slash1 @228 */ {{-1,1},{-1,2},{-1,3},{0,-4},{0,-3},{0,-2},{0,-1},{0,0},{0,1},{0,2},{0,3},{1,-4},{1,-3},{1,-2},{1,-1},{1,0},{1,1},{1,2},END},
    /* L slash2 @23B */ {{0,-4},{0,-3},{0,-2},{0,-1},{0,0},{1,-4},{1,-3},{1,-2},{1,-1},{1,0},END},
    /* L up0    @246 */ {{-4,0},{-4,1},{-3,0},{-3,1},{-3,2},{-3,3},{-2,0},{-2,1},{-2,2},{-2,3},{-2,4},{-1,-1},{-1,0},{-1,1},{-1,2},{-1,3},{-1,4},{0,-1},{0,0},{0,1},{0,2},{0,3},{0,4},END},
    /* L up1    @25E */ {{-3,-2},{-3,-1},{-3,0},{-2,-3},{-2,-2},{-2,-1},{-2,0},{-1,-3},{-1,-2},{-1,-1},{-1,0},{0,-3},{0,-2},{0,-1},{0,0},{1,-4},{1,-3},{1,-2},{1,-1},{1,0},{1,1},{2,-4},{2,-3},{2,-2},{2,-1},{2,0},{2,1},{3,-3},{3,-2},{3,-1},END},
    /* L thrust @27D */ {{1,-1},{1,0},{1,1},{2,-1},{2,0},{2,1},{3,-1},{3,0},{3,1},{4,0},{4,1},{5,0},{5,1},END},
  },
};
#undef END

/* 6F33..6F5E: word index = ((FF46 | facing<<4) + {0 slash, 6 upward}) & 0xFE,
 * a fixed facing<<4 + 0x0A for the thrust.  Pointers 6/7 (and 14/15) are
 * unused (0000 / FFFF); a slash applies with FF46 = 1, 3, 5 (lists 0..2), an
 * upward slash with 1, 3 (lists 3, 4; 5 would reach the thrust). */
static const Cellofs *sword_shape(const Game *g)
{
    unsigned bl = (g->hero_flags & FACE_LEFT) ? 0x10 : 0;              /* 6F33 */
    unsigned idx;
    if (g->attack_type >= 2) idx = (bl + 0x0A) & 0xFE;                  /* 6F51 */
    else idx = (((unsigned)g->attack_var | bl) + (g->attack_type ? 6u : 0u)) & 0xFE;   /* 6F57 */
    idx = (idx >> 1) & 0x0F;
    if ((idx & 7) > 5) return NULL;
    int sw = g->sword ? g->sword : 1; if (sw > 6) sw = 6;
    return SWORD_SHAPE[SWORD_SECTION[sw - 1]][(idx & 7) + (idx & 8 ? 6 : 0)];
}

/* The *renderer* runs the swing: gfmcga 3E34 (vector [3006]) increments
 * `[FF46]` (3E45), ends the swing at a count that depends on the kind --
 * 3EAC `cmp [FF46],7` for the slash, 3E81 / 3E54 `cmp [FF46],5` for the
 * upward slash and the down-thrust -- and draws blade frame `[FF46] - 1`.
 * fight.bin calls it **twice a game frame**: at 7047 in the first half and at
 * 7147 in the second, right after 6F07 has applied the blade.  So a slash
 * applies with [FF46] = 1, 3, 5 (shapes 0, 1, 2) and lasts three game frames
 * (the next frame's 7047 reaches 7 and ends it); an upward slash applies with
 * 1, 3 (two frames); a held down-thrust is re-armed to 2 by 6E61 every frame
 * and runs out one frame after it is let go (2 -> 4 -> 5).  The boundary sees
 * 2, 4, 6 -- confirmed against the DOSBox oracle (tools/tracediff.py).
 * `attack_var` is `[FF46]` itself. */

/* 0x3E34  [3006]: one swing step (twice per frame, 7047 and 7147) */
void sword_advance(Game *g)
{
    if (!g->attacking) return;
    g->attack_var++;                                                    /* 3E45 */
    if (g->attack_var >= (g->attack_type == 0 ? 7 : 5)) {               /* 3EAC / 3E81 / 3E54 */
        g->attacking = 0; g->attack_var = 0;                            /* 3F1A */
    }
}

/* 0x6E3B  Sword input, once per frame before frame(). */
void sword_input(Game *g)
{
    if (!g->sword) return;
    if ((g->buttons & 1) && g->vstate != V_GROUND && !g->conveyor && (g->dirs & DIR_DOWN)) {   /* 6E45 */
        g->attack_type = 2; g->attack_var = 2;
        if (!g->thrust_latch) { g->thrust_latch = 0xFF; g->sfx_request = 4; }
        g->btn1_edge = g->btn2_edge = 0; g->attacking = 0xFF;           /* 6EF7: both edges */
        return;
    }
    g->thrust_latch = 0;
    if (!g->btn1_edge || g->attacking || g->casting) return;            /* 6E81..6E96: [FF3C] too */
    g->attack_type = 0; g->attack_var = 0;
    if (!g->boss_map) {                                                 /* 6EA0: hittable sprite overhead? */
        int base = game_ring_add(game_hero_cell(g), -(4 * RING_W + 3)); /* rows -4..-1, cols -3..+4 */
        int found = 0;
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 8; c++) {
                uint8_t v = g->ring[game_ring_add(base, r * RING_W + c)];
                if (!(v & 0x80)) continue;
                int i = v & 0x7F;                                       /* 6EB9 / 6EBD: not immune/harmless, */
                if (i < g->nobj && !(g->obj[i].type & 0x60) && !(g->obj[i].flags & 0x10)) found = 1;   /* not a tall body's 2nd record; items count */
            }
        }
        if (found) g->attack_type = 1;                                  /* 6EDC: upward slash */
    }
    if (!g->attack_type && (g->dirs & DIR_UP)) g->attack_type = 1;      /* 6ED6 */
    g->sfx_request = 3;
    g->btn1_edge = g->btn2_edge = 0; g->attacking = 0xFF;               /* 6EF7 / 6EFC / 6F01 */
}

/* 0x6F07  Apply the blade, once per rendered frame while attacking.  Every
 * sprite on a shape cell that is not sword-immune (type & 0x20) and not
 * already stunned (hit & 0x20) gets hit source 1, pending (6F8D). */
void sword_apply(Game *g)
{
    if (!g->attacking) return;
    if (g->boss_map && g->boss_cutscene) return;
    const Cellofs *sh = sword_shape(g);
    if (!sh) return;
    int base = game_hero_cell(g) + (g->crouching ? RING_W : 0);         /* 6F21: origin one row lower when crouched */
    for (int i = 0; sh[i].dr != 127; i++) {                             /* 6F72: FF ends the list */
        uint8_t v = g->ring[game_ring_add(base, sh[i].dr * RING_W + sh[i].dc)];
        if (!(v & 0x80)) continue;
        int oi = v & 0x7F;
        if (oi >= g->nobj) continue;
        MapObj *o = &g->obj[oi];
        if (o->type & 0x20) continue;
        if (o->hit & 0x20) continue;
        o->hit = (uint8_t)((o->hit & 0xE0) | 0x40 | 1);
    }
}

/* ---------------------------------------------------------- contact damage */

/* 0x763E/0x7651/0x765E: scan `rows` cells downward (763E: 3 rows, 7651: 2
 * when crouched; the last row of either is 765E).  6DCB gives CF=1 for a
 * plain cell; a sprite whose type has bit 6 (harmless) is skipped (7643/7656/
 * 766C) and the scan goes on, any other sprite adds the AI's contact-damage
 * entry [A010+type&0F] (7675) and **ends the column** (stc; ret): one damage
 * per column, however many sprites it holds.  `p` is left on the last row
 * scanned, as SI is, so 765E can continue below it. */
static int contact_scan(Game *g, int *p, int rows)
{
    for (int r = 0; r < rows; r++) {
        if (r) *p = game_ring_add(*p, RING_W);                          /* 764B / 765E: add si,24 + 6D82 wrap */
        uint8_t v = g->ring[*p];
        if (!(v & 0x80)) continue;                                      /* 6DCB: CF=1, plain cell */
        int i = v & 0x7F;
        /* 7643..: the marker indexes the record table whatever the list length now is.  A
         * boss's stale marker (its readback restored the cell at the shifted ring column, and a
         * shorter part list followed) still reads its old record (jashiin_damage f606) */
        if (i >= MAX_OBJS) continue;
        MapObj *o = &g->obj[i];
        if (o->type & 0x40) continue;                                   /* harmless */
        if (g->ai) g->contact_damage = (uint16_t)(g->contact_damage + g->ai->contact[o->type & 0xF]);
        return 1;                                                       /* 7683: stc */
    }
    return 0;
}

/* 0x751F  Four columns (ring col -1 .. +2) x 3 rows (-1..+1; 0..+1 crouched).
 * The two middle columns, under the hero's body, look one row further down
 * when the column scan finds nothing: 7566/757E `jc ...; call 765E` (his
 * feet).  The outer two do not (7551, 7591: `call bx` only). */
void hero_enemy_contact(Game *g)
{
    if (g->boss_map && g->boss_cutscene) return;
    g->contact_damage = 0;
    int rows = 3;
    int s = game_hero_cell(g) - 1;
    if (g->crouching) rows = 2; else s = game_ring_add(s, -RING_W);     /* 7545 */
    for (int i = 0; i < 4; i++) {
        int p = game_ring_add(s, i);
        int hit = contact_scan(g, &p, rows);
        if (!hit && (i == 1 || i == 2)) {                               /* 7568 / 7580: call 765E */
            p = game_ring_add(p, RING_W);
            hit = contact_scan(g, &p, 1);
        }
        g->hit_side[i] = (uint8_t)(hit ? 0xFF : 0);
        if (!g->hit_side[i] || g->hero_dead) continue;
        int facing_left = (g->hero_flags & FACE_LEFT) != 0;
        int shielded = i < 2 ? facing_left : !facing_left;              /* 75BA / 75CE */
        if (shielded) hero_damage_shielded(g, g->contact_damage);
        else { hero_damage(g, g->contact_damage); g->sfx_request = 9; }
    }
    g->hero_hit = g->hero_hit_flash =
        (uint8_t)(g->hit_side[0] | g->hit_side[1] | g->hit_side[2] | g->hit_side[3]);
}

/* 0x6412  Knockback: 2 cells away from the hit side (walls stop it), then one
 * row of fall if there is no floor. */
void hero_knockback(Game *g)
{
    if (!g->hero_hit) return;
    int l = g->hit_side[0] | g->hit_side[1], r = g->hit_side[2] | g->hit_side[3];
    int left;
    if (l && r) left = (g->hero_flags & FACE_LEFT) != 0;                /* 642F */
    else left = !l;                                                     /* hit on the left -> pushed right */
    if (g->boss_knock_left) left = 1;                                   /* 9F01: [[A002]+8] boss rooms */
    if (g->on_ladder) {                                                 /* 6440 / 6463 */
        g->hero_flags = (uint8_t)((g->hero_flags & ~3) | (left ? FACE_LEFT : 0));
        g->vstate = V_FALL; g->btn1_edge = 0;
    }
    game_push_hero(g, left);
    game_push_hero(g, left);
    if (g->on_ladder) { g->on_ladder = V_KNOCK; g->vstate = V_GROUND; } /* 6481 */
    if (g->on_updraft || (g->vstate & 0x80)) return;
    game_knock_fall(g);                                                 /* 64A2 */
}
