/* opl2.c — the OPL2 core.  See opl2.h.
 *
 * Model
 *   phase       cycles (0..1), advanced by fnum * 2^block * mult / 2^20 per
 *               native (49716 Hz) sample, scaled to the output rate.
 *   env         attenuation in 0.1875 dB units, 0 = loud, 511 = silent, so the
 *               linear gain is 2^(-env/32) (TL adds 4 units per step = 0.75 dB).
 *   FM          the modulator's normalised output is added to the carrier's
 *               phase, full scale = 4 cycles (the 13-bit modulator output is
 *               added straight to the 10-bit sine index on the real chip).
 *   feedback    the average of the modulator's last two outputs, pi/16 .. 4pi.
 *   envelope    rate r = min(63, 4*R + rof); a rate-r generator moves
 *               (4 + (r&3))/4 * 2^((r>>2)-13) attenuation units per native
 *               sample (r = 48 -> 512 units in 1024 samples = 20.6 ms, which
 *               is the datasheet decay time), and the attack is the usual
 *               exponential env -= (env+1) * step / 8.
 */
#include "opl2.h"
#include <math.h>
#include <string.h>

#define ENV_MAX 511.0
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

enum { ST_OFF = 0, ST_ATTACK, ST_DECAY, ST_SUSTAIN, ST_RELEASE };

/* MULT nibble -> 2*multiplier (0 means 1/2) */
static const uint8_t MULT2[16] = { 1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30 };
/* sustain level, 0.1875 dB units: 3 dB per step, 15 = 93 dB */
static const uint16_t SL_UNITS[16] = { 0, 16, 32, 48, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208, 224, 496 };
/* KSL attenuation at block 7 in 0.75 dB units, indexed by the f-number's top
 * 4 bits: 56 - 8*log2(15/j) (6 dB per octave). */
static const uint8_t KSL_TAB[16] = { 0, 24, 32, 37, 40, 43, 45, 47, 48, 50, 51, 52, 53, 54, 55, 56 };
/* register bits 7-6 of 40h: 0 dB, 3 dB, 1.5 dB, 6 dB per octave */
static const double KSL_MUL[4] = { 0.0, 0.5, 0.25, 1.0 };
/* feedback modulation depth in cycles: pi/16, pi/8, pi/4, pi/2, pi, 2pi, 4pi */
static const double FB_CYCLES[8] = { 0.0, 1.0/32, 1.0/16, 1.0/8, 1.0/4, 1.0/2, 1.0, 2.0 };

/* operator register offset -> channel and operator index */
static const uint8_t OP_OFF[9] = { 0x00, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x10, 0x11, 0x12 };

static int op_lookup(uint8_t off, int *chan, int *op)
{
    for (int c = 0; c < 9; c++) {
        if (off == OP_OFF[c]) { *chan = c; *op = 0; return 1; }
        if (off == (uint8_t)(OP_OFF[c] + 3)) { *chan = c; *op = 1; return 1; }
    }
    return 0;
}

void opl2_reset(Opl2 *o, double sample_rate)
{
    memset(o, 0, sizeof *o);
    o->rate_scale = OPL2_NATIVE_HZ / (sample_rate > 0 ? sample_rate : OPL2_NATIVE_HZ);
    o->noise = 1;
    for (int c = 0; c < 9; c++)
        for (int i = 0; i < 2; i++) { o->ch[c].op[i].env = ENV_MAX; o->ch[c].op[i].state = ST_OFF; }
}

/* the four OPL2 waveforms over one cycle */
static double wave_of(int w, double ph)
{
    double s = sin(2.0 * M_PI * ph);
    switch (w & 3) {
    case 0: return s;
    case 1: return s > 0 ? s : 0.0;
    case 2: return fabs(s);
    default: {
        double f = ph - floor(ph);
        double q = f - floor(f * 2.0) * 0.5;      /* position inside the half cycle */
        return q < 0.25 ? fabs(s) : 0.0;
    }
    }
}

static void key_on(Opl2Op *p)  { p->state = ST_ATTACK; p->phase = 0.0; }
static void key_off(Opl2Op *p) { if (p->state != ST_OFF) p->state = ST_RELEASE; }

void opl2_write(Opl2 *o, uint8_t reg, uint8_t val)
{
    o->reg[reg] = val;
    if (reg == 0x01) { o->wave_sel = (uint8_t)(val & 0x20); return; }
    if (reg == 0xBD) {
        uint8_t old = o->rhythm;
        o->rhythm = val;
        if (val & 0x20) {
            static const uint8_t bits[5] = { 0x10, 0x08, 0x04, 0x02, 0x01 };  /* BD SD TT CY HH */
            static const uint8_t chn[5]  = { 6, 7, 8, 8, 7 };
            static const uint8_t opn[5]  = { 2, 1, 0, 1, 0 };  /* 2 = both */
            for (int i = 0; i < 5; i++) {
                int on = (val & bits[i]) != 0, was = (old & 0x20) && (old & bits[i]);
                if (on == was) continue;
                Opl2Chan *c = &o->ch[chn[i]];
                if (opn[i] == 2) { if (on) { key_on(&c->op[0]); key_on(&c->op[1]); } else { key_off(&c->op[0]); key_off(&c->op[1]); } }
                else             { if (on) key_on(&c->op[opn[i]]); else key_off(&c->op[opn[i]]); }
            }
        }
        return;
    }
    int ch, op;
    uint8_t hi = (uint8_t)(reg & 0xF0), lo = (uint8_t)(reg & 0x1F);
    if (hi >= 0x20 && hi <= 0x30 && op_lookup(lo, &ch, &op)) {
        Opl2Op *p = &o->ch[ch].op[op];
        p->am = (uint8_t)((val >> 7) & 1); p->vib = (uint8_t)((val >> 6) & 1);
        p->egt = (uint8_t)((val >> 5) & 1); p->ksr = (uint8_t)((val >> 4) & 1);
        p->mult = (uint8_t)(val & 0x0F);
        return;
    }
    if (hi >= 0x40 && hi <= 0x50 && op_lookup(lo, &ch, &op)) {
        Opl2Op *p = &o->ch[ch].op[op];
        p->ksl = (uint8_t)(val >> 6); p->tl = (uint8_t)(val & 0x3F);
        return;
    }
    if (hi >= 0x60 && hi <= 0x70 && op_lookup(lo, &ch, &op)) {
        Opl2Op *p = &o->ch[ch].op[op];
        p->ar = (uint8_t)(val >> 4); p->dr = (uint8_t)(val & 0x0F);
        return;
    }
    if (hi >= 0x80 && hi <= 0x90 && op_lookup(lo, &ch, &op)) {
        Opl2Op *p = &o->ch[ch].op[op];
        p->sl = (uint8_t)(val >> 4); p->rr = (uint8_t)(val & 0x0F);
        return;
    }
    if (hi >= 0xE0 && hi <= 0xF0 && op_lookup(lo, &ch, &op)) {
        o->ch[ch].op[op].wave = (uint8_t)(val & 3);
        return;
    }
    if (reg >= 0xA0 && reg <= 0xA8) {
        Opl2Chan *c = &o->ch[reg - 0xA0];
        c->fnum = (uint16_t)((c->fnum & 0x300) | val);
        return;
    }
    if (reg >= 0xB0 && reg <= 0xB8) {
        Opl2Chan *c = &o->ch[reg - 0xB0];
        c->fnum = (uint16_t)((c->fnum & 0xFF) | ((val & 3) << 8));
        c->block = (uint8_t)((val >> 2) & 7);
        uint8_t on = (uint8_t)((val >> 5) & 1);
        if (on && !c->keyon) { key_on(&c->op[0]); key_on(&c->op[1]); }
        else if (!on && c->keyon) { key_off(&c->op[0]); key_off(&c->op[1]); }
        c->keyon = on;
        return;
    }
    if (reg >= 0xC0 && reg <= 0xC8) {
        Opl2Chan *c = &o->ch[reg - 0xC0];
        c->fb = (uint8_t)((val >> 1) & 7); c->cnt = (uint8_t)(val & 1);
        return;
    }
}

/* attenuation units per native sample for envelope rate register value R */
static double env_rate(const Opl2Chan *c, const Opl2Op *p, int R)
{
    if (R == 0) return 0.0;
    int kcode = (c->block << 1) | ((c->fnum >> 9) & 1);
    int rof = p->ksr ? kcode : (kcode >> 2);
    int r = 4 * R + rof;
    if (r > 63) r = 63;
    return (4.0 + (r & 3)) / 4.0 * pow(2.0, (r >> 2) - 13);
}

static double ksl_att(const Opl2Chan *c, const Opl2Op *p)
{
    if (!p->ksl) return 0.0;
    int v = (int)KSL_TAB[(c->fnum >> 6) & 0x0F] + 8 * ((int)c->block - 7);
    if (v < 0) v = 0;
    return v * KSL_MUL[p->ksl] * 4.0;      /* 0.75 dB units -> 0.1875 dB units */
}

static void env_step(Opl2 *o, Opl2Chan *c, Opl2Op *p)
{
    double k = o->rate_scale;
    switch (p->state) {
    case ST_ATTACK: {
        double R = env_rate(c, p, p->ar);
        if (p->ar >= 15 || R * k >= 8.0) { p->env = 0.0; p->state = ST_DECAY; break; }
        p->env -= (p->env + 1.0) * R * k / 8.0;
        if (p->env <= 0.0) { p->env = 0.0; p->state = ST_DECAY; }
        break; }
    case ST_DECAY: {
        double sl = SL_UNITS[p->sl];
        p->env += env_rate(c, p, p->dr) * k;
        if (p->env >= sl) { p->env = sl; p->state = p->egt ? ST_SUSTAIN : ST_RELEASE; }
        break; }
    case ST_SUSTAIN:
        break;
    case ST_RELEASE:
        p->env += env_rate(c, p, p->rr) * k;
        if (p->env >= ENV_MAX) { p->env = ENV_MAX; p->state = ST_OFF; }
        break;
    default:
        p->env = ENV_MAX;
        break;
    }
}

/* one operator: advance the phase, return the (already attenuated) output */
static double op_run(Opl2 *o, Opl2Chan *c, Opl2Op *p, double mod_cycles, double am_depth, double vib_cents)
{
    double inc = (double)c->fnum * (double)(1u << c->block) * MULT2[p->mult] * 0.5 / 1048576.0;
    if (p->vib) inc *= vib_cents;
    p->phase += inc * o->rate_scale;
    if (p->phase >= 1024.0 || p->phase <= -1024.0) p->phase = fmod(p->phase, 1.0);
    env_step(o, c, p);
    double att = p->env + p->tl * 4.0 + ksl_att(c, p);
    if (p->am) att += am_depth;
    if (att >= ENV_MAX) { p->out = 0.0; return 0.0; }
    double gain = pow(2.0, -att / 32.0);
    double v = wave_of(o->wave_sel ? p->wave : 0, p->phase + mod_cycles) * gain;
    p->out = v;
    return v;
}

static double noise_bit(Opl2 *o)
{
    /* 23-bit LFSR, as in the OPL rhythm noise generator */
    uint32_t n = o->noise;
    uint32_t bit = ((n >> 0) ^ (n >> 14) ^ (n >> 15) ^ (n >> 22)) & 1;
    o->noise = (n >> 1) | (bit << 22);
    return (n & 1) ? 1.0 : -1.0;
}

double opl2_sample(Opl2 *o)
{
    /* the two global LFOs: tremolo 3.7 Hz (1 dB or 4.8 dB), vibrato 6.1 Hz */
    o->lfo_am += 3.7 / OPL2_NATIVE_HZ * o->rate_scale;
    o->lfo_vib += 6.1 / OPL2_NATIVE_HZ * o->rate_scale;
    if (o->lfo_am >= 1.0) o->lfo_am -= 1.0;
    if (o->lfo_vib >= 1.0) o->lfo_vib -= 1.0;
    double am_max = (o->rhythm & 0x80) ? 25.6 : 5.3;      /* 4.8 dB / 1.0 dB in 0.1875 dB units */
    double am_depth = (0.5 - 0.5 * cos(2 * M_PI * o->lfo_am)) * am_max;
    double vib_depth = (o->rhythm & 0x40) ? 0.0140 : 0.0070;   /* +-14 / 7 cents */
    double vib = 1.0 + vib_depth * sin(2 * M_PI * o->lfo_vib);

    int rhythm = (o->rhythm & 0x20) != 0;
    int melodic = rhythm ? 6 : 9;
    double out = 0.0;
    for (int i = 0; i < melodic; i++) {
        Opl2Chan *c = &o->ch[i];
        double fbamt = FB_CYCLES[c->fb] * ((c->fb1 + c->fb2) * 0.5);
        double m = op_run(o, c, &c->op[0], fbamt, am_depth, vib);
        c->fb2 = c->fb1; c->fb1 = m;
        if (c->cnt) {                                    /* additive */
            out += m + op_run(o, c, &c->op[1], 0.0, am_depth, vib);
        } else {
            out += op_run(o, c, &c->op[1], m * 4.0, am_depth, vib);
        }
    }
    if (rhythm) {
        double nz = noise_bit(o);
        Opl2Chan *bd = &o->ch[6], *sh = &o->ch[7], *tc = &o->ch[8];
        double fbamt = FB_CYCLES[bd->fb] * ((bd->fb1 + bd->fb2) * 0.5);
        double m = op_run(o, bd, &bd->op[0], fbamt, am_depth, vib);
        bd->fb2 = bd->fb1; bd->fb1 = m;
        out += bd->cnt ? (m + op_run(o, bd, &bd->op[1], 0.0, am_depth, vib))
                       : op_run(o, bd, &bd->op[1], m * 4.0, am_depth, vib);
        /* HH and CY take their phase from the chip's noise/phase mix; SD is
         * the hi-hat phase gated by the noise bit.  Approximated here as a
         * noise-driven phase offset, which gives the right character. */
        double hh = op_run(o, sh, &sh->op[0], nz * 0.5, am_depth, vib) * 2.0;
        double sd = op_run(o, sh, &sh->op[1], nz * 0.5, am_depth, vib) * 2.0;
        double tt = op_run(o, tc, &tc->op[0], 0.0, am_depth, vib) * 2.0;
        double cy = op_run(o, tc, &tc->op[1], nz * 0.25, am_depth, vib) * 2.0;
        out += hh + sd + tt + cy;
    }
    return out * 0.11;                                   /* 9 voices -> roughly -1..1 */
}
