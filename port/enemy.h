/* enemy.h — the C010 object table, the eai* AI overlay ABI and combat.
 *
 * Sources: docs/FIGHT.md §6-§8, docs/ENEMIES.md, src/ai/ai_common.h, src/fight.c.
 * Addresses in the comments are fight.bin / eai1.bin offsets. */
#ifndef ZEL_ENEMY_H
#define ZEL_ENEMY_H
#include "physics.h"

/* ---------------------------------------------------------------- overlay */
/* The AI overlay is a raw image loaded at BASE:A000 (fight.bin 7EBB).  The port
 * re-implements the *code* of eai1 in C (ai_eai1.c) but reads every *table*
 * straight out of the original image, so EXP/contact/drops/frames are exact. */
#define AI_BASE 0xA000
typedef struct AiOverlay {
    uint8_t *img;             /* the overlay image; address a is img[a - 0xA000] */
    size_t   len;
    int      loaded;
    int      index;           /* request-table index (0 = EAI1) */
    uint8_t  exp[8];          /* A008: EXP per class */
    uint8_t  contact[16];     /* A010: contact damage per (type & 0xF) */
    uint16_t frame_l[32];     /* A030: frame-list pointers, facing left */
    uint16_t frame_r[32];     /* A070: facing right */
    uint16_t drops;           /* [A006] -> u16[8] -> u8[4] */
} AiOverlay;

/* ZELRES3[index+1] (request table fight.bin 9CBC: 0 EAI1, 2 EAI2, ... ) */
int  ai_load(AiOverlay *o, const char *dir, int ai_index);
void ai_unload(AiOverlay *o);
/* 5-byte frame {palette, TL, TR, BL, BR} for an object, or NULL */
const uint8_t *ai_frame(const AiOverlay *o, uint8_t type, uint8_t hit, uint8_t phase);
/* the 4-entry drop-id list of a class (97E2), or NULL */
const uint8_t *ai_drop_list(const AiOverlay *o, int cls);

/* ------------------------------------------------ the per-frame enemy pass */
void enemies_load(Game *g);          /* copy the map's C010 records (map load / transition) */
void enemies_update(Game *g);        /* 8D19 */
void enemy_dying(Game *g, MapObj *o);/* 90E6 */
void enemy_remove(Game *g, MapObj *o);/* 914C */
void enemy_spawn(Game *g, MapObj *o);/* 94FF */
void item_update(Game *g, MapObj *o);/* 8E14 table */

/* --------------------------------------------- AI service vectors (ai.c) */
/* Directions: 0 R, 1 RU, 2 U, 3 LU, 4 L, 5 LD, 6 D, 7 RD.  All return
 * 1 = blocked (the 8086 CF) and leave the record untouched when blocked. */
int  ai_step_dir(Game *g, MapObj *o, uint8_t dir);      /* vec 2  9723 */
int  ai_probe_dir(Game *g, MapObj *o, uint8_t dir);     /* vec 3  973F */
int  ai_step(Game *g, MapObj *o, int dir);              /* vec 4..11 */
int  ai_probe(Game *g, MapObj *o, int dir);             /* vec 12..19 */
int  ai_cell_passable(const Game *g, uint8_t cell);     /* vec 23 94E1 */
int  ai_on_hazard(const Game *g, const MapObj *o);      /* vec 24 97A0 */
int  ai_map_col_to_ring(const Game *g, uint16_t col, uint8_t *rcol);  /* vec 27 96A1: 1 = off ring */
int  ai_find_spare(Game *g);                            /* vec 31 98C5: index or -1 */
int  ai_ride_current(Game *g, MapObj *o);               /* vec 32 975B: 1 = it moved */
uint16_t krn_random(Game *g);                           /* kernel [11A] */

/* --------------------------------------------------- combat (combat.c) */
uint8_t damage_for_source(const Game *g, uint8_t src);  /* vec 28 9851 */
void enemy_killed(Game *g, MapObj *o);                  /* vec 25 96D5 */
void enemy_take_damage(Game *g, MapObj *o);             /* vec 26 97B5 */
void kill_with_exp(Game *g, MapObj *o);                 /* 96C1 */
void sword_input(Game *g);                              /* 6E3B */
void sword_apply(Game *g);                              /* 6F07 */
void hero_enemy_contact(Game *g);                       /* 751F */
void hero_knockback(Game *g);                           /* 6412 */
void hero_damage(Game *g, unsigned dmg);                /* 7685 */
void hero_damage_shielded(Game *g, unsigned dmg);       /* 75E2 */
void hero_die(Game *g);                                 /* 718C / 98FC */
void gold_add(Game *g, unsigned n);                     /* 917C */
void exp_add(Game *g, unsigned n);                      /* 9715 */

/* ------------------------------------------------------ the eai1 classes */
void eai1_entry(Game *g, MapObj *o);                    /* A254 */

#endif
