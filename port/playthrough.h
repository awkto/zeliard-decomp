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
    P_TOWN_GOTO,    /* enter town a at column b */
    P_KEYS          /* --pilot only: feed `menu` as literal --script tokens (#87) */
} PKind;

typedef struct {
    PKind       kind;
    int         a, b;
    const char *menu;   /* P_TOWN_SHOP: one char per menu the shop opens —
                         * '0'..'9' = pick that row, 'c' = cancel, 'y' = Yes,
                         * 'n' = No; the driver pages the text itself.
                         * P_KEYS: the --script tokens to feed */
    const char *what;
    int         c;      /* P_FARM: the EXP to stop at */
} PStep;

typedef struct {
    Shell    sh;        /* play_run's own shell */
    Shell   *s;         /* the shell being driven: &sh, or the caller's (play_attach) */
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
    /* what the P_BOSS steps (the boss room alone) changed vs what the
     * overlays' [A002] blocks promise (+5 EXP, +B almas: 71E8 / 71F2) */
    long     boss_exp_paid, boss_exp_due, boss_almas_paid, boss_almas_due, boss_gold_paid;
    long     step_exp, step_almas, step_gold;
    int      entry_col, entry_scroll;   /* where the castle put the hero on frame 0 */
    unsigned last_deaths;
    int      pilot;     /* play_attach: input is asked for per boundary (play_pilot) */
    int      keys_pos, keys_left, keys_done;    /* P_KEYS: where in the token string */
    uint8_t  keys_dirs, keys_btns;
} Play;

/* run `route` to the end (or until the frame budget runs out).
 * Returns 0 when every step completed. */
int  play_run(Play *p, const char *dir, const PStep *route, unsigned budget);
/* run another route on the shell/player record the last one left behind */
int  play_continue(Play *p, const PStep *route, unsigned budget);
/* --pilot (main.c): drive the caller's shell, which is already set up and
 * placed.  play_pilot is called once per harness boundary and answers the
 * held directions / buttons (bit0 X, bit1 M) for the next frame without
 * touching the engine's input state: 0 running, 1 the route is done, -1 it
 * failed (p->msg says why). */
int  play_attach(Play *p, Shell *sh, const PStep *route, unsigned budget);
int  play_pilot(Play *p, uint8_t *dirs, uint8_t *btns);
/* "town-cave C; town-shop C [MENU]; town-edge 0|1; door C,R | door -1;
 *  cell C,R; break C,R; boss; keys TOKENS" -> PStep[] with P_END; returns the
 *  count or -1.  `keys` feeds TOKENS (--script syntax: U/D/L/R, X, M, '.', a
 *  count) verbatim, for the odd move the autopilot cannot find (#87). */
int  play_parse_route(const char *s, PStep *out, int max, char *err, size_t errlen);
/* the two routes test_playthrough.c drives */
extern const PStep PLAY_ROUTE_START[];
extern const PStep PLAY_ROUTE_BOSSES[];

#endif
