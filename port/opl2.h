/* opl2.h — a compact OPL2 (YM3812) synthesiser.
 *
 * Written from scratch for the port (no third-party dependency); the register
 * semantics are the ones src/music_adlib.c and disasm/SND*.asm rely on:
 * 01 waveform-select enable, 20-35 AM/VIB/EG/KSR/MULT, 40-55 KSL/TL,
 * 60-75 AR/DR, 80-95 SL/RR, A0-A8/B0-B8 F-number/block/key-on, BD rhythm,
 * C0-C8 feedback/connection, E0-F5 waveform.
 *
 * The chip is modelled in floating point at the caller's sample rate: phase
 * increments and envelope rates are scaled by 49716/rate so the timings match
 * the real 3.579545 MHz / 72 sample clock. */
#ifndef ZEL_OPL2_H
#define ZEL_OPL2_H
#include <stdint.h>

#define OPL2_NATIVE_HZ 49716.0

typedef struct Opl2Op {
    /* register image */
    uint8_t am, vib, egt, ksr, mult;   /* 20 */
    uint8_t ksl, tl;                   /* 40 */
    uint8_t ar, dr;                    /* 60 */
    uint8_t sl, rr;                    /* 80 */
    uint8_t wave;                      /* E0 */
    /* state */
    int      phase_is_car;
    double   phase;                    /* 0..1 cycles */
    double   env;                      /* attenuation, 0 (loud) .. 511 (silent) */
    int      state;                    /* 0 off, 1 attack, 2 decay, 3 sustain, 4 release */
    double   out;                      /* last output, -1..1 */
} Opl2Op;

typedef struct Opl2Chan {
    Opl2Op   op[2];
    uint16_t fnum;
    uint8_t  block, keyon, fb, cnt;
    double   fb1, fb2;                 /* feedback history */
} Opl2Chan;

typedef struct Opl2 {
    Opl2Chan ch[9];
    uint8_t  reg[256];
    uint8_t  wave_sel;                 /* reg 01 bit 5 */
    uint8_t  rhythm;                   /* reg BD */
    double   rate_scale;               /* 49716 / sample_rate */
    double   lfo_am, lfo_vib;          /* 3.7 Hz / 6.1 Hz phases, in cycles */
    uint32_t noise;                    /* LFSR */
} Opl2;

void  opl2_reset(Opl2 *o, double sample_rate);
void  opl2_write(Opl2 *o, uint8_t reg, uint8_t val);
/* one mono sample in roughly -1..1 (9 voices summed, not clipped) */
double opl2_sample(Opl2 *o);

#endif
