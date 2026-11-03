/* test_sync.c — save sync (#55 step 1), SDL-free and network-free: the protocol
 * core runs against an in-memory fake of the server (this file supplies its own
 * net_get/net_put/net_delete).  Covers key normalisation, the link-decision
 * refusal, the per-.USR newer-wins union, byte-for-byte pass-through of foreign
 * entries, the NAME.USR <-> name.usr case mapping, and the local backup. */
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "miniz.h"
#include "net.h"
#include "sync.h"

static int checks, failures;
static void ck(int ok, const char *fmt, ...)
{
    va_list ap; checks++;
    if (ok) return;
    failures++;
    printf("  FAIL ");
    va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    printf("\n");
}

/* ------------------------------------------------------------ fake server */
static struct {
    unsigned char *blob; size_t len; long long modified; int has;
    int healthy, puts, gets, deletes;
    char want_token[64], seen_key[80], seen_token[64];
} srv;

static int respond(NetResp *r, int status, const char *body)
{
    memset(r, 0, sizeof *r);
    r->status = status;
    if (body) { r->len = strlen(body); r->body = (unsigned char *)strdup(body); }
    return status;
}
static void note_auth(const NetAuth *au)
{
    snprintf(srv.seen_key, sizeof srv.seen_key, "%s", au && au->key ? au->key : "");
    snprintf(srv.seen_token, sizeof srv.seen_token, "%s", au && au->token ? au->token : "");
}
static int denied(const NetAuth *au) { return srv.want_token[0] && (!au || !au->token || strcmp(au->token, srv.want_token)); }
int net_supported(void) { return 1; }
int net_get(const char *url, const NetAuth *au, NetResp *r)
{
    const char *p = strstr(url, "/api/"); srv.gets++; note_auth(au);
    if (!srv.healthy) { memset(r, 0, sizeof *r); strcpy(r->error, "connection refused"); return 0; }
    if (!strcmp(p, "/api/health")) return respond(r, 200, srv.want_token[0] ? "{\"ok\": true, \"auth\": \"token\"}" : "{\"ok\": true, \"auth\": \"none\"}");
    if (denied(au)) return respond(r, 401, "token required");
    if (!strcmp(p, "/api/saves")) {
        char b[160] = "[]";
        if (srv.has) snprintf(b, sizeof b, "[{\"slot\": \"auto\", \"modified\": %lld, \"size\": %zu}]", srv.modified, srv.len);
        return respond(r, 200, b);
    }
    if (!strcmp(p, "/api/saves/auto")) {
        if (!srv.has) return respond(r, 404, "no such slot");
        respond(r, 200, NULL);
        r->body = malloc(srv.len + 1); memcpy(r->body, srv.blob, srv.len); r->body[srv.len] = 0; r->len = srv.len;
        r->modified = srv.modified; r->has_modified = 1;
        return 200;
    }
    return respond(r, 404, "nope");
}
int net_put(const char *url, const NetAuth *au, const void *body, size_t n, long long modified, NetResp *r)
{
    note_auth(au);
    if (!srv.healthy) { memset(r, 0, sizeof *r); strcpy(r->error, "connection refused"); return 0; }
    if (denied(au)) return respond(r, 401, "token required");
    if (!strstr(url, "/api/saves/auto") || !n) return respond(r, 413, "bad body size");
    free(srv.blob); srv.blob = malloc(n); memcpy(srv.blob, body, n); srv.len = n; srv.modified = modified; srv.has = 1; srv.puts++;
    return respond(r, 200, "{\"slot\": \"auto\"}");
}
int net_delete(const char *url, const NetAuth *au, NetResp *r) { (void)url; (void)au; srv.deletes++; return respond(r, 200, "{}"); }

static void srv_set(const void *blob, size_t n, long long modified)
{
    free(srv.blob); srv.blob = malloc(n); memcpy(srv.blob, blob, n); srv.len = n; srv.modified = modified; srv.has = 1;
}
static void srv_reset(void)
{
    free(srv.blob); memset(&srv, 0, sizeof srv); srv.healthy = 1;
}

/* ---------------------------------------------------------------- fixtures */
#define T0 1700000000LL                     /* even seconds: a zip can hold it exactly */

typedef struct Ent { const char *name; const void *data; size_t len; long long t; int level; } Ent;

static void *build_zip(const Ent *e, int n, size_t *len)
{
    mz_zip_archive z; memset(&z, 0, sizeof z);
    mz_zip_writer_init_heap(&z, 0, 0);
    for (int i = 0; i < n; i++) {
        time_t t = (time_t)e[i].t;
        mz_zip_writer_add_mem_ex_v2(&z, e[i].name, e[i].data, e[i].len, NULL, 0, (mz_uint)e[i].level, 0, 0, &t, NULL, 0, NULL, 0);
    }
    void *b; mz_zip_writer_finalize_heap_archive(&z, &b, len);
    mz_zip_writer_end(&z);
    return b;
}

/* entry `name` of a zip: data (malloc'd) + stat; NULL when absent */
static unsigned char *zget(const void *zip, size_t zl, const char *name, size_t *len, mz_zip_archive_file_stat *st)
{
    mz_zip_archive z; memset(&z, 0, sizeof z);
    if (!mz_zip_reader_init_mem(&z, zip, zl, 0)) return NULL;
    int idx = mz_zip_reader_locate_file(&z, name, NULL, 0);
    unsigned char *d = NULL;
    if (idx >= 0) {
        if (st) mz_zip_reader_file_stat(&z, (mz_uint)idx, st);
        d = mz_zip_reader_extract_to_heap(&z, (mz_uint)idx, len, 0);
    }
    mz_zip_reader_end(&z);
    return d;
}

static void setmt(const char *path, long long sec)
{
    struct timespec ts[2] = { { (time_t)sec, 0 }, { (time_t)sec, 0 } };
    utimensat(AT_FDCWD, path, ts, 0);
}
static long long getmt(const char *path)
{
    struct stat st; if (stat(path, &st) != 0) return -1;
    return (long long)st.st_mtim.tv_sec;
}
static void putf(const char *dir, const char *name, const void *d, size_t n, long long sec)
{
    char p[1024]; snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "wb"); fwrite(d, 1, n, f); fclose(f);
    setmt(p, sec);
}
static int readf(const char *dir, const char *name, unsigned char *buf, size_t max, size_t *n)
{
    char p[1024]; snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "rb"); if (!f) return 0;
    *n = fread(buf, 1, max, f); fclose(f); return 1;
}
static int exists(const char *dir, const char *name) { char p[1024]; snprintf(p, sizeof p, "%s/%s", dir, name); return access(p, F_OK) == 0; }

static char root[256];
static void mkroot(void)
{
    snprintf(root, sizeof root, "/tmp/zsync-test-XXXXXX");
    if (!mkdtemp(root)) { fprintf(stderr, "mkdtemp failed\n"); exit(2); }
}
static void fresh(SyncCtx *c, const char *tag)
{
    memset(c, 0, sizeof *c);
    snprintf(c->game_dir, sizeof c->game_dir, "%s/%s-game", root, tag);
    snprintf(c->cfg_dir, sizeof c->cfg_dir, "%s/%s-cfg", root, tag);
    mkdir(c->game_dir, 0777); mkdir(c->cfg_dir, 0777);
    snprintf(c->base, sizeof c->base, "http://127.0.0.1:1/");
    snprintf(c->key, sizeof c->key, "ABCD-EFGH");
}

static unsigned char USR_A[256], USR_B[256], USR_C[256], USR_D[256], USR_OLD[256];
static unsigned char SAV[5000], EXE[3000];
static void init_data(void)
{
    unsigned s = 12345;
    for (int i = 0; i < 256; i++) { USR_A[i] = (unsigned char)(i * 3); USR_B[i] = (unsigned char)(i + 7); USR_C[i] = (unsigned char)(255 - i); USR_D[i] = (unsigned char)(i ^ 0x5A); USR_OLD[i] = 0x11; }
    for (size_t i = 0; i < sizeof SAV; i++) { s = s * 1103515245u + 12345u; SAV[i] = (unsigned char)(s >> 16); }
    for (size_t i = 0; i < sizeof EXE; i++) EXE[i] = (unsigned char)(i % 251);
}

/* the web app's bundle: js-dos metadata, the DOSBox-X save state, the game, two saves */
static void *web_bundle(size_t *len, long long a_t, long long b_t)
{
    Ent e[] = {
        { ".jsdos/dosbox.conf", "[cpu]\ncycles=max\n", 17, T0, 6 },
        { "dosbox.conf", "[autoexec]\nzeliard\n", 19, T0, 6 },
        { "save/1.sav", SAV, sizeof SAV, T0 + 10, 0 },          /* stored, not deflated */
        { "ZELIARD.EXE", EXE, sizeof EXE, T0, 6 },
        { "ZELRES1.SAR", EXE, 1000, T0, 6 },
        { "HERO.USR", USR_A, 256, a_t, 6 },
        { "ZOE.USR", USR_B, 256, b_t, 6 },
    };
    return build_zip(e, 7, len);
}

/* ------------------------------------------------------------------- tests */
static void t_keys(void)
{
    printf("keys            ");
    char o[80];
    ck(sync_key_normalize("abcd", o, sizeof o) && !strcmp(o, "ABCD"), "4 chars stay bare");
    ck(sync_key_normalize("abcdefgh", o, sizeof o) && !strcmp(o, "ABCD-EFGH"), "dash every 4");
    ck(sync_key_normalize(" ab-cd ef_gh ij ", o, sizeof o) && !strcmp(o, "ABCD-EFGH-IJ"), "separators stripped: %s", o);
    ck(sync_key_normalize("abcde", o, sizeof o) && !strcmp(o, "ABCD-E"), "5 chars");
    ck(!sync_key_normalize("abc", o, sizeof o), "3 chars refused");
    ck(!sync_key_normalize("---", o, sizeof o) && !sync_key_normalize("", o, sizeof o), "nothing left refused");
    ck(sync_key_normalize("0123456789ABCDEF0123456789ABCDEF", o, sizeof o), "32 chars ok");
    ck(!sync_key_normalize("0123456789ABCDEF0123456789ABCDEFG", o, sizeof o), "33 chars refused");
    ck(sync_key_wire_ok("ABCD-EFGH") && sync_key_wire_ok("zn-0a1b_2") && !sync_key_wire_ok("a b") && !sync_key_wire_ok("") &&
       !sync_key_wire_ok("a/b"), "wire charset");
    unsigned char r[4] = { 0, 31, 32, 255 }; char k[5];
    sync_key_from_bytes(r, k);
    ck(!strcmp(k, "A99Z") || strlen(k) == 4, "generated key is 4 chars: %s", k);
    int bad = 0;
    for (int i = 0; i < 256; i++) { unsigned char q[4] = { (unsigned char)i, (unsigned char)(i * 7), (unsigned char)(i * 13), (unsigned char)(255 - i) }; sync_key_from_bytes(q, k);
        for (int j = 0; j < 4; j++) if (strchr("IO01", k[j]) || !strchr("ABCDEFGHJKLMNPQRSTUVWXYZ23456789", k[j])) bad++; }
    ck(!bad, "generated keys avoid I O 0 1");

    SyncCtx c; fresh(&c, "keys");
    ck(sync_key_resolve(&c, "wxyz-1234") == 0 && !strcmp(c.key, "WXYZ-1234"), "explicit key is normalised");
    ck(sync_key_resolve(&c, "ab") == -1, "invalid explicit key is an error, not silently replaced");
    ck(!exists(c.cfg_dir, "sync-key"), "an explicit key is not stored by resolve");
    ck(sync_key_resolve(&c, "") == 0 && strlen(c.key) == 4 && exists(c.cfg_dir, "sync-key"), "no key: one is generated and stored");
    char first[80]; snprintf(first, sizeof first, "%s", c.key);
    SyncCtx d = c; d.key[0] = 0;
    ck(sync_key_resolve(&d, NULL) == 0 && !strcmp(d.key, first), "the stored key is read back");
    unsigned char b[64]; size_t n;
    struct stat st; char p[1100]; snprintf(p, sizeof p, "%s/sync-key", c.cfg_dir); stat(p, &st);
    ck((st.st_mode & 077) == 0, "sync-key file is private");
    putf(c.cfg_dir, "sync-key", "zn-legacy_key-123\n", 18, T0);
    ck(sync_key_resolve(&d, "") == 0 && !strcmp(d.key, "zn-legacy_key-123"), "legacy zn- key used verbatim");
    putf(c.cfg_dir, "sync-key", "bad key!\n", 9, T0);
    ck(sync_key_resolve(&d, "") == -1, "a corrupt key file is refused");
    (void)b; (void)n;
}

static void t_decide(void)
{
    printf("decisions       ");
    ck(sync_decide_pull(0, 0, 5) == SD_NO_REMOTE, "no remote: nothing to pull");
    ck(sync_decide_pull(0, 100, 0) == SD_NEEDS_DECISION, "unlinked + remote save: refuse to pull even into an empty folder");
    ck(sync_decide_pull(0, 100, 999) == SD_NEEDS_DECISION, "unlinked: refuse regardless of clocks");
    ck(sync_decide_pull(1, 100, 50) == SD_GO, "linked, remote newer: pull");
    ck(sync_decide_pull(1, 100, 100) == SD_NOT_NEWER && sync_decide_pull(1, 100, 200) == SD_NOT_NEWER, "linked, not newer: no pull");
    ck(sync_decide_push(1, 100, 0) == SD_NO_LOCAL, "nothing local: no push");
    ck(sync_decide_push(0, 100, 500) == SD_NEEDS_DECISION, "unlinked + remote save: refuse to push over it");
    ck(sync_decide_push(0, 0, 500) == SD_GO, "unlinked, empty server: push is fine");
    ck(sync_decide_push(1, 100, 500) == SD_GO && sync_decide_push(1, 500, 500) == SD_NOT_NEWER && sync_decide_push(1, 900, 500) == SD_NOT_NEWER, "linked newer-wins");
}

static void t_link(void)
{
    printf("link record     ");
    SyncCtx c; fresh(&c, "link"); long long last = -1;
    ck(!sync_link_get(&c, &last) && last == 0, "fresh: not linked");
    ck(sync_link_mark(&c, 1700000123456LL) == 0 && sync_link_get(&c, &last) && last == 1700000123456LL, "mark links and records");
    SyncCtx o = c; snprintf(o.key, sizeof o.key, "ZZZZ");
    ck(!sync_link_get(&o, &last), "another key is not linked");
    sync_link_mark(&o, 42);
    ck(sync_link_get(&c, &last) && last == 1700000123456LL && sync_link_get(&o, &last) && last == 42, "two keys coexist");
    sync_link_mark(&c, 99);
    ck(sync_link_get(&c, &last) && last == 99, "re-mark updates");
    sync_link_unlink(&c);
    ck(!sync_link_get(&c, &last) && sync_link_get(&o, &last) && last == 42, "unlink forgets one key only");
    /* a file in exactly the Go client's MarshalIndent form */
    putf(c.cfg_dir, "sync-links.json", "{\n  \"keys\": {\n    \"ABCD-EFGH\": {\n      \"last_synced\": 1699999999000\n    },\n    \"QQQQ\": {\n      \"last_synced\": 7\n    }\n  }\n}\n", 118, T0);
    ck(sync_link_get(&c, &last) && last == 1699999999000LL, "reads the Go client's file");
    SyncCtx q = c; snprintf(q.key, sizeof q.key, "QQQQ");
    ck(sync_link_get(&q, &last) && last == 7, "second key in the Go file");
    putf(c.cfg_dir, "sync-links.json", "not json", 8, T0);
    ck(!sync_link_get(&c, &last), "garbage file = not linked");
}

static void t_names(void)
{
    printf("case mapping    ");
    char o[64];
    sync_usr_local_name("HERO.USR", o, sizeof o); ck(!strcmp(o, "hero.usr"), "NAME.USR -> name.usr: %s", o);
    sync_usr_zip_name("hero.usr", o, sizeof o);   ck(!strcmp(o, "HERO.USR"), "name.usr -> NAME.USR: %s", o);
    sync_usr_zip_name("HeRo.UsR", o, sizeof o);   ck(!strcmp(o, "HERO.USR"), "mixed case -> upper");
    ck(sync_usr_entry_ok("A.USR") && sync_usr_entry_ok("a.usr") && sync_usr_entry_ok("Zoe.Usr"), "any case is a save");
    ck(!sync_usr_entry_ok("save/1.sav") && !sync_usr_entry_ok(".jsdos/dosbox.conf") && !sync_usr_entry_ok("ZELIARD.EXE") &&
       !sync_usr_entry_ok("sub/X.USR") && !sync_usr_entry_ok("..\\X.USR") && !sync_usr_entry_ok(".USR") && !sync_usr_entry_ok("a:b.usr"), "not saves / unsafe names");
    SyncCtx c; fresh(&c, "names");
    putf(c.game_dir, "Mixed.Usr", USR_A, 256, T0);
    putf(c.game_dir, "notes.txt", "x", 1, T0);
    SyncUsr *l; int n; sync_scan_local(c.game_dir, &l, &n);
    ck(n == 1 && !strcmp(l[0].name, "Mixed.Usr") && l[0].len == 256 && l[0].mtime_ms == T0 * 1000, "scan lists only .usr in any case");
    sync_free_usr(l, n);
}

static void t_merge(void)
{
    printf("union           ");
    size_t rl; void *remote = web_bundle(&rl, T0 + 1000, T0 + 200);     /* HERO newer remote, ZOE old remote */
    SyncUsr loc[3]; memset(loc, 0, sizeof loc);
    strcpy(loc[0].name, "hero.usr"); loc[0].data = USR_OLD; loc[0].len = 256; loc[0].mtime_ms = (T0 + 500) * 1000;   /* stale */
    strcpy(loc[1].name, "zoe.usr");  loc[1].data = USR_C;   loc[1].len = 256; loc[1].mtime_ms = (T0 + 600) * 1000;   /* ahead */
    strcpy(loc[2].name, "new.usr");  loc[2].data = USR_D;   loc[2].len = 256; loc[2].mtime_ms = (T0 + 700) * 1000;   /* local only */
    void *out; size_t ol; long long carried; int nc;
    ck(sync_bundle_merge(remote, rl, loc, 3, &out, &ol, &carried, &nc) == 0, "merge runs");
    size_t n; unsigned char *d;
    d = zget(out, ol, "HERO.USR", &n, NULL); ck(d && n == 256 && !memcmp(d, USR_A, 256), "stale local save loses to the newer server copy"); free(d);
    d = zget(out, ol, "ZOE.USR", &n, NULL);  ck(d && n == 256 && !memcmp(d, USR_C, 256), "newer local save replaces the server's"); free(d);
    d = zget(out, ol, "NEW.USR", &n, NULL);  ck(d && n == 256 && !memcmp(d, USR_D, 256), "local-only save added, upper-cased"); free(d);
    ck(nc == 1 && carried == (T0 + 1000) * 1000, "one server save carried, newest %lld", carried);
    mz_zip_archive_file_stat st;
    d = zget(out, ol, "HERO.USR", &n, &st); ck(st.m_time == (time_t)(T0 + 1000), "carried entry keeps the server's mtime"); free(d);
    d = zget(out, ol, "ZOE.USR", &n, &st);  ck(st.m_time == (time_t)(T0 + 600), "local entry carries the file's mtime"); free(d);

    /* foreign entries: bytes identical, and the stored/deflated choice and CRC survive */
    const char *foreign[] = { ".jsdos/dosbox.conf", "dosbox.conf", "save/1.sav", "ZELIARD.EXE", "ZELRES1.SAR" };
    int same = 1, meta = 1;
    for (int i = 0; i < 5; i++) {
        size_t a, b; mz_zip_archive_file_stat sa, sb;
        unsigned char *x = zget(remote, rl, foreign[i], &a, &sa), *y = zget(out, ol, foreign[i], &b, &sb);
        if (!x || !y || a != b || memcmp(x, y, a)) same = 0;
        else if (sa.m_method != sb.m_method || sa.m_crc32 != sb.m_crc32 || sa.m_comp_size != sb.m_comp_size || sa.m_time != sb.m_time) meta = 0;
        free(x); free(y);
    }
    ck(same, "foreign entries (save/1.sav, .jsdos/*, game files) pass through byte for byte");
    ck(meta, "...with method, CRC, compressed size and mtime unchanged");
    free(out);

    /* tie inside the slack: local keeps its own copy */
    loc[0].mtime_ms = (T0 + 1000) * 1000 - 1500;
    sync_bundle_merge(remote, rl, loc, 1, &out, &ol, &carried, &nc);
    d = zget(out, ol, "HERO.USR", &n, NULL); ck(d && !memcmp(d, USR_OLD, 256) && nc == 1 /* ZOE carried */, "remote only wins when more than the 2 s slack ahead"); free(d);
    free(out);
    /* local files only, no remote bundle at all */
    ck(sync_bundle_merge(NULL, 0, loc, 3, &out, &ol, &carried, &nc) == 0 && zget(out, ol, "NEW.USR", &n, NULL) && nc == 0, "no remote: just the local saves");
    free(out);
    ck(sync_bundle_merge("not a zip at all", 16, loc, 1, &out, &ol, &carried, &nc) == -1, "an unreadable remote bundle is refused (never replaced)");
    free(remote);
}

static void t_extract(void)
{
    printf("extract         ");
    SyncCtx c; fresh(&c, "ext");
    size_t rl; void *b = web_bundle(&rl, T0 + 100, T0 + 200);
    int w, k, ov;
    ck(sync_bundle_extract(b, rl, c.game_dir, 0, (T0 + 1000) * 1000, 1, &w, &k, &ov) == 0 && w == 2 && !exists(c.game_dir, "hero.usr"), "dry run writes nothing");
    ck(sync_bundle_extract(b, rl, c.game_dir, 0, (T0 + 1000) * 1000, 0, &w, &k, &ov) == 0 && w == 2 && k == 0 && ov == 0, "extract writes both saves");
    unsigned char buf[300]; size_t n;
    ck(readf(c.game_dir, "hero.usr", buf, sizeof buf, &n) && n == 256 && !memcmp(buf, USR_A, 256), "NAME.USR lands as name.usr, bytes intact");
    ck(!exists(c.game_dir, "HERO.USR") && !exists(c.game_dir, "ZELIARD.EXE") && !exists(c.game_dir, "dosbox.conf") && !exists(c.game_dir, "save"), "nothing but .usr is written");
    char p[1100]; snprintf(p, sizeof p, "%s/hero.usr", c.game_dir);
    ck(getmt(p) == T0 + 1000, "pulled files are stamped with the server time");
    /* an existing file in another case is reused, not duplicated */
    SyncCtx e; fresh(&e, "ext2");
    putf(e.game_dir, "Hero.Usr", USR_OLD, 256, T0);
    ck(sync_bundle_extract(b, rl, e.game_dir, 0, (T0 + 1000) * 1000, 0, &w, &k, &ov) == 0 && ov == 1 && exists(e.game_dir, "Hero.Usr") && !exists(e.game_dir, "hero.usr"), "existing case is kept: %d", ov);
    ck(readf(e.game_dir, "Hero.Usr", buf, sizeof buf, &n) && !memcmp(buf, USR_A, 256), "...and overwritten with the newer save");
    /* a newer local save is kept unless forced */
    SyncCtx f; fresh(&f, "ext3");
    putf(f.game_dir, "hero.usr", USR_OLD, 256, T0 + 5000);
    ck(sync_bundle_extract(b, rl, f.game_dir, 0, (T0 + 1000) * 1000, 0, &w, &k, &ov) == 0 && k == 1 && w == 1, "newer local save kept, the other written");
    ck(readf(f.game_dir, "hero.usr", buf, sizeof buf, &n) && !memcmp(buf, USR_OLD, 256), "...untouched");
    snprintf(p, sizeof p, "%s/hero.usr", f.game_dir);
    ck(getmt(p) == T0 + 5000, "a kept save is not back-dated");
    ck(sync_bundle_extract(b, rl, f.game_dir, 1, (T0 + 1000) * 1000, 0, &w, &k, &ov) == 0 && k == 0 && ov == 1, "force takes the server's copy");
    ck(readf(f.game_dir, "hero.usr", buf, sizeof buf, &n) && !memcmp(buf, USR_A, 256), "...whole");
    ck(sync_bundle_extract("junk", 4, f.game_dir, 0, 0, 0, NULL, NULL, NULL) == -1, "junk bundle refused");
    /* names a DOS lister could not show are skipped, not written */
    Ent le[] = { { "TOOLONGNAME.USR", USR_A, 256, T0, 6 }, { "OK.USR", USR_B, 256, T0, 6 } };
    size_t ll; void *lb = build_zip(le, 2, &ll);
    SyncCtx g; fresh(&g, "ext4");
    ck(sync_bundle_extract(lb, ll, g.game_dir, 0, T0 * 1000, 0, &w, &k, &ov) == 0 && w == 1 && exists(g.game_dir, "ok.usr") && !exists(g.game_dir, "toolongname.usr"), "9+ character names skipped");
    free(lb); free(b);
}

static void t_backup(void)
{
    printf("backup          ");
    SyncCtx c; fresh(&c, "bak");
    char out[1200];
    ck(sync_backup(&c, "test", out, sizeof out) == 1, "nothing to back up reports 1");
    putf(c.game_dir, "hero.usr", USR_A, 256, T0);
    putf(c.game_dir, "zoe.usr", USR_B, 256, T0 + 4);
    putf(c.game_dir, "ZELIARD.EXE", EXE, sizeof EXE, T0);
    ck(sync_backup(&c, "before-cloud", out, sizeof out) == 0, "backup made");
    ck(strstr(out, "/backups/game-before-cloud-") && strstr(out, ".zip"), "named under backups/: %s", out);
    size_t n; unsigned char *z = (unsigned char *)0; FILE *f = fopen(out, "rb");
    ck(f != NULL, "file exists");
    if (f) {
        fseek(f, 0, SEEK_END); long zl = ftell(f); fseek(f, 0, SEEK_SET);
        z = malloc((size_t)zl); if (fread(z, 1, (size_t)zl, f) != (size_t)zl) ck(0, "read back the backup"); fclose(f);
        unsigned char *d = zget(z, (size_t)zl, "hero.usr", &n, NULL); ck(d && n == 256 && !memcmp(d, USR_A, 256), "hero.usr in the zip, identical"); free(d);
        d = zget(z, (size_t)zl, "zoe.usr", &n, NULL); ck(d && !memcmp(d, USR_B, 256), "zoe.usr in the zip, identical"); free(d);
        d = zget(z, (size_t)zl, "ZELIARD.EXE", &n, NULL); ck(d == NULL, "only the saves are backed up"); free(d);
        free(z);
    }
    char out2[1200];
    ck(sync_backup(&c, "before-cloud", out2, sizeof out2) == 0 && strcmp(out, out2), "a second backup in the same second does not overwrite the first");
}

static void t_flow_push_pull(void)
{
    printf("push/pull       ");
    SyncCtx c; fresh(&c, "flow");
    srv_reset();
    putf(c.game_dir, "hero.usr", USR_OLD, 256, T0 + 500);
    size_t rl; void *b = web_bundle(&rl, T0 + 100, T0 + 200);
    srv_set(b, rl, (T0 + 300) * 1000);

    /* unlinked key with a remote save: both directions refuse, nothing moves */
    int puts0 = srv.puts;
    ck(sync_push(&c, 0) == SYNC_DECIDE, "push refused on an unlinked key with a remote save");
    ck(srv.puts == puts0, "...and nothing was uploaded");
    ck(sync_pull(&c, 0) == SYNC_DECIDE, "pull refused too");
    unsigned char buf[300]; size_t n;
    ck(readf(c.game_dir, "hero.usr", buf, sizeof buf, &n) && !memcmp(buf, USR_OLD, 256) && !exists(c.game_dir, "zoe.usr"), "...local folder untouched");
    long long last;
    ck(!sync_link_get(&c, &last), "refusal does not link");
    ck(!strcmp(srv.seen_key, "ABCD-EFGH"), "the key travels as X-Client-Id");

    /* --force push: keeps the device's saves, preserves the web entries */
    ck(sync_push(&c, 1) == SYNC_OK && srv.puts == puts0 + 1, "push --force uploads");
    ck(sync_link_get(&c, &last) && last == (T0 + 500) * 1000, "...and links at the pushed time");
    unsigned char *d = zget(srv.blob, srv.len, "HERO.USR", &n, NULL);
    ck(d && !memcmp(d, USR_OLD, 256), "the pushed bundle holds this device's save"); free(d);
    d = zget(srv.blob, srv.len, "save/1.sav", &n, NULL); ck(d && n == sizeof SAV && !memcmp(d, SAV, n), "web quicksave survives the push"); free(d);
    d = zget(srv.blob, srv.len, "ZOE.USR", &n, NULL); ck(d && !memcmp(d, USR_B, 256), "a save this device never saw is carried"); free(d);
    ck(srv.modified == (T0 + 500) * 1000, "uploaded with X-Save-Modified = newest local mtime");

    /* linked, nothing new */
    ck(sync_push(&c, 0) == SYNC_OK && srv.puts == puts0 + 1, "linked + not newer: no second push");
    ck(sync_pull(&c, 0) == SYNC_OK, "linked + not newer: pull is a no-op");

    /* another device writes (web app): remote newer, linked -> pull */
    void *b2 = web_bundle(&rl, T0 + 900, T0 + 950);
    srv_set(b2, rl, (T0 + 1000) * 1000);
    ck(sync_pull(&c, 0) == SYNC_OK, "linked + server newer: pull");
    ck(readf(c.game_dir, "hero.usr", buf, sizeof buf, &n) && !memcmp(buf, USR_A, 256), "pulled hero.usr");
    ck(readf(c.game_dir, "zoe.usr", buf, sizeof buf, &n) && !memcmp(buf, USR_B, 256), "pulled zoe.usr");
    ck(sync_link_get(&c, &last) && last == (T0 + 1000) * 1000, "link advances to the server time");
    ck(sync_pull(&c, 0) == SYNC_OK, "and a second pull settles");
    char bd[1100]; snprintf(bd, sizeof bd, "%s/backups", c.cfg_dir);
    ck(access(bd, F_OK) == 0, "an overwrite made a backup first");
    free(b2); free(b);

    /* force pull backs up and replaces */
    SyncCtx f; fresh(&f, "flow2");
    putf(f.game_dir, "mine.usr", USR_C, 256, T0 + 5000);
    ck(sync_pull(&f, 1) == SYNC_OK && exists(f.game_dir, "hero.usr") && exists(f.game_dir, "mine.usr"), "pull --force on a fresh folder, linked");
    snprintf(bd, sizeof bd, "%s/backups", f.cfg_dir);
    ck(access(bd, F_OK) == 0, "pull --force backed up first");
    ck(sync_link_get(&f, &last) && last == srv.modified, "...and linked");

    /* offline / no server bundle / token */
    srv.healthy = 0;
    ck(sync_pull(&f, 0) == SYNC_OFFLINE && sync_push(&f, 0) == SYNC_OFFLINE, "offline: skipped with SYNC_OFFLINE");
    srv.healthy = 1;
    SyncCtx e; fresh(&e, "flow3"); snprintf(e.key, sizeof e.key, "NEWKEY");
    srv.has = 0;
    putf(e.game_dir, "me.usr", USR_A, 256, T0);
    ck(sync_push(&e, 0) == SYNC_NOBUNDLE && srv.puts == puts0 + 1, "empty server key: the port refuses to create `auto`");
    ck(sync_pull(&e, 0) == SYNC_OK && sync_pull(&e, 1) == SYNC_ERR, "empty server: pull is a no-op, pull --force an error");
    srv.has = 1; srv_set(srv.blob, srv.len, srv.modified);
    snprintf(srv.want_token, sizeof srv.want_token, "s3cret");
    ck(sync_push(&f, 1) == SYNC_ERR, "server wants a token: no token = error");
    snprintf(f.token, sizeof f.token, "s3cret");
    ck(sync_push(&f, 1) == SYNC_OK && !strcmp(srv.seen_token, "s3cret"), "token is sent as the bearer");
    srv_reset();
}

int main(void)
{
    mkroot(); init_data(); srv_reset();
    t_keys();        printf("ok\n");
    t_decide();      printf("ok\n");
    t_link();        printf("ok\n");
    t_names();       printf("ok\n");
    t_merge();       printf("ok\n");
    t_extract();     printf("ok\n");
    t_backup();      printf("ok\n");
    t_flow_push_pull(); printf("ok\n");
    char cmd[300]; snprintf(cmd, sizeof cmd, "rm -rf %s", root);
    if (system(cmd)) {}
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
