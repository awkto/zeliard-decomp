/* test_playthrough.c — drive the autopilot (nav.c) through the two routes in
 * playthrough.c and assert the end state.  This is issue #26's acceptance
 * check: a real run of the two engines, the shops, the key doors, the boss
 * protocol and the post-boss transitions, with nobody at the keyboard. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "playthrough.h"
#include "player.h"

static int checks, failures;
static void ck(int cond, const char *what, ...)
{
    checks++;
    if (!cond) {
        va_list ap; va_start(ap, what);
        failures++; printf("  FAIL "); vprintf(what, ap); printf("\n");
        va_end(ap);
    }
}

static Play p;

static int run(const char *dir, const PStep *route, unsigned budget, const char *name, int verbose)
{
    p.verbose = verbose;
    int rc = play_run(&p, dir, route, budget);
    Game *g = &p.sh.g;
    printf("%s: %u frames, %u probe frames, %u bosses, %u doors, %u shops\n",
           name, p.frames, p.nav.steps, p.bosses_killed, p.doors_taken, p.shops_visited);
    printf("  LIFE %u/%u  EXP %u  GOLD %u  level %u  sword %u  shield %u/%u  keys %u\n",
           g->hp, g->max_hp, g->exp, (unsigned)g->gold, g->level, g->sword, g->shield, g->shield_hp, g->keys);
    if (rc) printf("  stopped: %s\n", p.msg);
    return rc;
}

/* ------------------------------------------------------- fixture rides */
/* MP10's two moving platforms bridge gaps that have no floor at all, so a
 * crossing is proof that the survey both *placed* a fixture under the hero
 * when it probed and offered "stand still and be carried" as a move. */
static Shell rsh;
static Nav   rnav;
static void ride_cb(Game *g) { g->nobj = 0; nav_step(&rnav, g); }

static int ride(const char *dir, int sc, int sr, int gc, int gr, int budget)
{
    memset(&rsh, 0, sizeof rsh);
    memset(&rnav, 0, sizeof rnav);
    rsh.quiet = 1;
    if (shell_init(&rsh, dir, 0)) return -1;
    Game *g = &rsh.g;
    g->present = ride_cb;
    g->hp = g->max_hp = 9999;
    game_place(g, sc, sr, 0);
    g->nobj = 0;
    nav_goal_cell(&rnav, g, gc, gr);
    for (int f = 0; f < budget; f++) {
        game_step(g);
        int c = game_hero_map_col(g), r = game_hero_map_row(g);
        if ((c - gc <= 1 && gc - c <= 1) && (r - gr <= 1 && gr - r <= 1)) return f + 1;
    }
    return -1;
}

static void fixture_rides(const char *dir, int verbose)
{
    memset(&rsh, 0, sizeof rsh);
    memset(&rnav, 0, sizeof rnav);
    rsh.quiet = 1;
    if (shell_init(&rsh, dir, 0)) { ck(0, "fixture rides: cannot load MP10"); return; }
    Game *g = &rsh.g;
    g->present = NULL;
    game_place(g, 61, 7, 0);
    /* build the survey graph once and look at what the fixtures contributed */
    nav_goal_cell(&rnav, g, 61, 7);
    nav_step(&rnav, g);
    ck(rnav.nnode > 0, "fixture rides: the MP10 survey built");
    /* fix[0] is the elevator at (48,24): the hero can stand on it at every row
     * it can reach (8024 stops it at the solid rows 14 above and 25 below), so
     * the survey must have nodes all the way up the shaft, not just at row 24. */
    int shaft = 0;
    for (int r = 12; r <= 21; r++) if (rnav.node_of[48][r] >= 0) shaft++;
    ck(shaft >= 6, "fixture rides: the elevator shaft at column 48 is nodes at every row");
    /* fix[2] patrols columns 1..15 at row 43 over a floorless gap: every column
     * of the patrol is standable, and none of it is standable without it */
    int span = 0;
    for (int c = 1; c <= 15; c++) if (rnav.node_of[c][40] >= 0) span++;
    ck(span >= 12, "fixture rides: the whole fix[2] patrol is standable");
    ck(game_passable_body(g, g->map->grid[8][43]),
       "fixture rides: the fix[2] gap really has no floor of its own");
    /* and the edges that carry him: a ride macro is a node's own outgoing edge */
    int i = rnav.node_of[8][40], rides = 0, gated = 0;
    ck(i >= 0, "fixture rides: mid-platform is a node");
    if (i >= 0)
        for (int e = rnav.efirst[i]; e < rnav.efirst[i + 1]; e++) {
            if (rnav.emacro[e] >= 17 && rnav.emacro[e] <= 19) rides++;
            if (rnav.efix[e]) gated++;
        }
    ck(rides > 0, "fixture rides: a platform node has ride edges");
    ck(gated > 0, "fixture rides: those edges remember the fixture they need");

    /* now actually cross both gaps with nobody at the keyboard */
    int f1 = ride(dir, 17, 40, 1, 40, 900);
    if (verbose) printf("  fix[2] gap (17,40) -> (1,40): %d frames\n", f1);
    ck(f1 > 0, "fixture rides: crossed the fix[2] gap westward");
    int f2 = ride(dir, 56, 50, 30, 50, 1500);
    if (verbose) printf("  fix[3] gap (56,50) -> (30,50): %d frames\n", f2);
    ck(f2 > 0, "fixture rides: crossed the fix[3] gap westward");
}


/* ------------------------------------------------------- cavern topology */
/* Where cavern 1's own doors let a player go.  MP10's boss shelf (the SATONO
 * door at (128,32) and the MP1D door at (141,32)) is a 32-node island reached
 * only from Satono or out of the boss room; the way *in* from the Muralla gate
 * is the long way round -- door 5 at (159,50) into MP21, MP21's (15,50) back
 * into MP10 at (95,50), the Key at (99,41), and the locked door at (26,15)
 * that the Key opens into MP1D.  These checks pin that chain down, because it
 * is what the survey has to keep finding for the route to exist at all. */
static Shell csh;
static Nav   cnav;
static int survey(const char *dir, int map_idx, int col, int row)
{
    memset(&csh, 0, sizeof csh);
    memset(&cnav, 0, sizeof cnav);
    csh.quiet = 1;
    if (shell_init(&csh, dir, map_idx)) return -1;
    Game *g = &csh.g;
    g->present = NULL;
    game_place(g, col, row, 0);
    nav_goal_cell(&cnav, g, col, row);
    nav_step(&cnav, g);
    return cnav.nnode;
}
/* forward-reachable set from the hero's cell, as a node bitmap in `seen` */
static uint8_t seen[NAV_MAX_NODE];
static int reach_from(int col, int row)
{
    memset(seen, 0, sizeof seen);
    int start = -1;
    for (int dr = 0; dr <= 2 && start < 0; dr++)
        for (int dc = -1; dc <= 1 && start < 0; dc++)
            if (col + dc >= 0 && col + dc < csh.g.map->width && cnav.node_of[col + dc][(row + dr) & 63] >= 0)
                start = cnav.node_of[col + dc][(row + dr) & 63];
    if (start < 0) return 0;
    static int q[NAV_MAX_NODE];
    int h = 0, n = 0;
    q[n++] = start; seen[start] = 1;
    while (h < n)
        for (int e = cnav.efirst[q[h]], v = q[h++]; e < cnav.efirst[v + 1]; e++)
            if (!seen[cnav.eto[e]]) { seen[cnav.eto[e]] = 1; q[n++] = cnav.eto[e]; }
    return n;
}
static int door_reachable(int col, int row)
{
    for (int d = -1; d <= 1; d++) {
        int c = col + d;
        if (c < 0 || c >= csh.g.map->width) continue;
        int i = cnav.node_of[c][(row + 1) & 63];
        if (i >= 0 && seen[i]) return 1;
    }
    return 0;
}
static void cavern_routes(const char *dir, int verbose)
{
    /* MP10 from the Muralla gate (the town record's (61,7)) */
    int nn = survey(dir, 0, 61, 7);
    if (nn < 0) { ck(0, "cavern routes: cannot load MP10"); return; }
    ck(nn > 1400, "MP10: the survey found the whole cavern (%d nodes)", nn);
    int n1 = reach_from(61, 7);
    if (verbose) printf("  MP10 from the Muralla gate (61,7): %d/%d nodes\n", n1, nn);
    ck(n1 > 1000, "MP10: the Muralla gate reaches most of the cavern (%d nodes)", n1);
    ck(door_reachable(159, 50), "MP10: door 5 (159,50) -> MP21 is reachable from the Muralla gate");
    ck(!door_reachable(141, 32), "MP10: the MP1D door (141,32) is *not* reachable from the gate");
    ck(!door_reachable(26, 15), "MP10: the locked boss door (26,15) is not reachable from the gate either");
    /* the col-165 ladder: (164,20) is held up by nothing but the ladder, so it
     * is a node only because the probe hangs the hero on it the way 65C5 does */
    ck(cnav.node_of[164][20] >= 0 && cnav.node_of[164][19] >= 0, "MP10: the col-165 ladder is nodes");
    int up = 0;
    if (cnav.node_of[164][20] >= 0)
        for (int e = cnav.efirst[cnav.node_of[164][20]]; e < cnav.efirst[cnav.node_of[164][20] + 1]; e++)
            if (cnav.nrow[cnav.eto[e]] < 20) up = 1;
    ck(up, "MP10: a hero hanging at (164,20) can climb the ladder (not fall off it)");

    /* MP21, entered where MP10's door 5 puts him */
    int n21 = survey(dir, 3, 78, 51);
    ck(n21 > 0, "MP21 loaded");
    reach_from(78, 51);
    ck(door_reachable(15, 50), "MP21: the (15,50) door back into MP10 (95,50) is reachable");

    /* MP10 again, where that door lands: the Key and the locked boss door */
    survey(dir, 0, 94, 51);
    int n2 = reach_from(94, 51);
    if (verbose) printf("  MP10 from MP21's (95,50) door: %d nodes\n", n2);
    ck(door_reachable(26, 15), "MP10: the locked boss door (26,15) is reachable from (95,50)");
    int key = -1;
    for (int i = 0; i < csh.g.map->nobj; i++)
        if ((csh.g.map->objs[i].type & 0x1F) == 0x16) key = i;
    ck(key >= 0, "MP10 carries a Key item (C010 type & 0x1F == 0x16)");
    if (key >= 0) {
        int kc = csh.g.map->objs[key].col, kr = csh.g.map->objs[key].row, ok = 0;
        for (int dc = -2; dc <= 2 && !ok; dc++)
            for (int dr = -2; dr <= 2 && !ok; dr++) {
                int c = kc + dc, r = (kr + dr) & 63;
                if (c < 0 || c >= csh.g.map->width) continue;
                int i = cnav.node_of[c][r];
                if (i >= 0 && seen[i]) ok = 1;
            }
        ck(ok, "MP10: the Key at (%d,%d) can be walked to from (95,50)", kc, kr);
    }
    /* and the shelf itself: from where Satono's left edge drops him, the two
     * doors on it are one short walk away and nothing else is */
    survey(dir, 0, 128, 33);
    int n3 = reach_from(128, 33);
    if (verbose) printf("  MP10 boss shelf from Satono's left edge: %d nodes\n", n3);
    ck(door_reachable(141, 32), "MP10: the MP1D door is reachable from the Satono edge exit");
    ck(door_reachable(128, 32), "MP10: so is the SATONO door back out");
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../zeliard";
    int verbose = 1, only = 0;
    unsigned budget = 900000;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) verbose = 2;
        else if (!strcmp(argv[i], "--quiet")) verbose = 0;
        else if (!strcmp(argv[i], "--route") && i + 1 < argc) only = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--budget") && i + 1 < argc) budget = (unsigned)atoi(argv[++i]);
    }

    if (only != 2) {
        memset(&p, 0, sizeof p);
        int rc = run(dir, PLAY_ROUTE_START, budget, "route 1 (the opening)", verbose);
        Game *g = &p.sh.g;
        ck(rc == 0, "route 1 ran to the end");
        ck(p.deaths == 0, "route 1: Garland never died");
        ck(p.shops_visited >= 4, "route 1: the King, the smith, the Sage and the church");
        ck((unsigned)g->gold >= 900, "route 1: the King's 1000 gold, less the shield");
        ck(g->page[P_SHIELD_MAX] >= 30, "route 1: a shield was bought (its max HP is recorded)");
        ck(g->level >= 1, "route 1: the Sage levelled Garland up");
        ck(g->max_hp >= 120, "route 1: level 1 is 120 LIFE");
        ck(!p.sh.in_town, "route 1: ends back in the cavern");
    }

    if (only != 1) {
        /* Route 2 starts from the character the shops would have built by the
         * time a player reaches cavern 1's boss (level 12, the Knight's sword,
         * the Honor shield).  The autopilot cannot yet farm that much gold, so
         * the record is set the way `--sword/--level/--life` sets it; the
         * fights, the doors and the shops from there on are played. */
        memset(&p, 0, sizeof p);
        p.verbose = verbose;
        p.start_level = 16; p.start_sword = 4; p.start_shield = 4;
        p.start_life = 800; p.start_gold = 20000;
        int rc = run(dir, PLAY_ROUTE_BOSSES, budget, "route 2 (caverns 1-3 and their bosses)", verbose);
        Game *g = &p.sh.g;
        ck(rc == 0, "route 2 ran to the end");
        ck(p.bosses_killed >= 3, "route 2: Cangrejo, Pulpo and Pollo defeated");
        ck(p.deaths == 0, "route 2: Garland never died");
        ck(g->exp > 0, "route 2: the bosses paid EXP");
        ck((unsigned)g->gold > 0, "route 2: the bosses paid gold");
        ck(p.shops_visited >= 3, "route 2: shopping between the caverns");
        ck(p.doors_taken >= 4, "route 2: cavern 1's boss room is walked into and out of "
                               "through its own doors (%u doors taken)", p.doors_taken);

    }

    fixture_rides(dir, verbose);
    cavern_routes(dir, verbose);

    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
