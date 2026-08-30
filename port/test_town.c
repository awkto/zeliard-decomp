/* test_town.c — the town engine (docs/TOWN.md, src/town.c): map parsing, the
 * walk/collision model, doors, edge exits, NPC behaviours and the cavern
 * hand-off both ways. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "town.h"
#include "player.h"
#include "enemy.h"
#include "shell.h"
#include "render.h"

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

/* ------------------------------------------------------- the sentry gate */
/* bsmp's own `[C015]` list is one record: `if page[12] & 08: [CCFA]=80
 * [CCFB]=0E`, the `flags` and `script` bytes of NPC 0 -- the sentry standing at
 * Bosque's column 9, in front of the column-7 door into MP31.  Fresh he is
 * flags 0xC0 (6890: bit 6 makes him solid, and 6288's `flags & 0xC0` refuses to
 * talk) on script 11, "Hold on there!  Do you have the Hero's Crest?", whose
 * `0x81` opcode runs script 12 for Yes ("Don't lie") and 13 for No ("You cannot
 * pass here without the Hero's Crest").  Carry the crest and he becomes flags
 * 0x80, script 14, "You have the Hero's Crest, I see.  You may pass" -- and,
 * bit 6 gone, he stops blocking the way.  The flag is set by taking the item
 * MP30's tree at (166,54) breaks open; see test_playthrough. */
static void t_sentry(const char *dir)
{
    memset(G.page, 0, sizeof G.page);
    if (load(dir, 3)) return;
    CHECK(M.nnpcs > 0 && M.npcs[0].col == 9, "bsmp's NPC 0 stands at column %d",
          M.nnpcs ? M.npcs[0].col : -1);
    if (!M.nnpcs) return;
    CHECK(M.npcs[0].flags == 0xC0 && M.npcs[0].script == 0x0B,
          "and is solid, on script %d (flags %02X)", M.npcs[0].script, M.npcs[0].flags);
    CHECK(M.ndoors > 0 && M.doors[0].col == 7 && M.doors[0].dest == 9,
          "the door he guards is column 7, cave record %d", M.ndoors ? M.doors[0].dest - 8 : -1);
    CHECK(M.ncaves > 1 && M.caves[1].map == 6 && M.caves[1].col == 149,
          "which is MP31 at (%d,%d)", M.ncaves > 1 ? M.caves[1].col : -1,
          M.ncaves > 1 ? M.caves[1].row : -1);
    /* he blocks the way west: walking into column 9 stops at column 10 */
    town_place(&T, 14, 0);
    for (int i = 0; i < 40; i++) step(DIR_LEFT, 0);
    CHECK(town_hero_col(&T) > (int)M.npcs[0].col,
          "he blocks the way west (the hero stopped at column %d)", town_hero_col(&T));
    /* now with the crest */
    memset(G.page, 0, sizeof G.page);
    G.page[0x12] = 0x08;
    if (load(dir, 3)) return;
    town_apply_patches(&M, G.page);
    CHECK(M.npcs[0].flags == 0x80 && M.npcs[0].script == 0x0E,
          "with page[12] & 8 he is flags %02X on script %d", M.npcs[0].flags, M.npcs[0].script);
    CHECK(!strncmp(town_dialogue(&M, M.npcs[0].script), "Hold on there! You have the Hero", 31),
          "which is \"%.40s\"", town_dialogue(&M, M.npcs[0].script));
    town_place(&T, 14, 0);
    for (int i = 0; i < 40; i++) step(DIR_LEFT, 0);
    CHECK(town_hero_col(&T) < (int)M.npcs[0].col,
          "and the hero walks straight past him to column %d", town_hero_col(&T));
    memset(G.page, 0, sizeof G.page);
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

/* The NAME.USR save file (kenjpro A862 / town.bin 7592).  The file is a raw
 * 256-byte image of BASE:0000, so the hero's town position ([80] scroll_col,
 * [83] hero_scr_col, [C2] facing, [C4] cur_map) travels in it with the player
 * record — that is what makes a port-written .usr loadable by the real game's
 * F7 "Restore Game". */
static void t_save(const char *dir)
{
    if (load(dir, 2)) { fprintf(stderr, "  (no stmp)\n"); return; }
    memset(&G, 0, sizeof G);
    G.user = NULL;
    CHECK(player_load_stdply(&G, dir) == 0, "STDPLY.BIN loaded");
    town_place(&T, 210, 1);
    G.gold = 20000; G.almas = 7; G.level = 12; G.max_hp = G.hp = 600;
    G.sword = 3; G.shield = 4; G.shield_hp = 300; G.keys = 2;
    town_page_push(&T);
    CHECK(G.page[0x80] == (uint8_t)T.scroll_col, "[80] = scroll_col %d", T.scroll_col);
    CHECK(G.page[0x83] == (uint8_t)T.hero_scr_col, "[83] = hero_scr_col %d", T.hero_scr_col);
    CHECK(G.page[0xC2] == 1, "[C2] bit0 = facing left");
    CHECK(G.page[0xC4] == 0x82, "[C4] = 0x80|2 (Satono), got %02X", G.page[0xC4]);
    CHECK(town_hero_col(&T) == 210, "the hero is on column 210, got %d", town_hero_col(&T));

    const char *tmp = "/tmp";
    CHECK(player_save_usr(&G, tmp, "ZELTEST") == 0, "kenjpro A862 wrote ZELTEST.usr");
    char path[256]; snprintf(path, sizeof path, "%s/ZELTEST.usr", tmp);
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL, "the file exists");
    if (f) {
        uint8_t buf[300];
        size_t n = fread(buf, 1, sizeof buf, f);
        fclose(f);
        CHECK(n == 256, "it is exactly 256 bytes with no header, got %zu", n);
        CHECK(memcmp(buf, G.page, 256) == 0, "and byte-identical to the page");
        /* the three fields the real game's restore reads first */
        CHECK(buf[0xC4] == 0x82, "file [C4] = 82");
        CHECK(buf[0x8D] == 12, "file [8D] = level 12");
        CHECK(page_gold24(buf, 0x85) == 20000, "file [85..87] = 20000 gold");
    }

    /* town.bin 7592: load the file back over BASE:0000 and resume there */
    Game H; memset(&H, 0, sizeof H);
    CHECK(player_load_usr(&H, tmp, "ZELTEST") == 0, "restore_game 7592 read it back");
    CHECK(H.gold == 20000 && H.level == 12 && H.max_hp == 600, "the record survived");
    CHECK(H.sword == 3 && H.shield == 4 && H.shield_hp == 300, "so did the equipment");
    CHECK(H.keys == 2 && H.almas == 7, "and the keys / almas");
    CHECK(H.cur_map == 0x82, "cur_map picks the town to reload");
    Town T2; Game *save_g = T.g;
    T2 = T; T2.g = &H;
    town_page_pull(&T2);
    CHECK(T2.scroll_col == T.scroll_col, "scroll_col came back: %d", T2.scroll_col);
    CHECK(T2.hero_scr_col == T.hero_scr_col, "hero_scr_col came back: %d", T2.hero_scr_col);
    CHECK(T2.hero_flags == 1, "facing came back");
    CHECK(town_hero_col(&T2) == 210, "the hero resumes on column 210, got %d", town_hero_col(&T2));
    T.g = save_g;
    remove(path);
}

/* The two towns the DOSBox ground-truth captures were taken from: the edge
 * exits that reach cavern 2 and the door that reaches cavern 3 (docs/TOWN.md
 * §3 C007/C009/C00B).  These are the routes port/README.md documents. */
static void t_capture_routes(const char *dir)
{
    if (load(dir, 2)) { fprintf(stderr, "  (no stmp)\n"); return; }
    CHECK(M.width == 215, "Satono Town is 215 columns, got %d", M.width);
    int nleft = 0, nright = 0;
    for (int i = 0; i < M.nexits; i++) {
        const TownExit *e = &M.exits[i];
        if (!(e->flags & 0x80)) continue;                       /* not a cavern entry */
        const TownCave *c = &M.caves[e->dest];
        if (e->flags & 1) { nleft++;
            CHECK(c->map == 0 && c->col == 128 && c->row == 33,
                  "Satono's left edge -> MP10 (128,33), got map %d (%d,%d)", c->map, c->col, c->row); }
        else { nright++;
            CHECK(c->map == 2 && c->col == 6 && c->row == 62,
                  "Satono's right edge -> MP20 (6,62), got map %d (%d,%d)", c->map, c->col, c->row); }
    }
    CHECK(nleft == 1 && nright == 1, "both Satono edges are cavern entries");

    if (load(dir, 3)) { fprintf(stderr, "  (no bsmp)\n"); return; }
    const TownDoor *d = NULL;
    for (int i = 0; i < M.ndoors; i++) if (M.doors[i].col == 142) d = &M.doors[i];
    CHECK(d != NULL, "Bosque has a door at column 142");
    if (d) {
        CHECK(d->dest >= 8, "it is a cavern gate (dest %d)", d->dest);
        const TownCave *c = &M.caves[d->dest - 8];
        CHECK(c->map == 5 && c->col == 185 && c->row == 19,
              "Bosque column 142 -> MP30 (185,19), got map %d (%d,%d)", c->map, c->col, c->row);
    }
}

/* ------------------------------------------------- the edge-exit hand-off */
/* #38: walking off Felishika's Castle's right edge into Muralla is town.bin
 * 6CF5 -> change_town_map (6D30) -> the re-entry at 60B7, which does *not*
 * repaint the backdrop -- the ympd panorama and the near strips stay on screen
 * exactly as GT_CAPTURE_BACKDROP left them.  The port has to re-point the Town
 * at the shell's TownBackdrop after town_init() memsets it, or rows 14..101
 * come out as flat sky and the mountains vanish.  Everything below is driven
 * through the real shell, so it is the code path the game takes.            */
static uint8_t EFB[FB_W * FB_H];
static uint8_t edge_dirs;
static void edge_present(Town *t) { t->dirs = edge_dirs; t->btn1_edge = t->btn2_edge = 0; }
static void edge_game_present(Game *g) { (void)g; }

static void t_edge_backdrop(const char *dir)
{
    static Shell s;
    memset(&s, 0, sizeof s);
    s.quiet = 1;
    if (shell_init(&s, dir, 0)) { fprintf(stderr, "  (no resources)\n"); return; }
    s.present = edge_game_present; s.town_present = edge_present;
    if (!shell_enter_town(&s.g, 0, -1, 0)) { CHECK(0, "shell_enter_town(cmap)"); return; }
    CHECK(s.town.back == &s.tback && s.tback.loaded, "entering a town points it at the ympd panorama");
    /* GAME.BIN A1CB -> town.bin 601E: the boot reads the hero's town position
     * out of the page, which on a new game is STDPLY.BIN's [80]/[83] */
    town_page_pull(&s.town);
    CHECK(s.town.scroll_col == 30 && s.town.hero_scr_col == 10,
          "the castle boots at STDPLY's scroll 30 / screen column 10, got %d / %d",
          s.town.scroll_col, s.town.hero_scr_col);
    unsigned tr = s.transitions;
    edge_dirs = DIR_RIGHT;
    int steps = 0;
    while (s.transitions == tr && steps < 4000) { shell_frame(&s); steps++; }
    CHECK(s.transitions > tr, "walking right leaves cmap for the next town (%d frames)", steps);
    CHECK(s.tmap.width == 215 && s.town.map == &s.tmap, "and lands in Muralla (215 columns), got %d", s.tmap.width);
    /* 6D22: off the right edge -> the left end of the new map, phase 0 */
    CHECK(s.town.scroll_col == 0 && s.town.hero_scr_col == 0,
          "at its left end (scroll %d, scr %d)", s.town.scroll_col, s.town.hero_scr_col);
    /* 60B7 does not repaint: the strip phase carries over from cmap, where the
     * hero scrolled (114 - 0x24) - 30 = 48 columns since the painter ran */
    CHECK(s.town.back_steps == 48, "carrying cmap's 48 columns of parallax phase (%d)", s.town.back_steps);
    /* the bug: town_init() memsets ->back and the hand-off never restored it */
    CHECK(s.town.back == &s.tback, "the new town still has the backdrop (#38)");
    town_render(EFB, &s.town);
    int flat = 0, nz = 0;
    for (int y = 14; y < 78; y++)
        for (int x = 48; x < 272; x++) {
            uint8_t v = EFB[y * FB_W + x];
            if (v == 0x2C) flat++;
            if (v && v != 0x2C) nz++;   /* the flat fallback is 0x2C everywhere */
        }
    CHECK(flat < 14336, "the sky band is not the flat 0x2C fallback (%d/%d px)", flat, 64 * 224);
    CHECK(nz > 6000, "the mountains are painted after the edge exit (%d lit px)", nz);
    /* and keep walking to the far end: the whole of Muralla scrolls under the
     * same panorama, which is what docs/screenshots/town.png captures */
    while (s.town.scroll_col < s.tmap.width - 0x24 && steps < 8000) { shell_frame(&s); steps++; }
    CHECK(s.town.scroll_col == s.tmap.width - 0x24, "the hero reaches Muralla's right end");
    CHECK(s.town.back_steps == 48 + s.town.scroll_col,
          "the strips have rotated once per scrolled column, cmap's 48 included (%d vs %d)",
          s.town.back_steps, s.town.scroll_col);
    town_render(EFB, &s.town);
    nz = 0;
    for (int y = 14; y < 78; y++) for (int x = 48; x < 272; x++)
        { uint8_t v = EFB[y * FB_W + x]; nz += (v && v != 0x2C); }
    CHECK(nz > 6000, "and the panorama is still there at the far end (%d lit px)", nz);
    /* GT_SCROLL_NEAR_RIGHT slides the strips the same way the map goes -- one
     * more scrolled column moves the y142 strip 8 px *left*, the y150 strip 16
     * (town.c blit_strip; measured against the town_castle / town_edge_*
     * captures).  The port used to rotate them the other way. */
    {
        static uint8_t FB2[FB_W * FB_H];
        int s0 = s.town.back_steps;
        s.town.back_steps = s0 + 1;
        town_render(FB2, &s.town);
        s.town.back_steps = s0;
        int ok142 = 1, ok150 = 1;
        for (int x = 48; x + 16 < 272; x++) {
            if (FB2[142 * FB_W + x] != EFB[142 * FB_W + x + 8]) ok142 = 0;
            if (FB2[150 * FB_W + x] != EFB[150 * FB_W + x + 16]) ok150 = 0;
        }
        CHECK(ok142, "a scrolled column slides the y142 strip 8 px left");
        CHECK(ok150, "and the y150 strip 16 px left");
    }
}

/* The static screen art nothing in fight.bin or town.bin owns: mole.bin's
 * frame + HUD panel (GAME.BIN A185) and the two backdrop painters. */
static void t_art(const char *dir)
{
    static ScreenFrame f;
    CHECK(gfx_load_screen_frame(&f, dir) == 0, "mole.bin (ZELRES2[7]) loads");
    if (!f.loaded) return;
    /* the four blocks it paints, and nothing between them */
    int on_left = 0, on_right = 0, on_top = 0, on_hud = 0, on_play = 0;
    for (int y = 0; y < 200; y++)
        for (int x = 0; x < 320; x++) {
            int v = f.on[y * 320 + x];
            if (x < 48)            on_left  += v;
            else if (x >= 272)     on_right += v;
            else if (y < 13)       on_top   += v;
            else if (y >= 158)     on_hud   += v;
            else                   on_play  += v;
        }
    CHECK(on_left == 48 * 200,  "the left stone frame is 48x200 (mole.bin 0x34)");
    CHECK(on_right == 48 * 200, "the right stone frame is 48x200 (0x55)");
    CHECK(on_top == 224 * 13,   "the strip above the playfield is 224x13 (0x0E)");
    CHECK(on_hud == 224 * 42,   "the HUD panel is 224x42 (0x80)");
    CHECK(on_play == 0,         "and it never touches the playfield");
    /* the panel is the grey 0x08 (white+black pair) with three white slot frames */
    int grey = 0, white = 0;
    for (int y = 158; y < 200; y++)
        for (int x = 48; x < 272; x++) {
            uint8_t v = f.px[y * 320 + x];
            if (v == 0x08) grey++;
            if (v == 0x09) white++;
        }
    CHECK(grey > 3000, "the HUD panel is mostly grey (%d px)", grey);
    CHECK(white > 100, "with white item-slot frames (%d px)", white);
    /* the two cyan marks 0x38C ORs on at (8,47) and (304,47) */
    int cyan = 0;
    for (int y = 47; y < 52; y++)
        for (int x = 0; x < 320; x++)
            if ((f.px[y * 320 + x] & 0x24) == 0x24) cyan++;
    CHECK(cyan > 0, "0x38C's cyan marks are there (%d px)", cyan);

    /* ympd.bin: 224x88 at (48,14), colours 0/1/4/5 only, and two ground strips */
    static TownBackdrop b;
    CHECK(town_load_backdrop(&b, dir, 0) == 0, "ympd.bin (ZELRES2[8]) loads");
    if (b.loaded) {
        CHECK(b.back_y0 == 14 && b.back_y1 == 102, "ympd paints y14..101");
        CHECK(!b.has_far, "and has no separate far strip");
        int nz = 0, bad = 0;
        for (int y = 14; y < 102; y++)
            for (int x = 48; x < 272; x++) {
                uint8_t v = b.back[y * 320 + x];
                if (v) nz++;
                int l = v >> 3, r = v & 7;
                if (!(l == 0 || l == 1 || l == 4 || l == 5) || !(r == 0 || r == 1 || r == 4 || r == 5)) bad++;
            }
        CHECK(nz > 10000, "the mountain panorama is painted (%d non-black px)", nz);
        CHECK(bad == 0, "every pair is one of the four colours 0,1,4,5 (0x34F9)");
        int s142 = 0, s150 = 0;
        for (int r = 0; r < 8; r++)
            for (int x = 0; x < 112; x++) { s142 += b.near142[r][x] != 0; s150 += b.near150[r][x] != 0; }
        CHECK(s142 > 200 && s150 > 200, "both near strips have content (%d / %d px)", s142, s150);
    }
    /* ckpd.bin: 224x72 at (48,30) plus the 112x16 far strip */
    CHECK(town_load_backdrop(&b, dir, 1) == 0, "ckpd.bin (ZELRES2[9]) loads");
    if (b.loaded) {
        CHECK(b.back_y0 == 30 && b.back_y1 == 102, "ckpd paints y30..101");
        CHECK(b.has_far, "and a far parallax strip of its own");
        int nz = 0, far = 0;
        for (int y = 30; y < 102; y++)
            for (int x = 48; x < 272; x++) nz += b.back[y * 320 + x] != 0;
        for (int r = 0; r < 16; r++) for (int x = 0; x < 112; x++) far += b.far14[r][x] != 0;
        CHECK(nz > 4000, "the cave ceiling is painted (%d px)", nz);
        CHECK(far > 200, "the far strip has content (%d px)", far);
    }
    /* encnt.grp: "ENCOUNTER!" in the two reds 0x10 and 0x12 on black */
    static EncounterCard card;
    CHECK(gfx_load_encounter(&card, dir) == 0, "encnt.grp (ZELRES3[55]) + gfmcga's 0x4588 map load");
    if (card.loaded) {
        int red = 0, bad = 0;
        for (int i = 0; i < ENCNT_W * ENCNT_H; i++) {
            uint8_t v = card.px[i];
            if (v) red++;
            if (v && v != 0x10 && v != 0x12) bad++;
        }
        CHECK(red > 1000, "the card has %d lit pixels", red);
        CHECK(bad == 0, "and only 0x10 / 0x12 (4092's [4FF5] / [4FF6])");
        int left = 0;
        for (int y = 0; y < ENCNT_H; y++) left += card.px[y * ENCNT_W] != 0;
        CHECK(left == 0, "and its leftmost column is clear (the map's column 0 is cell 0)");
    }
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
        {"edges", t_edges}, {"npcs", t_npcs}, {"sentry", t_sentry}, {"hand-off", t_handoff},
        {"dialogue menu", t_dialogue_menu},
        {"save file", t_save}, {"capture routes", t_capture_routes},
        {"edge backdrop", t_edge_backdrop},
        {"screen art", t_art},
    };
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; i++) {
        int before = fails;
        tests[i].fn(dir);
        fprintf(stderr, "%-14s %s\n", tests[i].name, fails == before ? "ok" : "FAILED");
    }
    fprintf(stderr, "%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
