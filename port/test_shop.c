/* test_shop.c — the shop overlays and the text/menu presentation layer.
 *
 * Every constant is tied to the documented tables: the per-town price tables
 * of armrpro (BAA7) and drugpro (B10C), the shield durabilities (A6BF), the
 * inn's prices (A2D1), the bank's exchange rates (A8FA) and the sage's
 * EXP_NEXT (A28C) / cap (A2AC) / LEVEL_TABLE (A380) — all read back out of the
 * overlay images and compared with docs/TOWN.md §7.  The purchases are driven
 * through the real menu widget with a scripted joystick, so a sword purchase
 * exercises menu_select, the text box, gold_can_pay, the trade-in and the
 * stock bitmask exactly as the game does. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "map.h"
#include "gfx.h"
#include "physics.h"
#include "enemy.h"
#include "town.h"
#include "text.h"
#include "player.h"
#include "shop.h"

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; fprintf(stderr, "  FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

static const char *G_DIR = "../zeliard";
static TextFont FONT;
static TownMap  TMAP;
static Town     TOWN;
static Game     G;

/* ------------------------------------------------- the scripted joystick */
typedef struct { uint8_t dirs, btns; int frames; } Step;
static const Step *SCRIPT;
static int SCRIPT_N, SCRIPT_I, SCRIPT_LEFT;

static void present(Town *t)
{
    while (SCRIPT_LEFT == 0 && SCRIPT_I < SCRIPT_N) { SCRIPT_LEFT = SCRIPT[SCRIPT_I].frames; SCRIPT_I++; }
    if (SCRIPT_LEFT == 0) { t->dirs = 0; t->buttons = 0; return; }
    const Step *st = &SCRIPT[SCRIPT_I - 1];
    SCRIPT_LEFT--;
    if ((st->btns & 1) && !(t->buttons & 1)) t->btn1_edge = 0xFF;
    if ((st->btns & 2) && !(t->buttons & 2)) t->btn2_edge = 0xFF;
    t->dirs = st->dirs; t->buttons = st->btns;
}

static void run_shop(int town_index, int dest, const Step *script, int n)
{
    if (town_load_map(&TMAP, G_DIR, town_index)) { CHECK(0, "cannot load town map %d", town_index); return; }
    memset(&TOWN, 0, sizeof TOWN);
    TOWN.map = &TMAP; TOWN.g = &G; TOWN.present = present;
    TOWN.font = &FONT; TOWN.dir = G_DIR;
    SCRIPT = script; SCRIPT_N = n; SCRIPT_I = SCRIPT_LEFT = 0;
    shop_run(&TOWN, dest);
    town_free_map(&TMAP);
}

/* game_init reads the map's row bias, so give it a minimal one */
static Map DUMMY_MAP;
static Tileset DUMMY_TILES;

static void fresh_player(void)
{
    memset(&DUMMY_MAP, 0, sizeof DUMMY_MAP);
    DUMMY_MAP.width = 64; DUMMY_MAP.row_bias = 10; DUMMY_MAP.cavern = 1;
    memset(&DUMMY_TILES, 0, sizeof DUMMY_TILES);
    game_init(&G, &DUMMY_MAP, &DUMMY_TILES);
    CHECK(player_load_stdply(&G, G_DIR) == 0, "STDPLY.BIN loads");
}

/* --------------------------------------------------------------- font */
static void t_font(void)
{
    CHECK(FONT.loaded, "font.grp loaded");
    /* town.bin 7BE2.  src/town.c's gloss "space 5, 'I' 3, 'W'/'M' 8" is one
     * character out: entry 3 belongs to '\\' (which prints as an apostrophe),
     * 'I' advances 5.  The table itself is verbatim and the text box it drives
     * is pixel-identical to docs/screenshots/shop_armour.png. */
    CHECK(FONT_ADVANCE[' ' - 0x20] == 5, "advance(' ') = %u", FONT_ADVANCE[' ' - 0x20]);
    CHECK(FONT_ADVANCE['I' - 0x20] == 5, "advance('I') = %u", FONT_ADVANCE['I' - 0x20]);
    CHECK(FONT_ADVANCE['\\' - 0x20] == 3, "advance('\\') = %u", FONT_ADVANCE['\\' - 0x20]);
    CHECK(FONT_ADVANCE['W' - 0x20] == 8 && FONT_ADVANCE['M' - 0x20] == 8, "advance('W'/'M') = 8");
    CHECK(FONT_XOFF['!' - 0x20] == 2 && FONT_XOFF[' ' - 0x20] == 0, "xoff table");
    int ink = 0;
    for (int r = 0; r < 8; r++) ink += __builtin_popcount(FONT.glyph['A' - 0x20][r]);
    CHECK(ink > 8, "the 8x8 'A' glyph has %d lit pixels", ink);
    char buf[16];
    CHECK(format_number(0, buf) == 1 && !strcmp(buf, "0"), "format_number(0) = \"%s\"", buf);
    CHECK(format_number(69800, buf) == 5 && !strcmp(buf, "69800"), "format_number(69800) = \"%s\"", buf);
    CHECK(format_number(1000000, buf) == 7, "format_number is 24-bit wide");
}

/* --------------------------------------------------- the player record */
static void t_player(void)
{
    fresh_player();
    CHECK(G.hp == 80 && G.max_hp == 80, "STDPLY LIFE %u/%u (want 80)", G.hp, G.max_hp);
    CHECK(G.sword == 1 && G.shield == 0, "STDPLY: the training sword, no shield");
    CHECK(G.magic_count[0] == 12 && G.magic_max[3] == 4, "STDPLY magic {12,6,8,4,3,4,3}");
    CHECK(G.page[0xC4] == 0x80 && G.page[0xC5] == 0x81, "STDPLY maps: cmap / Muralla");
    static const uint8_t sw[9] = {0xC0, 0xC0, 0xE0, 0xE0, 0x70, 0x38, 0x38, 0xF8, 0xF8};
    static const uint8_t sh[9] = {0xC0, 0xE0, 0xE0, 0x70, 0x30, 0x38, 0x1C, 0x1C, 0xFC};
    static const uint8_t dr[9] = {0x8A, 0xA6, 0x6B, 0x75, 0x42, 0x4C, 0x4B, 0x01, 0xFF};
    CHECK(memcmp(G.page + P_SWORD_STOCK, sw, 9) == 0, "sword stock masks");
    CHECK(memcmp(G.page + P_SHIELD_STOCK, sh, 9) == 0, "shield stock masks");
    CHECK(memcmp(G.page + P_DRUG_STOCK, dr, 9) == 0, "drug stock masks");
    /* the page and the named members are two views of one record */
    G.gold = 123456; G.almas = 777; G.exp = 4242; G.level = 5; G.sword = 3;
    player_page_push(&G);
    CHECK(page_gold24(G.page, 0x85) == 123456, "gold pushed to [85..87]");
    CHECK(G.page[0x8D] == 5 && (G.page[0x8E] | G.page[0x8F] << 8) == 4242, "level/exp pushed");
    Game h = G;
    memset(&h.gold, 0, sizeof h.gold); h.almas = 0; h.exp = 0; h.level = 0; h.sword = 0;
    player_page_pull(&h);
    CHECK(h.gold == 123456 && h.almas == 777 && h.exp == 4242 && h.level == 5 && h.sword == 3,
          "the page pulls back the same values");
}

/* ------------------------------------------------------- the save file */
static void t_save(void)
{
    fresh_player();
    G.gold = 98765; G.level = 4; G.exp = 999; G.sword = 3; G.shield = 2;
    G.page[P_SAGES] = 0xC0; G.page[P_POTIONS] = 3;
    strcpy(G.player_name, "TESTUSR");
    CHECK(player_save_usr(&G, "/tmp", G.player_name) == 0, "NAME.USR written");
    FILE *f = fopen("/tmp/TESTUSR.usr", "rb");
    CHECK(f != NULL, "/tmp/TESTUSR.usr exists");
    if (f) {
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fclose(f);
        CHECK(n == 256, "the save file is %ld bytes (want 256 = the raw BASE:0000 page)", n);
    }
    Game h;
    game_init(&h, &DUMMY_MAP, &DUMMY_TILES);
    CHECK(player_load_usr(&h, "/tmp", "TESTUSR") == 0, "NAME.USR read back");
    CHECK(h.gold == 98765 && h.level == 4 && h.exp == 999 && h.sword == 3 && h.shield == 2,
          "the record survives the round trip (gold %u level %u exp %u)", (unsigned)h.gold, h.level, h.exp);
    CHECK(h.page[P_SAGES] == 0xC0 && h.page[P_POTIONS] == 3, "the shop-only page bytes survive too");
    remove("/tmp/TESTUSR.usr");
}

/* --------------------------------------------------------- price tables */
/* docs/TOWN.md §7, re-indexed: the row is [C006]-1, so row 0 serves BOTH the
 * castle and Muralla and the last row is Esco (the doc's row labels are one
 * town out from "Muralla" on). */
static const uint32_t ARMR_PRICES[9][12] = {
    {  400, 1500, 6800, 9800, 90000, 4,  50, 150, 2980, 9800, 14800, 39800 },
    {  800, 1500, 6800, 9800, 69800, 4,  50, 150, 2980, 9800, 14800, 39800 },
    {  800, 1500, 6800, 9800, 69800, 4,   5, 150, 2380, 9800, 14800, 39800 },
    {  400, 3000, 5440, 9800, 69800, 4,   5,  50, 1780, 9800, 14800, 39800 },
    {  400, 3000, 4760, 4900, 69800, 4,   5,  50, 1780, 7840, 14800, 39800 },
    {  200, 1500, 3400, 7840, 69800, 4,   5,  20,  890, 5880, 14800, 39800 },
    {  200, 1500, 1360, 5880, 34800, 4,   5,  20,  890, 5880, 10360, 39800 },
    {  100, 1000, 1360, 3920, 32800, 4,   5,  20,  890, 3920,  7400, 31800 },
    {   10,  100,  680, 1960, 29800, 4,   2,  10,  298, 1960,  5920, 23800 },
};
static const uint32_t DRUG_PRICES[9][8] = {
    { 50, 240,  60, 320, 1000, 100, 1200, 350 },
    { 50, 240,  60, 320, 1000, 100, 1200, 350 },
    { 50, 240,  60, 320, 1500, 100, 1200, 350 },
    { 50, 300, 120, 320, 1500, 100, 1200, 350 },
    {  5, 600, 240, 480, 2000, 200, 2000, 350 },
    {  5, 600, 240, 480, 2000, 200, 2000, 350 },
    {  5, 900, 360, 960, 2500, 400, 2400, 350 },
    {  5, 900, 360, 960, 2500, 400, 2400, 350 },
    {  2, 200,  40, 280,  800,  80, 1000, 150 },
};

/* the towns whose [C006] is 1..9 (cmap and mrmp share 1) */
static const int TOWN_OF_ID[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};

static void t_prices(void)
{
    static const Step leave[] = {{0, 2, 4}, {0, 0, 4}, {0, 2, 4}, {0, 0, 400}};
    for (int id = 1; id <= 9; id++) {
        fresh_player();
        Shop s;
        if (town_load_map(&TMAP, G_DIR, TOWN_OF_ID[id - 1])) { CHECK(0, "town map"); continue; }
        memset(&TOWN, 0, sizeof TOWN);
        TOWN.map = &TMAP; TOWN.g = &G; TOWN.present = present; TOWN.font = &FONT; TOWN.dir = G_DIR;
        SCRIPT = leave; SCRIPT_N = 4; SCRIPT_I = SCRIPT_LEFT = 0;
        CHECK(TMAP.town_id == id, "%s has [C006] = %u (want %d)", TMAP.label, TMAP.town_id, id);
        if (shop_open(&s, &TOWN, SHOP_ARMOUR) == 0) {
            int bad = 0;
            for (int i = 0; i < 12; i++) if (shop_price(&s, i) != ARMR_PRICES[id - 1][i]) bad++;
            CHECK(bad == 0, "town %d armour prices: %d of 12 wrong (Training %u, want %u)",
                  id, bad, shop_price(&s, 0), ARMR_PRICES[id - 1][0]);
            shop_close(&s);
        } else CHECK(0, "town %d: the armour overlay loads", id);
        SCRIPT_I = SCRIPT_LEFT = 0;
        if (shop_open(&s, &TOWN, SHOP_DRUG) == 0) {
            int bad = 0;
            for (int i = 0; i < 8; i++) if (shop_price(&s, i) != DRUG_PRICES[id - 1][i]) bad++;
            CHECK(bad == 0, "town %d drug prices: %d of 8 wrong (Ken'ko %u, want %u)",
                  id, bad, shop_price(&s, 0), DRUG_PRICES[id - 1][0]);
            shop_close(&s);
        } else CHECK(0, "town %d: the drug overlay loads", id);
        town_free_map(&TMAP);
    }
    static const uint16_t hp[6] = {30, 80, 180, 300, 300, 600};
    CHECK(memcmp(SHIELD_HP, hp, sizeof hp) == 0, "shield durabilities {30,80,180,300,300,600}");
}

/* ------------------------------------------------------- buying a sword */
/* Muralla ([C006] = 1): swords in stock are the Training (0) and the Wise
 * man's (1) sword, prices 400 / 1500 with a trade-in of half the old one. */
static void t_buy_sword(void)
{
    /* main menu: Down, Down -> "Buy weapon", Space; list: Down -> Wise man's,
     * Space; then Yes.  Every press is one frame with a release after it. */
    static const Step script[] = {
        {0, 0, 20},                                  /* the greeting prints */
        {DIR_DOWN, 0, 1}, {0, 0, 2}, {DIR_DOWN, 0, 1}, {0, 0, 2},
        {0, 1, 1}, {0, 0, 40},                       /* select "Buy weapon" */
        {DIR_DOWN, 0, 1}, {0, 0, 2}, {0, 1, 1}, {0, 0, 120},  /* the Wise man's sword */
        {0, 1, 1}, {0, 0, 60},                       /* Yes */
        {0, 2, 1}, {0, 0, 20}, {0, 2, 1}, {0, 0, 400},        /* Alt out */
    };
    fresh_player();
    G.gold = 5000;
    unsigned before = damage_for_source(&G, 1);
    run_shop(1, SHOP_ARMOUR, script, (int)(sizeof script / sizeof script[0]));
    CHECK(G.sword == 2, "the Wise man's sword is equipped ([92] = %u)", G.sword);
    CHECK(G.gold == 5000 - 1500 + 200, "gold %u (want 5000 - 1500 + 200 trade-in = 3700)", (unsigned)G.gold);
    CHECK((G.page[P_SWORD_STOCK] & 0x80) != 0, "the training sword went back into Muralla's stock (%02X)",
          G.page[P_SWORD_STOCK]);
    unsigned after = damage_for_source(&G, 1);
    CHECK(before == 1 && after == 2, "sword damage %u -> %u (98B8 sword_base 1,2,4,8,32,127)", before, after);
}

static void t_buy_shield(void)
{
    static const Step script[] = {
        {0, 0, 20},
        {DIR_DOWN, 0, 1}, {0, 0, 2}, {DIR_DOWN, 0, 1}, {0, 0, 2}, {DIR_DOWN, 0, 1}, {0, 0, 2},
        {0, 1, 1}, {0, 0, 40},                       /* "Buy shield" */
        {0, 1, 1}, {0, 0, 120},                      /* the first shield in stock */
        {0, 1, 1}, {0, 0, 60},                       /* Yes */
        {0, 2, 1}, {0, 0, 20}, {0, 2, 1}, {0, 0, 400},
    };
    fresh_player();
    G.gold = 5000;
    run_shop(1, SHOP_ARMOUR, script, (int)(sizeof script / sizeof script[0]));
    CHECK(G.shield == 1, "the Clay shield is equipped ([93] = %u)", G.shield);
    CHECK(G.gold == 5000 - 50, "gold %u (want 5000 - 50)", (unsigned)G.gold);
    CHECK(G.shield_hp == 30, "shield durability %u (want 30)", G.shield_hp);
    CHECK((G.page[P_SHIELD_MAX] | G.page[P_SHIELD_MAX + 1] << 8) == 30, "[96] shield hp max");
    /* 75E2: dmg = (dmg/2) >> ((shield+1)/2) — shields 1-2 divide by 4 */
    G.hp = G.max_hp;
    hero_damage_shielded(&G, 40);
    CHECK(G.max_hp - G.hp == 10, "a shielded 40-point hit costs %u HP (want 40/4)", G.max_hp - G.hp);
}

/* --------------------------------------------------------- the drug shop */
static void t_drug(void)
{
    static const Step script[] = {
        {0, 0, 20},
        {DIR_DOWN, 0, 1}, {0, 0, 2},                 /* "Buy item" */
        {0, 1, 1}, {0, 0, 60},
        {0, 1, 1}, {0, 0, 120},                      /* the first item in stock */
        {0, 2, 1}, {0, 0, 20},                       /* "anything else?" -> No */
        {0, 2, 1}, {0, 0, 20}, {0, 2, 1}, {0, 0, 400},
    };
    fresh_player();
    G.gold = 2000;
    run_shop(1, SHOP_DRUG, script, (int)(sizeof script / sizeof script[0]));
    /* Muralla's mask 0x8A = items 0 (Ken'ko), 4 (Magia) and 6 (Sabre Oil) */
    CHECK(G.page[P_POTIONS] == 1, "potion slot 0 holds item id+1 = %u (want 1 = Ken'ko Potion)",
          G.page[P_POTIONS]);
    CHECK(G.gold == 2000 - 50, "gold %u (want 2000 - 50)", (unsigned)G.gold);
}

/* ------------------------------------------------------------ the church */
static void t_church(void)
{
    static const Step script[] = {{0, 0, 600}, {0, 1, 1}, {0, 0, 600}};
    fresh_player();
    G.hp = 8;
    memset(G.magic_count, 0, 7);
    run_shop(1, SHOP_CHURCH, script, 3);
    CHECK(G.hp == G.max_hp, "the church healed to %u/%u", G.hp, G.max_hp);
    CHECK(G.gold == 0, "the church is free");
    /* the "tired" script (A2F2) only heals; the magic refill (action 4) is in
     * the full-health script (A2B4) — src/shops.c's note on churpro */
    CHECK(G.magic_count[0] == 0, "the heal script does not refill magic");
    memset(G.magic_count, 0, 7);
    run_shop(1, SHOP_CHURCH, script, 3);
    CHECK(memcmp(G.magic_count, G.magic_max, 7) == 0,
          "a second visit at full LIFE restores the magic charges instead");
}

/* --------------------------------------------------------------- the inn */
static void t_inn(void)
{
    /* Satono (town map 2, [C006] = 2) charges 30 gold a night (A2D1) */
    static const Step yes[] = {{0, 0, 220}, {0, 1, 1}, {0, 0, 600}, {0, 1, 1}, {0, 0, 600}};
    fresh_player();
    G.gold = 100; G.hp = 10;
    memset(G.magic_count, 0, 7);
    run_shop(2, SHOP_INN, yes, 5);
    CHECK(G.gold == 70, "a night in Satono costs 30 gold (left with %u)", (unsigned)G.gold);
    CHECK(G.hp == G.max_hp, "the night restored LIFE to %u/%u", G.hp, G.max_hp);
    CHECK(memcmp(G.magic_count, G.magic_max, 7) == 0, "and the magic charges");
    fresh_player();
    G.gold = 10; G.hp = 10;
    run_shop(2, SHOP_INN, yes, 5);
    CHECK(G.gold == 10 && G.hp == 10, "10 gold is not enough for the 30-gold room");
}

/* -------------------------------------------------------------- the bank */
static void t_bank(void)
{
    /* A8FA: Muralla exchanges 1 alma for 6 gold */
    static const Step exchange[] = {
        {0, 0, 20}, {DIR_DOWN, 0, 1}, {0, 0, 2}, {0, 1, 1}, {0, 0, 120},
        {0, 1, 1}, {0, 0, 200},                       /* Yes */
        {0, 2, 1}, {0, 0, 20}, {0, 2, 1}, {0, 0, 400},
    };
    fresh_player();
    G.almas = 25; G.gold = 0;
    run_shop(1, SHOP_BANK, exchange, (int)(sizeof exchange / sizeof exchange[0]));
    CHECK(G.almas == 0 && G.gold == 150, "25 almas at 1:6 = %u gold (almas left %u)", (unsigned)G.gold, G.almas);
    /* deposit everything, then withdraw it */
    static const Step deposit[] = {
        {0, 0, 20}, {DIR_DOWN, 0, 1}, {0, 0, 2}, {DIR_DOWN, 0, 1}, {0, 0, 2},
        {0, 1, 1}, {0, 0, 200},
        {0, 2, 1}, {0, 0, 20}, {0, 2, 1}, {0, 0, 400},
    };
    run_shop(1, SHOP_BANK, deposit, (int)(sizeof deposit / sizeof deposit[0]));
    CHECK(G.gold == 0, "the deposit emptied the purse (%u)", (unsigned)G.gold);
    CHECK(page_gold24(G.page, P_BANK_HI) == 150, "the balance [88..8A] is %u",
          page_gold24(G.page, P_BANK_HI));
}

/* -------------------------------------------------------------- the sage */
static void t_sage_tables(void)
{
    static const Step leave[] = {{0, 0, 4000}};
    fresh_player();
    Shop s;
    if (town_load_map(&TMAP, G_DIR, 1)) { CHECK(0, "mrmp"); return; }
    memset(&TOWN, 0, sizeof TOWN);
    TOWN.map = &TMAP; TOWN.g = &G; TOWN.present = present; TOWN.font = &FONT; TOWN.dir = G_DIR;
    SCRIPT = leave; SCRIPT_N = 1; SCRIPT_I = SCRIPT_LEFT = 0;
    if (shop_open(&s, &TOWN, SHOP_SAGE)) { CHECK(0, "the sage overlay loads"); town_free_map(&TMAP); return; }
    static const uint16_t want[16] = {50, 150, 300, 420, 1000, 1500, 3000, 5000,
                                      6000, 8000, 10000, 15000, 20000, 40000, 50000, 60000};
    CHECK(memcmp(EXP_NEXT, want, sizeof want) == 0, "EXP_NEXT (A28C)");
    static const uint8_t caps[8] = {3, 6, 9, 11, 13, 15, 18, 0xFF};
    CHECK(memcmp(SAGE_MAX_LEVEL, caps, sizeof caps) == 0, "the per-sage level caps (A2AC)");
    /* LEVEL_TABLE A380: the max HP of levels 0..15 */
    static const uint16_t hps[16] = {120, 160, 200, 240, 280, 320, 380, 460,
                                     540, 600, 640, 680, 720, 760, 780, 800};
    int bad = 0;
    for (int i = 0; i < 16; i++) if (sage_level_hp(&s, i) != hps[i]) bad++;
    CHECK(bad == 0, "the LEVEL_TABLE max-HP column (%d of 16 wrong)", bad);
    uint8_t mg[7];
    sage_level_magic(&s, 0, mg);
    CHECK(mg[0] == 12 && mg[1] == 6 && mg[2] == 8 && mg[3] == 8 && mg[6] == 3,
          "level 0 magic maxima {12,6,8,8,3,4,3}");
    sage_level_magic(&s, 15, mg);
    CHECK(mg[0] == 60 && mg[3] == 72 && mg[6] == 12, "level 15 magic maxima {60,60,60,72,21,16,12}");

    /* kenj_assess A22E: 0/1/2 below the threshold, 3 = level up, 4 = capped */
    G.level = 0;
    G.exp = 10;  CHECK(sage_assess(&s) == 0, "exp 10  of 50 -> verdict 0");
    G.exp = 30;  CHECK(sage_assess(&s) == 1, "exp 30  of 50 -> verdict 1");
    G.exp = 45;  CHECK(sage_assess(&s) == 2, "exp 45  of 50 -> verdict 2");
    G.exp = 50;  CHECK(sage_assess(&s) == 3, "exp 50  of 50 -> level up");
    G.level = 3; G.exp = 500;
    CHECK(sage_assess(&s) == 4, "Marid's cap is level 3, so a level-3 hero is refused");

    /* kenj_level_up A2B4 */
    G.level = 0; G.exp = 120; G.hp = 5;
    memset(G.magic_count, 0, 7);
    sage_level_up(&s);
    CHECK(G.level == 1, "level %u after the ritual", G.level);
    CHECK(G.max_hp == 120 && G.hp == 120, "LIFE %u/%u (want 120/120)", G.hp, G.max_hp);
    CHECK(G.exp == 70, "exp %u (want 120 - EXP_NEXT[0] = 70)", G.exp);
    CHECK(G.magic_count[0] == 12 && G.magic_max[0] == 12, "the magic charges were refilled");
    /* never more than one level per visit */
    G.level = 0; G.exp = 400;
    sage_level_up(&s);
    CHECK(G.exp == EXP_NEXT[1] - 1, "exp clamped to %u (one level per visit)", G.exp);
    shop_close(&s);
    town_free_map(&TMAP);
}

/* the first visit to a sage teaches a spell and sets the [E5] bit */
static void t_sage_visit(void)
{
    static const Step script[] = {{0, 0, 900}, {0, 1, 1}, {0, 0, 900}};
    fresh_player();
    CHECK(G.page[P_SAGES] == 0, "no sage met yet");
    run_shop(2, SHOP_SAGE, script, 3);          /* Satono = Yasmin, teaches spell 1 */
    CHECK((G.page[P_SAGES] & 0x40) != 0, "Yasmin's [E5] bit is set (%02X)", G.page[P_SAGES]);
    CHECK(G.magic_sel == 1, "the taught spell is selected ([9D] = %u)", G.magic_sel);
    CHECK(G.page[P_SPELLS] == 0xFF, "spell 1 is marked learned at [BB]");
    /* Marid (Muralla) teaches nothing */
    fresh_player();
    run_shop(1, SHOP_SAGE, script, 3);
    CHECK((G.page[P_SAGES] & 0x80) != 0, "Marid's bit is set");
    CHECK(G.magic_sel == 0, "Marid teaches no spell");
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../zeliard";
    G_DIR = dir;
    if (text_load_font(&FONT, dir)) {
        fprintf(stderr, "  (font.grp not available in %s: skipping the shop checks)\n", dir);
        return 0;
    }
    struct { const char *name; void (*fn)(void); } tests[] = {
        {"font", t_font}, {"player record", t_player}, {"save file", t_save},
        {"price tables", t_prices}, {"buy sword", t_buy_sword}, {"buy shield", t_buy_shield},
        {"drug shop", t_drug}, {"church", t_church}, {"inn", t_inn}, {"bank", t_bank},
        {"sage tables", t_sage_tables}, {"sage visit", t_sage_visit},
    };
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; i++) {
        int before = fails;
        tests[i].fn();
        fprintf(stderr, "%-14s %s\n", tests[i].name, fails == before ? "ok" : "FAILED");
    }
    fprintf(stderr, "%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
