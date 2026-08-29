/*
 * boss_mao2.c — final boss "Jashiin" (MAO2.BIN = ZELRES3[19], 3183 bytes @ A000).
 * Map mpa0 (73 cells wide, level record ai = 17, enemies = 17 = MAO2.GRP,
 * music mmao, flags 0xBB: FF34 boss map).  Sprites MAO2.GRP.
 * Boss info [A002] = AC03:
 *   col 0x30 (AC03), row 0x09 (AC05), HP 800 (AC06), EXP 10000, camera column
 *   12, knockback 0, name record AC10 {0x11, 0x2BB, 7 "Jashiin"}, gold 0.
 * Contact A010: 80 for every class (including the two projectiles, classes
 * 4 and 5).  Frame tables A030: classes 0..5 (A07C..).  No drops.
 *
 * Image: 6 columns x 9 rows, cell granularity, buffer AC39 (column-major,
 * stride 9, FF = empty), pasted by A939 from list [A9E4 + 2*pose] / bitmap
 * [AAE1 + 2*pose] when facing right (AC1E = FF) or [A957]/[AA71] facing left
 * (6 bitmap bytes = 6 columns, bit 7 = row 0; 7..9 parts).  Poses 0..13:
 * 0..3 walk cycle (class 0 frames 0-15), 4 crouch, 5 crouch + two extra
 * parts patched in at row 8 (A6EA: 0x23/0x1F right, 0x1F/0x21 left), 6 jump,
 * 7..9 throw (9 = release), 10..13 cast (12 = release).
 * Part records (A70E..A7AA): {col = boss_col + c, row = boss_row + r, type =
 * class | AC22 (0x60 = sword-immune + harmless while materialising), hit =
 * FACING_RIGHT when facing right | 0x20 on a hit, phase = byte}.
 * Weak point (A337): a class-0 part showing frame 0 marks the hit |0x80 —
 * computed but unused.
 *
 * Damage (A362): d = damage_for_source(src) / 2 for the sword, / 4 for
 * everything else; sound 0x39.  When HP drops below 200 the fight enters
 * PHASE 2 (AC32).
 *
 * Phase 1 — teleport & strike (A3BF): Jashiin is invisible most of the time.
 * When no projectile of his is in flight he picks a side at random (A479:
 * column = scroll_col + 4 (left of the hero, facing right) or + 24 (right,
 * facing left); if that column is outside 0x10..0x34 the other side), row 9,
 * and runs a 17-step sequence (AC25, one step per frame):
 *   1..5   flicker in: odd steps invisible, even steps drawn immune
 *          (type|0x60) in pose variant*10 with sound 0x3B
 *   6..10  solid: poses A46F[variant*5 + i] = {0,0,7,7,9} (throw: pose 9
 *          launches projectile 1) or {10,10,11,11,12} (cast: pose 12
 *          launches projectile 2)
 *   11..16 flicker out (even steps drawn immune, sound 0x3B)
 *   17     gone (AC23 = 0); the next teleport waits for the projectile
 * The variant is a random bit (A3E7).  He can only be hurt during steps
 * 6..10 (the rest are immune or not drawn).
 *
 * Phase 2 — HP < 200 (A4C9): he stays visible and fights on foot: every
 * frame he faces the hero and keeps EXACTLY 8 columns from him (A4F5..A5CB:
 * distance & 0xFE; < 8 -> walk away, > 8 -> walk toward, 1 cell on odd walk
 * frames and 2 on even ones, poses 0..3 cycling backwards when retreating);
 * a wall (columns 0x10..0x35) starts the jump (A666 table, 14 frames: 2 x
 * pose 4, rise 2 rows x 3, forward 2 cells x 8 of which the last 3 fall 2
 * rows, 2 x pose 4, pose 0 — i.e. 6 rows up, 16 cells forward, 6 rows down,
 * all in the facing direction).  Once at range for two consecutive frames,
 * 1/16 per frame he starts the throw sequence {0,0,7,7,9} (A5F7) which ends
 * with projectile 1.  While projectile 1 flies he stands still.
 * Every 32 frames (AB88) he REGENERATES 80 HP (sound 0x3C); if that brings
 * him back to 800 he leaves phase 2 and resumes phase 1 at step 10 (the
 * flicker-out), immune.  So the player has to take the last 200 HP within
 * 32-frame windows, or he heals right back.
 *
 * Projectiles (drawn as sprite-marker parts, NOT fight.bin shots, so they do
 * contact damage 80 and the shield does not apply):
 *   1 (A8E5, sound 0x3A): type 0x24 (class 4, immune); starts at (col + 5 if
 *     facing right else col, row + 4); ages 0..2 fall one row per frame,
 *     ages 0..8 move one column per frame in the facing direction, frame 0
 *     then (age&3)+1; gone at age 11 (A7B8..A852).
 *   2 (A90E): type 0x25 (class 5, immune); starts at (col + 8 - 1 if facing
 *     right else col - 1, row + 4); falls one row per frame for 3 frames
 *     (frame 2) then flies horizontally 1 column per frame (frame 0) until
 *     its column leaves 0x10..0x38 (A85B..A8E4).
 *
 * Start: nothing is drawn until [FF21] != 0 (A3A0; the byte is not written
 * by fight.bin — its only other reference is the enddemo overlay), then one
 * more frame, then phase 1 begins.
 *
 * Private state: AC1B pose, AC1C part counter, AC1D hit source, AC1E facing
 * (FF right), AC1F rcol temp, AC20 death counter, AC21 started, AC22 type
 * bits (0x60 immune), AC23 teleport sequence on, AC24 variant, AC25 step,
 * AC26 list pointer after the body, AC28/AC29/AC2A/AC2B/AC2C projectile 1
 * on/col/row/dir/age, AC2D..AC31 projectile 2, AC32 phase 2, AC33 jumping,
 * AC34 jump index, AC35 at-range latch, AC36 throwing, AC37 throw index,
 * AC38 frame counter, AC39.. buffer.
 */
#include "ai_common.h"

static u16 *const boss_col = (u16 *)0xAC03;  static u8 *const boss_row = (u8 *)0xAC05;  static u16 *const boss_hp = (u16 *)0xAC06;
static u8 pose, facing, death_cnt, started, type_bits, teleporting, variant, step, phase2,
          jumping, jump_idx, at_range, throwing, throw_idx, frame;
static struct { u8 on, col, row, dir, age; } shot1, shot2;         /* AC28.. / AC2D.. */

static const u8 strike_seq[10] = { 0,0,7,7,9, 10,10,11,11,12 };   /* A46F */
static const u8 death_seq[10]  = { 8,8,8,12,12,12,13,13,11,11 };  /* ABF9: one per 2 frames */
static const struct { u8 fwd, drow, pose; } jump_seq[14] = {       /* A666: 3-byte entries, 0x80 terminator */
    {0,0,4},{0,0,4},{0,-2,5},{1,-2,5},{1,-2,5},{1,0,6},{1,0,6},{1,0,6},{1,2,6},{1,2,6},{1,2,6},{0,0,4},{0,0,4},{0,0,0} };

/* 0xA2F2  Frame entry. */
void boss_entry(void)
{
    u8 hit = readback_parts();                                      /* A2F6..A34F */
    MAP_OBJECTS[0].col = 0xFFFF; list_end = MAP_OBJECTS;
    if (hit) {                                                      /* A362 */
        u16 d = damage_for_source(hit & 0x1F) / 2; if ((hit & 0x1F) != 1) d /= 2;
        boss_damage(d); sfx_request = 0x39;                         /* AB51 */
        if (*boss_hp < 200) phase2 = 0xFF;                          /* A389 */
    }
    if (boss_cutscene) { death_step(); return; }                    /* A396 */
    if (!started) { if (FF21) started = 0xFF; return; }             /* A3A0: invisible until [FF21] */
    if (phase2) { phase2_step(); return; }                          /* A4C9 */

    /* phase 1 (A3BF) */
    if (!teleporting) {
        if (shot1.on || shot2.on) { draw_shots(); return; }         /* A3C6: stay hidden while a projectile flies */
        teleport(); step = 0; teleporting = 0xFF; variant = KRN_RANDOM() >> 7;   /* A3DA..A3F0 */
    }
    step++;                                                         /* A3F3 */
    if (step < 6 || (step >= 11 && step < 17)) {                    /* flicker */
        if (step & 1) { draw_shots(); return; }
        sfx_request = 0x3B; type_bits = 0x60; if (step < 6) pose = variant * 10;
    } else if (step < 11) {                                         /* A41C: solid poses */
        pose = strike_seq[variant * 5 + step - 6]; type_bits = 0;
        if (pose == 9)  launch_shot1();                             /* A8E5 */
        if (pose == 12) launch_shot2();                             /* A90E */
    } else { teleporting = 0; draw_shots(); return; }               /* A467 */
    draw(); draw_shots();                                           /* A6BC / A7AE */
}

/* 0xA479  Pick a side of the hero: facing = random bit; column = scroll_col
 * + 4 (facing right) or + 24 (facing left), wrapped at the map width; if it
 * is outside 0x10..0x34 use the other side.  Row = 9. */
static void teleport(void);

/* 0xA4C9  Phase 2. */
static void phase2_step(void)
{
    if (!(++frame & 0x1F)) regenerate();                            /* AB88 */
    if (jumping)  { jump_step(); draw(); draw_shots(); return; }    /* A617 */
    if (throwing) {                                                 /* A5F7 */
        pose = strike_seq[throw_idx++];
        if (pose == 9) { throwing = 0; launch_shot1(); }
        draw(); draw_shots(); return;
    }
    if (shot1.on) { draw(); draw_shots(); return; }                 /* A4EB: wait for the projectile */
    u8 hero = (u8)(scroll_col + hero_scr_col + 3);  if (hero >= MAP_WIDTH) hero -= MAP_WIDTH;   /* A4F5 */
    facing = ((u8)*boss_col < hero) ? 0xFF : 0;                     /* A50A: face the hero */
    u8 d = (facing ? hero - (u8)*boss_col : (u8)*boss_col - hero) & 0xFE;
    if (d != 8) {
        bool toward = d > 8;                                        /* A52B / A585 */
        pose = toward ? (pose + 1) & 3 : (pose - 1) & 3;
        bool (*st)(void) = (toward == (facing != 0)) ? step_right : step_left;
        if (!(pose & 1)) st();                                      /* even walk frame: 2 cells, odd: 1 */
        bool blocked = st();
        if (blocked) { jump_idx = 0; jumping = 0xFF; }              /* A548 etc. */
        else { draw(); draw_shots(); return; }                      /* A5F4 */
    }
    /* A5CD: at range (or just blocked): second consecutive frame -> 1/16 throw */
    u8 was = at_range; at_range = 0xFF;
    if (was) { pose &= ~1; if (!(KRN_RANDOM() & 0xF)) { throw_idx = 0; throwing = 0xFF; } }
    draw(); draw_shots();
}

/* 0xA691 / 0xA6A7  one cell, columns 0x10..0x35; success clears the at-range latch (AC35); true = blocked */
static bool step_left(void)  { if (*boss_col - 1 <= 0x0E) return true; (*boss_col)--; at_range = 0; return false; }
static bool step_right(void) { if (*boss_col + 1 >  0x35) return true; (*boss_col)++; at_range = 0; return false; }

/* 0xA617  One jump_seq entry per frame: fwd -> two steps in the facing
 * direction, row += drow, pose; ends after the entry followed by 0x80. */
static void jump_step(void);

/* 0xAB88  HP += 80 (cap 800), sound 0x3C, bar; if capped: phase2 = 0,
 * step = 10, teleporting = 0xFF, immune (resume phase 1 at the flicker-out). */
static void regenerate(void)
{
    if (*boss_hp == 800) return;
    u16 hp = *boss_hp + 80;
    if (hp > 800) { hp = 800; phase2 = 0; step = 10; teleporting = 0xFF; type_bits = 0x60; }
    *boss_hp = hp; sfx_request = 0x3C; VID_200C(BX = hp);
}

/* 0xAB51  HP -= d (floor 0), bar; at 0 (not already in the cutscene): death
 * counter 0, both projectiles off, boss_cutscene. */
static void boss_damage(u16 d)
{
    *boss_hp = (*boss_hp > d) ? *boss_hp - d : 0;
    VID_200C(BX = *boss_hp);
    if (*boss_hp == 0 && !boss_cutscene) { death_cnt = 0; shot1.on = shot2.on = 0; boss_cutscene = 0xFF; }
}

/* 0xABC4  Death: 40 frames of boss_dying with sound 0x23 every 8th frame;
 * the first 20 run death_seq (one pose per 2 frames), then the last pose
 * (11) holds; then boss_defeated (10000 EXP, no gold). */
static void death_step(void)
{
    if (death_cnt >= 0x28) { boss_defeated = 0xFF; return; }
    if (!(death_cnt & 7)) sfx_request = 0x23;
    boss_dying = 0xFF;
    if (death_cnt < 0x14) pose = death_seq[death_cnt / 2];
    death_cnt++; draw();
}

/* 0xA6BC  Compose the pose into AC39 (pose 5 patch), emit the records after
 * list_end.  0xA7AE  projectile records (see header). */
static void draw(void);
static void draw_shots(void);
