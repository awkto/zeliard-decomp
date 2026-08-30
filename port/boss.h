/* boss.h — the boss protocol and the boss AI overlays.
 *
 * Sources: docs/ENEMIES.md §1/§3, src/ai/boss_*.c, src/fight.c (6078 the
 * encounter card, 8D1D the once-per-frame call, 71CC the rewards, 72F1 the
 * post-boss transition), docs/VIDEO_DRIVERS.md ([200A]/[200C] the ENEMY bar,
 * [2010] the name label).
 *
 * A boss overlay does not use fight.bin's enemy pass at all.  Every frame it
 *   1. walks last frame's C010 records, restores the cells its markers
 *      covered and picks up the first *pending* hit bit (0x40) — the sword
 *      and the magic/orb code are the only fight.bin parts still involved;
 *   2. scales `damage_for_source()` by its own rule (x4/x8 for CRAB, x2/x4
 *      TAKO, ...) and subtracts it from the HP word at [A002]+3, redrawing
 *      the bar through video [200C];
 *   3. moves, and rebuilds the whole record list from its part buffer.
 * At HP 0 it sets boss_cutscene, runs a 40-frame death (boss_dying) and then
 * boss_defeated, whereupon fight.bin awards [A002]+5 EXP and +B ALMAS (71F2 calls
 * 917C, the almas adder - NOT gold; see docs/FIGHT.md "The two purses") and runs
 * post_boss_transition(). */
#ifndef ZEL_BOSS_H
#define ZEL_BOSS_H
#include "enemy.h"

/* ---------------------------------------------------------------- protocol */
/* 9CBC request indices of the 11 boss overlays */
enum { BOSS_CRAB = 1, BOSS_TAKO = 3, BOSS_TORI = 5, BOSS_ZELA = 7, BOSS_MEDA = 9,
       BOSS_LEGA = 11, BOSS_DRGN = 13, BOSS_AKMA = 15, BOSS_MAO1 = 16,
       BOSS_MAO2 = 17, BOSS_ZEL2 = 18 };
int  boss_overlay_p(int ai_index);       /* 1 = the 9CBC entry is a boss overlay */
const char *boss_overlay_name(int ai_index);

/* 6078/6150: parse [A002] out of the loaded overlay, set the camera column,
 * the knock-left flag, the HP bar and the private state.  Called when a map
 * whose level record has bit7 (FF34) or bit6 ([E6]) is entered. */
int  boss_init(Game *g);
/* 8D1D: one call per frame, replacing the whole enemy pass. */
void boss_update(Game *g);
/* 71CC: EXP + almas once boss_defeated and boss_state == 0xFF. */
void boss_rewards(Game *g);
/* 72F1: swap in the post-boss AI/enemy banks, clear FF34, apply the level
 * record's pokes and move the exit door to the hero's column.  The port asks
 * the shell to reload the banks through this callback. */
void boss_set_post_hook(Game *g, PostBossFn fn);
int  post_boss_transition(Game *g);

/* ------------------------------------------------- helpers for the overlays */
/* [A002] field access straight out of the overlay image */
uint16_t boss_info_u16(const Game *g, unsigned off);
uint8_t  boss_info_u8(const Game *g, unsigned off);
/* a byte / word of the overlay image at an absolute A000-based address */
uint8_t  boss_img8(const Game *g, unsigned addr);
uint16_t boss_img16(const Game *g, unsigned addr);

/* step 1: restore the covered cells and return the first pending hit as
 * (source & 0x1F) | 0x80 when `weak` says the part is a weak point, or 0.
 * `weak` may be NULL (no weak points). */
uint8_t boss_readback(Game *g, int (*weak)(uint8_t type));
/* CRAB A6BC / TAKO A44D / TORI A510 / MEDA A4F5 / LEGA A501 / DRGN A644 /
 * AKMA A5D1 / MAO2 A763: while the overlay's own hit variable is non-zero,
 * every record it emits this frame carries `hit` bit 5.  fight.bin's
 * `sword_apply` (6F8B) skips a marker whose object already has it, so a boss
 * can be struck at most every other frame.  (ZELA, ZEL2 and MAO1 have no such
 * write in their images, so they must not set it.)  `boss_readback` clears it
 * again at the top of every frame. */
void boss_hit_flash(Game *g, int on);
/* the layer paste every image-composing overlay shares (DRGN A758, AKMA A7CC,
 * MAO1 A2D3, MAO2 A939): for `cols` columns take `bpc` bitmap bytes, and for
 * every set bit (bit 7 = the layer's first row) one byte off `list`, writing
 * them into a column-major buffer of `bh` rows at (x, y). */
void boss_paste(Game *g, uint8_t *buf, int bw, int bh, int x, int y,
                int cols, int bpc, unsigned list, unsigned bm);
/* step 3: rebuild the record list */
void boss_parts_begin(Game *g);
void boss_part(Game *g, uint16_t col, uint8_t row, uint8_t type, uint8_t phase);
void boss_parts_end(Game *g);
/* read a 13-byte projectile template (vec 29's argument) out of the overlay
 * image: {col, row, cell, age, life, flags(dir | 08 through walls | 40
 * scripted), damage, ...} — the bosses all keep theirs as data */
void boss_shot_template(const Game *g, unsigned addr, Shot *out);
/* A796 etc.: HP -= d, redraw the bar, start the cutscene at 0 */
void boss_damage(Game *g, unsigned d);
/* the common 40-frame death: `sfx` every `every` frames for the first `n`
 * frames; the caller supplies the poses.  Returns the frame counter. */
uint8_t boss_death_tick(Game *g);
/* A37E etc.: min(scroll_col + n, map width) — the hero's map column */
uint16_t boss_hero_col(const Game *g, int n);

/* --------------------------------------------------------- the overlays */
void boss_crab_entry(Game *g);
void boss_tako_entry(Game *g);
void boss_tori_entry(Game *g);
void boss_zela_entry(Game *g);           /* also ZEL2 (the same code) */
void boss_meda_entry(Game *g);
void boss_lega_entry(Game *g);
void boss_drgn_entry(Game *g);
void boss_akma_entry(Game *g);
void boss_mao1_entry(Game *g);
void boss_mao2_entry(Game *g);
void boss_generic_entry(Game *g);        /* nothing uses it now; kept as the fallback */

#endif
