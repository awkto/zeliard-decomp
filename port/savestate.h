/* savestate.h — realtime engine save states (epic #46 Phase B, issue #52).
 *
 * The wasm / DOSBox-X builds snapshot the whole machine; the port cannot, so
 * this lists the engine state explicitly instead: every dynamic byte of the
 * cavern Game, the town Town and the shell's mode, at a frame boundary.  The
 * blob is versioned, little-endian, and checksummed, so an old, truncated or
 * corrupt state is refused rather than dereferenced.
 *
 * The format is documented in docs/HARNESS.md ("Save states").  It is *engine*
 * state only: resource pointers (map, tiles, AI overlay, fonts, art) are not
 * stored; the loader rebuilds them through the shell's own entry points and
 * then overwrites the dynamic fields.  Presentation and shell-only bookkeeping
 * (frame counters, the front end's hooks) is deliberately out of scope. */
#ifndef ZEL_SAVESTATE_H
#define ZEL_SAVESTATE_H
#include "shell.h"

#define SAVESTATE_MAGIC   "ZELSTATE"
#define SAVESTATE_VERSION 1u

/* what the loader hands back to the harness: the trace frame this state is the
 * boundary of, and the kernel RNG call counter at that boundary (so the
 * `rng_calls` delta of the next frame is computed against the right base). */
typedef struct {
    unsigned frame;
    unsigned rng_calls;
} SavestateMeta;

/* Write the complete engine state at frame boundary `frame` to `path`.
 * Returns 0 on success, -1 when the shell is inside a blocking overlay (a shop,
 * the status screen, a dialogue box or the Tear cutscene) or on I/O error. */
int savestate_save(Shell *s, const char *path, unsigned frame, unsigned rng_calls);

/* Read `path` into `s` (already shell_init()ed) and fill `meta`.  The shell is
 * re-entered into the saved map or town first, so the resource pointers are
 * valid, then the dynamic fields are restored.  Returns 0 on success; a bad
 * magic, an unknown version, a wrong size, a failed checksum or a truncated
 * payload all return -1 cleanly, without touching memory out of bounds. */
int savestate_load(Shell *s, const char *path, SavestateMeta *meta);

#endif
