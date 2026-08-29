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
#include "text.h"
#include "png.h"

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; fprintf(stderr, "  FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

static const char *G_DIR = "../zeliard";
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

/* ------------------------------------------------------- the item states */
/* The 8E14 jump table read out of the overlay:
 *   8E14: 32 8e 8d 8e e9 8e f6 8e ab 8f ab 8f e8 8f f8 8f
 *         08 90 1c 90 9d 90 ab 8f 3c 90 7f 90 90 90
 * so 0x10 is 8E32 (breaks to the sword), 0x11 is 8E8D (breaks to a touch),
 * 0x1B is 8FAB (a coin, and 8FC0 makes it a hundred because `type & 0x0F` is
 * neither 4 nor 5), 0x1D is 907F (the Hero's Crest, `mov byte [0x9c],0xff`) and
 * 0x1E is 9090 (key item 1).  The port had 0x10 down as an eight-frame corpse
 * fade, which removed every breakable in the game on sight -- and 914C ORs an
 * event object's `+B/+D` pair into the player page as it goes, so MP30's crest
 * tree handed Garland the Hero's Crest for walking past it. */
static void t_items(void)
{
    flat_map(); start(30, 10, 0);
    MapObj *o = put_enemy(32, 11, 0xD0, 0);         /* state 0x10 */
    o->flags = 0x20; o->home_col = 0x0012; o->home_row = 0x08;   /* an event pair */
    o->next = 0x7D;                                 /* ... that breaks into a crest */
    G.page[0x12] = 0;
    for (int i = 0; i < 30; i++) step(0, 0);
    CHECK((o->col >> 8) != 0xFF, "0x10: a breakable does not remove itself (type %02X)", o->type);
    CHECK(G.page[0x12] == 0, "and does not fire its story flag: page[12] = %02X", G.page[0x12]);
    /* now hit it: 6F8D marks it, 8DB9 turns that into hit bit 5, 8E32 opens it */
    G.sword = 1;
    int broke = 0, took = 0;
    for (int i = 0; i < 80 && !took; i++) {
        step(0, (uint8_t)((i & 3) < 2 ? 1 : 0));
        if ((o->type & 0x1F) == 0x1D) broke = 1;
        if ((o->col >> 8) == 0xFF) took = 1;
    }
    CHECK(broke, "0x10: the blade opens it into its `next`, state 0x1D");
    CHECK(took && G.hero_crest == 0xFF, "0x1D: and taking that sets [9C] = %02X", G.hero_crest);
    CHECK(G.page[0x12] == 0x08, "and 914C's event pair sets page[12] = %02X", G.page[0x12]);

    /* 0x1B is a hundred gold, not a pair of boots */
    flat_map(); start(30, 10, 0);
    o = put_enemy(31, 11, 0x7B, 0);
    o->home_col = 0xFFFF;
    unsigned gold0 = (unsigned)G.gold;
    uint8_t shoes0 = G.shoes;
    for (int i = 0; i < 8 && (o->col >> 8) != 0xFF; i++) step(0, 0);
    CHECK((unsigned)G.gold == gold0 + 100, "0x1B: 100 gold (%u -> %u)", gold0, (unsigned)G.gold);
    CHECK(G.shoes == shoes0, "0x1B: and no shoes");

    /* 0x1E is key item 1 */
    flat_map(); start(30, 10, 0);
    o = put_enemy(31, 11, 0x7E, 0);
    o->home_col = 0xFFFF;
    memset(G.page + 0xA1, 0, 5);
    G.shoes = 0;
    for (int i = 0; i < 8 && (o->col >> 8) != 0xFF; i++) step(0, 0);
    CHECK(G.page[0xA1] == 1, "0x1E: 90B8 puts item 1 in the first free [A1..] slot (%02X)", G.page[0xA1]);
    G.hero_crest = 0; G.page[0x12] = 0; G.shoes = 0;
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
    render_hud(fb, &G, NULL, NULL, NULL);
    render_to_rgb(fb, rgb);
    return png_write_rgb(out, rgb, FB_W, FB_H);
}


/* ================================================================ shots */
/* docs/FIGHT.md §6 "Enemy projectiles": 1 cell per frame in one of 8
 * directions (85C2), death on a non-passable_shot cell, age >= life, and the
 * hero hit test with the shield rules (846F/8556). */
static void t_shots(void)
{
    /* the eight direction steps of 85A5 */
    static const int8_t D[8][2] = {{1,0},{1,-1},{0,-1},{-1,-1},{-1,0},{-1,1},{0,1},{1,1}};
    for (int d = 0; d < 8; d++) {
        start(40, 12, 0);
        shots_clear(&G);
        Shot t; memset(&t, 0, sizeof t);
        t.col = 8; t.row = 4; t.cell = 1; t.life = 30; t.flags = (uint8_t)d; t.damage = 5;
        t.flags |= 8;                                   /* pass through walls, so only the step is measured */
        shot_spawn(&G, &t);
        CHECK(G.projectile_count == 1, "dir %d: spawn -> count %u", d, G.projectile_count);
        uint8_t c0 = G.shots[0].col, r0 = G.shots[0].row;
        shots_update(&G);
        CHECK(G.shots[0].col == (uint8_t)(c0 + D[d][0]) && G.shots[0].row == (uint8_t)((r0 + D[d][1]) & 0x3F),
              "dir %d: (%u,%u) -> (%u,%u), expected (%d,%d)", d, c0, r0, G.shots[0].col, G.shots[0].row,
              c0 + D[d][0], (r0 + D[d][1]) & 0x3F);
    }
    /* 8611: at most 31 live shots */
    start(40, 12, 0);
    shots_clear(&G);
    Shot t; memset(&t, 0, sizeof t);
    t.col = 8; t.row = 4; t.cell = 1; t.life = 200; t.flags = 8;
    for (int i = 0; i < 40; i++) shot_spawn(&G, &t);
    CHECK(G.projectile_count == MAX_SHOTS, "shot list caps at 31, got %u", G.projectile_count);

    /* 8455: age >= life kills the shot */
    start(40, 12, 0);
    shots_clear(&G);
    memset(&t, 0, sizeof t);
    t.col = 8; t.row = 4; t.cell = 1; t.life = 3; t.flags = 8;
    shot_spawn(&G, &t);
    shots_update(&G); shots_update(&G);
    CHECK(G.shots[0].col != 0, "shot alive after 2 of 3 frames");
    shots_update(&G);
    CHECK(G.shots[0].col == 0, "shot dies when age reaches life");

    /* 8472: a solid cell kills a shot without flags & 8 */
    start(40, 12, 0);
    shots_clear(&G);
    memset(&t, 0, sizeof t);
    t.col = 0x10, t.row = (uint8_t)((G.scroll_row + 13) & 0x3F); t.cell = 1; t.life = 30; t.flags = 6;  /* down into the floor */
    shot_spawn(&G, &t);
    shots_update(&G);
    CHECK(G.shots[0].col == 0, "a shot dies on a solid cell");

    /* 846F/850E: a shot on the hero's row and column hits for `damage` */
    start(40, 12, 0);
    shots_clear(&G);
    G.shield = 0; G.hp = 0x50;
    memset(&t, 0, sizeof t);
    t.col = (uint8_t)(G.hero_scr_col + 4 + 1);           /* 84B4: the hero's own ring column, facing right */
    t.row = (uint8_t)((G.scroll_row + G.hero_scr_row) & 0x3F);
    t.cell = 1; t.life = 30; t.flags = 8 | 2; t.damage = 17;   /* moving up, so the row test uses the top row */
    t.row = (uint8_t)((t.row + 1) & 0x3F);
    shot_spawn(&G, &t);
    unsigned hp0 = G.hp;
    shots_update(&G);
    CHECK(G.hp == hp0 - 17, "shot damage 17: hp %u -> %u", hp0, G.hp);
    CHECK(G.shots[0].col == 0, "the shot is consumed by the hero");

    /* 854F: a shield of 4+ facing the shot blocks it completely */
    start(40, 12, 0);
    shots_clear(&G);
    G.shield = 4; G.shield_hp = 100; G.hp = 0x50;
    G.hero_flags |= FACE_LEFT;                           /* facing left, shot coming from the left (dir 0) */
    memset(&t, 0, sizeof t);
    t.col = (uint8_t)(G.hero_scr_col + 4 + 1);
    t.row = (uint8_t)((G.scroll_row + G.hero_scr_row) & 0x3F);
    t.cell = 1; t.life = 30; t.flags = 8 | 0; t.damage = 17;
    shot_spawn(&G, &t);
    hp0 = G.hp;
    shots_update(&G);
    CHECK(G.hp == hp0, "shield >= 4 facing the shot blocks it (hp %u -> %u)", hp0, G.hp);
}

/* ================================================================ magic */
/* docs/FIGHT.md §6 "Magic": a 6-frame cast, one charge on frame 4, bolts move
 * 2 cells/frame, hit source = magic_sel + 1 (8C4F), damage table 98BE. */
static void t_magic(void)
{
    start(40, 12, 0);
    G.magic_sel = 1; G.magic_count[0] = 3;
    G.btn2_edge = 0xFF;
    magic_input(&G);
    CHECK(G.casting == 0xFF && G.cast_timer == 0, "button 2 starts the cast");
    CHECK(G.sfx_request == 0x17, "cast start requests sound 0x17, got %02x", G.sfx_request);
    magic_input(&G);                                     /* 87F1: timer 2 */
    CHECK(G.magic_active == 0 && G.cast_timer == 2, "no spell before frame 4");
    magic_input(&G);                                     /* timer 4: the charge is spent */
    CHECK(G.magic_active == 0xFF, "the spell is released on cast frame 4");
    CHECK(G.magic_count[0] == 2, "one charge consumed, %u left", G.magic_count[0]);
    CHECK(G.magic[0].live && G.magic[0].dir == 1, "one bolt facing right");
    CHECK(G.magic[0].col == (uint16_t)(game_hero_map_col(&G)), "the bolt starts on the hero's column");
    /* 8BD0: 2 map columns per frame in its facing */
    uint16_t c0 = G.magic[0].col;
    magic_update(&G);
    CHECK(G.magic[0].col == (uint16_t)(c0 + 2), "bolt moves 2 columns right (%u -> %u)", c0, G.magic[0].col);
    /* 8AD4: spell 1 lives 5 frames */
    for (int i = 0; i < 4; i++) magic_update(&G);
    CHECK(G.magic_active == 0, "spell 1 ends after 5 frames");

    /* 98BE: magic damage by source */
    static const uint8_t want[7] = {2, 4, 8, 0x10, 0x20, 0x40, 0xFF};
    for (int sel = 1; sel <= 7; sel++)
        CHECK(damage_for_source(&G, (uint8_t)(sel + 1)) == want[sel - 1],
              "spell %d -> source %d -> damage %u, expected %u", sel, sel + 1,
              damage_for_source(&G, (uint8_t)(sel + 1)), want[sel - 1]);

    /* 8C4F: the bolt's 3x3 block marks the enemy hit with source magic_sel+1 */
    start(40, 12, 0);
    G.magic_sel = 2; G.magic_count[1] = 1;
    MapObj *e = put_enemy(46, 11, 0, 0);
    e->hp = 40;
    game_first_frame(&G);                                /* 8D19 places the sprite marker */
    G.btn2_edge = 0xFF;
    magic_input(&G); magic_input(&G); magic_input(&G);
    CHECK(G.magic_active == 0xFF, "spell 2 released");
    int hit = 0;
    for (int i = 0; i < 10 && !hit; i++) { magic_update(&G); if (e->hit & 0x40) hit = 1; }
    CHECK(hit, "the bolt reaches the enemy 6 columns away");
    CHECK((e->hit & 0x1F) == 3, "hit source is magic_sel + 1 = 3, got %u", e->hit & 0x1F);

    /* 8918: spell 7 hits every sprite in the window at once and stops */
    start(40, 12, 0);
    G.magic_sel = 7; G.magic_count[6] = 1;
    MapObj *a = put_enemy(44, 11, 0, 0);
    MapObj *b = &G.obj[1];
    memset(b, 0, sizeof *b);
    b->col = 50; b->row = 11; b->rcol = 0xFF; b->type = 1; b->home_col = 0xFFFF;
    G.nobj = 2;
    a->hp = 200; b->hp = 200;
    game_first_frame(&G);                                /* place the markers */
    G.btn2_edge = 0xFF;
    magic_input(&G); magic_input(&G); magic_input(&G);
    CHECK((a->hit & 0x40) && (b->hit & 0x40), "spell 7 hits both enemies in the window");
    CHECK(G.magic_active == 0, "spell 7 ends the same frame");
    CHECK((a->hit & 0x1F) == 8, "spell 7 hit source 8, got %u", a->hit & 0x1F);

    /* 87D1: no cast while attacking */
    start(40, 12, 0);
    G.magic_sel = 1; G.magic_count[0] = 1;
    G.attacking = 0xFF; G.btn2_edge = 0xFF;
    magic_input(&G);
    CHECK(G.casting == 0, "no cast while the sword is out");
}

/* ================================================================= orbs */
/* docs/FIGHT.md §6: orbs orbit the hero on the 16-entry table 8790 and hit
 * with source 9 = min(255, (level+1)*4) (9851). */
static void t_orbs(void)
{
    start(40, 12, 0);
    CHECK(damage_for_source(&G, 9) == (uint8_t)((G.level + 1) * 4),
          "orb damage (level+1)*4 = %u", damage_for_source(&G, 9));
    G.level = 100;
    CHECK(damage_for_source(&G, 9) == 255, "orb damage caps at 255");
    G.level = 0;
    MapObj *e = put_enemy(43, 11, 0, 0);
    e->hp = 200;
    game_first_frame(&G);
    orbs_arm(&G, 1, 1, 4);
    int hit = 0;
    for (int i = 0; i < 32 && !hit; i++) { orbs_update(&G); if (e->hit & 0x40) hit = 1; }
    CHECK(hit, "an armed orb hits an enemy beside the hero");
    if (hit) CHECK((e->hit & 0x1F) == 9, "orb hit source 9, got %u", e->hit & 0x1F);
}

/* ================================================================ sound */
/* the FF75 requests fight.bin produces (docs/FIGHT.md §6) reach the stub */
static void t_sound(void)
{
    CHECK(sound_name(3) && !strcmp(sound_name(3), "sword swing"), "sound 3 = sword swing");
    CHECK(sound_name(7) && !strcmp(sound_name(7), "enemy killed"), "sound 7 = enemy killed");
    start(40, 12, 0);
    unsigned before = sound_count(3);
    step(0, 1);                                          /* a sword swing requests sound 3 (6E96) */
    CHECK(sound_count(3) > before, "the sword swing reached the sound stub");
    CHECK(G.sfx_request == 0, "FF75 is consumed every frame");
}


/* ========================================================= the eai overlays */
/* The AI request table (fight.bin 9CBC) interleaves the cavern overlays with
 * the boss ones, so index 2 = EAI2, 4 = EAI3 ... 14 = EAI8.  These EXP and
 * contact-damage tables are the headers documented in src/ai/eai*.c, read out
 * of the real overlay images — they prove the right file was loaded. */
static void t_overlays(void)
{
    static const struct { int idx, n; const char *name; uint8_t exp[6], contact[6]; } OV[8] = {
        {0,  4, "EAI1", {3, 2, 5, 3},              {5, 5, 15, 8}},
        {2,  6, "EAI2", {10, 10, 4, 10, 4, 255},   {10, 10, 8, 10, 8, 40}},
        {4,  4, "EAI3", {20, 10, 10, 20},          {40, 40, 16, 40}},
        {6,  5, "EAI4", {10, 10, 0, 0, 20},        {20, 4, 80, 80, 80}},
        {8,  5, "EAI5", {50, 50, 20, 10, 10},      {40, 40, 20, 20, 10}},
        {10, 5, "EAI6", {100, 100, 50, 50, 0},     {80, 80, 40, 40, 80}},
        {12, 5, "EAI7", {80, 80, 200, 200, 50},    {80, 80, 80, 80, 40}},
        {14, 5, "EAI8", {255, 255, 255, 255, 255}, {160, 160, 60, 80, 80}},
    };
    for (int i = 0; i < 8; i++) {
        AiOverlay o;
        if (ai_load(&o, G_DIR, OV[i].idx)) { CHECK(0, "%s (AI index %d) loads", OV[i].name, OV[i].idx); continue; }
        int ok = 1;
        for (int c = 0; c < OV[i].n; c++) if (o.exp[c] != OV[i].exp[c]) ok = 0;
        CHECK(ok, "%s A008 EXP table: %u %u %u %u %u", OV[i].name, o.exp[0], o.exp[1], o.exp[2], o.exp[3], o.exp[4]);
        ok = 1;
        for (int c = 0; c < OV[i].n; c++) if (o.contact[c] != OV[i].contact[c]) ok = 0;
        CHECK(ok, "%s A010 contact table: %u %u %u %u %u", OV[i].name, o.contact[0], o.contact[1],
              o.contact[2], o.contact[3], o.contact[4]);
        CHECK(ai_drop_list(&o, 0) != NULL, "%s has a class-0 drop list", OV[i].name);
        ai_unload(&o);
    }
}

/* Every cavern map runs its overlay: the enemies must react (move, animate or
 * take damage) instead of standing inert. */
static void t_overlay_run(void)
{
    /* system map -> the cavern it belongs to (docs/RESOURCES.md) */
    static const int MAPS[7] = {2, 5, 8, 11, 14, 18, 23};   /* MP90 is the cavern-9 boss room */
    for (int i = 0; i < 7; i++) {
        static Map m; static Tileset t; static AiOverlay o; static EnemyGfx eg;
        if (map_load_system(&m, G_DIR, MAPS[i])) { CHECK(0, "map %d loads", MAPS[i]); continue; }
        if (gfx_load_tileset(&t, G_DIR, m.tileset)) continue;
        if (ai_load(&o, G_DIR, m.ai)) { CHECK(0, "map %d: AI overlay %d loads", MAPS[i], m.ai); continue; }
        gfx_load_enemy_cells(&eg, G_DIR, m.enemies);
        game_init(&G, &m, &t);
        G.present = present; G.ai = &o; G.egfx = &eg;
        game_place(&G, m.start_col != 0xFFFF ? m.start_col : 16, m.start_row ? m.start_row : m.row_bias, 0);
        G.hp = G.max_hp = 9999;                          /* survive the strong cavern-8 contact damage */
        uint8_t before[MAX_OBJS];
        for (int k = 0; k < G.nobj; k++) before[k] = (uint8_t)(G.obj[k].row ^ G.obj[k].phase ^ (uint8_t)G.obj[k].col);
        game_first_frame(&G);
        int seen = 0;
        for (int f = 0; f < 200; f++) {              /* walk far enough for enemies to enter the ring */
            step(DIR_RIGHT, 0);
            for (int k = 0; k < G.nobj; k++) if (G.obj[k].rcol != 0xFF) seen = 1;
        }
        int changed = 0;
        for (int k = 0; k < G.nobj; k++)
            if (before[k] != (uint8_t)(G.obj[k].row ^ G.obj[k].phase ^ (uint8_t)G.obj[k].col)) changed++;
        CHECK(seen, "%s (overlay %d): some of the %d enemies came into view", m.name, m.ai, G.nobj);
        CHECK(!seen || changed > 0, "%s (overlay %d): %d of %d enemies acted",
              m.name, m.ai, changed, G.nobj);
        CHECK(G.hp > 0, "%s: 40 frames without a crash", m.name);
        ai_unload(&o);
    }
}

/* docs/FIGHT.md §7: a tall (2x4) enemy uses two consecutive records; 94FF now
 * spawns both halves. */
static void t_tall_spawn(void)
{
    start(40, 12, 0);
    MapObj *o = &G.obj[0];
    memset(o, 0, sizeof *o * 2);
    o->col = 0xFF00; o->flags = 0x10;                    /* inactive, tall */
    o->home_col = 57; o->home_row = 8; o->home_type = 0; o->timer = 0xFF;   /* ring col 33 = off screen */
    o[1].col = 0xFF00;
    G.nobj = 2;
    enemy_spawn(&G, o);
    CHECK((o->col >> 8) != 0xFF && o->col == 57 && o->row == 8, "the upper half spawned at (%u,%u)", o->col, o->row);
    CHECK((o[1].col >> 8) != 0xFF && o[1].row == 10 && o[1].type == 1,
          "the lower half spawned 2 rows below with class+1 (row %u type %02x)", o[1].row, o[1].type);
    CHECK(o[1].rcol == o->rcol, "both halves share the ring column");
    int p = game_ring_index(&G, o->row, o->rcol);
    CHECK(G.ring[p] == 0x80 && G.ring[game_ring_add(p, 2 * RING_W)] == 0x81, "both markers are in the ring");
}

/* The HUD strip (render.c): the four [2004] gauge troughs, the [200E] labels
 * and the [2010] place name.  `make verify` compares all of it against the
 * DOSBox captures pixel for pixel; these checks pin the geometry so a
 * regression shows up without Pillow. */
static void t_hud(void)
{
    static uint8_t fb[FB_W * FB_H];
    TextFont tf;
    memset(&G, 0, sizeof G);
    G.max_hp = G.hp = 0x50; G.gold = 12345; G.almas = 7;
    int have_font = text_load_font(&tf, G_DIR) == 0;
    CHECK(have_font, "font.grp loaded (the [F504] narrow glyphs)");
    memset(fb, 0xEE, sizeof fb);
    render_hud(fb, &G, NULL, have_font ? &tf : NULL, NULL);
    /* the strip starts at y 158 and nothing above it is touched */
    CHECK(fb[157 * FB_W + 100] == 0xEE, "render_hud leaves the playfield alone");
    /* the four troughs: row 0 black, rows 1..8 dark blue 0x05, row 9 the
     * bright blue lip 0x2D, starting one column right of 48 + bh */
    static const struct { int bh, bl, w; const char *what; } BAR[4] = {
        {0x02, 0x04, 0x21, "LIFE"}, {0x02, 0x10, 0x88, "PLACE"},
        {0x02, 0x1C, 0x42, "GOLD"}, {0x48, 0x1C, 0x42, "ALMAS"},
    };
    for (int i = 0; i < 4; i++) {
        int x = 48 + BAR[i].bh + 1, y = 158 + BAR[i].bl;
        CHECK(fb[y * FB_W + x] == 0x00, "%s trough row 0 is black", BAR[i].what);
        CHECK(fb[(y + 4) * FB_W + x + BAR[i].w - 1] == 0x05, "%s trough is 0x05 to its last column", BAR[i].what);
        CHECK(fb[(y + 9) * FB_W + x] == 0x2D, "%s trough has the bright blue lip", BAR[i].what);
        CHECK(fb[(y + 4) * FB_W + x - 1] != 0x05, "%s trough starts at 48 + bh + 1 = %d", BAR[i].what, x);
    }
    if (have_font) {
        /* [200E] draws green (PC-88 3 = 0x1B) with a red shadow (2 = 0x12);
         * the labels sit inside their troughs */
        int green = 0, red = 0;
        for (int y = 163; y < 171; y++)
            for (int x = 51; x < 84; x++) {
                if (fb[y * FB_W + x] == 0x1B) green++;
                if (fb[y * FB_W + x] == 0x12) red++;
            }
        CHECK(green > 20 && red > 20, "\"LIFE\" is drawn green-on-red (%d/%d px)", green, red);
        /* [2010] the place name is white on blue; with no record there is none */
        int white = 0;
        for (int y = 175; y < 183; y++)
            for (int x = 88; x < 187; x++) if (fb[y * FB_W + x] == 0x09) white++;
        CHECK(white == 0, "no place record -> no place name");
        static const uint8_t rec[] = {0x16, 0xAF, 0x00, 4, 'T','E','S','T'};
        render_hud(fb, &G, NULL, &tf, rec);
        white = 0;
        for (int y = 175; y < 183; y++)
            for (int x = 88; x < 187; x++) if (fb[y * FB_W + x] == 0x09) white++;
        CHECK(white > 20, "a place record draws white glyphs at x4*4 (%d px)", white);
    }
    /* in a boss room the GOLD line becomes the boss's own name record */
    G.boss_map = 0xFF; G.boss.active = 1; G.boss.hp0 = G.boss.hp = 100;
    snprintf(G.boss.name, sizeof G.boss.name, "Cangrejo");
    G.boss.name_x4 = 0x10; G.boss.name_y = 0xBB; G.boss.name_xoff = 0;
    memset(fb, 0, sizeof fb);
    render_hud(fb, &G, NULL, have_font ? &tf : NULL, NULL);
    if (have_font) {
        int white = 0;
        for (int y = 187; y < 195; y++)
            for (int x = 64; x < 117; x++) if (fb[y * FB_W + x] == 0x09) white++;
        CHECK(white > 20, "the boss name is drawn in the GOLD box (%d px)", white);
    }
    if (have_font) { /* nothing to free: TextFont is a plain struct */ }
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../zeliard";
    G_DIR = dir;
    const char *shot = argc > 2 ? argv[2] : "/tmp/zel_frog.png";
    test_tiles();
    if (ai_load(&ovl, dir, 0)) {
        fprintf(stderr, "  (EAI1 overlay not available in %s: skipping the combat checks)\n", dir);
        return 0;
    }
    struct { const char *name; void (*fn)(void); } tests[] = {
        {"eai1 tables", t_tables}, {"damage", t_damage}, {"contact", t_contact},
        {"sword", t_sword}, {"drops", t_drop}, {"items", t_items}, {"bat wake", t_bat_wake}, {"frog hop", t_frog_hop},
        {"shots", t_shots}, {"magic", t_magic}, {"orbs", t_orbs}, {"sound", t_sound},
        {"eai tables", t_overlays}, {"eai run", t_overlay_run}, {"tall spawn", t_tall_spawn},
        {"hud", t_hud},
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
