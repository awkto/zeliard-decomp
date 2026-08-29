#define _POSIX_C_SOURCE 200809L
/* test_audio.c — the sound back end: the score parser, the tick/tempo model,
 * the FF75 -> SND*.DRV effect mapping and the OPL2 core.
 *
 * The parser check is a full cross-check against tools/msd2mid.py: every one
 * of the 17 scores is run through msd.c in all three blob-B arrangements
 * (AdLib, Tandy, PC speaker) and the decoded event stream — including the
 * loop-detection end tick — is diffed line by line against `msd2mid.py --dump`.
 * It is skipped with a note when python3 or the extracted resources are not
 * available. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "sar.h"
#include "msd.h"
#include "opl2.h"
#include "audio.h"
#include "enemy.h"

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; fprintf(stderr, "  FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

static const char *G_DIR = "../zeliard";
static const char *ROOT = "..";              /* the repo root (tools/, extracted/) */

/* ------------------------------------------------------------------ util */
static uint8_t *load_score(int idx, const uint8_t **blob, size_t *bl, uint8_t **owned)
{
    int n; const MusicRes *t = audio_music_table(&n);
    if (idx < 0 || idx >= n) return NULL;
    size_t len;
    uint8_t *res = sar_load(G_DIR, t[idx].archive, t[idx].index, 1, &len);
    if (!res) return NULL;
    if (msd_split(res, len, blob, bl)) { free(res); return NULL; }
    *owned = res;
    return res;
}

/* ============================ 1. the score parser vs tools/msd2mid.py ==== */
static const char *CHAN_ADLIB[7] = { "OPL2 ch 0", "OPL2 ch 1", "OPL2 ch 2", "OPL2 ch 3",
                                     "OPL2 ch 4", "OPL2 ch 5", "OPL2 rhythm" };
static const char *CHAN_JR[3] = { "SN76496 ch 0", "SN76496 ch 1", "SN76496 ch 2" };

typedef struct { FILE *f; int mode; } Dump;

static void dump_ev(void *u, int tick, int tr, int kind, int a, int b, int c)
{
    Dump *d = u;
    const char *nm = d->mode == MSD_ADLIB ? CHAN_ADLIB[tr] : CHAN_JR[tr];
    char args[64];
    const char *kn;
    switch (kind) {
    case MSD_ON:    kn = "on";    snprintf(args, sizeof args, "%d %d %s", a, b, c ? "True" : "False"); break;
    case MSD_OFF:   kn = "off";   snprintf(args, sizeof args, "%d", a); break;
    case MSD_DRUM:  kn = "drum";  snprintf(args, sizeof args, "%d %d", a, b); break;
    case MSD_INSTR: kn = "instr"; snprintf(args, sizeof args, "%d", a); break;
    default:        kn = "vol";   snprintf(args, sizeof args, "%d", a); break;
    }
    fprintf(d->f, "%6d %-8s %-7s %s\n", tick, nm, kn, args);
}

/* tools/msd2mid.py simulate(): hash the whole machine state every tick and
 * stop at the first repeat (or when every track has ended). */
#define MAXKEY 160
typedef struct { int32_t *keys; int *tick; int n, cap, words; int *bucket; int nb; } Seen;

static void seen_init(Seen *s, int words)
{
    s->words = words; s->cap = 4096; s->n = 0;
    s->keys = malloc((size_t)s->cap * words * sizeof(int32_t));
    s->tick = malloc((size_t)s->cap * sizeof(int));
    s->nb = 1 << 17;
    s->bucket = malloc((size_t)s->nb * sizeof(int));
    for (int i = 0; i < s->nb; i++) s->bucket[i] = -1;
}
static void seen_free(Seen *s) { free(s->keys); free(s->tick); free(s->bucket); }
static uint32_t keyhash(const int32_t *k, int n)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; i++) { h ^= (uint32_t)k[i]; h *= 16777619u; }
    return h;
}
/* returns the tick of an identical earlier state, or -1 */
static int seen_add(Seen *s, const int32_t *k, int tick)
{
    uint32_t h = keyhash(k, s->words);
    for (uint32_t j = h & (uint32_t)(s->nb - 1); s->bucket[j] != -1; j = (j + 1) & (uint32_t)(s->nb - 1))
        if (!memcmp(s->keys + (size_t)s->bucket[j] * s->words, k, (size_t)s->words * sizeof(int32_t)))
            return s->tick[s->bucket[j]];
    if (s->n == s->cap) {
        s->cap *= 2;
        s->keys = realloc(s->keys, (size_t)s->cap * s->words * sizeof(int32_t));
        s->tick = realloc(s->tick, (size_t)s->cap * sizeof(int));
        for (int i = 0; i < s->nb; i++) s->bucket[i] = -1;
        for (int i = 0; i < s->n; i++) {
            uint32_t hh = keyhash(s->keys + (size_t)i * s->words, s->words);
            for (uint32_t j = hh & (uint32_t)(s->nb - 1); ; j = (j + 1) & (uint32_t)(s->nb - 1))
                if (s->bucket[j] == -1) { s->bucket[j] = i; break; }
        }
        h = keyhash(k, s->words);
    }
    memcpy(s->keys + (size_t)s->n * s->words, k, (size_t)s->words * sizeof(int32_t));
    s->tick[s->n] = tick;
    for (uint32_t j = h & (uint32_t)(s->nb - 1); ; j = (j + 1) & (uint32_t)(s->nb - 1))
        if (s->bucket[j] == -1) { s->bucket[j] = s->n; break; }
    s->n++;
    return -1;
}

/* Run one arrangement to its loop point / end.  Returns the end tick, and the
 * loop start in *loop (-1 = the score ends).  `out` may be NULL. */
static int run_score(MsdPlayer *p, const uint8_t *blob, size_t bl, int mode, FILE *out, int *loop, int compat)
{
    Dump d = { out, mode };
    memset(p, 0, sizeof *p);
    p->compat_msd2mid = compat;
    if (out) { p->ev = dump_ev; p->ev_u = &d; }
    if (msd_start(p, blob, bl, mode)) return -1;
    Seen seen;
    int32_t key[MAXKEY];
    int words = msd_state_key(p, key, MAXKEY);
    seen_init(&seen, words);
    int end = -1;
    *loop = -1;
    for (int t = 0; t < 400000; t++) {
        msd_state_key(p, key, MAXKEY);
        int prev = seen_add(&seen, key, t);
        if (prev >= 0) { end = t; *loop = prev; break; }
        msd_score_tick(p);
        int all = 1;
        for (int i = 0; i < p->nch; i++) if (!(p->ch[i].flags & 1)) all = 0;
        if (all) { end = t + 1; break; }
    }
    seen_free(&seen);
    return end;
}

static int have_tool(char *path, size_t n)
{
    static const char *cands[] = { "../tools/msd2mid.py", "tools/msd2mid.py", NULL };
    for (int i = 0; cands[i]; i++) {
        FILE *f = fopen(cands[i], "r");
        if (f) { fclose(f); snprintf(path, n, "%s", cands[i]); return 1; }
    }
    return 0;
}

static void t_parser(void)
{
    char tool[256];
    if (!have_tool(tool, sizeof tool)) {
        fprintf(stderr, "  (tools/msd2mid.py not found: skipping the parser cross-check)\n");
        return;
    }
    ROOT = strncmp(tool, "../", 3) == 0 ? ".." : ".";
    int n; const MusicRes *tab = audio_music_table(&n);
    /* the 9E53 table plus the three scores it does not name */
    static const struct { const char *name; int archive, index; } EXTRA[3] = {
        { "zopn", 0, 39 }, { "zend", 0, 38 }, { "mfan", 2, 94 },
    };
    static const char *FLAG[3] = { "", " --jr", " --std" };
    for (int i = 0; i < n + 3; i++) {
        const char *name = i < n ? tab[i].name : EXTRA[i - n].name;
        int arc = i < n ? tab[i].archive : EXTRA[i - n].archive;
        int idx = i < n ? tab[i].index : EXTRA[i - n].index;
        size_t len;
        uint8_t *res = sar_load(G_DIR, arc, idx, 1, &len);
        if (!res) { CHECK(0, "%s: cannot load ZELRES%d[%d]", name, arc + 1, idx); continue; }
        const uint8_t *blob; size_t bl;
        if (msd_split(res, len, &blob, &bl)) { CHECK(0, "%s: bad {lenA,lenB} container", name); free(res); continue; }
        for (int mode = 0; mode < 3; mode++) {
            char cpath[256], cmd[512];
            snprintf(cpath, sizeof cpath, "/tmp/zel_msd_%s_%d.txt", name, mode);
            FILE *f = fopen(cpath, "w");
            if (!f) { CHECK(0, "cannot write %s", cpath); continue; }
            MsdPlayer p;
            int loop, end = run_score(&p, blob, bl, mode, f, &loop, 1);
            fclose(f);
            CHECK(end > 0, "%s [%d]: the arrangement runs to a loop or an end", name, mode);
            snprintf(cmd, sizeof cmd, "python3 %s %s /dev/null --dump%s 2>&1 | head -n -1",
                     tool, name, FLAG[mode]);
            FILE *py = popen(cmd, "r");
            if (!py) { CHECK(0, "cannot run %s", cmd); continue; }
            FILE *cf = fopen(cpath, "r");
            char lp[256], lc[256];
            long line = 0; int diff = 0;
            for (;;) {
                char *a = fgets(lp, sizeof lp, py), *b = fgets(lc, sizeof lc, cf);
                line++;
                if (!a && !b) break;
                if (!a || !b || strcmp(a, b)) {
                    diff = 1;
                    fprintf(stderr, "  %s [%d] line %ld:\n    py: %s    c : %s", name, mode, line,
                            a ? a : "(eof)\n", b ? b : "(eof)\n");
                    break;
                }
            }
            fclose(cf);
            pclose(py);
            CHECK(!diff, "%s [%s]: the event stream matches tools/msd2mid.py",
                  name, mode == 0 ? "adlib" : mode == 1 ? "jr" : "std");
            remove(cpath);
        }
        free(res);
    }
}

/* The AdLib build's C0-CF clamp (MSCADLIB 06B3 `test bl,0xC0`) is a driver bug
 * that tools/msd2mid.py does not model; msd.c reproduces the driver by default
 * and the tool only under compat_msd2mid.  zopn is the one score that shows it. */
static void t_volume_clamp(void)
{
    const uint8_t *blob; size_t bl; uint8_t *own = NULL;
    size_t len;
    uint8_t *res = sar_load(G_DIR, 0, 39, 1, &len);           /* zopn */
    if (!res) { CHECK(0, "zopn loads"); return; }
    if (msd_split(res, len, &blob, &bl)) { CHECK(0, "zopn container"); free(res); return; }
    (void)own;
    MsdPlayer p;
    memset(&p, 0, sizeof p);
    CHECK(msd_start(&p, blob, bl, MSD_ADLIB) == 0, "zopn parses as an AdLib arrangement");
    /* track 0: F0 00 E0 7D 80 D4 E5 4F ... C0 -> 0x4F - 4 = 0x4B, bit 6 set */
    for (int t = 0; t < 40 && p.ch[0].lev[0] == 0x7F; t++) msd_score_tick(&p);
    CHECK(p.ch[0].lev[0] == 0x4F, "E5 4F sets the channel attenuation to 0x4F (got %02X)", p.ch[0].lev[0]);
    for (int t = 0; t < 40 && p.ch[0].lev[0] == 0x4F; t++) msd_score_tick(&p);
    CHECK(p.ch[0].lev[0] == 0x00, "the AdLib C0 clamp snaps 0x4B to 0 (got %02X)", p.ch[0].lev[0]);
    memset(&p, 0, sizeof p);
    p.compat_msd2mid = 1;
    msd_start(&p, blob, bl, MSD_ADLIB);
    for (int t = 0; t < 80 && p.ch[0].lev[0] >= 0x4F; t++) msd_score_tick(&p);
    CHECK(p.ch[0].lev[0] == 0x4B, "compat mode keeps 0x4B, like tools/msd2mid.py (got %02X)", p.ch[0].lev[0]);
    free(res);
}

/* ============================ 2. tick / tempo ========================== */
static void t_tempo(void)
{
    /* docs/MUSIC.md section 1 "Timing": INT 8 = 236.7 Hz, driver tick = half
     * that, score tick rate = 118.35 * (256-T)/256, 24 ticks per quarter. */
    CHECK(fabs(audio_int8_hz() - 236.70) < 0.05, "INT 8 is 236.7 Hz (%.2f)", audio_int8_hz());
    CHECK(fabs(msd_score_hz(0x7F) - 59.6) < 0.05, "T=0x7F -> 59.6 score ticks/s (%.2f)", msd_score_hz(0x7F));
    CHECK(fabs(msd_score_hz(0x7D) - 60.6) < 0.05, "T=0x7D -> 60.6 score ticks/s (%.2f)", msd_score_hz(0x7D));
    CHECK(fabs(msd_score_hz(0x2D) - 97.5) < 0.05, "T=0x2D -> 97.5 score ticks/s (%.2f)", msd_score_hz(0x2D));
    CHECK(fabs(msd_us_per_quarter(0x7F) - 51913590.0 / (256 - 0x7F)) < 20.0,
          "us/quarter = 51,913,590/(256-T) at T=0x7F (%.0f)", msd_us_per_quarter(0x7F));
    CHECK(fabs(60e6 / msd_us_per_quarter(0x7F) - 149.0) < 0.6, "T=0x7F is 149 BPM (%.1f)",
          60e6 / msd_us_per_quarter(0x7F));
    CHECK(fabs(60e6 / msd_us_per_quarter(0x7D) - 151.0) < 0.6, "T=0x7D is 151 BPM (%.1f)",
          60e6 / msd_us_per_quarter(0x7D));

    /* the accumulator model: over 256 driver ticks exactly 256-T of them are
     * score ticks, whatever the starting phase */
    const uint8_t *blob; size_t bl; uint8_t *own;
    if (!load_score(4, &blob, &bl, &own)) { CHECK(0, "mus1 loads"); return; }
    for (int T = 0x20; T <= 0xA0; T += 0x10) {
        MsdPlayer p; memset(&p, 0, sizeof p);
        msd_start(&p, blob, bl, MSD_ADLIB);
        p.tempo = (uint8_t)T;
        int before = p.tick;
        for (int i = 0; i < 256; i++) { p.tempo = (uint8_t)T; msd_driver_tick(&p); }
        CHECK(p.tick - before == 256 - T, "T=%02X: %d score ticks per 256 driver ticks (want %d)",
              T, p.tick - before, 256 - T);
    }
    /* docs/MUSIC.md cross-check: mus5's AdLib loop body is 5664 ticks at
     * T=0x2D = 58.1 s, the length the MT-32 arrangement plays it in. */
    MsdPlayer p; int loop;
    int end = run_score(&p, blob, bl, MSD_ADLIB, NULL, &loop, 0);
    CHECK(end == 5377 && loop == 2305, "mus1 loops at tick 2305..5377 (got %d..%d)", loop, end);
    free(own);
    if (load_score(8, &blob, &bl, &own)) {                     /* mus5 */
        end = run_score(&p, blob, bl, MSD_ADLIB, NULL, &loop, 0);
        CHECK(end - loop == 5664, "mus5's AdLib loop body is 5664 ticks (got %d)", end - loop);
        double s = (end - loop) / msd_score_hz(0x2D);
        CHECK(fabs(s - 58.1) < 0.15, "mus5's loop body is 58.1 s (got %.1f)", s);
        free(own);
    }
    /* every score in the 9E53 table parses and runs */
    int n; const MusicRes *tab = audio_music_table(&n);
    for (int i = 0; i < n; i++) {
        if (!load_score(i, &blob, &bl, &own)) { CHECK(0, "%s loads", tab[i].name); continue; }
        int e = run_score(&p, blob, bl, MSD_ADLIB, NULL, &loop, 0);
        CHECK(e > 100, "%s: the AdLib arrangement runs (%d ticks)", tab[i].name, e);
        free(own);
    }
}

/* ============================ 3. the 9E53 music table =================== */
static void t_music_table(void)
{
    size_t len;
    uint8_t *fight = sar_load(G_DIR, 1, 0, 1, &len);           /* ZELRES2[0] = fight.bin @6000 */
    if (!fight || len < 0x3F00) { CHECK(0, "fight.bin loads"); free(fight); return; }
    int n; const MusicRes *tab = audio_music_table(&n);
    CHECK(n == 14, "the 9E53 table has 14 entries (%d)", n);
    size_t o = 0x9E53 - 0x6000;
    for (int i = 0; i < n; i++) {
        uint8_t arc = fight[o], res = fight[o + 1];
        char name[16];
        size_t k = 0;
        while (k < 12 && fight[o + 2 + k] && fight[o + 2 + k] != '.') { name[k] = (char)(fight[o + 2 + k] | 0x20); k++; }
        name[k] = 0;
        CHECK(arc == tab[i].archive, "9E53[%d] archive %d (want %d)", i, arc, tab[i].archive);
        CHECK(res - 1 == tab[i].index, "9E53[%d] resource %d (want %d)", i, res - 1, tab[i].index);
        CHECK(!strcmp(name, tab[i].name), "9E53[%d] is %s (want %s)", i, name, tab[i].name);
        while (o < len && fight[o + 2]) o++;                   /* skip to the NUL */
        o += 3;
    }
    free(fight);
}

/* ============================ 4. FF75 -> SND*.DRV ====================== */
static void t_sfx(void)
{
    if (audio_sfx_load(G_DIR, AUDIO_ADLIB)) {
        fprintf(stderr, "  (SNDADLIB.DRV not found in %s: skipping the effect table)\n", G_DIR);
        return;
    }
    int n = audio_sfx_count();
    /* the records run from 1743 up to the first track at 190A: 65 * 7 bytes */
    CHECK(n == 65, "SNDADLIB.DRV defines 65 effects (%d)", n);
    /* the priorities the driver arbitrates with (SNDADLIB 112A) */
    static const struct { int id, prio; const char *what; } P[] = {
        { 0x01, 0x00, "confirm blip" }, { 0x02, 0xFF, "dialog open" },
        { 0x03, 0x00, "sword swing" },  { 0x04, 0x00, "down-thrust" },
        { 0x06, 0x01, "enemy hurt" },   { 0x07, 0x09, "enemy killed" },
        { 0x09, 0x08, "hero hurt" },    { 0x0A, 0x07, "shot blocked" },
        { 0x0B, 0xFF, "menu open" },    { 0x10, 0x09, "coin" },
        { 0x11, 0x09, "treasure box" }, { 0x13, 0x00, "potion" },
        { 0x14, 0x09, "key" },          { 0x19, 0x09, "screen-wide spell" },
        { 0x1D, 0xFF, "dialogue page" },
    };
    for (size_t i = 0; i < sizeof P / sizeof P[0]; i++) {
        int prio; unsigned t0, t1, dt;
        CHECK(audio_sfx_entry(P[i].id, &prio, &t0, &t1, &dt) == 0, "effect %02X exists", P[i].id);
        CHECK(prio == P[i].prio, "effect %02X (%s) priority %02X (want %02X)", P[i].id, P[i].what, prio, P[i].prio);
        CHECK(t0 >= 0x190A && t0 < 0x2020, "effect %02X track A %04X is in the track area", P[i].id, t0);
        CHECK(t1 == 0x201F || (t1 >= 0x190A && t1 < 0x2020),
              "effect %02X track B %04X is a track or the empty stub", P[i].id, t1);
        CHECK(dt >= 0x190A && dt < 0x2020, "effect %02X duration table %04X is in range", P[i].id, dt);
    }
    /* every id the engines actually write to FF75 must be in the table and
     * must name a sound (sound.c) */
    static const uint8_t USED[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 0x0A, 0x0B, 0x10, 0x11, 0x13,
                                    0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1D };
    for (size_t i = 0; i < sizeof USED; i++) {
        int prio;
        CHECK(audio_sfx_entry(USED[i], &prio, NULL, NULL, NULL) == 0,
              "FF75 = %02X maps to an effect record", USED[i]);
    }
    CHECK(audio_sfx_entry(0, NULL, NULL, NULL, NULL) != 0, "FF75 = 0 is not a request");
    CHECK(audio_sfx_entry(n + 1, NULL, NULL, NULL, NULL) != 0, "ids past the table are rejected");
    /* the id -> name table in sound.c stays in step with the effect table */
    CHECK(sound_name(3) && !strcmp(sound_name(3), "sword swing"), "id 03 is the sword swing");
    CHECK(sound_name(0x1D) && !strcmp(sound_name(0x1D), "dialogue page"), "id 1D is the dialogue page");

    /* INT 60h AX=6: the effect driver claims OPL channels 4 and 5 from the
     * music while a patch of its own is loaded (MSCADLIB 01A0) */
    const uint8_t *blob; size_t bl; uint8_t *own;
    if (load_score(4, &blob, &bl, &own)) {
        MsdPlayer p; memset(&p, 0, sizeof p);
        msd_start(&p, blob, bl, MSD_ADLIB);
        CHECK(!p.ch[4].muted && !p.ch[5].muted, "the music owns OPL 4/5 to start with");
        msd_claim_opl(&p, 1);
        CHECK(p.ch[4].muted && !p.ch[5].muted, "CL bit 0 claims OPL channel 4");
        msd_claim_opl(&p, 3);
        CHECK(p.ch[4].muted && p.ch[5].muted, "CL bits 0-1 claim both channels");
        msd_claim_opl(&p, 0);
        CHECK(!p.ch[4].muted && !p.ch[5].muted, "CL = 0 gives both channels back");
        free(own);
    }

    /* the PC-speaker driver has its own, shorter table (5-byte records) */
    if (audio_sfx_load(G_DIR, AUDIO_SPEAKER) == 0) {
        int m = audio_sfx_count();
        CHECK(m == 65, "SNDSTD.DRV defines the same 65 effects (%d)", m);
        int prio;
        CHECK(audio_sfx_entry(0x02, &prio, NULL, NULL, NULL) == 0 && prio == 0xFF,
              "SNDSTD effect 02 also has priority FF (%02X)", prio);
    }
}

/* ============================ 5. the OPL2 core ========================= */
static void t_opl(void)
{
    Opl2 o;
    opl2_reset(&o, 49716.0);
    /* one additive voice, carrier at full level, instant attack, no decay */
    opl2_write(&o, 0x01, 0x20);
    opl2_write(&o, 0x20, 0x21); opl2_write(&o, 0x23, 0x21);   /* EG-type, MULT 1 */
    opl2_write(&o, 0x40, 0x3F); opl2_write(&o, 0x43, 0x00);   /* modulator muted */
    opl2_write(&o, 0x60, 0xF0); opl2_write(&o, 0x63, 0xF0);   /* AR 15, DR 0    */
    opl2_write(&o, 0x80, 0x0F); opl2_write(&o, 0x83, 0x0F);   /* SL 0, RR 15    */
    opl2_write(&o, 0xC0, 0x01);                               /* additive       */
    /* 440 Hz at block 4: fnum = f * 2^(20-block) / 49716 */
    int fnum = (int)(440.0 * 65536.0 / 49716.0 + 0.5);
    opl2_write(&o, 0xA0, (uint8_t)(fnum & 0xFF));
    opl2_write(&o, 0xB0, (uint8_t)(0x20 | (4 << 2) | (fnum >> 8)));
    int cross = 0, nz = 0;
    double prev = 0, peak = 0;
    int N = 49716;                                            /* one second     */
    for (int i = 0; i < N; i++) {
        double v = opl2_sample(&o);
        if (i > 200) {
            if ((prev < 0 && v >= 0)) cross++;
            if (fabs(v) > 1e-4) nz++;
            if (fabs(v) > peak) peak = fabs(v);
        }
        prev = v;
    }
    CHECK(nz > N / 2, "a keyed-on OPL2 voice produces sound (%d/%d non-zero samples)", nz, N);
    CHECK(peak > 0.02, "the voice reaches a usable level (peak %.3f)", peak);
    CHECK(fabs(cross - 440.0) < 8.0, "the voice sounds at 440 Hz (%d zero crossings/s)", cross);
    /* key-off releases: RR 15 must silence it inside 100 ms */
    opl2_write(&o, 0xB0, (uint8_t)((4 << 2) | (fnum >> 8)));
    double after = 0;
    for (int i = 0; i < 4972; i++) { double v = opl2_sample(&o); if (fabs(v) > after) after = fabs(v); }
    double tail = 0;
    for (int i = 0; i < 4972; i++) { double v = opl2_sample(&o); if (fabs(v) > tail) tail = fabs(v); }
    CHECK(tail < peak / 100.0, "key-off releases the voice (tail %.5f vs peak %.3f)", tail, peak);
    /* the f-number/block relation is one octave per block */
    opl2_reset(&o, 49716.0);
    CHECK(fabs(49716.0 * fnum / 65536.0 - 440.0) < 1.0, "fnum %d at block 4 is 440 Hz", fnum);
}

/* ==== 6. the pitch the driver programmes matches the note it logs ======= */
/* Every note-on writes A0/B0 for its channel; f = fnum * 2^block * 49716 / 2^20.
 * The game's f-number table (MSCADLIB 0B51) is a flat equal-tempered scale
 * about 8 cents below A=440, so the frequency for the MIDI note that
 * tools/msd2mid.py logs must agree to well inside a semitone. */
static int PT_note[6];

static void pitch_ev(void *u, int tick, int tr, int kind, int a, int b, int c)
{
    if (kind == MSD_ON && tr <= 5) PT_note[tr] = a;
}

static void t_pitch(void)
{
    const uint8_t *blob; size_t bl; uint8_t *own;
    if (!load_score(4, &blob, &bl, &own)) { CHECK(0, "mus1 loads"); return; }
    MsdPlayer p;
    memset(&p, 0, sizeof p);
    p.ev = pitch_ev;
    CHECK(msd_start(&p, blob, bl, MSD_ADLIB) == 0, "mus1 starts on the AdLib arrangement");
    int worst = 0, bad = 0, n = 0, detuned = 0, worst_det = 0;
    for (int t = 0; t < 2400; t++) {
        for (int i = 0; i < 6; i++) PT_note[i] = -1;
        msd_score_tick(&p);
        for (int i = 0; i < 6; i++) {
            if (PT_note[i] < 0) continue;
            unsigned fb = p.ch[i].fnum_block;
            int fnum = (int)(fb & 0x3FF), block = (int)((fb >> 10) & 7);
            double want = 440.0 * pow(2.0, (PT_note[i] - 69) / 12.0);
            double f = fnum * (double)(1u << block) * OPL2_NATIVE_HZ / 1048576.0;
            /* E1 detune is added to the f-number before the block shift */
            double plain = (fnum - p.ch[i].detune) * (double)(1u << block) * OPL2_NATIVE_HZ / 1048576.0;
            int cents = (int)fabs(1200.0 * log2(plain / want));
            int dcents = (int)fabs(1200.0 * log2(f / want));
            n++;
            if (cents > worst) worst = cents;
            if (cents > 15) bad++;
            if (p.ch[i].detune) { detuned++; if (dcents > worst_det) worst_det = dcents; }
        }
    }
    CHECK(n > 500, "mus1 keys on %d notes in its first 2400 ticks", n);
    CHECK(bad == 0, "%d of %d f-numbers are more than 15 cents off the logged MIDI note", bad, n);
    CHECK(worst <= 20, "the f-number table sits %d cents below A=440 equal temperament", worst);
    CHECK(detuned > 0 && worst_det > 15 && worst_det < 100,
          "%d notes carry an E1 detune, at most %d cents (a deliberate chorus)", detuned, worst_det);
    free(own);
}

/* ============================ 7. headless is silent ==================== */
static void t_silence(void)
{
    CHECK(!audio_active(), "audio is off until main.c opens it");
    audio_music(4);                       /* must be a no-op */
    audio_sfx_request(3);
    audio_advance_ms(100.0);
    CHECK(!audio_active(), "the engine hooks stay no-ops while audio is off");

    /* --dump-audio is deterministic: the same script twice, byte for byte */
    const char *pa = "/tmp/zel_audio_a.wav", *pb = "/tmp/zel_audio_b.wav";
    long sizes[2] = { 0, 0 };
    unsigned char *bufs[2] = { NULL, NULL };
    for (int pass = 0; pass < 2; pass++) {
        const char *path = pass ? pb : pa;
        if (audio_init(G_DIR, AUDIO_ADLIB, 0)) { CHECK(0, "audio_init"); return; }
        if (audio_dump_open(path)) { CHECK(0, "audio_dump_open(%s)", path); audio_shutdown(); return; }
        audio_music(4);                                       /* mus1 */
        audio_advance_ms(500);
        audio_sfx_request(3);                                 /* sword swing */
        audio_advance_ms(500);
        audio_sfx_request(7);                                 /* enemy killed */
        audio_advance_ms(500);
        audio_shutdown();
        FILE *f = fopen(path, "rb");
        if (!f) { CHECK(0, "%s written", path); return; }
        fseek(f, 0, SEEK_END); sizes[pass] = ftell(f); fseek(f, 0, SEEK_SET);
        bufs[pass] = malloc((size_t)sizes[pass]);
        if (fread(bufs[pass], 1, (size_t)sizes[pass], f) != (size_t)sizes[pass]) sizes[pass] = -1;
        fclose(f);
    }
    CHECK(sizes[0] > 44 && sizes[0] == sizes[1], "--dump-audio writes the same length twice (%ld/%ld)",
          sizes[0], sizes[1]);
    CHECK(sizes[0] == 44 + 2 * (long)(1.5 * 44100), "1.5 s of 16-bit mono at 44.1 kHz (%ld bytes)", sizes[0]);
    if (sizes[0] > 0 && sizes[0] == sizes[1])
        CHECK(!memcmp(bufs[0], bufs[1], (size_t)sizes[0]), "the two renders are bit-identical");
    /* and it is not silence */
    long loud = 0;
    for (long i = 44; i + 1 < sizes[0]; i += 2) {
        int v = (int16_t)(bufs[0][i] | bufs[0][i + 1] << 8);
        if (v > 1000 || v < -1000) loud++;
    }
    CHECK(loud > 1000, "the dump contains audio (%ld loud samples)", loud);
    free(bufs[0]); free(bufs[1]);
    remove(pa); remove(pb);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../zeliard";
    G_DIR = dir;
    size_t len;
    uint8_t *probe = sar_load(dir, 2, 85, 1, &len);
    if (!probe) {
        fprintf(stderr, "  (ZELRES3.SAR not available in %s: skipping the audio checks)\n", dir);
        return 0;
    }
    free(probe);
    struct { const char *name; void (*fn)(void); } tests[] = {
        {"music table", t_music_table}, {"tick/tempo", t_tempo},
        {"score parser", t_parser}, {"volume clamp", t_volume_clamp},
        {"FF75 effects", t_sfx}, {"OPL2 core", t_opl}, {"note pitch", t_pitch},
        {"silence/dump", t_silence},
    };
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; i++) {
        int before = fails;
        tests[i].fn();
        fprintf(stderr, "%-16s %s\n", tests[i].name, fails == before ? "ok" : "FAILED");
    }
    fprintf(stderr, "%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
