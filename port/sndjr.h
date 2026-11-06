/* sndjr.h — SNDJR.DRV, the Tandy / PCjr sound-effect driver, in C.
 *
 * The same program as MSCJR.DRV cut down to two voices (chip channels 2 and 1)
 * that talk to the SN76496 directly and take the chip channels they use away
 * from the music driver with INT 60h AX=5.  Ticked on every INT 8 (236.7 Hz).
 * `K xxxx` tags in sndjr.c are addresses in disasm/SNDJR.asm, which the driver
 * file is loaded at (segment BASE+FF0, offset 1100). */
#ifndef ZEL_SNDJR_H
#define ZEL_SNDJR_H
#include <stddef.h>
#include <stdint.h>

typedef struct SndJrChan {
    uint16_t pos, durtab, patch, base, step_up, step_down;
    uint8_t  chip, flags, remaining, att, octave, nflags, gate, mask;
    int8_t   detune;
    int16_t  vib_off;
    uint8_t  vib_frac, vib_delay, scale, vib_start;
    uint8_t  mul_up, mul_down, len_up, len_down, ctl, half;
    uint8_t  wait, level, step, count, pflags, phase;
} SndJrChan;

typedef struct SndJr {
    uint8_t *drv;
    size_t   len;
    unsigned neff;                    /* effects in the table at 170E */
    SndJrChan ch[2];
    uint8_t  prio, tempo, acc, skip, idle, ended, mask, off;
    uint16_t durtabs;
    void   (*sn)(void *u, uint8_t byte);        /* port C0h */
    void    *sn_u;
    void   (*claim)(void *u, uint8_t cl);       /* INT 60h AX=5 */
    void    *claim_u;
} SndJr;

/* read SNDJR.DRV from `dir`; the sinks are kept */
int  sndjr_load(SndJr *s, const char *dir);
void sndjr_free(SndJr *s);
/* one INT 8 tick: takes the FF75 request (cleared), then steps both voices */
void sndjr_tick(SndJr *s, int *request);
/* FF27 */
void sndjr_enable(SndJr *s, int on);
int  sndjr_entry(const SndJr *s, int id, int *prio, unsigned *t0, unsigned *t1, unsigned *durtab);

#endif
