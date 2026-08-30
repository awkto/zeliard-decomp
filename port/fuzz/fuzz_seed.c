/* fuzz_seed.c — carve a seed corpus for the three fuzz harnesses out of a real
 * game directory.  Usage:  ./fuzz/fuzz_seed GAMEDIR CORPUS_ROOT
 *
 * Writes CORPUS_ROOT/{sar,map,grp}/...  The seeds are byte ranges of the
 * copyrighted game files, so the corpus must NEVER be committed — generate it
 * where the game files already are (the CI smoke run starts from an empty
 * corpus instead).
 *   sar/  the compressed streams sar_decompress sees (container prefix parsed
 *         off, both variants of two-variant containers)
 *   map/  decompressed .mdt images (map_parse's input)
 *   grp/  raw archive entries with a 2-byte {target, sub} harness prefix, one
 *         per loader that reads that resource */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "../sar.h"

static const char *root;

static void put(const char *sub, const char *name, const uint8_t *pre, size_t npre,
                const uint8_t *d, size_t n)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s/%s", root, sub, name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    if (npre) fwrite(pre, 1, npre, f);
    fwrite(d, 1, n, f);
    fclose(f);
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: %s GAMEDIR CORPUS_ROOT\n", argv[0]); return 2; }
    const char *dir = argv[1];
    root = argv[2];
    char path[1024];
    mkdir(root, 0755);
    static const char *const SUBS[3] = { "sar", "map", "grp" };
    for (int s = 0; s < 3; s++) {
        snprintf(path, sizeof path, "%s/%s", root, SUBS[s]);
        mkdir(path, 0755);
    }

    int nseed = 0;
    /* every entry of every archive: the raw payload for grp/, the compressed
     * stream(s) for sar/ */
    for (int a = 0; a < 3; a++) {
        for (int i = 0; i < 96; i++) {
            size_t len;
            uint8_t *raw = sar_read_raw(dir, a, i, &len);
            if (!raw) break;
            char name[64];
            /* one grp/ seed per (loader, resource): the harness prefix picks
             * the loader, and coverage does the rest.  Spread the entries over
             * all the file-driven targets (3..18 in fuzz_grp.c). */
            uint8_t pre[2] = { (uint8_t)(3 + i % 16), (uint8_t)i };
            snprintf(name, sizeof name, "r%d_%02d", a + 1, i);
            put("grp", name, pre, 2, raw, len);
            if (len && raw[0] == 0) {
                snprintf(name, sizeof name, "r%d_%02d_p", a + 1, i);
                put("sar", name, NULL, 0, raw + 1, len - 1);
            } else if (len >= 5) {
                size_t la = rd16(raw + 1), lb = rd16(raw + 3);
                if (5 + la + lb == len) {
                    snprintf(name, sizeof name, "r%d_%02d_a", a + 1, i);
                    put("sar", name, NULL, 0, raw + 5, la);
                    snprintf(name, sizeof name, "r%d_%02d_b", a + 1, i);
                    put("sar", name, NULL, 0, raw + 5 + la, lb);
                }
            }
            free(raw);
            nseed++;
        }
    }
    /* the decompressed system and town map images for map/ */
    static const struct { int a, r; } MAPS[] = {
        {2,20},{2,21},{2,22},{2,23},{2,24},{2,25},{2,26},{2,27},{2,28},{2,29},
        {2,30},{2,31},{2,32},{2,33},{2,34},{2,35},{2,36},{2,37},{2,38},{2,39},
        {2,40},{2,41},{2,42},{2,43},{2,44},{2,45},{2,46},{2,47},{2,48},{2,49},
        {2,50},{1,36},{1,37},{1,38},{1,39},{1,40},{1,41},{1,42},{1,43},{1,44},{1,45},
    };
    for (size_t i = 0; i < sizeof MAPS / sizeof MAPS[0]; i++) {
        size_t len;
        uint8_t *d = sar_load(dir, MAPS[i].a, MAPS[i].r, 1, &len);
        if (!d) continue;
        char name[64];
        snprintf(name, sizeof name, "m%d_%02d", MAPS[i].a + 1, MAPS[i].r);
        put("map", name, NULL, 0, d, len);
        free(d);
        nseed++;
    }
    fprintf(stderr, "%d seeds under %s (game-derived: do not commit)\n", nseed, root);
    return 0;
}
