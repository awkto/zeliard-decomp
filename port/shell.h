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
    int      town_tiles_idx, town_spr_idx;
    int      ai_index, enp_index;
    Game     g;
    int      quiet;                     /* 1 = do not log transitions */
    /* the front end */
    void         *user;
    PresentFn     present;              /* installed as g.present */
    TownPresentFn town_present;         /* installed as town.present */
    /* set by the shell for the front end / the tests */
    unsigned  transitions;              /* doors + gates taken */
} Shell;

/* zeliard/, ../zeliard/ or ./ unless `hint` names one */
const char *shell_find_dir(const char *hint);
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
