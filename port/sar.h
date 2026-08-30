/* sar.h — Game Arts .SAR archive reader + STICK.BIN RLE decompressor (C port of
 * tools/sarex.py and tools/sardec.py).  See docs/ARCHITECTURE.md ".SAR archive
 * format" and "Compression". */
#ifndef ZEL_SAR_H
#define ZEL_SAR_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Open a game file for reading, trying `name` as given and then all-lower-case.
 * The originals are upper-case on the DOS media, but a dump made on a
 * case-sensitive filesystem is very often lower-cased throughout, and every
 * loader that open-coded this dance and forgot the second half lost its file
 * silently: `SNDADLIB.DRV` took every sound effect with it, and shell.c's
 * game-directory probe failed to find a lower-case dump at all. */
FILE *game_fopen(const char *dir, const char *name);

/* Read entry `index` (0-based) of ZELRES{archive+1}.SAR in `dir`, parse the
 * kernel AL=2 container and decompress it.  Returns a malloc'd buffer (caller
 * frees) and its length, or NULL.  `variant` picks stream A (0, EGA) or B (1,
 * every other video mode) for two-variant containers. */
uint8_t *sar_load(const char *dir, int archive, int index, int variant, size_t *out_len);

/* Raw payload of an entry (no container parsing). */
uint8_t *sar_read_raw(const char *dir, int archive, int index, size_t *out_len);

/* Decompress one STICK.BIN stream (first byte & 7 = opcode).  Returns malloc'd
 * output or NULL on a malformed stream. */
uint8_t *sar_decompress(const uint8_t *stream, size_t len, size_t *out_len);

#endif
