/* fuzz_map.c — libFuzzer harness for the .mdt cavern-map parser (map.c).
 * The input is a raw (already decompressed) map image, exactly what
 * map_load_system hands to map_parse; the harness then forces every C00C
 * patch on and re-parses, and walks the C00E place record, because those two
 * paths follow file-internal offsets of their own.
 *
 * Build and run:  make fuzz && ./fuzz/fuzz_map CORPUS_DIR
 * Seed a corpus:  ./fuzz/fuzz_seed ../zeliard CORPUS_ROOT */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../map.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static Map m;                       /* map_parse frees the previous raw */
    if (map_parse(&m, data, size) == 0) {
        uint8_t all_on[256];
        memset(all_on, 0xFF, sizeof all_on);
        map_apply_patches(&m, all_on);  /* applies every poke, then re-parses */
        const uint8_t *rec = map_place_record(&m);
        if (rec) { volatile uint8_t v = rec[4 + rec[3] - (rec[3] ? 1 : 0)]; (void)v; }
    }
    map_free(&m);
    return 0;
}
