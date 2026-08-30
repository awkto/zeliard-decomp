/* fuzz_grp.c — libFuzzer harness for the .grp/.bin section walkers and the
 * file-driven resource loaders (gfx.c, gd.c, town.c, text.c, status.c,
 * enemy.c) plus sar_read_raw's own archive-table walk.
 *
 * The two gd unpackers take plain buffers and are fuzzed directly.  Everything
 * else loads through `sar_load(dir, archive, index, ...)`, so the harness
 * materialises a synthetic game directory once (in TMPDIR), writes the fuzz
 * payload as the single shared entry of ZELRES1-3.SAR before each run, and
 * calls one loader chosen by the first input byte.  All three archives are
 * hard links to one file, so one write feeds every loader.
 *
 * Build and run:  make fuzz && ./fuzz/fuzz_grp CORPUS_DIR
 * Seed a corpus:  ./fuzz/fuzz_seed ../zeliard CORPUS_ROOT */
#define _DEFAULT_SOURCE                 /* mkdtemp, link */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../sar.h"
#include "../gfx.h"
#include "../gd.h"
#include "../map.h"
#include "../town.h"
#include "../text.h"
#include "../status.h"
#include "../enemy.h"

#define NENT 96                         /* covers every index the loaders use */

static char gdir[512];
static char arcpath[600];

static void die(const char *what) { perror(what); exit(1); }

static void setup_dir(void)
{
    const char *tmp = getenv("TMPDIR");
    if (!tmp) tmp = access("/dev/shm", W_OK) == 0 ? "/dev/shm" : "/tmp";
    snprintf(gdir, sizeof gdir, "%s/zelfuzz.XXXXXX", tmp);
    if (!mkdtemp(gdir)) die("mkdtemp");
    snprintf(arcpath, sizeof arcpath, "%s/ZELRES1.SAR", gdir);
    FILE *f = fopen(arcpath, "wb");
    if (!f) die("ZELRES1.SAR");
    fclose(f);
    char lnk[600];
    snprintf(lnk, sizeof lnk, "%s/ZELRES2.SAR", gdir);
    if (link(arcpath, lnk)) die("link ZELRES2");
    snprintf(lnk, sizeof lnk, "%s/ZELRES3.SAR", gdir);
    if (link(arcpath, lnk)) die("link ZELRES3");
    /* itemp_load also opens GMMCGA.BIN for the blank-slot picture */
    snprintf(lnk, sizeof lnk, "%s/GMMCGA.BIN", gdir);
    if (link(arcpath, lnk)) die("link GMMCGA");
}

static void put32(FILE *f, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    fwrite(b, 1, 4, f);
}

/* a well-formed archive whose every entry is the payload (fuzzes the loaders) */
static void write_archive(const uint8_t *payload, size_t n)
{
    FILE *f = fopen(arcpath, "wb");
    if (!f) die("archive");
    for (int i = 0; i < NENT; i++) put32(f, NENT * 4);
    put32(f, (uint32_t)n);
    fwrite(payload, 1, n, f);
    fclose(f);
}

/* the payload IS the archive (fuzzes sar_read_raw's table walk itself) */
static void write_raw_archive(const uint8_t *payload, size_t n)
{
    FILE *f = fopen(arcpath, "wb");
    if (!f) die("archive");
    fwrite(payload, 1, n, f);
    fclose(f);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 2) return 0;
    if (!gdir[0]) setup_dir();
    int target = data[0] % 20;
    int sub = data[1];
    data += 2; size -= 2;

    static uint8_t unpacked[0x30000];
    static uint8_t fb[GD_W * GD_H], scratch[GD_SCRATCH];
    static Tileset tiles;
    static HeroGfx hero;                /* + SwordGfx, loaded with it */
    static EnemyGfx egfx;
    static DigitFont digits;
    static ScreenFrame frame;
    static EncounterCard encnt;
    static Gd gd;
    static ItemPics pics;
    static TextFont font;
    static TownMap tmap;
    static TownTiles ttiles;
    static TownSprites tspr;
    static TownHero thero;
    static TownBackdrop tback;
    static AiOverlay ai;
    static Map map;

    switch (target) {
    case 0: gd_unpack_mask(data, size, unpacked, sizeof unpacked, 1); break;
    case 1: gd_unpack_mask(data, size, unpacked, sizeof unpacked, 0); break;
    case 2: gd_unpack_rle(data, size, unpacked, sizeof unpacked); break;
    case 3:
        write_archive(data, size);
        gfx_load_tileset(&tiles, gdir, sub % 11);
        break;
    case 4:
        write_archive(data, size);
        gfx_load_hero(&hero, gdir);     /* fman.grp + sword.grp + gfmcga */
        break;
    case 5:
        write_archive(data, size);
        gfx_load_enemy_cells(&egfx, gdir, sub % 18);
        break;
    case 6:
        write_archive(data, size);
        gfx_load_digits(&digits, gdir);
        break;
    case 7:
        write_archive(data, size);
        gfx_load_screen_frame(&frame, gdir);
        break;
    case 8:
        write_archive(data, size);
        gfx_load_encounter(&encnt, gdir);
        break;
    case 9:
        write_archive(data, size);
        if (gd_init(&gd, gdir, fb, scratch, NULL) == 0) {
            gd_set_palette(&gd, sub % 10);
            gd_free(&gd);
        }
        break;
    case 10:
        write_archive(data, size);
        itemp_load(&pics, gdir);
        itemp_free(&pics);
        break;
    case 11:
        write_archive(data, size);
        text_load_font(&font, gdir);
        break;
    case 12:
        write_archive(data, size);
        if (town_load_map(&tmap, gdir, sub % 10) == 0) {
            uint8_t all_on[256];
            memset(all_on, 0xFF, sizeof all_on);
            town_apply_patches(&tmap, all_on);
            for (int i = 0; i < tmap.ndlg; i++) town_dialogue(&tmap, i);
            town_place_record(&tmap);
        }
        town_free_map(&tmap);
        break;
    case 13:
        write_archive(data, size);
        town_load_tiles(&ttiles, gdir, sub % 3);
        break;
    case 14:
        write_archive(data, size);
        town_load_sprites(&tspr, gdir, sub % 2);
        break;
    case 15:
        write_archive(data, size);
        town_load_backdrop(&tback, gdir, sub % 2);
        break;
    case 16:
        write_archive(data, size);
        town_load_hero(&thero, gdir);
        break;
    case 17:
        write_archive(data, size);
        if (ai_load(&ai, gdir, sub % 19) == 0) {
            for (int t = 0; t < 8; t++) ai_drop_list(&ai, t);
            for (int t = 0; t < 32; t++) ai_frame(&ai, (uint8_t)t, 0x80, (uint8_t)sub);
        }
        ai_unload(&ai);
        break;
    case 18:
        write_archive(data, size);
        if (map_load_system(&map, gdir, sub % 42) == 0) {
            uint8_t all_on[256];
            memset(all_on, 0xFF, sizeof all_on);
            map_apply_patches(&map, all_on);
        }
        map_free(&map);
        break;
    case 19:                            /* the archive header itself */
        write_raw_archive(data, size);
        {
            size_t len;
            uint8_t *d = sar_load(gdir, sub % 3, sub / 3 % NENT, sub & 1, &len);
            free(d);
        }
        break;
    }
    return 0;
}
