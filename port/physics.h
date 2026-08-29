/* physics.h — the fight.bin hero/camera model (docs/FIGHT.md §2, §4, §5).
 * Everything is cell-granular: the ring is 36 x 64 cells, the hero is a 3x3
 * sprite whose solid body is the middle column, the world scrolls under him. */
#ifndef ZEL_PHYSICS_H
#define ZEL_PHYSICS_H
#include <stdint.h>
#include "map.h"
#include "gfx.h"

#define RING_W    36
#define RING_H    64
#define RING_SIZE (RING_W * RING_H)
#define SCREEN_COLS 28
#define SCREEN_ROWS 19

#define DIR_UP 1
#define DIR_DOWN 2
#define DIR_LEFT 4
#define DIR_RIGHT 8

#define FACE_LEFT 1
#define WALKING   2

#define V_GROUND 0x00
#define V_RISE   0xFF
#define V_FALL   0x7F
#define V_KNOCK  0x80

#define DOOR_CELL 0x4A

#define MAX_OBJS 128            /* ED20 has 128 saved cells, so 128 markers max */
#define MAX_SHOTS 31            /* EB80 holds 31 live projectiles (8611) */

/* EB80: a 13-byte enemy projectile (docs/FIGHT.md §6, src/fight.c struct shot).
 * `col`/`row` are *ring* coordinates; the shot moves one cell per frame. */
typedef struct {
    uint8_t  col;               /* +0  ring column; 0 = dead, 0xFF = end of list */
    uint8_t  row;               /* +1  ring row */
    uint8_t  cell;              /* +2  bank cell; bits 6-7 pick the anim mask {0,1,3,7} (83D7) */
    uint8_t  age;               /* +3 */
    uint8_t  life;              /* +4  dies at age >= life unless flags & 0x40 */
    uint8_t  flags;             /* +5  bits0-2 direction, 0x08 through walls, 0x40 scripted */
    uint8_t  damage;            /* +6 */
    uint16_t drawn;             /* +7  screen ptr | 0x8000 while drawn (port: 0/1) */
    const uint8_t *script;      /* +9  direction byte per age, 0xFF ends the shot (85F2) */
    uint8_t  last_col, last_row;/* +B, +C */
} Shot;

/* EB15: one of the hero's four spell sprites (884D/88A8/88F8) */
typedef struct {
    uint16_t col;               /* +0  map column (0xFFFF = end of list) */
    uint8_t  row;               /* +2  ring row */
    uint8_t  dir;               /* +3  bit0 1 = moving right; bit7 = hit something (8AF2) */
    uint8_t  age;               /* +4  only record 0 is counted for multi-sprite spells */
    uint8_t  anim;              /* +5  0..2 */
    uint8_t  rcol, srow;        /* +6, +7 */
    uint8_t  live;              /* port: 0 once the record is retired (col high byte 0xFF) */
} Magic;

/* EB60: one of the four orbiting spheres (86FC), 7 bytes */
typedef struct { uint8_t phase, speed, hits; } Orb;

/* The boss protocol (docs/ENEMIES.md §1/§3).  A boss overlay is called once
 * per frame instead of the per-enemy pass (8D1D) and rebuilds the whole C010
 * list from its own part buffer; the `[A002]` info block below is read by
 * fight.bin at 6150/6162/6FE1/71E1. */
typedef struct Boss {
    int      active;                /* the map's AI overlay is a boss overlay */
    int      index;                 /* 9CBC request index (1 CRAB, 3 TAKO, ...) */
    uint16_t info;                  /* [A002] */
    /* the info block */
    uint16_t start_col; uint8_t start_row;
    uint16_t hp0;                   /* +3  initial HP = the bar's full value */
    uint16_t exp;                   /* +5 */
    uint8_t  cam_col;               /* +7  hero screen column (6FE8) */
    uint8_t  knock_left;            /* +8  -> 9F01 */
    uint16_t name_ptr;              /* +9  -> {u8 x4, u16 y, u8 len, chars} */
    uint16_t gold;                  /* +B */
    char     name[24];
    /* live state */
    uint16_t col; uint8_t row;      /* the boss's top-left map cell */
    uint16_t hp;
    uint8_t  death_cnt;
    uint8_t  parts;                 /* parts placed this frame */
    uint8_t  ported;                /* 0 = the generic placeholder overlay */
    /* per-overlay private state (only one boss is ever live) */
    uint8_t  pose, walk_dir, parity, u1, u2, u3, u4, u5, u6, u7, u8_, u9;
    uint8_t  st[24];                /* the overlay's own byte variables */
    uint16_t sw[4];                 /* ... and word variables */
    unsigned hits_taken;            /* port counter, for the tests */
} Boss;

struct AiOverlay;

typedef struct Game Game;

/* one rendered frame: main loop presents the frame, waits 4*speed ticks and
 * refreshes g->dirs.  Called once per frame() (ladder climbs render 1-2 per step). */
typedef void (*PresentFn)(Game *g);
/* door transition (docs/FIGHT.md §8): return 1 if the caller switched maps */
typedef int (*DoorFn)(Game *g, const Door *d);
/* hand-off to the town engine (99E0 / 72D9): `col` < 0 means "use the map's
 * own start column"; `died` is the sage path.  Return 1 when the shell took
 * over, 0 to fall back to restarting in the cavern. */
typedef int (*TownFn)(Game *g, int town_index, int col, int died);
/* 72F1 post_boss_transition: reload the level record's +6/+7 AI and enemy
 * banks (the shell owns the resource cache).  Return 1 on success. */
typedef int (*PostBossFn)(Game *g, int post_ai, int post_enemies);

struct Game {
    const Map     *map;
    const Tileset *tiles;
    uint8_t  ring[RING_SIZE];       /* E000: row-major, stride 36; ring row == map row */

    /* player record 0049-00E8 */
    int      scroll_col;            /* [80] map column in ring column 0 */
    uint8_t  scroll_row;            /* [82] ring row on screen row 0 */
    uint8_t  hero_scr_col;          /* [83] */
    uint8_t  hero_scr_row;          /* [84] */
    uint8_t  hero_flags;            /* [C2] bit0 facing left, bit1 walking */
    uint8_t  hero_anim;             /* [E7] 0x80 idle, walk frames & 0x7F, 0 jump pose */
    uint8_t  hero_dead;             /* [E8] */
    uint8_t  shoes;                 /* [9E] */
    uint8_t  shield;                /* [93] */
    uint16_t hp, max_hp;            /* [90], [B2] */

    /* FF00 page */
    uint8_t  hero_map_row;          /* FF35 */
    uint8_t  hero_hidden;           /* FF37 */
    uint8_t  crouching;             /* FF38 */
    uint8_t  on_ladder;             /* FF39 */
    uint8_t  hero_entering;         /* FF3A */
    uint8_t  vstate;                /* FF3D */
    uint8_t  conveyor;              /* FF42 */
    uint8_t  hero_hit_flash;        /* FF36 */

    /* fight.bin locals 9F00.. */
    uint8_t  hero_home_row;         /* 9F00 */
    uint8_t  fall_rows, rise_rows;  /* 9F08, 9F09 */
    uint8_t  crouch_release;        /* 9F0A */
    uint8_t  diag_jump;             /* 9F0B */
    uint8_t  conveyor_kick;         /* 9F0C */
    uint8_t  max_rise;              /* 9F0D */
    uint8_t  hero_hit;              /* 9F14 */
    uint8_t  on_updraft;            /* 9F15 */
    uint8_t  conveyor_phase;        /* 9F16 */
    uint8_t  on_hazard;             /* 9F17 */
    uint8_t  regen_tick;            /* 9F18 */
    uint8_t  door_msg_latch;        /* 9F19 */
    uint8_t  ice_slide, ice_steps;  /* 9F20, 9F21 */
    uint8_t  walk_dir;              /* 9F22 */
    uint8_t  slide_dir;             /* 9F23 */
    uint8_t  prev_facing;           /* 9F24 */

    /* enemies: the live copy of the map's C010 table (docs/FIGHT.md §7) */
    MapObj   obj[MAX_OBJS];
    int      nobj;
    uint8_t  under_sprite[MAX_OBJS];   /* ED20 */
    uint8_t  obj_index;                /* FF4A */
    const struct AiOverlay *ai;        /* eai1..8 tables */
    const EnemyGfx         *egfx;      /* enp1..8 cells */

    /* player record (docs/STATE_PAGE.md) */
    uint8_t  level;                 /* [8D] strength term of the damage formulas */
    uint16_t exp;                   /* [8E] */
    uint32_t gold;                  /* [85..87] */
    uint16_t almas;                 /* [8B] */
    uint8_t  sword;                 /* [92] 1..6 */
    uint16_t shield_hp;             /* [94] */
    uint8_t  keys, lion_keys;       /* [98], [99] */
    uint8_t  hero_crest;            /* [9C] */
    uint16_t hp_regen_pending;      /* [C6] */
    uint8_t  attack_bonus;          /* [E4] */

    /* combat state (FF page + 9F0E..) */
    uint8_t  attacking;             /* FF43 */
    uint8_t  attack_type;           /* FF45 0 slash, 1 upward, 2 down-thrust */
    uint8_t  attack_var;            /* FF46 swing frame */
    uint8_t  thrust_latch;          /* FF47 */
    uint8_t  buttons;               /* INT 61h AH: bit0 sword, bit1 magic */
    uint8_t  btn1_edge;             /* FF1D */
    uint8_t  hit_side[4];           /* 9F0E..9F11 */
    uint16_t contact_damage;        /* 9F12 */
    uint8_t  sfx_request;           /* FF75, consumed by sound_request() */

    /* projectiles / magic / orbs (docs/FIGHT.md §6, shots.c) */
    Shot     shots[MAX_SHOTS + 1];
    uint8_t  projectile_count;      /* 9F1F */
    Magic    magic[4];
    Orb      orbs[4];
    uint8_t  magic_sel;             /* [9D] 1..7 */
    uint8_t  magic_count[7];        /* [AB] */
    uint8_t  magic_max[7];          /* [B4] */
    uint8_t  casting;               /* FF3C */
    uint8_t  magic_active;          /* FF3E */
    uint8_t  cast_timer;            /* 9F2B */
    uint8_t  magic_hit_any;         /* 9F2A */
    uint8_t  btn2_edge;             /* FF1E */
    unsigned magic_casts, shots_fired;  /* port counters for the tests */
    uint8_t  heat_timer;            /* 9F25 */
    uint8_t  boss_map;              /* FF34  level record flags bit7 */
    uint8_t  boss_room;             /* 00E6  level record flags bit6 */
    uint8_t  boss_cutscene;         /* FF2E */
    uint8_t  boss_dying;            /* FF2F */
    uint8_t  boss_defeated;         /* FF30 */
    uint8_t  boss_state;            /* EDA0  0xFF until the rewards are paid (71DA) */
    uint8_t  boss_knock_left;       /* 9F01 */
    uint8_t  post_boss_pending;     /* 9F1E */
    uint8_t  boss_intro;            /* 9F26 */
    unsigned encounter_frames;      /* 60E6: the 6 flashes of the encounter card */
    Boss     boss;
    PostBossFn post_boss;
    uint16_t rng;                   /* kernel [11A] KRN_RANDOM source (FF1B) */
    unsigned deaths;                /* hero deaths (the port restarts at the entry) */
    uint8_t  death_anim;            /* 9F28 */
    int      entry_col, entry_row;  /* where hero_die() puts him back */
    uint8_t  entry_face;
    char     player_name[9];        /* FF6C..FF73: the NAME.USR base name */
    uint8_t  town_map;              /* [C5] the town to return to, default 0x81 = Muralla */
    uint8_t  cur_map;               /* [C4] */
    uint8_t  jashiin_defeated;      /* [49] */
    TownFn   on_town;

    /* fixtures: the live copy of the map's A/B/C lists (they move) */
    Fixture  fix[256];
    int      nfix;
    uint8_t  fixture_anim;          /* 9F07 */

    /* the STDPLY player-record page, for the C00C patch conditions (6BFC) */
    uint8_t  page[256];

    /* the status / inventory screen (select.bin -> status.c, docs/TOWN.md §12).
     * fight.bin 7202 runs it on the menu key and warps to town when the
     * overlay leaves 8 (the Kioku Feather) in menu_result [FF4B]. */
    uint8_t  menu_key;              /* FF18 bit0 (Enter) */
    uint8_t  menu_debounce;         /* 9EF5 */
    uint8_t  menu_result;           /* FF4B */
    struct Status         *status;  /* set while the screen owns the frame loop */
    const struct TextFont *font;    /* font.grp, shared with the town and the shops */
    const struct ItemPics *pics;    /* itemp.grp icon sections */

    /* messages (7210/73E0): a box stays up for 32 frames */
    uint8_t  msg_timer;             /* 9EED */
    uint8_t  msg_box;               /* 9EEF */

    /* the 26-frame walk-in after a map transition (7C6E) */
    int      walk_in;               /* frames left */
    int      walk_in_x;             /* pixel x of the sprite */
    int      walk_in_dir;           /* +8 / -8 px per frame */

    /* port side */
    uint8_t  dirs;                  /* INT 61h AL: current direction bits */
    unsigned frame_no;              /* rendered frames */
    unsigned hazard_frames;         /* frames spent on hazard tiles (damage is stubbed) */
    PresentFn present;
    DoorFn    on_door;
    void     *user;
    char      message[64];          /* last game message ("Can't open this door.") */
};

void game_init(Game *g, const Map *m, const Tileset *t);
/* place the hero's top-left at map (col,row): the fight.bin entry model
 * (scroll_col = col-16, scroll_row = (row - row_bias) & 63, hero on screen (12, row_bias)) */
void game_place(Game *g, int col, int row, int face_left);
/* switch maps through a door (7DC1 scroll_to_entry) */
void game_enter(Game *g, const Map *m, const Tileset *t, int dest_col, int dest_row, int face_left);
/* one iteration of the 629C main loop (renders one frame, more while climbing) */
void game_step(Game *g);
/* the initial frame() call before the loop (6254) */
void game_first_frame(Game *g);

/* helpers shared with the renderer / tests */
int  game_hero_cell(const Game *g);                /* ring index of the hero's top-left */
int  game_hero_map_col(const Game *g);
int  game_hero_map_row(const Game *g);
int  game_win(const Game *g);                      /* ring index of screen (0,0) */
uint8_t game_ring_cell(const Game *g, int scr_col, int scr_row);   /* screen cell (28x19) */
int  game_passable_wall(const Game *g, uint8_t v);
int  game_passable_body(const Game *g, uint8_t v);
int  game_ring_index(const Game *g, uint8_t row, uint8_t col);     /* 6D6E ring_addr */
int  game_ring_add(int p, int delta);                              /* 6D82/6D8E wrap */
int  game_push_hero(Game *g, int left);      /* 66A5 / 684C try_move; 1 = blocked */
void game_knock_fall(Game *g);               /* 64A2: one row of fall after a knockback */
void game_message(Game *g, const char *text);/* 73E0: show a message box for 32 frames */
const char *fight_message(int idx);          /* the 9A1E text table */
/* fight.bin 9A1E message indices */
enum { MSG_GOLD50, MSG_GOLD100, MSG_GOLD500, MSG_GOLD1000, MSG_KEY, MSG_RECOVERED,
       MSG_RECOVERED_FULL, MSG_SHIELD_BROKEN, MSG_DOOR_LOCKED, MSG_BOX_EMPTY,
       MSG_HERO_CREST, MSG_RUZERIA, MSG_GLORY_CREST, MSG_PIRIKA, MSG_FERUZA,
       MSG_SILKARN, MSG_ENCHANT_SWORD, MSG_TOO_HOT, MSG_LION_KEY, MSG_COUNT };
/* 7C6E: start the 26-frame walk-in (face_left = enter from the right) */
void game_start_walk_in(Game *g, int face_left);

#endif
