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

    /* The long haul: the Muralla gate to door 5 at (159,50) is a full lap of
     * MP10's 240-column ring, about three hundred macros, and it crosses the
     * fix[5] platform at columns 118-127 on ring row 0 -- the gap in the row-0
     * floor, which stops at column 117 and picks up again at 130.  That leg is
     * what the navigator used to lose every time (issue #28): 82B4's carry
     * window was a column out, so a rightward ride walked the hero off the
     * platform's leading end, and `step_walks_off` read his *current* support
     * cell instead of the one he was stepping onto, so nothing caught it. */
    int f3 = ride(dir, 61, 7, 158, 51, 40000);
    if (verbose) printf("  the Muralla gate (61,7) -> door 5 (159,50): %d frames\n", f3);
    ck(f3 > 0, "fixture rides: walked the whole Muralla gate -> door 5 lap unaided");
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
static int cell_reachable(int col, int row)
{
    int i = (col >= 0 && col < csh.g.map->width) ? cnav.node_of[col][row & 63] : -1;
    return i >= 0 && seen[i];
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

    /* --- cavern 2 and cavern 3 are built to the same plan (issue #28) ----
     * The boss room is entered through a LOCKED door and left through an
     * unlocked one onto a shelf, and the Key for the locked door is lying in
     * the cavern.  Route 2 walks cavern 2's half of that now. */
    /* Where cavern 2 stops.  MP20's row-36 corridor -- and with it the
     * column-157 elevator, the second Key and the whole descent to the boss
     * door -- hangs off one door, (146,35) <-> MP21 (66,35).  From Satono's
     * right edge the only doors in reach are the way back out and the *locked*
     * (95,35), which lands in a pocket of MP21 that reaches nothing else.
     * (This used to read the other way round: the survey believed an elevator
     * could be anywhere in its shaft, and MP20's fix[4] appeared to bridge the
     * row-42 pocket to the corridor -- but an elevator only moves while the
     * hero rides it, so that edge could never be taken.  See port/README.md.) */
    int n20 = survey(dir, 2, 6, 62);
    ck(n20 > 1400, "MP20: the survey found the cavern (%d nodes)", n20);
    reach_from(6, 62);
    ck(door_reachable(95, 35), "MP20: the locked (95,35) into MP21 is reachable from Satono's right edge");
    ck(!door_reachable(171, 54), "MP20: the LOCKED boss door (171,54) is *not*");
    ck(!door_reachable(190, 47), "MP20: nor is the boss room's exit shelf (190,47)");
    ck(!door_reachable(205, 47), "MP20: nor the locked (205,47) door on it");
    ck(!door_reachable(146, 35), "MP20: nor the (146,35) door into MP21's east half");
    /* ... and from where that door lands, all of it is */
    survey(dir, 2, 145, 36);
    reach_from(145, 36);
    ck(door_reachable(171, 54), "MP20: the boss door is reachable from (145,36), where (146,35) lands");
    ck(cell_reachable(149, 43), "MP20: so is the second Key's ledge at (149,43)");
    ck(cell_reachable(155, 36) && cell_reachable(110, 36),
       "MP20: and the whole row-36 corridor, from the column-157 elevator to the column-108 ladder");
    /* MP21's own east half has no way in either: nothing that any of its four
     * doors lands on can reach (65,36), the cell MP20's (146,35) door arrives
     * at.  That is the one leg route 2 still asks the shell for. */
    survey(dir, 3, 14, 36);
    reach_from(14, 36);
    ck(!door_reachable(66, 35), "MP21: (66,35) is not reachable from (14,36), where MP20's (95,35) lands");
    survey(dir, 3, 14, 51);
    reach_from(14, 51);
    ck(!door_reachable(66, 35), "MP21: nor from (14,51), where MP10's (95,50) lands");
    survey(dir, 3, 78, 51);
    reach_from(78, 51);
    ck(!door_reachable(66, 35), "MP21: nor from (78,51), where MP10's door 5 lands");
    /* MP20's two Keys: one on the entrance side at (89,44) for the locked
     * (95,35), one in the corridor at (149,44) for the locked boss door. */
    survey(dir, 2, 6, 62);
    reach_from(6, 62);
    int k20a = -1, k20b = -1;
    for (int i = 0; i < csh.g.map->nobj; i++)
        if ((csh.g.map->objs[i].type & 0x1F) == 0x16) { if (k20a < 0) k20a = i; else k20b = i; }
    ck(k20a >= 0 && k20b >= 0, "MP20 carries two Key items");
    if (k20b >= 0) {
        ck(csh.g.map->objs[k20a].col == 89 && csh.g.map->objs[k20a].row == 44,
           "MP20: the first Key is at (%d,%d)", csh.g.map->objs[k20a].col,
           csh.g.map->objs[k20a].row);
        ck(csh.g.map->objs[k20b].col == 149 && csh.g.map->objs[k20b].row == 44,
           "MP20: the second Key is at (%d,%d)", csh.g.map->objs[k20b].col,
           csh.g.map->objs[k20b].row);
        ck(cell_reachable(88, 43), "MP20: the first Key is on the entrance side");
    }
    /* MP31 (system map 6) is the same shape, and its way in is Bosque's
     * column-7 door -- the one the sentry at column 9 stands in front of. */
    int n31 = survey(dir, 6, 149, 14);
    ck(n31 > 1000, "MP31: the survey found the cavern (%d nodes)", n31);
    reach_from(149, 14);
    ck(door_reachable(188, 20), "MP31: the LOCKED boss door (188,20) is reachable from Bosque's column-7 door");
    ck(!door_reachable(174, 4), "MP31: the boss room's exit shelf (174,4) is not");
    /* And the crest tree the sentry's flag comes from: MP30 (166,54), an item
     * state 0x10 (8E32) that only opens to the blade, holding a state 0x1D
     * (907F) Hero's Crest whose own +B/+D pair is `page[12] |= 8`.  Only MP30
     * (113,7) -- four doors from where MP20's (205,47) lands -- reaches it. */
    survey(dir, 5, 113, 7);
    reach_from(113, 7);
    ck(cell_reachable(164, 54), "MP30: the crest tree's ledge is reachable from (113,7)");
    {
        const MapObj *tree = NULL;
        for (int i = 0; i < csh.g.map->nobj; i++)
            if (csh.g.map->objs[i].col == 166 && csh.g.map->objs[i].row == 54) tree = &csh.g.map->objs[i];
        ck(tree != NULL, "MP30 has an object at (166,54)");
        if (tree) {
            ck((tree->type & 0x1F) == 0x10, "and it is item state 0x10, the sword-breakable (type %02X)", tree->type);
            ck((tree->next & 0x1F) == 0x1D, "whose `next` is the Hero's Crest, state 0x1D (next %02X)", tree->next);
            ck((tree->flags & 0x20) && (tree->home_col & 0xFF) == 0x12 && tree->home_row == 0x08,
               "and whose event pair is page[12] |= 08 (flags %02X, [%02X] |= %02X)",
               tree->flags, tree->home_col & 0xFF, tree->home_row);
        }
    }
    survey(dir, 5, 20, 7);
    reach_from(20, 7);
    ck(!cell_reachable(164, 54), "MP30: and *not* from (20,7), where MP20's (205,47) lands");
    /* and cavern 3's Key is in MP30 (map 5), on the far side of MP31's own
     * (153,43) door -- not on the half MP20's (205,47) lands on */
    survey(dir, 5, 21, 6);
    reach_from(21, 6);
    int k30 = -1;
    for (int i = 0; i < csh.g.map->nobj; i++)
        if ((csh.g.map->objs[i].type & 0x1F) == 0x16) k30 = i;
    ck(k30 >= 0 && csh.g.map->objs[k30].col == 133,
       "MP30 carries a Key, at column %d", k30 >= 0 ? csh.g.map->objs[k30].col : -1);
    ck(!cell_reachable(133, 54), "MP30: it is out of reach of the (21,6) door MP20 lands on");
    survey(dir, 5, 153, 44);
    reach_from(153, 44);
    ck(cell_reachable(133, 54), "MP30: and in reach of the (153,43) door from MP31");

    /* --- a cave record's row is not the hero's map row (issue #28) -------
     * town.bin 7005 subtracts a hard-coded 10 from the record's row to make
     * [82], and fight.bin 7D2D pins the hero's *screen* row at the map's own
     * [C016] row_bias, so his map row is `row - 10 + row_bias`.  Every cavern
     * proper has row_bias 10 and the two agree; every boss room has 12, and
     * the one cave record in the game that names a boss room is Llama Town's
     * (27,13) for MP73 -- 13 - 10 + 12 = 15, the room's floor. */
    memset(&csh, 0, sizeof csh);
    csh.quiet = 1;
    if (shell_init(&csh, dir, 0) == 0) {
        csh.g.present = NULL;
        ck(csh.maps[csh.cur].row_bias == 10, "MP10's row_bias is 10");
        if (shell_enter_town(&csh.g, 7, 222, 0)) {
            const TownCave *c = NULL;
            for (int i = 0; i < csh.tmap.ncaves; i++)
                if (csh.tmap.caves[i].map == 0x15) c = &csh.tmap.caves[i];
            ck(c != NULL, "Llama Town has a cave record for MP73");
            if (c) {
                ck(c->col == 27 && c->row == 13, "and it reads (%d,%d)", c->col, c->row);
                shell_enter_cavern(&csh, c->map, c->col, c->row, c->side & 1, 1);
                ck(csh.g.map->row_bias == 12, "MP73's row_bias is %d", csh.g.map->row_bias);
                ck(game_hero_map_col(&csh.g) == 27 && game_hero_map_row(&csh.g) == 15,
                   "the record puts Garland on MP73's floor at (27,15), not in the air at "
                   "(27,13): he is at (%d,%d)",
                   game_hero_map_col(&csh.g), game_hero_map_row(&csh.g));
            }
        } else ck(0, "Llama Town loads");
    } else ck(0, "cave records: MP10 loads");
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
        /* Route 2 starts from a made-up character, and it has to: the record
         * it wants cannot exist this early in the game at all.
         *
         * A level is bought from a sage, and kenj's `A2AC` caps each sage by
         * town -- {3, 6, 9, 11, 13, 15, 18, 0xFF} for Muralla, Satono, Bosque,
         * Helada, Tumba, Dorado, Llama, Pureza/Esco.  The two sages this route
         * can reach stop at **level 6** (Satono) and **level 9** (Bosque, and
         * only once cavern 3 has been walked); level 16 needs *Llama Town's*
         * sage, four caverns further on.  So no amount of grinding produces a
         * level-16 Garland in caverns 1-3.
         *
         * The grind is out of reach too.  Route 1 measures the rate: its
         * `P_FARM` on MP10's entrance shelf earns **500 EXP and 51 gold in
         * 133,256 frames**, i.e. 267 frames an EXP point and 2,613 frames a
         * gold piece, and a frame is 84.5 ms (20 ticks of 236.7 Hz), so that
         * one step is already three hours of play.  `A28C`'s EXP_NEXT wants
         * 3,370 EXP to walk level 1 up to level 6 (~21 hours) and 220,370 to
         * reach 16 (~57 days); the 20,000 gold is another ~51 days.
         *
         * And level 6 is not enough anyway: with 320 LIFE (`A380`'s table) and
         * the best sword and shield Satono sells, Garland reaches MP2D and dies
         * to Pulpo.  So the record stays, and it stays documented. */
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
        ck(p.doors_taken >= 14, "route 2: every boss room is walked into and out of *twice* "
                                "through its own doors (%u doors taken)", p.doors_taken);
        /* the three Keys the boss rooms hand back on the second visit: two of
         * them are spent again on the spot (MP10's (128,32) into Satono and
         * MP20's (205,47) into MP30), so what is left at the end is Pollo's. */
        ck(g->keys == 1, "route 2: Pollo's Key is in the bag (%u keys)", g->keys);
        ck(g->page[0] == 0xFF && g->page[8] == 0xFF && g->page[0x10] == 0xFF,
           "route 2: 72F1's boss-defeated flags are set for all three rooms");
        /* cavern 2 is now played the way the game means it: off Satono's right
         * edge into MP20, the Key that lies at (149,44), and MP2D through its
         * own LOCKED door at (171,54) -- 72F1's flag for that door is [0B] */
        ck(p.sh.g.page[0x0B] != 0, "route 2: MP20's locked boss door was opened with a Key");
        ck((g->page[3] & 0x40) && (g->page[0x0B] & 0x20),
           "route 2: both locked doors a boss Key opened stay unlocked (page[03]=%02X page[0B]=%02X)",
           g->page[3], g->page[0x0B]);

    }

    fixture_rides(dir, verbose);
    cavern_routes(dir, verbose);

    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
