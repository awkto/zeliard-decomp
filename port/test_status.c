/* test_status.c — the status / inventory screen (select.bin, status.c).
 *
 * The eight potion effects are checked against the jump table at select.bin
 * A452 (docs/TOWN.md §12.4): what each one changes in the player record and by
 * how much, including the two that do nothing when their slot is empty and the
 * Kioku Feather's menu_result = 8.  The rest drives the real screen with a
 * scripted joystick: the row lists come out of the record, moving the cursor
 * writes [9D] / [9E], and equipping a sword is visible in damage_for_source. */
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
#include "status.h"

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; fprintf(stderr, "  FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

static const char *G_DIR = "../zeliard";
static TextFont FONT;
static ItemPics PICS;
static Game     G;
static Map      DUMMY_MAP;
static Tileset  DUMMY_TILES;

static void fresh_player(void)
{
    memset(&DUMMY_MAP, 0, sizeof DUMMY_MAP);
    DUMMY_MAP.width = 64; DUMMY_MAP.row_bias = 10; DUMMY_MAP.cavern = 1;
    memset(&DUMMY_TILES, 0, sizeof DUMMY_TILES);
    game_init(&G, &DUMMY_MAP, &DUMMY_TILES);
    if (player_load_stdply(&G, G_DIR)) CHECK(0, "STDPLY.BIN loads");
    player_page_pull(&G);
}

/* ------------------------------------------------- the scripted joystick */
typedef struct { uint8_t dirs, btns, menu; int frames; } Step;
static const Step *SCRIPT;
static int SCRIPT_N, SCRIPT_I, SCRIPT_LEFT;

static void present(Status *s)
{
    while (SCRIPT_LEFT == 0 && SCRIPT_I < SCRIPT_N) { SCRIPT_LEFT = SCRIPT[SCRIPT_I].frames; SCRIPT_I++; }
    if (SCRIPT_LEFT == 0) { s->dirs = 0; s->buttons = 0; s->menu_key = 0; s->done = 1; return; }
    const Step *st = &SCRIPT[SCRIPT_I - 1];
    SCRIPT_LEFT--;
    s->dirs = st->dirs; s->buttons = st->btns; s->menu_key = st->menu;
}

static Status ST;
static void run_status(int in_town, const Step *script, int n)
{
    memset(&ST, 0, sizeof ST);
    ST.present = present; ST.frame_guard = 4000;
    SCRIPT = script; SCRIPT_N = n; SCRIPT_I = SCRIPT_LEFT = 0;
    status_open(&ST, &G, &FONT, &PICS, in_town);
    status_loop(&ST);
}
/* open the screen with no script at all — just the initial draw */
static void open_only(int in_town)
{
    static const Step none[] = {{0, 0, 0, 1}};
    run_status(in_town, none, 1);
}

/* ----------------------------------------------------------- itemp.grp */
static void t_itemp(void)
{
    CHECK(PICS.loaded, "itemp.grp (ZELRES2[27]) loads");
    CHECK(PICS.sec[0] == 14, "section 0 starts right after the 7 pointers (got %u)", PICS.sec[0]);
    for (int i = 1; i < 7; i++)
        CHECK(PICS.sec[i] > PICS.sec[i - 1] && PICS.sec[i] < PICS.len,
              "section %d offset %u is inside the image (len %u)", i, PICS.sec[i], (unsigned)PICS.len);
    /* section 0 holds six 270-byte sword pictures, sections 1..6 192-byte icons */
    CHECK(PICS.sec[1] - PICS.sec[0] == 6u * 270u, "section 0 = 6 sword pictures (%u bytes)",
          PICS.sec[1] - PICS.sec[0]);
    for (int i = 1; i < 6; i++)
        CHECK((PICS.sec[i + 1] - PICS.sec[i]) % 192u == 0,
              "section %d is a whole number of 32x16 icons (%u)", i, PICS.sec[i + 1] - PICS.sec[i]);
    CHECK(PICS.have_blank, "GMMCGA.BIN @2658 blank slot picture read");
    /* the sword picture actually paints something inside its 20x18 box */
    static uint8_t fb[TEXT_W * TEXT_H];
    memset(fb, 0, sizeof fb);
    itemp_sword(fb, &PICS, 0, 0x17, 0x4D);
    int set = 0;
    for (int y = 0x4D; y < 0x4D + 18; y++)
        for (int x = 184; x < 204; x++) if (fb[y * TEXT_W + x]) set++;
    CHECK(set > 25, "the Training Sword picture draws %d pixels", set);
}

/* ------------------------------------------------------------ the lists */
static void t_lists(void)
{
    fresh_player();
    open_only(1);
    CHECK(ST.n_magic == 0 && ST.n_items == 0 && ST.n_potions == 0,
          "a fresh player has empty rows (%d/%d/%d)", ST.n_magic, ST.n_items, ST.n_potions);
    CHECK(ST.pane == 0xFF, "with nothing to select the screen only idles (pane %u)", ST.pane);

    /* spells 1, 3 and 6 learned (kenjpro writes [BB+n-1]) */
    fresh_player();
    G.page[P_SPELLS + 0] = 0xFF; G.page[P_SPELLS + 2] = 0xFF; G.page[P_SPELLS + 5] = 0xFF;
    G.magic_sel = 3; G.page[0x9D] = 3;
    open_only(1);
    CHECK(ST.n_magic == 3, "three spells listed (%d)", ST.n_magic);
    CHECK(ST.magic_list[0] == 1 && ST.magic_list[1] == 3 && ST.magic_list[2] == 6,
          "the list is the spell numbers (%u %u %u)", ST.magic_list[0], ST.magic_list[1], ST.magic_list[2]);
    CHECK(ST.magic_cursor == 1, "the cursor starts on the selected spell (%d)", ST.magic_cursor);
    CHECK(ST.pane == 0, "the magic row is the default pane");

    /* key items: the leading 0 slot plus the packed [A1..A5] */
    fresh_player();
    G.page[0xA1] = 1; G.page[0xA2] = 5;                 /* Feruza shoes + Asbestos cape */
    G.shoes = 5; G.page[0x9E] = 5;
    open_only(1);
    CHECK(ST.n_items == 3, "two items + the NO USE slot (%d)", ST.n_items);
    CHECK(ST.item_list[0] == 0 && ST.item_list[1] == 1 && ST.item_list[2] == 5,
          "item list 0,1,5 (%u %u %u)", ST.item_list[0], ST.item_list[1], ST.item_list[2]);
    CHECK(ST.item_cursor == 2, "the cursor starts on the worn item (%d)", ST.item_cursor);

    /* potions: the slots hold the drug id + 1 (drugpro A26B) */
    fresh_player();
    G.page[P_POTIONS + 0] = 1; G.page[P_POTIONS + 1] = 8;    /* Ken'ko + Kioku Feather */
    open_only(0);
    CHECK(ST.n_potions == 3, "two potions + the NO USE slot (%d)", ST.n_potions);
    CHECK(ST.potion_list[1] == 1 && ST.potion_list[2] == 8, "potion list 1,8");
    CHECK(ST.pane == 2, "with only potions the potion row is the default pane outside town");
    /* ... but not in town (A09E) */
    open_only(1);
    CHECK(ST.pane == 0xFF, "the potion row is not offered in town (pane %u)", ST.pane);
}

/* ------------------------------------------------------- potion effects */
/* drink potion `id` (0..7) from the first slot and return menu_result */
static int drink(int id)
{
    G.page[P_POTIONS + 0] = (uint8_t)(id + 1);
    G.page[P_POTIONS + 1] = 0; G.page[P_POTIONS + 2] = 0;
    G.page[P_POTIONS + 3] = 0; G.page[P_POTIONS + 4] = 0;
    open_only(0);
    ST.potion_cursor = 1;
    ST.potion_sel = ST.potion_list[1];
    int r = status_use_potion(&ST);
    player_page_pull(&G);
    return r;
}

static void t_potions(void)
{
    /* id 0 Ken'ko Potion: +80 HP, capped */
    fresh_player();
    G.max_hp = 300; G.hp = 100;
    CHECK(drink(0) == 1, "menu_result = the slot value");
    CHECK(G.hp == 180, "Ken'ko Potion +80 HP (%u)", G.hp);
    G.hp = 280;
    drink(0);
    CHECK(G.hp == 300, "Ken'ko Potion is capped at max_hp (%u)", G.hp);
    CHECK(G.page[P_POTIONS] == 0, "the slot is emptied");

    /* id 1 Juu-en Fruit: HP to full */
    fresh_player();
    G.max_hp = 300; G.hp = 7;
    drink(1);
    CHECK(G.hp == 300, "Juu-en Fruit refills HP (%u)", G.hp);

    /* id 2 Elixir of Kashi: only the selected spell */
    fresh_player();
    G.magic_sel = 2; G.page[0x9D] = 2;
    G.magic_max[1] = 6; G.magic_count[1] = 1;
    G.magic_max[0] = 12; G.magic_count[0] = 2;
    drink(2);
    CHECK(G.magic_count[1] == 6, "Elixir refills the selected spell (%u)", G.magic_count[1]);
    CHECK(G.magic_count[0] == 2, "Elixir leaves the others alone (%u)", G.magic_count[0]);
    /* with no spell selected it is wasted */
    fresh_player();
    G.magic_sel = 0; G.page[0x9D] = 0;
    G.magic_max[0] = 12; G.magic_count[0] = 0;
    drink(2);
    CHECK(G.magic_count[0] == 0, "Elixir does nothing with no spell selected");
    CHECK(G.page[P_POTIONS] == 0, "... but is still consumed");

    /* id 3 Chikara Powder: every spell */
    fresh_player();
    for (int i = 0; i < 7; i++) { G.magic_max[i] = (uint8_t)(3 + i); G.magic_count[i] = 0; }
    drink(3);
    for (int i = 0; i < 7; i++)
        CHECK(G.magic_count[i] == 3 + i, "Chikara Powder refills spell %d (%u)", i + 1, G.magic_count[i]);

    /* id 4 Magia Stone: the four orbs, phases 0/4/8/12, 0x50 hits */
    fresh_player();
    drink(4);
    CHECK(G.orbs[0].phase == 0 && G.orbs[1].phase == 4 && G.orbs[2].phase == 8 && G.orbs[3].phase == 12,
          "Magia Stone orb phases (%u %u %u %u)", G.orbs[0].phase, G.orbs[1].phase, G.orbs[2].phase, G.orbs[3].phase);
    CHECK(G.orbs[0].speed == 1 && G.orbs[1].speed == 0xFF && G.orbs[2].speed == 0xFF && G.orbs[3].speed == 1,
          "Magia Stone orb directions +1/-1/-1/+1");
    for (int i = 0; i < 4; i++) CHECK(G.orbs[i].hits == 0x50, "orb %d gets 80 hits (%u)", i, G.orbs[i].hits);

    /* id 5 Holy Water of Acero: A520, capped at [96] */
    fresh_player();
    G.shield = 3;                                       /* Stone Shield: +100, max 180 */
    G.page[P_SHIELD_MAX] = 180; G.page[P_SHIELD_MAX + 1] = 0;
    G.shield_hp = 20;
    drink(5);
    CHECK(G.shield_hp == 120, "Holy Water +100 on the Stone Shield (%u)", G.shield_hp);
    G.shield_hp = 150;
    drink(5);
    CHECK(G.shield_hp == 180, "Holy Water is capped at [96] (%u)", G.shield_hp);
    CHECK(HOLY_WATER[0] == 80 && HOLY_WATER[1] == 90 && HOLY_WATER[2] == 100 &&
          HOLY_WATER[3] == 110 && HOLY_WATER[4] == 115 && HOLY_WATER[5] == 120,
          "the A520 table is 80/90/100/110/115/120");
    /* wasted without a shield */
    fresh_player();
    G.shield = 0; G.shield_hp = 0;
    drink(5);
    CHECK(G.shield_hp == 0, "Holy Water does nothing without a shield");

    /* id 6 Sabre Oil: [E4] += 1, and that is a real damage multiplier */
    fresh_player();
    G.sword = 2; G.level = 4;
    unsigned before = damage_for_source(&G, 1);
    drink(6);
    CHECK(G.attack_bonus == 1, "Sabre Oil sets [E4] = 1 (%u)", G.attack_bonus);
    unsigned after = damage_for_source(&G, 1);
    CHECK(after == before * 2, "Sabre Oil doubles sword damage (%u -> %u)", before, after);
    drink(6);
    CHECK(G.attack_bonus == 2, "Sabre Oil stacks (%u)", G.attack_bonus);
    CHECK(damage_for_source(&G, 1) == before * 3, "a second Sabre Oil triples it (%u)",
          damage_for_source(&G, 1));

    /* id 7 Kioku Feather: menu_result 8 and the screen closes */
    fresh_player();
    CHECK(drink(7) == 8, "the Kioku Feather leaves 8 in menu_result");
    CHECK(ST.done, "... and returns straight out of the overlay");
    CHECK(ST.menu_result == 8, "menu_result [FF4B] = 8");
}

/* ------------------------------------------------ selecting and equipping */
static void t_select(void)
{
    /* Right on the magic row moves the cursor and rewrites [9D] */
    static const Step right[] = {{0, 0, 0, 2}, {DIR_RIGHT, 0, 0, 2}, {0, 0, 0, 2}};
    fresh_player();
    G.page[P_SPELLS + 0] = 0xFF; G.page[P_SPELLS + 3] = 0xFF;   /* spells 1 and 4 */
    G.magic_sel = 1; G.page[0x9D] = 1;
    run_status(1, right, 3);
    CHECK(ST.magic_cursor == 1, "Right moves the magic cursor (%d)", ST.magic_cursor);
    CHECK(G.magic_sel == 4, "the selected spell is now 4 ([9D] = %u)", G.magic_sel);
    CHECK(G.page[0x9D] == 4, "the record page agrees");

    /* Right on the item row wears the item ([9E]) */
    static const Step right2[] = {{0, 0, 0, 2}, {DIR_RIGHT, 0, 0, 2}, {0, 0, 0, 2}};
    fresh_player();
    G.page[0xA1] = 4;                                   /* Ruzeria shoes */
    G.shoes = 0; G.page[0x9E] = 0;
    run_status(1, right2, 3);
    CHECK(G.shoes == 4, "the item row wears the item ([9E] = %u)", G.shoes);
    /* and Left takes it off again (the leading NO USE slot) */
    static const Step left[] = {{0, 0, 0, 2}, {DIR_LEFT, 0, 0, 2}, {0, 0, 0, 2}};
    run_status(1, left, 3);
    CHECK(G.shoes == 0, "the NO USE slot takes it off ([9E] = %u)", G.shoes);

    /* Down leaves the magic row for the item row */
    static const Step down[] = {{0, 0, 0, 2}, {DIR_DOWN, 0, 0, 2}, {0, 0, 0, 2}};
    fresh_player();
    G.page[P_SPELLS] = 0xFF; G.magic_sel = 1; G.page[0x9D] = 1;
    G.page[0xA1] = 1;
    run_status(1, down, 3);
    CHECK(ST.pane == 1, "Down moves to the item row (pane %u)", ST.pane);

    /* the menu key closes the screen, but only after it has been released */
    static const Step close[] = {{0, 0, 1, 3}, {0, 0, 0, 2}, {0, 0, 1, 1}, {0, 0, 0, 40}};
    fresh_player();
    G.page[P_SPELLS] = 0xFF; G.magic_sel = 1; G.page[0x9D] = 1;
    run_status(1, close, 4);
    CHECK(ST.frames <= 8, "the second Enter closes the screen (%u frames)", ST.frames);
}

/* -------------------------------------------------- equipment / inventory */
static void t_equipment(void)
{
    /* damage_for_source is the shared damage formula the sword feeds (9851) */
    fresh_player();
    G.level = 4;
    G.sword = 1; unsigned d1 = damage_for_source(&G, 1);
    G.sword = 4; unsigned d4 = damage_for_source(&G, 1);
    CHECK(d4 > d1, "a Knight's sword hits harder than a Training sword (%u > %u)", d4, d1);

    /* the INVENTORY window names whatever is equipped */
    fresh_player();
    G.sword = 6; G.shield = 2;
    G.page[P_SHIELD_MAX] = 80; G.page[P_SHIELD_MAX + 1] = 0;
    G.shield_hp = 80;
    G.keys = 3; G.lion_keys = 1;
    G.page[0x9A] = 0xFF;                                /* the Elf Crest */
    open_only(1);
    CHECK(strcmp(SWORD_NAME[5][0], "Enchantment") == 0, "sword 6 is the Enchantment Sword");
    CHECK(strcmp(SHIELD_NAME[1][0], "Wise Man\\s") == 0, "shield 2 is the Wise Man's Shield");
    /* something was painted in each INVENTORY row */
    const uint8_t *fb = status_framebuffer(&ST);
    int sword_px = 0, shield_px = 0, key_px = 0, crest_px = 0;
    for (int y = 0x4D; y < 0x4D + 18; y++) for (int x = 184; x < 204; x++) sword_px += fb[y * TEXT_W + x] != 0;
    for (int y = 0x61; y < 0x71; y++)      for (int x = 186; x < 202; x++) shield_px += fb[y * TEXT_W + x] != 0;
    for (int y = 0x75; y < 0x85; y++)      for (int x = 186; x < 202; x++) key_px += fb[y * TEXT_W + x] != 0;
    for (int y = 0x89; y < 0x99; y++)      for (int x = 192; x < 208; x++) crest_px += fb[y * TEXT_W + x] != 0;
    CHECK(sword_px > 20, "the sword picture is drawn (%d px)", sword_px);
    CHECK(shield_px > 20, "the shield icon is drawn (%d px)", shield_px);
    CHECK(key_px > 10, "the key icon is drawn (%d px)", key_px);
    CHECK(crest_px > 10, "the crest icon is drawn (%d px)", crest_px);

    /* nothing equipped: the boxes stay empty */
    fresh_player();
    G.sword = 0; G.shield = 0; G.keys = 0; G.lion_keys = 0;
    open_only(1);
    fb = status_framebuffer(&ST);
    sword_px = 0;
    for (int y = 0x4D; y < 0x4D + 18; y++) for (int x = 184; x < 204; x++) sword_px += fb[y * TEXT_W + x] != 0;
    CHECK(sword_px == 0, "no sword, no picture (%d px)", sword_px);
}

/* ----------------------------------------------------------- the layout */
static void t_layout(void)
{
    fresh_player();
    open_only(1);
    const uint8_t *fb = status_framebuffer(&ST);
    /* the four windows are framed in white (PC-88 colour 1 = VGA 0x09) */
    struct { int x4, y, w4, h; } W[4] = {
        { 0x0C, 0x0E, 0x38, 0x33 }, { 0x0C, 0x3F, 0x22, 0x30 },
        { 0x0C, 0x6D, 0x22, 0x30 }, { 0x2D, 0x3F, 0x17, 0x5E },
    };
    for (int i = 0; i < 4; i++) {
        int x = W[i].x4 * 4 + W[i].w4 * 2, y = W[i].y;      /* the middle of the top edge */
        CHECK(fb[y * TEXT_W + x] == 0x09, "window %d has a white top edge at (%d,%d) = %02X",
              i, x, y, fb[y * TEXT_W + x]);
        CHECK(fb[(y + W[i].h - 1) * TEXT_W + x] == 0x09, "window %d has a white bottom edge", i);
    }
    /* the header of the active row is red (0x12), the others green (0x1B).
     * With nothing to select `pane` is 0xFF, so all four are green. */
    fresh_player();
    G.page[P_SPELLS] = 0xFF; G.magic_sel = 1; G.page[0x9D] = 1;
    open_only(1);
    fb = status_framebuffer(&ST);
    int red = 0, green = 0;
    for (int y = 0x12; y < 0x1A; y++) for (int x = 0x34; x < 0x34 + 104; x++) {
        red   += fb[y * TEXT_W + x] == 0x12;
        green += fb[y * TEXT_W + x] == 0x1B;
    }
    CHECK(red > 40 && green == 0, "SELECT-MAGIC: is red when active (%d red, %d green)", red, green);
    red = green = 0;
    for (int y = 0x43; y < 0x4B; y++) for (int x = 0x34; x < 0x34 + 40; x++) {
        red   += fb[y * TEXT_W + x] == 0x12;
        green += fb[y * TEXT_W + x] == 0x1B;
    }
    CHECK(green > 20 && red == 0, "WEAR: is green when inactive (%d red, %d green)", red, green);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../zeliard";
    G_DIR = dir;
    if (text_load_font(&FONT, dir)) {
        fprintf(stderr, "  (font.grp not available in %s: skipping the status checks)\n", dir);
        return 0;
    }
    if (itemp_load(&PICS, dir)) fprintf(stderr, "  (itemp.grp not available: the icons will not draw)\n");
    struct { const char *name; void (*fn)(void); } tests[] = {
        {"itemp.grp", t_itemp}, {"row lists", t_lists}, {"potion effects", t_potions},
        {"selection", t_select}, {"equipment", t_equipment}, {"layout", t_layout},
    };
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; i++) {
        int before = fails;
        tests[i].fn();
        fprintf(stderr, "%-16s %s\n", tests[i].name, fails == before ? "ok" : "FAILED");
    }
    fprintf(stderr, "%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
