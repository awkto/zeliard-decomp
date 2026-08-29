/* town.h — town.bin (ZELRES1[6] @6000) and the town maps.
 *
 * A different engine from the caverns: the map is a raw width x 8 grid, the
 * hero occupies two columns of rows 5-7, and there is no ring buffer, no
 * gravity and no combat.  Sources: src/town.c, docs/TOWN.md, tools/mdt2png.py
 * (--town).  Addresses in the comments are town.bin / gtmcga offsets. */
#ifndef ZEL_TOWN_H
#define ZEL_TOWN_H
#include <stdint.h>
#include <stddef.h>
#include "gfx.h"
#include "physics.h"

#define TOWN_ROWS      8
#define TOWN_MAX_W     384
#define TOWN_NPC_ROW   5            /* the row NPC markers live on (6C2B) */
#define TOWN_GROUND    7            /* the row walkability is read from (686E) */
#define TOWN_MARK      0xFD         /* NPC marker written into the grid */
#define TOWN_SCR_COLS  28           /* screen column c shows map column scroll_col + 4 + c */

typedef struct { uint8_t flags, dest, gfx, tileset; } TownExit;   /* C007, 4 bytes, no terminator */
typedef struct { uint16_t col; uint8_t dest; } TownDoor;          /* C009, 3 bytes, FFFF ends */
typedef struct { uint16_t col; uint8_t row, side, map; } TownCave;/* C00B, 5 bytes, indexed */
typedef struct {                                                  /* C00F, 8 bytes, FFFF ends */
    uint16_t col; uint8_t sprite, saved, anim, type, flags, script;
} TownNpc;

typedef struct {
    int       width;
    uint8_t   grid[TOWN_MAX_W][TOWN_ROWS];
    uint8_t   music, gfx, town_flags, tileset, town_id;
    uint16_t  start_col;                        /* C013, used by the death return (99F4) */
    char      label[24];
    TownExit  exits[8];   int nexits;
    TownDoor  doors[16];  int ndoors;
    TownCave  caves[8];   int ncaves;
    TownNpc   npcs[32];   int nnpcs;
    uint16_t  range_min, range_max;             /* C011: walkers turn around here */
    uint16_t  patches;                          /* C015 */
    uint16_t  dlg_ptr[48]; int ndlg;            /* C00D: 0xFF-terminated scripts */
    uint8_t  *raw; size_t rawlen;
    int       index;                            /* 0 cmap .. 9 esmp; cur_map = 0x80 | index */
} TownMap;

/* cpat/mpat/dpat.grp (ZELRES2[33..35]): {u16 6, u16 off_block, u16 off_anim,
 * u8 type[], ..., 48-byte cells from 0x100} — docs/TOWN.md §4.1 */
typedef struct {
    Cell8   cell[256];
    uint8_t present[256];
    uint8_t sky[256][8][8];                     /* row 0-2 cells show the backdrop here */
    uint8_t block[16]; int nblock;              /* ground cells that stop the hero (686E) */
    uint8_t anim_from[32], anim_to[32]; int nanim;
    int     index;
} TownTiles;

/* mman/cman.grp (ZELRES2[29..30]): 5 sprites x 8 frames x 6 cell indices
 * (1-based), then 48-byte cells from 0x100 (docs/TOWN.md §4.2) */
typedef struct { uint8_t frame[5][8][6]; Cell2 cell[256]; int ncells; int index; } TownSprites;
/* tman.grp (ZELRES2[31]): 46 plain hero cells */
typedef struct { Cell2 cell[64]; int ncells; } TownHero;

int  town_load_map(TownMap *m, const char *dir, int index);
void town_free_map(TownMap *m);
int  town_apply_patches(TownMap *m, const uint8_t page[256]);   /* 6AED */
int  town_load_tiles(TownTiles *t, const char *dir, int index);
int  town_load_sprites(TownSprites *s, const char *dir, int index);
int  town_load_hero(TownHero *h, const char *dir);
const char *town_dialogue(const TownMap *m, int script);

typedef struct Town Town;
typedef void (*TownPresentFn)(Town *t);
struct Shop;
struct TextFont;

/* what a frame asked the shell to do (the original jumps out of the loop) */
enum { TOWN_NONE = 0, TOWN_TO_CAVERN, TOWN_TO_TOWN, TOWN_SHOP, TOWN_PAST_DOOR };

struct Town {
    TownMap     *map;
    TownTiles   *tiles;
    TownSprites *spr;
    TownHero    *hero;
    Game        *g;                 /* the shared player record (gold, hp, keys, page) */

    int      scroll_col;            /* [80] */
    int      hero_scr_col;          /* [83] 0..0x1B; -1 / 0x1C = off the left / right edge */
    uint8_t  hero_flags;            /* [C2] bit0 facing left */
    uint8_t  hero_anim;             /* [E7] 0..3 walk, 4 = back view */
    uint8_t  was_idle;              /* 7C4B */
    uint8_t  dirs, buttons, btn1_edge, btn2_edge;
    uint8_t  sfx_request;           /* FF75 */
    unsigned frame_no;
    char     message[512];          /* the dialogue text the box would show */
    int      action, action_arg;    /* TOWN_* */
    TownPresentFn present;
    void    *user;
    /* the shop overlay currently swapped into A000 (town.bin 6E7E) — while it
     * is set the shop owns the screen and the frame loop */
    /* the dialogue box (town.bin dialogue_run 63C5) while it is up: the box
     * geometry and the lines currently visible in it.  The renderer replays
     * them with the proportional font every frame. */
    struct {
        int  active, x8, y, w4, h, marker, nvis;
        char line[8][80];
        /* the yes/no widget dialogue opcodes 0x81 / 0x89 open (6655 / 66AD):
         * town.bin's own 74D3 yes_no_prompt, which the shops call through
         * vector [6008].  Only the geometry differs between the two. */
        int  menu_n, menu_row, menu_x4, menu_y, menu_w4, menu_h;
        const char *menu_item[2];
    } dlg;
    int      dlg_forced;            /* 7C54: an auto-talk script cannot be cancelled */
    int      quit;                  /* port: the shell wants out of a blocking box */

    struct Shop *shop;
    /* select.bin, swapped in on Enter (town.bin 68F3); while it is set the
     * status screen owns the screen and the frame loop */
    struct Status *status;
    uint8_t  menu_key;              /* FF18 bit0 (Enter) */
    uint8_t  menu_debounce;
    const struct ItemPics *pics;    /* itemp.grp, shared with the status screen */
    const struct TextFont *font;    /* font.grp, shared with the shops */
    const char  *dir;               /* the game directory, for the overlays */
};

void town_init(Town *t, TownMap *m, TownTiles *ti, TownSprites *s, TownHero *h, Game *g);
/* 7DE1: put the hero on map column `col` (scroll clamped to the map ends) */
void town_place(Town *t, int col, int face_left);
/* one iteration of the 61FC main loop; sets t->action when it wants to leave */
void town_step(Town *t);
int  town_hero_col(const Town *t);                  /* scroll_col + hero_scr_col + 4 */
int  town_cell_walkable(const Town *t, uint8_t v);  /* 686E */
void town_npc_update(Town *t);                      /* 6B1C */
void town_npc_markers_reset(Town *t);               /* 6C2B */
/* 635A/63C5: open the box for `script` and run it to the end.  Blocks on
 * t->present exactly as the original blocks on its frame loop. */
void town_dialogue_run(Town *t, int script, int face_left);
/* render one 320x200 MCGA frame (playfield rows at y 78..141) */
void town_render(uint8_t *fb, const Town *t);

#endif
