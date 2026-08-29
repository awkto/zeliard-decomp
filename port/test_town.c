/* test_town.c — the town engine (docs/TOWN.md, src/town.c): map parsing, the
 * walk/collision model, doors, edge exits, NPC behaviours and the cavern
 * hand-off both ways. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "town.h"
#include "enemy.h"

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; fprintf(stderr, "  FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

static TownMap   M;
static TownTiles TT;
static TownSprites TS;
static TownHero  TH;
static Town      T;
static Game      G;
static void present(Town *t) { (void)t; }

/* the yes/no widget answers: 0 = pick row 0 (Yes / Take), 1 = row 1, 2 = Alt */
static int menu_answer, menu_seen;
static void present_menu(Town *t)
{
    t->dirs = 0; t->btn1_edge = t->btn2_edge = 0;
    if (t->dlg.menu_n) {
        menu_seen++;
        if (menu_answer == 2) { t->btn2_edge = 0xFF; return; }
        if (t->dlg.menu_row < menu_answer) { t->dirs = DIR_DOWN; return; }
        if (t->dlg.menu_row > menu_answer) { t->dirs = DIR_UP; return; }
        t->btn1_edge = 0xFF;
        return;
    }
    t->btn1_edge = 0xFF;                        /* page the text */
}

static int load(const char *dir, int idx)
{
    if (town_load_map(&M, dir, idx)) return -1;
    if (town_load_tiles(&TT, dir, M.tileset)) return -1;
    if (town_load_sprites(&TS, dir, M.gfx)) return -1;
    town_load_hero(&TH, dir);
    town_init(&T, &M, &TT, &TS, &TH, &G);
    T.present = present;
    return 0;
}
static void step(uint8_t dirs, uint8_t btn)
{
    T.dirs = dirs;
    if ((btn & 1) && !(T.buttons & 1)) T.btn1_edge = 0xFF;
    T.buttons = btn;
    T.action = 0;
    town_step(&T);
}

/* ------------------------------------------------------- the map header */
/* docs/TOWN.md §3 and the per-map table in the same section. */
static void t_map(const char *dir)
{
    CHECK(load(dir, 1) == 0, "mrmp.mdt loads");
    if (!M.raw) return;
    CHECK(M.width == 215, "Muralla is 215 columns, got %d", M.width);
    CHECK(!strcmp(M.label, "Muralla Town"), "label \"%s\"", M.label);
    CHECK(M.town_id == 1, "town id %d", M.town_id);
    CHECK(M.tileset == 1 && M.gfx == 0 && M.town_flags == 0, "mpat / mman / surface town (ts %d gfx %d flags %02x)",
          M.tileset, M.gfx, M.town_flags);
    /* doors: 39 armour 59 church 111 drug 138 bank 172 sage 205 cave0 */
    static const struct { int col, dest; } D[6] = {{39,3},{59,5},{111,4},{138,6},{172,2},{205,8}};
    CHECK(M.ndoors == 6, "6 doors, got %d", M.ndoors);
    for (int i = 0; i < 6 && i < M.ndoors; i++)
        CHECK(M.doors[i].col == D[i].col && M.doors[i].dest == D[i].dest,
              "door %d: %d:%d", i, M.doors[i].col, M.doors[i].dest);
    /* the single cave record is the MP10 entry the DOSBOX recipe uses */
    CHECK(M.ncaves == 1 && M.caves[0].col == 61 && M.caves[0].row == 7 && M.caves[0].map == 0,
          "cave 0 = (61,7,MP10), got (%d,%d,map %d)", M.caves[0].col, M.caves[0].row, M.caves[0].map);
    CHECK(M.nexits >= 1 && (M.exits[0].flags & 1) && M.exits[0].dest == 0, "left exit -> cmap");
    CHECK(M.nnpcs == 9, "9 NPCs, got %d", M.nnpcs);
    CHECK(M.range_min == 5 && M.range_max == 150, "walker range %d..%d", M.range_min, M.range_max);
    CHECK(M.ndlg >= 9, "%d dialogue scripts", M.ndlg);
    CHECK(strstr(town_dialogue(&M, 1), "demon") != NULL, "script 1: \"%.40s\"", town_dialogue(&M, 1));
    /* the tile bank: mpat blocks cells 0x96/0x97 (docs/TOWN.md §4.1) */
    CHECK(TT.nblock == 2 && TT.block[0] == 0x96 && TT.block[1] == 0x97,
          "mpat block list: %d entries %02x %02x", TT.nblock, TT.block[0], TT.block[1]);
    CHECK(!town_cell_walkable(&T, 0x96) && town_cell_walkable(&T, 1), "block list stops the hero");
    CHECK(TH.ncells == 46, "tman.grp has 46 hero cells, got %d", TH.ncells);
}

/* ------------------------------------------------------------- walking */
/* 6781 / 67F4: one column per frame; the hero moves inside screen columns
 * 0x0B..0x10 and scrolls the map outside them. */
static void t_walk(const char *dir)
{
    if (load(dir, 1)) return;
    town_place(&T, 100, 0);
    CHECK(town_hero_col(&T) == 100, "placed on column %d", town_hero_col(&T));
    CHECK(T.scroll_col == 83 && T.hero_scr_col == 13, "7DE1: scroll %d hero_scr %d", T.scroll_col, T.hero_scr_col);
    int sc0 = T.scroll_col;
    step(DIR_RIGHT, 0);
    CHECK(town_hero_col(&T) == 101, "one column right per frame: %d", town_hero_col(&T));
    CHECK(T.hero_scr_col == 14 && T.scroll_col == sc0, "the hero moves inside the free zone (scr %d)", T.hero_scr_col);
    step(DIR_RIGHT, 0); step(DIR_RIGHT, 0);
    CHECK(T.hero_scr_col == 16 && T.scroll_col == sc0, "still inside the free zone at 0x10");
    step(DIR_RIGHT, 0);
    CHECK(T.hero_scr_col == 16 && T.scroll_col == sc0 + 1, "past 0x10 the map scrolls (scr %d scroll %d)",
          T.hero_scr_col, T.scroll_col);
    CHECK(!(T.hero_flags & 1), "facing right");
    step(DIR_LEFT, 0);
    CHECK(T.hero_flags & 1, "facing left after one left step");
    /* 67BF: `hero_scr_col >= 0x0B` moves the hero, so he settles on 0x0A and
     * the map scrolls from there on */
    for (int i = 0; i < 6; i++) step(DIR_LEFT, 0);
    CHECK(T.hero_scr_col == 0x0A, "left walk settles on screen column 0x0A (%d)", T.hero_scr_col);
    int sc1 = T.scroll_col;
    step(DIR_LEFT, 0);
    CHECK(T.hero_scr_col == 0x0A && T.scroll_col == sc1 - 1, "then the map scrolls (scr %d scroll %d)",
          T.hero_scr_col, T.scroll_col);
    /* hero_anim cycles 0..3 (6824) */
    uint8_t a0 = T.hero_anim;
    step(DIR_LEFT, 0);
    CHECK(T.hero_anim == ((a0 + 1) & 3), "walk frame %u -> %u", a0, T.hero_anim);

    /* 686E: a blocked ground cell stops the hero.  Fake one in front of him. */
    int col = town_hero_col(&T);
    M.grid[col + 2][TOWN_GROUND] = TT.block[0];
    int before = town_hero_col(&T);
    step(DIR_RIGHT, 0); step(DIR_RIGHT, 0);
    CHECK(town_hero_col(&T) == before, "a blocked ground cell at col+2 stops the hero (%d -> %d)",
          before, town_hero_col(&T));
    M.grid[col + 2][TOWN_GROUND] = 0;
}

/* --------------------------------------------------------------- doors */
static void t_doors(const char *dir)
{
    if (load(dir, 1)) return;
    /* 6E29: the door test is "hero column within col +- 1" */
    for (int d = -2; d <= 2; d++) {
        town_place(&T, 205 + d, 0);
        step(DIR_UP, 0);
        int want = (d >= -1 && d <= 1);
        CHECK((T.action == TOWN_TO_CAVERN) == want, "door at 205, hero at %d: action %d (want %s)",
              205 + d, T.action, want ? "cavern" : "none");
        if (want) CHECK(T.action_arg == 0, "cave index 0, got %d", T.action_arg);
    }
    town_place(&T, 172, 0);
    step(DIR_UP, 0);
    CHECK(T.action == TOWN_SHOP && T.action_arg == 2, "the sage's door at 172 -> shop 2 (got %d/%d)",
          T.action, T.action_arg);
    CHECK(T.hero_anim == 4, "6E5B: the hero turns his back (anim %u)", T.hero_anim);
}

/* ---------------------------------------------------------- edge exits */
static void t_edges(const char *dir)
{
    if (load(dir, 1)) return;
    /* 6CB5: hero_scr_col 0xFF (left) picks the exit record with bit0 set */
    town_place(&T, 4, 0);
    T.scroll_col = 0; T.hero_scr_col = 0;
    step(DIR_LEFT, 0);
    CHECK(T.hero_scr_col < 0, "walking left at column 0 leaves the map (scr %d)", T.hero_scr_col);
    step(0, 0);
    CHECK(T.action == TOWN_TO_TOWN && (T.action_arg & 0xFF) == 0, "left edge -> town 0 (cmap), got %d/%d",
          T.action, T.action_arg & 0xFF);
    /* Muralla has no right-edge record, so the hero is pushed back */
    if (load(dir, 1)) return;
    T.scroll_col = M.width - 0x24; T.hero_scr_col = 0x1B;
    town_npc_markers_reset(&T);
    step(DIR_RIGHT, 0);
    step(0, 0);
    CHECK(T.action == 0 && T.hero_scr_col <= 0x1B, "no right-edge exit: the hero stays (scr %d)", T.hero_scr_col);
}

/* ----------------------------------------------------------- NPCs / talk */
static void t_npcs(const char *dir)
{
    if (load(dir, 1)) return;
    town_place(&T, 100, 0);
    /* 6C2B: every NPC's column carries the 0xFD marker on row 5 */
    for (int i = 0; i < M.nnpcs; i++)
        CHECK(M.grid[M.npcs[i].col][TOWN_NPC_ROW] == TOWN_MARK, "NPC %d marker at column %d", i, M.npcs[i].col);
    /* 6B6C: a type-1 walker moves one column every 2nd frame and turns at the
     * range limits; NPC 2 (column 21) is one. */
    TownNpc *w = NULL;
    for (int i = 0; i < M.nnpcs; i++) if (M.npcs[i].type == 1) { w = &M.npcs[i]; break; }
    CHECK(w != NULL, "Muralla has a type-1 walker");
    if (w) {
        uint16_t c0 = w->col;
        int moved = 0;
        for (int i = 0; i < 8; i++) { uint16_t p = w->col; town_npc_update(&T); if (w->col != p) moved++; }
        CHECK(moved == 4, "a type-1 walker steps every 2nd frame: %d steps in 8 frames", moved);
        CHECK(w->col != c0, "the walker moved");
    }
    /* 6BB7: a type-3 NPC always faces the hero */
    TownNpc *f = NULL;
    for (int i = 0; i < M.nnpcs; i++) if (M.npcs[i].type == 3) { f = &M.npcs[i]; break; }
    if (f) {
        town_place(&T, f->col + 10, 0);
        town_npc_update(&T);
        CHECK(!(f->sprite & 0x80), "the NPC faces right when the hero is to its right");
        town_place(&T, f->col - 10, 0);
        town_npc_update(&T);
        CHECK(f->sprite & 0x80, "the NPC faces left when the hero is to its left");
    }
    /* 623F: Space talks to an NPC 1..3 columns ahead */
    if (load(dir, 1)) return;
    TownNpc *n = &M.npcs[0];
    town_place(&T, (int)n->col - 2, 0);
    T.message[0] = 0;
    step(0, 1);
    CHECK(T.message[0] != 0, "Space starts the NPC's dialogue: \"%.40s\"", T.message);
    CHECK(!strcmp(T.message, town_dialogue(&M, n->script)), "the text is the NPC's own script %d", n->script);
    /* out of range: nothing */
    if (load(dir, 1)) return;
    town_place(&T, (int)M.npcs[0].col - 6, 0);
    T.message[0] = 0;
    step(0, 1);
    CHECK(T.message[0] == 0, "no dialogue 6 columns away");
}

/* -------------------------------------------------- the cavern hand-off */
/* docs/TOWN.md §9: goto_cavern 6FF8 puts the hero at scroll_col = col - 16,
 * scroll_row = (row - 10) & 63, which is exactly fight.bin's entry model. */
static void t_handoff(const char *dir)
{
    static Map cm; static Tileset ct;
    if (load(dir, 1)) return;
    if (map_load_system(&cm, dir, M.caves[0].map)) return;
    if (gfx_load_tileset(&ct, dir, cm.tileset)) return;
    game_init(&G, &cm, &ct);
    game_place(&G, M.caves[0].col, M.caves[0].row, M.caves[0].side & 1);
    CHECK(game_hero_map_col(&G) == 61 && game_hero_map_row(&G) == 7,
          "town cave 0 -> MP10 (61,7), got (%d,%d)", game_hero_map_col(&G), game_hero_map_row(&G));
    CHECK(G.scroll_col == 45 && G.scroll_row == 61, "6FF8: scroll (%d,%d), the DOSBox capture's view",
          G.scroll_col, G.scroll_row);
    /* the return door: MP10's C00A record (61,6) has dest_row 0xFF = a town */
    const Door *d = NULL;
    for (int i = 0; i < cm.ndoors; i++) if (cm.doors[i].col == 61 && cm.doors[i].row == 6) d = &cm.doors[i];
    CHECK(d && d->dest_row == 0xFF && d->dest_map == 1 && d->dest_col == 205,
          "MP10's town door -> town 1 column 205");
    /* 7DE1: coming back puts the hero on column 205 of a 215-column map */
    town_place(&T, 205, 0);
    CHECK(town_hero_col(&T) == 205, "back in Muralla on column %d", town_hero_col(&T));
    CHECK(T.scroll_col == 188 - 1 || T.scroll_col == M.width - 0x24 || T.scroll_col == 188,
          "scroll clamped to %d (max %d)", T.scroll_col, M.width - 0x24);
    /* 99AD: dying costs all gold and half the almas, and sends the hero to [C5] */
    game_init(&G, &cm, &ct);
    G.gold = 1234; G.almas = 500; G.level = 3; G.hp = 0;
    G.town_map = 0x81;
    hero_die(&G);
    CHECK(G.gold == 0, "death clears GOLD, got %u", (unsigned)G.gold);
    CHECK(G.almas == 250, "death halves ALMAS, got %u", G.almas);
    CHECK(G.exp == 127 - 2 * 3, "death awards 127 - 2*level = %u exp, got %u", 127 - 6, G.exp);
    CHECK(G.hp == G.max_hp, "hp restored to %u", G.max_hp);
    CHECK(G.cur_map == 0x81, "cur_map = town_map (%02x)", G.cur_map);
}

/* the two dialogue opcodes that open the 74D3 widget (docs/TOWN.md Â§6) */
static void t_dialogue_menu(const char *dir)
{
    /* 0x81: bsmp's sentry.  Yes -> script 12, No -> script 13 (6655/6676). */
    if (load(dir, 3)) { fprintf(stderr, "  (no bsmp)\n"); return; }
    T.present = present_menu;
    memset(&G, 0, sizeof G); G.max_hp = G.hp = 0x50;
    menu_answer = 1; menu_seen = 0;
    town_dialogue_run(&T, 11, 0);
    CHECK(menu_seen > 0, "opcode 0x81 opened the yes/no widget");
    CHECK(!T.dlg.menu_n, "the widget came down again");
    char no_text[512]; snprintf(no_text, sizeof no_text, "%s", T.message);
    menu_answer = 0; menu_seen = 0;
    town_dialogue_run(&T, 11, 0);
    CHECK(menu_seen > 0, "opcode 0x81 asked again");
    CHECK(strcmp(no_text, T.message) != 0, "Yes and No run different scripts (12 / 13)");

    /* 0x89: llmp's Asbestos cape, 2500 almas (66AD..6706). */
    if (load(dir, 7)) { fprintf(stderr, "  (no llmp)\n"); return; }
    T.present = present_menu;
    memset(&G, 0, sizeof G); G.max_hp = G.hp = 0x50;
    G.almas = 100;
    menu_answer = 0; menu_seen = 0;
    town_dialogue_run(&T, 3, 0);
    CHECK(menu_seen > 0, "opcode 0x89 opened the Take / No Take widget");
    CHECK(G.almas == 100, "100 almas is not enough: nothing is spent");
    CHECK(!(G.page[0x34] & 0x40), "and the cape flag stays clear");
    G.almas = 3000;
    menu_answer = 0;
    town_dialogue_run(&T, 3, 0);
    CHECK(G.almas == 500, "2500 almas paid, %u left", G.almas);
    CHECK(G.page[0x34] & 0x40, "[34] bit6 = the Asbestos cape was bought");
    CHECK(G.page[0xA1] == 5, "item 5 (the cape) went into [A1], got %u", G.page[0xA1]);
    G.almas = 3000; G.page[0x34] = 0; G.page[0xA1] = 0;
    menu_answer = 1;
    town_dialogue_run(&T, 3, 0);
    CHECK(G.almas == 3000, "declining costs nothing");
    CHECK(!(G.page[0x34] & 0x40), "and gives no cape");
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../zeliard";
    memset(&G, 0, sizeof G);
    G.max_hp = G.hp = 0x50;
    if (town_load_map(&M, dir, 1)) {
        fprintf(stderr, "  (town maps not available in %s: skipping)\n", dir);
        return 0;
    }
    struct { const char *name; void (*fn)(const char *); } tests[] = {
        {"town map", t_map}, {"walk", t_walk}, {"doors", t_doors},
        {"edges", t_edges}, {"npcs", t_npcs}, {"hand-off", t_handoff},
        {"dialogue menu", t_dialogue_menu},
    };
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; i++) {
        int before = fails;
        tests[i].fn(dir);
        fprintf(stderr, "%-14s %s\n", tests[i].name, fails == before ? "ok" : "FAILED");
    }
    fprintf(stderr, "%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
