/* synclife.c -- see synclife.h. */
#include "synclife.h"
#include "net.h"
#include <stdio.h>
#include <string.h>

static void say(const char *fmt, const char *a) { fprintf(stderr, "[sync] "); fprintf(stderr, fmt, a); fputc('\n', stderr); }

/* one push, the way the lifecycle does it: newer-wins, never forced */
static int do_push(SyncLife *sl)
{
    net_set_timeouts(SL_CONNECT_S, SL_PUSH_S);
    atomic_store(&sl->dirty, 0);                /* what is on disk now is what this push carries */
    int rc = sync_push(&sl->ctx, 0);
    sl->last_rc = rc;
    if (rc == SYNC_OFFLINE || rc == SYNC_ERR) atomic_store(&sl->dirty, 1);   /* try again at exit */
    if (rc == SYNC_OFFLINE) say("offline, %s", "push skipped; the next save tries again");
    else if (rc == SYNC_DECIDE) say("%s", "not pushed: the key is not linked yet");
    return rc;
}

int synclife_begin(SyncLife *sl, int want, const Config *cfg, const char *cfgpath,
                   const char *game_dir, const SyncRunner *run)
{
    memset(sl, 0, sizeof *sl);
    atomic_init(&sl->busy, 0); atomic_init(&sl->pending, 0);
    if (run) sl->run = *run;
    sl->state = SL_OFF;
    if (!want || cfg->sync != 1) return SL_OFF;
    if (!net_supported()) { say("%s", "sync = on, but this build has no network support (libcurl); sync is off"); return SL_OFF; }
    if (sync_ctx_init(&sl->ctx, cfg, cfgpath, game_dir, NULL, NULL, 0) != 0) { say("%s", "sync is off for this session"); return SL_OFF; }
    /* nothing local to lose: take the server's save without asking (the web app does the same) */
    SyncUsr *l = NULL; int n = 0;
    int have_local = 0;
    if (sync_scan_local(game_dir, &l, &n) == 0) { have_local = n > 0; sync_free_usr(l, n); }
    net_set_timeouts(SL_CONNECT_S, SL_PULL_S);
    int rc = sync_pull(&sl->ctx, 0);
    if (rc == SYNC_DECIDE && !have_local) rc = sync_pull(&sl->ctx, 1);
    sl->last_rc = rc;
    switch (rc) {
    case SYNC_OK:      sl->state = SL_ON; break;
    case SYNC_OFFLINE: say("%s", "offline: playing from the local saves"); sl->state = SL_ON; break;
    case SYNC_DECIDE:  sl->state = SL_ASK; break;
    default:           say("%s", "sync failed; off for this session"); sl->state = SL_OFF; break;
    }
    return sl->state;
}

const char *synclife_key(const SyncLife *sl) { return sl->ctx.key; }

int synclife_resolve(SyncLife *sl, int choice)
{
    if (sl->state != SL_ASK) return sl->state;
    int rc = SYNC_ERR;
    if (choice == SLC_CLOUD) { net_set_timeouts(SL_CONNECT_S, SL_PULL_S); rc = sync_pull(&sl->ctx, 1); }
    else if (choice == SLC_LOCAL) { net_set_timeouts(SL_CONNECT_S, SL_PUSH_S); rc = sync_push(&sl->ctx, 1); }
    else { say("%s", "sync off for this session (nothing changed)"); sl->state = SL_OFF; return SL_OFF; }
    sl->last_rc = rc;
    if (rc == SYNC_OK) { sl->state = SL_ON; return SL_ON; }
    say("%s", rc == SYNC_OFFLINE ? "offline: not linked, sync off for this session" : "that did not work: not linked, sync off for this session");
    sl->state = SL_OFF;
    return SL_OFF;
}

/* the push loop; runs on the runner's thread or inline.  `busy` is held by
 * whoever runs it; a request that lands while it finishes is not lost. */
static void push_worker(void *arg)
{
    SyncLife *sl = arg;
    for (;;) {
        while (atomic_exchange(&sl->pending, 0)) do_push(sl);
        atomic_store(&sl->busy, 0);
        int zero = 0;
        if (!atomic_load(&sl->pending) || !atomic_compare_exchange_strong(&sl->busy, &zero, 1)) break;
    }
}

void synclife_request_push(SyncLife *sl)
{
    if (sl->state != SL_ON) return;
    atomic_store(&sl->dirty, 1);
    atomic_store(&sl->pending, 1);
    int zero = 0;
    if (!atomic_compare_exchange_strong(&sl->busy, &zero, 1)) return;   /* the running worker will see it */
    if (sl->thr && sl->run.join) { sl->run.join(sl->thr); sl->thr = NULL; }
    if (sl->run.spawn) sl->thr = sl->run.spawn(push_worker, sl);
    if (!sl->thr) push_worker(sl);
}

void synclife_finish(SyncLife *sl)
{
    if (sl->thr && sl->run.join) { sl->run.join(sl->thr); sl->thr = NULL; }
    if (sl->state == SL_ON && atomic_load(&sl->dirty) && !atomic_load(&sl->busy)) {
        atomic_store(&sl->pending, 0);
        do_push(sl);
    }
    sl->state = SL_OFF;
}
