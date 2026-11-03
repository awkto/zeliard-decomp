/* sync.h — save sync with the zeliard-wasm server (#55 step 1; docs/SYNC.md).
 *
 * The wire protocol, key scheme, link record and per-.USR union are those of
 * the existing web/desktop clients (zeliard-wasm: native/launcher/core/sync.go,
 * link.go; docker/saves-api.py).  This is step 1: the protocol core plus pull
 * and push of the `auto` slot, driven from the command line.  No UI, no
 * lifecycle hooks.  SDL-free; HTTP goes through net.h.
 *
 * Port rules that differ from the Go client (docs/SYNC.md 6.4):
 *  - only the player's `.usr` files are managed.  Every other bundle entry
 *    (game files, dosbox.conf, .jsdos/ files, save/1.sav) is foreign and passes
 *    through a push byte for byte;
 *  - the port cannot make a bootable bundle, so it never creates `auto`: a
 *    push needs a bundle on the server already. */
#ifndef ZEL_SYNC_H
#define ZEL_SYNC_H
#include <stddef.h>
#include <stdio.h>
#include "config.h"

#define SYNC_PATH 1024
#define SYNC_SLACK_MS 2000          /* zip stores 2 s timestamps (Go: timeSlack) */

typedef struct SyncCtx {
    char base[512];                 /* server URL, normalised to end in '/' */
    char key[80];                   /* X-Client-Id */
    char token[128];                /* optional bearer token */
    char game_dir[SYNC_PATH];       /* where NAME.usr live */
    char cfg_dir[SYNC_PATH];        /* sync-key, sync-links.json, backups/ */
} SyncCtx;

/* exit codes of the --sync tool and the return values of sync_pull/push/status */
enum { SYNC_OK = 0, SYNC_ERR = 1, SYNC_DECIDE = 3 /* unlinked key with a remote save */,
       SYNC_OFFLINE = 4, SYNC_NONET = 5, SYNC_NOBUNDLE = 6 /* push, nothing on the server */ };

/* ---- keys (docs/SYNC.md 2) */
/* uppercase, keep A-Z0-9, 4..32 chars, '-' every 4.  1 = ok. */
int  sync_key_normalize(const char *in, char *out, size_t n);
/* a key valid on the wire: ^[A-Za-z0-9_-]{1,64}$ */
int  sync_key_wire_ok(const char *k);
/* 4 chars from the web alphabet (no I/O/0/1) out of 4 random bytes */
void sync_key_from_bytes(const unsigned char rnd[4], char out[5]);
/* explicit (CLI/ini; "" = none) > cfg_dir/sync-key > generate and store.
 * 0 ok, -1 explicit key invalid or no key could be stored. */
int  sync_key_resolve(SyncCtx *c, const char *explicit_key);
/* persist `key` to cfg_dir/sync-key (what `--sync-key` does). */
int  sync_key_store(const SyncCtx *c, const char *key);

/* ---- link record (sync-links.json, same schema as the Go client) */
int  sync_link_get(const SyncCtx *c, long long *last_synced);   /* 1 = linked */
int  sync_link_mark(const SyncCtx *c, long long remote_ms);
int  sync_link_unlink(const SyncCtx *c);

/* ---- decisions (link.go NeedsDecision/ShouldPull/ShouldPush) */
enum { SD_GO = 1, SD_NO_REMOTE, SD_NO_LOCAL, SD_NEEDS_DECISION, SD_NOT_NEWER };
int  sync_decide_pull(int linked, long long remote, long long local);
int  sync_decide_push(int linked, long long remote, long long local);

/* ---- the bundle */
typedef struct SyncUsr {
    char           name[64];        /* as on disk ("hero.usr") or in the zip ("HERO.USR") */
    unsigned char *data; size_t len;
    long long      mtime_ms;
} SyncUsr;

/* "NAME.USR" in the zip <-> "name.usr" in the game folder */
int  sync_usr_entry_ok(const char *zip_name);                   /* root-level, *.usr any case, safe chars */
void sync_usr_local_name(const char *zip_name, char *out, size_t n);   /* lowercase */
void sync_usr_zip_name(const char *local_name, char *out, size_t n);   /* uppercase */

int  sync_scan_local(const char *dir, SyncUsr **out, int *n);   /* .usr files; 0 ok */
void sync_free_usr(SyncUsr *list, int n);
long long sync_local_modified(const SyncUsr *list, int n);      /* newest mtime, 0 = none */

/* Rebuild the bundle: every non-.USR entry of `remote` copied through byte for
 * byte (compressed data, mtime and all); each .USR is the newer of local and
 * remote (remote wins only if > local + slack); remote-only .USR carried.
 * *carried_newest = newest remote mtime that had to be carried, else 0.
 * Returns 0 and a malloc'd blob, or -1 (remote unreadable). */
int  sync_bundle_merge(const void *remote, size_t rlen, const SyncUsr *loc, int nloc,
                       void **out, size_t *outlen, long long *carried_newest, int *ncarried);

/* Write the bundle's .usr entries into dir.  Without `force`, a local file newer
 * than its entry (+ slack) is kept.  Written files are stamped with stamp_ms.
 * dry_run writes nothing and only counts.  *would_overwrite = existing files
 * whose content would change.  0 ok, -1 bad bundle. */
int  sync_bundle_extract(const void *blob, size_t len, const char *dir, int force, long long stamp_ms,
                         int dry_run, int *written, int *kept, int *would_overwrite);

/* zip the game folder's .usr files into cfg_dir/backups/game-<tag>-<time>.zip.
 * 0 = made (path in out), 1 = nothing to back up, -1 = failed. */
int  sync_backup(const SyncCtx *c, const char *tag, char *out, size_t n);

/* ---- network */
int  sync_health(const SyncCtx *c);                              /* 1 = our server */
int  sync_remote_auto(const SyncCtx *c, long long *modified);    /* 0 ok (0 = no auto), else error */

/* The three --sync actions.  Messages go to stderr/stdout; return a SYNC_* code. */
int  sync_status(SyncCtx *c);
int  sync_pull(SyncCtx *c, int force);
int  sync_push(SyncCtx *c, int force);

/* the context the config describes (shared by --sync and the lifecycle hooks,
 * synclife.h).  `remember` stores an explicit key in <cfg dir>/sync-key.
 * 0, or SYNC_ERR with the reason on stderr. */
int  sync_ctx_init(SyncCtx *c, const Config *cfg, const char *cfgpath, const char *game_dir,
                   const char *cli_key, const char *cli_base, int remember);

/* main.c entry for `--sync ACTION`: builds the context from the config (+ the
 * --sync-key / --sync-base overrides), runs, returns the exit code.  Never
 * writes the config. */
int  sync_cli(const Config *cfg, const char *cfgpath, const char *game_dir,
              const char *action, int force, const char *cli_key, const char *cli_base);

#endif
