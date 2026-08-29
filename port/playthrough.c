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
    case P_CAV_CELL:
        nav_goal_cell(&p->nav, g, st->a, st->b);
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
        if (!shell_enter_cavern(&p->sh, st->a, st->b, st->c + 1, 0)) { p->failed = 1; return; }
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
    case P_CAV_CELL: {
        /* the point of walking here is the object standing on the cell: the
         * step is done when it has been collected (col high byte 0xFF, 914C) */
        if (p->item_obj >= 0 && p->item_obj < g->nobj)
            return (g->obj[p->item_obj].col >> 8) == 0xFF;
        int dc = game_hero_map_col(g) - st->a, dr = game_hero_map_row(g) - st->b;
        if (dc < 0) dc = -dc;
        if (dr < 0) dr = -dr;
        return dc <= 2 && dr <= 2; }
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

/* Route 2 — caverns 1, 2 and 3 with their bosses and the shops between them.
 * Each boss room is *entered* with the shell call its door makes (P_GOTO with
 * the door's own destination cell); from there everything — the encounter
 * card, the fight, the reward, the post-boss exit door, the locked door into
 * Satono and the shops — is played by the autopilot. */
const PStep PLAY_ROUTE_BOSSES[] = {
  {P_GOTO,       1,47,  NULL,      "MP10 door (141,32) -> MP1D (47,14)", 14},
  {P_BOSS,       0, 0,  NULL,      "MP1D: Cangrejo", 0},
  {P_CAV_CELL,  38,16,  NULL,      "MP1D: the boss reward (a Key) at (38,16)", 0},
  {P_CAV_DOOR,  -1, 0,  NULL,      "MP1D: the exit door 72F1 put there -> MP10", 0},
  {P_TOWN_GOTO,  2, 4,  NULL,      "MP10 door (128,32) -> Satono Town, column 4", 0},
  {P_TOWN_SHOP,185, 0,  "22yc",    "Satono weapon shop: the Spirit sword", 0},
  {P_TOWN_SHOP, 92, 0,  "1yc",     "Satono: the Sage levels him up", 0},
  {P_GOTO,       4,41,  NULL,      "MP20 door (190,47) -> MP2D (41,18)", 18},
  {P_BOSS,       0, 0,  NULL,      "MP2D: Pulpo", 0},
  {P_CAV_CELL,  33,20,  NULL,      "MP2D: the boss reward at (33,20)", 0},
  {P_CAV_DOOR,  -1, 0,  NULL,      "MP2D: the exit door -> MP20", 0},
  {P_TOWN_GOTO,  3,60,  NULL,      "Bosque village (MP30's town door at (185,18))", 0},
  {P_TOWN_SHOP, 81, 0,  "30yc",    "Bosque weapon shop: a shield", 0},
  {P_GOTO,       7,52,  NULL,      "MP31 door (174,4) -> MP3D (52,21)", 21},
  {P_BOSS,       0, 0,  NULL,      "MP3D: Pollo", 0},
  {P_CAV_CELL,  33,20,  NULL,      "MP3D: the boss reward at (33,20)", 0},
  {P_CAV_DOOR,  -1, 0,  NULL,      "MP3D: the exit door -> MP31", 0},
  {P_END, 0, 0, NULL, NULL, 0},
};

