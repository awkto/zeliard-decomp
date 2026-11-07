#define _POSIX_C_SOURCE 200809L
/* test_audio.c — the sound back end: the score parser, the tick/tempo model,
 * the FF75 -> SND*.DRV effect mapping, the OPL2 core and the Tandy / PCjr
 * (SN76496) back end.
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
#include <unistd.h>
#include "sar.h"
#include "msd.h"
#include "opl2.h"
#include "sn76496.h"
#include "audio.h"
#include "enemy.h"
#include "cutscene.h"

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

/* ======== 9. #37: the opening demo is silent until the title act ========= */
/* GAME.BIN A080 hands a no-argument boot straight to opdemo, *before* it reads
 * any level record, so the prologue plays over silence and the first score in
 * the game is zopn.msd at the title (opdemo 622E, docs/CUTSCENES.md §2).  The
 * port has to build its scaffold Game first, and that map's level-record score
 * (fight.bin 7E93 -> the 9E53 table) used to start at boot and play through the
 * whole prologue.  audio_music_hold() is what keeps the two apart: it gates the
 * table path only, never audio_music_play_res(), which is how the cutscene
 * overlays call INT 60h AX=0.                                               */
#define ZOPN_RES  0x27                 /* ZELRES1[0x27]: cutscene.c R_ZOPN - 1 */
static int intro_first_music;
static void intro_present(Cutscene *c)
{
    if (intro_first_music < 0 && !audio_music_stopped()) intro_first_music = (int)c->frames;
    c->key = 0;
}
static void t_intro_silence(void)
{
    static Cutscene c;
    if (audio_init(G_DIR, AUDIO_ADLIB, 0)) { CHECK(0, "audio_init"); return; }
    /* exactly what main.c does between audio_init() and run_cutscenes() */
    audio_music_hold(1);
    audio_music(4);                            /* shell_load_enemy_banks: MP10's mus1 */
    CHECK(audio_music_stopped(), "the boot map's score does not start before the demo");
    audio_music(0);                            /* and neither does a town's mgt1 */
    CHECK(audio_music_stopped(), "nor a town record's score");
    /* the demo's own INT 60h AX=0 is never held */
    audio_music_play_res(0, ZOPN_RES);
    CHECK(!audio_music_stopped(), "zopn.msd still plays while the level score is held");
    audio_music_stop();

    if (cutscene_init(&c, G_DIR, NULL)) { CHECK(0, "cutscene_init"); audio_shutdown(); return; }
    size_t len = 0;
    c.img = sar_load(G_DIR, 0, 0, 1, &len); c.imglen = len;
    if (!c.img) { CHECK(0, "opdemo (ZELRES1[0])"); cutscene_free(&c); audio_shutdown(); return; }
    c.present = intro_present;
    c.max_frames = 6600;                       /* act 1 is 6465 frames (test_cutscene) */
    intro_first_music = -1;
    cutscene_act1(&c);
    CHECK(intro_first_music >= 0, "act 1 starts a score at all (the title)");
    /* frame 4000 is still the demon's warning ("Beware, for I shall wake..."),
     * 4085 is 622E, the first frame of the title block */
    CHECK(intro_first_music > 4000,
          "the prologue, the storm and the demon are silent (first score at frame %d)",
          intro_first_music);
    CHECK(intro_first_music == 4085,
          "and zopn.msd starts with the title screen at 622E (frame %d)", intro_first_music);
    cutscene_free(&c);

    /* ...and the same thing end to end, through main.c's own boot order, which
     * is where the bug actually was: `zeliard --intro-act 1` opens the audio,
     * builds the scaffold Game (map 0 = MP10, score mus1) and then runs the
     * demo, so the first 200 frames of the prologue have to render bit-silent. */
    if (access("./zeliard", X_OK) == 0) {
        const char *wav = "/tmp/zel_intro_silence.wav";
        char cmd[512];
        remove(wav);
        snprintf(cmd, sizeof cmd,
                 "./zeliard --dir %s --intro-act 1 --cutscene-frames 200 "
                 "--screenshot 1 /dev/null --dump-audio %s >/dev/null 2>&1", G_DIR, wav);
        if (system(cmd) == 0) {
            FILE *f = fopen(wav, "rb");
            CHECK(f != NULL, "the intro dump %s was written", wav);
            if (f) {
                fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 44, SEEK_SET);
                long loud = 0, samples = (n - 44) / 2;
                for (long i = 0; i < samples; i++) {
                    unsigned char b[2];
                    if (fread(b, 1, 2, f) != 2) break;
                    if ((int16_t)(b[0] | b[1] << 8)) loud++;
                }
                fclose(f);
                CHECK(samples > 8000, "3.4 s of the prologue was rendered (%ld samples)", samples);
                CHECK(loud == 0, "and it is bit-silent: no level score plays over the "
                                 "opening demo (%ld non-zero samples)", loud);
            }
            remove(wav);
        }
    }

    /* opdemo 6A41 hands back: from here a level entry may start its score */
    audio_music_stop();
    audio_music_hold(0);
    audio_music(0);                            /* shell_enter_town -> town.bin 60A9 */
    CHECK(!audio_music_stopped(), "the town score starts once the demo is over");

    /* town.bin 6E7E run_shop: 6EA0 `int 60h AX=1` stops the score for the visit; 6EF8
     * restarts it on the way out (#115) */
    audio_music_stop();
    CHECK(audio_music_stopped(), "entering a shop stops the town score (6EA0)");
    audio_music_restart(0);
    CHECK(!audio_music_stopped(), "leaving the shop restarts it (6EF8)");
    audio_music_restart(0);
    CHECK(!audio_music_stopped(), "and a restart while it still plays is fine");
    /* 6E7E FF24 = 4: the score fades (FF25 += 4 every 4 driver ticks, 118.35 Hz) while the
     * dissolve runs, 8 frames = 0.68 s: still sounding when 6EA0 stops it (#118); left alone
     * it would run to silence in 64 steps = 2.16 s */
    CHECK(audio_dump_open("/tmp/zel_fade_dsv.wav") == 0, "WAV dump opens");
    audio_music_restart(0);
    audio_music_fade(4);
    audio_advance_ms(8 * 84.0);
    CHECK(!audio_music_stopped(), "the fade is still going after the dissolve's 0.68 s");
    audio_music_stop();
    CHECK(audio_music_stopped(), "and 6EA0 then stops it");
    audio_music_restart(0);
    audio_music_fade(4);
    audio_advance_ms(2400);
    CHECK(audio_music_stopped(), "FF24 = 4 left alone fades the score out by itself in 2.2 s");
    audio_dump_close();
    remove("/tmp/zel_fade_dsv.wav");
    audio_shutdown();
}


/* ============================ 6. the Tandy / PCjr back end ============= */
/* The register-write stream of MSCJR.DRV / SNDJR.DRV.  tools/jrtrace.py runs the
 * real driver code under a CPU emulator and hashes every byte written to port C0h
 * (with the INT 8 tick it was written on); the hashes below were produced that
 * way, and when python3 + unicorn are available the test runs the oracle live
 * as well.  A script is `music:NAME sfx:ID run:N stop pause:0/1 enable:0/1 fade:RATE`. */
typedef struct { uint32_t h; int n; } ChipLog;

static void chip_tap(void *u, int tick, uint8_t b)
{
    ChipLog *l = u;
    uint8_t v[5] = { (uint8_t)tick, (uint8_t)(tick >> 8), (uint8_t)(tick >> 16), (uint8_t)(tick >> 24), b };
    for (int i = 0; i < 5; i++) { l->h ^= v[i]; l->h *= 16777619u; }
    l->n++;
}

static int run_script(const char *script, ChipLog *l)
{
    char buf[512];
    snprintf(buf, sizeof buf, "%s", script);
    l->h = 2166136261u; l->n = 0;
    audio_init(G_DIR, AUDIO_TANDY, 0);
    audio_chip_tap(chip_tap, l);
    int n; const MusicRes *tab = audio_music_table(&n);
    for (char *tok = strtok(buf, " "); tok; tok = strtok(NULL, " ")) {
        char *c = strchr(tok, ':');
        if (c) *c++ = 0;
        int v = c ? atoi(c) : 0;
        if (!strcmp(tok, "music")) {
            if (!strcmp(c, "zopn")) audio_music_play_res(0, 39);
            else for (int k = 0; k < n; k++) if (!strcmp(tab[k].name, c)) audio_music_force(k);
        }
        else if (!strcmp(tok, "sfx")) audio_sfx_request(v);
        else if (!strcmp(tok, "run")) audio_run_ticks(v);
        else if (!strcmp(tok, "stop")) audio_music_stop();
        else if (!strcmp(tok, "pause")) audio_music_pause(v);
        else if (!strcmp(tok, "enable")) audio_music_enable(v);
        else if (!strcmp(tok, "fade")) audio_music_fade(v);
        else { audio_shutdown(); return -1; }
    }
    audio_shutdown();
    return 0;
}

/* does `python3 tools/jrtrace.py` work here (Unicorn installed)? */
static const char *oracle_python(void)
{
    static const char *py = NULL;
    static int tried = 0;
    if (tried) return py;
    tried = 1;
    const char *want = getenv("ZEL_PYTHON");
    static char cmd[600];
    snprintf(cmd, sizeof cmd, "%s -c 'import unicorn' >/dev/null 2>&1", want ? want : "python3");
    if (system(cmd) == 0) py = want ? want : "python3";
    return py;
}

static int oracle_run(const char *script, uint32_t *h, int *n)
{
    const char *py = oracle_python();
    if (!py) return -1;
    char cmd[1200];
    snprintf(cmd, sizeof cmd, "%s %s/tools/jrtrace.py --quiet --root %s --zeliard %s %s 2>/dev/null",
             py, ROOT, ROOT, G_DIR, script);
    FILE *f = popen(cmd, "r");
    if (!f) return -1;
    char line[128]; unsigned hh = 0; int nn = -1, got = 0;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "fnv32 %x", &hh) == 1) got |= 1;
        if (sscanf(line, "count %d", &nn) == 1) got |= 2;
    }
    pclose(f);
    if (got != 3) return -1;
    *h = hh; *n = nn;
    return 0;
}

static double chip_hz(const Sn76496 *c, int ch) { return SN_CLOCK_TANDY / 32.0 / c->period[ch]; }

static void t_tandy_chip(void)
{
    Sn76496 s;
    sn76496_init(&s, SN_CLOCK_TANDY, 44100.0);
    /* reset: everything attenuated to nothing */
    CHECK(sn76496_tick(&s) == 0.0, "a reset chip is silent");
    /* tone 0, period 427: 3.579545 MHz / 32 / 427 = 262 Hz (the driver's C4) */
    sn76496_write(&s, 0x80 | (427 & 15)); sn76496_write(&s, (uint8_t)(427 >> 4));
    sn76496_write(&s, 0x90);                                  /* attenuation 0 */
    CHECK(s.reg[0] == 427 && s.period[0] == 427, "latch + data byte make a 10-bit period (%d)", s.reg[0]);
    CHECK(fabs(chip_hz(&s, 0) - 262.0) < 0.5, "period 427 is %.1f Hz", chip_hz(&s, 0));
    int toggles = 0, last = s.out[0];
    long ticks = (long)(SN_CLOCK_TANDY / 16.0);               /* one second of chip time */
    for (long i = 0; i < ticks; i++) {
        sn76496_tick(&s);
        if (s.out[0] != last) { toggles++; last = s.out[0]; }
    }
    CHECK(abs(toggles / 2 - 262) <= 1, "one second of ticks = %d square-wave cycles (want 262)", toggles / 2);
    /* attenuation: 2 dB a step, 15 = off */
    CHECK(fabs(s.vol[0] / s.vol[1] - pow(10.0, 0.1)) < 0.001, "2 dB per attenuation step");
    CHECK(s.vol[15] == 0.0, "attenuation 15 is off");
    sn76496_write(&s, 0x9F);
    double mx = 0;
    for (int i = 0; i < 20000; i++) { double v = sn76496_tick(&s); if (v > mx) mx = v; }
    CHECK(mx == 0.0, "attenuation 15 mutes the channel");
    /* a period of 0 behaves as 1024 */
    sn76496_write(&s, 0x80); sn76496_write(&s, 0x00);
    CHECK(s.period[0] == 0x400, "period 0 counts as 1024");
    /* noise: shift rates N/512, N/1024, N/2048, then tone 2's rate */
    sn76496_write(&s, 0xE4); CHECK(s.period[3] == 32,  "noise rate 0 shifts every 32 ticks");
    sn76496_write(&s, 0xE5); CHECK(s.period[3] == 64,  "noise rate 1 every 64");
    sn76496_write(&s, 0xE6); CHECK(s.period[3] == 128, "noise rate 2 every 128");
    sn76496_write(&s, 0xC0 | 5); sn76496_write(&s, 0x02);     /* tone 2 period 0x25 */
    sn76496_write(&s, 0xE7); CHECK(s.period[3] == 2 * s.period[2], "noise rate 3 follows tone 2 (%d)", s.period[3]);
    sn76496_write(&s, 0xE4);
    uint32_t seed = s.rng;
    CHECK(seed == s.fb_mask, "a noise-control write re-seeds the shift register");
    sn76496_write(&s, 0xF0);                                  /* noise attenuation 0 */
    int ones = 0, flips = 0, prev = s.out[3];
    for (int i = 0; i < 32 * 4096; i++) {
        sn76496_tick(&s);
        ones += s.out[3]; if (s.out[3] != prev) { flips++; prev = s.out[3]; }
    }
    double frac = (double)ones / (32 * 4096);
    CHECK(frac > 0.4 && frac < 0.6, "white noise is balanced (%.3f ones)", frac);
    CHECK(flips > 1500, "white noise flips often (%d flips in 4096 shifts)", flips);
    /* periodic noise (FB = 0) repeats; white noise does not within the same span */
    Sn76496 q; sn76496_init(&q, SN_CLOCK_TANDY, 44100.0);
    sn76496_write(&q, 0xE0);                                  /* periodic, fastest */
    int bits[200], period = 0;
    for (int i = 0; i < 200; i++) { for (int k = 0; k < 32; k++) sn76496_tick(&q); bits[i] = q.out[3]; }
    for (int p = 1; p <= 100 && !period; p++) {
        int ok = 1;
        for (int i = 20; i + p < 200; i++) if (bits[i] != bits[i + p]) { ok = 0; break; }
        if (ok) period = p;
    }
    CHECK(period > 0, "periodic noise is a short repeating pattern (period %d)", period);
    /* the NCR 8496 variant ignores data bytes for noise control and volumes */
    Sn76496 r; sn76496_init(&r, SN_CLOCK_TANDY, 44100.0); sn76496_set_lfsr(&r, SN_LFSR_NCR);
    sn76496_write(&r, 0xE5); uint16_t before = r.reg[6]; sn76496_write(&r, 0x02);
    CHECK(r.reg[6] == before, "NCR: a data byte after the noise latch is ignored");
    /* sampling: a held 262 Hz note renders a 262 Hz square wave */
    Sn76496 w; sn76496_init(&w, SN_CLOCK_TANDY, 44100.0);
    sn76496_write(&w, 0x80 | (427 & 15)); sn76496_write(&w, (uint8_t)(427 >> 4)); sn76496_write(&w, 0x90);
    int cross = 0; double pv = 0, lo = 9, hi = -9;
    for (int i = 0; i < 44100; i++) {
        double v = sn76496_sample(&w);
        if (v < lo) lo = v;
        if (v > hi) hi = v;
        if ((v > 0.125) != (pv > 0.125)) cross++;
        pv = v;
    }
    CHECK(abs(cross / 2 - 262) <= 2, "the sample stream is a 262 Hz square wave (%d cycles)", cross / 2);
    CHECK(lo == 0.0 && hi > 0.2 && hi <= 0.25 + 1e-9, "one full-level channel swings 0..0.25 (%.3f..%.3f)", lo, hi);
}


/* a byte recorder for the unit checks below */
typedef struct { uint8_t b[512]; int n; } Rec;
static void rec_sink(void *u, uint8_t b) { Rec *r = u; if (r->n < 512) r->b[r->n++] = b; }

static void t_tandy_regs(void)
{
    const uint8_t *blob; size_t bl; uint8_t *own;
    if (!load_score(4, &blob, &bl, &own)) return;               /* mus1 */
    MsdPlayer p; Rec r;
    memset(&p, 0, sizeof p); memset(&r, 0, sizeof r);
    p.sn = rec_sink; p.sn_u = &r;
    /* INT 60h AX=0 ends in J 02D8: all four attenuations to "off" */
    msd_start(&p, blob, bl, MSD_JR);
    CHECK(r.n == 4 && r.b[0] == 0x9F && r.b[1] == 0xBF && r.b[2] == 0xDF && r.b[3] == 0xFF,
          "AX=0 writes 9F BF DF FF (%d bytes, %02X ...)", r.n, r.b[0]);
    /* the first instrument select (J 04E4) turns that channel's tone off again */
    r.n = 0;
    msd_jr_int8(&p);
    CHECK(r.n > 0 && (r.b[0] & 0x90) == 0x90, "the first tick starts with an attenuation byte (%02X)", r.b[0]);
    /* AX=5: SNDJR takes chip channel 0 and the noise generator (J 0196) */
    r.n = 0;
    msd_claim_sn(&p, 0x09);
    CHECK(r.n == 2 && r.b[0] == 0xFF && r.b[1] == 0x9F,
          "claiming tone 0 + noise silences them (%d bytes: %02X %02X)", r.n, r.b[0], r.b[1]);
    CHECK(p.ch[0].jr_claimed && !p.ch[1].jr_claimed && p.jr_noise_claim, "the claim flags");
    r.n = 0;
    for (int i = 0; i < 4000; i++) msd_jr_int8(&p);
    int own0 = 0;
    for (int i = 0; i < r.n; i++) if ((r.b[i] & 0xE0) == 0x80 && (r.b[i] & 0x80)) own0++;
    CHECK(own0 == 0, "while claimed, the music never writes tone 0's latch bytes (%d)", own0);
    /* CL = 0: give everything back, silence the chip, resend the shadow registers (J 0170) */
    r.n = 0;
    msd_claim_sn(&p, 0);
    CHECK(r.n == 15 && r.b[0] == 0x9F && r.b[1] == 0xBF && r.b[2] == 0xDF && r.b[3] == 0xFF,
          "CL=0 sends 4 silence bytes and 11 shadow bytes (%d)", r.n);
    CHECK(r.n == 15 && r.b[10] == p.jr_noisectl && r.b[11] == p.jr_att[0] && r.b[14] == p.jr_att[3],
          "the shadow image is tone x3, noise control, four attenuations");
    CHECK(!p.ch[0].jr_claimed && !p.jr_noise_claim, "and the claims are cleared");
    /* AX=3 pause: silence now, resend on resume */
    r.n = 0;
    msd_pause(&p, 1);
    CHECK(r.n == 4 && r.b[0] == 0x9F, "pause writes the four silence bytes");
    r.n = 0;
    for (int i = 0; i < 10; i++) msd_jr_int8(&p);
    CHECK(r.n == 0, "a paused driver does not tick");
    msd_pause(&p, 0);
    CHECK(r.n == 15, "resume resends the registers (%d)", r.n);
    /* AX=2 F1: music off is silent and stays silent */
    r.n = 0;
    msd_enable(&p, 0);
    for (int i = 0; i < 400; i++) msd_jr_int8(&p);
    CHECK(r.n == 4, "F1 off writes silence once and then nothing at all (%d bytes)", r.n);
    free(own);
}

/* the chip stream and the (msd2mid-checked) event stream have to agree on pitch */
typedef struct { int type, a, b, c; } SemItem;
typedef struct { SemItem *it; int n, cap; } SemLog;
static void sem_ev(void *u, int tick, int tr, int kind, int a, int b, int c)
{
    SemLog *l = u; (void)tick;
    if (kind != MSD_ON || l->n >= l->cap) return;
    l->it[l->n++] = (SemItem){ 0, tr, a, c };
}
static void sem_sn(void *u, uint8_t b)
{
    SemLog *l = u;
    if (l->n < l->cap) l->it[l->n++] = (SemItem){ 1, b, 0, 0 };
}

static void t_tandy_pitch(void)
{
    int nsc; const MusicRes *tab = audio_music_table(&nsc);
    SemLog l; l.cap = 400000; l.it = malloc((size_t)l.cap * sizeof *l.it);
    long total = 0, exact = 0, octave = 0, other = 0;
    for (int k = 0; k < nsc; k++) {
        const uint8_t *blob; size_t bl; uint8_t *own;
        if (!load_score(k, &blob, &bl, &own)) continue;
        MsdPlayer p; memset(&p, 0, sizeof p);
        p.ev = sem_ev; p.ev_u = &l; p.sn = sem_sn; p.sn_u = &l;
        l.n = 0;
        msd_start(&p, blob, bl, MSD_JR);
        for (int i = 0; i < 12000 && !p.stopped && l.n < l.cap - 64; i++) msd_jr_int8(&p);
        for (int i = 0; i < l.n; i++) {
            if (l.it[i].type != 0 || l.it[i].c) continue;           /* a fresh (non-legato) note-on */
            int ch = l.it[i].a, note = l.it[i].b;
            for (int j = i + 1; j < l.n && j < i + 60; j++) {
                if (l.it[j].type == 0 && l.it[j].a == ch) break;
                if (l.it[j].type == 1 && (l.it[j].a & 0xF0) == (0x80 | ch << 5) &&
                    j + 1 < l.n && l.it[j + 1].type == 1 && !(l.it[j + 1].a & 0x80)) {
                    int per = (l.it[j + 1].a << 4) | (l.it[j].a & 15);
                    int m = (int)lround(69 + 12 * log2(SN_CLOCK_TANDY / 32.0 / per / 440.0));
                    total++;
                    if (m == note || (per < 24 && abs(m - note) <= 1)) exact++;   /* a tiny period quantises coarsely */
                    /* the driver cannot make periods above 1023: the low notes of octave 0/1 are
                     * written an octave up (J 0646) while tools/msd2mid.py maps them straight */
                    else if (m == note + 12 && note >= 36 && note <= 44) octave++;
                    else { other++; if (other < 4) fprintf(stderr, "  %s ch%d note %d sounds as %d\n", tab[k].name, ch, note, m); }
                    break;
                }
            }
        }
        free(own);
    }
    free(l.it);
    CHECK(total > 8000, "the 14 scores yield %ld pitched tone writes", total);
    CHECK(other == 0, "every note the score names is written at its pitch on its chip channel (%ld exact, "
                      "%ld an octave up where the 10-bit period runs out, %ld other)", exact, octave, other);
    CHECK(octave * 4 < total, "the octave clamp is the exception, not the rule (%ld of %ld)", octave, total);
}

static const struct { const char *script; uint32_t h; int n; } TANDY_SCRIPTS[] = {
    { "music:mgt1 run:3000", 0x226792abu, 4689 },
    { "music:mus1 run:4000", 0xaf66a49cu, 4370 },
    { "music:mbos run:3000", 0x910c0ad6u, 3538 },
    { "music:ugm2 run:3000", 0xa012f79au, 3659 },
    { "music:mmao run:3000", 0x1c77bb6au, 4551 },
    { "music:zopn run:3000", 0x7659102bu, 3180 },
    /* effects over music: the claim traffic (AX=5), preemption, a fresh score mid-effect */
    { "music:mus3 run:300 sfx:3 run:400 sfx:6 run:300 sfx:7 run:900", 0xcffd534eu, 3220 },
    { "music:mus5 run:100 sfx:44 run:50 sfx:55 run:1500 sfx:30 run:20 sfx:12 run:600", 0x9ad7f9beu, 4587 },
    { "music:mus8 run:200 sfx:20 run:30 music:mus4 run:500 sfx:21 run:200", 0x59983a49u, 1206 },
    /* AX=3 pause, AX=2 F1, the fade to silence (FF24) */
    { "music:mus2 run:300 pause:1 run:50 sfx:9 run:100 pause:0 run:300 enable:0 run:100 sfx:9 run:200 enable:1 run:300",
      0xb9d81b07u, 1078 },
    { "music:mus6 run:300 fade:3 run:3000", 0x30d1f41eu, 1693 },
};

/* SNDJR effect 1..65, each on its own after AX=1, 3000 INT 8 ticks */
static const struct { uint32_t h; int n; } TANDY_SFX[65] = {
    { 0xcf6e4498u, 44 }, { 0x80d9011cu, 109 }, { 0x02822453u, 91 }, { 0xdd83a150u, 123 }, 
    { 0x72501514u, 44 }, { 0x5a4f381du, 193 }, { 0xfc2b5ed5u, 152 }, { 0xcbf6e461u, 113 }, 
    { 0x88791123u, 136 }, { 0x85780278u, 62 }, { 0x3cb34449u, 150 }, { 0x446e12bfu, 76 }, 
    { 0x9d5b193fu, 76 }, { 0x1cfa5424u, 150 }, { 0x475af826u, 2452 }, { 0xbe107d62u, 80 }, 
    { 0x898aca65u, 79 }, { 0xd14ec1d1u, 165 }, { 0x9a51f8b5u, 44 }, { 0x521b58dcu, 153 }, 
    { 0xbf004608u, 145 }, { 0x3462dbddu, 145 }, { 0xc048a629u, 51 }, { 0x4de4d286u, 154 }, 
    { 0xe8190767u, 132 }, { 0xfd5c433bu, 64 }, { 0x0453b645u, 194 }, { 0xd2af811au, 96 }, 
    { 0x04b79a3bu, 202 }, { 0x1b10e61cu, 204 }, { 0x74329d97u, 151 }, { 0xd8f5ad34u, 313 }, 
    { 0xfca8d556u, 251 }, { 0xad6825b5u, 180 }, { 0xa1e0b362u, 232 }, { 0xe32bbeacu, 183 }, 
    { 0xd200679bu, 183 }, { 0x274a4dddu, 177 }, { 0x1a7cd1fau, 101 }, { 0x87216493u, 560 }, 
    { 0x511bf3dbu, 162 }, { 0xa2cb3cfau, 305 }, { 0x5bea1f51u, 121 }, { 0xc96381d4u, 768 }, 
    { 0x719dfdb5u, 99 }, { 0xb8b9ae62u, 85 }, { 0x04c8c99du, 197 }, { 0xae221ce6u, 455 }, 
    { 0xadb862b5u, 88 }, { 0xb7298e57u, 281 }, { 0x0e935b75u, 388 }, { 0xe72c6249u, 118 }, 
    { 0xaca31719u, 76 }, { 0x6f324c04u, 167 }, { 0x4abccbdbu, 874 }, { 0x5769f35fu, 123 }, 
    { 0x9157f708u, 206 }, { 0x382c7eafu, 55 }, { 0xd2bdbfbfu, 73 }, { 0x2f3f9e89u, 73 }, 
    { 0xb6e5df2bu, 50 }, { 0xff544b1fu, 50 }, { 0x72fe45a3u, 50 }, { 0xc97a5f6du, 50 }, 
    { 0xea91388fu, 50 },
};

static void t_tandy_streams(void)
{
    ChipLog l;
    int live = oracle_python() != NULL;
    if (!live) fprintf(stderr, "  (no python3 with unicorn: the live MSCJR/SNDJR oracle is skipped, golden hashes only)\n");
    for (size_t i = 0; i < sizeof TANDY_SCRIPTS / sizeof TANDY_SCRIPTS[0]; i++) {
        CHECK(run_script(TANDY_SCRIPTS[i].script, &l) == 0, "script runs");
        CHECK(l.h == TANDY_SCRIPTS[i].h && l.n == TANDY_SCRIPTS[i].n,
              "chip writes for `%s`: %08x/%d (want %08x/%d)", TANDY_SCRIPTS[i].script,
              l.h, l.n, TANDY_SCRIPTS[i].h, TANDY_SCRIPTS[i].n);
        uint32_t oh; int on;
        if (live && oracle_run(TANDY_SCRIPTS[i].script, &oh, &on) == 0)
            CHECK(oh == l.h && on == l.n, "the real driver code writes the same bytes for `%s` (%08x/%d vs %08x/%d)",
                  TANDY_SCRIPTS[i].script, oh, on, l.h, l.n);
    }
    int bad = 0, nonempty = 0;
    for (int id = 1; id <= 65; id++) {
        char sc[64];
        snprintf(sc, sizeof sc, "stop sfx:%d run:3000", id);
        run_script(sc, &l);
        if (l.h != TANDY_SFX[id - 1].h || l.n != TANDY_SFX[id - 1].n) {
            bad++;
            fprintf(stderr, "  effect %d: %08x/%d (want %08x/%d)\n", id, l.h, l.n, TANDY_SFX[id - 1].h, TANDY_SFX[id - 1].n);
        }
        if (l.n > 6) nonempty++;
    }
    CHECK(bad == 0, "all 65 SNDJR effects write the bytes the real driver writes (%d differ)", bad);
    CHECK(nonempty == 65, "and every one of them makes sound (%d)", nonempty);
    if (live) {
        int obad = 0;
        for (int id = 1; id <= 65; id += 4) {                    /* the live oracle on every 4th effect */
            char sc[64]; uint32_t oh; int on;
            snprintf(sc, sizeof sc, "stop sfx:%d run:3000", id);
            run_script(sc, &l);
            if (oracle_run(sc, &oh, &on) != 0 || oh != l.h || on != l.n) obad++;
        }
        CHECK(obad == 0, "the live oracle agrees on effects 1, 5, 9 ... (%d differ)", obad);
    }
}

/* the Tandy back end through the front door: selection, WAV render */
static void t_tandy_backend(void)
{
    CHECK(audio_backend_by_name("tandy") == AUDIO_TANDY && audio_backend_by_name("pcjr") == AUDIO_TANDY,
          "--audio tandy / pcjr");
    CHECK(audio_backend_by_name("adlib") == AUDIO_ADLIB && audio_backend_by_name("speaker") == AUDIO_SPEAKER,
          "--audio adlib / speaker");
    CHECK(audio_backend_by_name("sb16") < 0 && audio_backend_by_name(NULL) < 0, "unknown names are rejected");
    CHECK(!strcmp(audio_backend_key(AUDIO_TANDY), "tandy"), "the ini spelling");
    CHECK(audio_sfx_load(G_DIR, AUDIO_TANDY) == 0, "SNDJR.DRV loads");
    CHECK(audio_sfx_count() == 65, "SNDJR.DRV defines the same 65 effects (%d)", audio_sfx_count());
    int prio; unsigned t0, t1, dt;
    CHECK(audio_sfx_entry(0x02, &prio, &t0, &t1, &dt) == 0 && prio == 0xFF,
          "SNDJR effect 02 also has priority FF (%02X)", prio);
    CHECK(audio_sfx_entry(3, &prio, &t0, &t1, &dt) == 0 && prio == 0 && t0 > 0x170E && dt > 0x170E,
          "the effect table at 170E: sword swing tracks %04X %04X, durations %04X", t0, t1, dt);
    audio_shutdown();

    const char *wav = "/tmp/zel_tandy_test.wav";
    audio_init(G_DIR, AUDIO_TANDY, 0);
    CHECK(!strcmp(audio_backend_name(), "Tandy / PCjr (SN76496)"), "backend name");
    CHECK(audio_dump_open(wav) == 0, "WAV dump opens");
    audio_music_force(4);
    audio_advance_ms(2000);
    audio_sfx_request(6);
    audio_advance_ms(500);
    audio_dump_close();
    audio_shutdown();
    FILE *f = fopen(wav, "rb");
    CHECK(f != NULL, "the Tandy WAV was written");
    if (f) {
        fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 44, SEEK_SET);
        long samples = (sz - 44) / 2, nz = 0, clip = 0;
        double peak = 0;
        for (long i = 0; i < samples; i++) {
            unsigned char b[2];
            if (fread(b, 1, 2, f) != 2) break;
            int v = (int16_t)(b[0] | b[1] << 8);
            if (v) nz++;
            if (abs(v) > peak) peak = abs(v);
            if (abs(v) >= 30000) clip++;
        }
        fclose(f);
        CHECK(samples >= 2 * 44100, "2.5 s of audio (%ld samples)", samples);
        CHECK(nz > samples / 2, "the score is audible (%ld non-zero samples)", nz);
        CHECK(peak > 2000 && clip == 0, "at a sensible level (peak %.0f, %ld clipped)", peak, clip);
        remove(wav);
    }
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
        {"silence/dump", t_silence}, {"intro silence", t_intro_silence},
        {"SN76496 core", t_tandy_chip}, {"MSCJR services", t_tandy_regs},
        {"Tandy pitch", t_tandy_pitch}, {"Tandy streams", t_tandy_streams},
        {"Tandy backend", t_tandy_backend},
    };
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; i++) {
        int before = fails;
        tests[i].fn();
        fprintf(stderr, "%-16s %s\n", tests[i].name, fails == before ? "ok" : "FAILED");
    }
    audio_shutdown();                   /* the t_sfx SND*.DRV buffer */
    fprintf(stderr, "%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
