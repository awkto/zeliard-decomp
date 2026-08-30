/* playthrough.c — the route driver (see playthrough.h). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "playthrough.h"
#include "render.h"
#include "player.h"
#include "shop.h"

#define VLOG(p, ...) do { if ((p)->verbose) fprintf(stderr, __VA_ARGS__); } while (0)

static const PStep *cur(Play *p) { return &p->route[p->step]; }

/* ---------------------------------------------------------------- shops */
/* The shop overlays take over the frame loop, so the driver has to answer
 * them frame by frame: page the text with the select button and, whenever
 * menu_select opens a widget, walk the cursor to the row the route names. */
static void drive_shop(Play *p, Town *t)
{
    Shop *s = t->shop;
    t->dirs = 0; t->buttons = 0; t->menu_key = 0;
    if (!s) return;
    if (!s->in_menu) {                                  /* text: tap select */
        p->menu_press = 0;
        if ((p->frames & 1) == 0) t->btn1_edge = 0xFF;
        return;
    }
    const char *m = cur(p)->menu;
    char want = (m && p->menu_pos < (int)strlen(m)) ? m[p->menu_pos] : 'c';
    if (want == 'c') {                                  /* cancel out */
        if ((p->frames & 1) == 0) t->btn2_edge = 0xFF;
        p->menu_pos++;
        return;
    }
    int target = (want == 'y') ? 0 : (want == 'n') ? 1 : want - '0';
    if (target >= s->menu_n) target = s->menu_n - 1;
    if (s->menu_row < target)      { if (!p->menu_press) t->dirs = DIR_DOWN; p->menu_press ^= 1; return; }
    else if (s->menu_row > target) { if (!p->menu_press) t->dirs = DIR_UP;   p->menu_press ^= 1; return; }
    t->btn1_edge = 0xFF;                                /* on the row: select */
    p->menu_pos++;
    p->menu_press = 0;
}

/* ---------------------------------------------------------------- towns */
static void drive_town(Play *p, Town *t)
{
    t->dirs = 0; t->buttons = 0; t->menu_key = 0;
    if (t->dlg.active) {                                /* page a dialogue box */
        if ((p->frames & 1) == 0) t->btn1_edge = 0xFF;
        return;
    }
    const PStep *st = cur(p);
    int hc = town_hero_col(t);
    if (st->kind == P_TOWN_EDGE) { t->dirs = st->a ? DIR_RIGHT : DIR_LEFT; return; }
    if (st->kind == P_TOWN_SHOP || st->kind == P_TOWN_CAVE) {
        if (hc < st->a - 1)      t->dirs = DIR_RIGHT;
        else if (hc > st->a + 1) t->dirs = DIR_LEFT;
        else                     t->dirs = DIR_UP;
        return;
    }
    /* an unexpected town (a death, say): stand still */
}

static void town_present_cb(Town *t)
{
    Play *p = ((Shell *)t->user)->user;
    p->frames++;
    if (t->status) { t->menu_key = 0; t->status->done = 1; return; }
    if (t->shop) { p->shop_seen = 1; drive_shop(p, t); return; }
    drive_town(p, t);
    if (p->frames > p->budget) { t->quit = 1; p->failed = 1; }
}

/* -------------------------------------------------------------- caverns */
static void present_cb(Game *g)
{
    Play *p = ((Shell *)g->user)->user;
    p->frames++;
    g->menu_key = 0;
    if (g->boss_defeated) p->boss_seen = 1;     /* 72F1 clears it again */
    nav_step(&p->nav, g);
    if (p->verbose > 1 && (p->frames % 20) == 0)
        fprintf(stderr, "  [nav] f%u %s (%d,%d) v=%02x dist %d hp %u haz %u shield %u m%d\n", p->frames, g->map->name,
                game_hero_map_col(g), game_hero_map_row(g), g->vstate, nav_distance(&p->nav, g), g->hp,
                g->hazard_frames, g->shield_hp, p->nav.last_macro);
    if (p->frames > p->budget) p->failed = 1;
}

/* ------------------------------------------------------------ the route */
static const Door *find_door(const Map *m, int col, int row)
{
    for (int i = 0; i < m->ndoors; i++)
        if (m->doors[i].col == col && m->doors[i].row == row) return &m->doors[i];
    return NULL;
}

static void set_goal(Play *p)
{
    Game *g = &p->sh.g;
    const PStep *st = cur(p);
    if (p->sh.in_town) return;
    switch (st->kind) {
    case P_CAV_DOOR: {
        /* a < 0 = "the door this map has": 72F1 puts the post-boss exit door
         * on whatever column the hero was standing on */
        const Door *d = st->a < 0 ? (g->map->ndoors ? &g->map->doors[0] : NULL)
                                  : find_door(g->map, st->a, st->b);
        if (!d) { snprintf(p->msg, sizeof p->msg, "step %d (%s): %s has no door at (%d,%d)",
                           p->step, st->what, g->map->name, st->a, st->b); p->failed = 1; return; }
        nav_goal_door(&p->nav, g, d);
        break; }
    case P_CAV_BREAK:
    case P_CAV_CELL:
        if (st->kind == P_CAV_BREAK) nav_goal_break(&p->nav, g, st->a, st->b);
        else                         nav_goal_cell(&p->nav, g, st->a, st->b);
        p->item_obj = -1;
        for (int i = 0; i < g->nobj; i++)
            if (g->obj[i].col == (uint16_t)st->a && g->obj[i].row == (uint8_t)st->b) { p->item_obj = i; break; }
        break;
    case P_BOSS:     nav_goal_fight(&p->nav, g); break;
    case P_FARM:     nav_goal_cell(&p->nav, g, p->farm_end ? st->b : st->a, game_hero_map_row(g)); break;
    default:         nav_goal_cell(&p->nav, g, game_hero_map_col(g), game_hero_map_row(g)); break;
    }
    p->step_map = g->map;
}

static void step_enter(Play *p)
{
    Game *g = &p->sh.g;
    const PStep *st = cur(p);
    p->step_start = p->frames;
    p->shop_seen = 0; p->menu_pos = 0; p->menu_press = 0; p->farm_end = 0; p->boss_seen = 0;
    p->town_at_start = p->sh.in_town ? p->sh.tmap.index : -1;
    p->step_map = g->map;
    if (st->kind == P_END) { p->done = 1; return; }
    VLOG(p, "[play] step %d: %s\n", p->step, st->what ? st->what : "?");
    /* The two "put him there" steps are the shell calls a door or a town gate
     * makes (7B32 / 6FF8); they exist because the autopilot cannot yet cross
     * MP10's lower level unaided.  Everything after them is played. */
    if (st->kind == P_GOTO) {
        if (!shell_enter_cavern(&p->sh, st->a, st->b, st->c + 1, 0, 0)) { p->failed = 1; return; }
        p->step_map = g->map;
        return;
    }
    if (st->kind == P_TOWN_GOTO) {
        if (!shell_enter_town(g, st->a, st->b, 0)) { p->failed = 1; return; }
        return;
    }
    set_goal(p);
}

static int step_done(Play *p)
{
    Game *g = &p->sh.g;
    const PStep *st = cur(p);
    switch (st->kind) {
    case P_TOWN_SHOP: return p->shop_seen && !p->sh.town.shop;
    case P_TOWN_CAVE: return !p->sh.in_town;
    case P_TOWN_EDGE: return !p->sh.in_town || p->sh.tmap.index != p->town_at_start;
    case P_CAV_DOOR:  return p->sh.in_town || g->map != p->step_map;
    case P_CAV_BREAK:
    case P_CAV_CELL: {
        /* the point of walking here is the object standing on the cell: the
         * step is done when it has been collected (col high byte 0xFF, 914C) */
        if (p->item_obj >= 0 && p->item_obj < g->nobj)
            return (g->obj[p->item_obj].col >> 8) == 0xFF;
        /* nothing there: this is a plain waypoint, and the point of one is to
         * pin the hero to a *particular* cell before the next leg starts.  Two
         * cells of slack was enough to end MP30's column-58 climb one row above
         * the row-7 gallery, from which the next door is unreachable. */
        int dc = game_hero_map_col(g) - st->a, dr = game_hero_map_row(g) - st->b;
        if (dc < 0) dc = -dc;
        if (dr < 0) dr = -dr;
        return dc <= 1 && dr <= 1; }
    case P_BOSS:      return p->boss_seen && !g->post_boss_pending && !g->boss_dying;
    case P_GOTO:      return !p->sh.in_town && g->map == p->step_map;
    case P_TOWN_GOTO: return p->sh.in_town;
    case P_FARM: {
        /* walk the safe stretch back and forth, killing what wanders in, until
         * the sage has something to work with */
        int hc = game_hero_map_col(g);
        int target = p->farm_end ? st->b : st->a;
        if (hc >= target - 2 && hc <= target + 2) {
            p->farm_end = !p->farm_end;
            nav_goal_cell(&p->nav, g, p->farm_end ? st->b : st->a, game_hero_map_row(g));
        }
        return g->exp >= (unsigned)st->c; }
    default:          return 1;
    }
}

/* the common loop of play_run / play_continue */
static int play_loop(Play *p)
{
    Game *g = &p->sh.g;
    step_enter(p);
    unsigned last_deaths = g->deaths;
    while (!p->done && !p->failed) {

        shell_frame(&p->sh);
        if (g->deaths != last_deaths) {
            last_deaths = g->deaths; p->deaths++;
            snprintf(p->msg, sizeof p->msg, "step %d (%s): Garland died at %s (%d,%d) after %u frames",
                     p->step, cur(p)->what, p->sh.in_town ? p->sh.tmap.label : g->map->name,
                     p->sh.in_town ? town_hero_col(&p->sh.town) : game_hero_map_col(g),
                     p->sh.in_town ? 0 : game_hero_map_row(g), p->frames - p->step_start);
            p->failed = 1; break;
        }
        if (step_done(p)) {
            const PStep *st = cur(p);
            if (st->kind == P_BOSS) p->bosses_killed++;
            if (st->kind == P_CAV_DOOR || st->kind == P_TOWN_CAVE) p->doors_taken++;
            if (st->kind == P_TOWN_SHOP) p->shops_visited++;
            VLOG(p, "[play] step %d done in %u frames (LIFE %u/%u, EXP %u, GOLD %u, keys %u)\n",
                 p->step, p->frames - p->step_start, g->hp, g->max_hp, g->exp, (unsigned)g->gold, g->keys);
            p->step++;
            step_enter(p);
            continue;
        }
        /* the map changed under a running step (a door we did not plan for) */
        if (!p->sh.in_town && g->map != p->step_map && cur(p)->kind != P_CAV_DOOR) set_goal(p);
        if (p->frames > p->budget) {
            snprintf(p->msg, sizeof p->msg, "step %d (%s): out of frames at %s (%d,%d), dist %d",
                     p->step, cur(p)->what ? cur(p)->what : "?",
                     p->sh.in_town ? p->sh.tmap.label : g->map->name,
                     p->sh.in_town ? town_hero_col(&p->sh.town) : game_hero_map_col(g),
                     p->sh.in_town ? 0 : game_hero_map_row(g),
                     p->sh.in_town ? -1 : nav_distance(&p->nav, g));
            p->failed = 1;
        }
    }
    return p->failed ? -1 : 0;
}

int play_run(Play *p, const char *dir, const PStep *route, unsigned budget)
{
    Play save = *p;                                     /* keep the caller's setup */
    memset(p, 0, sizeof *p);
    p->verbose = save.verbose;
    p->start_level = save.start_level; p->start_sword = save.start_sword;
    p->start_shield = save.start_shield; p->start_life = save.start_life;
    p->start_gold = save.start_gold;
    p->route = route; p->budget = budget ? budget : 200000;
    p->sh.user = p; p->sh.present = present_cb; p->sh.town_present = town_present_cb;
    p->sh.quiet = 1;
    if (shell_init(&p->sh, dir, 0)) { snprintf(p->msg, sizeof p->msg, "cannot load the game files"); return -1; }
    Game *g = &p->sh.g;
    if (p->start_sword)  g->sword = (uint8_t)p->start_sword;             /* [92] */
    if (p->start_shield) {                                              /* [93]/[94]/[96] */
        g->shield = (uint8_t)p->start_shield;
        g->shield_hp = SHIELD_HP[p->start_shield - 1];
        g->page[P_SHIELD_MAX] = (uint8_t)g->shield_hp;
        g->page[P_SHIELD_MAX + 1] = (uint8_t)(g->shield_hp >> 8);
    }
    if (p->start_level)  g->level = (uint8_t)p->start_level;             /* [8D] */
    if (p->start_life)   { g->max_hp = (uint16_t)p->start_life; g->hp = g->max_hp; }
    if (p->start_gold)   g->gold = (uint32_t)p->start_gold;              /* [85..87] */
    player_page_push(g);
    /* the game starts in Felishika's Castle (cmap), as the intro leaves it */
    if (!shell_enter_town(g, 0, -1, 0)) { snprintf(p->msg, sizeof p->msg, "cannot enter the castle"); return -1; }
    /* ...and GAME.BIN A1CB places the hero from the **page** -- [80] scroll_col
     * and [83] hero_scr_col, which STDPLY.BIN sets to 30 / 10 -- not from the
     * map's own C013 column, which is the *death* return (99F4).  main.c has
     * entered towns that way since issue #38; the autopilot was still taking
     * shell_enter_town's C013 fallback (issue #40). */
    if ((g->page[0xC4] & 0x80) && (g->page[0xC4] & 0x7F) == 0)
        town_page_pull(&p->sh.town);
    p->entry_col = town_hero_col(&p->sh.town);
    p->entry_scroll = p->sh.town.scroll_col;
    return play_loop(p);
}

int play_continue(Play *p, const PStep *route, unsigned budget)
{
    p->route = route; p->step = 0; p->done = 0; p->failed = 0;
    p->frames = 0; p->budget = budget ? budget : 200000;
    p->msg[0] = 0;
    return play_loop(p);
}

/* ---------------------------------------------------------------------- */
/* The acceptance route for issue #26: from the castle through caverns 1-3   */
/* and their bosses, shopping in Muralla and Satono on the way.              */
/*                                                                           */
/* Doors are named by their own C00A cell, so every one of them is looked up  */
/* in the live map: if a patch list moves or unlocks one the route follows.   */
/* Route 1 — the opening, played end to end: the castle, the King's gift, the
 * road east into Muralla Town, the weapon shop, the cavern gate, a spell of
 * work on MP10's entrance shelf, back out through the MURALLA door, three
 * visits to the Sage and back into the cavern. */
const PStep PLAY_ROUTE_START[] = {
  {P_TOWN_SHOP, 52, 0, "c",        "cmap: the King of Felishika (1000 gold)", 0},
  {P_TOWN_EDGE,  1, 0, NULL,       "east out of the castle -> Muralla Town", 0},
  {P_TOWN_SHOP, 39, 0, "30yc",     "Muralla weapon shop: buy the Clay shield", 0},
  {P_TOWN_CAVE,205, 0, NULL,       "the Muralla cavern gate -> MP10 (61,7)", 0},
  {P_FARM,      66,104, NULL,      "MP10: work the entrance shelf for experience", 500},
  {P_CAV_DOOR,  61, 6, NULL,       "MP10: back out through the MURALLA door", 0},
  {P_TOWN_SHOP,172, 0, "1yc",      "Muralla: the Sage levels Garland up", 0},
  {P_TOWN_SHOP,172, 0, "1yc",      "Muralla: the Sage again (one level per visit)", 0},
  {P_TOWN_SHOP, 59, 0, "c",        "Muralla: the church heals him", 0},
  {P_TOWN_CAVE,205, 0, NULL,       "back into MP10", 0},
  {P_END, 0, 0, NULL, NULL, 0},
};

/* Route 2 — caverns 1, 2 and 3 with their bosses, their rewards and the shops
 * between them.
 *
 * All three boss rooms are now played the same way, and the way the game means
 * them to be played (port/README.md, "What 72F1's third poke is for"):
 *
 *   walk in through the room's own door -> kill the boss -> out through the
 *   exit door 72F1 puts on the hero's column -> *straight back in through the
 *   door beside it*, which reloads the room from disk as an ordinary room with
 *   the boss's Key standing on its floor -> pick the Key up -> out again ->
 *   open the locked door the Key was for.
 *
 * In cavern 1 that last step is MP10's (128,32) into Satono, so the route now
 * walks out of the cavern instead of asking the shell for the town.  Cavern 2's
 * is MP20's (205,47) into MP30 and cavern 3's is MP31's (188,20); both are
 * taken with the Key the boss room handed back.
 *
 * There is no `P_GOTO` left: every leg of caverns 1-3 is walked by nav.c with
 * the real physics, every door is found and entered on foot, and every locked
 * door is opened with a Key the route picked up.  The one that used to need
 * the shell -- MP20's row-36 corridor -- is opened the way the game means it
 * to be: the Key at (89,44) unlocks MP20's (95,35) into MP21, and MP21 is
 * crossed through the ring's *column* wrap (port/README.md, "How MP21
 * crosses").  `P_TOWN_GOTO` at the head of the route is not a leg at all; it
 * is the character this route starts from (see test_playthrough.c). */
const PStep PLAY_ROUTE_BOSSES[] = {
  {P_TOWN_GOTO,  2, 4,  NULL,      "Satono Town, column 4", 0},
  {P_TOWN_EDGE,  0, 0,  NULL,      "off Satono's left edge -> MP10 (128,33)", 0},
  {P_CAV_DOOR, 141,32,  NULL,      "MP10: walk to the door at (141,32) -> MP1D (47,14)", 0},
  {P_BOSS,       0, 0,  NULL,      "MP1D: Cangrejo", 0},
  {P_CAV_DOOR,  -1, 0,  NULL,      "MP1D: the exit door 72F1 put there -> MP10 (141,32)", 0},
  {P_CAV_DOOR, 141,32,  NULL,      "MP10: straight back into MP1D (47,14), an ordinary room now", 0},
  {P_CAV_CELL,  38,16,  NULL,      "MP1D: Cangrejo's Key, on the floor at last", 0},
  {P_CAV_DOOR,  47,14,  NULL,      "MP1D: out again -> MP10 (141,32)", 0},
  {P_CAV_DOOR, 128,32,  NULL,      "MP10: the Key opens (128,32) -> Satono Town", 0},
  {P_TOWN_SHOP,185, 0,  "22yc",    "Satono weapon shop: the best sword it stocks", 0},
  {P_TOWN_SHOP, 92, 0,  "1yc",     "Satono: the Sage levels him up", 0},
  {P_TOWN_EDGE,  1, 0,  NULL,      "off Satono's right edge -> MP20 (6,62)", 0},
  {P_CAV_CELL,  89,44,  NULL,      "MP20: the first of its two Keys, at (89,44)", 0},
  /* Cavern 2's own hidden half, and the way the game means you to open it.
   * MP20's row-36 corridor -- the column-157 elevator, the second Key and the
   * whole descent to the boss door -- hangs off one door, (146,35) <-> MP21
   * (66,35); the way to MP21's side of it is MP20's *locked* (95,35), which is
   * exactly what the Key at (89,44) is for, and then a lap of MP21 that goes
   * out through the ring's column wrap (port/README.md, "How MP21 crosses").
   * Route 2 has no `P_GOTO` left. */
  {P_CAV_DOOR,  95,35,  NULL,      "MP20: the first Key opens the locked (95,35) -> MP21 (14,36)", 0},
  {P_CAV_DOOR,  66,35,  NULL,      "MP21: west along row 36, down to the fix[2] platform, off the map's "
                                   "west edge round the ring to (94,56), then the east ledge to the "
                                   "(66,35) door -> MP20 (145,36), the row-36 corridor", 0},
  {P_CAV_CELL, 149,44,  NULL,      "MP20: down the column-157 elevator to the second Key at (149,44)", 0},
  {P_CAV_DOOR, 171,54,  NULL,      "MP20: back up the elevator, west over fix[7], down the column-108 "
                                   "ladder and along row 50 to the locked (171,54) -> MP2D (24,18)", 0},
  {P_BOSS,       0, 0,  NULL,      "MP2D: Pulpo", 0},
  {P_CAV_DOOR,  -1, 0,  NULL,      "MP2D: the exit door -> MP20 (190,47)", 0},
  {P_CAV_DOOR, 190,47,  NULL,      "MP20: back into MP2D, an ordinary room now", 0},
  {P_CAV_CELL,  33,20,  NULL,      "MP2D: Pulpo's Key", 0},
  {P_CAV_DOOR,  41,18,  NULL,      "MP2D: out again -> MP20 (190,47)", 0},
  {P_CAV_DOOR, 205,47,  NULL,      "MP20: the Key opens (205,47) -> MP30 (21,6)", 0},
  /* Cavern 3's gate is a *story flag*, not a door: bsmp's sentry stands at
   * Bosque's column 9 in front of the column-7 door into MP31 and will not
   * move until `page[12] & 8` -- "Garland is carrying the Hero's Crest" -- is
   * set (bsmp's own `[C015]` list pokes his record to flags 0x80, script 14,
   * "You have the Hero's Crest, I see.  You may pass").  The crest is in the
   * trunk of MP30's biggest tree, which is fight.bin's item state 0x10: it
   * only opens when the blade marks it, and what falls out is a state-0x1D
   * pickup that sets `[9C]` and, through its own `+B/+D` pair, that flag.
   * Bosque's own villagers say exactly this (scripts 5, 8 and 11). */
  /* MP30 and MP31 are one maze in two halves: nine of their doors are pairs at
   * identical coordinates and the way from one side of either map to the other
   * is through the other map.  The tree is at MP30 (166,54) and only MP30
   * (113,7) reaches it, which is four doors away from where MP20 lets you in. */
  {P_CAV_DOOR,  19,49,  NULL,      "MP30: the (19,49) door -> MP31 (18,50)", 0},
  {P_CAV_DOOR,  47,14,  NULL,      "MP31: the (47,14) door -> MP30 (46,15)", 0},
  /* the column-58 ladder, rows 4-16.  Name cells the survey really has a node
   * at: (58,8) is the ladder tile itself, not a place the hero can be, and a
   * waypoint two cells wide let him "arrive" hanging one rung short with the
   * gallery already out of reach. */
  {P_CAV_CELL,  57,11,  NULL,      "MP30: onto the column-58 ladder", 0},
  {P_CAV_CELL,  57, 5,  NULL,      "MP30: up it to the top rung", 0},
  {P_CAV_CELL,  63, 7,  NULL,      "MP30: east off the ladder onto the row-7 gallery", 0},
  {P_CAV_CELL,  70, 7,  NULL,      "MP30: east along the gallery", 0},
  {P_CAV_DOOR,  88, 6,  NULL,      "MP30: the (88,6) door -> MP31 (87,7)", 0},
  {P_CAV_DOOR, 114, 6,  NULL,      "MP31: the (114,6) door -> MP30 (113,7)", 0},
  {P_CAV_BREAK,166,54,  NULL,      "MP30: break the biggest tree and take the Hero's Crest", 0},
  {P_CAV_DOOR, 153,43,  NULL,      "MP30: the (153,43) door -> MP31 (152,44)", 0},
  /* cavern 3's own Key, and the last one the accounting needs: seven Keys lie
   * in caverns 1-3 (four lying in the maps, one handed back by each of the three
   * boss rooms on the second visit) and there are exactly seven locked doors --
   * MP30's is what opens MP31's boss door at (188,20).  It lies on MP30's
   * row-54 shelf, which the crest ledge drops into one way only, so the way to
   * it is back through the *paired* (153,43) door from the MP31 side. */
  {P_CAV_DOOR, 153,43,  NULL,      "MP31: straight back through the paired door -> MP30 (152,44)", 0},
  {P_CAV_CELL, 133,55,  NULL,      "MP30: the Key at (133,55)", 0},
  /* back up the column-131 ladder and along the row-42 shelf.  Waypoints,
   * because the shelf is one of MP30's burning ones and the field's own next
   * hop out of (131,44) is eight columns at once: the hero was left jumping at
   * it and taking hazard damage until he died. */
  {P_CAV_CELL, 130,44,  NULL,      "MP30: back to the foot of the column-131 ladder", 0},
  {P_CAV_CELL, 132,43,  NULL,      "MP30: up onto the row-42 shelf", 0},
  {P_CAV_CELL, 140,42,  NULL,      "MP30: east along it", 0},
  {P_CAV_DOOR, 153,43,  NULL,      "MP30: out again -> MP31 (152,44)", 0},
  {P_CAV_DOOR, 186,46,  NULL,      "MP31: the (186,46) door -> MP30 (185,47)", 0},
  {P_CAV_CELL,   6,47,  NULL,      "MP30: east round the ring to the column-6 elevator", 0},
  {P_CAV_CELL,   4,20,  NULL,      "MP30: up the elevator and the column-5 ladder", 0},
  {P_CAV_CELL, 200,21,  NULL,      "MP30: west round the ring again", 0},
  {P_CAV_DOOR, 185,18,  NULL,      "MP30: the BOSQUE door at (185,18)", 0},
  {P_TOWN_SHOP, 81, 0,  "30yc",    "Bosque weapon shop: a shield", 0},
  {P_TOWN_CAVE,  7, 0,  NULL,      "Bosque: west past the sentry, through his own door -> MP31 (149,14)", 0},
  {P_CAV_DOOR, 188,20,  NULL,      "MP31: the Key opens the boss door (188,20) -> MP3D (17,21)", 0},
  {P_BOSS,       0, 0,  NULL,      "MP3D: Pollo", 0},
  {P_CAV_DOOR,  -1, 0,  NULL,      "MP3D: the exit door -> MP31 (174,4)", 0},
  {P_CAV_DOOR, 174, 4,  NULL,      "MP31: back into MP3D, an ordinary room now", 0},
  {P_CAV_CELL,  33,20,  NULL,      "MP3D: Pollo's Key", 0},
  {P_CAV_DOOR,  52,21,  NULL,      "MP3D: out again -> MP31 (174,4)", 0},
  {P_END, 0, 0, NULL, NULL, 0},
};

