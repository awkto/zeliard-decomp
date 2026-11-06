/* sn76496.h — the SN76496 sound chip of the Tandy 1000 / IBM PCjr (port C0h).
 *
 * Three square-wave tone generators with 10-bit dividers, one noise generator
 * (an LFSR clocked at N/512, N/1024, N/2048 or at tone 3's rate) and a 4-bit
 * attenuator per channel in 2 dB steps (15 = off).  The chip divides its clock
 * by 16, then each tone divider toggles its output every `period` of those
 * ticks: f = clock / (32 * period), i.e. 262 Hz for period 427 at 3.579545 MHz.
 *
 * Programming model (one byte at a time on port C0h):
 *   1 rrr dddd   latch register rrr (0/2/4 tone period, 1/3/5/7 attenuation,
 *                6 noise control) and write its low 4 bits
 *   0 - dddddd   data byte: the high 6 bits of the latched tone period, or the
 *                latched register's low bits again
 * Noise control: bit 2 = white (1) / periodic (0), bits 0-1 = shift rate
 * (3 = follow tone 2).  Writing it re-seeds the shift register.
 *
 * The synth is a plain behavioural model of the TI chip as MAME and DOSBox-X
 * implement it, run at the chip's own tick rate (clock/16, about 223.7 kHz) and
 * box-filtered down to the output rate. */
#ifndef ZEL_SN76496_H
#define ZEL_SN76496_H
#include <stdint.h>

#define SN_CLOCK_TANDY 3579545.0

typedef struct Sn76496 {
    uint16_t reg[8];                 /* 0 2 4: tone periods; 1 3 5 7: attenuation; 6: noise control */
    int      latch;                  /* register addressed by the next data byte */
    int      period[4], count[4], out[4];   /* the four generators (3 = noise) */
    uint32_t rng, fb_mask, tap1, tap2;      /* noise shift register and its feedback taps */
    int      ncr;                    /* NCR 8496 quirks: data bytes cannot touch regs 1/3/5/6/7 */
    double   vol[16];                /* attenuation nibble -> level, 2 dB per step */
    double   ticks_per_sample, tick_acc;
} Sn76496;

/* feedback presets for sn76496_set_lfsr */
enum { SN_LFSR_TI = 0, SN_LFSR_NCR = 1 };

void   sn76496_init(Sn76496 *s, double clock, double sample_rate);
void   sn76496_set_lfsr(Sn76496 *s, int preset);
void   sn76496_write(Sn76496 *s, uint8_t byte);
/* one chip tick (clock/16): returns the mixed output, 0..1 */
double sn76496_tick(Sn76496 *s);
/* one output sample: the average over the chip ticks that fall inside it */
double sn76496_sample(Sn76496 *s);

#endif
