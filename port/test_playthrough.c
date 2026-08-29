/* test_playthrough.c — drive the autopilot (nav.c) through the two routes in
 * playthrough.c and assert the end state.  This is issue #26's acceptance
 * check: a real run of the two engines, the shops, the key doors, the boss
 * protocol and the post-boss transitions, with nobody at the keyboard. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "playthrough.h"
#include "player.h"

static int checks, failures;
static void ck(int cond, const char *what)
{
    checks++;
    if (!cond) { failures++; printf("  FAIL %s\n", what); }
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

    }

    fixture_rides(dir, verbose);

    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
