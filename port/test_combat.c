/* test_combat.c — milestone (b) assertions: the eai1 tables, the damage
 * formulas, frog contact + knockback, sword kills, EXP, the bat wake window
 * and the drop table (docs/FIGHT.md §6-§8, docs/ENEMIES.md §2).
 *
 * It also renders one headless frame with a frog standing where the DOSBox
 * capture docs/screenshots/cavern_enemy.png has one, so `make verify` can
 * diff the sprite's bounding box against the real game. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "map.h"
#include "gfx.h"
#include "physics.h"
#include "render.h"
#include "enemy.h"
#include "png.h"

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; fprintf(stderr, "  FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

static Map tmap;
static Tileset ttiles;
static AiOverlay ovl;
static Game G;
static void present(Game *g) { (void)g; }

/* same synthetic tileset/map as test_physics.c */
static void test_tiles(void)
{
    memset(&ttiles, 0, sizeof ttiles);
    static const uint8_t pass[24] = {0, 1, 2, 8, 0x0B, 0x0C};
    memcpy(ttiles.lists, pass, 24);
    ttiles.lists[0x18] = 0x0B; ttiles.lists[0x1C] = 0x0C; ttiles.lists[0x20] = 0x0D;
}
static void flat_map(void)
{
    static char rows[MAP_ROWS][MAP_MAX_WIDTH + 1];
    static const char *ptr[MAP_ROWS];
    for (int r = 0; r < MAP_ROWS; r++) { memset(rows[r], r >= 13 ? '6' : '.', 96); rows[r][96] = 0; ptr[r] = rows[r]; }
    map_from_text(&tmap, 96, ptr);
}
static void start(int col, int row, int face_left)
{
    game_init(&G, &tmap, &ttiles);
    G.present = present; G.ai = &ovl;
    game_place(&G, col, row, face_left);
    G.nobj = 0;
    game_first_frame(&G);
}
static void step(uint8_t dirs, uint8_t btns)
{
    G.dirs = dirs;
    if ((btns & 1) && !(G.buttons & 1)) G.btn1_edge = 0xFF;
    G.buttons = btns;
    game_step(&G);
}
static int hcol(void) { return game_hero_map_col(&G); }

/* one enemy of `cls` with its top-left at map (col,row) */
static MapObj *put_enemy(int col, int row, int cls, int facing_right)
{
    MapObj *o = &G.obj[0];
    memset(o, 0, sizeof *o);
    o->col = (uint16_t)col; o->row = (uint8_t)row; o->rcol = 0xFF;
    o->type = (uint8_t)cls; o->hit = (uint8_t)(facing_right ? 0x80 : 0);
    o->home_col = 0xFFFF; o->home_row = (uint8_t)row; o->home_type = (uint8_t)cls;
    G.nobj = 1;
    return o;
}

/* ------------------------------------------------------------------ tables */
static void t_tables(void)
{
    CHECK(ovl.exp[0] == 3 && ovl.exp[1] == 2 && ovl.exp[2] == 5 && ovl.exp[3] == 3,
          "A008 EXP per class = {%u,%u,%u,%u} (want 3,2,5,3)", ovl.exp[0], ovl.exp[1], ovl.exp[2], ovl.exp[3]);
    CHECK(ovl.contact[0] == 5 && ovl.contact[1] == 5 && ovl.contact[2] == 15 && ovl.contact[3] == 8,
          "A010 contact damage = {%u,%u,%u,%u} (want 5,5,15,8)",
          ovl.contact[0], ovl.contact[1], ovl.contact[2], ovl.contact[3]);
    const uint8_t *d0 = ai_drop_list(&ovl, 0), *d1 = ai_drop_list(&ovl, 1);
    const uint8_t *d2 = ai_drop_list(&ovl, 2), *d3 = ai_drop_list(&ovl, 3);
    CHECK(d0 && d0[0] == 5 && d0[1] == 4 && d0[2] == 4 && d0[3] == 0, "bat drops A24C {10G,1G,1G,-}");
    CHECK(d1 && d1[0] == 4 && d1[1] == 0 && d1[2] == 4 && d1[3] == 0, "snail drops A250 {1G,-,1G,-}");
    CHECK(d2 && d2[0] == 4 && d2[1] == 0 && d2[2] == 4 && d2[3] == 0, "frog drops A250 {1G,-,1G,-}");
    CHECK(d3 && d3[0] == 5 && d3[1] == 0 && d3[2] == 0 && d3[3] == 0, "hedgehog drops A248 {10G,-,-,-}");
    /* the frame lists the renderer uses */
    const uint8_t *fr = ai_frame(&ovl, 2, 0x80, 0);
    CHECK(fr && fr[0] == 0 && fr[1] == 141 && fr[2] == 142 && fr[3] == 143 && fr[4] == 144,
          "frog frame 0 facing right = A141 {0,141,142,143,144}");
    fr = ai_frame(&ovl, 2, 0, 0);
    CHECK(fr && fr[1] == 117, "frog frame 0 facing left = A11E {0,117,...}");
}

/* ------------------------------------------------------------------ damage */
static void t_damage(void)
{
    flat_map(); start(30, 10, 0);
    G.sword = 1; G.level = 0; G.attack_type = 0; G.attack_bonus = 0;
    CHECK(damage_for_source(&G, 1) == 1, "training sword at level 0: %u (want 1)", damage_for_source(&G, 1));
    G.level = 4;
    CHECK(damage_for_source(&G, 1) == 3, "sword 1 + level/2 at level 4: %u (want 3)", damage_for_source(&G, 1));
    G.attack_type = 2;
    CHECK(damage_for_source(&G, 1) == 6, "down-thrust doubles: %u (want 6)", damage_for_source(&G, 1));
    G.attack_type = 0; G.level = 0;
    CHECK(damage_for_source(&G, 0) == 1, "stomp = level/2 + 1");
    CHECK(damage_for_source(&G, 9) == 4, "orb = (level+1)*4");
    CHECK(damage_for_source(&G, 2) == 2 && damage_for_source(&G, 8) == 255, "magic table 98BE");
    G.sword = 6;
    CHECK(damage_for_source(&G, 1) == 0x7F, "Enchantment sword base 127");

    /* frog HP 1 -> one hit; bat HP 2 -> two hits (sword 1) */
    G.sword = 1;
    MapObj *o = put_enemy(31, 11, 2, 0);
    o->hp = 1; o->hit = 0x21;                    /* stunned by a sword hit */
    enemy_take_damage(&G, o);
    CHECK((o->type & 0x08) && G.exp == 5, "one training-sword hit kills the frog and gives 5 EXP (type %02x exp %u)", o->type, G.exp);
    G.exp = 0;
    o = put_enemy(31, 11, 0, 0);
    o->hp = 2; o->hit = 0x21;
    enemy_take_damage(&G, o);
    CHECK(o->hp == 1 && !(o->type & 0x08), "first hit leaves the bat on 1 HP: hp %u type %02x", o->hp, o->type);
    o->hit = 0x21;
    enemy_take_damage(&G, o);
    CHECK((o->type & 0x08) && G.exp == 3, "second hit kills the bat and gives 3 EXP (type %02x exp %u)", o->type, G.exp);
    CHECK((o->flags & 0xF) == 5 || (o->flags & 0xF) == 4 || (o->flags & 0xF) == 0,
          "a drop id from the bat's list was rolled: %u", o->flags & 0xF);
}

/* -------------------------------------------------- contact and knockback */
static void t_contact(void)
{
    flat_map(); start(30, 10, 0);
    MapObj *o = put_enemy(31, 11, 2, 0);         /* frog, top-left one column right of the hero */
    o->hp = 1;
    unsigned hp0 = G.hp;
    step(0, 0);
    CHECK(o->rcol == 17, "the frog's ring column is recomputed by the enemy pass: %u (want 17)", o->rcol);
    CHECK(G.hp == hp0 - 15, "frog contact costs 15 LIFE per overlapping frame: %u -> %u", hp0, G.hp);
    CHECK(G.hit_side[2] == 0xFF, "the contact was flagged on the right-hand columns");
    CHECK(hcol() == 28, "knockback is 2 cells away from the hit side: hero col %d (want 28)", hcol());
    /* out of reach: no damage */
    flat_map(); start(30, 10, 0);
    o = put_enemy(35, 11, 2, 0); o->hp = 1;
    hp0 = G.hp;
    step(0, 0);
    CHECK(G.hp == hp0 && hcol() == 30, "a frog 5 columns away does not touch the hero");
    /* the bat's 5 and the hedgehog's 8 come from the same table */
    flat_map(); start(30, 10, 0);
    o = put_enemy(31, 11, 0, 0); o->hp = 2; hp0 = G.hp;
    step(0, 0);
    CHECK(G.hp == hp0 - 5, "bat contact costs 5: %u -> %u", hp0, G.hp);
}

/* ------------------------------------------------------------- the sword */
static void t_sword(void)
{
    flat_map(); start(30, 10, 0);                 /* facing right */
    MapObj *o = put_enemy(32, 11, 2, 0);          /* frog two columns to the right: inside the slash */
    o->hp = 1;
    G.sword = 1;
    int killed = 0;
    for (int i = 0; i < 8 && !killed; i++) { step(0, 1); killed = (o->type & 0x08) != 0; }
    CHECK(killed, "one sword swing kills the frog (type %02x hp %u)", o->type, o->hp);
    CHECK(G.exp == 5, "the frog awards 5 EXP: %u", G.exp);
    /* the dying object becomes its drop and then a pickup */
    for (int i = 0; i < 10; i++) step(0, 0);
    CHECK((o->type & 0x10) || (o->col >> 8) == 0xFF, "the corpse turned into an item or vanished: type %02x col %04x", o->type, o->col);

    /* the bat needs two hits */
    flat_map(); start(30, 10, 0);
    o = put_enemy(32, 11, 0, 0); o->hp = 2; G.sword = 1; G.exp = 0;
    killed = 0;
    for (int i = 0; i < 12 && !killed; i++) { step(0, 1); killed = (o->type & 0x08) != 0; }
    CHECK(killed && G.exp == 3, "the bat dies after two sword hits and awards 3 EXP (exp %u)", G.exp);

    /* a sword-immune object (type bit 5) is never flagged */
    flat_map(); start(30, 10, 0);
    o = put_enemy(32, 11, 2, 0); o->hp = 1; o->type |= 0x20; G.exp = 0;
    for (int i = 0; i < 8; i++) step(0, 1);
    CHECK(!(o->type & 0x08) && G.exp == 0, "type & 0x20 is immune to the sword");
}

/* ------------------------------------------------------ drops and pickups */
static void t_drop(void)
{
    /* 90E6: a killed enemy animates for 6 frames, then becomes type 0x70|id */
    flat_map(); start(30, 10, 0);
    MapObj *o = put_enemy(31, 11, 2, 0);
    o->hp = 1;
    o->flags = (uint8_t)((o->flags & 0xF0) | 4);   /* drop id 4 = the 1 G coin */
    enemy_killed(&G, o);                          /* vec 25 */
    CHECK(o->type == 0x6A && o->phase == 0, "vec 25 sets type |= 0x68 (dying, immune, harmless): %02x", o->type);
    unsigned gold0 = (unsigned)G.gold;
    int became_item = 0;
    for (int i = 0; i < 8; i++) { step(0, 0); if (o->type == 0x74) became_item = 1; }
    CHECK(became_item, "the corpse becomes item type 0x74 after 6 dying frames");
    CHECK((unsigned)G.gold == gold0 + 1, "walking into the coin adds 1 gold: %u -> %u", gold0, (unsigned)G.gold);
    CHECK((o->col >> 8) == 0xFF, "and the object is removed");

    /* 97F2: a down-thrust kill always takes entry 0 of the drop list */
    flat_map(); start(30, 10, 0);
    o = put_enemy(31, 11, 3, 0);                  /* hedgehog: {10 G, -, -, -} */
    o->hp = 1; o->hit = 0x21;
    G.attack_type = 2; G.sword = 1;
    enemy_take_damage(&G, o);
    CHECK((o->flags & 0xF) == 5, "down-thrust kill rolls drop entry 0 (10 G): id %u", o->flags & 0xF);
    G.attack_type = 0;
}

/* ------------------------------------------------ the bat's wake-up window */
static void t_bat_wake(void)
{
    flat_map(); start(30, 10, 0);
    static const struct { int rcol, wake; } cases[] = {
        {0x09, 0}, {0x0A, 0}, {0x0B, 1}, {0x11, 1}, {0x1A, 1}, {0x1B, 0}, {0x20, 0},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        MapObj *o = put_enemy(30, 6, 0, 0);
        o->hp = 2; o->phase = 0; o->next = 0;
        o->rcol = (uint8_t)cases[i].rcol;
        eai1_entry(&G, o);
        CHECK((o->next == 0x40) == cases[i].wake,
              "bat at ring col %02x: next %02x (wake expected %d; the window is 0x0B..0x1A around the hero's body column 0x11)",
              cases[i].rcol, o->next, cases[i].wake);
    }
    /* the idle countdown runs first: phase 0x10 -> one frame of waiting */
    MapObj *o = put_enemy(30, 6, 0, 0);
    o->hp = 2; o->phase = 0x10; o->next = 0; o->rcol = 0x11;
    eai1_entry(&G, o);
    CHECK(o->phase == 0 && o->next == 0, "phase counts down 0x10 per frame before the bat can wake");
    eai1_entry(&G, o);
    CHECK(o->next == 0x40, "then it wakes");
}

/* --------------------------------------------------------------- the frog */
static void t_frog_hop(void)
{
    flat_map(); start(40, 10, 0);
    MapObj *o = put_enemy(30, 11, 2, 1);         /* far to the left, facing right (toward the hero) */
    o->hp = 1;
    int moved = 0;
    for (int i = 0; i < 12; i++) { step(0, 0); if (o->col > 30) moved = 1; }
    CHECK(moved, "the frog hops toward the hero (col %u)", o->col);
    CHECK(o->row <= 11, "and lands back on the ground (row %u)", o->row);
}

/* ------------------------------------------------- the DOSBox frog render */
/* docs/screenshots/cavern_enemy.png: the hero's top-left is at map (69,7) of
 * MP10 (scroll 53/61) and a frog sits two cells to his left, its 2x2 sprite's
 * top-left at screen cell (10,11) = map (67,8), facing LEFT and sitting.  The
 * frog is placed by hand so the frame index is deterministic; frame 1 (the
 * second sit frame, A11E+5) is the one in the capture — it reproduces the
 * 16x16 sprite box pixel for pixel, frame 0 differs in 8 px. */
#define FROG_PHASE 1
#define SNAIL_PHASE 0
#define FROG_BOX_X 128
#define FROG_BOX_Y 102
static int render_frog_shot(const char *dir, const char *out, int phase)
{
    static Map m; static Tileset t; static HeroGfx hero; static EnemyGfx eg;
    static uint8_t fb[FB_W * FB_H]; static uint8_t rgb[FB_W * FB_H * 3];
    if (map_load_system(&m, dir, 0)) return -1;
    if (gfx_load_tileset(&t, dir, m.tileset)) return -1;
    if (gfx_load_hero(&hero, dir)) return -1;
    if (gfx_load_enemy_cells(&eg, dir, m.enemies)) return -1;
    game_init(&G, &m, &t);
    G.present = present; G.ai = &ovl; G.egfx = &eg;
    game_place(&G, 69, 7, 0);
    G.nobj = 0;                                   /* the live enemies would have moved by now */
    game_first_frame(&G);
    MapObj *o = put_enemy(67, 8, 2, 0);           /* facing left in the capture */
    o->hp = 1; o->phase = (uint8_t)phase; o->rcol = 14;   /* 67 - scroll_col 53 */
    {   /* The second creature in the capture is a SNAIL (class 1, frame 0,
         * facing left) standing on the cave ceiling one row above the window,
         * so only its lower two cells (enp1 79/80) are visible.  DOSBOX_RECIPE
         * §6 and ENEMIES.md call it a "salmon/red ceiling blob" / bat: the
         * cells prove it is the class-1 snail. */
        MapObj *b = &G.obj[1];
        memset(b, 0, sizeof *b);
        b->col = 75; b->row = (uint8_t)((G.scroll_row - 1) & 0x3F); b->rcol = 22;
        b->type = 1; b->hp = 2; b->hit = 0; b->phase = SNAIL_PHASE;
        b->home_col = 0xFFFF;
        G.nobj = 2;
    }
    render_frame(fb, &G, &hero);
    render_hud(fb, &G, NULL);
    render_to_rgb(fb, rgb);
    return png_write_rgb(out, rgb, FB_W, FB_H);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../zeliard";
    const char *shot = argc > 2 ? argv[2] : "/tmp/zel_frog.png";
    test_tiles();
    if (ai_load(&ovl, dir, 0)) {
        fprintf(stderr, "  (EAI1 overlay not available in %s: skipping the combat checks)\n", dir);
        return 0;
    }
    struct { const char *name; void (*fn)(void); } tests[] = {
        {"eai1 tables", t_tables}, {"damage", t_damage}, {"contact", t_contact},
        {"sword", t_sword}, {"drops", t_drop}, {"bat wake", t_bat_wake}, {"frog hop", t_frog_hop},
    };
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; i++) {
        int before = fails;
        tests[i].fn();
        fprintf(stderr, "%-14s %s\n", tests[i].name, fails == before ? "ok" : "FAILED");
    }
    if (render_frog_shot(dir, shot, FROG_PHASE) == 0)
        fprintf(stderr, "%-14s wrote %s (frog 2x2 sprite at %d,%d; compare with tools/compare_shot.py --box)\n",
                "frog render", shot, FROG_BOX_X, FROG_BOX_Y);
    else
        fprintf(stderr, "%-14s skipped (game files not found in %s)\n", "frog render", dir);
    fprintf(stderr, "%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
