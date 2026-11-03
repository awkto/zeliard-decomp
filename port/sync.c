/* sync.c — save sync, step 1 (#55).  See sync.h and docs/SYNC.md.
 *
 * A port of the behaviour of zeliard-wasm's native/launcher/core/sync.go and
 * link.go, not of the code: same endpoints, key format, link-record schema and
 * newer-wins-per-.USR union, restricted to what the port can honour. */
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#include <sys/utime.h>
#define SY_MKDIR(d) _mkdir(d)
#else
#include <fcntl.h>
#include <unistd.h>
#define SY_MKDIR(d) mkdir((d), 0777)
#endif
#include "miniz.h"
#include "net.h"
#include "sync.h"

#define USR_MAX (1u << 20)              /* a .usr is 256 bytes; anything past 1 MiB is not one */
#define BUNDLE_MAX (16u << 20)          /* the server's default SAVES_MAX_BYTES */

static void logs(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    fputs("zeliard: sync: ", stderr); vfprintf(stderr, fmt, ap); fputc('\n', stderr);
    va_end(ap);
}

/* ------------------------------------------------------------ small utils */
static int ci_eq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}

static int has_usr_ext(const char *s)
{
    size_t n = strlen(s);
    return n > 4 && ci_eq(s + n - 4, ".usr");
}

static void mkdir_p(const char *path)
{
    char t[SYNC_PATH]; snprintf(t, sizeof t, "%s", path);
    for (char *p = t + 1; *p; p++)
        if (*p == '/' || *p == '\\') { char ch = *p; *p = 0; SY_MKDIR(t); *p = ch; }
    SY_MKDIR(t);
}

static unsigned char *read_all(const char *path, size_t *n, size_t max)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t cap = 4096, len = 0;
    unsigned char *b = malloc(cap + 1);
    while (b) {
        size_t r = fread(b + len, 1, cap - len, f);
        len += r;
        if (len > max) { free(b); b = NULL; break; }
        if (len < cap) break;
        cap *= 2;
        unsigned char *q = realloc(b, cap + 1);
        if (!q) { free(b); b = NULL; break; }
        b = q;
    }
    int bad = ferror(f);
    fclose(f);
    if (bad && b) { free(b); b = NULL; }
    if (b) { b[len] = 0; *n = len; }
    return b;
}

/* tmp + rename, so an interrupted pull never leaves half a save */
static int write_atomic(const char *path, const void *data, size_t n)
{
    char tmp[SYNC_PATH + 8]; snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return -1;
    if ((n && fwrite(data, 1, n, f) != n) || fclose(f) != 0) { remove(tmp); return -1; }
#ifdef _WIN32
    remove(path);
#endif
    if (rename(tmp, path) != 0) { remove(tmp); return -1; }
    return 0;
}

static long long st_mtime_ms(const struct stat *st)
{
#if defined(__APPLE__)
    return (long long)st->st_mtimespec.tv_sec * 1000 + st->st_mtimespec.tv_nsec / 1000000;
#elif defined(_WIN32)
    return (long long)st->st_mtime * 1000;
#else
    return (long long)st->st_mtim.tv_sec * 1000 + st->st_mtim.tv_nsec / 1000000;
#endif
}

static void set_mtime_ms(const char *path, long long ms)
{
#ifdef _WIN32
    struct _utimbuf u; u.actime = u.modtime = (time_t)((ms + 999) / 1000);   /* round up: never look older */
    _utime(path, &u);
#else
    struct timespec ts[2];
    ts[0].tv_sec = ts[1].tv_sec = (time_t)(ms / 1000);
    ts[0].tv_nsec = ts[1].tv_nsec = (long)(ms % 1000) * 1000000L;
    utimensat(AT_FDCWD, path, ts, 0);
#endif
}

/* ------------------------------------------------------------------- keys */
int sync_key_normalize(const char *in, char *out, size_t n)
{
    char clean[40]; size_t c = 0;
    for (; in && *in; in++) {
        int ch = toupper((unsigned char)*in);
        if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
            if (c >= 33) return 0;              /* > 32 */
            clean[c++] = (char)ch;
        }
    }
    if (c < 4 || c > 32) return 0;
    size_t o = 0;
    if (n < c + c / 4 + 1) return 0;
    for (size_t i = 0; i < c; i++) {
        if (i > 0 && i % 4 == 0) out[o++] = '-';
        out[o++] = clean[i];
    }
    out[o] = 0;
    return 1;
}

int sync_key_wire_ok(const char *k)
{
    size_t n = k ? strlen(k) : 0;
    if (n < 1 || n > 64) return 0;
    for (; *k; k++)
        if (!isalnum((unsigned char)*k) && *k != '_' && *k != '-') return 0;
    return 1;
}

void sync_key_from_bytes(const unsigned char rnd[4], char out[5])
{
    static const char alpha[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";   /* 32: no I O 0 1 */
    for (int i = 0; i < 4; i++) out[i] = alpha[rnd[i] % 32];
    out[4] = 0;
}

static void key_path(const SyncCtx *c, char *out, size_t n) { snprintf(out, n, "%s/sync-key", c->cfg_dir); }

int sync_key_store(const SyncCtx *c, const char *key)
{
    char p[SYNC_PATH + 16], body[96];
    mkdir_p(c->cfg_dir);
    key_path(c, p, sizeof p);
    snprintf(body, sizeof body, "%s\n", key);
    if (write_atomic(p, body, strlen(body)) != 0) return -1;
#ifndef _WIN32
    chmod(p, 0600);
#endif
    return 0;
}

static void random_bytes(unsigned char *b, size_t n)
{
    FILE *f = fopen("/dev/urandom", "rb");
    size_t got = f ? fread(b, 1, n, f) : 0;
    if (f) fclose(f);
    if (got != n) {                             /* no urandom (Windows): clock + address mix */
        unsigned s = (unsigned)time(NULL) ^ (unsigned)(uintptr_t)b ^ (unsigned)clock();
        srand(s);
        for (size_t i = 0; i < n; i++) b[i] = (unsigned char)(rand() >> 7);
    }
}

int sync_key_resolve(SyncCtx *c, const char *explicit_key)
{
    char tmp[80];
    if (explicit_key && *explicit_key) {
        if (!sync_key_normalize(explicit_key, tmp, sizeof tmp)) {
            logs("sync key \"%s\" is not valid (4-32 letters or digits)", explicit_key);
            return -1;
        }
        snprintf(c->key, sizeof c->key, "%s", tmp);
        return 0;
    }
    char p[SYNC_PATH + 16]; size_t n;
    key_path(c, p, sizeof p);
    unsigned char *b = read_all(p, &n, 256);
    if (b) {                                    /* legacy zn-... keys are used verbatim */
        char *s = (char *)b;
        while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
        char *e = s + strlen(s);
        while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
        int ok = *s && sync_key_wire_ok(s) && strlen(s) < sizeof c->key, nonempty = *s != 0;
        if (ok) snprintf(c->key, sizeof c->key, "%s", s);
        free(b);
        if (ok) return 0;
        if (nonempty) { logs("%s does not hold a valid key", p); return -1; }
    }
    unsigned char r[4]; char k[5];
    random_bytes(r, 4);
    sync_key_from_bytes(r, k);
    if (sync_key_store(c, k) != 0) { logs("cannot store a new key in %s", p); return -1; }
    snprintf(c->key, sizeof c->key, "%s", k);
    return 0;
}

/* ------------------------------------------------------------ link record */
typedef struct LinkEnt { char key[80]; long long last; } LinkEnt;
#define LINK_MAX 64

static void link_path(const SyncCtx *c, char *out, size_t n) { snprintf(out, n, "%s/sync-links.json", c->cfg_dir); }

/* Reads {"keys": {"KEY": {"last_synced": N}, ...}} -- the Go client's file. */
static int link_load(const SyncCtx *c, LinkEnt *e)
{
    char p[SYNC_PATH + 24]; size_t n; int cnt = 0;
    link_path(c, p, sizeof p);
    unsigned char *b = read_all(p, &n, 1u << 20);
    if (!b) return 0;
    const char *s = strstr((char *)b, "\"keys\"");
    s = s ? strchr(s, '{') : NULL;
    while (s && cnt < LINK_MAX) {
        s++;
        const char *q = strchr(s, '"');
        const char *close = strchr(s, '}');
        if (!q) break;
        /* the keys object ends at the first '}' that comes before any further key name */
        if (close && close < q) break;
        const char *qe = strchr(q + 1, '"');
        if (!qe || (size_t)(qe - q - 1) >= sizeof e[0].key) break;
        memcpy(e[cnt].key, q + 1, (size_t)(qe - q - 1)); e[cnt].key[qe - q - 1] = 0;
        const char *ob = strchr(qe, '{'), *oe = ob ? strchr(ob, '}') : NULL;
        if (!ob || !oe) break;
        e[cnt].last = 0;
        const char *ls = strstr(ob, "\"last_synced\"");
        if (ls && ls < oe) { ls = strchr(ls, ':'); if (ls) e[cnt].last = strtoll(ls + 1, NULL, 10); }
        cnt++;
        s = oe + 1;
        while (*s == ' ' || *s == '\n' || *s == '\t' || *s == '\r') s++;
        if (*s != ',') break;
    }
    free(b);
    return cnt;
}

static int link_save(const SyncCtx *c, const LinkEnt *e, int n)
{
    char p[SYNC_PATH + 24];
    size_t cap = 256 + (size_t)n * 160; char *b = malloc(cap); size_t o = 0;
    if (!b) return -1;
    o += (size_t)snprintf(b + o, cap - o, "{\n  \"keys\": {");
    for (int i = 0; i < n; i++)
        o += (size_t)snprintf(b + o, cap - o, "%s\n    \"%s\": {\n      \"last_synced\": %lld\n    }",
                              i ? "," : "", e[i].key, e[i].last);
    o += (size_t)snprintf(b + o, cap - o, "%s}\n}\n", n ? "\n  " : "");
    mkdir_p(c->cfg_dir);
    link_path(c, p, sizeof p);
    int rc = write_atomic(p, b, o);
    free(b);
#ifndef _WIN32
    if (rc == 0) chmod(p, 0600);
#endif
    return rc;
}

int sync_link_get(const SyncCtx *c, long long *last)
{
    LinkEnt e[LINK_MAX]; int n = link_load(c, e);
    for (int i = 0; i < n; i++) if (!strcmp(e[i].key, c->key)) { if (last) *last = e[i].last; return 1; }
    if (last) *last = 0;
    return 0;
}

int sync_link_mark(const SyncCtx *c, long long remote)
{
    LinkEnt e[LINK_MAX]; int n = link_load(c, e), i;
    for (i = 0; i < n; i++) if (!strcmp(e[i].key, c->key)) break;
    if (i == n) { if (n >= LINK_MAX) return -1; snprintf(e[i].key, sizeof e[i].key, "%s", c->key); n++; }
    e[i].last = remote;
    return link_save(c, e, n);
}

int sync_link_unlink(const SyncCtx *c)
{
    LinkEnt e[LINK_MAX]; int n = link_load(c, e), o = 0;
    for (int i = 0; i < n; i++) if (strcmp(e[i].key, c->key)) e[o++] = e[i];
    return link_save(c, e, o);
}

/* -------------------------------------------------------------- decisions */
int sync_decide_pull(int linked, long long remote, long long local)
{
    if (remote == 0) return SD_NO_REMOTE;
    if (!linked) return SD_NEEDS_DECISION;      /* never a coin toss (link.go) */
    return remote > local ? SD_GO : SD_NOT_NEWER;
}

int sync_decide_push(int linked, long long remote, long long local)
{
    if (local == 0) return SD_NO_LOCAL;
    if (remote != 0 && !linked) return SD_NEEDS_DECISION;
    return local > remote ? SD_GO : SD_NOT_NEWER;
}

/* ------------------------------------------------------ names and the scan */
int sync_usr_entry_ok(const char *z)
{
    if (!z || !has_usr_ext(z)) return 0;
    if (z[0] == '.') return 0;
    for (const char *p = z; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        if (ch < 0x21 || ch > 0x7E || strchr("/\\:*?\"<>|", ch)) return 0;
    }
    return 1;
}

void sync_usr_local_name(const char *z, char *out, size_t n)
{
    size_t i = 0;
    for (; z[i] && i + 1 < n; i++) out[i] = (char)tolower((unsigned char)z[i]);
    out[i] = 0;
}

void sync_usr_zip_name(const char *l, char *out, size_t n)
{
    size_t i = 0;
    for (; l[i] && i + 1 < n; i++) out[i] = (char)toupper((unsigned char)l[i]);
    out[i] = 0;
}

static int cmp_usr(const void *a, const void *b) { return strcmp(((const SyncUsr *)a)->name, ((const SyncUsr *)b)->name); }

int sync_scan_local(const char *dir, SyncUsr **out, int *n)
{
    *out = NULL; *n = 0;
    DIR *d = opendir(dir && *dir ? dir : ".");
    if (!d) return -1;
    SyncUsr *l = NULL; int cnt = 0, cap = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!sync_usr_entry_ok(e->d_name) || strlen(e->d_name) >= sizeof l[0].name) continue;
        char path[SYNC_PATH + 280]; struct stat st;
        snprintf(path, sizeof path, "%s/%s", dir && *dir ? dir : ".", e->d_name);
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        size_t len;
        unsigned char *data = read_all(path, &len, USR_MAX);
        if (!data) continue;
        if (cnt == cap) {
            cap = cap ? cap * 2 : 8;
            SyncUsr *q = realloc(l, (size_t)cap * sizeof *l);
            if (!q) { free(data); break; }
            l = q;
        }
        memset(&l[cnt], 0, sizeof l[cnt]);
        snprintf(l[cnt].name, sizeof l[cnt].name, "%s", e->d_name);
        l[cnt].data = data; l[cnt].len = len; l[cnt].mtime_ms = st_mtime_ms(&st);
        cnt++;
    }
    closedir(d);
    if (cnt) qsort(l, (size_t)cnt, sizeof *l, cmp_usr);
    *out = l; *n = cnt;
    return 0;
}

void sync_free_usr(SyncUsr *l, int n)
{
    for (int i = 0; i < n; i++) free(l[i].data);
    free(l);
}

long long sync_local_modified(const SyncUsr *l, int n)
{
    long long m = 0;
    for (int i = 0; i < n; i++) if (l[i].mtime_ms > m) m = l[i].mtime_ms;
    return m;
}

/* ---------------------------------------------------------------- bundles */
typedef struct Row { char name[64]; unsigned char *data; size_t len; long long mtime_ms; int own_data; int carried; } Row;

static int cmp_row(const void *a, const void *b) { return strcmp(((const Row *)a)->name, ((const Row *)b)->name); }

static int row_add_zip(mz_zip_archive *zr, mz_uint i, Row **rows, int *n, int *cap)
{
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(zr, i, &st)) return 0;
    if (st.m_uncomp_size > USR_MAX || strlen(st.m_filename) >= sizeof (*rows)[0].name) return 0;
    size_t len = 0;
    unsigned char *d = mz_zip_reader_extract_to_heap(zr, i, &len, 0);
    if (!d) return 0;
    char zn[64]; sync_usr_zip_name(st.m_filename, zn, sizeof zn);
    for (int k = 0; k < *n; k++)                /* same name twice: the later one wins */
        if (!strcmp((*rows)[k].name, zn)) {
            free((*rows)[k].data); (*rows)[k].data = d; (*rows)[k].len = len;
            (*rows)[k].mtime_ms = (long long)st.m_time * 1000;
            return 1;
        }
    if (*n == *cap) {
        *cap = *cap ? *cap * 2 : 8;
        Row *q = realloc(*rows, (size_t)*cap * sizeof **rows);
        if (!q) { free(d); return 0; }
        *rows = q;
    }
    Row *r = &(*rows)[(*n)++]; memset(r, 0, sizeof *r);
    snprintf(r->name, sizeof r->name, "%s", zn);
    r->data = d; r->len = len; r->mtime_ms = (long long)st.m_time * 1000; r->own_data = 1;
    return 1;
}

int sync_bundle_merge(const void *remote, size_t rlen, const SyncUsr *loc, int nloc,
                      void **out, size_t *outlen, long long *carried_newest, int *ncarried)
{
    mz_zip_archive zr, zw; int have_r = 0, rc = -1;
    Row *rem = NULL; int nrem = 0, caprem = 0;
    Row *res = NULL; int nres = 0;
    long long cn = 0; int nc = 0;
    memset(&zr, 0, sizeof zr); memset(&zw, 0, sizeof zw);
    *out = NULL; *outlen = 0;
    if (remote && rlen) {
        if (!mz_zip_reader_init_mem(&zr, remote, rlen, 0)) return -1;
        have_r = 1;
    }
    if (!mz_zip_writer_init_heap(&zw, 0, 0)) { if (have_r) mz_zip_reader_end(&zr); return -1; }

    /* 1. foreign entries: copied compressed, as stored (bytes, mtime, comment) */
    if (have_r) {
        mz_uint cnt = mz_zip_reader_get_num_files(&zr);
        for (mz_uint i = 0; i < cnt; i++) {
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&zr, i, &st)) goto done;
            if (!st.m_is_directory && sync_usr_entry_ok(st.m_filename) &&
                row_add_zip(&zr, i, &rem, &nrem, &caprem)) continue;
            /* anything else (or a .usr too odd to manage) passes through untouched */
            if (!mz_zip_writer_add_from_zip_reader(&zw, &zr, i)) goto done;
        }
    }
    /* 2. the union of the .USR entries */
    res = calloc((size_t)(nloc + nrem + 1), sizeof *res);
    if (!res) goto done;
    char *used = calloc((size_t)nrem + 1, 1);
    if (!used) goto done;
    for (int i = 0; i < nloc; i++) {
        Row *r = &res[nres++];
        sync_usr_zip_name(loc[i].name, r->name, sizeof r->name);
        r->data = loc[i].data; r->len = loc[i].len; r->mtime_ms = loc[i].mtime_ms;
        for (int k = 0; k < nrem; k++) {
            if (strcmp(rem[k].name, r->name)) continue;
            used[k] = 1;
            if (rem[k].mtime_ms > loc[i].mtime_ms + SYNC_SLACK_MS) {    /* server is ahead on this save */
                r->data = rem[k].data; r->len = rem[k].len; r->mtime_ms = rem[k].mtime_ms; r->carried = 1;
            }
        }
    }
    for (int k = 0; k < nrem; k++) {            /* saves this machine has never seen */
        if (used[k]) continue;
        Row *r = &res[nres++];
        *r = rem[k]; r->carried = 1;
    }
    free(used);
    qsort(res, (size_t)nres, sizeof *res, cmp_row);
    for (int i = 0; i < nres; i++) {
        time_t t = (time_t)(res[i].mtime_ms / 1000);
        if (!mz_zip_writer_add_mem_ex_v2(&zw, res[i].name, res[i].data, res[i].len, NULL, 0, 6, 0, 0, &t, NULL, 0, NULL, 0)) goto done;
        if (res[i].carried) { nc++; if (res[i].mtime_ms > cn) cn = res[i].mtime_ms; }
    }
    {
        void *buf = NULL; size_t sz = 0;
        if (!mz_zip_writer_finalize_heap_archive(&zw, &buf, &sz)) goto done;
        *out = buf; *outlen = sz; rc = 0;
    }
done:
    mz_zip_writer_end(&zw);                     /* after finalize the buffer is ours; this frees the rest */
    if (have_r) mz_zip_reader_end(&zr);
    for (int k = 0; k < nrem; k++) free(rem[k].data);
    free(rem); free(res);
    if (carried_newest) *carried_newest = cn;
    if (ncarried) *ncarried = nc;
    return rc;
}

/* Find an existing file in dir whose name matches `want` ignoring case; the
 * port lists *.usr in any case, so reuse whatever case is already there. */
static int find_existing(const char *dir, const char *want, char *out, size_t n)
{
    DIR *d = opendir(dir && *dir ? dir : ".");
    if (!d) return 0;
    struct dirent *e; int found = 0;
    while ((e = readdir(d)))
        if (ci_eq(e->d_name, want) && strlen(e->d_name) < n) { memcpy(out, e->d_name, strlen(e->d_name) + 1); found = 1; break; }
    closedir(d);
    return found;
}

int sync_bundle_extract(const void *blob, size_t len, const char *dir, int force, long long stamp_ms,
                        int dry_run, int *written, int *kept, int *would_overwrite)
{
    mz_zip_archive zr; memset(&zr, 0, sizeof zr);
    int w = 0, k = 0, ov = 0;
    if (!blob || !len || !mz_zip_reader_init_mem(&zr, blob, len, 0)) return -1;
    mz_uint cnt = mz_zip_reader_get_num_files(&zr);
    for (mz_uint i = 0; i < cnt; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zr, i, &st) || st.m_is_directory) continue;
        if (!sync_usr_entry_ok(st.m_filename)) continue;
        char lname[64]; sync_usr_local_name(st.m_filename, lname, sizeof lname);
        if (strlen(lname) - 4 > 8) { logs("skipping %s: a DOS save name is at most 8 characters", st.m_filename); continue; }
        if (st.m_uncomp_size > USR_MAX) continue;
        char path[SYNC_PATH + 80], have[64];
        int exists = find_existing(dir, lname, have, sizeof have);
        snprintf(path, sizeof path, "%s/%s", dir && *dir ? dir : ".", exists ? have : lname);
        struct stat ls; int ex = exists && stat(path, &ls) == 0;
        if (!force && ex && st_mtime_ms(&ls) > (long long)st.m_time * 1000 + SYNC_SLACK_MS) { k++; continue; }
        size_t dl = 0;
        unsigned char *d = mz_zip_reader_extract_to_heap(&zr, i, &dl, 0);
        if (!d) { mz_zip_reader_end(&zr); return -1; }
        size_t ol = 0; unsigned char *old = ex ? read_all(path, &ol, USR_MAX) : NULL;
        int same = old && ol == dl && memcmp(old, d, dl) == 0;
        free(old);
        if (ex && !same) ov++;
        if (!dry_run) {
            if (!same && write_atomic(path, d, dl) != 0) { free(d); mz_zip_reader_end(&zr); return -1; }
            set_mtime_ms(path, stamp_ms);
        }
        if (!same) w++;
        free(d);
    }
    mz_zip_reader_end(&zr);
    if (written) *written = w;
    if (kept) *kept = k;
    if (would_overwrite) *would_overwrite = ov;
    return 0;
}

int sync_backup(const SyncCtx *c, const char *tag, char *out, size_t n)
{
    SyncUsr *l; int cnt;
    if (sync_scan_local(c->game_dir, &l, &cnt) != 0) return -1;
    if (cnt == 0) { sync_free_usr(l, cnt); return 1; }
    mz_zip_archive zw; memset(&zw, 0, sizeof zw);
    void *buf = NULL; size_t sz = 0; int rc = -1;
    if (!mz_zip_writer_init_heap(&zw, 0, 0)) { sync_free_usr(l, cnt); return -1; }
    for (int i = 0; i < cnt; i++) {
        time_t t = (time_t)(l[i].mtime_ms / 1000);
        if (!mz_zip_writer_add_mem_ex_v2(&zw, l[i].name, l[i].data, l[i].len, NULL, 0, 6, 0, 0, &t, NULL, 0, NULL, 0)) goto done;
    }
    if (!mz_zip_writer_finalize_heap_archive(&zw, &buf, &sz)) goto done;
    {
        char dir[SYNC_PATH + 16], stamp[32]; time_t now = time(NULL);
        snprintf(dir, sizeof dir, "%s/backups", c->cfg_dir);
        mkdir_p(dir);
        strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&now));
        for (int tries = 0; tries < 100; tries++) {         /* two backups in one second must not collide */
            char suf[16] = ""; if (tries) snprintf(suf, sizeof suf, "-%d", tries);
            snprintf(out, n, "%s/game-%s-%s%s.zip", dir, tag, stamp, suf);
            FILE *f = fopen(out, "rb");
            if (f) { fclose(f); continue; }
            rc = write_atomic(out, buf, sz) == 0 ? 0 : -1;
            break;
        }
    }
done:
    mz_zip_writer_end(&zw);
    sync_free_usr(l, cnt);
    return rc;
}

/* ---------------------------------------------------------------- network */
static void api_url(const SyncCtx *c, const char *path, char *out, size_t n) { snprintf(out, n, "%s%s", c->base, path); }
static NetAuth auth_of(const SyncCtx *c) { NetAuth a; a.key = c->key; a.token = c->token; return a; }

int sync_health(const SyncCtx *c)
{
    char url[640]; NetResp r; NetAuth a = auth_of(c);
    api_url(c, "api/health", url, sizeof url);
    int st = net_get(url, &a, &r);
    int ok = 0;
    if (st == 0) logs("server unreachable: %s", r.error);
    else if (st == 401 || st == 403) logs("server at %s requires a token (set sync_token in zeliard.ini)", c->base);
    else if (st != 200) logs("no sync API at %s (HTTP %d)", c->base, st);
    else {
        const char *b = r.body ? (const char *)r.body : "";
        const char *o = strstr(b, "\"ok\"");
        if (o && (o = strchr(o, ':'))) { while (*++o == ' '); ok = !strncmp(o, "true", 4); }
        if (!ok) logs("%s answers, but it is not a Zeliard save server", c->base);
    }
    net_resp_free(&r);
    return ok;
}

/* the value after "key": in a flat JSON object text */
static const char *jfind(const char *obj, const char *key)
{
    char pat[48]; snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(obj, pat);
    if (!p || !(p = strchr(p + strlen(pat), ':'))) return NULL;
    while (*++p == ' ');
    return p;
}

/* GET /api/saves: the `auto` entry's modified (0 if there is none) */
int sync_remote_auto(const SyncCtx *c, long long *modified)
{
    char url[640]; NetResp r; NetAuth a = auth_of(c);
    *modified = 0;
    api_url(c, "api/saves", url, sizeof url);
    int st = net_get(url, &a, &r);
    if (st == 401 || st == 403) { logs("HTTP %d: the server requires a token (set sync_token in zeliard.ini)", st); net_resp_free(&r); return -1; }
    if (st != 200) { logs("list failed (%s)", st ? "HTTP error" : r.error); net_resp_free(&r); return -1; }
    const char *p = r.body ? (const char *)r.body : "";
    if (!strchr(p, '[')) { logs("list: not a JSON array"); net_resp_free(&r); return -1; }
    while ((p = strchr(p, '{'))) {
        const char *e = strchr(p, '}');
        if (!e) break;
        char obj[256]; size_t n = (size_t)(e - p); if (n >= sizeof obj) n = sizeof obj - 1;
        memcpy(obj, p, n); obj[n] = 0;
        const char *sv = jfind(obj, "slot");
        if (sv && !strncmp(sv, "\"auto\"", 6)) {
            const char *mv = jfind(obj, "modified");
            *modified = mv ? strtoll(mv, NULL, 10) : 0;
            break;
        }
        p = e + 1;
    }
    net_resp_free(&r);
    return 0;
}

/* 0 = have it (*blob malloc'd), 1 = no such slot, -1 = error */
static int fetch_auto(const SyncCtx *c, unsigned char **blob, size_t *len)
{
    char url[640]; NetResp r; NetAuth a = auth_of(c);
    api_url(c, "api/saves/auto", url, sizeof url);
    int st = net_get(url, &a, &r);
    if (st == 404) { net_resp_free(&r); return 1; }
    if (st != 200 || !r.body || !r.len) { logs("download of auto failed (%s)", st ? "HTTP error" : r.error); net_resp_free(&r); return -1; }
    *blob = r.body; *len = r.len;               /* ownership moves to the caller */
    return 0;
}

static void say_decide(const SyncCtx *c)
{
    logs("this device is not linked to save key %s yet, and the server already holds a save for it.", c->key);
    logs("Not touching either copy.  Choose one:");
    logs("  --sync pull --force   take the server's save (your local .usr files are backed up first)");
    logs("  --sync push --force   keep this device's saves and upload them (merged per save into the bundle)");
}

static void fmt_time(long long ms, char *out, size_t n)
{
    if (ms <= 0) { snprintf(out, n, "-"); return; }
    time_t t = (time_t)(ms / 1000); struct tm *g = gmtime(&t);
    if (!g || !strftime(out, n, "%Y-%m-%d %H:%M:%S UTC", g)) snprintf(out, n, "?");
}

int sync_status(SyncCtx *c)
{
    SyncUsr *l; int n; long long last = 0, remote = 0; char ts[40];
    if (sync_scan_local(c->game_dir, &l, &n) != 0) { logs("cannot read the game folder %s", c->game_dir); return SYNC_ERR; }
    long long local = sync_local_modified(l, n);
    sync_free_usr(l, n);
    int linked = sync_link_get(c, &last);
    printf("key:     %s\nserver:  %s\ngame:    %s (%d .usr files)\n", c->key, c->base, c->game_dir, n);
    fmt_time(local, ts, sizeof ts); printf("local:   newest save %s\n", ts);
    if (linked) { fmt_time(last, ts, sizeof ts); printf("link:    linked, last synced %s\n", ts); }
    else printf("link:    not linked\n");
    if (!sync_health(c)) { printf("server:  unreachable or not a save server\n"); return SYNC_OFFLINE; }
    if (sync_remote_auto(c, &remote) != 0) return SYNC_ERR;
    fmt_time(remote, ts, sizeof ts); printf("remote:  auto %s\n", remote ? ts : "none");
    int dp = sync_decide_pull(linked, remote, local), du = sync_decide_push(linked, remote, local);
    printf("pull:    %s\n", dp == SD_GO ? "server is newer, would pull" : dp == SD_NEEDS_DECISION ? "refused: key not linked yet" :
                            dp == SD_NO_REMOTE ? "nothing on the server" : "nothing to pull");
    printf("push:    %s\n", du == SD_GO ? (remote ? "this device is newer, would push" : "no bundle on the server: the port cannot create one") :
                            du == SD_NEEDS_DECISION ? "refused: key not linked yet" :
                            du == SD_NO_LOCAL ? "no local .usr files" : "nothing to push");
    if (dp == SD_NEEDS_DECISION || du == SD_NEEDS_DECISION) say_decide(c);
    return SYNC_OK;
}

int sync_pull(SyncCtx *c, int force)
{
    long long remote = 0, last;
    if (!sync_health(c)) return SYNC_OFFLINE;
    if (sync_remote_auto(c, &remote) != 0) return SYNC_ERR;
    SyncUsr *l; int n;
    if (sync_scan_local(c->game_dir, &l, &n) != 0) { logs("cannot read the game folder %s", c->game_dir); return SYNC_ERR; }
    long long local = sync_local_modified(l, n);
    sync_free_usr(l, n);
    if (remote == 0) {
        if (force) { logs("there is no save on the server for key %s", c->key); return SYNC_ERR; }
        logs("nothing on the server for key %s", c->key); return SYNC_OK;
    }
    if (!force) {
        int d = sync_decide_pull(sync_link_get(c, &last), remote, local);
        if (d == SD_NEEDS_DECISION) { say_decide(c); return SYNC_DECIDE; }
        if (d != SD_GO) { logs("up to date (this device is not older than the server)"); return SYNC_OK; }
    }
    unsigned char *blob = NULL; size_t bl = 0;
    int f = fetch_auto(c, &blob, &bl);
    if (f != 0) return SYNC_ERR;
    int ov = 0, w = 0, k = 0;
    if (sync_bundle_extract(blob, bl, c->game_dir, force, remote, 1, NULL, NULL, &ov) != 0) {
        logs("the server's bundle is not a readable zip"); free(blob); return SYNC_ERR;
    }
    if (force ? n > 0 : ov > 0) {               /* back up before anything is overwritten */
        char bp[SYNC_PATH + 128];
        int b = sync_backup(c, force ? "before-cloud" : "before-pull", bp, sizeof bp);
        if (b < 0) { logs("cannot back up the local saves first; not pulling"); free(blob); return SYNC_ERR; }
        if (b == 0) logs("backed up local saves to %s", bp);
    }
    if (sync_bundle_extract(blob, bl, c->game_dir, force, remote, 0, &w, &k, &ov) != 0) {
        logs("pull failed while writing %s", c->game_dir); free(blob); return SYNC_ERR;
    }
    free(blob);
    sync_link_mark(c, remote);
    logs("pulled auto: %d save(s) written%s", w, k ? ", newer local saves kept" : "");
    return SYNC_OK;
}

int sync_push(SyncCtx *c, int force)
{
    long long remote = 0, last;
    SyncUsr *l; int n;
    if (sync_scan_local(c->game_dir, &l, &n) != 0) { logs("cannot read the game folder %s", c->game_dir); return SYNC_ERR; }
    long long local = sync_local_modified(l, n);
    int rc = SYNC_ERR; unsigned char *blob = NULL, *out = NULL; size_t bl = 0, ol = 0;
    if (local == 0) { logs("no .usr files in %s: nothing to push", c->game_dir); rc = SYNC_OK; goto done; }
    if (!sync_health(c)) { rc = SYNC_OFFLINE; goto done; }
    if (sync_remote_auto(c, &remote) != 0) goto done;
    if (remote == 0) {
        logs("the server holds no `auto` bundle for key %s.  The port cannot create a bootable one", c->key);
        logs("(that needs the game files js-dos boots); save once in the web app with this key first.");
        rc = SYNC_NOBUNDLE; goto done;
    }
    if (!force) {
        int d = sync_decide_push(sync_link_get(c, &last), remote, local);
        if (d == SD_NEEDS_DECISION) { say_decide(c); rc = SYNC_DECIDE; goto done; }
        if (d != SD_GO) { logs("nothing to push (the server is not older than this device)"); rc = SYNC_OK; goto done; }
    }
    /* never push over a bundle we have not read: it holds the web app's own state */
    if (fetch_auto(c, &blob, &bl) != 0) { logs("push skipped: cannot read the server's bundle, uploading now could discard the web app's save"); goto done; }
    long long carried = 0; int nc = 0;
    if (sync_bundle_merge(blob, bl, l, n, (void **)&out, &ol, &carried, &nc) != 0) { logs("push skipped: the server's bundle is not a readable zip"); goto done; }
    if (ol > BUNDLE_MAX) { logs("push skipped: bundle is %zu bytes, over the server limit", ol); goto done; }
    if (carried > local) local = carried;       /* do not advertise the bundle older than what it holds */
    char url[640]; NetResp r; NetAuth a = auth_of(c);
    api_url(c, "api/saves/auto", url, sizeof url);
    int st = net_put(url, &a, out, ol, local, &r);
    if (st != 200) logs("push failed (%s)", st ? "HTTP error" : r.error);
    net_resp_free(&r);
    if (st != 200) goto done;
    sync_link_mark(c, local);
    logs("pushed auto (%zu KB, %d save(s) kept from the server)", ol / 1024, nc);
    rc = SYNC_OK;
done:
    free(blob); free(out);
    sync_free_usr(l, n);
    return rc;
}

/* ------------------------------------------------------------------- CLI */
int sync_cli(const Config *cfg, const char *cfgpath, const char *game_dir,
             const char *action, int force, const char *cli_key, const char *cli_base)
{
    int is_status = !strcmp(action, "status"), is_pull = !strcmp(action, "pull"), is_push = !strcmp(action, "push");
    if (!is_status && !is_pull && !is_push) {
        fprintf(stderr, "--sync: expected status, pull or push\n"); return 2;
    }
    if (force && is_status) { fprintf(stderr, "--force only applies to --sync pull and push\n"); return 2; }
    if (!net_supported()) {
        fprintf(stderr, "zeliard: sync: built without network support (libcurl was not found when this binary was built)\n");
        return SYNC_NONET;
    }
    static SyncCtx c; memset(&c, 0, sizeof c);
    const char *base = cli_base && *cli_base ? cli_base : cfg->sync_base;
    if (!base || !*base) { fprintf(stderr, "zeliard: sync: no server set (--sync-base URL, or sync_base in zeliard.ini)\n"); return SYNC_ERR; }
    if (strncmp(base, "http://", 7) && strncmp(base, "https://", 8)) { fprintf(stderr, "zeliard: sync: server URL must start with http:// or https://\n"); return SYNC_ERR; }
    snprintf(c.base, sizeof c.base, "%s", base);
    size_t bl = strlen(c.base);
    if (bl && c.base[bl - 1] != '/' && bl + 1 < sizeof c.base) { c.base[bl] = '/'; c.base[bl + 1] = 0; }
    snprintf(c.token, sizeof c.token, "%s", cfg->sync_token);
    snprintf(c.game_dir, sizeof c.game_dir, "%s", game_dir);
    /* link record and key file live beside zeliard.ini (never inside it) */
    snprintf(c.cfg_dir, sizeof c.cfg_dir, "%s", cfgpath);
    char *sl = strrchr(c.cfg_dir, '/');
#ifdef _WIN32
    { char *bs = strrchr(c.cfg_dir, '\\'); if (bs && (!sl || bs > sl)) sl = bs; }
#endif
    if (sl && sl != c.cfg_dir) *sl = 0; else snprintf(c.cfg_dir, sizeof c.cfg_dir, ".");
    if (cli_key && *cli_key) {
        if (sync_key_resolve(&c, cli_key) != 0) return SYNC_ERR;
        if (sync_key_store(&c, c.key) != 0) logs("note: could not remember the key in %s/sync-key", c.cfg_dir);
        if (cfg->sync_key[0] && !ci_eq(cfg->sync_key, c.key))
            logs("note: sync_key in zeliard.ini overrides the remembered key on later runs");
    } else if (sync_key_resolve(&c, cfg->sync_key) != 0) return SYNC_ERR;
    if (is_status) return sync_status(&c);
    return is_pull ? sync_pull(&c, force) : sync_push(&c, force);
}
