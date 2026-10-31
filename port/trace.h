/* trace.h — the port side of the differential harness (docs/HARNESS.md, #48).
 *
 * One JSON line per frame boundary (fight.bin 717F / town.bin 68ED):
 *   {"f":n,"site":"717F","rng_calls":k,"input":"R","fields":{...}}
 * `fields` holds every tools/harness/fields.tsv field the port models, under
 * the schema's names, serialised from the engine's C structs.  Integers for
 * 1- and 2-byte fields, lowercase hex strings (memory order) for longer ones.
 * Repeated records follow the schema's list rows: `obj.n`, `obj[i].col`, ...
 *
 * Nothing here runs unless `--trace` installed the boundary hooks. */
#ifndef ZEL_TRACE_H
#define ZEL_TRACE_H
#include <stdio.h>
#include "physics.h"
#include "town.h"

/* the script-language spelling of one frame's input: "UR", "X", "." (idle) */
void trace_input_token(char out[12], uint8_t dirs, uint8_t buttons, uint8_t menu_key);

/* append one frame line.  `t` = NULL for a fight.bin boundary (site 717F),
 * else the town engine (site 68ED).  `g` is only read (its page[] is used as a
 * scratch copy and restored). */
void trace_write_frame(FILE *f, unsigned n, unsigned rng_calls, const char *input, Game *g, const Town *t);

/* every field name the port can emit, one per line (`zeliard --trace-fields`),
 * so tools/tracediff.py can check them against the schema */
void trace_list_fields(FILE *f);

#endif
