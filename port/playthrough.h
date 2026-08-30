/* playthrough.h — a scripted, autopilot-driven run of the whole game.
 *
 * The route is a list of objectives ("walk to Muralla's weapon shop and buy a
 * shield", "enter the door at map (141,32)", "kill the boss"); the *movement*
 * is not scripted at all — `nav.c` plays the game with the real physics.  The
 * point is a regression test that exercises every seam a real player crosses:
 * town -> cavern hand-off, cavern -> cavern doors, key doors, boss rooms, the
 * post-boss transition, the shops and the return trips. */
#ifndef ZEL_PLAYTHROUGH_H
#define ZEL_PLAYTHROUGH_H
#include "shell.h"
#include "nav.h"

typedef enum {
    P_END = 0,
    P_TOWN_SHOP,    /* walk to town column a, enter the shop, drive its menus */
    P_TOWN_CAVE,    /* walk to town column a and take the cave door there */
    P_TOWN_EDGE,    /* walk off the a-side edge (0 = left, 1 = right) */
    P_CAV_DOOR,     /* reach the C00A door at (a,b) and go through it */
    P_CAV_CELL,     /* reach map cell (a,b) — treasure, keys, the boss reward */
    P_CAV_BREAK,    /* reach map cell (a,b) and swing until what stands on it is gone */
    P_BOSS,         /* fight the boss in this room until it is defeated */
    P_FARM,         /* patrol between map columns a and b until EXP reaches c */
    P_GOTO,         /* enter system map a at the door destination (b, c) */
    P_TOWN_GOTO     /* enter town a at column b */
} PKind;

typedef struct {
    PKind       kind;
    int         a, b;
    const char *menu;   /* P_TOWN_SHOP: one char per menu the shop opens —
                         * '0'..'9' = pick that row, 'c' = cancel, 'y' = Yes,
                         * 'n' = No; the driver pages the text itself */
    const char *what;
    int         c;      /* P_FARM: the EXP to stop at */
} PStep;

typedef struct {
    Shell    sh;
    Nav      nav;
    const PStep *route;
    int      step;
    unsigned frames, budget;
    unsigned step_start;
    /* per-step bookkeeping */
    const Map *step_map;
    int      town_at_start;
    int      shop_seen;
    int      menu_pos, menu_press;
    int      key_gate;
    int      farm_end;
    int      boss_seen;
    int      item_obj;      /* P_CAV_CELL: the C010 slot standing on the cell */
    int      done, failed;
    char     msg[256];
    int      verbose;
    /* the character the route starts from (0 = whatever STDPLY.BIN says).
     * Route 2 uses this because the autopilot cannot yet farm the gold the
     * shops want for a boss-worthy sword; everything else it plays. */
    int      start_level, start_sword, start_shield, start_life;
    long     start_gold;
    /* the report */
    unsigned bosses_killed, doors_taken, shops_visited, deaths;
    int      entry_col, entry_scroll;   /* where the castle put the hero on frame 0 */
} Play;

/* run `route` to the end (or until the frame budget runs out).
 * Returns 0 when every step completed. */
int  play_run(Play *p, const char *dir, const PStep *route, unsigned budget);
/* run another route on the shell/player record the last one left behind */
int  play_continue(Play *p, const PStep *route, unsigned budget);
/* the two routes test_playthrough.c drives */
extern const PStep PLAY_ROUTE_START[];
extern const PStep PLAY_ROUTE_BOSSES[];

#endif
