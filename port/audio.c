/* audio.c — the sound back end.
 *
 * Three pieces, in the shape of the originals:
 *   MSC*.DRV   the score interpreter (msd.c), ticked every 2nd INT 8
 *   SND*.DRV   the sound-effect interpreter below, ticked every INT 8, reading
 *              its tracks and OPL patches straight out of the .DRV file
 *   the chip   opl2.c (AdLib) or a PIT-style square wave (PC speaker)
 * The INT 8 clock is the original's 236.7 Hz, derived from the sample counter,
 * so the tempo is right whatever the frame rate is.
 *
 * Hex tags in the effect driver are addresses in disasm/SNDADLIB.asm (`A`) and
 * disasm/SNDSTD.asm (`S`); both files are loaded at (BASE+FF0):1100. */
#include "audio.h"
#include "msd.h"
#include "opl2.h"
#include "sar.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_SDL
#include <SDL.h>
#endif

#define DRV_BASE 0x1100
#define INT8_HZ  (1193182.0 / 0x13B1)          /* 236.70 Hz, PIT divisor 0x13B1 */
double audio_int8_hz(void) { return INT8_HZ; }

/* fight.bin 9E53 (and the same records for the town maps): the level record's
 * music index selects one of these {archive, resource} pairs.  Archive n is
 * ZELRES(n+1).SAR; the driver's own byte is 1-based, so 0x2F -> index 46. */
static const MusicRes MUSIC[14] = {
    { "mgt1", 1, 46 }, { "ugm1", 1, 48 }, { "mgt2", 1, 47 }, { "ugm2", 1, 49 },
    { "mus1", 2, 85 }, { "mus2", 2, 86 }, { "mus3", 2, 87 }, { "mus4", 2, 88 },
    { "mus5", 2, 89 }, { "mus6", 2, 90 }, { "mus7", 2, 91 }, { "mus8", 2, 92 },
    { "mbos", 2, 93 }, { "mmao", 2, 95 },
};
const MusicRes *audio_music_table(int *n) { if (n) *n = 14; return MUSIC; }

/* =====================================================================
 * SND*.DRV — the sound-effect driver
 * ===================================================================== */

typedef struct SfxChan {
    uint16_t pos, durtab;
    uint8_t  opl_ch, claim_bit;
    uint8_t  flags;                 /* bit0 ended, bit6 vibrato on, bit7 direction */
    uint8_t  remaining, att, block, nflags, gate, conn;
    uint16_t fnum_block, divisor;
    int8_t   detune;
    int16_t  vib_offset;
    uint8_t  vib_frac, vib_delay, vib_note_scale, vib_start;
    uint8_t  vib_mul_up, vib_mul_down, vib_len_up, vib_len_down, vib_ctl, vib_half;
    uint16_t vib_step_up, vib_step_down;
    uint16_t patch;
} SfxChan;

typedef struct Sfx {
    uint8_t *drv;
    size_t   len;
    int      adlib;
    unsigned tab, entry, neff;       /* effect table, bytes per record, records */
    unsigned patches, fnum, vibsc, opoff, pitch;
    SfxChan  ch[2];
    int      nch;
    uint8_t  tempo, acc, skip, prio, idle, ended, claim, off;
    uint16_t durtabs;
    /* output hooks */
    Opl2    *opl;
    uint8_t  spk_own;               /* S 12E2: INT 60h AX=4, the speaker is ours */
    uint16_t spk_div;
} Sfx;

static uint8_t  sb(const Sfx *s, unsigned a)
{ a -= DRV_BASE; return a < s->len ? s->drv[a] : 0xFF; }
static uint16_t sw(const Sfx *s, unsigned a) { return (uint16_t)(sb(s, a) | sb(s, a + 1) << 8); }
static uint8_t  sfetch(Sfx *s, SfxChan *c) { return sb(s, c->pos++); }

static void sfx_opl(Sfx *s, uint8_t r, uint8_t v) { if (s->opl) opl2_write(s->opl, r, v); }

/* A 1319: carrier (and, for additive patches, modulator) level = patch level +
 * attenuation/2, clamped to 3F. */
static void sfx_set_level(Sfx *s, SfxChan *c)
{
    if (!s->adlib) return;
    unsigned m = sb(s, s->opoff + c->opl_ch);
    unsigned q = c->patch;
    int bl = c->att >> 1;
    int mod = sb(s, q + 2);
    if (c->conn & 1) { int l = (mod & 0x3F) + bl; if (l > 0x3F) l = 0x3F; mod = (mod & 0xC0) | l; }
    sfx_opl(s, (uint8_t)(0x40 + m), (uint8_t)mod);
    int car = sb(s, q + 3);
    int l = (car & 0x3F) + bl; if (l > 0x3F) l = 0x3F;
    sfx_opl(s, (uint8_t)(0x43 + m), (uint8_t)((car & 0xC0) | l));
}

/* A 1433 */
static void sfx_write_freq(Sfx *s, SfxChan *c)
{
    if (!s->adlib) return;
    unsigned v = (unsigned)((c->fnum_block + c->vib_offset) & 0x1FFF);
    if (c->nflags & 0x40) v |= 0x2000;
    sfx_opl(s, (uint8_t)(0xA0 + c->opl_ch), (uint8_t)(v & 0xFF));
    sfx_opl(s, (uint8_t)(0xB0 + c->opl_ch), (uint8_t)(v >> 8));
}

/* A 125F / S 1231 */
static void sfx_vib_load(Sfx *s, SfxChan *c, unsigned si)
{
    c->flags &= (uint8_t)~0x40;
    uint8_t d = sb(s, si);
    if (!d) return;
    c->flags |= 0x40;
    c->vib_start = d;
    c->vib_mul_up = sb(s, si + 1); c->vib_mul_down = sb(s, si + 2);
    c->vib_len_up = sb(s, si + 3); c->vib_len_down = sb(s, si + 4);
    c->vib_ctl = sb(s, si + 5);
    c->nflags &= (uint8_t)~0x02;
}

/* A 1292: 9-byte OPL patch {20,23,40,43,60,63,80,83, waveform/feedback} */
static void sfx_patch(Sfx *s, SfxChan *c, int n)
{
    if (!s->adlib) return;
    unsigned q = s->patches + 9u * (unsigned)n;
    c->patch = (uint16_t)q;
    unsigned m = sb(s, s->opoff + c->opl_ch);
    sfx_opl(s, (uint8_t)(0x40 + m), 0xFF); sfx_opl(s, (uint8_t)(0x43 + m), 0xFF);
    sfx_opl(s, (uint8_t)(0x20 + m), sb(s, q + 0));
    sfx_opl(s, (uint8_t)(0x23 + m), sb(s, q + 1));
    sfx_opl(s, (uint8_t)(0x60 + m), sb(s, q + 4));
    sfx_opl(s, (uint8_t)(0x63 + m), sb(s, q + 5));
    sfx_opl(s, (uint8_t)(0x80 + m), sb(s, q + 6));
    sfx_opl(s, (uint8_t)(0x83 + m), sb(s, q + 7));
    uint8_t b8 = sb(s, q + 8);
    sfx_opl(s, (uint8_t)(0xE0 + m), (uint8_t)(((b8 << 2) | (b8 >> 6)) & 3));
    sfx_opl(s, (uint8_t)(0xE3 + m), (uint8_t)(((b8 << 4) | (b8 >> 4)) & 3));
    c->conn = (uint8_t)(b8 & 0x0F);
    sfx_opl(s, (uint8_t)(0xC0 + c->opl_ch), (uint8_t)(b8 & 0x0F));
    sfx_set_level(s, c);
    s->claim |= c->claim_bit;                       /* A 130F */
}

/* A 1409 / S 12F3 */
static void sfx_set_pitch(Sfx *s, SfxChan *c, int p)
{
    int i = p - 1; if (i < 0) i = 0; if (i > 11) i = 11;
    c->vib_note_scale = sb(s, s->vibsc + i);
    if (s->adlib)
        c->fnum_block = (uint16_t)((sw(s, s->fnum + 2 * i) + (int16_t)c->detune) | (c->block << 10));
    else
        c->divisor = (uint16_t)((sw(s, s->pitch + 2 * i) + (int16_t)c->detune) >> (c->block & 7));
}

static void sfx_release(Sfx *s, SfxChan *c)         /* A 142D */
{
    c->nflags &= (uint8_t)~0x40;
    if (s->adlib) sfx_write_freq(s, c);
    else s->spk_own = 0;
}

static void sfx_end_track(Sfx *s, SfxChan *c)       /* A 150C / S 1400 range */
{
    c->flags |= 1;
    if (++s->ended >= s->nch) {
        s->idle = 0xFF; s->prio = 0; s->claim = 0;
        if (!s->adlib) s->spk_own = 0;
    }
}

static void sfx_command(Sfx *s, SfxChan *c, uint8_t b)
{
    if (b < 0xC0) {                                  /* 80-BF patch */
        if (s->adlib) sfx_patch(s, c, b & 0x3F);
        return;
    }
    if (b < 0xD0) {                                  /* A 1367: relative volume */
        int v = (int8_t)(uint8_t)(b << 4) >> 2;
        int bl = c->att;
        if (v >= 0) { bl = (bl - (v + 4)) & 0xFF; if (bl & 0xC0) bl = 0; }
        else        { bl = (bl - v) & 0xFF;       if (bl & 0xC0) bl = 0x3F; }
        c->att = (uint8_t)bl;
        sfx_set_level(s, c);
        return;
    }
    if (b < 0xD8) { c->block = (uint8_t)(b & 7); return; }
    if (b < 0xE0) { c->gate = sb(s, c->durtab + (b & 7)); return; }
    switch (b) {
    case 0xE0: s->tempo = sfetch(s, c); return;
    case 0xE1: c->detune = (int8_t)sfetch(s, c); return;
    case 0xE2: { uint8_t d = sb(s, c->pos); sfx_vib_load(s, c, c->pos); c->pos++; if (d) c->pos += 5; return; }
    case 0xE3: c->block--; return;
    case 0xE4: c->block++; return;
    case 0xE5:
        if (s->adlib) { c->att = sfetch(s, c); sfx_set_level(s, c); }
        else sfetch(s, c);                            /* S 125D: the speaker has no volume */
        return;
    case 0xE7: c->nflags |= 0x20; return;
    case 0xE9: case 0xEA: case 0xEB:
        if (!s->adlib) sfetch(s, c);                  /* S 125D */
        return;
    case 0xF0: c->durtab = (uint16_t)(s->durtabs + 8u * sfetch(s, c)); return;
    case 0xFF: sfx_end_track(s, c); return;
    default: return;                                  /* E6/E8/EC-EF and F1-FE are nops */
    }
}

static void sfx_note(Sfx *s, SfxChan *c, uint8_t b)   /* A 13B0 / S 1282 */
{
    c->nflags &= (uint8_t)~0x10;
    if (sb(s, c->pos) == 0xE7) c->nflags |= 0x10;
    c->remaining = sb(s, c->durtab + (b >> 4));
    if (!c->remaining) c->remaining = 1;
    int p = b & 0x0F;
    if (p == 0) { sfx_release(s, c); return; }
    if (p == 0x0F) return;
    sfx_set_pitch(s, c, p);
    uint8_t n = c->nflags;
    c->nflags &= (uint8_t)~0x20;
    if (!(n & 0x20)) {
        c->vib_delay = c->vib_start; c->vib_offset = 0; c->vib_frac = 0x80;
        c->nflags &= (uint8_t)~0x02;
        c->nflags |= 0x40;
        if (!s->adlib) s->spk_own = 0xFF;             /* S 12E2: int60 AX=4 CL=FF */
    }
    if (s->adlib) sfx_write_freq(s, c);
}

static void sfx_vibrato(Sfx *s, SfxChan *c)           /* A 1458 / S 1332 */
{
    if (!(c->flags & 0x40)) return;
    if (--c->vib_delay) return;
    if (!(c->nflags & 0x02)) {
        unsigned shift = s->adlib ? 0 : (c->block & 7);
        c->vib_step_up = (uint16_t)((c->vib_mul_up * c->vib_note_scale) >> shift);
        c->vib_step_down = (uint16_t)((c->vib_mul_down * c->vib_note_scale) >> shift);
        c->vib_half = (uint8_t)(((c->vib_ctl & 0x80) ? c->vib_len_down : c->vib_len_up) >> 1);
        c->vib_frac = 0x80;
        c->flags = (uint8_t)((c->flags & 0x7F) | (c->vib_ctl & 0x80));
        c->nflags |= 0x02;
    }
    c->vib_delay = (uint8_t)(c->vib_ctl & 0x1F);
    if (!c->vib_delay) c->vib_delay = 1;
    if (--c->vib_half == 0) {
        if (c->flags & 0x80) { c->vib_half = c->vib_len_up; c->flags &= (uint8_t)~0x80; }
        else                 { c->vib_half = c->vib_len_down; c->flags |= 0x80; }
        if (!c->vib_half) c->vib_half = 1;
    }
    if (!(c->flags & 0x80)) {
        unsigned f = c->vib_frac + (c->vib_step_up & 0xFF);
        c->vib_frac = (uint8_t)f;
        c->vib_offset = (int16_t)(c->vib_offset + (c->vib_step_up >> 8) + (f >> 8));
    } else {
        int f = (int)c->vib_frac - (int)(c->vib_step_down & 0xFF);
        c->vib_frac = (uint8_t)f;
        c->vib_offset = (int16_t)(c->vib_offset - (c->vib_step_down >> 8) - (f < 0 ? 1 : 0));
    }
    if (s->adlib) sfx_write_freq(s, c);
}

static void sfx_chan_tick(Sfx *s, SfxChan *c)         /* A 11B4 / S 118A */
{
    if (c->flags & 1) return;                         /* A 11BA: ended, no vibrato */
    if (s->skip) { sfx_vibrato(s, c); return; }       /* A 11C6 falls into 1458   */
    if (--c->remaining != 0) {
        if (c->gate >= c->remaining && !(c->nflags & 0x10)) sfx_release(s, c);
        sfx_vibrato(s, c);
        return;
    }
    for (int guard = 0; guard < 4096; guard++) {
        uint8_t b = sfetch(s, c);
        if (b < 0x80) { sfx_note(s, c, b); break; }
        sfx_command(s, c, b);
        if (c->flags & 1) break;
    }
    sfx_vibrato(s, c);
}

/* A 110D / S 110D: the FF75 request */
static void sfx_start(Sfx *s, int id)
{
    if (s->off || id <= 0 || (unsigned)id > s->neff) return;
    unsigned rec = s->tab + s->entry * (unsigned)(id - 1);
    uint8_t prio = sb(s, rec);
    if (prio < s->prio) return;                       /* A 112A: lower priority loses */
    s->prio = prio;
    for (int i = 0; i < s->nch; i++) {
        SfxChan *c = &s->ch[i];
        memset(c, 0, sizeof *c);
        c->pos = sw(s, rec + 1 + 2 * i);
        c->remaining = 1; c->block = 3; c->gate = 1; c->att = 0x7F;
        c->opl_ch = (uint8_t)(4 + i); c->claim_bit = (uint8_t)(1 + i);
    }
    s->durtabs = sw(s, rec + 1 + 2 * s->nch);
    s->idle = 0; s->ended = 0; s->tempo = 0x7F; s->acc = 0; s->claim = 0;
    if (!s->adlib) s->spk_own = 0;
}

/* A 1188 / S 1166: one INT 8 tick */
static void sfx_tick(Sfx *s, int *request)
{
    if (*request) { sfx_start(s, *request); *request = 0; }
    if (s->idle || !s->drv) return;
    unsigned a = (unsigned)s->acc + s->tempo;
    s->skip = (uint8_t)(a > 0xFF ? 0xFF : 0);
    s->acc = (uint8_t)a;
    for (int i = 0; i < s->nch; i++) sfx_chan_tick(s, &s->ch[i]);
    if (!s->adlib) {
        SfxChan *c = &s->ch[0];
        s->spk_div = (uint16_t)((c->divisor + c->vib_offset) << 3);
    }
}

static int sfx_load(Sfx *s, const char *dir, int adlib)
{
    FILE *f = game_fopen(dir, adlib ? "SNDADLIB.DRV" : "SNDSTD.DRV");
    if (!f) return -1;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 0x600) { fclose(f); return -1; }
    uint8_t *d = malloc((size_t)n);
    if (!d || fread(d, 1, (size_t)n, f) != (size_t)n) { free(d); fclose(f); return -1; }
    fclose(f);
    free(s->drv);
    memset(s, 0, sizeof *s);
    s->drv = d; s->len = (size_t)n; s->adlib = adlib; s->idle = 0xFF;
    if (adlib) {
        s->tab = 0x1743; s->entry = 7; s->nch = 2;
        s->patches = 0x2020; s->fnum = 0x1576; s->vibsc = 0x158E; s->opoff = 0x159A;
    } else {
        s->tab = 0x1502; s->entry = 5; s->nch = 1;
        s->pitch = 0x1430; s->vibsc = 0x1448;
    }
    /* the records run up to the first track, which gives the effect count */
    unsigned first = sw(s, s->tab + 1);
    s->neff = first > s->tab ? (first - s->tab) / s->entry : 0;
    return 0;
}

/* =====================================================================
 * The mixer / front end
 * ===================================================================== */

typedef struct Audio {
    int      on, backend, rate;
    const char *dir;
    Opl2     opl;
    MsdPlayer music;
    Sfx      sfx;
    uint8_t *score;                  /* the resource the player is reading */
    int      music_idx, music_locked, music_hold;
    int      sfx_request;
    double   tick_acc;
    int      div2;
    /* a one-pole DC blocker: the half/abs-sine OPL waveforms have a large DC
     * component that the real card's output stage removes */
    double   dc_x1, dc_y1;
    /* PC speaker */
    double   spk_phase;
    uint16_t spk_div;
    int      spk_on;
    /* WAV dump */
    FILE    *wav;
    long     wav_frames;
    /* SDL */
    int      dev_open;
#ifdef HAVE_SDL
    SDL_AudioDeviceID dev;
#endif
} Audio;

static Audio A;

static void opl_sink(void *u, uint8_t reg, uint8_t val) { opl2_write(&((Audio *)u)->opl, reg, val); }

static void audio_lock(void)
{
#ifdef HAVE_SDL
    if (A.dev_open) SDL_LockAudioDevice(A.dev);
#endif
}
static void audio_unlock(void)
{
#ifdef HAVE_SDL
    if (A.dev_open) SDL_UnlockAudioDevice(A.dev);
#endif
}

/* one 236.7 Hz INT 8 tick: the sound driver every tick, the music driver every
 * second one (STICK's [FF10] then [FF0C], docs/MUSIC.md section 1) */
static void int8_tick(void)
{
    uint8_t was = A.sfx.claim;
    sfx_tick(&A.sfx, &A.sfx_request);
    if (A.backend == AUDIO_ADLIB && A.sfx.claim != was) msd_claim_opl(&A.music, A.sfx.claim);
    if (--A.div2 <= 0) {
        A.div2 = 2;
        msd_driver_tick(&A.music);
        if (A.backend == AUDIO_SPEAKER) {
            uint16_t d = msd_speaker_divisor(&A.music);
            A.spk_div = d; A.spk_on = d != 0;
        }
    }
    if (A.backend == AUDIO_SPEAKER && A.sfx.spk_own) { A.spk_div = A.sfx.spk_div; A.spk_on = 1; }
}

static double speaker_sample(void)
{
    if (!A.spk_on || A.spk_div < 2) return 0.0;
    double hz = 1193182.0 / A.spk_div;
    A.spk_phase += hz / A.rate;
    if (A.spk_phase >= 1.0) A.spk_phase -= floor(A.spk_phase);
    return A.spk_phase < 0.5 ? 0.35 : -0.35;
}

static void render(float *out, int n)
{
    for (int i = 0; i < n; i++) {
        A.tick_acc += INT8_HZ / A.rate;
        while (A.tick_acc >= 1.0) { A.tick_acc -= 1.0; int8_tick(); }
        double v = A.backend == AUDIO_ADLIB ? opl2_sample(&A.opl) : speaker_sample();
        double y = v - A.dc_x1 + 0.9993 * A.dc_y1;
        A.dc_x1 = v; A.dc_y1 = y; v = y;
        if (v > 1.0) v = 1.0;
        if (v < -1.0) v = -1.0;
        out[i] = (float)v;
    }
}

#ifdef HAVE_SDL
static void sdl_cb(void *u, Uint8 *stream, int bytes)
{
    (void)u;
    int n = bytes / (int)sizeof(int16_t);
    static float buf[4096];
    int16_t *o = (int16_t *)stream;
    while (n > 0) {
        int k = n > 4096 ? 4096 : n;
        render(buf, k);
        for (int i = 0; i < k; i++) o[i] = (int16_t)(buf[i] * 30000.0f);
        o += k; n -= k;
    }
}
#endif

int audio_sfx_load(const char *dir, int backend)
{
    return sfx_load(&A.sfx, dir, backend == AUDIO_ADLIB);
}

int audio_init(const char *dir, int backend, int want_sdl)
{
    memset(&A, 0, sizeof A);
    A.dir = dir; A.backend = backend; A.rate = 44100; A.music_idx = -1; A.div2 = 1;
    if (sfx_load(&A.sfx, dir, backend == AUDIO_ADLIB) != 0)
        fprintf(stderr, "note: no %s (no sound effects)\n",
                backend == AUDIO_ADLIB ? "SNDADLIB.DRV" : "SNDSTD.DRV");
    A.sfx.opl = &A.opl;
    A.music.opl = opl_sink; A.music.opl_u = &A;
    A.music.stopped = 0xFF;
#ifdef HAVE_SDL
    if (want_sdl) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            fprintf(stderr, "note: no audio device (%s)\n", SDL_GetError());
        } else {
            SDL_AudioSpec want, have;
            memset(&want, 0, sizeof want);
            want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 1;
            want.samples = 1024; want.callback = sdl_cb;
            A.dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
            if (!A.dev) fprintf(stderr, "note: no audio device (%s)\n", SDL_GetError());
            else { A.rate = have.freq; A.dev_open = 1; }
        }
    }
#else
    (void)want_sdl;
#endif
    opl2_reset(&A.opl, A.rate);
    A.on = 1;
#ifdef HAVE_SDL
    if (A.dev_open) SDL_PauseAudioDevice(A.dev, 0);
#endif
    return 0;
}

void audio_shutdown(void)
{
    if (!A.on) return;
    audio_dump_close();
#ifdef HAVE_SDL
    if (A.dev_open) { SDL_CloseAudioDevice(A.dev); A.dev_open = 0; }
#endif
    free(A.sfx.drv); A.sfx.drv = NULL;
    free(A.score); A.score = NULL;
    A.on = 0;
}

int audio_active(void) { return A.on; }
const char *audio_backend_name(void) { return A.backend == AUDIO_ADLIB ? "AdLib (OPL2)" : "PC speaker"; }

/* ------------------------------------------------------------ WAV dump */
static void wav_header(FILE *f, int rate, long frames)
{
    unsigned data = (unsigned)(frames * 2), riff = 36 + data;
    uint8_t h[44];
    memcpy(h, "RIFF", 4);
    h[4] = (uint8_t)riff; h[5] = (uint8_t)(riff >> 8); h[6] = (uint8_t)(riff >> 16); h[7] = (uint8_t)(riff >> 24);
    memcpy(h + 8, "WAVEfmt ", 8);
    h[16] = 16; h[17] = h[18] = h[19] = 0;
    h[20] = 1; h[21] = 0; h[22] = 1; h[23] = 0;
    h[24] = (uint8_t)rate; h[25] = (uint8_t)(rate >> 8); h[26] = (uint8_t)(rate >> 16); h[27] = (uint8_t)(rate >> 24);
    unsigned br = (unsigned)rate * 2;
    h[28] = (uint8_t)br; h[29] = (uint8_t)(br >> 8); h[30] = (uint8_t)(br >> 16); h[31] = (uint8_t)(br >> 24);
    h[32] = 2; h[33] = 0; h[34] = 16; h[35] = 0;
    memcpy(h + 36, "data", 4);
    h[40] = (uint8_t)data; h[41] = (uint8_t)(data >> 8); h[42] = (uint8_t)(data >> 16); h[43] = (uint8_t)(data >> 24);
    fwrite(h, 1, 44, f);
}

int audio_dump_open(const char *path)
{
    if (!A.on) return -1;
    A.wav = fopen(path, "wb");
    if (!A.wav) return -1;
    wav_header(A.wav, A.rate, 0);
    A.wav_frames = 0;
    return 0;
}

void audio_dump_close(void)
{
    if (!A.wav) return;
    fseek(A.wav, 0, SEEK_SET);
    wav_header(A.wav, A.rate, A.wav_frames);
    fclose(A.wav);
    A.wav = NULL;
}

void audio_advance_ms(double ms)
{
    if (!A.wav) return;
    int n = (int)(ms * A.rate / 1000.0 + 0.5);
    static float buf[2048];
    static int16_t pcm[2048];
    while (n > 0) {
        int k = n > 2048 ? 2048 : n;
        render(buf, k);
        for (int i = 0; i < k; i++) pcm[i] = (int16_t)(buf[i] * 30000.0f);
        fwrite(pcm, sizeof(int16_t), (size_t)k, A.wav);
        A.wav_frames += k;
        n -= k;
    }
}

/* ------------------------------------------------------------- control */
void audio_music(int idx)
{
    if (!A.on || A.music_locked || A.music_hold || idx < 0 || idx >= 14) return;
    if (idx == A.music_idx && !A.music.stopped) return;
    size_t len;
    uint8_t *res = sar_load(A.dir, MUSIC[idx].archive, MUSIC[idx].index, 1, &len);
    if (!res) { fprintf(stderr, "note: cannot load %s.MSD\n", MUSIC[idx].name); return; }
    const uint8_t *blob; size_t bl;
    if (msd_split(res, len, &blob, &bl)) { free(res); return; }
    audio_lock();
    free(A.score);
    A.score = res;
    A.music.opl = opl_sink; A.music.opl_u = &A;
    if (msd_start(&A.music, A.score + (blob - res), bl,
                  A.backend == AUDIO_ADLIB ? MSD_ADLIB : MSD_STD) != 0)
        A.music.stopped = 0xFF;
    A.music_idx = idx;
    audio_unlock();
}

/* GAME.BIN A080: with no command-line argument the boot path jumps straight
 * into opdemo, *before* any level record is read, so nothing sounds until the
 * demo starts its own score (zopn at opdemo 6223).  The port has to build its
 * scaffold Game first, and fight.bin 7E93's level-record score would then play
 * over the silent prologue (#37) -- hold it until the demo has handed back.
 * Only the 9E53 table path is held: audio_music_play_res(), which is how the
 * cutscene overlays call INT 60h AX=0, is not. */
void audio_music_hold(int on)
{
    A.music_hold = on ? 1 : 0;
}

void audio_music_force(int idx)
{
    A.music_locked = 0;
    A.music_hold = 0;                 /* --music N is an explicit override */
    if (idx < 0) audio_music_stop();
    else audio_music(idx);
    A.music_locked = 1;
}

/* the cutscene overlays call INT 60h AX=0 with DS:SI = a score they loaded
 * themselves (zopn.msd, zend.msd, mfan.msd), which the 9E53 table does not
 * name; audio_music() only knows that table, so this is the raw form. */
void audio_music_play_res(int archive, int index)
{
    if (!A.on) return;
    size_t len;
    uint8_t *res = sar_load(A.dir, archive, index, 1, &len);
    if (!res) return;
    const uint8_t *blob; size_t bl;
    if (msd_split(res, len, &blob, &bl)) { free(res); return; }
    audio_lock();
    free(A.score);
    A.score = res;
    A.music.opl = opl_sink; A.music.opl_u = &A;
    if (msd_start(&A.music, A.score + (blob - res), bl,
                  A.backend == AUDIO_ADLIB ? MSD_ADLIB : MSD_STD) != 0)
        A.music.stopped = 0xFF;
    A.music_idx = -1;
    audio_unlock();
}

int audio_music_stopped(void)
{
    return A.on ? (A.music.stopped != 0) : 1;
}

int audio_music_sync0(void)
{
    return A.on ? A.music.sync[0] : 0;
}

void audio_music_sync0_clear(void)
{
    if (A.on) { audio_lock(); A.music.sync[0] = 0; audio_unlock(); }
}

void audio_music_stop(void)
{
    if (!A.on) return;
    audio_lock(); msd_stop(&A.music); A.music_idx = -1; audio_unlock();
}
void audio_music_pause(int on)
{
    if (!A.on) return;
    audio_lock(); msd_pause(&A.music, on); audio_unlock();
}
void audio_music_fade(int rate)
{
    if (!A.on) return;
    audio_lock(); A.music.sync[3] = (uint8_t)rate; A.music.fade_wait = 1; audio_unlock();
}
/* F1: INT 60h AX=2 CL = the current off-flag.  Returns the new state. */
int audio_music_enable(int on)
{
    if (!A.on) return 0;
    audio_lock(); msd_enable(&A.music, on); audio_unlock();
    return on;
}
/* F2: FF27, the sound driver's own off flag (SNDADLIB 1115) */
int audio_sfx_enable(int on)
{
    if (!A.on) return 0;
    audio_lock();
    A.sfx.off = (uint8_t)(on ? 0 : 0xFF);
    if (!on) { A.sfx.idle = 0xFF; A.sfx.prio = 0; A.sfx.claim = 0; A.sfx_request = 0; }
    audio_unlock();
    return on;
}

void audio_sfx_request(int id)
{
    if (!A.on || id <= 0) return;
    audio_lock(); A.sfx_request = id; audio_unlock();
}

int audio_sfx_count(void) { return (int)A.sfx.neff; }

int audio_sfx_entry(int id, int *prio, unsigned *t0, unsigned *t1, unsigned *durtab)
{
    Sfx *s = &A.sfx;
    if (!s->drv || id <= 0 || (unsigned)id > s->neff) return -1;
    unsigned rec = s->tab + s->entry * (unsigned)(id - 1);
    if (prio) *prio = sb(s, rec);
    if (t0) *t0 = sw(s, rec + 1);
    if (t1) *t1 = s->nch > 1 ? sw(s, rec + 3) : 0;
    if (durtab) *durtab = sw(s, rec + 1 + 2 * s->nch);
    return 0;
}
