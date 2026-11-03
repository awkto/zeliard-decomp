/* synclife.h -- save sync while playing (#55 step 2; docs/SYNC.md 4, 5, 6.4).
 *
 * Active only in an interactive run with `sync = on` (zeliard.ini) and a
 * server.  Headless/tool runs (--script, --trace, --save, --screenshot, the
 * harness) never create one, so scenarios are unaffected.
 *
 *   begin    before shell_init: pull (newer-wins), or find that the key has to
 *            be linked first (SL_ASK);
 *   resolve  the answer to the ask-before-link dialog: keep the cloud save
 *            (pull --force), keep this device's (push --force), or cancel
 *            (sync off for the session);
 *   request_push  after each player_save_usr: a background push (the runner
 *            below; inline when there is none);
 *   finish   at exit: wait for a push in flight, then a last push if anything
 *            was saved this session.
 *
 * Offline never blocks play for long: connect 3 s, whole request 10 s on the
 * start-up pull, 20 s on a push (which is off the frame loop).  SDL-free; the
 * thread is the caller's (SyncRunner), the protocol is sync.h's. */
#ifndef ZEL_SYNCLIFE_H
#define ZEL_SYNCLIFE_H
#include <stdatomic.h>
#include "sync.h"

enum { SL_OFF = 0,      /* not syncing (not enabled, cancelled, or a hard error) */
       SL_ON,           /* syncing */
       SL_ASK };        /* the key is unlinked and both sides have a save: ask first */
enum { SLC_CLOUD = 1, SLC_LOCAL, SLC_CANCEL };

#define SL_CONNECT_S 3
#define SL_PULL_S    10
#define SL_PUSH_S    20

/* a way to run fn(arg) on another thread; spawn returns a handle (NULL = could
 * not, the push then runs inline), join waits for it and frees it */
typedef struct SyncRunner {
    void *(*spawn)(void (*fn)(void *), void *arg);
    void  (*join)(void *handle);
} SyncRunner;

typedef struct SyncLife {
    int         state;
    SyncCtx     ctx;
    SyncRunner  run;
    atomic_int  dirty;          /* saved since the last good push */
    int         last_rc;        /* of the last pull/push, a SYNC_* code */
    void       *thr;            /* the push thread (joined before the next spawn) */
    atomic_int  busy, pending;
} SyncLife;

/* Decide and do the start-up pull.  `want` = the run is interactive; cfg->sync
 * must be 1.  Returns SL_OFF / SL_ON / SL_ASK.  An offline server or an error
 * is logged once and is never fatal; SL_ASK touched nothing. */
int  synclife_begin(SyncLife *sl, int want, const Config *cfg, const char *cfgpath,
                    const char *game_dir, const SyncRunner *run);
/* the answer to SL_ASK (SLC_*).  Returns the new state: SL_ON when it worked,
 * else SL_OFF (cancel, offline, or an error: nothing is recorded as linked). */
int  synclife_resolve(SyncLife *sl, int choice);
/* the key / server the dialog names */
const char *synclife_key(const SyncLife *sl);
/* after a player_save_usr: queue a push (returns at once with a runner) */
void synclife_request_push(SyncLife *sl);
/* at exit: join, then one last push if a save has not been pushed yet.  Leaves the state SL_OFF. */
void synclife_finish(SyncLife *sl);

#endif
