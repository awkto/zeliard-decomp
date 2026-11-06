/* shell.h — the two-engine shell: everything that owns resources and drives
 * the hand-offs between fight.bin (caverns) and town.bin (towns).
 *
 * This is the part of the original that lives *outside* both engines: GAME.BIN
 * keeps the map, tileset, AI overlay and enemy bank loaded and swaps the two
 * engine overlays over the same BASE:0000 player record (docs/ARCHITECTURE.md).
 * `main.c` (SDL / headless) and `playthrough.c` (the autopilot) are two front
 * ends over the same shell, so both take exactly the same code path through a
 * door, a town gate, a shop or a boss room. */
#ifndef ZEL_SHELL_H
#define ZEL_SHELL_H
#include <stdint.h>
#include "map.h"
#include "gfx.h"
#include "physics.h"
#include "enemy.h"
#include "town.h"
#include "text.h"
#include "status.h"
#include "tear.h"

typedef struct Shell {
    const char *dir;
    Map       maps[2];
    Tileset   tiles[2];
    int       cur;                      /* which maps[]/tiles[] slot is live */
    HeroGfx   hero;
    AiOverlay ai;
    EnemyGfx  egfx;
    DigitFont font;
    ScreenFrame frame;                  /* mole.bin: the boot-time screen furniture */
    EncounterCard encnt;                /* encnt.grp: the boss-room card */
    CaveMouth   mouth;                  /* roka.grp: the walk-in backdrop */
    /* the town side of the loop (docs/TOWN.md) */
    TownMap     tmap;
    TownTiles   ttiles;
    TownSprites tspr;
    TownHero    thero;
    TownBackdrop tback;  int town_back_idx;
    Town        town;
    TextFont    tfont;
    ItemPics    pics;
    TearArt     tear_art;               /* rokademo + GAME.BIN A3A5 art */
    int      in_town;                   /* 0 = fight.bin, 1 = town.bin */
    /* the frame a town entry runs before the 61FC loop: 1 = 61B1 (68AC),
     * 2 = the death return's 6EE6 (68AF) after kenjpro's [A004] */
    int      town_entry;
    int      town_walk_in;          /* town.bin [7C44]: 61B4's walk-in pending (6092) */
    int      town_tiles_idx, town_spr_idx;
    int      ai_index, enp_index;
    Game     g;
    int      quiet;                     /* 1 = do not log transitions */
    /* the front end */
    void         *user;
    PresentFn     present;              /* installed as g.present */
    TownPresentFn town_present;         /* installed as town.present */
    FrameEndFn    frame_end;            /* installed as g.frame_end (--trace; NULL normally) */
    TownFrameEndFn town_frame_end;      /* installed as town.frame_end */
    /* called once per shell_frame right after the engine's step completes (the
     * true end of a frame, after the mid-frame 717F/68ED boundary's own tail)
     * and before any transition the frame asked for.  Used by --state-save-at
     * (#52); NULL normally. */
    void        (*frame_done)(struct Shell *s);
    /* set by the shell for the front end / the tests */
    unsigned  transitions;              /* doors + gates taken */
} Shell;

/* The game folder, in the order `--dir`, the config's remembered folder (set
 * with shell_set_config_dir), then zeliard/, ../zeliard/, ./.  A candidate only
 * counts when ZELRES1-3.SAR are all there (game_fopen: upper- or lower-case).
 * Returns NULL when none of them has the files. */
const char *shell_find_dir(const char *hint);
/* The folder `config.c` remembered, consulted by shell_find_dir between the
 * --dir hint and the built-in probes.  NULL or "" clears it. */
void shell_set_config_dir(const char *dir);
/* #56: an extra candidate after the config's folder: <pref path>/game. */
void shell_set_data_dir(const char *dir);
/* load system map `map_idx` into slot 0, the fonts, the hero and item art, and
 * game_init() the player record (STDPLY.BIN).  Returns 0 on success. */
int  shell_init(Shell *s, const char *dir_hint, int map_idx);
/* free everything shell_init and the later map/town/AI/bank loads acquired;
 * safe on a zeroed Shell, and before a re-init of the same Shell */
void shell_free(Shell *s);
/* the two hand-offs; both are what the engines call through g.on_town / on_door */
int  shell_enter_town(Game *g, int town_index, int col, int died);
/* `from_cave_record` = 1 for town.bin 6FF8's own MAP_CAVES arithmetic (the
 * record's row is relative to a hard-coded screen row of 10, not to the map's
 * [C016] row_bias); 0 when `row` is already the hero's map row. */
int  shell_enter_cavern(Shell *s, int sys_map, int col, int row, int face_left,
                        int from_cave_record);
/* fight.bin 7EBB / 6117: (re)load the level record's AI overlay and enemy bank */
void shell_load_enemy_banks(Shell *s, const Map *m);
/* one iteration of whichever engine is live (town.bin 61FC or fight.bin 629C) */
void shell_frame(Shell *s);

#endif
