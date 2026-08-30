/* fuzz_sar.c — libFuzzer harness for sar_decompress (sar.c), the 8-opcode
 * STICK.BIN RLE engine.  This is the first parser every byte of every game
 * file goes through, so it sees fully attacker-controlled input the moment a
 * web build (#44) lets files be dropped onto a page.
 *
 * Build and run:  make fuzz && ./fuzz/fuzz_sar CORPUS_DIR
 * Seed a corpus:  ./fuzz/fuzz_seed ../zeliard CORPUS_ROOT   (never commit it:
 * the seeds are carved out of the copyrighted game files). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../sar.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    size_t out_len = 0;
    uint8_t *out = sar_decompress(data, size, &out_len);
    if (out) {
        /* touch both ends so ASan sees the whole claimed length */
        if (out_len) { volatile uint8_t v = out[0] ^ out[out_len - 1]; (void)v; }
        free(out);
    }
    return 0;
}
