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

typedef struct Game Game;

/* one rendered frame: main loop presents the frame, waits 4*speed ticks and
 * refreshes g->dirs.  Called once per frame() (ladder climbs render 1-2 per step). */
typedef void (*PresentFn)(Game *g);
/* door transition (docs/FIGHT.md §8): return 1 if the caller switched maps */
typedef int (*DoorFn)(Game *g, const Door *d);

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

#endif
