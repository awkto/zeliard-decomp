/* test_boss.c — the boss protocol and the ported boss overlays.
 *
 * Every constant checked here comes from docs/ENEMIES.md §3 (the table of the
 * eleven bosses) and from the `[A002]` info block inside the overlay image
 * itself, so the two have to agree: HP, EXP, almas, the camera column, the
 * knock-left flag, the start cell and the name record.  On top of that the
 * protocol is exercised end to end on the real mp1d: the crab's pose matrix,
 * the x4 / x8 damage rule, the 40-frame death, the EXP/almas award and
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
#include "shell.h"

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; fprintf(stderr, "  FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

static const char *G_DIR = "../zeliard";
static Map bmap;
static Tileset btiles;
static AiOverlay ovl;
static Game G;
static void present(Game *g) { (void)g; }

/* ------------------------------------------------------------- info blocks */
/* docs/ENEMIES.md §3: overlay, map, name, start (col,row), HP, EXP, almas ([A002]+B
 * feeds 917C, the almas adder),
 * camera column, knock-left. */
static const struct {
    int idx; const char *name; int col, row; unsigned hp, exp, almas; int cam, knock; int contact0;
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
        CHECK(boss_info_u16(&G, 0xB) == BOSS_TABLE[i].almas, "%s almas = %u (want %u)",
              BOSS_TABLE[i].name, boss_info_u16(&G, 0xB), BOSS_TABLE[i].almas);
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

/* CRAB A6BC and its seven cousins: a hit read back this frame makes every
 * record the overlay emits carry `hit` bit 5, and fight.bin's `sword_apply`
 * (6F8B) refuses a marker whose object already has it — so the blade lands on
 * a boss at most every other frame.  ZELA, ZEL2 and MAO1 have no such write
 * in their images and must not set it. */
static void t_hit_flash(void)
{
    start_boss();
    boss_update(&G);
    int i = find_part(0);
    CHECK(i >= 0, "hit flash: a part to strike");
    CHECK((G.obj[i].hit & 0x20) == 0, "hit flash: an unstruck boss emits hit = 0");
    G.obj[i].hit = 0x41;                                            /* pending | source 1 */
    boss_update(&G);
    int flashing = 0, total = G.nobj;
    for (int k = 0; k < G.nobj; k++) if (G.obj[k].hit & 0x20) flashing++;
    CHECK(total > 0 && flashing == total,
          "hit flash: all %d parts carry hit bit 5 the frame after a hit (%d did)", total, flashing);
    /* ... and the sword cannot land on them while they do (6F8B) */
    unsigned hp = G.boss.hp;
    G.attacking = 0xFF; G.attack_type = 0; G.attack_var = 0;
    sword_apply(&G);
    int pending = 0;
    for (int k = 0; k < G.nobj; k++) if (G.obj[k].hit & 0x40) pending++;
    CHECK(pending == 0, "hit flash: sword_apply skips a part that is already flashing");
    G.attacking = 0;
    boss_update(&G);
    CHECK(G.boss.hp == hp, "hit flash: so the boss takes no damage on that frame");
    flashing = 0;
    for (int k = 0; k < G.nobj; k++) if (G.obj[k].hit & 0x20) flashing++;
    CHECK(flashing == 0, "hit flash: the flag is cleared again the next frame");
    /* and a fresh pending hit does land */
    i = find_part(0);
    if (i >= 0) {
        hp = G.boss.hp;
        G.obj[i].hit = 0x41;
        boss_update(&G);
        CHECK(G.boss.hp < hp, "hit flash: the frame after that, the sword lands again");
    }
}

/* ZELA (MP4D) is one of the three overlays whose image has no `or [si+5],0x20` */
static void t_no_hit_flash(void)
{
    static Map m; static Tileset t; static AiOverlay o;
    memset(&m, 0, sizeof m); memset(&t, 0, sizeof t);
    if (map_load_system(&m, G_DIR, 10) || gfx_load_tileset(&t, G_DIR, m.tileset) || ai_load(&o, G_DIR, m.ai)) {
        CHECK(0, "ZELA: cannot load MP4D"); return;
    }
    game_init(&G, &m, &t);
    G.present = present; G.ai = &o;
    G.boss_map = (uint8_t)((m.lvl_flags & 0x80) ? 0xFF : 0);
    boss_init(&G);
    G.encounter_frames = 0;
    game_place(&G, (int)G.boss.start_col - 8, G.boss.start_row + 3, 0);
    enemies_load(&G);
    boss_update(&G);
    int z = -1;
    for (int k = 0; k < G.nobj; k++) if (!(G.obj[k].type & 0x20)) { z = k; break; }
    CHECK(z >= 0, "ZELA: a part to strike");
    if (z >= 0) {
        unsigned hp = G.boss.hp;
        G.obj[z].hit = 0x48;                    /* magic 7: every boss scales it to something */
        boss_update(&G);
        CHECK(G.boss.hp < hp, "ZELA: the hit still lands");
        int any = 0;
        for (int k = 0; k < G.nobj; k++) if (G.obj[k].hit & 0x20) any = 1;
        CHECK(!any, "ZELA's image has no `or [si+5],0x20`, so it never sets hit bit 5");
    }
    ai_unload(&o);
    map_free(&m);
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

    G.exp = 0; G.almas = 0; G.gold = 0;
    boss_rewards(&G);
    CHECK(G.exp == 120, "EXP awarded = %u (want [A002]+5 = 120)", G.exp);
    CHECK(G.almas == 150, "almas awarded = %u (want [A002]+B = 150)", (unsigned)G.almas);
    CHECK(G.gold == 0, "the boss award is almas, not gold (71F2 -> 917C)");
    CHECK(G.post_boss_pending == 0xFF, "9F1E set: the next frame runs 72F1");
    /* 71DA gates on EDA0; 71C2 runs 72F1 before 71CC can pay a second time */
    G.boss_state = 0;
    boss_rewards(&G);
    CHECK(G.exp == 120 && G.almas == 150, "with EDA0 clear the reward is not paid again");
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

/* What 72F1's third poke is for: the reward is meant to be collected on the
 * *next* visit, not on the way out.
 *
 * Every boss room's level record ends with a {u16 addr, u16 val} list (7351)
 * that installs a post-boss object list, a post-boss door list and -- for the
 * six rooms that carry a reward -- a write to the object's +2/+3, which puts
 * its row five to thirteen rows above the room's only floor and its rcol at
 * 0xFF.  The last entry sets a story-flag byte, and the room's own C00C list
 * (6BFC) keys off exactly that byte: the next time the map is loaded from disk
 * the same object list comes back with the row the .mdt image has -- two rows
 * above the floor, where the hero walks into it -- the level record loses its
 * bit 7 so the room is no longer a boss room, and the door list becomes the
 * ordinary two-door one.  A third record, keyed on the item's own flag, retires
 * the object once it has been taken.  So the room is entered twice: once for
 * the boss, once for the Key. */
static void t_boss_reward(void)
{
    static const struct { int map; const char *name; uint8_t flag; uint8_t item_flag, item_mask;
                          int parked, home, floor; int back_col, back_row; } R[] = {
        {1,  "MP1D", 0x00, 0x02, 0x08,  5, 16, 18,  26, 15},
        {4,  "MP2D", 0x08, 0x0B, 0x10, 13, 20, 22, 171, 54},
        {7,  "MP3D", 0x10, 0x13, 0x04, 13, 20, 25, 188, 20},
    };
    for (unsigned k = 0; k < sizeof R / sizeof R[0]; k++) {
        static Map m; static uint8_t page[256];
        memset(page, 0, sizeof page);
        memset(&m, 0, sizeof m);
        if (map_load_system(&m, G_DIR, R[k].map)) { CHECK(0, "%s: cannot load", R[k].name); continue; }
        CHECK(m.ndoors == 0 && m.nobj == 0 && (m.lvl_flags & 0x80),
              "%s fresh: a boss room with no doors and no objects", R[k].name);
        map_free(&m);
        /* the boss is dead: 72F1's last poke set the flag byte */
        memset(&m, 0, sizeof m);
        map_load_system(&m, G_DIR, R[k].map);
        page[R[k].flag] = 0xFF;
        map_apply_patches(&m, page);
        CHECK(!(m.lvl_flags & 0x80), "%s after the boss: no longer a boss room (flags %02X)",
              R[k].name, m.lvl_flags);
        CHECK(m.ndoors == 2, "%s after the boss: %d doors (want 2 - back the way you came, and the exit)",
              R[k].name, m.ndoors);
        if (m.ndoors == 2)
            CHECK(m.doors[0].dest_col == (uint16_t)R[k].back_col && m.doors[0].dest_row == R[k].back_row,
                  "%s door 0 leads back to (%u,%u)", R[k].name, m.doors[0].dest_col, m.doors[0].dest_row);
        CHECK(m.nobj == 1, "%s after the boss: %d objects (want 1 - the reward)", R[k].name, m.nobj);
        if (m.nobj == 1) {
            CHECK((m.objs[0].type & 0x1F) == 0x16, "%s reward is a Key (type %02X)", R[k].name, m.objs[0].type);
            CHECK(m.objs[0].row == (uint8_t)R[k].home,
                  "%s reward is back at its image row %u (want %d), not the %d 72F1 parks it at",
                  R[k].name, m.objs[0].row, R[k].home, R[k].parked);
            CHECK(m.grid[m.objs[0].col][R[k].floor] && R[k].home < R[k].floor &&
                  R[k].home > R[k].parked,
                  "%s row %d is below the row 72F1 parks it at (%d) and above the floor at %d",
                  R[k].name, R[k].home, R[k].parked, R[k].floor);
            CHECK(!m.grid[m.objs[0].col][R[k].parked],
                  "%s row %d, where 72F1 parks it, is empty air", R[k].name, R[k].parked);
            CHECK((m.objs[0].home_col & 0xFF) == R[k].item_flag && m.objs[0].home_row == R[k].item_mask,
                  "%s taking it sets page[%02X] |= %02X (914C)", R[k].name,
                  m.objs[0].home_col & 0xFF, m.objs[0].home_row);
        }
        map_free(&m);
        /* ... and once it has been taken it does not come back */
        memset(&m, 0, sizeof m);
        map_load_system(&m, G_DIR, R[k].map);
        page[R[k].item_flag] |= R[k].item_mask;
        map_apply_patches(&m, page);
        CHECK(m.nobj == 0 || (m.objs[0].col >> 8) == 0xFF,
              "%s with the Key taken: the reward is gone (%d objects, col %04X)", R[k].name,
              m.nobj, m.nobj ? m.objs[0].col : 0xFF00);
        map_free(&m);
    }
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
    /* MAO1's script has no death branch at all (A23C never tests [FF2E]) —
     * its overlay is a cutscene, so the fight below does not apply to it. */
    if (ai_index == BOSS_MAO1) {
        CHECK(!G.boss_cutscene && !G.boss_defeated, "MAO1 never enters a death cutscene");
        ai_unload(&o); map_free(&m);
        return;
    }
    /* kill it and check the 40-frame death and the reward */
    boss_damage(&G, 0xFFFF);
    CHECK(G.boss.hp == 0 && G.boss_cutscene == 0xFF, "%s: HP 0 -> cutscene", name);
    int n = 0, dying = 0;
    while (!G.boss_defeated && n < 80) { G.boss_dying = 0; boss_update(&G); if (G.boss_dying) dying++; n++; }
    CHECK(dying == 0x28, "%s death animation is %d frames (want 40)", name, dying);
    G.exp = 0; G.almas = 0; G.boss_state = 0xFF;
    boss_rewards(&G);
    if (G.boss_map)
        CHECK(G.exp == G.boss.exp && G.almas == G.boss.almas, "%s pays %u EXP / %u almas", name, G.exp, (unsigned)G.almas);
    else                                        /* 71CC gates on FF34: an [E6] boss room pays nothing */
        CHECK(G.exp == 0 && G.almas == 0, "%s is an [E6] boss room: no reward (docs/ENEMIES.md MAO1)", name);
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

/* ---- the four overlays ported last -------------------------------------- */
/* Every table below is read out of the shipped overlay image, so these checks
 * are against the original's own data, not against transcribed constants. */

static int pcount(unsigned bm, int n)
{
    int k = 0;
    for (int i = 0; i < n; i++) k += __builtin_popcount(boss_img8(&G, bm + (unsigned)i));
    return k;
}

/* boss_paste consumes one list byte per set bit, so a layer is only
 * self-consistent when the byte list is exactly as long as the bitmap's
 * popcount.  That is also how the layer/bitmap pointer pairs were told apart
 * in the first place (the same argument as t_layers). */
static void t_drgn(void)
{
    AiOverlay o;
    if (ai_load(&o, G_DIR, BOSS_DRGN)) { CHECK(0, "DRGN overlay"); return; }
    memset(&G, 0, sizeof G); G.ai = &o; G.boss.info = boss_img16(&G, 0xA002);
    CHECK(G.boss.info == 0xAA3C, "DRGN [A002] = AA3C, got %04X", G.boss.info);
    /* the pose layer: 12 bitmap bytes, 11 poses, 10-11 parts each */
    static const int want[11] = {11, 11, 11, 11, 10, 11, 10, 11, 11, 11, 11};
    for (int p = 0; p < 11; p++)
        CHECK(pcount(boss_img16(&G, 0xA810 + 2 * p), 12) == want[p],
              "DRGN pose %d has %d parts (want %d)", p, pcount(boss_img16(&G, 0xA810 + 2 * p), 12), want[p]);
    CHECK(pcount(boss_img16(&G, 0xA8FD), 11) == 15 && pcount(boss_img16(&G, 0xA8FF), 11) == 12,
          "DRGN body frames are 15 and 12 parts");
    CHECK(pcount(0xA8D7, 7) == 8, "DRGN's hind legs share one 8-part bitmap");
    CHECK(pcount(0xA87D, 4) == 3, "DRGN's tail is the 3 bytes at A87A");
    /* the head-hit reaction tables end with bit 7 set on their last entry */
    CHECK((boss_img8(&G, 0xA4B4 + 6) & 0x80) && (boss_img8(&G, 0xA4BB + 6) & 0x80),
          "both reaction tables end with the 0x80 flag");
    CHECK((boss_img8(&G, 0xA4B4) & 0x7F) == 0x0A, "the low reaction starts at pose 0x0A");
    ai_unload(&o);
}

static void t_akma(void)
{
    AiOverlay o;
    if (ai_load(&o, G_DIR, BOSS_AKMA)) { CHECK(0, "AKMA overlay"); return; }
    memset(&G, 0, sizeof G); G.ai = &o; G.boss.info = boss_img16(&G, 0xA002);
    CHECK(G.boss.info == 0xAA06, "AKMA [A002] = AA06, got %04X", G.boss.info);
    /* 13 columns x 2 bitmap bytes; the three wing frames have 25/16/18 parts,
     * and the mirrored (left) tables have exactly the same counts */
    static const int want[3] = {25, 16, 18};
    for (int a = 0; a < 3; a++) {
        CHECK(pcount(boss_img16(&G, 0xA876 + 2 * a), 26) == want[a],
              "AKMA wing %d (right) has %d parts (want %d)", a, pcount(boss_img16(&G, 0xA876 + 2 * a), 26), want[a]);
        CHECK(pcount(boss_img16(&G, 0xA870 + 2 * a), 26) == want[a],
              "AKMA wing %d (left) matches", a);
    }
    /* the flight path: high above the top row at the near end, row 1 at the far
     * end, and the two tables are each other's mirror image */
    for (int i = 0; i < 21; i++)
        CHECK(boss_img8(&G, 0xA954 + i) == boss_img8(&G, 0xA969 + 20 - i),
              "AKMA path[%d]: A954 and A969 mirror", i);
    CHECK(boss_img8(&G, 0xA954) == 60 && boss_img8(&G, 0xA954 + 20) == 1,
          "the right-bound path runs from row 60 down to row 1");
    ai_unload(&o);
}

static void t_mao1(void)
{
    AiOverlay o;
    if (ai_load(&o, G_DIR, BOSS_MAO1)) { CHECK(0, "MAO1 overlay"); return; }
    memset(&G, 0, sizeof G); G.ai = &o; G.boss.info = boss_img16(&G, 0xA002);
    CHECK(G.boss.info == 0xA581, "MAO1 [A002] = A581, got %04X", G.boss.info);
    /* the script: 135 bytes ending in 0xFF, three 0x80|n text commands, three
     * 0xC0 clears and one 0xE0 sound */
    int len = 0, texts = 0, clears = 0, sounds = 0, poses = 0;
    for (int i = 0; i < 200; i++) {
        uint8_t c = boss_img8(&G, 0xA3BB + (unsigned)i);
        len = i + 1;
        if (c == 0xFF) break;
        if (!(c & 0x80)) { poses++; CHECK(c <= 0x0A, "MAO1 script pose %u is 0..0x0A", c); }
        else if ((c & 0xF0) == 0x80) texts++;
        else if ((c & 0xF0) == 0xC0) clears++;
        else if ((c & 0xF0) == 0xE0) sounds++;
    }
    CHECK(len == 135, "MAO1's script is 135 bytes, got %d", len);
    CHECK(texts == 3 && clears == 3 && sounds == 1,
          "MAO1 shows 3 texts, clears 3 times and plays one sound (%d/%d/%d)", texts, clears, sounds);
    CHECK(poses == 127, "the other %d bytes are poses", poses);
    /* the three strings, {u16 x, chars, 0xFF} */
    static const char *want[3] = {"Finally, you reached me.", "I enjoyed your show.", "Come on!"};
    for (int t = 0; t < 3; t++) {
        unsigned sp = boss_img16(&G, 0xA442 + 2u * (unsigned)t);
        char buf[64]; unsigned n = 0;
        for (unsigned a = sp + 2; n < sizeof buf - 1; a++) {
            uint8_t c = boss_img8(&G, a);
            if (c == 0xFF) break;
            buf[n++] = (char)c;
        }
        buf[n] = 0;
        CHECK(strncmp(buf, want[t], strlen(want[t])) == 0, "MAO1 text %d = \"%s\"", t, buf);
    }
    /* poses 0..2 are class 0 only; the later ones bring in classes 1..6 */
    int cls0 = 1, clsmax = 0;
    for (int p = 0; p < 11; p++) {
        unsigned ls = boss_img16(&G, 0xA495 + 2 * p);
        int n = pcount(boss_img16(&G, 0xA52F + 2 * p), 6);
        for (int i = 0; i < n; i++) {
            int cls = boss_img8(&G, ls + (unsigned)i) >> 4;
            if (p < 2 && cls != 0) cls0 = 0;
            if (cls > clsmax) clsmax = cls;
        }
    }
    /* src/ai/boss_mao1.c and docs/ENEMIES.md say "poses 0..2 use class 0 only";
     * the image disagrees — pose 2's list ([A4BA]) already carries 0x18/0x16/
     * 0x17, i.e. class 1.  Only poses 0 and 1 are pure class 0. */
    CHECK(cls0, "MAO1 poses 0-1 are the class-0 human figure");
    CHECK(clsmax == 6, "the demon grows up to class 6, got %d", clsmax);
    ai_unload(&o);
}

static void t_mao2(void)
{
    AiOverlay o;
    if (ai_load(&o, G_DIR, BOSS_MAO2)) { CHECK(0, "MAO2 overlay"); return; }
    memset(&G, 0, sizeof G); G.ai = &o; G.boss.info = boss_img16(&G, 0xA002);
    CHECK(G.boss.info == 0xAC03, "MAO2 [A002] = AC03, got %04X", G.boss.info);
    for (int p = 0; p < 14; p++) {
        int r = pcount(boss_img16(&G, 0xAAE1 + 2 * p), 6);
        int l = pcount(boss_img16(&G, 0xAA71 + 2 * p), 6);
        CHECK(r == l && r >= 7 && r <= 9, "MAO2 pose %d: %d parts right / %d left", p, r, l);
    }
    static const uint8_t strike[10] = {0, 0, 7, 7, 9, 10, 10, 11, 11, 12};
    for (int i = 0; i < 10; i++)
        CHECK(boss_img8(&G, 0xA46F + i) == strike[i], "MAO2 strike[%d]", i);
    static const uint8_t death[10] = {8, 8, 8, 12, 12, 12, 13, 13, 11, 11};
    for (int i = 0; i < 10; i++)
        CHECK(boss_img8(&G, 0xABF9 + i) == death[i], "MAO2 death pose[%d]", i);
    /* the jump arc: 14 entries then 0x80, six rows up and six back down */
    int up = 0, down = 0, fwd = 0, n = 0;
    for (; n < 20; n++) {
        if (boss_img8(&G, 0xA666 + 3u * (unsigned)n) == 0x80) break;
        int8_t dr = (int8_t)boss_img8(&G, 0xA666 + 3u * (unsigned)n + 1);
        fwd += boss_img8(&G, 0xA666 + 3u * (unsigned)n) ? 1 : 0;
        if (dr < 0) up -= dr; else down += dr;
    }
    CHECK(n == 14, "MAO2's jump is 14 frames, got %d", n);
    CHECK(up == 6 && down == 6, "it rises 6 rows and falls 6 (%d/%d)", up, down);
    CHECK(fwd == 8, "with 8 forward frames (2 cells each), got %d", fwd);
    ai_unload(&o);
}

/* the level records of the eleven boss maps, and the bank 6117 ends up with.
 * MPA0's +5 is 0xFF while its +4 names MAO2.GRP, so taking +5 unconditionally
 * left the final boss with no sprite bank at all. */
static void t_boss_banks(void)
{
    static const struct { int sys; const char *name; int ai, enp, bank, want; } B[] = {
        { 1, "MP1D", 1, 0xFF, 1, 1}, { 4, "MP2D", 3, 0xFF, 3, 3}, { 7, "MP3D", 5, 0xFF, 5, 5},
        {10, "MP4D", 7, 0xFF, 7, 7}, {13, "MP5D", 9, 0xFF, 9, 9}, {17, "MP6D", 11, 0xFF, 11, 11},
        {21, "MP73", 18, 0xFF, 7, 7}, {22, "MP7D", 13, 0xFF, 13, 13}, {28, "MP8D", 15, 0xFF, 15, 15},
        {29, "MP90", 16, 16, -1, 16}, {30, "MPA0", 17, 17, 0xFF, 17},
    };
    for (unsigned i = 0; i < sizeof B / sizeof B[0]; i++) {
        Map m; memset(&m, 0, sizeof m);
        if (map_load_system(&m, G_DIR, B[i].sys)) { CHECK(0, "%s", B[i].name); continue; }
        CHECK(m.ai == B[i].ai, "%s level record +3 = %d (want %d)", B[i].name, m.ai, B[i].ai);
        CHECK(m.enemies == B[i].enp, "%s +4 = %d (want %d)", B[i].name, m.enemies, B[i].enp);
        if (B[i].bank >= 0)
            CHECK(m.boss_bank == B[i].bank, "%s +5 = %d (want %d)", B[i].name, m.boss_bank, B[i].bank);
        int enp = (m.enemies != 0xFF) ? m.enemies : m.boss_bank;
        CHECK(enp == B[i].want, "%s loads sprite bank %d (want %d)", B[i].name, enp, B[i].want);
        map_free(&m);
    }
}

/* the four overlays ported last: DRGN, AKMA, MAO1 (the cutscene) and MAO2 */
static void t_generic(void)
{
    static const struct { int ai, map; const char *name; } G6[] = {
        {BOSS_DRGN, 22, "DRGN"}, {BOSS_AKMA, 28, "AKMA"},
        {BOSS_MAO1, 29, "MAO1"}, {BOSS_MAO2, 30, "MAO2"},
    };
    for (unsigned i = 0; i < sizeof G6 / sizeof G6[0]; i++) run_overlay(G6[i].ai, G6[i].map, G6[i].name);
}

/* 61A8/61BE/61DB: MP90 (system map 0x1D) is the one level whose record has
 * bit6, the "[E6] boss room" the hero walks into from the left under MAO1's
 * control; when the overlay clears [E6] (A36A) the main loop loads MPA0 and
 * puts him at (0x18,0x0D) for the last fight. */
static void t_e6_room(void)
{
    static Shell sh;
    memset(&sh, 0, sizeof sh);
    sh.quiet = 1;
    if (shell_init(&sh, G_DIR, 0x1D)) { CHECK(0, "cannot load MP90"); return; }
    Game *g = &sh.g;
    g->present = NULL;
    CHECK((sh.maps[0].lvl_flags & 0x40) != 0, "MP90 level flags %02X: bit6 -> [E6]", sh.maps[0].lvl_flags);
    CHECK((sh.maps[0].lvl_flags & 0x80) == 0, "MP90 is *not* an FF34 boss map");
    shell_load_enemy_banks(&sh, &sh.maps[0]);
    CHECK(g->boss_room == 0xFF, "[E6] is set when MP90 is entered");
    game_place(g, sh.maps[0].width / 2, sh.maps[0].row_bias, 0);
    game_boss_room_intro(g);
    CHECK(g->scroll_col == 0x29 && g->hero_scr_col == 5,
          "61A8 puts the camera at 0x29 with the hero 5 columns in (got %d/%u)",
          g->scroll_col, g->hero_scr_col);
    CHECK(g->boss_intro == 0xFF, "[9F26] boss_intro is set for the walk-in");
    CHECK(!g->walk_in, "the ordinary 7C6E walk-in does not run in an [E6] room");
    /* run the intro out: the overlay clears [E6] and shell_frame loads MPA0 */
    g->hp = g->max_hp = 9999;
    int loaded = 0;
    for (int f = 0; f < 900 && !loaded; f++) {
        g->dirs = 0; g->buttons = 0;
        shell_frame(&sh);
        if (g->map != &sh.maps[0] || !strcmp(g->map->name, "MPA0")) loaded = 1;
    }
    CHECK(loaded, "the [E6] intro ends and 61DB loads the next map");
    if (loaded) {
        CHECK(!strcmp(g->map->name, "MPA0"), "it is MPA0 (got %s)", g->map->name);
        CHECK(game_hero_map_col(g) == 0x18 && game_hero_map_row(g) == 0x0D + 1,
              "621F + 7DC1 place him at (0x18,0x0D+1) (got %d,%d)", game_hero_map_col(g), game_hero_map_row(g));
        CHECK(g->boss_map == 0xFF, "MPA0 is an FF34 boss map: the last fight starts");
    }
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
        {"crab damage", t_damage}, {"hit flash", t_hit_flash}, {"zela no flash", t_no_hit_flash}, {"crab death", t_death}, {"post-boss", t_post_boss}, {"boss reward", t_boss_reward},
        {"tako", t_tako}, {"tori", t_tori}, {"zela", t_zela}, {"zel2", t_zel2},
        {"meda", t_meda}, {"lega", t_lega}, {"layer tables", t_layers},
        {"drgn tables", t_drgn}, {"akma tables", t_akma},
        {"mao1 script", t_mao1}, {"mao2 tables", t_mao2},
        {"boss banks", t_boss_banks},
        {"drgn/akma/mao", t_generic}, {"[E6] room", t_e6_room},
    };
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; i++) {
        int before = fails;
        tests[i].fn();
        fprintf(stderr, "%-14s %s\n", tests[i].name, fails == before ? "ok" : "FAILED");
    }
    fprintf(stderr, "%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
