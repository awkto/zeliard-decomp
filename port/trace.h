/* trace.h — the port side of the differential harness (docs/HARNESS.md, #48).
 *
 * One JSON line per boundary (fight.bin 717F / town.bin 68ED, or a wait loop's
 * poll site inside a shop, menu or the status screen):
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

/* append one frame line.  `site`: 717F / 68ED for the frame boundaries, or a
 * wait loop's (71EC, 7358, select:AA58: docs/HARNESS.md §1).  `t` = NULL
 * serialises fight.bin's field set, else the town engine's.  `g` is only read
 * (its page[] is used as a scratch copy and restored). */
void trace_write_frame(FILE *f, unsigned n, const char *site, unsigned rng_calls, const char *input, Game *g, const Town *t);

/* a boundary line with no fields: the ending's "end:wait" (#113), a cutscene that has
 * no game state; tracediff compares its site, and the PNGs compare the picture */
void trace_write_site(FILE *f, unsigned n, const char *site, unsigned rng_calls, const char *input);

/* the last line of a run that leaves for DOS through the game's own exit (the
 * Sage's "No", kenjpro A934; docs/HARNESS.md section 6): no fields, no frame
 *   {"f":N,"site":"exit","rng_calls":k,"input":"M","code":0}
 * N = the frame the program ended in (what the next boundary would have been) */
void trace_write_exit(FILE *f, unsigned n, unsigned rng_calls, const char *input, unsigned code);

/* every field name the port can emit, one per line (`zeliard --trace-fields`),
 * so tools/tracediff.py can check them against the schema */
void trace_list_fields(FILE *f);

#endif
