/* combat.c — the sword, contact damage, knockback, enemy damage/death and the
 * hero's HP.  Port of src/fight.c 6412 / 6E3B / 6F07 / 751F / 75E2 / 7685 /
 * 9851 / 96C1 / 96D5 / 97B5 (docs/FIGHT.md §6). */
#include "enemy.h"
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
        game_message(g, fight_message(MSG_SHIELD_BROKEN));
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
    game_message(g, "Garland died.");
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
            int r = g->attack_type == 2 ? 0 : (krn_random(g) & 3);      /* 97F2: down-thrust always entry 0 */
            *f = (uint8_t)((*f & 0xF0) | (tbl[r] & 0xF));
        }
    }
    kill_with_exp(g, o);
}

/* ------------------------------------------------------------- the sword */

/* Blade shapes decoded from sword.grp section 0 (docs/FIGHT.md §6), as (row,
 * col) cells relative to the hero's top-left, facing right.  The left-facing
 * shapes are the mirror about the body column (+1), i.e. col' = 2 - col. */
typedef struct { int8_t dr, dc; } Cellofs;
static const Cellofs SLASH0[] = {                                       /* frames 0-1: wind-up behind */
    {-2,-2},{-2,-1},{-1,-2},{-1,-1},{-1,0},{0,-2},{0,-1},{0,0},{0,1},{1,-2},{1,-1},{1,0},{1,1}};
static const Cellofs SLASH1[] = {                                       /* frames 2-3: rows 0..1 x cols -1..+4 */
    {0,-1},{0,0},{0,1},{0,2},{0,3},{0,4},{1,-1},{1,0},{1,1},{1,2},{1,3},{1,4}};
static const Cellofs SLASH2[] = {                                       /* frame 4: rows 0..1 x cols +1..+4 */
    {0,1},{0,2},{0,3},{0,4},{1,1},{1,2},{1,3},{1,4}};
static const Cellofs UP0[] = {                                          /* upward slash frames 0-1 */
    {-2,-1},{-2,0},{-2,1},{-1,-2},{-1,-1},{-1,0},{-1,1},{0,-2},{0,-1},{0,0},{0,1},{0,2}};
static const Cellofs UP1[] = {                                          /* upward slash frames 2-3 */
    {-2,1},{-2,2},{-2,3},{-1,1},{-1,2},{-1,3},{-1,4},{0,1},{0,2},{0,3},{0,4},
    {1,1},{1,2},{1,3},{1,4},{2,1},{2,2},{2,3},{2,4},{3,2},{3,3}};
static const Cellofs THRUST[] = {                                       /* down-thrust, 1 row below the feet */
    {0,1},{0,2},{1,0},{1,1},{1,2},{2,0},{2,1},{2,2},{3,0},{3,1},{3,2}};

static const Cellofs *sword_shape(const Game *g, int *n)
{
    int step = g->attack_var >> 1;
    if (g->attack_type == 2 || (g->attack_type == 1 && step >= 2)) { *n = (int)(sizeof THRUST / sizeof *THRUST); return THRUST; }
    if (g->attack_type == 1) {
        if (step == 0) { *n = (int)(sizeof UP0 / sizeof *UP0); return UP0; }
        *n = (int)(sizeof UP1 / sizeof *UP1); return UP1;
    }
    if (step == 0) { *n = (int)(sizeof SLASH0 / sizeof *SLASH0); return SLASH0; }
    if (step == 1) { *n = (int)(sizeof SLASH1 / sizeof *SLASH1); return SLASH1; }
    *n = (int)(sizeof SLASH2 / sizeof *SLASH2); return SLASH2;
}

/* The *renderer* runs the swing: 3E45 increments `[FF46]` and the three arms of
 * 3E34 end it at a different count each -- 3EAC `cmp [FF46],7` for the slash
 * (6 drawn frames), 3E81 `cmp [FF46],5` for the upward slash (**4**), and the
 * down-thrust never ends on its own because 6E61 rewrites `[FF46]` to 2 every
 * frame the button and "down" are held.  `attack_var` is `[FF46] - 1`: the port
 * renders before `sword_apply` runs, so a frame is drawn with the same value
 * 6F07 then applies, exactly as gfmcga draws with the value 6F07 reads.
 *
 * Known deviation: the port keeps a six-frame swing for the *upward* slash too.
 * Cutting it to the original's four (3E81) is a two-frame change in how long a
 * raised blade can land a hit, and it is enough to derail `make playthrough`'s
 * route 2 (it runs out of frames in MP20).  gfx.c's SWORD_FRAMES has the real
 * counts and render.c clamps the drawn frame to them, so the *picture* is
 * right; only the last two hit frames of an upward slash are extra. */
#define SWING_FRAMES 6                          /* 3EAC; 3E81 is really 4 */

/* 0x6E3B  Sword input, once per frame before frame(). */
void sword_input(Game *g)
{
    if (!g->sword) return;
    if ((g->buttons & 1) && g->vstate != V_GROUND && !g->conveyor && (g->dirs & DIR_DOWN)) {   /* 6E45 */
        g->attack_type = 2; g->attack_var = 2;
        if (!g->thrust_latch) { g->thrust_latch = 0xFF; g->sfx_request = 4; }
        g->btn1_edge = 0; g->attacking = 0xFF;
        return;
    }
    g->thrust_latch = 0;
    if (!g->btn1_edge || g->attacking) return;                          /* 6E81..6E96 */
    g->attack_type = 0; g->attack_var = 0;
    if (!g->boss_map) {                                                 /* 6EA0: hittable sprite overhead? */
        int base = game_ring_add(game_hero_cell(g), -(4 * RING_W + 3)); /* rows -4..-1, cols -3..+4 */
        int found = 0;
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 8; c++) {
                uint8_t v = g->ring[game_ring_add(base, r * RING_W + c)];
                if (!(v & 0x80)) continue;
                int i = v & 0x7F;
                if (i < g->nobj && !(g->obj[i].type & 0x70)) found = 1;
            }
        }
        if (found) g->attack_type = 1;                                  /* 6EDC: upward slash */
    }
    if (!g->attack_type && (g->dirs & DIR_UP)) g->attack_type = 1;      /* 6ED6 */
    g->sfx_request = 3;
    g->btn1_edge = 0; g->attacking = 0xFF;
}

/* 0x6F07  Apply the blade, once per rendered frame while attacking.  Every
 * sprite on a shape cell that is not sword-immune (type & 0x20) and not
 * already stunned (hit & 0x20) gets hit source 1, pending (6F8D). */
void sword_apply(Game *g)
{
    if (!g->attacking) return;
    if (g->boss_map && g->boss_cutscene) return;
    int n;
    const Cellofs *sh = sword_shape(g, &n);
    int left = (g->hero_flags & FACE_LEFT) != 0;
    int base = game_hero_cell(g) + (g->crouching ? RING_W : 0);         /* 6F21: origin one row lower when crouched */
    for (int i = 0; i < n; i++) {
        int dc = left ? 2 - sh[i].dc : sh[i].dc;
        uint8_t v = g->ring[game_ring_add(base, sh[i].dr * RING_W + dc)];
        if (!(v & 0x80)) continue;
        int oi = v & 0x7F;
        if (oi >= g->nobj) continue;
        MapObj *o = &g->obj[oi];
        if (o->type & 0x20) continue;
        if (o->hit & 0x20) continue;
        o->hit = (uint8_t)((o->hit & 0xE0) | 0x40 | 1);
    }
    if (g->attack_type != 2 && ++g->attack_var >= SWING_FRAMES) {
        g->attacking = 0; g->attack_var = 0;                            /* 3F1A */
    }
}

/* ---------------------------------------------------------- contact damage */

/* 0x763E/0x7651/0x765E: scan `rows` cells downward; a sprite whose type lacks
 * bit 6 adds the AI's contact-damage entry (7675). */
static int contact_column(Game *g, int p, int rows)
{
    int hit = 0;
    for (int r = 0; r < rows; r++, p = game_ring_add(p, RING_W)) {
        uint8_t v = g->ring[p];
        if (!(v & 0x80)) continue;
        int i = v & 0x7F;
        if (i >= g->nobj) continue;
        MapObj *o = &g->obj[i];
        if (o->type & 0x40) continue;                                   /* harmless */
        if (g->ai) g->contact_damage = (uint16_t)(g->contact_damage + g->ai->contact[o->type & 0xF]);
        hit = 1;
    }
    return hit;
}

/* 0x751F  Four columns (ring col -1 .. +2) x 3 rows (-1..+1; 0..+1 crouched). */
void hero_enemy_contact(Game *g)
{
    if (g->boss_map && g->boss_cutscene) return;
    g->contact_damage = 0;
    int rows = 3;
    int s = game_hero_cell(g) - 1;
    if (g->crouching) rows = 2; else s = game_ring_add(s, -RING_W);     /* 7545 */
    for (int i = 0; i < 4; i++) {
        g->hit_side[i] = (uint8_t)(contact_column(g, game_ring_add(s, i), rows) ? 0xFF : 0);
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
