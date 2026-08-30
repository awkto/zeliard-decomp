/* nav.c — the playthrough autopilot (see nav.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nav.h"
#include "enemy.h"
#include "boss.h"

/* ------------------------------------------------------- the button macros */
/* Each macro is a direction pattern (held at its last entry), a length in
 * frames and a button mask.  The probe holds the pattern until the hero
 * settles again (grounded or on a ladder), so every edge in the graph is a
 * complete, executable move. */
typedef struct { const uint8_t *dirs; int plen, n; uint8_t btn; const char *name; } Macro;

#define UR (DIR_UP | DIR_RIGHT)
#define UL (DIR_UP | DIR_LEFT)
static const uint8_t M_R[]   = {DIR_RIGHT};
static const uint8_t M_L[]   = {DIR_LEFT};
static const uint8_t M_U[]   = {DIR_UP};
static const uint8_t M_D[]   = {DIR_DOWN};
static const uint8_t M_0[]   = {0};
static const uint8_t M_JR[]  = {UR, UR, DIR_RIGHT};
static const uint8_t M_JL[]  = {UL, UL, DIR_LEFT};
static const uint8_t M_JR0[] = {UR, UR, 0};
static const uint8_t M_JL0[] = {UL, UL, 0};
static const uint8_t M_JR1[] = {UR, 0};
static const uint8_t M_JL1[] = {UL, 0};
static const uint8_t M_URR[] = {DIR_UP, DIR_UP, DIR_RIGHT};
static const uint8_t M_ULL[] = {DIR_UP, DIR_UP, DIR_LEFT};

static const Macro MACRO[] = {
    {M_R,   1,  1, 0, "R"},             /*  0 */
    {M_L,   1,  1, 0, "L"},             /*  1 */
    {M_R,   1,  4, 0, "R4"},            /*  2 */
    {M_L,   1,  4, 0, "L4"},            /*  3 */
    {M_R,   1, 10, 0, "R10"},           /*  4 */
    {M_L,   1, 10, 0, "L10"},           /*  5 */
    {M_JR,  3,  9, 0, "jump R"},        /*  6 */
    {M_JL,  3,  9, 0, "jump L"},        /*  7 */
    {M_JR0, 3,  3, 0, "hop R"},         /*  8 */
    {M_JL0, 3,  3, 0, "hop L"},         /*  9 */
    {M_JR1, 2,  2, 0, "step-hop R"},    /* 10 */
    {M_JL1, 2,  2, 0, "step-hop L"},    /* 11 */
    {M_URR, 3,  5, 0, "up then R"},     /* 12 */
    {M_ULL, 3,  5, 0, "up then L"},     /* 13 */
    {M_U,   1,  3, 0, "hop"},           /* 14 */
    {M_U,   1, 14, 0, "climb up"},      /* 15 */
    {M_D,   1, 14, 0, "climb down"},    /* 16 */
    /* Two frames of "right"/"left" -- the granularity between the one-frame
     * nudge, which on a moving platform does not leave the cell it started in
     * (the macro settles where it began, so no edge is recorded), and the
     * four-frame walk, which carries the hero off the end of the platform into
     * thin air.  Stepping onto a platform's *outermost* cell takes exactly
     * two, and MP21's fix[2] is where that matters: it is the second half of
     * cavern 2's missing link (port/README.md, "How MP21 crosses"). */
    {M_R,   1,  2, 0, "R2"},            /* 17 */
    {M_L,   1,  2, 0, "L2"},            /* 18 */
    /* the fixture rides: only probed from a node a fixture holds up, because
     * "stand still and be carried" is a move only a moving platform offers */
    {M_0,   1,  4, 0, "ride 4"},        /* 19 */
    {M_0,   1,  8, 0, "ride 8"},        /* 20 */
    {M_D,   1,  3, 0, "sink 3"},        /* 21 */
    /* not survey moves: the executor's own idle and swing */
    {M_0,   1,  4, 0, "wait"},          /* 22 */
    {M_0,   1,  3, 1, "thrust"},        /* 23 */
};
#define NMACRO   ((int)(sizeof MACRO / sizeof MACRO[0]))
#define MACRO_MOVE_N  19                /* 0..18: probed from every node */
#define MACRO_RIDE0   19                /* 19..21: probed from fixture nodes */
#define MACRO_RIDE1   21
#define MACRO_WAIT    (NMACRO - 2)      /* also the swing: the button is on frame 0 */
#define MACRO_STRIKE  MACRO_WAIT
#define MACRO_THRUST  (NMACRO - 1)
static uint8_t macro_dir(const Macro *m, int f) { return m->dirs[f < m->plen ? f : m->plen - 1]; }

#define SETTLE 26                   /* extra frames allowed for a jump to land */

static void set_btn(Game *g, uint8_t b)
{
    if ((b & 1) && !(g->buttons & 1)) g->btn1_edge = 0xFF;
    if ((b & 2) && !(g->buttons & 2)) g->btn2_edge = 0xFF;
    g->buttons = b;
}
/* Has the hero come to rest?  `vstate` alone is not enough: 6F9B runs
 * `gravity` *before* `hero_input` (game_step), so the frame a walk carries him
 * off the end of something he is still V_GROUND -- the fall does not start
 * until the next frame.  A probe that stops there records an edge that lands in
 * mid-air, and the executor's own walks-off guard then refuses the last step of
 * it for ever: MP20's L4 from (125,36) "landed" on (122,36) with fix[7] four
 * columns further west, and the hero paced the east lip of that gap for the
 * whole frame budget.  So insist that something really is under his feet. */
static int settled(const Game *g)
{
    if (g->on_ladder) return !g->walk_in;
    return g->vstate == V_GROUND && game_hero_has_floor(g) && !g->walk_in;
}

/* --------------------------------------------------------------- the graph */

/* Rows are the ring's rows: 64 of them and cyclic (6D6E/6D82), so a hero can
 * straddle the wrap — MP20's entry from Satono puts him at row 62 with his
 * feet on row 1 — and every row test here has to wrap with it. */
static int passable_here(const Game *g, int c, int r, int body)
{
    const Map *m = g->map;
    if (c < 0 || c >= m->width) return 0;
    uint8_t v = m->grid[c][r & 0x3F];
    return body ? game_passable_body(g, v) : game_passable_wall(g, v);
}
/* 66A5/684C: the hero's body column must clear one "wall" cell and two "body" */
static int fits(const Game *g, int bc, int r)
{
    return passable_here(g, bc, r, 0) && passable_here(g, bc, r + 1, 1) && passable_here(g, bc, r + 2, 1);
}
static int is_ladder(const Game *g, int bc, int r)
{
    const Map *m = g->map;
    if (bc < 0 || bc >= m->width) return 0;
    return (uint8_t)(m->grid[bc][r & 0x3F] - 1) < 2;                    /* 6BBD */
}
/* ------------------------------------------------------------- fixtures */
/* A fixture is a *moving* standable cell: the three cells of an elevator, a
 * gate or a patrolling platform live only in the ring, so the map grid has a
 * hole wherever one of them can be.  Every position a fixture can reach is
 * therefore ground the hero can stand on, and the survey has to (a) make a
 * node of each of them, (b) put the fixture under the hero before it probes
 * such a node, and (c) offer "stand still and be carried" as a move.
 *
 * Because a fixture's position is not part of the node, every edge probed
 * with one moved out of its map position remembers which fixture it needs and
 * where: `edge_ready` refuses the edge until the live fixture really is
 * there, which is what makes "wait on the ledge for the platform" fall out of
 * the ordinary planner. */

/* 8024: a platform may enter a row only when all three of its cells are empty */
static int fixture_row_free(const Map *m, const Fixture *f, int row)
{
    for (int k = 0; k < 3; k++) {
        int c = (int)f->col + k;
        while (c >= m->width) c -= m->width;
        if (m->grid[c][row & 0x3F]) return 0;
    }
    return 1;
}
#define FIX_ROW_SPAN 24                 /* sanity bound on an open shaft */
/* How far an elevator (7FDC/8074, both ways) or a gate (818E, down only) can
 * travel from its record row.  The hero drives both, so every row it can reach
 * is real ground.  Counted as a distance *up* and *down* rather than as a
 * [lo,hi] pair, because the ring's rows are cyclic: 8024/80AF walk the shaft
 * with 6D82/6D8E, which wrap, and MP20's fix[0] (column 102, home row 61) has
 * a shaft that really runs rows 55..63 **and on into 0..5**, the top-of-map
 * corridor whose floor is row 6.  Clamping the walk at row 0/63 lost six of
 * its fifteen positions and every node that hangs off them. */
static void fixture_span(const Map *m, const Fixture *f, int *up, int *down)
{
    int home = f->row & 0x3F, n = 0;
    if (f->kind == 0)
        while (n < FIX_ROW_SPAN && fixture_row_free(m, f, (home - n - 1) & 0x3F)) n++;
    *up = n;
    n = 0;
    while (n < FIX_ROW_SPAN && fixture_row_free(m, f, (home + n + 1) & 0x3F)) n++;
    *down = n;
}
/* is row `r` one of them? */
static int fixture_row_in_span(const Map *m, const Fixture *f, int r)
{
    int up, down, d = (r - (f->row & 0x3F)) & 0x3F;
    fixture_span(m, f, &up, &down);
    return d <= down || 64 - d <= up;
}
static void fixture_cols(const Fixture *f, int *lo, int *hi)
{
    if (f->kind != 2 || !f->var) { *lo = f->col; *hi = (int)f->col + 2; return; }
    *lo = f->lim_l < f->lim_r ? f->lim_l : f->lim_r;                    /* 827B/8296 */
    *hi = (f->lim_l > f->lim_r ? f->lim_l : f->lim_r) + 2;
}
/* which fixture can hold up the cell under a hero body column `bc` at row `r`? */
static int fixture_at(const Map *m, int bc, int r)
{
    for (int i = 0; i < m->nfix; i++) {
        const Fixture *f = &m->fix[i];
        int lo, hi;
        if (f->kind == 2) {
            if ((f->row & 0x3F) != (r & 0x3F)) continue;
        } else {
            if (!fixture_row_in_span(m, f, r & 0x3F)) continue;
        }
        fixture_cols(f, &lo, &hi);
        if (bc >= lo && bc <= hi) return i;
    }
    return -1;
}
/* a fixture that can move at all (the survey needs a node at every position
 * such a one can reach); `fixture_self_moving` below is the stricter test. */
/* ... but only some of them move *by themselves*.  A fixture-B gate sinks a row
 * a frame under the hero's weight (818E) and a fixture-C platform patrols on
 * its own (81AE), so where either of those is when the executor looks is not
 * where the probe had it.  A fixture-A elevator (7FDC/8074) moves only while
 * the hero stands on it holding up or down: between rides it is as fixed as
 * rock, and an edge probed with one at a given row reproduces exactly, because
 * `edge_ready` has already checked that row.  Telling the two apart is what
 * lets the hero step *off* an elevator onto the ledge below it -- MP20's ride
 * down the column-157 shaft to the Key ledge is a plain walk left off the
 * platform into a one-row drop, and the walks-off guard refused it for ever. */
static int fixture_self_moving(const Fixture *f) { return f->kind == 1 || (f->kind == 2 && f->var); }

/* A cell nothing solid holds up, that the hero keeps only because he is
 * *hanging on a ladder* (62DB ladder_step: he stays while (bc,r) or (bc,r+1)
 * is a ladder cell).  Such a node exists only with [FF39] set, so the probe
 * has to place him there the way ladder_mount (65C5) leaves him -- otherwise
 * gravity drops him on frame 1 and the node looks like a hole in the graph.
 * That is what sealed MP10's col-165 and col-108 ladders. */
static int ladder_only(const Game *g, int c, int r)
{
    int bc = c + 1;
    if (!passable_here(g, bc, r + 3, 1)) return 0;                      /* solid floor */
    if (fixture_at(g->map, bc, r + 3) >= 0) return 0;
    return is_ladder(g, bc, r) || is_ladder(g, bc, r + 1);
}
static int standable(const Game *g, int c, int r)
{
    int bc = c + 1;
    if (!fits(g, bc, r)) return 0;
    if (!passable_here(g, bc, r + 3, 1)) return 1;                      /* solid floor */
    if (fixture_at(g->map, bc, r + 3) >= 0) return 1;
    return is_ladder(g, bc, r) || is_ladder(g, bc, r + 1);
}
/* 7505/73C0: the hero burns while any of the nine cells of his body -- or,
 * off the ladder, the cell under his feet -- is one of the tileset's four
 * hazard cells.  The survey cannot feel that (the probe is immortal), so every
 * node is marked and `build_field` charges NAV_HAZCOST to enter one. */
static int node_hazard(const Game *g, int c, int r)
{
    const Map *m = g->map;
    if (g->shoes == 2) return 0;                                        /* 7505: Pirika shoes */
    for (int dr = 0; dr < 3; dr++)
        for (int dc = 0; dc < 3; dc++) {
            int cc = c + dc;
            if (cc < 0 || cc >= m->width) continue;
            if (game_cell_hazard(g, m->grid[cc][(r + dr) & 0x3F])) return 1;
        }
    return c + 1 < m->width && game_cell_hazard(g, m->grid[c + 1][(r + 3) & 0x3F]);
}

/* 65C5 leaves the hero hanging: [FF39] = FF, no crouch, an odd climb frame */
static void probe_grab_ladder(Game *p, int c, int r)
{
    if (!ladder_only(p, c, r)) return;
    p->on_ladder = 0xFF; p->crouching = 0; p->hero_anim |= 1;
}

/* Put the fixture this node needs where the node needs it.  Two cases:
 *   - the node is *on* a fixture: move it under the hero's feet;
 *   - the node is a ledge a moving platform passes below or beside: bring the
 *     platform alongside, which is what a player does by waiting for it.
 * Returns the fixture index (-1 none); *pos is what edge_ready must see. */
/* which fixture, if any, this node's probes want moved */
static int probe_fixture_for(const Map *m, int c, int r)
{
    int bc = c + 1, feet = r + 3;
    int sup = fixture_at(m, bc, feet);
    if (sup >= 0) return (m->fix[sup].kind == 2 && !m->fix[sup].var) ? -1 : sup;
    for (int i = 0; i < m->nfix; i++) {          /* a ledge a platform passes */
        const Fixture *f = &m->fix[i];
        int lo, hi, frow = f->row & 0x3F;
        if (f->kind != 2 || !f->var) continue;
        fixture_cols(f, &lo, &hi);
        if (frow < feet || frow > feet + 6) continue;
        if (bc < lo - 3 || bc > hi + 3) continue;
        return i;
    }
    return -1;
}
/* Did the probe's hero pass over a cell fixture `fx` can occupy?  An edge only
 * *depends* on where that fixture is if it did: `probe_fixture_for` also brings
 * a patrolling platform alongside a ledge node so that the step onto it exists
 * at all, and without this test every other edge of that ledge -- plain walks
 * along solid rock included -- waited for the platform to come round to the
 * column the probe happened to put it at.  MP20's east lip at (123,36) has ten
 * such edges and `edge_ready` refused all of them for sixteen frames in every
 * eighteen, so the hero paced the lip instead of boarding.  Testing the *ends*
 * of the edge is not enough: MP10's (57,50) walks west off solid rock, over
 * twenty-four columns of fix[3]'s gap, and lands on solid rock at (52,56). */
static int probe_over_fixture(const Game *p, const Map *m, int fx)
{
    if (fx < 0) return 0;
    int bc = game_hero_map_col(p) + 1, feet = (game_hero_map_row(p) + 3) & 0x3F;
    while (bc >= m->width) bc -= m->width;
    while (bc < 0) bc += m->width;
    return fixture_at(m, bc, feet) == fx;
}
static int probe_place_fixture(Nav *n, Game *p, int c, int r, uint8_t dirstate, int *pos)
{
    const Map *m = &n->navmap;
    int bc = c + 1, feet = r + 3;
    *pos = 0;
    int sup = probe_fixture_for(m, c, r);
    if (sup < 0) return -1;
    Fixture *f = &p->fix[sup];
    if (f->kind == 2) {
        int lo = f->lim_l < f->lim_r ? f->lim_l : f->lim_r;
        int hi = f->lim_l > f->lim_r ? f->lim_l : f->lim_r;
        int col = bc - 1;
        if (col < lo) col = lo;
        if (col > hi) col = hi;
        /* A platform standing on one of its own limits is never going the way
         * that would take it past: 8244 turns it round only when the column it
         * has just *moved to* equals the limit for the direction it is going
         * (82A6 `sub bx,ax / jz 82AB`, then 82AB `xor [si+2],0x80`), so one
         * sitting on lim_l has just flipped to moving right and one on lim_r to
         * moving left.  Probed in the other state the mover walks it straight
         * past the limit and off down the ring -- 82AB never fires again --
         * and the edges that come back promise rides the live platform will
         * never give.  That is what pinned Garland to MP20's fix[7] at columns
         * 114-121: the only two edges out of (114,36) that improved the
         * distance were "ride west from lim_l", and the live platform, having
         * just turned, carried him east every time.  So take the direction the
         * live one really has.  (Skipping the probe instead loses far more: a
         * *ledge* node beside a platform is only ever probed one way, and
         * dropping that probe drops the node's ordinary walks with it.) */
        if (f->var && lo != hi) {
            if (col == lo) dirstate = 0;
            else if (col == hi) dirstate = 0x80;
        }
        f->col = (uint16_t)col;
        f->state = dirstate;
        /* the direction matters as much as the position: a platform going the
         * other way ends the ride somewhere else entirely */
        *pos = col | (dirstate ? NAV_FIXDIR : 0);
    } else {
        f->row = (uint8_t)(feet & 0x3F);
        *pos = feet & 0x3F;
    }
    return sup;
}

/* is the fixture an edge was probed with where the edge needs it? */
static int step_walks_off(const Game *g, uint8_t dirs);
static uint8_t macro_first_dir(const Nav *n, int e);
static int hero_carried(const Game *g);
static int on_foreign_door(const Nav *n, const Game *g);
static int macro_has_up(int mi);
/* does this edge depend on a fixture that moves on its own? */
static int edge_self_moving(const Nav *n, const Game *g, int e)
{
    int fx = n->efix[e];
    return fx && fx <= g->nfix && fixture_self_moving(&g->fix[fx - 1]);
}
/* a macro that leaves the ground: nothing can be corrected once it starts */
static int macro_aerial(int mi) { return mi >= 6 && mi <= 14; }
static int edge_ready(const Nav *n, const Game *g, int e)
{
    int fx = n->efix[e];
    if (!fx || fx > g->nfix) return 1;
    if (n->efixpos[e] & NAV_FIXFREE) return 1;      /* neither end rides it */
    const Fixture *f = &g->fix[fx - 1];
    if (f->kind == 2) {
        /* A patrolling platform's *phase*: the probe ran the whole macro with the
         * real 81AE mover under it, so the edge does what it promised only from
         * about the column and the direction it was probed at.  One column of
         * tolerance, not zero: `probe_place_fixture` records where it *put* the
         * platform, and by the time the executor looks, fixtures_draw has
         * already moved it once for this frame (it runs at the top of frame(),
         * above `present`).  Matching the post-move column exactly instead was
         * tried and is worse on every leg -- `game_place` restarts [9F07] at 0,
         * so the probe's first frame always moves a half-speed platform while
         * the live one moves every other frame, and the phases drift apart over
         * a ride.  What the tolerance cannot cover is a jump, so build_graph
         * simply never records one that lands on a platform. */
        int d = (int)f->col - (int)(n->efixpos[e] & ~NAV_FIXDIR);
        if (d < -1 || d > 1) return 0;
        if (((f->state & 0x80) != 0) == ((n->efixpos[e] & NAV_FIXDIR) != 0)) return 1;
        /* The direction matters for a *ride*: the probe ran the macro with the
         * mover going one way.  It does not matter for the step **off** the
         * platform -- if the cell the edge lands on is held up by the map, the
         * platform only has to be under him for the first frame.  Without this
         * the hero rode MP20's fix[5] to the far end of its patrol, found every
         * outgoing edge refused the moment it turned round, and was dragged
         * back across the gap again, for ever. */
        int to = n->eto[e], w = g->map->width;
        int bc = (int)n->ncol[to] + 1, feet = (n->nrow[to] + 3) & 0x3F;
        while (bc >= w) bc -= w;
        return !game_passable_body(g, g->map->grid[bc][feet]);
    }
    return (f->row & 0x3F) == (n->efixpos[e] & 0x3F);
}
/* ... and does its first step keep him on something? */
static int edge_ok(const Nav *n, const Game *g, int e, int from)
{
    /* the walks-off test guards *fixture* edges only: an ordinary walk off a
     * ledge is a legitimate move (the survey probed where it lands, and 695A's
     * ladder catch may even grab something on the way down), and refusing it
     * hides whole routes -- MP10 (164,26) steps left off its shelf onto the
     * col-163 ladder, and that is the only way down to door 5. */
    if (!edge_ready(n, g, e)) return 0;
    if (macro_has_up(n->emacro[e]) && on_foreign_door(n, g)) return 0;
    /* The walks-off test is about the world having moved under the hero, so it
     * only applies to a move the survey saw stay on the same row: a step that
     * ends lower *is* a drop, the probe measured where it lands, and refusing
     * it hides whole routes (MP10 steps left off the (164,26) shelf onto the
     * col-163 ladder, and that is the only way down to door 5). */
    if (from >= 0 && n->nrow[n->eto[e]] != n->nrow[from] && !edge_self_moving(n, g, e) &&
        !hero_carried(g))
        return 1;
    return !step_walks_off(g, macro_first_dir(n, e));
}
/* Is the hero standing in front of a door that is *not* the one he is heading
 * for?  7A83 takes a door the moment "up" is pressed on the cell below it (and
 * on either side of that, 7AF6), so any macro with an up frame -- every jump,
 * every climb -- opens it.  The survey never sees this: the probe runs on a
 * private Map copy with `on_door` unhooked, so those edges look like ordinary
 * moves.  Walking the crest tree's ledge in MP30, Garland jumped on the cell
 * under the (186,46) door and spent the rest of the step in MP31. */
static int on_foreign_door(const Nav *n, const Game *g)
{
    int hc = game_hero_map_col(g), hr = game_hero_map_row(g);
    for (int i = 0; i < g->map->ndoors; i++) {
        const Door *d = &g->map->doors[i];
        if (d->row + 1 != hr) continue;
        if (hc < (int)d->col - 1 || hc > (int)d->col + 1) continue;
        if (n->mode == NAV_DOOR && (int)d->col == n->door_col && d->row == n->door_row) continue;
        return 1;
    }
    return 0;
}
static int macro_has_up(int mi)
{
    const Macro *m = &MACRO[mi];
    for (int f = 0; f < m->plen; f++) if (m->dirs[f] & DIR_UP) return 1;
    return 0;
}

/* the moving fixture under the hero's feet, or -1 */
static int carried_fixture(const Game *g)
{
    if (g->vstate || g->on_ladder) return -1;
    /* his collision column, the one 66A5/684C test: map col + 1 */
    int bc = g->hero_scr_col + 5 + g->scroll_col, feet = (g->hero_scr_row + g->scroll_row + 3) & 0x3F;
    while (bc >= g->map->width) bc -= g->map->width;
    for (int i = 0; i < g->nfix; i++) {
        const Fixture *f = &g->fix[i];
        if (f->kind != 2 || !f->var) continue;
        if ((f->row & 0x3F) != feet) continue;
        if (bc >= (int)f->col && bc <= (int)f->col + 2) return i;
    }
    return -1;
}

/* is the hero being carried by something that moves?  (his cell changes on its
 * own, so the stall detector must not read that as progress or as a stall) */
static int hero_carried(const Game *g)
{
    if (g->vstate || g->on_ladder) return 0;
    /* the cell 6D6E+0x6D tests for ground: his own map column + 1 */
    int bc = g->hero_scr_col + 5 + g->scroll_col, feet = (g->hero_scr_row + g->scroll_row + 3) & 0x3F;
    while (bc >= g->map->width) bc -= g->map->width;
    for (int i = 0; i < g->nfix; i++) {
        const Fixture *f = &g->fix[i];
        if (!fixture_self_moving(f)) continue;
        if ((f->row & 0x3F) != feet) continue;
        if (bc >= (int)f->col && bc <= (int)f->col + 2) return 1;
    }
    return 0;
}

/* run one macro from the hero's current probe state; returns the number of
 * frames used, and leaves the probe wherever the hero settled.
 *
 * (Recording where the platform is *after* the first 81AE move -- nominally
 * where the executor sees it, since fixtures_draw runs above `present` -- and
 * matching that exactly was tried and is worse in every measured leg: the
 * probe's [9F07] restarts at 0 with `game_place`, so its first frame always
 * moves a half-speed platform while the live one moves on every other frame,
 * and the two phases drift apart over a multi-frame ride.  The placement plus
 * a one-column window is what actually reproduces.) */
static int run_macro(Nav *n, Game *p, int mi, int fx, int *touched)
{
    const Macro *m = &MACRO[mi];
    int f = 0;
    if (touched) *touched = probe_over_fixture(p, &n->navmap, fx);
    for (; f < m->n; f++) {
        p->dirs = macro_dir(m, f);
        set_btn(p, m->btn);
        game_step(p);
        n->steps++;
        if (touched && probe_over_fixture(p, &n->navmap, fx)) *touched = 1;
        if (p->hero_dead) return f + 1;
    }
    for (int k = 0; k < SETTLE && !settled(p); k++) {
        p->dirs = 0; set_btn(p, 0);
        game_step(p);
        n->steps++; f++;
        if (touched && probe_over_fixture(p, &n->navmap, fx)) *touched = 1;
        if (p->hero_dead) break;
    }
    return f;
}

static void probe_reset(Nav *n, const Game *g)
{
    Game *p = &n->probe;
    memcpy(p, g, sizeof *p);
    p->present = NULL; p->on_door = NULL; p->on_town = NULL; p->post_boss = NULL;
    p->status = NULL; p->menu_key = 0; p->user = NULL;
    p->map = &n->navmap;
    p->hp = p->max_hp = 9999;                   /* the probe must not die */
    p->boss_map = p->boss_room = 0; p->boss.active = 0; p->encounter_frames = 0;
    p->walk_in = 0; p->hero_entering = 0;
    p->shoes = g->shoes;
}

static void build_graph(Nav *n, const Game *g)
{
    const Map *m = g->map;
    int W = m->width;
    n->builds++;
    for (int c = 0; c < W; c++) for (int r = 0; r < MAP_ROWS; r++) n->node_of[c][r] = -1;
    n->nnode = 0;
    memset(n->haz, 0, sizeof n->haz);
    for (int c = 0; c < W && n->nnode < NAV_MAX_NODE; c++)
        for (int r = 0; r < MAP_ROWS; r++)
            if (standable(g, c, r)) {
                n->haz[c][r] = (uint8_t)node_hazard(g, c, r);
                n->node_of[c][r] = n->nnode;
                n->ncol[n->nnode] = (uint16_t)c; n->nrow[n->nnode] = (uint16_t)r;
                n->nnode++;
                if (n->nnode >= NAV_MAX_NODE) break;
            }
    memcpy(&n->navmap, m, sizeof n->navmap);
    n->nedge = 0;
    for (int i = 0; i < n->nnode; i++) {
        n->efirst[i] = n->nedge;
        int nc = n->ncol[i], nr = n->nrow[i];
        /* a node a fixture holds up also gets the ride macros, and — when the
         * platform patrols — both of its directions, because which way it is
         * going when the hero steps on decides where the ride ends */
        int onfix = fixture_at(&n->navmap, nc + 1, nr + 3);
        int last = onfix >= 0 ? MACRO_RIDE1 : MACRO_MOVE_N - 1;
        /* A *ledge* node beside a patrolling platform is probed with the
         * platform going both ways too, and for every macro rather than only
         * the rides: stepping off the ledge is a drop of a row or two, and
         * which way the platform is travelling decides whether it is still
         * under the hero when he lands.  Only when the platform is *below* the
         * ledge, though -- one level with it is a step across, which the
         * single-direction probe already covers, and probing that twice gates
         * the ledge's ordinary walks on two different platform positions.
         * MP21's fix[2] (row 61, columns 1-14) is what needs this: the step
         * west off the column-13 ladder at (12,56) lands on the platform only
         * while it is moving left, and that step is the first half of cavern
         * 2's missing link (port/README.md, "How MP21 crosses"). */
        int nearfix = onfix >= 0 ? -1 : probe_fixture_for(&n->navmap, nc, nr);
        int patrol  = nearfix >= 0 && n->navmap.fix[nearfix].kind == 2 && n->navmap.fix[nearfix].var
                   && (n->navmap.fix[nearfix].row & 0x3F) != ((nr + 3) & 0x3F);
        int ndir = ((onfix >= 0 && n->navmap.fix[onfix].kind == 2) || patrol) ? 2 : 1;
        for (int d = 0; d < ndir; d++) {
            uint8_t st = (uint8_t)(d ? 0x80 : 0);
            /* ...and every node is surveyed in *both facings*.  6824 spends the
             * first frame of a move in the wrong facing turning the hero round
             * instead of moving him, so a hero who already faces left steps a
             * whole frame earlier than one who does not -- and on a full-speed
             * patrolling platform a frame is a column.  MP20's fix[7] is the
             * case that needs it: the platform's rightmost cell reaches the
             * east lip at (123,36) for exactly the two frames 8244 leaves it on
             * its own lim_r (the frame it arrives and the paused frame after
             * 82AB sets bit 6), and only a hero already facing left can use
             * them.  Probed facing right there is no edge onto that platform at
             * all, and the hero paced the lip for the whole frame budget.
             * Only the leftward macros are worth a second probe: a rightward
             * one from a left-facing hero is just the same move with a wasted
             * turn in front of it, which the executor can produce anyway. */
            for (int fc = 0; fc < 2; fc++)
            for (int mi = 0; mi <= last; mi++) {
                if (d && onfix >= 0 && mi < MACRO_RIDE0) continue;  /* on a fixture: both ways only for the rides */
                if (fc && !(MACRO[mi].dirs[0] & DIR_LEFT)) continue;
                probe_reset(n, g);
                Game *p = &n->probe;
                game_place(p, nc, nr, fc);
                probe_grab_ladder(p, nc, nr);
                p->nobj = 0;                                /* no enemies while surveying */
                p->hp = p->max_hp = 9999;
                int fpos = 0;
                int fx = probe_place_fixture(n, p, nc, nr, st, &fpos);
                int touched = 0;
                int cost = run_macro(n, p, mi, fx, &touched);
                if (p->hero_dead || !settled(p)) continue;
                int c = game_hero_map_col(p), r = game_hero_map_row(p);
                if (c < 0 || c >= W || r < 0 || r >= MAP_ROWS) continue;
                int j = n->node_of[c][r];
                if (j < 0 || j == i) continue;
                /* Never plan a jump that *lands* on a patrolling platform.  A
                 * jump is the one move nothing can correct once it has started:
                 * the hero is in the air for up to nine frames while the
                 * platform slides four to nine columns under him, so the cell
                 * the probe measured is only where he lands if the platform is
                 * exactly where the probe had it -- which `edge_ready`'s
                 * one-column tolerance cannot promise, and which a phase-exact
                 * test cannot make happen often enough to be useful.  Missing
                 * it is usually fatal rather than merely wrong: the jump west
                 * off MP10's fix[2] lands in the lava pit at (0..6, 44..47),
                 * which nothing can climb out of, and the run is over there.
                 * Jumping *off* a platform onto solid ground is still offered,
                 * because that is how the hero leaves one; only the landing
                 * matters. */
                if (macro_aerial(mi)) {
                    int lf = fixture_at(&n->navmap, c + 1, r + 3);
                    if (lf >= 0 && n->navmap.fix[lf].kind == 2 && n->navmap.fix[lf].var) continue;
                }
                if (n->nedge >= NAV_MAX_EDGE) break;
                n->eto[n->nedge] = j;
                n->emacro[n->nedge] = (uint8_t)mi;
                n->ecost[n->nedge] = (uint8_t)(cost < 1 ? 1 : cost > 60 ? 60 : cost);
                /* Gate the edge on the fixture only when the fixture is what
                 * holds one of its ends up.  `probe_fixture_for` also brings a
                 * patrolling platform alongside a *ledge* node, so that the
                 * step onto it exists at all -- but that made every other edge
                 * of that ledge, plain walks along solid rock included, wait
                 * for the platform to come round to the column the probe put it
                 * at.  MP20's east lip at (123,36) has ten such edges and
                 * `edge_ready` refused all of them for sixteen frames in every
                 * eighteen, so the hero paced the lip instead of boarding. */
                n->efix[n->nedge] = (uint8_t)(fx + 1);
                n->eface[n->nedge] = (uint8_t)fc;
                n->efixpos[n->nedge] = touched ? (uint16_t)fpos : (uint16_t)NAV_FIXFREE;
                n->nedge++;
            }
        }
    }
    n->efirst[n->nnode] = n->nedge;
    memset(n->efail, 0, sizeof n->efail[0] * (size_t)n->nedge);
    n->built = 1;
}

/* Dijkstra from the goal over the reversed graph (bucket queue: costs <= 60) */
static void build_field(Nav *n, const Game *g)
{
    for (int i = 0; i < n->nnode; i++) n->dist[i] = NAV_INF;
    /* reverse adjacency */
    static int rfirst[NAV_MAX_NODE + 1], rto[NAV_MAX_EDGE], rcost[NAV_MAX_EDGE];
    memset(rfirst, 0, sizeof(int) * (size_t)(n->nnode + 1));
    for (int e = 0; e < n->nedge; e++) rfirst[n->eto[e]]++;
    int acc = 0;
    for (int i = 0; i <= n->nnode; i++) { int t = i < n->nnode ? rfirst[i] : 0; rfirst[i] = acc; acc += t; }
    static int fill[NAV_MAX_NODE];
    memcpy(fill, rfirst, sizeof(int) * (size_t)n->nnode);
    for (int i = 0; i < n->nnode; i++)
        for (int e = n->efirst[i]; e < n->efirst[i + 1]; e++) {
            int j = n->eto[e];
            rto[fill[j]] = i; rcost[fill[j]] = n->ecost[e]; fill[j]++;
        }
    /* seeds: the goal cell and everything standable within 2 cells of it */
    static int q[NAV_MAX_NODE * 4];
    int qh = 0, qt = 0;
    int gc = n->goal_col, gr = n->goal_row;
    for (int dc = -2; dc <= 2; dc++) for (int dr = -2; dr <= 2; dr++) {
        int c = gc + dc, r = gr + dr;
        if (c < 0 || c >= g->map->width || r < 0 || r >= MAP_ROWS) continue;
        if (n->mode == NAV_DOOR && (dr || dc < -1 || dc > 1)) continue;
        int i = n->node_of[c][r];
        if (i < 0) continue;
        unsigned d0 = (unsigned)((dc < 0 ? -dc : dc) + (dr < 0 ? -dr : dr));
        if (d0 >= n->dist[i]) continue;
        n->dist[i] = (uint16_t)d0;
        q[qt++] = i;
    }
    /* plain Bellman-Ford style relaxation over a FIFO: the graph is small */
    while (qh < qt && qh < (int)(sizeof q / sizeof q[0]) - 8) {
        int v = q[qh++];
        unsigned dv = n->dist[v];
        for (int e = rfirst[v]; e < rfirst[v + 1]; e++) {
            int u = rto[e];
            unsigned nd = dv + (unsigned)rcost[e] + n->pen[n->ncol[u]][n->nrow[u]]
                        + (n->haz[n->ncol[u]][n->nrow[u]] ? NAV_HAZCOST : 0u);
            if (nd < n->dist[u]) {
                n->dist[u] = (uint16_t)nd;
                if (qt < (int)(sizeof q / sizeof q[0])) q[qt++] = u;
            }
        }
    }
    /* the same relaxation with the edges taken in both directions: a rough
     * "which way is the goal" for the places the directed field cannot leave */
    for (int i = 0; i < n->nnode; i++) n->udist[i] = n->dist[i];
    qh = qt = 0;
    for (int i = 0; i < n->nnode; i++) if (n->udist[i] < NAV_INF) q[qt++] = i;
    while (qh < qt && qh < (int)(sizeof q / sizeof q[0]) - 8) {
        int v = q[qh++];
        unsigned dv = n->udist[v];
        for (int e = rfirst[v]; e < rfirst[v + 1]; e++) {
            int u = rto[e];
            unsigned nd = dv + (unsigned)rcost[e] + 8;
            if (nd < n->udist[u]) { n->udist[u] = (uint16_t)nd; if (qt < (int)(sizeof q / sizeof q[0])) q[qt++] = u; }
        }
        for (int e = n->efirst[v]; e < n->efirst[v + 1]; e++) {
            int u = n->eto[e];
            unsigned nd = dv + (unsigned)n->ecost[e] + 8;
            if (nd < n->udist[u]) { n->udist[u] = (uint16_t)nd; if (qt < (int)(sizeof q / sizeof q[0])) q[qt++] = u; }
        }
    }
}

int nav_distance(Nav *n, const Game *g)
{
    int c = game_hero_map_col(g), r = game_hero_map_row(g);
    if (c < 0 || c >= g->map->width || r < 0 || r >= MAP_ROWS) return NAV_INF;
    int i = n->node_of[c][r];
    return i < 0 ? NAV_INF : n->dist[i];
}

/* debug: run one macro from one cell and describe the trajectory */
void nav_trace(Nav *n, const Game *g, int col, int row, int mi)
{
    memcpy(&n->navmap, g->map, sizeof n->navmap);
    probe_reset(n, g);
    Game *p = &n->probe;
    game_place(p, col, row, 0);
    probe_grab_ladder(p, col, row);
    p->nobj = 0; p->hp = p->max_hp = 9999;
    int fpos = 0, fx = probe_place_fixture(n, p, col, row, (uint8_t)(mi & 0x100 ? 0x80 : 0), &fpos);
    mi &= 0xFF;
    const Macro *m = &MACRO[mi];
    fprintf(stderr, "trace (%d,%d) macro %d '%s' fix %d pos %04x:", col, row, mi, m->name, fx, fpos);
    for (int f = 0; f < m->n; f++) {
        p->dirs = macro_dir(m, f); set_btn(p, m->btn); game_step(p);
        if (fx >= 0) fprintf(stderr, " {fix %d/%02x}", p->fix[fx].col, p->fix[fx].state);
        {int tl = game_hero_cell(p);
         fprintf(stderr, " [%d,%d v%02x f%02x rr%u L:%02x/%02x/%02x]", game_hero_map_col(p), game_hero_map_row(p),
                 p->vstate, p->hero_flags, p->rise_rows,
                 p->ring[tl], p->ring[game_ring_add(tl, RING_W)], p->ring[game_ring_add(tl, 2 * RING_W)]);}
    }
    for (int k = 0; k < SETTLE && !settled(p); k++) {
        p->dirs = 0; set_btn(p, 0); game_step(p);
        fprintf(stderr, " .%d,%d", game_hero_map_col(p), game_hero_map_row(p));
    }
    fprintf(stderr, "  => (%d,%d) settled=%d\n", game_hero_map_col(p), game_hero_map_row(p), settled(p));
}

/* ------------------------------------------------------------ goal setup */
static void reset(Nav *n, const Game *g)
{
    if (n->map != g->map) { n->built = 0; n->map = g->map; }
    n->cur_macro = -1; n->cur_frame = 0; n->expect_node = -1; n->cur_edge = -1;
    n->stall = 0; n->best_dist = NAV_INF; n->fixwait = 0;
    memset(n->pen, 0, sizeof n->pen);
}
void nav_goal_cell(Nav *n, const Game *g, int col, int row)
{
    reset(n, g);
    n->mode = NAV_REACH; n->goal_col = col; n->goal_row = row;
    n->door_col = n->door_row = -1;
    if (n->built) build_field(n, g);
}
void nav_goal_door(Nav *n, const Game *g, const Door *d)
{
    reset(n, g);
    n->mode = NAV_DOOR;
    n->goal_col = (int)d->col - 1; n->goal_row = d->row + 1;    /* 7A83 */
    n->door_col = d->col; n->door_row = d->row;
    if (n->built) build_field(n, g);
}
void nav_goal_break(Nav *n, const Game *g, int col, int row)
{
    nav_goal_cell(n, g, col, row);
    n->mode = NAV_BREAK;
}
void nav_goal_fight(Nav *n, const Game *g)
{
    reset(n, g);
    n->mode = NAV_FIGHT; n->goal_col = n->goal_row = -1;
    n->door_col = n->door_row = -1;
}

/* --------------------------------------------------------------- playing */

/* Where is the nearest live enemy relative to the hero's body column?  99 =
 * none within reach.  The sign is the map direction, not the facing, because
 * something that walks into his back hurts just as much (751F). */
static int enemy_near(const Game *g, int *side, int *below)
{
    int hc = g->hero_scr_col + 4;
    int best = 99, bs = 0;
    if (below) *below = 0;
    for (int i = 0; i < g->nobj; i++) {
        const MapObj *o = &g->obj[i];
        if ((o->col >> 8) == 0xFF || o->rcol == 0xFF) continue;
        if (o->type & 0x18) continue;                                   /* items / dying */
        int dc = (int)o->rcol - hc;
        int dr = (int)((o->row - (g->hero_scr_row + g->scroll_row)) & 0x3F);
        if (dr > 32) dr -= 64;
        if (dr < -3 || dr > 4) continue;
        int a = dc < 0 ? -dc : dc;
        if (a > 5) continue;
        if (below && a <= 2 && dr >= 2) *below = 1;
        if (a < best) { best = a; bs = dc < 0 ? -1 : 1; }
    }
    if (side) *side = bs;
    return best;
}

static void emit(Game *g, uint8_t dirs, uint8_t btn) { g->dirs = dirs; set_btn(g, btn); }
/* ... and never press "up" while standing under a door that is not the one we
 * are heading for: 7AF6 takes it on that single frame, wherever the press came
 * from -- a planned jump, the off-graph recovery, the anti-stuck shake.  The
 * survey cannot see this because the probe runs with `on_door` unhooked. */
static void nemit(const Nav *n, Game *g, uint8_t dirs, uint8_t btn)
{
    if ((dirs & DIR_UP) && on_foreign_door(n, g)) dirs &= (uint8_t)~DIR_UP;
    emit(g, dirs, btn);
}
static uint8_t macro_first_dir(const Nav *n, int e) { return MACRO[n->emacro[e]].dirs[0]; }

/* A safety net, not a movement model: the survey ran with the platform where
 * the probe put it, the live one has moved on since, so a plain walk that the
 * graph believes stays on the platform can in fact walk off the end of it.
 * Refuse a bare left/right step whose destination has neither solid ground
 * nor a fixture under it. */
static int step_walks_off(const Game *g, uint8_t dirs)
{
    if (dirs & (DIR_UP | DIR_DOWN)) return 0;
    if (!(dirs & (DIR_LEFT | DIR_RIGHT))) return 0;
    if (g->vstate || g->on_ladder) return 0;
    int w = g->map->width;
    /* Where his feet will be *after* the step: the cell fight.bin tests for
     * ground is 6D6E + 0x6D, i.e. his own map column + 1 (`game_hero_cell` is
     * at ring column [83]+4 and 0x6D is three rows down and one column right),
     * so a step ends over map column + 1 +/- 1.  This used to read map column
     * +/- 1 -- his *current* support cell going right, which always has ground
     * under it -- so the guard never fired on a step off the right-hand end of
     * anything, and MP10's row-0 floor (which stops at column 117, where the
     * fix[5] platform takes over) dropped the hero through the ring wrap every
     * single lap. */
    int bc = g->scroll_col + g->hero_scr_col + 5 + ((dirs & DIR_RIGHT) ? 1 : -1);
    while (bc < 0) bc += w;
    while (bc >= w) bc -= w;
    int feet = (g->hero_scr_row + g->scroll_row + 3) & 0x3F;
    if (!game_passable_body(g, g->map->grid[bc][feet])) return 0;       /* real ground */
    for (int i = 0; i < g->nfix; i++) {
        const Fixture *f = &g->fix[i];
        if ((f->row & 0x3F) != feet) continue;
        /* a patrolling platform moves *before* the hero does -- fixtures_draw
         * runs at the top of frame() and hero_input at the bottom of
         * game_step -- so the cell he lands on is held up by where the
         * platform will be, not where it is.  Accept both. */
        int lo = f->col, hi = f->col;
        if (f->kind == 2 && f->var) {
            int nx = (f->state & 0x80) ? (int)f->col - 1 : (int)f->col + 1;
            if (nx < lo) lo = nx;
            if (nx > hi) hi = nx;
        }
        if (bc >= lo && bc <= hi + 2) return 0;                         /* still carried */
    }
    /* 6B76's one-cell-gap walk-over: a hero *in mid-stride* keeps his footing
     * over a single empty cell as long as the cells either side of it are
     * solid (`if (hero_anim == 0x80) return 0; if (passable_body(ring[c-1]))
     * return 0; return !passable_body(ring[c+1])`).  Without this the guard
     * refused every step across such a hole -- MP31's row-7 corridor has one at
     * column 101 and Garland stood in front of it for the whole frame budget. */
    if (g->hero_anim != 0x80) {
        int a = bc - 1, b = bc + 1;
        while (a < 0) a += w;
        while (b >= w) b -= w;
        if (!game_passable_body(g, g->map->grid[a][feet]) &&
            !game_passable_body(g, g->map->grid[b][feet])) return 0;
    }
    return 1;
}

void nav_step(Nav *n, Game *g)
{
    if (g->walk_in || g->encounter_frames) { nemit(n, g, 0, 0); return; }

    if (n->mode == NAV_FIGHT) {                                 /* boss rooms */
        int hc = game_hero_map_col(g);
        int bc = g->boss.active ? (int)g->boss.col : hc;
        /* A boss part carries `hit` bit 5 for the frame after it is struck
         * (CRAB A6BC and its seven cousins), so `sword_apply` can only land a
         * blow every other frame however hard the button is held; standing in
         * contact the whole time just trades HP away.  Back off and let the
         * 719E regeneration run when the fight is going badly, and close again
         * once there is life to spend. */
        if (g->hp * 3 < g->max_hp) n->stall = 1;                /* retreat */
        else if (g->hp * 3 >= g->max_hp * 2) n->stall = 0;
        uint8_t d = 0;
        if (n->stall) {
            if (hc < bc) d = DIR_LEFT;
            else if (hc > bc) d = DIR_RIGHT;
            nemit(n, g, d, 0);
            return;
        }
        if (hc < bc - 2)      d = DIR_RIGHT;
        else if (hc > bc + 3) d = DIR_LEFT;
        nemit(n, g, d, (uint8_t)((g->frame_no & 3) < 2 ? 1 : 0));
        return;
    }

    if (n->map != g->map || !n->built) {
        n->map = g->map;
        build_graph(n, g);
        build_field(n, g);
        n->cur_macro = -1; n->best_dist = NAV_INF; n->stall = 0;
    }

    int hc = game_hero_map_col(g), hr = game_hero_map_row(g);

    /* Within the blade's reach of a breakable (8E32): face it and swing.  The
     * sword shape covers the two columns in front of the hero, and 6F8D only
     * marks a sprite whose ring cell the shape lands on, so "close enough" is
     * the hero's own column plus one or two on the side he faces. */
    if (n->mode == NAV_BREAK && settled(g)) {
        int dc = n->goal_col - hc, dr = n->goal_row - hr;
        if (dr < 0) dr = -dr;
        if (dr <= 1 && dc >= -2 && dc <= 2) {       /* 6F21's shape reaches two cells */
            int want_left = dc < 0;
            if (((g->hero_flags & FACE_LEFT) != 0) != want_left) {
                n->cur_macro = want_left ? 1 : 0; n->cur_frame = 1; n->cur_edge = -1;
                nemit(n, g, (uint8_t)(want_left ? DIR_LEFT : DIR_RIGHT), 0);
                return;
            }
            n->cur_macro = MACRO_STRIKE; n->cur_frame = 1; n->cur_edge = -1;
            nemit(n, g, 0, (uint8_t)((g->frame_no & 3) < 2 ? 1 : 0));
            return;
        }
    }

    /* standing in front of the target door: "up" opens it (7A83) */
    if (n->mode == NAV_DOOR && settled(g) && hr == n->door_row + 1 &&
        hc >= n->door_col - 1 && hc <= n->door_col + 1) {
        /* 7AF6 only takes the door when the arch is over the hero's own
         * column; one cell to either side it nudges him towards it -- but
         * only if he is already facing that way, so turn him first (6824). */
        if (hc != n->door_col) {
            int want_left = hc > n->door_col;
            if (((g->hero_flags & FACE_LEFT) != 0) != want_left) {
                nemit(n, g, (uint8_t)(want_left ? DIR_LEFT : DIR_RIGHT), 0);
                return;
            }
        }
        nemit(n, g, DIR_UP, 0);
        return;
    }

    /* An enemy that walks into the hero costs contact damage every frame
     * (751F), so a macro in flight is abandoned the moment one gets close. */
    int eside = 0, ebelow = 0, ea = enemy_near(g, &eside, &ebelow);
    if (n->cur_macro >= 0 && n->cur_macro != MACRO_STRIKE && ea <= 2) { n->cur_macro = -1; n->cur_edge = -1; }

    /* finish the macro in flight */
    if (n->cur_macro >= 0) {
        const Macro *m = &MACRO[n->cur_macro];
        if (n->cur_frame < m->n) {
            uint8_t d = macro_dir(m, n->cur_frame);
            /* same rule as edge_ok: only a move the survey saw stay level is
             * worth aborting when the ground under the next cell has gone */
            int guard = !(n->cur_edge >= 0 && n->cur_edge < n->nedge && n->expect_node >= 0 &&
                          !edge_self_moving(n, g, n->cur_edge) && !hero_carried(g) &&
                          n->nrow[n->expect_node] != game_hero_map_row(g));
            if (guard && step_walks_off(g, d)) { n->cur_macro = -1; n->cur_edge = -1; nemit(n, g, 0, 0); return; }
            n->cur_frame++;
            nemit(n, g, d, m->btn);
            return;
        }
        if (!settled(g) && n->cur_frame < m->n + SETTLE) { n->cur_frame++; nemit(n, g, 0, 0); return; }
        /* The survey's edges are executable by construction, but only from the
         * state the probe had: the live hero can arrive facing the other way,
         * mid-crouch, knocked back or with a platform somewhere else, and then
         * the same buttons end somewhere else.  Rather than let the planner
         * insist on an edge the executor cannot reproduce, count the misses
         * and stop offering it. */
        if (n->cur_edge >= 0 && n->cur_edge < n->nedge && n->expect_node >= 0) {
            int c = game_hero_map_col(g), r = game_hero_map_row(g);
            int at = (c >= 0 && c < g->map->width && r >= 0 && r < MAP_ROWS) ? n->node_of[c][r] : -1;
            if (at != n->expect_node && n->efail[n->cur_edge] < 250) n->efail[n->cur_edge]++;
            else if (at == n->expect_node && n->efail[n->cur_edge]) n->efail[n->cur_edge]--;
        }
        n->cur_edge = -1;
        n->cur_macro = -1;
    }

    if (!settled(g)) { nemit(n, g, 0, 0); return; }                 /* let gravity finish */

    /* Deal with whatever is standing in the way: back off out of contact
     * range first and let the blade do the work from a cell away. */
    /* Rooted to the spot (an enemy under his feet, a platform that has moved
     * away, a probe that lied): shake it off with an arbitrary move.  Being
     * carried does not count: the platform is doing the moving. */
    int carried = hero_carried(g);
    if (hc == n->last_col && hr == n->last_row && !carried && !n->fixwait) {
        if (++n->same > 60) {
            n->same = 0;
            for (int c = hc - 1; c <= hc + 1; c++)
                for (int r = hr - 1; r <= hr + 1; r++)
                    if (c >= 0 && c < g->map->width && r >= 0 && r < MAP_ROWS && n->pen[c][r] < 60)
                        n->pen[c][r] = (uint8_t)(n->pen[c][r] + 8);
            build_field(n, g);
            n->best_dist = NAV_INF;
            n->cur_macro = (int)((g->frame_no >> 3) % (unsigned)MACRO_MOVE_N);
            n->cur_frame = 1; n->cur_edge = -1;
            nemit(n, g, macro_dir(&MACRO[n->cur_macro], 0), MACRO[n->cur_macro].btn);
            return;
        }
    } else { n->same = 0; n->last_col = hc; n->last_row = hr; }

    if (ebelow) {                       /* standing on something: 6E3B's down-thrust */
        n->cur_macro = MACRO_THRUST; n->cur_frame = 1; n->cur_edge = -1;
        nemit(n, g, DIR_DOWN, 1);
        return;
    }
    if (ea <= 4) {
        /* In contact (751F charges every frame): get out of it before anything
         * else, and give the move four frames.  A one-frame step is not enough:
         * 6824 spends the first frame of a move in the wrong facing turning the
         * hero round without moving him, so "turn to face it, then step away"
         * as two one-frame macros just flips his facing back and forth on the
         * spot while the enemy eats him -- which is exactly how Garland died
         * beside the (149,50) crawler with a full 800 LIFE. */
        if (ea <= 1) {
            int away_left = eside > 0;                      /* it is on his right */
            n->cur_macro = away_left ? 3 : 2;               /* L4 / R4 */
            n->cur_frame = 1; n->cur_edge = -1;
            nemit(n, g, (uint8_t)(away_left ? DIR_LEFT : DIR_RIGHT), 0);
            return;
        }
        int left = (g->hero_flags & FACE_LEFT) != 0;
        int face_ok = (eside < 0) == (left != 0);
        if (!face_ok) {                                     /* turn to it first (6824) */
            n->cur_macro = eside < 0 ? 1 : 0; n->cur_frame = 1; n->cur_edge = -1;
            nemit(n, g, (uint8_t)(eside < 0 ? DIR_LEFT : DIR_RIGHT), 0);
            return;
        }
        n->cur_macro = MACRO_STRIKE; n->cur_frame = 1; n->cur_edge = -1;
        nemit(n, g, 0, 1);
        return;
    }

    /* Riding a patrolling platform (81AE): it slides a column at a time under
     * his feet, so standing still runs him off the trailing end.  A player
     * walks with it; do the same whenever he is on the last cell the platform
     * still covers on the side it is leaving. */
    int rf = carried_fixture(g);
    if (rf >= 0) {
        const Fixture *f = &g->fix[rf];
        int left = (f->state & 0x80) != 0, bc = hc + 1;
        /* he is carried while f->col <= bc <= f->col+2; one more step of the
         * platform in the direction it is going and he is off its trailing
         * end.  The test is `== bc`, not `== bc-1`: firing a column early
         * walks him off the *leading* end instead, which is how MP20's
         * 11-column gap at columns 62-72 (fix[5], half speed) dropped him
         * through the ring wrap every single time. */
        int feet = (hr + 3) & 0x3F, w = g->map->width;
        int rc = bc + 1, lc = bc - 1;                   /* beside him */
        int re = (int)f->col + 3, le = (int)f->col - 1; /* beyond the platform's ends */
        while (rc >= w) rc -= w;
        while (lc < 0) lc += w;
        while (re >= w) re -= w;
        while (le < 0) le += w;
        int beside = !game_passable_body(g, g->map->grid[rc][feet]) ||
                     !game_passable_body(g, g->map->grid[lc][feet]);
        /* Ground of his own within a step: this is the far side, so hand him
         * back to the planner -- edge_ok's walks-off test guards the hop.
         * Holding the ride here is what kept him going round with the platform
         * for ever: the moment it turns, the trailing guard drags him back. */
        if (!beside) {
            if (!left && (int)f->col >= bc) { n->cur_macro = 0; n->cur_frame = 1; n->cur_edge = -1; nemit(n, g, DIR_RIGHT, 0); return; }
            if ( left && (int)f->col + 2 <= bc) { n->cur_macro = 1; n->cur_frame = 1; n->cur_edge = -1; nemit(n, g, DIR_LEFT, 0); return; }
            /* Mid-platform over a gap: stand still and be carried.  The survey
             * makes every column the platform can reach a node, so the field is
             * happy to walk straight across the gap -- and a hero walking a
             * column a frame outruns a half-speed platform and steps off its
             * front.  A player waits, and so does this: until the platform has
             * carried him to within a step of ground at one end or the other. */
            if (game_passable_body(g, g->map->grid[re][feet]) &&
                game_passable_body(g, g->map->grid[le][feet])) {
                n->cur_macro = MACRO_WAIT; n->cur_frame = 1; n->cur_edge = -1;
                n->fixwait++;
                nemit(n, g, 0, 0);
                return;
            }
        }
    }

    int here = (hc >= 0 && hc < g->map->width && hr >= 0 && hr < MAP_ROWS) ? n->node_of[hc][hr] : -1;
    if (here < 0) {
        /* Off the graph.  fight.bin's own resting rule (6B76: only the cell at
         * row+3 has to be solid) is looser than `standable`, so a fall with a
         * direction held can wedge the hero in a cell whose lowest body cell is
         * rock -- MP10 (114,21) under the col-112 ladder is one.  63DA does not
         * free him (his top cell is clear), and walking at the goal column just
         * pushes him further into the wall, so head for the nearest *node*
         * instead, preferring the one the field likes. */
        int bc = -1, br = 0; unsigned bd = ~0u;
        for (int dc = -4; dc <= 4; dc++)
            for (int dr = -3; dr <= 3; dr++) {
                int c = hc + dc, r = (hr + dr) & 0x3F;
                if (c < 0 || c >= g->map->width) continue;
                int j = n->node_of[c][r];
                if (j < 0) continue;
                unsigned d = (unsigned)n->udist[j] * 16u
                           + (unsigned)((dc < 0 ? -dc : dc) + (dr < 0 ? -dr : dr));
                if (d < bd) { bd = d; bc = c; br = r; }
            }
        if (bc < 0) { n->cur_macro = (n->goal_col >= hc) ? 0 : 1; }
        else if (br < hr)  n->cur_macro = bc < hc ? 7 : bc > hc ? 6 : 14;   /* jump up-ish */
        else               n->cur_macro = bc < hc ? 1 : bc > hc ? 0 : 14;
        n->cur_frame = 1; n->cur_edge = -1;
        nemit(n, g, MACRO[n->cur_macro].dirs[0], 0);
        return;
    }

    /* Hurt and out of trouble: stand still and let the 719E regeneration run
     * (2 HP every 16 frames) rather than walking into the next fight. */
    if (g->hp * 3 < g->max_hp * 2 && ea > 8) {
        if (g->hp * 4 < g->max_hp * 3) { n->cur_macro = MACRO_WAIT; n->cur_frame = 1; n->cur_edge = -1; nemit(n, g, 0, 0); return; }
    }

    /* anti-stuck: when the distance stops falling, make this patch expensive.
     * A ride is exempt — a platform can spend a whole span carrying him the
     * wrong way before it turns round, and that is not being stuck. */
    if (n->dist[here] < n->best_dist) { n->best_dist = n->dist[here]; n->stall = 0; }
    else if (carried || n->fixwait) { /* the platform owns the clock */ }
    else if (++n->stall > 12) {
        n->stall = 0;
        for (int c = hc - 1; c <= hc + 1; c++)
            for (int r = hr - 1; r <= hr + 1; r++)
                if (c >= 0 && c < g->map->width && r >= 0 && r < MAP_ROWS && n->pen[c][r] < 60)
                    n->pen[c][r] = (uint8_t)(n->pen[c][r] + 12);
        build_field(n, g);
        n->best_dist = n->dist[here];
    }

    /* The cheapest outgoing edge that is executable *now*: an edge probed with
     * a fixture under the hero only exists while the fixture is there, so a
     * platform the hero has to board is simply an edge that is not ready yet
     * and "wait for it" is what the planner does on its own. */
    /* An edge is a candidate when the node it lands on is strictly closer to
     * the goal than this one; among those the cheapest `dist + cost` wins.
     * (Testing `dist[to] + cost < dist[here]` instead would reject the very
     * edge the field was built from, because `build_field` sets
     * `dist[here] = dist[to] + cost` exactly wherever the anti-stuck penalty
     * is still zero — which is everywhere the hero has not been stuck yet.) */
    int best = -1, gated = 0; unsigned bestd = 2u * NAV_INF;
    for (int e = n->efirst[here]; e < n->efirst[here + 1]; e++) {
        int j = n->eto[e];
        if (n->efail[e] >= 3) continue;
        if (n->dist[j] >= n->dist[here]) continue;
        unsigned d = (unsigned)n->dist[j] + n->ecost[e];
        if (d >= bestd) continue;
        if (!edge_ok(n, g, e, here)) { gated = 1; continue; }
        bestd = d; best = e;
    }
    if (best < 0) {
        /* Every improving edge has been dropped as unrepeatable.  On a moving
         * platform that verdict is usually about *phase*, not about the move:
         * the hop from MP10's fix[2] onto the col-5 ladder at (4,37) only works
         * from the column the probe took off at, and three misses in a row say
         * nothing about the fourth.  Give them back rather than settle for
         * riding up and down for ever -- the field will offer them again the
         * next time the hero is on that cell, and by then the platform will
         * have come round. */
        int dropped = 0;
        for (int e = n->efirst[here]; e < n->efirst[here + 1]; e++)
            if (n->efail[e] >= 3 && n->dist[n->eto[e]] < n->dist[here]) { n->efail[e] = 0; dropped = 1; }
        if (dropped) {
            bestd = 2u * NAV_INF;
            for (int e = n->efirst[here]; e < n->efirst[here + 1]; e++) {
                int j = n->eto[e];
                if (n->dist[j] >= n->dist[here]) continue;
                unsigned d = (unsigned)n->dist[j] + n->ecost[e];
                if (d >= bestd) continue;
                if (!edge_ok(n, g, e, here)) { gated = 1; continue; }
                bestd = d; best = e;
            }
        }
    }
    if (best < 0 && gated) {
        /* The improving edges exist; they are only waiting on a platform that
         * has not come round yet.  Stand still one frame at a time and look
         * again -- and do it *before* the undirected fallback below, because
         * that fallback will happily take a one-way drop out of the field
         * altogether.  MP10's (57,50) is the case: its two westward edges share
         * a gate, and for the two frames in every eighty-four when fix[3] is
         * one column short of its right limit the harmless-looking one -- a
         * walk clean over the twenty-four-column gap into the pit at (52,56),
         * which the field cannot leave -- reads as ready (its landing cell is
         * solid rock, so `edge_ready` stops caring which way the platform is
         * going) while the useful one, a ride, still does not.  If the platform
         * never arrives the patch gets expensive like any other dead end and
         * the fallback gets its turn. */
#ifdef NAV_DEBUG
        { fprintf(stderr, "   [gated] (%d,%d) fw%d fix7 %d/%02x |", hc, hr, n->fixwait, g->fix[7].col, g->fix[7].state);
          for (int e = n->efirst[here]; e < n->efirst[here+1]; e++)
            if (n->dist[n->eto[e]] < n->dist[here])
              fprintf(stderr, " e%d m%d ->(%d,%d) pos%04x rdy%d ok%d f%d", e, n->emacro[e],
                      n->ncol[n->eto[e]], n->nrow[n->eto[e]], n->efixpos[e], edge_ready(n,g,e), edge_ok(n,g,e,here), n->efail[e]);
          fprintf(stderr, "\n"); }
#endif
        if (++n->fixwait > 240) {
            n->fixwait = 0;
            if (n->pen[hc][hr] < 60) n->pen[hc][hr] = (uint8_t)(n->pen[hc][hr] + 12);
            build_field(n, g);
        } else {
            nemit(n, g, 0, 0);
            return;
        }
    }
    if (best < 0) {
        /* no outgoing edge improves the distance.  Either the goal is not
         * reachable from here at all (a one-way drop) or this is a local
         * minimum: fall back on the undirected field, which at least points
         * at the goal, and let the anti-stuck bumps do the rest. */
        bestd = 2u * NAV_INF;
        for (int e = n->efirst[here]; e < n->efirst[here + 1]; e++) {
            int j = n->eto[e];
            if (n->efail[e] >= 3) continue;
            if (n->udist[j] >= n->udist[here]) continue;
            unsigned d = (unsigned)n->udist[j] + n->ecost[e];
            if (d >= bestd) continue;
            if (!edge_ok(n, g, e, here)) continue;
            bestd = d; best = e;
        }
    }
    n->fixwait = 0;
    if (best < 0) { n->cur_macro = MACRO_WAIT; n->cur_frame = 1; n->cur_edge = -1; nemit(n, g, 0, 0); return; }
    /* The facing is part of the move: 65C5 mounts the ladder beside the hero
     * only on the side he faces, and 6634/67BC spend the first frame turning
     * (6824) instead of moving.  So put him the way the probe had him before
     * the macro starts -- one frame of the direction he must end up facing,
     * which turn_around consumes without moving him. */
    if (((g->hero_flags & FACE_LEFT) != 0) != (n->eface[best] != 0)) {
        nemit(n, g, (uint8_t)(n->eface[best] ? DIR_LEFT : DIR_RIGHT), 0);
        return;
    }
    /* ... and not crouching.  6B41 crouches him for two frames after a fall of
     * two rows or more, and a crouch swallows 663E/67BC's step (and with it
     * 65C5's "walk onto the ladder beside you"), so a macro started in that
     * window does something else entirely.  Two idle frames clear it (6B41's
     * [9F0A] counter). */
    if (g->crouching) { nemit(n, g, 0, 0); return; }
#ifdef NAV_DEBUG
    fprintf(stderr, "   [pick] (%d,%d) e%d m%d -> (%d,%d) d%d->%d pos%04x fail%d\n", hc, hr, best,
            n->emacro[best], n->ncol[n->eto[best]], n->nrow[n->eto[best]],
            n->dist[here], n->dist[n->eto[best]], n->efixpos[best], n->efail[best]);
#endif
    const Macro *m = &MACRO[n->emacro[best]];
    n->cur_macro = n->emacro[best];
    n->last_macro = n->cur_macro;
    n->cur_frame = 1;
    n->cur_edge = best;
    n->expect_node = n->eto[best];
    nemit(n, g, macro_dir(m, 0), m->btn);
}
