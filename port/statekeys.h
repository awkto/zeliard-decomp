/* statekeys.h — the player-facing realtime save states (#52 step 2).
 *
 * Step 1 (port/savestate.c) owns the state blob and the two CLI flags.  This is
 * the layer a *player* touches: a small request queue (a key press or an
 * injected event), the numbered slots' file names under the states directory,
 * and the short on-screen confirmation.  It is deliberately SDL-free so the
 * test binaries can link it and check the slot mapping; the front end owns the
 * key mapping and the actual drawing.
 *
 * A request is never executed where it is made: it is queued and run by the
 * shell's frame_done hook at the true end of a frame, which is the only point a
 * fresh game_step/town_step can resume from.  A save/load made while a shop, the
 * status screen, a dialogue box or the Tear cutscene owns the frame loop is
 * refused (step 1 cannot serialise those), with the same message a player sees. */
#ifndef ZEL_STATEKEYS_H
#define ZEL_STATEKEYS_H
#include <stddef.h>
#include <stdint.h>
#include "shell.h"

enum { STATE_REQ_NONE = 0, STATE_REQ_SAVE = 1, STATE_REQ_LOAD = 2 };

#define STATEKEYS_QUEUE 4               /* requests that may fire at one boundary */
#define STATEKEYS_OSD   48
#define STATEKEYS_OSD_FRAMES 96         /* ~2.5 s at speed 5 */

typedef struct StateKeys {
    int      q_kind[STATEKEYS_QUEUE];   /* pending requests, in order */
    int      q_slot[STATEKEYS_QUEUE];
    int      q_n;
    unsigned rng;                       /* g->rng_calls at the boundary they fire */
    int      rng_set;
    char     dir[1024];                 /* the states directory (resolved once) */
    int      dir_ok;
    char     osd[STATEKEYS_OSD];
    int      osd_frames;
    /* counters, for tests and the verbose log */
    unsigned saved, loaded, refused;
    unsigned last_rng;                  /* the loaded state's meta.rng_calls */
} StateKeys;

void statekeys_init(StateKeys *k);

/* Resolve the states directory, creating it.  `override` (--state-dir) wins;
 * else `pref` (SDL_GetPrefPath's trailing-slash result) + "states"; else
 * `gamedir` + "/states".  Returns 0 when the directory is usable. */
int  statekeys_dir(StateKeys *k, const char *override, const char *pref, const char *gamedir);

/* The file a slot uses: quick.zst for -1, slot1.zst..slot9.zst for 1..9.
 * Returns -1 when the directory is unusable. */
int  statekeys_path(const StateKeys *k, int slot, char *out, size_t n);

/* Queue a request.  Refuses (and shows the OSD) when an overlay owns the loop,
 * or when the queue is full. */
void statekeys_request(StateKeys *k, const Shell *s, int kind, int slot);

/* Queue the next request for the harness/key path without the overlay check,
 * used by --state-inject (the boundary is known to be a plain frame). */
void statekeys_queue(StateKeys *k, int kind, int slot);

/* ---- OSD text and the 5x7 font (presentation; the front end draws it) ---- */
void        statekeys_osd(StateKeys *k, const char *fmt, ...);
void        statekeys_osd_tick(StateKeys *k);           /* one presented frame */
const char *statekeys_osd_text(const StateKeys *k);
const uint8_t *statekeys_glyph(char c);                 /* 5 column bytes, or NULL */
int         statekeys_text_width(const char *s, int scale);

#endif
