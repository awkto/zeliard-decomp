/* msd.h — the .msd score interpreter: a C port of MSCADLIB.DRV / MSCSTD.DRV.
 *
 * docs/MUSIC.md is the spec, src/music_std.c the reference interpreter and
 * src/music_adlib.c the OPL2 output stage; every hex tag in msd.c is an
 * address in those two drivers.  One player runs one arrangement of one score:
 *   MSD_ADLIB   6 OPL2 melodic tracks + the rhythm track (blob B, +01/+13)
 *   MSD_JR      the 3 Tandy tracks (blob B, +0D) with the SN76496 octave map
 *   MSD_STD     the same 3 tracks with the PC-speaker octave map; only the
 *               first two are played, on one square-wave voice
 * The player emits OPL2 register writes through `opl` (MSD_ADLIB) and/or an
 * event log through `ev` (used by test_audio.c to diff against
 * tools/msd2mid.py --dump). */
#ifndef ZEL_MSD_H
#define ZEL_MSD_H
#include <stddef.h>
#include <stdint.h>

enum { MSD_ADLIB = 0, MSD_JR = 1, MSD_STD = 2 };
/* event kinds, matching tools/msd2mid.py's Log */
enum { MSD_ON = 0, MSD_OFF, MSD_DRUM, MSD_INSTR, MSD_VOL };

#define MSD_MAXCH 7

typedef struct MsdChan {
    uint16_t pos, ret, saved_durtab;
    uint8_t  counter[4];
    uint16_t durtab;                 /* 0xFFFF = not selected yet */
    uint8_t  opl_ch;                 /* 0..5, 0x80 = the rhythm track */
    uint8_t  flags;                  /* bit0 ended, bit6 vibrato on, bit7 direction */
    uint8_t  remaining;
    uint8_t  lev[5];                 /* lev[0] = attenuation; rhythm: BD HH SD TT CY */
    uint8_t  block;                  /* OPL block / Tandy octave, default 3 */
    uint8_t  nflags;                 /* bit1 vib started, bit4 no-release, bit5 legato, bit6 key-on */
    uint8_t  gate;
    uint8_t  conn;                   /* patch byte 14 & 0F */
    uint8_t  default_dur, bd_bits;   /* rhythm */
    uint16_t fnum_block;
    int8_t   detune;
    int16_t  vib_offset;
    uint8_t  vib_frac, vib_delay;
    uint16_t vib_step_up, vib_step_down;
    uint8_t  vib_note_scale, vib_start;
    uint8_t  vib_mul_up, vib_mul_down, vib_len_up, vib_len_down, vib_ctl, vib_half;
    uint16_t patch;                  /* offset of the patch record */
    int      instr;                  /* patch number, -1 = none */
    uint8_t  muted;                  /* INT 60h AX=6: the sfx driver owns this channel */
    int      cur_note;               /* MIDI note currently logged as sounding, -1 = none */
    int      idx;
    uint8_t  ret_set;                /* FC/FD seen: tools/msd2mid.py's ret is None until then */
    /* PC-speaker (MSCSTD) envelope */
    uint16_t divisor;
    uint8_t  env_wait, env_level, env_step, env_count, env_phase, out_level;
} MsdChan;

typedef struct MsdPlayer {
    const uint8_t *b;
    size_t   blen;
    int      mode, nch;
    uint16_t adlib_tr[6], jr_tr[3], rhythm_tr, opl_instr, instr_tab, durtabs;
    MsdChan  ch[MSD_MAXCH];
    uint8_t  sync[5];                /* FF21..FF25: sync[0..2], fade rate, fade level */
    uint8_t  tempo, tempo_acc, skip_tick, vib_phase, fade_wait;
    uint8_t  tracks_ended, stopped, paused, music_off;
    uint8_t  out_phase, alternate, speaker_on;
    uint16_t last_divisor;
    int      tick;                   /* score ticks since music_start */
    int      warned;
    int      end_count;              /* FF opcodes needed to stop the chip (STD 2, JR 3, ADLIB 7) */
    int      compat_msd2mid;         /* 1: reproduce tools/msd2mid.py's C0-CF clamp (see msd.c) */
    /* sinks */
    void   (*opl)(void *u, uint8_t reg, uint8_t val);
    void    *opl_u;
    void   (*ev)(void *u, int tick, int track, int kind, int a, int b, int c);
    void    *ev_u;
} MsdPlayer;

/* blob B of a .msd resource ({u16 lenA, u16 lenB} blobA blobB) */
int  msd_split(const uint8_t *res, size_t len, const uint8_t **blobB, size_t *lenB);
/* INT 60h AX=0: parse the header and arm every track.  0 on success. */
int  msd_start(MsdPlayer *p, const uint8_t *blobB, size_t len, int mode);
/* one 118.35 Hz driver tick (fade, tempo accumulator, score tick, vibrato) */
void msd_driver_tick(MsdPlayer *p);
/* one score tick — what tools/msd2mid.py's simulate() calls */
void msd_score_tick(MsdPlayer *p);
/* INT 60h AX=1 / AX=3 / AX=2 / AX=6 */
void msd_stop(MsdPlayer *p);
void msd_pause(MsdPlayer *p, int on);
void msd_enable(MsdPlayer *p, int on);
void msd_claim_opl(MsdPlayer *p, uint8_t mask);
int  msd_all_ended(const MsdPlayer *p);

/* the loop-detection key tools/msd2mid.py hashes; returns the number of
 * int32 words written (<= 8*MSD_MAXCH + 8). */
int  msd_state_key(const MsdPlayer *p, int32_t *out, int max);

/* score ticks per second for tempo byte T, and microseconds per quarter note */
double msd_score_hz(int T);
double msd_us_per_quarter(int T);

/* the PC-speaker output stage (MSD_STD): PIT divisor of the audible voice,
 * 0 when the speaker is off. */
uint16_t msd_speaker_divisor(MsdPlayer *p);

#endif
