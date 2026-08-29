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
        p.start_level = 12; p.start_sword = 4; p.start_shield = 4;
        p.start_life = 600; p.start_gold = 20000;
        int rc = run(dir, PLAY_ROUTE_BOSSES, budget, "route 2 (caverns 1-3 and their bosses)", verbose);
        Game *g = &p.sh.g;
        ck(rc == 0, "route 2 ran to the end");
        ck(p.bosses_killed >= 3, "route 2: Cangrejo, Pulpo and Pollo defeated");
        ck(p.deaths == 0, "route 2: Garland never died");
        ck(g->exp > 0, "route 2: the bosses paid EXP");
        ck((unsigned)g->gold > 0, "route 2: the bosses paid gold");
        ck(p.shops_visited >= 3, "route 2: shopping between the caverns");

    }

    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
