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
    /* the fixture rides: only probed from a node a fixture holds up, because
     * "stand still and be carried" is a move only a moving platform offers */
    {M_0,   1,  4, 0, "ride 4"},        /* 17 */
    {M_0,   1,  8, 0, "ride 8"},        /* 18 */
    {M_D,   1,  3, 0, "sink 3"},        /* 19 */
    /* not survey moves: the executor's own idle and swing */
    {M_0,   1,  4, 0, "wait"},          /* 20 */
    {M_0,   1,  3, 1, "thrust"},        /* 21 */
};
#define NMACRO   ((int)(sizeof MACRO / sizeof MACRO[0]))
#define MACRO_MOVE_N  17                /* 0..16: probed from every node */
#define MACRO_RIDE0   17                /* 17..19: probed from fixture nodes */
#define MACRO_RIDE1   19
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
static int settled(const Game *g) { return (g->vstate == V_GROUND || g->on_ladder) && !g->walk_in; }

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
/* the rows an elevator (7FDC/8074, both ways) or a gate (818E, down only) can
 * be at.  The hero drives both, so every one of those rows is real ground. */
static void fixture_rows(const Map *m, const Fixture *f, int *lo, int *hi)
{
    int home = f->row & 0x3F, r = home;
    if (f->kind == 0)
        while (r > 0 && home - r < FIX_ROW_SPAN && fixture_row_free(m, f, r - 1)) r--;
    *lo = r;
    r = home;
    while (r < MAP_ROWS - 1 && r - home < FIX_ROW_SPAN && fixture_row_free(m, f, r + 1)) r++;
    *hi = r;
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
            int rlo, rhi;
            fixture_rows(m, f, &rlo, &rhi);
            if ((r & 0x3F) < rlo || (r & 0x3F) > rhi) continue;
        }
        fixture_cols(f, &lo, &hi);
        if (bc >= lo && bc <= hi) return i;
    }
    return -1;
}
static int fixture_moves(const Fixture *f) { return f->kind != 2 || f->var != 0; }

static int standable(const Game *g, int c, int r)
{
    int bc = c + 1;
    if (!fits(g, bc, r)) return 0;
    if (!passable_here(g, bc, r + 3, 1)) return 1;                      /* solid floor */
    if (fixture_at(g->map, bc, r + 3) >= 0) return 1;
    return is_ladder(g, bc, r) || is_ladder(g, bc, r + 1);
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
static int edge_ready(const Nav *n, const Game *g, int e)
{
    int fx = n->efix[e];
    if (!fx || fx > g->nfix) return 1;
    const Fixture *f = &g->fix[fx - 1];
    if (f->kind == 2) {
        if (((f->state & 0x80) != 0) != ((n->efixpos[e] & NAV_FIXDIR) != 0)) return 0;
        int d = (int)f->col - (int)(n->efixpos[e] & ~NAV_FIXDIR);
        return d >= -1 && d <= 1;
    }
    return (f->row & 0x3F) == (n->efixpos[e] & 0x3F);
}
/* ... and does its first step keep him on something? */
static int edge_ok(const Nav *n, const Game *g, int e)
{
    return edge_ready(n, g, e) && !step_walks_off(g, macro_first_dir(n, e));
}
/* is the hero being carried by something that moves?  (his cell changes on its
 * own, so the stall detector must not read that as progress or as a stall) */
static int hero_carried(const Game *g)
{
    if (g->vstate || g->on_ladder) return 0;
    int bc = g->hero_scr_col + 4 + g->scroll_col, feet = (g->hero_scr_row + g->scroll_row + 3) & 0x3F;
    while (bc >= g->map->width) bc -= g->map->width;
    for (int i = 0; i < g->nfix; i++) {
        const Fixture *f = &g->fix[i];
        if (!fixture_moves(f)) continue;
        if ((f->row & 0x3F) != feet) continue;
        if (bc >= (int)f->col && bc <= (int)f->col + 2) return 1;
    }
    return 0;
}

/* run one macro from the hero's current probe state; returns the number of
 * frames used, and leaves the probe wherever the hero settled. */
static int run_macro(Nav *n, Game *p, int mi)
{
    const Macro *m = &MACRO[mi];
    int f = 0;
    for (; f < m->n; f++) {
        p->dirs = macro_dir(m, f);
        set_btn(p, m->btn);
        game_step(p);
        n->steps++;
        if (p->hero_dead) return f + 1;
    }
    for (int k = 0; k < SETTLE && !settled(p); k++) {
        p->dirs = 0; set_btn(p, 0);
        game_step(p);
        n->steps++; f++;
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
    for (int c = 0; c < W && n->nnode < NAV_MAX_NODE; c++)
        for (int r = 0; r < MAP_ROWS; r++)
            if (standable(g, c, r)) {
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
        int ndir = (onfix >= 0 && n->navmap.fix[onfix].kind == 2) ? 2 : 1;
        for (int d = 0; d < ndir; d++) {
            uint8_t st = (uint8_t)(d ? 0x80 : 0);
            for (int mi = 0; mi <= last; mi++) {
                if (d && mi < MACRO_RIDE0) continue;         /* both ways only for the rides */
                probe_reset(n, g);
                Game *p = &n->probe;
                game_place(p, nc, nr, 0);
                p->nobj = 0;                                /* no enemies while surveying */
                p->hp = p->max_hp = 9999;
                int fpos = 0;
                int fx = probe_place_fixture(n, p, nc, nr, st, &fpos);
                int cost = run_macro(n, p, mi);
                if (p->hero_dead || !settled(p)) continue;
                int c = game_hero_map_col(p), r = game_hero_map_row(p);
                if (c < 0 || c >= W || r < 0 || r >= MAP_ROWS) continue;
                int j = n->node_of[c][r];
                if (j < 0 || j == i) continue;
                if (n->nedge >= NAV_MAX_EDGE) break;
                n->eto[n->nedge] = j;
                n->emacro[n->nedge] = (uint8_t)mi;
                n->ecost[n->nedge] = (uint8_t)(cost < 1 ? 1 : cost > 60 ? 60 : cost);
                n->efix[n->nedge] = (uint8_t)(fx + 1);
                n->efixpos[n->nedge] = (uint16_t)fpos;
                n->nedge++;
            }
        }
    }
    n->efirst[n->nnode] = n->nedge;
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
            unsigned nd = dv + (unsigned)rcost[e] + n->pen[n->ncol[u]][n->nrow[u]];
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
    p->nobj = 0; p->hp = p->max_hp = 9999;
    const Macro *m = &MACRO[mi];
    fprintf(stderr, "trace (%d,%d) macro %d '%s':", col, row, mi, m->name);
    for (int f = 0; f < m->n; f++) {
        p->dirs = macro_dir(m, f); set_btn(p, m->btn); game_step(p);
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
    n->cur_macro = -1; n->cur_frame = 0; n->expect_node = -1;
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
    int bc = g->scroll_col + g->hero_scr_col + 4 + ((dirs & DIR_RIGHT) ? 1 : -1);
    while (bc < 0) bc += w;
    while (bc >= w) bc -= w;
    int feet = (g->hero_scr_row + g->scroll_row + 3) & 0x3F;
    if (!game_passable_body(g, g->map->grid[bc][feet])) return 0;       /* real ground */
    for (int i = 0; i < g->nfix; i++) {
        const Fixture *f = &g->fix[i];
        if ((f->row & 0x3F) != feet) continue;
        if (bc >= (int)f->col && bc <= (int)f->col + 2) return 0;       /* still carried */
    }
    return 1;
}

void nav_step(Nav *n, Game *g)
{
    if (g->walk_in || g->encounter_frames) { emit(g, 0, 0); return; }

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
            emit(g, d, 0);
            return;
        }
        if (hc < bc - 2)      d = DIR_RIGHT;
        else if (hc > bc + 3) d = DIR_LEFT;
        emit(g, d, (uint8_t)((g->frame_no & 3) < 2 ? 1 : 0));
        return;
    }

    if (n->map != g->map || !n->built) {
        n->map = g->map;
        build_graph(n, g);
        build_field(n, g);
        n->cur_macro = -1; n->best_dist = NAV_INF; n->stall = 0;
    }

    int hc = game_hero_map_col(g), hr = game_hero_map_row(g);

    /* standing in front of the target door: "up" opens it (7A83) */
    if (n->mode == NAV_DOOR && settled(g) && hr == n->door_row + 1 &&
        hc >= n->door_col - 1 && hc <= n->door_col + 1) {
        emit(g, DIR_UP, 0);
        return;
    }

    /* An enemy that walks into the hero costs contact damage every frame
     * (751F), so a macro in flight is abandoned the moment one gets close. */
    int eside = 0, ebelow = 0, ea = enemy_near(g, &eside, &ebelow);
    if (n->cur_macro >= 0 && n->cur_macro != MACRO_STRIKE && ea <= 2) n->cur_macro = -1;

    /* finish the macro in flight */
    if (n->cur_macro >= 0) {
        const Macro *m = &MACRO[n->cur_macro];
        if (n->cur_frame < m->n) {
            uint8_t d = macro_dir(m, n->cur_frame);
            if (step_walks_off(g, d)) { n->cur_macro = -1; emit(g, 0, 0); return; }
            n->cur_frame++;
            emit(g, d, m->btn);
            return;
        }
        if (!settled(g) && n->cur_frame < m->n + SETTLE) { n->cur_frame++; emit(g, 0, 0); return; }
        n->cur_macro = -1;
    }

    if (!settled(g)) { emit(g, 0, 0); return; }                 /* let gravity finish */

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
            n->cur_frame = 1;
            emit(g, macro_dir(&MACRO[n->cur_macro], 0), MACRO[n->cur_macro].btn);
            return;
        }
    } else { n->same = 0; n->last_col = hc; n->last_row = hr; }

    if (ebelow) {                       /* standing on something: 6E3B's down-thrust */
        n->cur_macro = MACRO_THRUST; n->cur_frame = 1;
        emit(g, DIR_DOWN, 1);
        return;
    }
    if (ea <= 4) {
        int left = (g->hero_flags & FACE_LEFT) != 0;
        int face_ok = (eside < 0) == (left != 0);
        if (!face_ok) {                                     /* turn to it first (6824) */
            n->cur_macro = eside < 0 ? 1 : 0; n->cur_frame = 1;
            emit(g, (uint8_t)(eside < 0 ? DIR_LEFT : DIR_RIGHT), 0);
            return;
        }
        if (ea <= 1) {                                      /* too close: step out of contact */
            n->cur_macro = eside < 0 ? 0 : 1; n->cur_frame = 1;
            emit(g, (uint8_t)(eside < 0 ? DIR_RIGHT : DIR_LEFT), 0);
            return;
        }
        n->cur_macro = MACRO_STRIKE; n->cur_frame = 1;
        emit(g, 0, 1);
        return;
    }

    int here = (hc >= 0 && hc < g->map->width && hr >= 0 && hr < MAP_ROWS) ? n->node_of[hc][hr] : -1;
    if (here < 0) {                     /* off the graph: head for the goal column */
        n->cur_macro = (n->goal_col >= hc) ? 0 : 1; n->cur_frame = 1;
        emit(g, MACRO[n->cur_macro].dirs[0], 0);
        return;
    }

    /* Hurt and out of trouble: stand still and let the 719E regeneration run
     * (2 HP every 16 frames) rather than walking into the next fight. */
    if (g->hp * 3 < g->max_hp * 2 && ea > 8) {
        if (g->hp * 4 < g->max_hp * 3) { n->cur_macro = MACRO_WAIT; n->cur_frame = 1; emit(g, 0, 0); return; }
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
        if (n->dist[j] >= n->dist[here]) continue;
        unsigned d = (unsigned)n->dist[j] + n->ecost[e];
        if (d >= bestd) continue;
        if (!edge_ok(n, g, e)) { gated = 1; continue; }
        bestd = d; best = e;
    }
    if (best < 0) {
        /* no outgoing edge improves the distance.  Either the goal is not
         * reachable from here at all (a one-way drop) or this is a local
         * minimum: fall back on the undirected field, which at least points
         * at the goal, and let the anti-stuck bumps do the rest. */
        bestd = 2u * NAV_INF;
        for (int e = n->efirst[here]; e < n->efirst[here + 1]; e++) {
            int j = n->eto[e];
            if (n->udist[j] >= n->udist[here]) continue;
            unsigned d = (unsigned)n->udist[j] + n->ecost[e];
            if (d >= bestd) continue;
            if (!edge_ok(n, g, e)) { gated = 1; continue; }
            bestd = d; best = e;
        }
    }
    if (best < 0 && gated) {
        /* the only way on is a platform that has not come round yet: stand
         * still one frame at a time and look again.  If it never arrives the
         * patch gets expensive like any other dead end. */
        if (++n->fixwait > 240) {
            n->fixwait = 0;
            if (n->pen[hc][hr] < 60) n->pen[hc][hr] = (uint8_t)(n->pen[hc][hr] + 12);
            build_field(n, g);
        }
        emit(g, 0, 0);
        return;
    }
    n->fixwait = 0;
    if (best < 0) { n->cur_macro = MACRO_WAIT; n->cur_frame = 1; emit(g, 0, 0); return; }
    const Macro *m = &MACRO[n->emacro[best]];
    n->cur_macro = n->emacro[best];
    n->last_macro = n->cur_macro;
    n->cur_frame = 1;
    n->expect_node = n->eto[best];
    emit(g, macro_dir(m, 0), m->btn);
}
