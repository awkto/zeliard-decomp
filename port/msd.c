/* msd.c — the .msd score interpreter (MSCADLIB.DRV / MSCSTD.DRV in C).
 *
 * Hex tags are addresses in the original drivers: `A xxxx` = MSCADLIB.DRV,
 * `S xxxx` = MSCSTD.DRV (src/music_adlib.c, src/music_std.c, docs/MUSIC.md).
 * The event log mirrors tools/msd2mid.py exactly so the two can be diffed. */
#include "msd.h"
#include <math.h>
#include <string.h>

/* ---------------------------------------------------------------- tables */
/* A 0B51: semitone -> OPL2 f-number (block 4, pitch 1 = 0x156 = 259 Hz ~ C4) */
static const uint16_t FNUM[12] =
    { 0x156, 0x16B, 0x181, 0x197, 0x1B0, 0x1C9, 0x1E4, 0x201, 0x220, 0x240, 0x263, 0x287 };
/* A 0B69 */
static const uint8_t VIBSCALE_A[12] = { 0x13, 0x14, 0x15, 0x16, 0x18, 0x19, 0x1B, 0x1C, 0x1E, 0x20, 0x22, 0x24 };
/* S 08B2 / S 08CA: PIT divisor before the octave shift, and the vibrato scale */
static const uint16_t PITDIV[12] = { 2280, 2152, 2031, 1917, 1809, 1708, 1612, 1521, 1436, 1355, 1279, 1207 };
static const uint8_t VIBSCALE_S[12] = { 0x80, 0x78, 0x72, 0x6B, 0x65, 0x5F, 0x5A, 0x55, 0x50, 0x4C, 0x47, 0x43 };
/* A 0B75 */
static const uint8_t OPOFF[9] = { 0x00, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x10, 0x11, 0x12 };
/* A 0B7E: the driver's own percussion patches for OPL channels 6, 7, 8 */
static const uint8_t PERC[3][9] = {
    { 0x00, 0x00, 0x0B, 0x40, 0xA8, 0xD6, 0xBC, 0xBF, 0x00 },
    { 0x01, 0x0C, 0x00, 0x00, 0xD8, 0xC7, 0x68, 0x46, 0x0F },
    { 0x02, 0x88, 0x00, 0x00, 0xC8, 0xF5, 0x67, 0x65, 0x00 },
};
/* tools/msd2mid.py DRUM_NOTES / DRUM_LEVEL_IDX, in dict order: BD SD TT CY HH */
static const uint8_t DRUM_BIT[5] = { 0x10, 0x08, 0x04, 0x02, 0x01 };
static const uint8_t DRUM_NOTE[5] = { 36, 38, 45, 49, 42 };
static const uint8_t DRUM_LEV[5] = { 0, 2, 3, 4, 1 };

#define INT8_HZ   (1193182.0 / 0x13B1)      /* 236.70 Hz */
#define DRIVER_HZ (INT8_HZ / 2.0)           /* 118.35 Hz */

double msd_score_hz(int T) { return DRIVER_HZ * (256 - T) / 256.0; }
double msd_us_per_quarter(int T) { return 24.0 * 1e6 / msd_score_hz(T); }

/* ------------------------------------------------------------- accessors */
static uint8_t  rd8(const MsdPlayer *p, unsigned o)  { return o < p->blen ? p->b[o] : 0xFF; }
static uint16_t rd16(const MsdPlayer *p, unsigned o) { return (uint16_t)(rd8(p, o) | rd8(p, o + 1) << 8); }
static uint8_t  fetch8(MsdPlayer *p, MsdChan *c)     { return rd8(p, c->pos++); }
static int8_t   fetchi8(MsdPlayer *p, MsdChan *c)    { return (int8_t)fetch8(p, c); }
static uint16_t fetch16(MsdPlayer *p, MsdChan *c)    { uint16_t v = rd16(p, c->pos); c->pos += 2; return v; }
static uint8_t  dur(MsdPlayer *p, MsdChan *c, int i) { return c->durtab == 0xFFFF ? 1 : rd8(p, c->durtab + i); }

static void opl(MsdPlayer *p, uint8_t reg, uint8_t val)      /* A 0B08 */
{ if (p->opl && !p->music_off) p->opl(p->opl_u, reg, val); }
static void oplc(MsdPlayer *p, MsdChan *c, uint8_t r, uint8_t v)  /* A 0B00 */
{ if (!c->muted) opl(p, r, v); }
static void oplraw(MsdPlayer *p, uint8_t reg, uint8_t val)
{ if (p->opl) p->opl(p->opl_u, reg, val); }

static void logev(MsdPlayer *p, MsdChan *c, int kind, int a, int b, int d)
{ if (p->ev) p->ev(p->ev_u, p->tick, c->idx, kind, a, b, d); }

/* msd2mid.py att_to_velocity: OPL total-level units (0.75 dB) -> velocity on
 * the GM curve.  rint() is round-half-to-even, like Python's round(). */
static int att_to_vel(int units)
{
    double v = 127.0 * pow(10.0, -(0.75 * units) / 40.0);
    long r = (long)rint(v);
    if (r < 8) r = 8;
    if (r > 127) r = 127;
    return (int)r;
}

/* --------------------------------------------------------------- OPL out */
static void set_level(MsdPlayer *p, MsdChan *c)                /* A 0630 */
{
    if (p->mode != MSD_ADLIB || (c->opl_ch & 0x80)) return;
    int m = OPOFF[c->opl_ch];
    int att = (c->lev[0] >> 1) + (p->sync[4] >> 2);
    int mod = rd8(p, c->patch + 8);
    if (c->conn & 1) {
        int l = (mod & 0x3F) + att; if (l > 0x3F) l = 0x3F;
        mod = (mod & 0xC0) | l;
    }
    oplc(p, c, (uint8_t)(0x40 + m), (uint8_t)mod);
    int car = rd8(p, c->patch + 9);
    int l = (car & 0x3F) + att; if (l > 0x3F) l = 0x3F;
    oplc(p, c, (uint8_t)(0x43 + m), (uint8_t)((car & 0xC0) | l));
}

static void rhythm_levels(MsdPlayer *p, MsdChan *c)            /* A 0988 */
{
    if (p->mode != MSD_ADLIB) return;
    static const uint8_t REG[5] = { 0x53, 0x51, 0x54, 0x52, 0x55 };
    int f = p->sync[4] >> 2;
    for (int i = 0; i < 5; i++) {
        int v = c->lev[i] + f; if (v > 0x3F) v = 0x3F;
        opl(p, REG[i], (uint8_t)v);
    }
}

static void cmd_vibrato_from(MsdPlayer *p, MsdChan *c, unsigned si)  /* S 0412 */
{
    c->flags &= (uint8_t)~0x40;
    uint8_t d = rd8(p, si);
    if (d == 0) return;
    c->flags |= 0x40;
    c->vib_start = d;
    c->vib_mul_up = rd8(p, si + 1); c->vib_mul_down = rd8(p, si + 2);
    c->vib_len_up = rd8(p, si + 3); c->vib_len_down = rd8(p, si + 4);
    c->vib_ctl = rd8(p, si + 5);
    c->nflags &= (uint8_t)~0x02;
}

static void load_patch(MsdPlayer *p, MsdChan *c)               /* A 05AF */
{
    if (p->mode != MSD_ADLIB || (c->opl_ch & 0x80)) return;
    unsigned q = c->patch;
    cmd_vibrato_from(p, c, q);
    int m = OPOFF[c->opl_ch];
    oplc(p, c, (uint8_t)(0x80 + m), 0xFF); oplc(p, c, (uint8_t)(0x83 + m), 0xFF);
    oplc(p, c, (uint8_t)(0x40 + m), 0xFF); oplc(p, c, (uint8_t)(0x43 + m), 0xFF);
    oplc(p, c, (uint8_t)(0x20 + m), rd8(p, q + 6));
    oplc(p, c, (uint8_t)(0x23 + m), rd8(p, q + 7));
    oplc(p, c, (uint8_t)(0x60 + m), rd8(p, q + 10));
    oplc(p, c, (uint8_t)(0x63 + m), rd8(p, q + 11));
    oplc(p, c, (uint8_t)(0x80 + m), rd8(p, q + 12));
    oplc(p, c, (uint8_t)(0x83 + m), rd8(p, q + 13));
    uint8_t b14 = rd8(p, q + 14);
    oplc(p, c, (uint8_t)(0xE0 + m), (uint8_t)(((b14 << 2) | (b14 >> 6)) & 3));
    oplc(p, c, (uint8_t)(0xE3 + m), (uint8_t)(((b14 << 4) | (b14 >> 4)) & 3));
    c->conn = (uint8_t)(b14 & 0x0F);
    oplc(p, c, (uint8_t)(0xC0 + c->opl_ch), (uint8_t)(b14 & 0x0F));
    set_level(p, c);
}

static void write_freq(MsdPlayer *p, MsdChan *c)               /* A 077D */
{
    if (p->mode != MSD_ADLIB || (c->opl_ch & 0x80)) return;
    unsigned v = (unsigned)((c->fnum_block + c->vib_offset) & 0x1FFF);
    if (c->nflags & 0x40) v |= 0x2000;
    oplc(p, c, (uint8_t)(0xA0 + c->opl_ch), (uint8_t)(v & 0xFF));
    oplc(p, c, (uint8_t)(0xB0 + c->opl_ch), (uint8_t)(v >> 8));
}

static void load_perc(MsdPlayer *p)                            /* A 02C3 */
{
    if (p->mode != MSD_ADLIB) return;
    for (int ch = 6; ch <= 8; ch++) {
        const uint8_t *q = PERC[ch - 6];
        int m = OPOFF[ch];
        for (int reg = 0x20, i = 0; i < 8; reg += 0x20, i += 2) {
            opl(p, (uint8_t)(reg + m), q[i]);
            opl(p, (uint8_t)(reg + m + 3), q[i + 1]);
        }
        opl(p, (uint8_t)(0xE0 + m), (uint8_t)(((q[8] << 2) | (q[8] >> 6)) & 3));
        opl(p, (uint8_t)(0xE0 + m + 3), (uint8_t)(((q[8] << 4) | (q[8] >> 4)) & 3));
        opl(p, (uint8_t)(0xC0 + ch), q[8]);
    }
    opl(p, 0xA6, 0x20); opl(p, 0xB6, 0x04);        /* 0x120, block 1 */
    opl(p, 0xA7, 0x50); opl(p, 0xB7, 0x05);        /* 0x150, block 1 */
    opl(p, 0xA8, 0xC0); opl(p, 0xB8, 0x00);        /* 0x3C0, block 0 */
}

static void silence(MsdPlayer *p)                              /* A 03D2 */
{
    if (p->mode != MSD_ADLIB) { p->speaker_on = 0; return; }
    oplraw(p, 0xBD, 0x00);
    for (int r = 0xB0; r <= 0xB8; r++) oplraw(p, (uint8_t)r, 0x00);
}

/* ------------------------------------------------------------ note logic */
static void note_release(MsdPlayer *p, MsdChan *c)             /* A 0778 / S 0570 */
{
    if (c->cur_note >= 0) { logev(p, c, MSD_OFF, c->cur_note, 0, 0); c->cur_note = -1; }
    if (p->mode == MSD_ADLIB) {
        c->nflags &= (uint8_t)~0x40;
        write_freq(p, c);
    } else {
        unsigned e = c->patch + 6;
        uint8_t drop = (uint8_t)(rd8(p, e + 6) & 0xF0);
        c->env_level = (uint8_t)(c->env_level >= drop ? c->env_level - drop : 0);
        c->env_phase = 1;
        c->env_count = rd8(p, e + 5);
        c->env_step = rd8(p, e + 4);
    }
}

static void note_key_on(MsdPlayer *p, MsdChan *c)              /* A 072D / S 0500 */
{
    c->vib_delay = c->vib_start; c->vib_offset = 0; c->vib_frac = 0x80;
    c->nflags &= (uint8_t)~0x02;
    if (p->mode == MSD_ADLIB) {
        c->nflags |= 0x40;
        write_freq(p, c);
    } else {
        unsigned e = c->patch + 6;
        c->env_level = (uint8_t)(rd8(p, e + 7) << 4);
        c->env_phase = 0x12;
        c->env_count = rd8(p, e + 1);
        c->env_step = rd8(p, e + 0);
        c->env_wait = 1;
    }
}

static void set_pitch(MsdPlayer *p, MsdChan *c, int pitch)     /* A 074E / S 0548 */
{
    int i = pitch - 1; if (i < 0) i = 0; if (i > 11) i = 11;
    if (p->mode == MSD_ADLIB) {
        c->vib_note_scale = VIBSCALE_A[i];
        c->fnum_block = (uint16_t)((FNUM[i] + (int16_t)c->detune) | (c->block << 10));
    } else {
        c->vib_note_scale = VIBSCALE_S[i];
        c->divisor = (uint16_t)((PITDIV[i] + (int16_t)c->detune) >> (c->block & 7));
    }
}

static int midi_note(const MsdPlayer *p, const MsdChan *c, int pitch)
{
    int base;
    if (p->mode == MSD_ADLIB) base = 12 * (c->block + 1);
    else if (p->mode == MSD_JR) base = 12 * ((c->block > 1 ? c->block : 1) + 2);
    else base = 12 * (c->block + 3);
    return base + pitch - 1;
}

static int velocity(const MsdPlayer *p, const MsdChan *c)
{
    if (p->mode == MSD_ADLIB && c->instr >= 0) {
        unsigned q = p->opl_instr + 15u * (unsigned)c->instr;
        if (q + 15 <= p->blen) return att_to_vel(rd8(p, q + 9) & 0x3F);
    }
    return 127;
}
static int cc7(const MsdPlayer *p, const MsdChan *c) { return att_to_vel(c->lev[0] >> 1); }

/* ------------------------------------------------------------- commands */
static void flow(MsdPlayer *p, MsdChan *c, uint8_t op, int allow_f4);

static void select_patch(MsdPlayer *p, MsdChan *c, int n)      /* A 0598 / S 0457 */
{
    c->instr = n;
    if (p->mode == MSD_ADLIB) {
        c->patch = (uint16_t)(p->opl_instr + 15 * n);
        load_patch(p, c);
    } else {
        c->patch = (uint16_t)(p->instr_tab + 17 * n);
        cmd_vibrato_from(p, c, c->patch);
    }
}

static void volume_rel(MsdPlayer *p, MsdChan *c, uint8_t nib)  /* A 0699 / S 046E */
{
    int s = (int8_t)(uint8_t)(nib << 4) >> 2;                  /* 4 * signed nibble */
    int bl = c->lev[0];
    if (s >= 0) {
        bl = (bl - (s + 4)) & 0xFF;
        /* A 06B3 `test bl,0xC0`: the AdLib build clamps to 0 as soon as bit 6
         * or 7 is set, so any attenuation above 0x3F snaps to "loudest" on the
         * first C0-CF.  That is a driver bug (set_level uses attenuation>>1 as
         * a 6-bit level, so the range is meant to be 0..0x7F, and the other
         * three builds test 0x80) and it flattens zopn's AdLib fade-in.
         * tools/msd2mid.py models the intended 0..0x7F range instead; the
         * compat flag reproduces the tool so test_audio can diff the two. */
        if (p->mode == MSD_ADLIB) { if (!p->compat_msd2mid && (bl & 0xC0)) bl = 0; else if (bl & 0x80) bl = 0; }
        else { if (bl & 0x80) bl = 0; }
    } else {
        bl = (bl - s) & 0xFF;
        if (p->mode == MSD_ADLIB) { if (bl & 0xC0) bl = 0x3F; }
        else { if (bl & 0x80) bl = 0x7F; }
    }
    c->lev[0] = (uint8_t)bl;
    if (p->mode == MSD_ADLIB) set_level(p, c);
}

static void do_command(MsdPlayer *p, MsdChan *c, uint8_t b)
{
    if (b < 0xC0) {                                            /* 80-BF patch      */
        select_patch(p, c, b & 0x3F);
        logev(p, c, MSD_INSTR, c->instr, 0, 0);
        return;
    }
    if (b < 0xD0) { volume_rel(p, c, (uint8_t)(b & 0x0F)); logev(p, c, MSD_VOL, cc7(p, c), 0, 0); return; }
    if (b < 0xD8) { c->block = (uint8_t)(b & 7); return; }     /* D0-D7 octave     */
    if (b < 0xE0) { c->gate = dur(p, c, b & 7); return; }      /* D8-DF gate       */
    switch (b) {
    case 0xE0: p->tempo = fetch8(p, c); return;
    case 0xE1: { int8_t v = fetchi8(p, c); c->detune = (int8_t)(p->mode == MSD_JR ? (v >> 3) : v); return; }
    case 0xE2: { uint8_t d = fetch8(p, c); c->pos--; cmd_vibrato_from(p, c, c->pos); c->pos++; if (d) c->pos += 5; return; }
    case 0xE3: c->block--; return;
    case 0xE4: c->block++; return;
    case 0xE5: c->lev[0] = fetch8(p, c);
               if (p->mode == MSD_ADLIB) set_level(p, c);
               logev(p, c, MSD_VOL, cc7(p, c), 0, 0); return;
    case 0xE7: c->nflags |= 0x20; return;
    case 0xE6: case 0xE8: case 0xED: case 0xEE: case 0xEF: return;
    case 0xE9: case 0xEA:
        if (p->mode != MSD_ADLIB) fetch8(p, c);
        return;
    case 0xEB:
        if (p->mode == MSD_STD) fetch8(p, c);
        return;
    case 0xEC:
        if (p->mode != MSD_ADLIB) { if (fetch8(p, c)) fetch8(p, c); }
        return;
    default:
        flow(p, c, b, p->mode == MSD_ADLIB);
        return;
    }
}

static void flow(MsdPlayer *p, MsdChan *c, uint8_t op, int allow_f4)
{
    switch (op) {
    case 0xF0: c->durtab = (uint16_t)(p->durtabs + 8u * fetch8(p, c)); return;    /* S 071C */
    case 0xF1: { uint8_t i = (uint8_t)(fetch8(p, c) % 5); p->sync[i]++; return; }
    case 0xF2: { uint8_t i = (uint8_t)(fetch8(p, c) % 5); p->sync[i]--; return; }
    case 0xF3: { uint8_t i = (uint8_t)(fetch8(p, c) % 5); p->sync[i] = fetch8(p, c); return; }
    case 0xF4: {
        if (!allow_f4) return;
        uint8_t i = (uint8_t)(fetch8(p, c) % 5), v = fetch8(p, c); uint16_t a = fetch16(p, c);
        if (p->sync[i] == v) c->pos = a;
        return; }
    case 0xF5: { uint8_t v = fetch8(p, c); c->counter[v & 3] = (uint8_t)(v >> 2); return; }
    case 0xF6: { uint16_t a = fetch16(p, c); int i = a >> 14;
                 if (--c->counter[i] != 0) c->pos = (uint16_t)(a & 0x3FFF);
                 return; }
    case 0xF7: { uint16_t a = fetch16(p, c); int i = a >> 14;
                 if (--c->counter[i] == 0) c->pos = (uint16_t)(a & 0x3FFF);
                 return; }
    case 0xF8: { uint8_t v = fetch8(p, c); uint16_t a = fetch16(p, c);
                 if (c->counter[a >> 14] == v) c->pos = (uint16_t)(a & 0x3FFF);
                 return; }
    case 0xF9: { uint8_t i = fetch8(p, c); c->counter[i & 3]++; return; }
    case 0xFA: { uint8_t i = fetch8(p, c); c->counter[i & 3]--; return; }
    case 0xFB: c->pos = fetch16(p, c); return;
    case 0xFC: { uint16_t a = fetch16(p, c); c->ret = c->pos; c->ret_set = 1; c->saved_durtab = c->durtab; c->pos = a; return; }
    case 0xFD: { uint8_t v = fetch8(p, c); uint16_t a = fetch16(p, c);
                 if (c->counter[a >> 14] == v) { c->ret = c->pos; c->ret_set = 1; c->saved_durtab = c->durtab; c->pos = (uint16_t)(a & 0x3FFF); }
                 return; }
    case 0xFE: c->pos = c->ret; c->durtab = c->saved_durtab; return;
    case 0xFF:
        c->flags |= 1;
        if (++p->tracks_ended >= p->end_count) { p->stopped = 0x3F; silence(p); }
        return;
    default: return;
    }
}

/* ----------------------------------------------------------- note bytes */
static void do_note(MsdPlayer *p, MsdChan *c, uint8_t b)       /* A 06E8 / S 04B7 */
{
    c->nflags &= (uint8_t)~0x10;
    if (rd8(p, c->pos) == 0xE7) c->nflags |= 0x10;
    c->remaining = dur(p, c, b >> 4);
    if (c->remaining == 0) { p->warned = 1; c->remaining = 1; }
    int pitch = b & 0x0F;
    if (pitch == 0) { note_release(p, c); return; }
    if (pitch == 0x0F) return;
    int note = midi_note(p, c, pitch);
    set_pitch(p, c, pitch);
    int legato = (c->nflags & 0x20) != 0;
    c->nflags &= (uint8_t)~0x20;
    if (c->cur_note >= 0) logev(p, c, MSD_OFF, c->cur_note, 0, 0);
    logev(p, c, MSD_ON, note, velocity(p, c), legato);
    c->cur_note = note;
    if (!legato) note_key_on(p, c);
}

static void mel_tick(MsdPlayer *p, MsdChan *c)                 /* A 0356 / S 0356 */
{
    if (c->flags & 1) return;
    if (--c->remaining != 0) {
        if (c->gate >= c->remaining && !(c->nflags & 0x10)) note_release(p, c);
        return;
    }
    for (int guard = 0; guard < 10000; guard++) {
        uint8_t b = fetch8(p, c);
        if (b < 0x80) { do_note(p, c, b); return; }
        do_command(p, c, b);
        if (c->flags & 1) { note_release(p, c); return; }
    }
    c->flags |= 1;                                             /* runaway guard */
}

/* --------------------------------------------------------- rhythm track */
static void rhythm_tick(MsdPlayer *p, MsdChan *c)              /* A 08A9 */
{
    if (c->flags & 1) return;
    if (--c->remaining != 0) return;
    for (int guard = 0; guard < 10000; guard++) {
        uint8_t b = fetch8(p, c);
        if (b < 0x80) {                                        /* A 08F8: hit */
            uint8_t mask = (uint8_t)(b & 0x1F);
            if (mask) {
                opl(p, 0xBD, (uint8_t)((c->bd_bits & ~mask & 0x1F) | 0x20));
                c->bd_bits = (uint8_t)(c->bd_bits | mask | 0x20);
                opl(p, 0xBD, c->bd_bits);
                for (int i = 0; i < 5; i++)
                    if (mask & DRUM_BIT[i])
                        logev(p, c, MSD_DRUM, DRUM_NOTE[i], att_to_vel(c->lev[DRUM_LEV[i]]), 0);
            }
            c->remaining = (b & 0x20) ? fetch8(p, c) : c->default_dur;
            if (c->remaining == 0) { p->warned = 1; c->remaining = 1; }
            return;
        }
        if (b < 0xA0) {                                        /* A 0932 */
            c->bd_bits = (uint8_t)((c->bd_bits & ~(b & 0x1F)) | 0x20);
            opl(p, 0xBD, c->bd_bits);
        } else if (b < 0xC8) {                                 /* A 0945 */
            c->default_dur = dur(p, c, b & 7);
        } else if (b < 0xCD) {                                 /* A 0956 */
            int i = b - 0xC8;
            int v = (c->lev[i] & 0x3F) + fetchi8(p, c);
            c->lev[i] = (uint8_t)(v < 0 ? 0 : v > 0x3F ? 0x3F : v);
            rhythm_levels(p, c);
        } else if (b < 0xD2) {                                 /* A 0979 */
            c->lev[b - 0xCD] = (uint8_t)((fetch8(p, c) * 4) & 0xFF);
            rhythm_levels(p, c);
        } else {
            flow(p, c, (uint8_t)(0xF0 | (b & 0x0F)), 1);
            if (c->flags & 1) return;
        }
    }
    c->flags |= 1;
}

/* -------------------------------------------------- vibrato / envelope */
static void vibrato(MsdPlayer *p, MsdChan *c)                  /* S 05A0 */
{
    if (c->flags & 0x40) {
        if (--c->vib_delay == 0) {
            if (!(c->nflags & 0x02)) {
                unsigned shift = (p->mode == MSD_ADLIB) ? 0 : (c->block & 7);
                c->vib_step_up = (uint16_t)((c->vib_mul_up * c->vib_note_scale) >> shift);
                c->vib_step_down = (uint16_t)((c->vib_mul_down * c->vib_note_scale) >> shift);
                c->vib_half = (uint8_t)(((c->vib_ctl & 0x80) ? c->vib_len_down : c->vib_len_up) >> 1);
                c->vib_frac = 0x80;
                c->flags = (uint8_t)((c->flags & 0x7F) | (c->vib_ctl & 0x80));
                c->nflags |= 0x02;
            }
            c->vib_delay = (uint8_t)(c->vib_ctl & 0x1F);
            if (c->vib_delay == 0) c->vib_delay = 1;
            if (--c->vib_half == 0) {
                if (c->flags & 0x80) { c->vib_half = c->vib_len_up; c->flags &= (uint8_t)~0x80; }
                else                 { c->vib_half = c->vib_len_down; c->flags |= 0x80; }
                if (c->vib_half == 0) c->vib_half = 1;
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
            if (p->mode == MSD_ADLIB) write_freq(p, c);
        }
    }
    if (p->mode == MSD_ADLIB) return;
    /* S 0677: the PC-speaker envelope */
    if (--c->env_wait) return;
    unsigned e = c->patch + 6;
    c->env_wait = (uint8_t)(rd8(p, e + 6) & 0x0F);
    if (c->env_wait == 0) c->env_wait = 1;
    if ((c->env_phase & 0x0F) == 0) return;
    int level = c->env_level, step = c->env_step;
    if (step & 1) level = level >= (step & 0xFE) ? level - (step & 0xFE) : 0;
    else          level = level + step > 0xFF ? 0xFF : level + step;
    if (--c->env_count == 0) {
        if (--c->env_phase == 0x11) {
            c->env_count = rd8(p, e + 3);
            c->env_step = rd8(p, e + 2);
            int drop = rd8(p, e + 7) & 0xF0;
            level = level >= drop ? level - drop : 0;
        }
    }
    c->env_level = (uint8_t)level;
}

static void compute_volume(MsdPlayer *p, MsdChan *c)           /* S 06EC */
{
    int a = p->sync[4] + c->lev[0];
    if (a > 0x7F) a = 0x7F;
    int vol = (~a >> 3) & 0x0F;
    c->out_level = (uint8_t)((c->env_level >> 4) * vol);
}

uint16_t msd_speaker_divisor(MsdPlayer *p)                     /* S 0845 */
{
    if (p->mode == MSD_ADLIB || p->stopped || p->paused || p->music_off) return 0;
    compute_volume(p, &p->ch[0]); compute_volume(p, &p->ch[1]);
    MsdChan *v = &p->ch[0], *o = &p->ch[1];
    if (p->alternate) {
        if (!p->out_phase) { MsdChan *t = v; v = o; o = t; }
        if (v->out_level < 0x88) { MsdChan *t = v; v = o; o = t; }
    }
    p->last_divisor = (uint16_t)((v->divisor + v->vib_offset) << 3);
    p->speaker_on = (uint8_t)(v->out_level >= 0x88 ? 0xFF : 0);
    p->out_phase = (uint8_t)~p->out_phase;
    return p->speaker_on ? p->last_divisor : 0;
}

/* --------------------------------------------------------------- driver */
int msd_split(const uint8_t *res, size_t len, const uint8_t **blobB, size_t *lenB)
{
    if (len < 4) return -1;
    unsigned la = res[0] | res[1] << 8, lb = res[2] | res[3] << 8;
    if (4u + la + lb > len) return -1;
    *blobB = res + 4 + la; *lenB = lb;
    return 0;
}

static void chan_init(MsdPlayer *p, MsdChan *c, int idx, uint16_t start, uint8_t opl_ch)
{
    memset(c, 0, sizeof *c);
    c->idx = idx;
    c->pos = c->ret = start;
    c->remaining = 1;
    c->block = 3;
    c->gate = 1;
    c->lev[0] = 0x7F;
    c->durtab = 0xFFFF;
    c->saved_durtab = 0xFFFF;
    c->opl_ch = opl_ch;
    c->instr = -1;
    c->cur_note = -1;
    c->patch = 0;
}

int msd_start(MsdPlayer *p, const uint8_t *blob, size_t len, int mode)
{
    if (!blob || len < 0x23 || blob[0] != 4) return -1;
    void (*o)(void *, uint8_t, uint8_t) = p->opl; void *ou = p->opl_u;
    void (*e)(void *, int, int, int, int, int, int) = p->ev; void *eu = p->ev_u;
    int compat = p->compat_msd2mid;
    memset(p, 0, sizeof *p);
    p->opl = o; p->opl_u = ou; p->ev = e; p->ev_u = eu; p->compat_msd2mid = compat;
    p->b = blob; p->blen = len; p->mode = mode;
    for (int i = 0; i < 6; i++) p->adlib_tr[i] = rd16(p, 1 + 2 * i);
    for (int i = 0; i < 3; i++) p->jr_tr[i] = rd16(p, 0x0D + 2 * i);
    p->rhythm_tr = rd16(p, 0x13);
    p->opl_instr = rd16(p, 0x15);
    p->instr_tab = rd16(p, 0x17);
    p->durtabs   = rd16(p, 0x19);
    if (mode == MSD_ADLIB) {
        for (int i = 0; i < 6; i++) chan_init(p, &p->ch[i], i, p->adlib_tr[i], (uint8_t)i);
        chan_init(p, &p->ch[6], 6, p->rhythm_tr, 0x80);
        p->ch[6].lev[0] = 0;                       /* rhythm levels start at 0 */
        p->nch = 7;
    } else {
        /* MSCSTD.DRV only arms jr_track[0] and [1] (S 020B skips the third),
         * but all three are parsed here so the event stream matches
         * tools/msd2mid.py --std; the speaker output stage still uses only the
         * first two, and `end_count` keeps the driver's stop condition. */
        for (int i = 0; i < 3; i++) chan_init(p, &p->ch[i], i, p->jr_tr[i], (uint8_t)i);
        p->nch = 3;
    }
    p->end_count = (mode == MSD_ADLIB) ? 7 : (mode == MSD_STD) ? 2 : 3;
    p->tempo = 0x7F; p->tempo_acc = 0;
    p->fade_wait = 1; p->vib_phase = 0; p->tick = 0;
    p->stopped = 0;
    if (mode == MSD_ADLIB) {
        silence(p);
        oplraw(p, 0x01, 0x20);
        oplraw(p, 0xBD, 0x20);
        load_perc(p);
    }
    return 0;
}

void msd_score_tick(MsdPlayer *p)
{
    for (int i = 0; i < p->nch; i++) {
        if (p->mode == MSD_ADLIB && i == 6) rhythm_tick(p, &p->ch[i]);
        else mel_tick(p, &p->ch[i]);
    }
    p->tick++;
}

void msd_driver_tick(MsdPlayer *p)                             /* S 02C3 */
{
    if (p->stopped || p->paused) return;
    if (p->sync[3]) {                                          /* S 0328 fade */
        if (--p->fade_wait == 0) {
            p->fade_wait = p->sync[3];
            if (p->sync[4] + 4 > 0xFF) {
                p->sync[3] = 0; p->stopped = 0xFF; silence(p); p->sync[4] = 0xFF;
                return;
            }
            p->sync[4] = (uint8_t)(p->sync[4] + 4);
            for (int i = 0; i < p->nch; i++) {
                if (p->ch[i].opl_ch & 0x80) rhythm_levels(p, &p->ch[i]);
                else set_level(p, &p->ch[i]);
            }
        }
    }
    p->vib_phase = (uint8_t)~p->vib_phase;
    unsigned s = (unsigned)p->tempo_acc + p->tempo;
    p->skip_tick = (uint8_t)(s > 0xFF ? 0xFF : 0);
    p->tempo_acc = (uint8_t)s;
    if (!p->skip_tick) msd_score_tick(p);
    if (p->vib_phase) for (int i = 0; i < p->nch; i++) if (!(p->ch[i].opl_ch & 0x80)) vibrato(p, &p->ch[i]);
}

void msd_stop(MsdPlayer *p)  { silence(p); p->stopped = 0xFF; }
void msd_pause(MsdPlayer *p, int on)
{
    if (on) { silence(p); p->paused = 0xFF; }
    else {
        p->paused = 0;
        if (p->mode == MSD_ADLIB && !p->stopped) {
            load_perc(p);
            rhythm_levels(p, &p->ch[6]);
            for (int i = 0; i < 6; i++) load_patch(p, &p->ch[i]);
        }
    }
}
void msd_enable(MsdPlayer *p, int on)
{
    if (!on) { silence(p); p->music_off = 0xFF; return; }
    p->music_off = 0;
    if (p->mode == MSD_ADLIB && !p->stopped) {
        load_perc(p);
        rhythm_levels(p, &p->ch[6]);
        for (int i = 0; i < 6; i++) load_patch(p, &p->ch[i]);
    }
}
/* INT 60h AX=6 (A 01A0): bit0 -> OPL channel 4, bit1 -> channel 5 */
void msd_claim_opl(MsdPlayer *p, uint8_t mask)
{
    if (p->mode != MSD_ADLIB) return;
    if (mask) {
        p->ch[4].muted = (uint8_t)((mask & 1) ? 0xFF : 0);
        p->ch[5].muted = (uint8_t)((mask & 2) ? 0xFF : 0);
        return;
    }
    for (int i = 4; i <= 5; i++)
        if (p->ch[i].muted) { p->ch[i].muted = 0; p->ch[i].nflags &= (uint8_t)~0x40; write_freq(p, &p->ch[i]); }
    if (p->stopped || p->paused || p->music_off) { silence(p); return; }
    load_patch(p, &p->ch[4]); load_patch(p, &p->ch[5]);
}

int msd_all_ended(const MsdPlayer *p) { return p->stopped != 0; }

int msd_state_key(const MsdPlayer *p, int32_t *out, int max)
{
    int n = 0;
#define PUT(v) do { if (n < max) out[n] = (int32_t)(v); n++; } while (0)
    PUT(p->tempo);
    for (int i = 0; i < 5; i++) PUT(p->sync[i]);
    for (int i = 0; i < p->nch; i++) {
        const MsdChan *c = &p->ch[i];
        if (c->opl_ch & 0x80) {
            PUT(c->pos); PUT(c->remaining); PUT(c->default_dur);
            for (int k = 0; k < 5; k++) PUT(c->lev[k]);
            PUT(c->durtab == 0xFFFF ? -1 : c->durtab);
            for (int k = 0; k < 4; k++) PUT(c->counter[k]);
            PUT(c->ret_set ? c->ret : -1);
            PUT(c->saved_durtab == 0xFFFF ? -1 : c->saved_durtab);
            PUT(c->flags & 1);
        } else {
            PUT(c->pos); PUT(c->remaining); PUT(c->block); PUT(c->gate); PUT(c->lev[0]); PUT(c->detune);
            PUT(c->durtab == 0xFFFF ? -1 : c->durtab);
            PUT(c->instr);
            for (int k = 0; k < 4; k++) PUT(c->counter[k]);
            PUT(c->ret_set ? c->ret : -1); PUT(c->saved_durtab == 0xFFFF ? -1 : c->saved_durtab);
            PUT((c->nflags >> 4) & 1); PUT((c->nflags >> 5) & 1); PUT(c->flags & 1); PUT(c->cur_note);
        }
    }
#undef PUT
    return n;
}
