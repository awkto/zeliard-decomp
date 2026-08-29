/* nav.h — the playthrough autopilot.
 *
 * Not part of the original: this is the harness that *plays* the port, so a
 * whole run (town -> cavern -> boss -> town -> cavern ...) can be driven and
 * asserted without a human at the keyboard.
 *
 * The hard part of navigating a Zeliard cavern is that "can Garland get from
 * here to there" is a question about fight.bin's own movement rules — the
 * 3-step horizontal test, the 2-row rise, the one-cell-gap walk-over, ladders,
 * elevators, patrolling platforms and the ring's wrap.  Rather than model any
 * of that a second time (every approximation was wrong somewhere), the
 * navigator *asks the engine*:
 *
 *   1. every cell the hero can stand on is a node;
 *   2. from each node it runs a dozen short button macros on a throw-away
 *      clone of the real Game with the enemies switched off, and whatever
 *      cell the hero settles on becomes an edge, tagged with the macro and its
 *      length.  The graph is therefore executable by construction;
 *   3. Dijkstra from the goal over the reversed graph gives the distance
 *      field, and playing is "run the macro on the cheapest outgoing edge,
 *      re-plan whenever the hero ends up somewhere else".
 *
 * Enemies, knockback and moving platforms are exactly what makes the hero end
 * up somewhere else, so the re-plan is the normal case, not the exception. */
#ifndef ZEL_NAV_H
#define ZEL_NAV_H
#include "physics.h"
#include "map.h"

#define NAV_INF      60000
#define NAV_MAX_NODE 24000
#define NAV_MAX_EDGE (NAV_MAX_NODE * 12)
#define NAV_FIXDIR   0x8000u    /* efixpos flag: the platform was moving left */

typedef enum { NAV_REACH, NAV_DOOR, NAV_FIGHT } NavMode;

typedef struct Nav {
    const Map *map;                 /* the map the graph was built for */
    NavMode  mode;
    int      goal_col, goal_row;    /* hero top-left target cell */
    int      door_col, door_row;    /* NAV_DOOR: the C00A door record's own cell */

    /* the verified graph */
    int      node_of[MAP_MAX_WIDTH][MAP_ROWS];   /* cell -> node index, -1 none */
    uint16_t ncol[NAV_MAX_NODE], nrow[NAV_MAX_NODE];
    int      nnode;
    int      eto[NAV_MAX_EDGE];     /* forward edges, grouped by source node */
    uint8_t  emacro[NAV_MAX_EDGE], ecost[NAV_MAX_EDGE];
    /* fixture rides: an edge probed with a moving platform / elevator put
     * under the hero is only executable while that fixture really is there.
     * efix = fixture index + 1 (0 = the edge needs no fixture), efixpos = the
     * column (kind 2) or row (kinds 0/1) it was probed at. */
    uint8_t  efix[NAV_MAX_EDGE];
    uint16_t efixpos[NAV_MAX_EDGE];   /* | NAV_FIXDIR when it was probed moving left */
    int      efirst[NAV_MAX_NODE + 1];
    int      nedge;
    uint16_t dist[NAV_MAX_NODE];    /* to the goal */
    uint16_t udist[NAV_MAX_NODE];   /* the same with the edges undirected */
    uint8_t  pen[MAP_MAX_WIDTH][MAP_ROWS];       /* anti-stuck cost bumps */
    int      built;

    /* execution */
    int      cur_macro, cur_frame, last_macro;
    int      expect_node;
    int      stall, same, last_col, last_row;
    int      fixwait;               /* frames spent waiting for a platform */
    uint16_t best_dist;

    /* scratch */
    Map      navmap;                /* private copy: probes may unlock doors */
    Game     probe;
    unsigned steps;                 /* simulated frames, for the report */
    unsigned builds;
} Nav;

void nav_goal_door(Nav *n, const Game *g, const Door *d);
void nav_goal_cell(Nav *n, const Game *g, int col, int row);
void nav_goal_fight(Nav *n, const Game *g);
/* set g->dirs / the button edges for this frame.  Call from g->present. */
void nav_step(Nav *n, Game *g);
/* the field distance at the hero's cell (NAV_INF when he is not on a node) */
int  nav_distance(Nav *n, const Game *g);
void nav_trace(Nav *n, const Game *g, int col, int row, int macro);

#endif
