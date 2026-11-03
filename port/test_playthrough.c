/* test_playthrough.c — drive the autopilot (nav.c) through the two routes in
 * playthrough.c and assert the end state.  This is issue #26's acceptance
 * check: a real run of the two engines, the shops, the key doors, the boss
 * protocol and the post-boss transitions, with nobody at the keyboard. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "sar.h"
#include "playthrough.h"
#include "player.h"
#include "town.h"
#include "savestate.h"
#include "statekeys.h"
#include "restore.h"
#include "text.h"

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
    shell_free(&rsh); memset(&rsh, 0, sizeof rsh);
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
    shell_free(&rsh); memset(&rsh, 0, sizeof rsh);
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
    shell_free(&csh); memset(&csh, 0, sizeof csh);
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
    /* How cavern 2 opens (issue #31).  MP20's row-36 corridor -- and with it
     * the column-157 elevator, the second Key and the whole descent to the
     * boss door -- hangs off one door, (146,35) <-> MP21 (66,35), and MP20's
     * own half never reaches either side of it.  The way in is the *locked*
     * (95,35), which the first Key opens, and then a lap of MP21 that leaves
     * by the ring's *column* wrap (port/README.md, "How MP21 crosses"). */
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
    /* MP21 is the hinge, and it is crossed by the ring's *column* wrap.  From
     * (14,36), where MP20's locked (95,35) lands, the way east is not east at
     * all: down the column-13 ladder into the bottom-left pit, west onto
     * fix[2] -- the platform that patrols row 61 between columns 1 and 14 --
     * out to its own west limit, and then a jump *left* off column 0, which
     * wraps to column 95 and lands on the row-59 shelf at (94,56).  From there
     * the far-east ladders climb to the row-39 level and the rows 33-36 ledge
     * carries the hero west to the (66,35) door.  Two things in the survey had
     * to change for it (nav.c): a ledge beside a patrolling platform is probed
     * with the platform going *both* ways, because the step west off the
     * ladder at (12,56) only lands on it while it is moving left; and there
     * are now two-frame "R2"/"L2" macros, because stepping onto the platform's
     * outermost cell at (0,58) takes exactly two frames -- one leaves the hero
     * where he started and four walk him off the end. */
    survey(dir, 3, 14, 36);
    int r21 = reach_from(14, 36);
    ck(door_reachable(66, 35), "MP21: (66,35) IS reachable from (14,36), where MP20's (95,35) lands");
    ck(cell_reachable(12, 56), "MP21: by way of the column-13 ladder at (12,56)");
    ck(cell_reachable(0, 58), "MP21: fix[2]'s west limit at (0,58)");
    ck(cell_reachable(94, 56), "MP21: and (94,56), the far side of the ring's column wrap");
    ck(cnav.node_of[0][58] >= 0 && cnav.node_of[94][56] >= 0, "MP21: both are nodes");
    /* the two halves are one component now: (65,36) is where MP20's other door
     * lands, and it sees exactly what (14,36) sees */
    survey(dir, 3, 65, 36);
    int r21b = reach_from(65, 36);
    ck(r21 == r21b, "MP21: the two halves are one component (%d nodes from (14,36), %d from (65,36))",
       r21, r21b);
    /* fix[2] is the only thing holding the pit's row-58 band up: the map has
     * no floor under it at all */
    ck(game_passable_body(&csh.g, csh.g.map->grid[1][61]) &&
       game_passable_body(&csh.g, csh.g.map->grid[13][61]),
       "MP21: fix[2]'s row-61 patrol runs over a gap with no floor of its own");
    /* cavern 1's own traffic through MP21 still cannot climb to the C doors:
     * the row-51 level is where MP10's two green (D) doors land, and the game
     * says so itself -- Satono's script 3, "if you fall down the stone slab in
     * front of the blue door you will see a green door nearby ... a doorway to
     * the past".  The slab is MP21's fix[0] (column 47, parked at row 36 flush
     * with the rows 33-36 ledge the blue door opens onto) and it only ever
     * goes down. */
    survey(dir, 3, 14, 51);
    reach_from(14, 51);
    ck(!door_reachable(66, 35), "MP21: (66,35) is NOT reachable from (14,51), where MP10's (95,50) lands");
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
    /* The ring's 64 rows are cyclic (6D6E/6D82) and so is an elevator's shaft:
     * 8024/80AF walk it with the wrapping ring helpers, so MP20's fix[0]
     * (column 102, home row 61) really runs rows 55..63 *and on into* 0..5 --
     * the top-of-map corridor whose floor is row 6, the same strip the entry
     * from Satono lands on at row 62 with his feet on row 1.  `fixture_rows`
     * clamped the walk at row 0/63 and lost six of the shaft's fifteen
     * positions, and fix[1] (column 124, home 61, bottom row 7 over the row-8
     * floor) lost eight. */
    ck(cnav.node_of[102][63] >= 0 && cnav.node_of[102][1] >= 0,
       "MP20: fix[0]'s shaft wraps past row 63 into rows 0-2");
    ck(cell_reachable(102, 63) && cell_reachable(102, 1),
       "MP20: and the hero can ride it round the wrap");
    ck(cnav.node_of[124][63] >= 0 && cnav.node_of[124][4] >= 0,
       "MP20: so does fix[1]'s, down to row 4 over the row-8 floor");
    /* Where cavern 2 stops, pinned to the cell (issue #31).  Three elevators
     * -- MP20's fix[1] (column 124, home row 61, shaft 57..7 through the row
     * wrap, floor row 8), MP20's fix[4] (column 157, home 39, shaft 35..45,
     * floor 46) and MP21's fix[0] (column 47, home 36, shaft 32..44, floor 45)
     * -- have exactly the same shape: the record row is flush with a ledge
     * above, and the *bottom* of the shaft is exactly one row above a floor
     * the hero already walks.  Every one of those floors is in reach from
     * Satono's right edge and every one of those platforms is not, because
     * 7FB1 only draws a fixture-A record: 7FDC/8074 are the only code that
     * moves one and both need the hero standing on it first.  So each is a
     * one-way drop out of the corridor until somebody has ridden it down --
     * and standing on any one of them opens all of MP20 and MP21's east half.
     * That is the whole of route 2's last `P_GOTO`. */
    survey(dir, 2, 6, 62);
    reach_from(6, 62);
    ck(cnav.node_of[123][4] >= 0 && !cell_reachable(123, 4),
       "MP20: fix[1]'s shaft bottom (123,4) is a node, and not reachable");
    ck(cell_reachable(122, 5), "MP20: ... one row under it the row-5 corridor is walked");
    ck(cnav.node_of[157][42] >= 0 && !cell_reachable(157, 42),
       "MP20: fix[4]'s shaft bottom (157,42) is a node, and not reachable");
    ck(cell_reachable(156, 43), "MP20: ... one row under it the second Key's pocket is walked");
    survey(dir, 2, 123, 4);
    reach_from(123, 4);
    ck(door_reachable(146, 35), "MP20: standing on fix[1] at its shaft bottom opens (146,35)");
    ck(door_reachable(171, 54), "MP20: ... and the LOCKED boss door (171,54) with it");
    ck(!door_reachable(205, 47),
       "MP20: (205,47) still needs the boss room's own exit shelf, as before");
    survey(dir, 3, 78, 51);
    reach_from(78, 51);
    ck(cnav.node_of[47][41] >= 0 && !cell_reachable(47, 41),
       "MP21: fix[0]'s shaft bottom (47,41) is a node, and not reachable from the lower level");
    ck(cell_reachable(46, 42), "MP21: ... one row under it the row-42 floor is walked");

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
    shell_free(&csh); memset(&csh, 0, sizeof csh);
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


/* ------------------------------------------------- the progression graph */
/* Caverns 1-3 as a graph of doors and Keys (issue #31).  Three things fall
 * out of the tables and they settle the order the game is meant to be played
 * in:
 *
 *  - **Seven locked doors, seven Keys.**  MP10 (26,15) and (128,32), MP20
 *    (95,35), (171,54) and (205,47), MP31 (188,20) and (192,4) are the only
 *    locked doors in caverns 1-3; four Keys lie in the maps (MP10 (99,41),
 *    MP20 (89,44) and (149,44), MP30 (133,55)) and the three boss rooms hand
 *    one back apiece on the second visit.  Every Key has exactly one door, and
 *    MP20's (95,35) -- the one the port could not use -- is in the list.
 *  - **Nothing outside MP20/MP21 opens into them but MP30's (21,6)**, and that
 *    is the far side of MP20's own locked (205,47).  So cavern 2's east half
 *    cannot be picked up from cavern 3 first: Bosque, the only other way to
 *    MP30/MP31, is itself only reachable *from* MP30 (185,18) or MP31
 *    (149,13).  The loop closes; the entrance side has to open it.
 *  - **A map's `[C013]` start record is its boss door**, not its entrance:
 *    774E measures the hero's distance to it and turns that into [FF08],
 *    and the death return at 99F4 reads the *town's* [C013], not a cavern's.
 *    MP10's is (26,16), MP20's (171,55), MP31's (188,21) -- each the cell a
 *    hero stands on to open the locked B (red) door into the boss room. */
static void cavern_progression(const char *dir, int verbose)
{
    static const struct { int idx; const char *name; } CAV[8] = {
        {0,"MP10"},{1,"MP1D"},{2,"MP20"},{3,"MP21"},{4,"MP2D"},{5,"MP30"},{6,"MP31"},{7,"MP3D"} };
    int locked = 0, keys = 0, towns = 0;
    uint32_t maskbits = 0;
    for (int i = 0; i < 8; i++) {
        Map m; memset(&m, 0, sizeof m);
        if (map_load_system(&m, dir, CAV[i].idx)) { ck(0, "%s loads", CAV[i].name); continue; }
        for (int d = 0; d < m.ndoors; d++) {
            const Door *o = &m.doors[d];
            if (!(o->letter & 0x80)) {
                locked++;
                ck(o->flag_ptr != 0xFFFF, "%s's locked (%d,%d) records the Key that opened it",
                   m.name, o->col, o->row);
                maskbits |= (uint32_t)o->flag_mask << (8 * ((o->flag_ptr & 0xFF) == 0x03 ? 0 :
                                                           (o->flag_ptr & 0xFF) == 0x0B ? 1 : 2));
            }
            if (o->dest_row == 0xFF) towns++;
        }
        for (int k = 0; k < m.nobj; k++) if ((m.objs[k].type & 0x1F) == 0x16) keys++;
        map_free(&m);
    }
    if (verbose) printf("  caverns 1-3: %d locked doors, %d Keys in the maps, %d town doors\n",
                        locked, keys, towns);
    ck(locked == 7, "caverns 1-3 hold exactly seven locked doors (%d)", locked);
    ck(keys == 4, "and four Keys lying in the maps (%d)", keys);
    ck(__builtin_popcount(maskbits) == 7, "each locked door has its own story-flag bit");
    ck(towns == 5, "and five doors out to a town (%d): MP10's two, MP20's, MP30's and MP31's", towns);

    /* nothing in the whole game opens into MP20 or MP21 but their own doors
     * and MP30's (21,6), which is the far side of MP20's locked (205,47) */
    int in20 = 0, in21 = 0, foreign = 0;
    for (int i = 0; i <= 0x1E; i++) {
        Map m; memset(&m, 0, sizeof m);
        if (map_load_system(&m, dir, i)) continue;
        for (int d = 0; d < m.ndoors; d++) {
            const Door *o = &m.doors[d];
            if (o->dest_row == 0xFF) continue;
            if (o->dest_map == 2) { in20++; if (i != 2 && i != 3) { foreign++;
                ck(i == 5 && o->col == 21 && o->row == 6,
                   "the only foreign door into MP20 is MP30's (21,6) (%s (%d,%d))", m.name, o->col, o->row); } }
            if (o->dest_map == 3) { in21++; if (i != 2 && i != 3 && i != 0) foreign++; }
        }
        map_free(&m);
    }
    if (verbose) printf("  doors into MP20: %d, into MP21: %d, from outside cavern 2: %d\n",
                        in20, in21, foreign);
    ck(in20 == 3 && in21 == 4, "three doors lead into MP20 and four into MP21 (%d/%d)", in20, in21);
    ck(foreign == 1, "exactly one of them comes from outside cavern 2 (%d)", foreign);

    /* Bosque -- the only other way to MP30/MP31 -- hangs off MP30 and MP31
     * themselves, so cavern 3 cannot come before cavern 2's east half */
    {
        TownMap t; memset(&t, 0, sizeof t);
        ck(town_load_map(&t, dir, 3) == 0, "Bosque village loads");
        ck(t.ncaves == 2 && t.caves[0].map == 5 && t.caves[1].map == 6,
           "Bosque's cave table is MP30 and MP31");
        ck(t.nexits == 0, "and it has no edge exits at all -- nothing walks into Bosque");
        town_free_map(&t);
    }

    /* the header start record is the boss door */
    static const struct { int idx; int col, row; } BOSSDOOR[3] = { {0,26,15}, {2,171,54}, {6,188,20} };
    for (int i = 0; i < 3; i++) {
        Map m; memset(&m, 0, sizeof m);
        if (map_load_system(&m, dir, BOSSDOOR[i].idx)) continue;
        const Door *b = NULL;
        for (int d = 0; d < m.ndoors; d++)
            if (m.doors[d].col == BOSSDOOR[i].col && m.doors[d].row == BOSSDOOR[i].row) b = &m.doors[d];
        ck(b && !(b->letter & 0x80) && (b->letter & 7) == 1,
           "%s's (%d,%d) is a locked letter-B (red) door", m.name, BOSSDOOR[i].col, BOSSDOOR[i].row);
        ck(m.start_col == BOSSDOOR[i].col && m.start_row == BOSSDOOR[i].row + 1,
           "%s's [C013] start record (%d,%d) is that door's own standing cell",
           m.name, m.start_col, m.start_row);
        map_free(&m);
    }
}

/* #52: the realtime save state, exercised in-process -- a blob round-trips the
 * engine fields, and a garbage / truncated / bit-flipped blob is refused
 * without being trusted.  (The cross-run trace proof is
 * tools/harness/state_roundtrip.sh.) */
static void state_roundtrip_unit(const char *dir)
{
    static Shell ss;
    shell_free(&ss); memset(&ss, 0, sizeof ss);
    if (shell_init(&ss, dir, 0)) { ck(0, "state: shell_init for the save-state test"); return; }
    Game *g = &ss.g;
    if (!shell_enter_cavern(&ss, 0, 61, 7, 0, 0)) { ck(0, "state: entering MP10 for the save-state test"); shell_free(&ss); return; }
    g->hp = 123; g->gold = 4567; g->exp = 89; g->level = 3;
    g->dirs = DIR_RIGHT; g->ring[100] = 0xAB;
    const char *good = "/tmp/zel_state_unit.bin";
    ck(savestate_save(&ss, good, 42, 7) == 0, "state: a blob is written");

    g->hp = 0; g->gold = 0; g->level = 0; g->dirs = 0; g->ring[100] = 0;
    SavestateMeta meta;
    ck(savestate_load(&ss, good, &meta) == 0, "state: the blob loads");
    ck(meta.frame == 42 && meta.rng_calls == 7, "state: meta round-trips (%u, %u)",
       meta.frame, meta.rng_calls);
    ck(g->hp == 123 && g->gold == 4567 && g->level == 3 && g->dirs == DIR_RIGHT,
       "state: the player record round-trips (hp %u gold %u level %u dirs %u)",
       g->hp, (unsigned)g->gold, g->level, g->dirs);
    ck(g->ring[100] == 0xAB, "state: the ring round-trips (%02X)", g->ring[100]);

    /* a garbage file, a truncated copy and a flipped byte must all be refused */
    FILE *f = fopen("/tmp/zel_state_bad.bin", "wb");
    if (f) { fputs("this is not a state blob at all", f); fclose(f); }
    ck(savestate_load(&ss, "/tmp/zel_state_bad.bin", &meta) != 0, "state: a garbage file is refused");

    uint8_t blob[64 * 1024]; size_t n = 0;
    f = fopen(good, "rb");
    if (f) { n = fread(blob, 1, sizeof blob, f); fclose(f); }
    f = fopen("/tmp/zel_state_trunc.bin", "wb");
    if (f) { fwrite(blob, 1, n / 2, f); fclose(f); }
    ck(savestate_load(&ss, "/tmp/zel_state_trunc.bin", &meta) != 0, "state: a truncated blob is refused");

    if (n > 24) blob[20] ^= 0x01;                   /* a byte inside the payload */
    f = fopen("/tmp/zel_state_flip.bin", "wb");
    if (f) { fwrite(blob, 1, n, f); fclose(f); }
    ck(savestate_load(&ss, "/tmp/zel_state_flip.bin", &meta) != 0, "state: a bit-flipped blob is refused");

    shell_free(&ss);
}

/* #42 step 2b: F7 "Restore Game", the dialog and the commit, in-process.  The
 * flow the original runs: [11E] "Sure?(Y/N)" -> town.bin 7695's name box and
 * *.usr list -> 7592 reloads NAME.USR and re-enters the town the page names at
 * the saved column.  Saves a file in the game folder and removes it again. */
static void type_name(Restore *r, Shell *s, const char *t)
{
    for (; *t; t++) restore_key(r, s, RK_CHAR, *t);
}
static void restore_unit(const char *dir)
{
    static Shell rs; static Restore r; static uint8_t fb[320 * 200], fb2[320 * 200];
    shell_free(&rs); memset(&rs, 0, sizeof rs); rs.quiet = 1;
    if (shell_init(&rs, dir, 0)) { ck(0, "restore: shell_init"); return; }
    Game *g = &rs.g;
    if (!shell_enter_town(g, 1, 172, 0)) { ck(0, "restore: entering Muralla"); shell_free(&rs); return; }
    char path[1200]; snprintf(path, sizeof path, "%s/ZRSTEST.usr", dir);

    /* save: Muralla, column 172, a distinctive record (the Sage's A862) */
    g->gold = 4321; g->level = 7; g->max_hp = g->hp = 345; g->exp = 99;
    town_page_push(&rs.town);
    ck(player_save_usr(g, dir, "ZRSTEST") == 0, "restore: ZRSTEST.usr written");
    ck(town_hero_col(&rs.town) == 172, "restore: saved at column 172 (%d)", town_hero_col(&rs.town));

    /* move on: another place, another record */
    town_place(&rs.town, 40, 0);
    g->gold = 5; g->level = 1; g->max_hp = g->hp = 120; g->exp = 3;

    /* F7: the confirm box, drawn over the frame; N takes it down again */
    memset(fb, 0, sizeof fb); memset(fb2, 0, sizeof fb2);
    restore_open(&r);
    ck(r.state == RS_CONFIRM, "restore: F7 opens the Sure?(Y/N) box");
    restore_draw(&r, &rs, fb);
    ck(memcmp(fb, fb2, sizeof fb) != 0, "restore: the box is drawn");
    restore_key(&r, &rs, RK_CHAR, 'N');
    ck(r.state == RS_CLOSED && !r.apply, "restore: N cancels, nothing is loaded");
    ck(g->gold == 5 && town_hero_col(&rs.town) == 40, "restore: and the game is untouched");

    /* Y: the name box.  No name yet -> "Re-Start", the list starts with it */
    restore_open(&r);
    restore_key(&r, &rs, RK_CHAR, 'Y');
    ck(r.state == RS_PICK && r.nfiles >= 2, "restore: Y opens the name box (%d entries)", r.nfiles);
    ck(!strcmp(r.files[0], "Re-Start") && r.restart && !memcmp(r.name, "Re-Start", 8),
       "restore: it offers Re-Start first");
    memset(fb, 0, sizeof fb);
    restore_draw(&r, &rs, fb);
    ck(memcmp(fb, fb2, sizeof fb) != 0, "restore: the name box is drawn");

    /* a name that is not a file: "User File / Not Found", Space, back to the box */
    restore_key(&r, &rs, RK_CHAR, 'Z'); restore_key(&r, &rs, RK_CHAR, 'Z');
    ck(!r.restart && r.name[0] == 'Z' && r.name[1] == 'Z' && r.used == 2, "restore: typing replaces Re-Start");
    restore_key(&r, &rs, RK_BACKSPACE, 0);
    ck(r.name[1] == 0x60 && r.used == 1, "restore: Backspace deletes");
    restore_key(&r, &rs, RK_ACCEPT, 0);
    ck(r.state == RS_NOTFOUND && !r.apply, "restore: an unknown name is User File Not Found");
    restore_key(&r, &rs, RK_TAKE, 0);
    ck(r.state == RS_PICK, "restore: Space goes back to the box");

    /* Down to the saved file, Space copies it, Enter accepts it */
    int want = -1;
    for (int i = 0; i < r.nfiles; i++) if (!strcmp(r.files[i], "ZRSTEST")) want = i;
    ck(want > 0, "restore: ZRSTEST is in the list (row %d)", want);
    for (int i = 0; i < want; i++) restore_key(&r, &rs, RK_DOWN, 0);
    ck(r.sel == want && r.sel < r.top + 5 && r.sel >= r.top, "restore: Down scrolls the list to it");
    restore_key(&r, &rs, RK_TAKE, 0);
    ck(!memcmp(r.name, "ZRSTEST`", 8) && !r.restart, "restore: Space copies the name into the box");
    restore_key(&r, &rs, RK_ACCEPT, 0);
    ck(r.state == RS_CLOSED && r.apply && !strcmp(r.pick, "ZRSTEST"), "restore: Enter accepts (%s)", r.pick);
    ck(strncmp(g->player_name, "ZRSTEST", 8) == 0 || g->player_name[0] == 'Z', "restore: FF6C holds the name");

    /* the commit: the page comes back and the town is re-entered at column 172 */
    ck(restore_commit(&r, &rs) == 0, "restore: the commit succeeds");
    ck(rs.in_town && town_hero_col(&rs.town) == 172, "restore: back in the town at column 172 (%d)", town_hero_col(&rs.town));
    ck(g->gold == 4321 && g->level == 7 && g->max_hp == 345 && g->exp == 99,
       "restore: the record is the saved one (gold %u, level %u)", (unsigned)g->gold, g->level);
    ck(g->cur_map == 0x81, "restore: Muralla (cur_map %02X)", g->cur_map);

    /* from a cavern: F7 leaves it for the town the save names */
    if (shell_enter_cavern(&rs, 0, 61, 7, 0, 0)) {
        g->gold = 9; g->hp = 17;
        restore_open(&r); restore_key(&r, &rs, RK_CHAR, 'Y');
        for (int i = 0; i < 10; i++) restore_key(&r, &rs, RK_BACKSPACE, 0);   /* the box holds the last name */
        type_name(&r, &rs, "ZRSTEST");
        restore_key(&r, &rs, RK_ACCEPT, 0);
        ck(r.apply && restore_commit(&r, &rs) == 0 && rs.in_town && town_hero_col(&rs.town) == 172
           && g->gold == 4321, "restore: from a cavern it lands in the town at column 172 with the saved record");
    } else ck(0, "restore: entering MP10");

    /* Enter on an empty box turns it into Re-Start; the second Enter takes it */
    restore_open(&r); restore_key(&r, &rs, RK_CHAR, 'Y');
    for (int i = 0; i < 10; i++) restore_key(&r, &rs, RK_BACKSPACE, 0);     /* ZRSTEST -> empty */
    restore_key(&r, &rs, RK_ACCEPT, 0);
    ck(r.state == RS_PICK && r.restart && !memcmp(r.name, "Re-Start", 8), "restore: an empty box becomes Re-Start");
    restore_key(&r, &rs, RK_ACCEPT, 0);
    ck(r.apply && r.pick[0] == 0, "restore: and the second Enter takes it");
    ck(restore_commit(&r, &rs) == 0 && rs.in_town && g->level == 0 && g->gold != 4321,
       "restore: Re-Start is a new game (STDPLY.BIN: level %u, gold %u)", g->level, (unsigned)g->gold);

    remove(path);
    shell_free(&rs);
}

/* #52 step 2: the player-facing layer -- the slot file names, the request
 * queue and the overlay refusal.  Pure C, no game files needed. */
static void statekeys_unit(void)
{
    StateKeys k;
    statekeys_init(&k);
    ck(statekeys_path(&k, -1, (char[16]){0}, 16) < 0, "statekeys: no states directory -> no path");

    ck(statekeys_dir(&k, "/tmp/zel_statekeys_unit", NULL, NULL) == 0,
       "statekeys: the states directory is created");
    char path[160];
    ck(statekeys_path(&k, -1, path, sizeof path) == 0 && !strcmp(path, "/tmp/zel_statekeys_unit/quick.zst"),
       "statekeys: the quick slot is quick.zst (%s)", path);
    ck(statekeys_path(&k, 3, path, sizeof path) == 0 && !strcmp(path, "/tmp/zel_statekeys_unit/slot3.zst"),
       "statekeys: slot 3 is slot3.zst (%s)", path);

    ck(k.q_n == 0, "statekeys: nothing is queued at first");
    statekeys_queue(&k, STATE_REQ_SAVE, 2);
    statekeys_queue(&k, STATE_REQ_LOAD, -1);
    ck(k.q_n == 2 && k.q_kind[0] == STATE_REQ_SAVE && k.q_slot[0] == 2
       && k.q_kind[1] == STATE_REQ_LOAD && k.q_slot[1] == -1,
       "statekeys: requests queue in order (save slot 2, then load quick)");

    static Shell s; memset(&s, 0, sizeof s);
    statekeys_request(&k, &s, STATE_REQ_SAVE, -1);
    ck(k.q_n == 3, "statekeys: a plain frame queues a request (%d)", k.q_n);
    s.in_town = 1; s.town.shop = (struct Shop *)(void *)&s;      /* a shop owns the loop */
    statekeys_request(&k, &s, STATE_REQ_SAVE, -1);
    ck(k.q_n == 3 && k.refused == 1, "statekeys: a shop refuses it (%d queued, %u refused)", k.q_n, k.refused);
    ck(strstr(statekeys_osd_text(&k), "CANNOT SAVE") != NULL,
       "statekeys: and says so on screen (%s)", statekeys_osd_text(&k));

    ck(statekeys_glyph('A') && statekeys_glyph('7') && statekeys_glyph(' '),
       "statekeys: the font has the glyphs the OSD needs");
    ck(statekeys_glyph('A')[0] == 0x7E, "statekeys: 'A' has the expected first column");
    ck(statekeys_text_width("AB", 3) == 33, "statekeys: 'AB' at scale 3 is 33 px (%d)",
       statekeys_text_width("AB", 3));
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../zeliard";
    int verbose = 1, only = 0;
    unsigned budget = 900000;

    /* Every check below walks a real map out of the original game files, which
     * are not redistributable and so are absent from a clean checkout (and from
     * CI).  Probe for them once and skip cleanly rather than reporting 34
     * failures that only mean "no game data here". */
    {
        size_t probe_len = 0;
        uint8_t *probe = sar_load(dir, 0, 0, 1, &probe_len);
        if (!probe) {
            fprintf(stderr, "  (ZELRES1.SAR not available in %s: skipping the playthrough routes)\n", dir);
            return 0;
        }
        free(probe);
    }
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) verbose = 2;
        else if (!strcmp(argv[i], "--quiet")) verbose = 0;
        else if (!strcmp(argv[i], "--route") && i + 1 < argc) only = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--budget") && i + 1 < argc) budget = (unsigned)atoi(argv[++i]);
    }

    if (only != 2) {
        shell_free(&p.sh); memset(&p, 0, sizeof p);
        int rc = run(dir, PLAY_ROUTE_START, budget, "route 1 (the opening)", verbose);
        Game *g = &p.sh.g;
        ck(rc == 0, "route 1 ran to the end");
        /* GAME.BIN A1CB places the hero in Felishika's Castle from the page,
         * STDPLY.BIN's [80] = 30 / [83] = 10 -> column 44, not from the map's
         * C013 death return (issue #40). */
        ck(p.entry_scroll == 30 && p.entry_col == 44,
           "route 1: the autopilot starts where the real game starts (scroll %d, column %d)",
           p.entry_scroll, p.entry_col);
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
        shell_free(&p.sh); memset(&p, 0, sizeof p);
        p.verbose = verbose;
        p.start_level = 16; p.start_sword = 4; p.start_shield = 4;
        p.start_life = 800; p.start_gold = 20000;
        int rc = run(dir, PLAY_ROUTE_BOSSES, budget, "route 2 (caverns 1-3 and their bosses)", verbose);
        Game *g = &p.sh.g;
        ck(rc == 0, "route 2 ran to the end");
        ck(p.bosses_killed >= 3, "route 2: Cangrejo, Pulpo and Pollo defeated");
        ck(p.deaths == 0, "route 2: Garland never died");
        /* 71CC..71F5 (#68, crab_fight: EXP 0 -> 120, ALMAS 0 -> 150, GOLD
         * unchanged in the original): a boss pays its [A002]+5 EXP through
         * 9715 and its +B through 917C, the ALMAS adder -- never gold */
        ck(p.boss_exp_due > 0 && p.boss_exp_paid == p.boss_exp_due,
           "route 2: the bosses paid their EXP (%ld of %ld)", p.boss_exp_paid, p.boss_exp_due);
        ck(p.boss_almas_due > 0 && p.boss_almas_paid == p.boss_almas_due,
           "route 2: the bosses paid their almas (%ld of %ld)", p.boss_almas_paid, p.boss_almas_due);
        ck(p.boss_gold_paid == 0, "route 2: the bosses paid no gold (%ld)", p.boss_gold_paid);
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
    cavern_progression(dir, verbose);
    state_roundtrip_unit(dir);
    statekeys_unit();
    restore_unit(dir);

    /* teardown, so `make SAN=1 test` proves the load-once allocations all
     * have an owner and a shell_free that reaches them */
    shell_free(&p.sh);
    shell_free(&rsh);
    shell_free(&csh);

    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
