/*
 * eai2.c — cavern 2 enemy AI (EAI2.BIN = ZELRES3[2], 2720 bytes @ BASE:A000).
 * Sprites ENP2.GRP (ZELRES3[57]).  Maps mp20/mp21: classes 0+1 (tall pairs),
 * 2, 3, 4.  Class 5 exists in the tables but is placed in no map.
 *
 * Header:  [A000] = A369 entry   [A006] = A349 drop lists
 *   A008 exp[]     = { 10, 10, 4, 10, 4, 255 }
 *   A010 contact[] = { 10, 10, 8, 10, 8, 40 }
 *   drops: class 0/1 A355 {5,5,5,5} (10 G always), 2 A359 {4,0,4,0},
 *          3 A35D {5,4,4,0}, 4 A361 {5,4,5,0}, 5 A365 {9,9,9,9} (full potion)
 *   Frames (L/R): 0 A0B0/A0D8 (8), 1 A10F/A137 (8, lower half), 2 A16E (12, both
 *   facings), 3 A1B9/A1E1 (8), 4 A218/A23B (7), 5 A26D/A290 (7 = class 4 in
 *   palette 1); dying 8..D at A100/A15F/A1AA/A209/A25E/A2B3.
 * Dispatch A377: { A384, A383, A6D8, A7A4, A923, A923 }; class 1 = `ret`
 * (the lower record of the tall class-0 enemy is driven by the upper one).
 */
#include "ai_common.h"

/* ======================================================================== */
/* Class 0 (+1) — tall 2x4 "plant" shooter.  HP 8, contact 10, EXP 10.       */
/* next: bit0 attacking, bit1 use the straight shot.  Private data:           */
/* [A6D6]/[A6D7] preferred distance while facing right/left (initially 8/8,  */
/* re-rolled to 7..10).  The routine ends through tall_sync (A6BF), which    */
/* copies phase and facing into the lower record (SI+0x10).                  */
/* ======================================================================== */
static u8 dist_r = 8, dist_l = 8;                       /* A6D6, A6D7 */

/* shot templates (struct shot, 13 bytes): col/row are patched in at A4BA */
static struct shot shot_arc_r  = { 0, 0, 0x9A, 0, 0xFF, 0x40, 8,  0, arc_r };   /* A4FD: scripted, damage 8 */
static struct shot shot_arc_l  = { 0, 0, 0x9A, 0, 0xFF, 0x40, 8,  0, arc_l };   /* A50A */
static struct shot shot_str_r  = { 0, 0, 0x9A, 0, 7,    0,    20, 0, 0 };       /* A517: straight right, 7 cells, damage 20 */
static struct shot shot_str_l  = { 0, 0, 0x9A, 0, 7,    4,    20, 0, 0 };       /* A524: straight left */
static const u8 arc_r[] = { 1,1,1, 0,0, 7,7,7,7,7,7, 0xFF };   /* A531: RU x3, R x2, RD x6, then stop */
static const u8 arc_l[] = { 3,3,3, 4,4, 5,5,5,5,5,5, 0xFF };   /* A53D */

/* 0xA8F4  hero within 5 rows?  AL = 0xFF if not, else the facing bit toward
 * him; CF=1 when the enemy already faces him. */
static u8 hero_dir5(struct enemy *e, bool *facing_hero)
{
    u8 d = hero_map_row - e->row; if ((s8)d < 0) d = -d;
    *facing_hero = false;
    if (d >= 5) return 0xFF;
    if (e->rcol < 0x11) { *facing_hero = (e->hit & FACING_RIGHT) != 0; return FACING_RIGHT; }
    else                { *facing_hero = (e->hit & FACING_RIGHT) == 0; return 0; }
}

/* 0xA6BF  epilogue of class 0: lower.phase = phase, lower.hit facing = ours */
static void tall_sync(struct enemy *e)
{
    e[1].phase = e->phase;
    e[1].hit = (e->hit & 0x80) | (e[1].hit & 0x7F);
}

/* 0xA6AB  either half was hit: take the source bits from the lower record,
 * mark both halves stunned and let vec 26 damage the upper (HP holder).
 * Quirk: a hit on the upper half uses the lower half's stale source bits
 * (0 = "stomp" formula until the lower half has been hit once). */
static void tall_take_hit(struct enemy *e)
{
    u8 a = (e[1].hit & 0xBF) | 0x20;
    e->hit = a; e[1].hit = a | 0x60;
    fight_take_damage(e);
}

/* 0xA653  gravity for the 4-row sprite: rcol must be 1..0x22; cells
 * (row+4, rcol) and (row+4, rcol+1) passable (A679); then row+1 on both
 * records.  CF=1 blocked. */
static bool tall_step_down(struct enemy *e);
/* 0xA549 / 0xA5CE  step right / left: rcol < 0x22 / >= 2, the 4 cells of
 * column rcol+2 / rcol-1 passable (A56F/A5F4) and no sprite marker in the
 * column above them (rows row-1..row-4, i.e. 5 cells OR-ed); then col/rcol
 * +-1 on both records (map-width wrap). */
static bool tall_step_right(struct enemy *e);
static bool tall_step_left(struct enemy *e);

/* 0xA384 */
static void plant_update(struct enemy *e)
{
    bool facing;
    if (!e->hp) e->hp = 8;                                          /* A384 */
    if ((e->hit & HIT_STUN) || (e[1].hit & 0x40)) { tall_take_hit(e); return; }   /* A38E/A397 */
    if (!tall_step_down(e)) return;                                 /* A3A0: falling */
    if (e->next & 1) { plant_attack(e); return; }                   /* A3A6 */
    u8 f = hero_dir5(e, &facing);                                   /* A3AF */
    if (!facing) {
        if (f != 0xFF) e->hit ^= FACING_RIGHT;                      /* A3B8: hero behind and near: turn */
        if ((e->phase += 0x80) did not carry) { tall_sync(e); return; }   /* A3BC: walk every 2nd frame */
        e->phase = (e->phase + 1) & 3;                              /* A3C5 */
        if (e->hit & FACING_RIGHT) { if (tall_step_right(e)) e->hit &= ~FACING_RIGHT; }   /* A3E1: wall -> turn */
        else                       { if (tall_step_left(e))  e->hit |= FACING_RIGHT;  }   /* A3D2 */
        tall_sync(e); return;
    }
    /* facing the hero within 5 rows: keep the preferred distance (A3F0) */
    e->hit &= ~FACING_RIGHT; if (e->rcol <= 0x11) e->hit |= FACING_RIGHT;   /* A3F0..A3FB */
    if (e->hit & FACING_RIGHT) {
        u8 dist = 0x11 - e->rcol;                                   /* A405 */
        if (dist == dist_r) goto at_range;
        if (dist < dist_r) { if (tall_step_left(e))  goto cornered; e->phase = (e->phase - 1) & 3; }   /* A41F: back off */
        else               { if (tall_step_right(e)) goto at_range; e->phase = (e->phase + 1) & 3; }  /* A410 */
    } else {
        u8 dist = e->rcol - 0x11;                                   /* A42E */
        if (dist == dist_l) goto at_range;
        if (dist < dist_l) { if (tall_step_right(e)) goto cornered; e->phase = (e->phase - 1) & 3; }  /* A44A */
        else               { if (tall_step_left(e))  goto at_range; e->phase = (e->phase + 1) & 3; }  /* A43B */
    }
    tall_sync(e); return;
at_range:                                                           /* A459 */
    dist_r = (KRN_RANDOM() & 3) + 7;                                /* 7..10 */
    dist_l = (KRN_RANDOM() & 3) + 7;
    hero_dir5(e, &facing);
    if (facing) { e->next |= 1; e->phase = 4; }                     /* A47D: start an arc shot */
    tall_sync(e); return;
cornered:                                                           /* A488: wall behind while backing off */
    if (KRN_RANDOM() & 1) return;                                   /* (no tall_sync this frame) */
    e->next |= 3; e->phase = 4;                                     /* straight shot */
    tall_sync(e);
}

/* 0xA49D  Attack: frames 4..7; at phase 6 fire, at 8 back to walking. */
static void plant_attack(struct enemy *e)
{
    e->phase++;
    if (e->phase == 8) { e->next &= ~3; e->phase = 0; }            /* A4AF */
    else if (e->phase == 6) {                                       /* A4BA */
        u8 c = e->rcol, r = e->row + 2;                             /* shot from the sprite's 3rd row */
        shot_arc_l.col = shot_str_l.col = c; shot_arc_r.col = shot_str_r.col = c + 1;
        shot_arc_r.row = shot_arc_l.row = shot_str_r.row = shot_str_l.row = r;
        struct shot *t = (e->hit & FACING_RIGHT) ? ((e->next & 2) ? &shot_str_r : &shot_arc_r)
                                                 : ((e->next & 2) ? &shot_str_l : &shot_arc_l);
        fight_shot_spawn(t);                                        /* A4F5: vec 29 */
    }
    tall_sync(e);                                                   /* A4FA */
}

/* ======================================================================== */
/* Class 2 — blue slime.  HP 4, contact 8, EXP 4.  12 frames: 0-7 idle      */
/* bubbling, 8-11 moving.  next bit0 = moving, bit1 = after the step.        */
/* ======================================================================== */
/* 0xA6D8  Idle 8 frames, then 8 frames of move animation with ONE step on
 * the 8th: pick a random side; if the floor beyond that side is open
 * (cell (row+2, rcol-1) / (row+2, rcol+2)) step the other way, else that way
 * -> never walks off a ledge.  Then 4 more animation frames.  Falls 1 row /
 * frame when airborne.  Net speed: 1 cell per 20 frames. */
static void slime_update(struct enemy *e)
{
    if (!e->hp) e->hp = 4;                                          /* A6D8 */
    if (e->hit & HIT_STUN) { fight_take_damage(e); return; }        /* A6E2 */
    if (!fight_step_down(e)) return;                                /* A6ED */
    if (!(e->next & 1)) {                                           /* A6F5 */
        e->phase = (e->phase + 1) & 7;
        if (e->phase == 0) { e->next = (e->next | 1) & ~2; e->link = 0; }   /* A705 */
        return;
    }
    e->phase = (e->link & 3) + 8;                                   /* A718 / A787 */
    e->link++;
    if (!(e->next & 2)) {
        if (e->link != 8) return;                                   /* A725 */
        e->next |= 2;
        if (!(KRN_RANDOM() & 0x80)) {                               /* A730 */
            u8 *p = ring_wrap_down(ring_addr(e->row, e->rcol) + 0x4A);   /* (row+2, rcol+2) */
            if (cell_passable_ai(*p)) fight_step_left(e); else fight_step_right(e);   /* A754 */
        } else {
            u8 *p = ring_wrap_down(ring_addr(e->row, e->rcol) + 0x47);   /* (row+2, rcol-1) */
            if (cell_passable_ai(*p)) fight_step_right(e); else fight_step_left(e);   /* A77B */
        }
        return;
    }
    if (e->link == 12) { e->next &= ~1; e->phase = 0; }             /* A794 */
}

/* ======================================================================== */
/* Class 3 — red hopping shooter.  HP 2, contact 10, EXP 10.  Frames 0/1    */
/* sit, 2-6 hop, 7 spit.  next: 8 hopping, 4 winding up, 2 recovering.       */
/* ======================================================================== */
static const u8 hop_r[4] = { 1, 0, 0, 7 }, hop_l[4] = { 3, 4, 4, 5 };   /* A8EC / A8F0 */
static struct shot spit_r = { 0, 0, 0x9E, 0, 6, 0, 20 };            /* A8D2: right, 6 cells, damage 20 */
static struct shot spit_l = { 0, 0, 0x9E, 0, 6, 4, 20 };            /* A8DF: left */

/* 0xA7A4  Like the cavern-1 frog (hop at once when facing a hero within 5
 * rows, else turn toward him every 8th frame), but each time it would hop
 * while facing him a coin decides: hop, or spit (4 frames wind-up, shot from
 * (rcol / rcol+1, row+1), 6 frames recovery). */
static void redfrog_update(struct enemy *e)
{
    bool facing;
    if (fight_on_hazard(e)) { fight_enemy_killed(e); return; }     /* A7A4 */
    if (!e->hp) e->hp = 2;                                          /* A7B0 */
    if (e->hit & HIT_STUN) { fight_take_damage(e); return; }        /* A7BA */
    if (e->next & 2) { e->link++; e->phase = (e->phase + 1) & 1; if (e->link == 6) e->next &= ~2; return; }   /* A8BC */
    if (e->next & 4) { redfrog_spit(e); return; }                   /* A871 */
    if (e->next & 8) { redfrog_hop(e); return; }                    /* A82B */
    e->phase = (e->phase + 0x21) & 0xE1;                            /* A7DD */
    if (!fight_step_down(e)) return;                                /* A7E5 */
    u8 f = hero_dir5(e, &facing);                                   /* A7ED */
    if (!facing) {
        if (e->phase & 0xE0) return;                                /* A7F2 */
        f = hero_dir5(e, &facing);                                  /* A7FA */
        if (f != 0xFF) { e->hit = (e->hit & 0x7F) | f; e->phase = 2; e->next |= 8; return; }   /* A801 */
    }
    if (KRN_RANDOM() & 1) { e->phase = 2; e->next |= 8; }           /* A811..A823: hop */
    else                  { e->next |= 4; e->link = 0; }            /* A81A: spit */
}

/* 0xA82B  identical to eai1 frog_hop (RU,R,R,RD / LU,L,L,LD; turn when
 * blocked unless facing the hero). */
static void redfrog_hop(struct enemy *e)
{
    bool facing;
    u8 old = e->phase, n = (old + 1) & 7;
    if (n < 7) {
        e->phase = (old & 0xF0) | n;
        if (!fight_step_dir(((e->hit & FACING_RIGHT) ? hop_r : hop_l)[old - 2])) return;
        hero_dir5(e, &facing); if (!facing) e->hit ^= FACING_RIGHT;        /* A85B */
    }
    e->next &= ~8; e->phase = 0; fight_step_down(e);                /* A864 */
}

/* 0xA871 */
static void redfrog_spit(struct enemy *e)
{
    e->link++; e->phase = (e->phase + 1) & 1;
    if (e->link != 4) return;
    e->phase = 7;                                                   /* A882 */
    spit_l.col = e->rcol; spit_r.col = e->rcol + 1;
    spit_l.row = spit_r.row = (e->row + 1) & 0x3F;
    fight_shot_spawn((e->hit & FACING_RIGHT) ? &spit_r : &spit_l);  /* A8AA */
    e->next = (e->next & ~4) | 2; e->link = 0;                      /* A8AF */
}

/* ======================================================================== */
/* Class 4 (and 5) — green/red "bird": the cavern-1 bat again with HP 3,    */
/* contact 8, EXP 4 (class 5: palette 1, contact 40, EXP 255, drops full     */
/* potions; not placed in any map).                                          */
/* ======================================================================== */
/* 0xA923  Same state machine as eai1 bat_update (table A956 = { A95E, A989,
 * A99C, AA3C }) with one difference: the idle state first tries one step UP
 * every frame (A95E: call vec 6), so an idle bird climbs to the ceiling.
 * Wake-up range, chase (A99C) and retreat (AA3C) are byte-identical to eai1
 * A2D0/A2E3/A383 (activation 0x0B <= rcol <= 0x1A, 7-frame idle after a
 * retreat, retreat triggered by hero_hit_flash or by a floor under it at the
 * hero's column). */
static void bird_update(struct enemy *e)
{
    if (fight_on_hazard(e)) { fight_enemy_killed(e); return; }     /* A923 */
    if (!e->hp) e->hp = 3;                                          /* A92F */
    if (e->hit & HIT_STUN) { fight_take_damage(e); return; }        /* A939 */
    switch (e->next >> 6) {                                         /* A944 */
    case 0: fight_step_up(e); bat_idle(e);   break;                 /* A95E */
    case 1: bat_wake(e);    break;                                  /* A989 */
    case 2: bat_chase(e);   break;                                  /* A99C */
    case 3: bat_retreat(e); break;                                  /* AA3C */
    }
}

/* 0xA369 */
void ai_entry(struct enemy *e)
{
    static void (*const fn[6])(struct enemy *) = { plant_update, tall_lower_noop, slime_update, redfrog_update, bird_update, bird_update };   /* A377 */
    fn[e->type & 0xF](e);
}
static void tall_lower_noop(struct enemy *e) { }                    /* A383 */
