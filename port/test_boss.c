/* test_boss.c — the boss protocol and the ported boss overlays.
 *
 * Every constant checked here comes from docs/ENEMIES.md §3 (the table of the
 * eleven bosses) and from the `[A002]` info block inside the overlay image
 * itself, so the two have to agree: HP, EXP, gold, the camera column, the
 * knock-left flag, the start cell and the name record.  On top of that the
 * protocol is exercised end to end on the real mp1d: the crab's pose matrix,
 * the x4 / x8 damage rule, the 40-frame death, the EXP/gold award and
 * post_boss_transition's pokes (the exit door and the reward item). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "map.h"
#include "gfx.h"
#include "physics.h"
#include "render.h"
#include "enemy.h"
#include "boss.h"

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; fprintf(stderr, "  FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

static const char *G_DIR = "../zeliard";
static Map bmap;
static Tileset btiles;
static AiOverlay ovl;
static Game G;
static void present(Game *g) { (void)g; }

/* ------------------------------------------------------------- info blocks */
/* docs/ENEMIES.md §3: overlay, map, name, start (col,row), HP, EXP, gold,
 * camera column, knock-left. */
static const struct {
    int idx; const char *name; int col, row; unsigned hp, exp, gold; int cam, knock; int contact0;
} BOSS_TABLE[] = {
    { BOSS_CRAB, "Cangrejo", 0x2B, 0x0C, 150,   120,  150, 12, 0,   6 },
    { BOSS_TAKO, "Pulpo",    0x24, 0x10, 250,   200,  200,  7, 1,  10 },
    { BOSS_TORI, "Pollo",    0x2E, 0x12, 500,   500,  500,  8, 1,  56 },
    { BOSS_ZELA, "Agar",     0x30, 0x0C, 500,  1000,  600, 12, 0,  30 },
    { BOSS_MEDA, "Vista",    0x30, 0x0B, 700,  3000,  800, 12, 0,  30 },
    { BOSS_LEGA, "Tarso",    0x26, 0x07, 640,  6000, 1500,  8, 1, 160 },
    { BOSS_DRGN, "Dragon",   0x1E, 0x08, 800, 12000, 2500,  5, 0,  40 },
    { BOSS_AKMA, "Alguien",  0x2A, 0x00, 800, 30000, 3800, 12, 0,  40 },
    { BOSS_MAO1, "Jashiin",  0x10, 0x01, 250,   200,    0,  5, 1,   0 },
    { BOSS_MAO2, "Jashiin",  0x30, 0x09, 800, 10000,    0, 12, 0,  80 },
    { BOSS_ZEL2, "Paguro",   0x30, 0x0C, 600,  3000, 1600, 12, 0,  30 },
};

static void t_info(void)
{
    for (unsigned i = 0; i < sizeof BOSS_TABLE / sizeof BOSS_TABLE[0]; i++) {
        AiOverlay o;
        if (ai_load(&o, G_DIR, BOSS_TABLE[i].idx)) { CHECK(0, "cannot load boss overlay %d", BOSS_TABLE[i].idx); continue; }
        memset(&G, 0, sizeof G);
        G.ai = &o;
        G.boss.info = boss_img16(&G, 0xA002);
        CHECK(G.boss.info != 0, "%s has an [A002] block", BOSS_TABLE[i].name);
        CHECK(boss_info_u16(&G, 3) == BOSS_TABLE[i].hp, "%s HP = %u (want %u)",
              BOSS_TABLE[i].name, boss_info_u16(&G, 3), BOSS_TABLE[i].hp);
        CHECK(boss_info_u16(&G, 5) == BOSS_TABLE[i].exp, "%s EXP = %u (want %u)",
              BOSS_TABLE[i].name, boss_info_u16(&G, 5), BOSS_TABLE[i].exp);
        CHECK(boss_info_u16(&G, 0xB) == BOSS_TABLE[i].gold, "%s gold = %u (want %u)",
              BOSS_TABLE[i].name, boss_info_u16(&G, 0xB), BOSS_TABLE[i].gold);
        CHECK(boss_info_u8(&G, 7) == BOSS_TABLE[i].cam, "%s camera column = %u (want %d)",
              BOSS_TABLE[i].name, boss_info_u8(&G, 7), BOSS_TABLE[i].cam);
        CHECK((boss_info_u8(&G, 8) != 0) == BOSS_TABLE[i].knock, "%s knock-left = %u (want %d)",
              BOSS_TABLE[i].name, boss_info_u8(&G, 8), BOSS_TABLE[i].knock);
        CHECK(boss_info_u16(&G, 0) == (unsigned)BOSS_TABLE[i].col && boss_info_u8(&G, 2) == BOSS_TABLE[i].row,
              "%s starts at (%02X,%02X) (want %02X,%02X)", BOSS_TABLE[i].name,
              boss_info_u16(&G, 0), boss_info_u8(&G, 2), BOSS_TABLE[i].col, BOSS_TABLE[i].row);
        CHECK(o.contact[0] == BOSS_TABLE[i].contact0, "%s A010[0] = %u (want %d)",
              BOSS_TABLE[i].name, o.contact[0], BOSS_TABLE[i].contact0);
        /* The name record handed to video [2010] is a positioned label
         * {u8 x4, u8 y, u8 xoff_px, u8 len, chars} (docs/VIDEO_DRIVERS.md
         * §1.1), NOT {u8 x, u16 y, u8 len, chars} as docs/ENEMIES.md §1 and
         * src/ai/ai_common.h say: MEDA/LEGA/AKMA/MAO1/MAO2 have 2 in the
         * third byte, which would make y = 0x02BB. */
        unsigned np = boss_info_u16(&G, 9);
        unsigned len = boss_img8(&G, np + 3);
        char nm[24]; memset(nm, 0, sizeof nm);
        for (unsigned k = 0; k < len && k < sizeof nm - 1; k++) nm[k] = (char)boss_img8(&G, np + 4 + k);
        CHECK(strcmp(nm, BOSS_TABLE[i].name) == 0, "name record = \"%s\" (want \"%s\")", nm, BOSS_TABLE[i].name);
        CHECK(boss_img8(&G, np + 1) == 0xBB, "%s name label y = %u (want 0xBB)", nm, boss_img8(&G, np + 1));
        CHECK(boss_img8(&G, np + 2) <= 4, "%s name label x offset = %u px", nm, boss_img8(&G, np + 2));
        CHECK(len == strlen(BOSS_TABLE[i].name), "%s name length byte = %u", nm, len);
        ai_unload(&o);
    }
    /* the 9CBC request table interleaves cavern and boss overlays */
    CHECK(boss_overlay_p(0) == 0 && boss_overlay_p(1) == 1 && boss_overlay_p(2) == 0 && boss_overlay_p(3) == 1,
          "9CBC: index 0/2 are EAI overlays, 1/3 are bosses");
    CHECK(boss_overlay_p(16) && boss_overlay_p(17) && boss_overlay_p(18), "16/17/18 are MAO1/MAO2/ZEL2");
}

/* ------------------------------------------------------------ the real mp1d */
static int load_boss_map(void)
{
    if (map_load_system(&bmap, G_DIR, 1)) return -1;                /* system map 1 = MP1D */
    if (gfx_load_tileset(&btiles, G_DIR, bmap.tileset)) return -1;
    if (ai_load(&ovl, G_DIR, bmap.ai)) return -1;
    return 0;
}

static void start_boss(void)
{
    game_init(&G, &bmap, &btiles);
    G.present = present; G.ai = &ovl;
    game_place(&G, 0x2B - 6, 0x0C + 3, 0);          /* the hero a few cells left of the crab */
    G.boss_map = (uint8_t)((bmap.lvl_flags & 0x80) ? 0xFF : 0);
    G.boss_room = (uint8_t)((bmap.lvl_flags & 0x40) ? 0xFF : 0);
    boss_init(&G);
    G.encounter_frames = 0;
    enemies_load(&G);
}

static void t_map(void)
{
    CHECK(bmap.cavern == 1 && bmap.width == 73, "mp1d: cavern %u, %d columns (want 1, 73)", bmap.cavern, bmap.width);
    CHECK(bmap.row_bias == 12, "mp1d row bias %u (want 12: a boss room)", bmap.row_bias);
    CHECK((bmap.lvl_flags & 0x80) != 0, "mp1d level flags %02X: bit 7 -> FF34 boss_map", bmap.lvl_flags);
    CHECK((bmap.lvl_flags & 0x40) == 0, "mp1d is not a [E6] boss_room");
    CHECK(bmap.ai == BOSS_CRAB, "mp1d AI index %u (want %d = CRAB)", bmap.ai, BOSS_CRAB);
    CHECK(bmap.enemies == 0xFF && bmap.boss_bank == 1,
          "mp1d enemy bank +4 = %02X (keep), +5 = %u (CRAB.GRP)", bmap.enemies, bmap.boss_bank);
    CHECK(bmap.post_ai == 0 && bmap.post_enemies == 0, "post-boss banks +6/+7 = EAI1/ENP1 (%u/%u)",
          bmap.post_ai, bmap.post_enemies);
    CHECK(bmap.nobj == 0, "the boss map ships an EMPTY C010 list (%d records): the overlay builds it", bmap.nobj);
    CHECK(bmap.ndoors == 0, "the exit door only exists after the pokes (%d doors)", bmap.ndoors);
}

static void t_init(void)
{
    start_boss();
    CHECK(G.boss.active, "boss_init accepted the CRAB overlay");
    CHECK(G.boss.hp == 150 && G.boss.hp0 == 150, "boss HP %u/%u (want 150)", G.boss.hp, G.boss.hp0);
    CHECK(G.boss.col == 0x2B && G.boss.row == 0x0C, "boss at (%02X,%02X) (want 2B,0C)", G.boss.col, G.boss.row);
    CHECK(strcmp(G.boss.name, "Cangrejo") == 0, "boss name \"%s\"", G.boss.name);
    CHECK(G.boss.cam_col == 12, "camera column %u", G.boss.cam_col);
    CHECK(G.boss_knock_left == 0, "9F01 clear for the crab");
    CHECK(G.boss_state == 0xFF, "EDA0 = FF at level start (6058)");
    start_boss();
    G.encounter_frames = 12;
    int n = 0;
    while (G.encounter_frames && n < 40) { game_step(&G); n++; }
    CHECK(n == 12, "the encounter card runs 12 half-flashes (60E6: 6 x 2 waits), got %d", n);
}

/* the pose matrices at [A70A]: poses 0..8 share one 6x10 matrix, pose 9 has
 * its own; 12 parts each */
static int matrix_parts(unsigned m)
{
    int n = 0;
    for (int i = 0; i < 60; i++) if (boss_img8(&G, m + (unsigned)i) != 0xFF) n++;
    return n;
}

static void t_parts(void)
{
    start_boss();
    unsigned m0 = boss_img16(&G, 0xA70A), m9 = boss_img16(&G, 0xA70A + 18);
    CHECK(m0 == 0xA71E && m9 == 0xA75A, "pose matrices at %04X / %04X (want A71E / A75A)", m0, m9);
    for (int p = 0; p <= 8; p++)
        CHECK(boss_img16(&G, 0xA70A + 2u * (unsigned)p) == m0, "pose %d shares the walk matrix", p);
    CHECK(matrix_parts(m0) == 12, "the walk matrix has %d parts (want 12)", matrix_parts(m0));
    CHECK(matrix_parts(m9) == 10, "the jump matrix has %d parts (want 10)", matrix_parts(m9));
    /* the three weak points 0x10..0x12 sit on row 4 of the walk matrix */
    CHECK(boss_img8(&G, m0 + 42) == 0x10 && boss_img8(&G, m0 + 44) == 0x11 && boss_img8(&G, m0 + 46) == 0x12,
          "row 4 = 07 10 11 12 08 (the weak points)");
    CHECK(boss_img8(&G, m9 + 32) == 0x90, "the jump matrix's 0x90 part is solid (0x80|0x10)");

    G.boss.pose = 0;
    boss_update(&G);
    CHECK(G.nobj == 12, "one frame places %d part records (want 12)", G.nobj);
    int marked = 0, weak = 0;
    for (int i = 0; i < G.nobj; i++) {
        if (G.ring[game_ring_index(&G, G.obj[i].row, G.obj[i].rcol)] == (0x80 | i)) marked++;
        if (G.obj[i].type & 0x10) weak++;
        CHECK(G.ai->contact[G.obj[i].type & 0xF] == 6, "every crab part does 6 contact damage");
    }
    CHECK(marked == G.nobj, "%d/%d part markers are in the ring", marked, G.nobj);
    CHECK(weak == 3, "%d weak-point parts on the ground pose (want 3)", weak);
}

/* find a placed part, weak or not */
static int find_part(int want_weak)
{
    for (int i = 0; i < G.nobj; i++) if (((G.obj[i].type & 0x10) != 0) == want_weak) return i;
    return -1;
}

static void t_damage(void)
{
    start_boss();
    boss_update(&G);
    unsigned base = damage_for_source(&G, 1);
    CHECK(base == 1, "training sword damage = %u (sword_base[0] + level/2)", base);

    int i = find_part(0);
    CHECK(i >= 0, "a normal part exists");
    G.obj[i].hit = 0x41;                                            /* pending | source 1 */
    unsigned before = G.boss.hp;
    boss_update(&G);
    CHECK(G.boss.hp == before - base * 4, "a sword hit on a normal part costs %u HP (x4), got %u",
          base * 4, before - G.boss.hp);
    CHECK(G.sfx_request == 0x22 || sound_count(0x22) > 0, "hit sound 0x22");

    i = find_part(1);
    CHECK(i >= 0, "a weak-point part exists");
    G.obj[i].hit = 0x41;
    before = G.boss.hp;
    boss_update(&G);
    CHECK(G.boss.hp == before - base * 8, "a hit on a weak point costs %u HP (x8), got %u",
          base * 8, before - G.boss.hp);

    /* magic 7 (source 8) is 255 per hit -> x4 = capped at the remaining HP */
    G.magic_sel = 7;
    i = find_part(0);
    G.obj[i].hit = (uint8_t)(0x40 | 8);
    boss_update(&G);
    CHECK(G.boss.hp == 0, "magic 7 (255 x4) empties the 150-HP bar in one hit");
    CHECK(G.boss_cutscene == 0xFF, "HP 0 starts the death cutscene (FF2E)");
}

static void t_death(void)
{
    start_boss();
    boss_update(&G);
    G.boss.hp = 1;
    int i = find_part(0);
    G.obj[i].hit = 0x41;
    boss_update(&G);
    CHECK(G.boss.hp == 0 && G.boss_cutscene, "the last hit starts the cutscene");
    int n = 1, dying = G.boss_dying ? 1 : 0;
    while (!G.boss_defeated && n < 60) { G.boss_dying = 0; boss_update(&G); if (G.boss_dying) dying++; n++; }
    CHECK(dying == 0x28, "the death animation is %d frames (want 40)", dying);
    CHECK(G.nobj == 0, "the last frame removes every part");

    G.exp = 0; G.gold = 0;
    boss_rewards(&G);
    CHECK(G.exp == 120, "EXP awarded = %u (want [A002]+5 = 120)", G.exp);
    CHECK(G.gold == 150, "gold awarded = %u (want [A002]+B = 150)", (unsigned)G.gold);
    CHECK(G.post_boss_pending == 0xFF, "9F1E set: the next frame runs 72F1");
    /* 71DA gates on EDA0; 71C2 runs 72F1 before 71CC can pay a second time */
    G.boss_state = 0;
    boss_rewards(&G);
    CHECK(G.exp == 120 && G.gold == 150, "with EDA0 clear the reward is not paid again");
}

/* 72F1: the level record's pokes give mp1d its exit door and its reward item */
static void t_post_boss(void)
{
    start_boss();
    CHECK(G.map->ndoors == 0 && G.map->nobj == 0, "before: no door, no object");
    G.boss_defeated = 0xFF;
    G.post_boss_pending = 0xFF;
    G.scroll_col = 20; G.hero_scr_col = 12;
    int ok = post_boss_transition(&G);
    CHECK(ok, "post_boss_transition ran");
    CHECK(G.boss_map == 0, "FF34 cleared (734C)");
    CHECK(bmap.ndoors == 1, "the C00A poke installed %d door (want 1)", bmap.ndoors);
    if (bmap.ndoors == 1) {
        const Door *d = &bmap.doors[0];
        CHECK(d->col == 32, "the exit door sits at the hero's column %u (want 20+12)", d->col);
        CHECK(d->dest_map == 0 && d->dest_col == 0x8D && d->dest_row == 0x20,
              "it leads back to MP10 at (%u,%u)", d->dest_col, d->dest_row);
    }
    CHECK(bmap.nobj == 1, "the C010 poke installed %d object (want 1: the reward)", bmap.nobj);
    if (bmap.nobj == 1) {
        CHECK(bmap.objs[0].type == 0x76, "the reward is item state %u (type %02X)",
              (bmap.objs[0].type & 0x1F) - 0x10, bmap.objs[0].type);
        CHECK(bmap.objs[0].row == 0x05, "the C226 poke moved it to row %u (want 5)", bmap.objs[0].row);
        CHECK((bmap.objs[0].flags & 0x20) != 0, "it is an event object (flags %02X)", bmap.objs[0].flags);
    }
    CHECK(G.page[0] == 0xFF && G.page[1] == 0xFF, "the [0000] poke set the story flag pair");
    /* re-load so a later run sees the pristine map */
    map_free(&bmap);
    load_boss_map();
}

/* -------------------------------------------------- TAKO / TORI / ZELA runs */
static void run_overlay(int ai_index, int sysmap, const char *name)
{
    Map m; Tileset t; AiOverlay o;
    memset(&m, 0, sizeof m); memset(&t, 0, sizeof t);
    if (map_load_system(&m, G_DIR, sysmap)) { CHECK(0, "%s: cannot load system map %d", name, sysmap); return; }
    CHECK(m.ai == ai_index, "%s map: AI index %u (want %d)", name, m.ai, ai_index);
    if (gfx_load_tileset(&t, G_DIR, m.tileset) || ai_load(&o, G_DIR, m.ai)) {
        CHECK(0, "%s: cannot load the tileset / overlay", name); map_free(&m); return;
    }
    game_init(&G, &m, &t);
    G.present = present; G.ai = &o;
    G.boss_map = (uint8_t)((m.lvl_flags & 0x80) ? 0xFF : 0);
    G.boss_room = (uint8_t)((m.lvl_flags & 0x40) ? 0xFF : 0);
    boss_init(&G);
    G.encounter_frames = 0;
    game_place(&G, (int)G.boss.start_col - 8, G.boss.start_row + 3, 0);
    enemies_load(&G);
    int drew = 0, maxparts = 0, bad_rcol = 0, bad_marker = 0;
    for (int f = 0; f < 200; f++) {
        boss_update(&G);
        if (G.nobj) drew++;
        if (G.nobj > maxparts) maxparts = G.nobj;
        for (int i = 0; i < G.nobj; i++) {
            if (G.obj[i].rcol >= RING_W) bad_rcol++;
            if (G.ring[game_ring_index(&G, G.obj[i].row, G.obj[i].rcol)] != (0x80 | i)) bad_marker++;
        }
    }
    CHECK(bad_rcol == 0, "%s: %d parts landed outside the ring", name, bad_rcol);
    CHECK(bad_marker == 0, "%s: %d part markers missing from the ring", name, bad_marker);
    CHECK(drew > 50, "%s drew parts on %d/200 frames", name, drew);
    CHECK(maxparts >= 8, "%s composes at least 8 parts (max %d)", name, maxparts);
    CHECK(G.boss.hp == G.boss.hp0, "%s took no damage while idle", name);
    /* one magic-7 hit (source 8 = 255) costs HP under every boss's own rule.
     * MAO1 is the exception: its script reads no hit bits at all. */
    if (G.nobj && ai_index != BOSS_MAO1) {
        unsigned before = G.boss.hp;
        G.obj[0].hit = 0x48;
        boss_update(&G);
        CHECK(G.boss.hp < before, "%s: a magic hit costs HP (%u -> %u)", name, before, G.boss.hp);
        /* magic 7 alone can empty the smaller bars: put the boss back on its feet */
        G.boss.hp = G.boss.hp0; G.boss_cutscene = 0; G.boss_dying = 0; G.boss.death_cnt = 0;
    }
    /* kill it and check the 40-frame death and the reward */
    boss_damage(&G, 0xFFFF);
    CHECK(G.boss.hp == 0 && G.boss_cutscene == 0xFF, "%s: HP 0 -> cutscene", name);
    int n = 0, dying = 0;
    while (!G.boss_defeated && n < 80) { G.boss_dying = 0; boss_update(&G); if (G.boss_dying) dying++; n++; }
    CHECK(dying == 0x28, "%s death animation is %d frames (want 40)", name, dying);
    G.exp = 0; G.gold = 0; G.boss_state = 0xFF;
    boss_rewards(&G);
    if (G.boss_map)
        CHECK(G.exp == G.boss.exp && G.gold == G.boss.gold, "%s pays %u EXP / %u gold", name, G.exp, (unsigned)G.gold);
    else                                        /* 71CC gates on FF34: an [E6] boss room pays nothing */
        CHECK(G.exp == 0 && G.gold == 0, "%s is an [E6] boss room: no reward (docs/ENEMIES.md MAO1)", name);
    ai_unload(&o);
    map_free(&m);
}

static void t_tako(void) { run_overlay(BOSS_TAKO, 4, "TAKO");  }   /* MP2D */
static void t_tori(void) { run_overlay(BOSS_TORI, 7, "TORI");  }   /* MP3D */
static void t_zela(void) { run_overlay(BOSS_ZELA, 10, "ZELA"); }   /* MP4D */
static void t_zel2(void) { run_overlay(BOSS_ZEL2, 21, "ZEL2"); }   /* MP73 */
static void t_meda(void) { run_overlay(BOSS_MEDA, 13, "MEDA"); }   /* MP5D */
static void t_lega(void) { run_overlay(BOSS_LEGA, 17, "LEGA"); }   /* MP6D */

/* The layer tables of the two composed bosses: in both overlays the byte list
 * and the column bitmap are told apart by the popcount matching the run
 * length, which is also how src/ai/boss_tori.c's swapped labels were caught. */
static void t_layers(void)
{
    AiOverlay o;
    if (ai_load(&o, G_DIR, BOSS_MEDA) == 0) {
        memset(&G, 0, sizeof G); G.ai = &o;
        int total = 0;
        static const struct { unsigned bm; int rows; } L[3] = {{0xA606, 13}, {0xA623, 11}, {0xA682, 5}};
        static const int want[3] = {21, 8, 6};
        for (int i = 0; i < 3; i++) {
            int n = 0;
            for (int r = 0; r < L[i].rows; r++) n += __builtin_popcount(boss_img8(&G, L[i].bm + (unsigned)r));
            CHECK(n == want[i], "MEDA layer %d has %d parts (want %d)", i, n, want[i]);
            total += n;
        }
        CHECK(total == 35, "MEDA's body + right side + tentacles = %d parts", total);
        /* the drip template A6E0.  docs/ENEMIES.md §3 and src/ai/boss_meda.c
         * call the cell 0x32; that is the *life* byte — the cell is 0x30. */
        CHECK(boss_img8(&G, 0xA6E2) == 0x30 && boss_img8(&G, 0xA6E4) == 50 &&
              boss_img8(&G, 0xA6E5) == 6 && boss_img8(&G, 0xA6E6) == 80,
              "MEDA's drip {cell %02X, life %u, dir %u, damage %u} (want 30, 50, 6, 80)", boss_img8(&G, 0xA6E2),
              boss_img8(&G, 0xA6E4), boss_img8(&G, 0xA6E5), boss_img8(&G, 0xA6E6));
        ai_unload(&o);
    } else CHECK(0, "MEDA overlay");
    /* ZELA's two bolts: cells 0x15 (left) and 0x14 (right), damage 80 — the
     * decompilation calls the right one 0x12 */
    if (ai_load(&o, G_DIR, BOSS_ZELA) == 0) {
        memset(&G, 0, sizeof G); G.ai = &o;
        CHECK(boss_img8(&G, 0xA554) == 0x15 && boss_img8(&G, 0xA557) == 4 && boss_img8(&G, 0xA558) == 80,
              "ZELA left bolt {cell %02X, dir %u, damage %u}", boss_img8(&G, 0xA554),
              boss_img8(&G, 0xA557), boss_img8(&G, 0xA558));
        CHECK(boss_img8(&G, 0xA561) == 0x14 && boss_img8(&G, 0xA564) == 0 && boss_img8(&G, 0xA565) == 80,
              "ZELA right bolt {cell %02X, dir %u, damage %u} (0x14, not the documented 0x12)",
              boss_img8(&G, 0xA561), boss_img8(&G, 0xA564), boss_img8(&G, 0xA565));
        ai_unload(&o);
    }
    if (ai_load(&o, G_DIR, BOSS_ZEL2) == 0) {
        memset(&G, 0, sizeof G); G.ai = &o;
        CHECK(boss_img8(&G, 0xA545) == 0x05 && boss_img8(&G, 0xA549) == 120 &&
              boss_img8(&G, 0xA552) == 0x04 && boss_img8(&G, 0xA556) == 120,
              "ZEL2's bolts are cells 05/04 at damage 120");
        ai_unload(&o);
    }
    if (ai_load(&o, G_DIR, BOSS_LEGA) == 0) {
        memset(&G, 0, sizeof G); G.ai = &o;
        static const int want[9] = {7, 9, 10, 11, 11, 12, 10, 12, 11};
        int bad = 0;
        for (int p = 0; p < 8; p++) {
            unsigned bm = boss_img16(&G, 0xA744 + 2u * (unsigned)p);
            int n = 0;
            for (int r = 0; r < 8; r++) n += __builtin_popcount(boss_img8(&G, bm + (unsigned)r));
            unsigned l0 = boss_img16(&G, 0xA6C8 + 2u * (unsigned)p);
            unsigned l1 = boss_img16(&G, 0xA6C8 + 2u * (unsigned)p + 2);
            if (n != want[p] || (int)(l1 - l0) != want[p]) bad++;
        }
        CHECK(bad == 0, "LEGA: %d of 8 poses disagree with their byte-list length", bad);
        /* the 17-step projectile path A5D8 starts {-1,0} {-1,0} {-1,1} */
        CHECK((int8_t)boss_img8(&G, 0xA5D8) == -1 && (int8_t)boss_img8(&G, 0xA5D9) == 0 &&
              (int8_t)boss_img8(&G, 0xA5DC) == -1 && (int8_t)boss_img8(&G, 0xA5DD) == 1,
              "LEGA's projectile path (A5D8)");
        ai_unload(&o);
    } else CHECK(0, "LEGA overlay");
}

/* the six overlays that still run on the generic protocol placeholder */
static void t_generic(void)
{
    static const struct { int ai, map; const char *name; } G6[] = {
        {BOSS_DRGN, 22, "DRGN"}, {BOSS_AKMA, 28, "AKMA"},
        {BOSS_MAO1, 29, "MAO1"}, {BOSS_MAO2, 30, "MAO2"},
    };
    for (unsigned i = 0; i < sizeof G6 / sizeof G6[0]; i++) run_overlay(G6[i].ai, G6[i].map, G6[i].name);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../zeliard";
    G_DIR = dir;
    if (load_boss_map()) {
        fprintf(stderr, "  (mp1d / CRAB not available in %s: skipping the boss checks)\n", dir);
        return 0;
    }
    struct { const char *name; void (*fn)(void); } tests[] = {
        {"info blocks", t_info}, {"mp1d", t_map}, {"init", t_init}, {"crab parts", t_parts},
        {"crab damage", t_damage}, {"crab death", t_death}, {"post-boss", t_post_boss},
        {"tako", t_tako}, {"tori", t_tori}, {"zela", t_zela}, {"zel2", t_zel2},
        {"meda", t_meda}, {"lega", t_lega}, {"layer tables", t_layers},
        {"generic", t_generic},
    };
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; i++) {
        int before = fails;
        tests[i].fn();
        fprintf(stderr, "%-14s %s\n", tests[i].name, fails == before ? "ok" : "FAILED");
    }
    fprintf(stderr, "%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
