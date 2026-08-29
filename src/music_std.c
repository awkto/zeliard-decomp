/*
 * Zeliard (DOS, 1990) — MSCSTD.DRV, the PC-speaker music driver, hand-cleaned C.
 *
 * Source: zeliard/MSCSTD.DRV (2123 bytes, raw, loaded at (BASE+FF0):0100),
 * disasm/MSCSTD.asm (ndisasm, origin 0x100), Ghidra decompile cross-checked against
 * the listing.  Every function carries its original address.  Readable pseudo-C in
 * the style of src/kernel.c — not a buildable translation unit.
 *
 * This is the reference score interpreter: MSCJR.DRV and MSCADLIB.DRV are the same
 * program with a different output stage (see src/music_adlib.c for the OPL2 parts),
 * MSCMT.DRV interprets a different blob (docs/MUSIC.md §5).
 *
 * Conventions
 *   CS/ES          the driver segment BASE+0xFF0; offsets 0x00..0xFF alias the game's
 *                  state page BASE:FF00..FFFF (so FF0B here == kernel FF0B etc.)
 *   DS             while playing: the segment of the score (ARENA), see music_start
 *   score          byte array = blob B of the .msd resource (docs/MUSIC.md §2)
 *   driver tick    one call of tick() that does work = every 2nd INT 8 = 118.35 Hz
 *   score tick     a driver tick on which the tempo accumulator did NOT carry
 */

/* ------------------------------------------------------------------------- */
/* FF-page variables (offsets 0x00..0xFF of the driver segment)              */
/* ------------------------------------------------------------------------- */

uint8_t  FF0B_pause;          /* 0x0B  ≠0: tick() returns at once (INT 60h AX=3)      */
uint8_t  FF21_sync[3];        /* 0x21  score-writable counters (F1/F2/F3), game reads  */
uint8_t  FF24_fade_rate;      /* 0x24  ≠0: fade-out, ticks per step (game pokes it)    */
uint8_t  FF25_fade_level;     /* 0x25  attenuation added to every channel, +4 per step */
uint8_t  FF26_stopped;        /* 0x26  0 playing, FF stopped (AX=1 / fade), 3F all FF */
uint8_t  FF28_music_off;      /* 0x28  set by AX=2 CL=0 (F1 hotkey)                    */
uint8_t  FF76_volume;         /* 0x76  ~CL of AX=7 (unused by this back-end)           */

/* ------------------------------------------------------------------------- */
/* Driver-private data                                                       */
/* ------------------------------------------------------------------------- */

/* Semitone tables, index 0 = C.  PIT divisor before the octave shift and the <<3
 * of the output stage: 2280<<3 = 18240 -> 1193182/18240 = 65.4 Hz = C2 at octave 0. */
const uint16_t pit_divisor[12] /* 0x08B2 */ =
    { 2280, 2152, 2031, 1917, 1809, 1708, 1612, 1521, 1436, 1355, 1279, 1207 };
const uint8_t  vib_scale[12]   /* 0x08CA */ =          /* vibrato depth scale per note */
    { 0x80, 0x78, 0x72, 0x6B, 0x65, 0x5F, 0x5A, 0x55, 0x50, 0x4C, 0x47, 0x43 };

/* One per voice: ch[0] at 0x08D6, ch[1] at 0x0905 (0x2F bytes each). */
struct channel {
    uint16_t pos;             /* +00  track pointer (offset in the score)                */
    uint16_t ret;             /* +02  return address of FC/FD call                        */
    uint16_t saved_durtab;    /* +04  duration table saved by the call                    */
    uint8_t  counter[4];      /* +06  loop counters (F5..FA)                              */
    uint16_t durtab;          /* +0A  current 8-byte duration table (F0)                  */
    uint8_t  flags;           /* +0C  bit0 ended, bit6 vibrato on, bit7 vibrato direction */
    uint8_t  remaining;       /* +0D  score ticks left in the current note                */
    uint8_t  octave;          /* +0E  0..7, default 3                                     */
    uint8_t  nflags;          /* +0F  bit1 vib started, bit4 no-release (E7 follows),
                                      bit5 legato pending (E7 executed)                   */
    uint8_t  gate;            /* +10  release when remaining <= gate; default 1 (D8..DF)  */
    uint16_t divisor;         /* +11  PIT divisor of the note (before <<3)                */
    int8_t   detune;          /* +13  E1                                                  */
    int16_t  vib_offset;      /* +14  current vibrato offset (divisor units)              */
    uint8_t  vib_frac;        /* +16  8-bit fraction of vib_offset                        */
    uint8_t  vib_delay;       /* +17  ticks until the next vibrato step                   */
    uint16_t vib_step_up;     /* +18  }                                                   */
    uint16_t vib_step_down;   /* +1A  } depth per step, scaled by vib_scale[note]         */
    uint8_t  vib_note_scale;  /* +1C  vib_scale[note]                                     */
    uint8_t  vib_start;       /* +1D  patch +0: delay before vibrato                      */
    uint8_t  vib_mul_up;      /* +1E  patch +1                                            */
    uint8_t  vib_mul_down;    /* +1F  patch +2                                            */
    uint8_t  vib_len_up;      /* +20  patch +3                                            */
    uint8_t  vib_len_down;    /* +21  patch +4                                            */
    uint8_t  vib_ctl;         /* +22  patch +5: bit7 start downwards, bits0-4 step period */
    uint8_t  vib_half;        /* +23  steps left in the current half period               */
    uint8_t  attenuation;     /* +24  0 loud .. 7F silent (E5, C0-CF)                      */
    uint8_t  pad25, pad26;
    uint16_t patch;           /* +27  17-byte instrument record (80-BF)                   */
    uint8_t  env_wait;        /* +29  ticks until next envelope step                      */
    uint8_t  env_level;       /* +2A  0..FF, the high nibble is the audible part          */
    uint8_t  env_step;        /* +2B  bit0 = subtract, value & FE = amount                */
    uint8_t  env_count;       /* +2C  steps left in this phase                            */
    uint8_t  env_phase;       /* +2D  0x12 attack, 0x11 decay, 0x01 release, low nibble 0 = idle */
    uint8_t  out_level;       /* +2E  env_hi * volume nibble; speaker on if >= 0x88       */
};
struct channel ch0 /* 0x08D6 */, ch1 /* 0x0905 */;

uint8_t  tempo            /* 0x0934 */;    /* E0; default 0x7F                            */
uint8_t  tempo_acc        /* 0x0935 */;
uint8_t  fade_wait        /* 0x0936 */;    /* ticks until the next fade step              */
uint8_t  skip_tick        /* 0x0937 */;    /* FF on driver ticks where tempo_acc carried  */
uint8_t  vib_phase        /* 0x0938 */;    /* toggles every driver tick: vibrato runs at half rate */
uint8_t  out_phase        /* 0x0939 */;    /* toggles every output: which voice in alternation mode */
uint16_t score_off        /* 0x093A */;    /* DS:SI of music_start                        */
uint16_t score_seg        /* 0x093C */;
uint16_t patch_table      /* 0x093E */;    /* score + header[+17]                         */
uint16_t durtab_base      /* 0x0940 */;    /* score + header[+19]                         */
uint8_t  tracks_ended     /* 0x0942 */;    /* FF opcodes seen; 2 = all done               */
uint8_t  speaker_on       /* 0x0943 */;
uint16_t last_divisor     /* 0x0944 */;
uint8_t  div2             /* 0x0947 */;    /* INT 8 prescaler, reloaded with 2            */
uint8_t  enable_count     /* 0x0948 */;    /* AX=2 CL≠0 calls; odd -> alternation mode    */
uint8_t  alternate        /* 0x0949 */;    /* FF: swap voices every tick                  */
uint8_t  sfx_active       /* 0x094A */;    /* AX=4: sound driver owns the speaker         */

/* ------------------------------------------------------------------------- */
/* Entry points                                                              */
/* ------------------------------------------------------------------------- */

/* 0x0100: far entry called by STICK's INT 8 through [FF0C] every 236.7 Hz tick. */
void far tick_entry(void)             /* 0x02BE */
{
    cld;
    tick();                           /* 0x02C3 */
    retf;
}

/* 0x0103: INT 60h.  All registers saved, ES = CS, AX >= 8 ignored. */
void interrupt int60(void)            /* 0x0103 */
{
    push all; ES = CS; cld;
    if (AX < 8) fn[AX]();             /* 0x0118  call [cs:0127 + 2*AX] */
    pop all; iret;
}
const near_fn fn[8] /* 0x0127 */ = {
    music_start,      /* 0x020B  AX=0  DS:SI = score                    */
    music_stop,       /* 0x0295  AX=1                                    */
    music_enable,     /* 0x0137  AX=2  CL: 0 off, else on                */
    music_pause,      /* 0x015D  AX=3  CL: ≠0 pause, 0 resume            */
    sfx_mute,         /* 0x0172  AX=4  CL: ≠0 sound driver owns speaker  */
    sfx_claim,        /* 0x0181  AX=5  CL = SN76496 channel mask (JR)    */
    sfx_reset_opl,    /* 0x01B5  AX=6  CL=0: OPL all key-off (ADLIB)     */
    set_volume,       /* 0x01D1  AX=7  CL = volume (MT-32)               */
};

void music_enable(void)               /* 0x0137 */
{
    if (CL == 0) { silence(); FF28_music_off = 0xFF; return; }
    FF28_music_off = 0;
    enable_count++;                                   /* 0x014B */
    alternate = (enable_count & 1) ? 0xFF : 0;        /* every 2nd F1 toggles 2-voice mode */
}

void music_pause(void)                /* 0x015D */
{
    if (CL) { silence(); FF0B_pause = 0xFF; }
    else      FF0B_pause = 0;
}

void sfx_mute(void)                   /* 0x0172 */
{
    sfx_active = CL;
    AH = 0; sound_driver_hook();      /* call [cs:1102] with AX = 0x0004               */
    output();                         /* 0x0845 — re-arm the speaker if we own it      */
}

/* AX=5/6/7 are the JR/ADLIB/MT flavours' cross-calls; the STD build carries them
 * verbatim and they only talk to the sound driver hook ([cs:1102] = SND*.DRV). */
void sfx_claim(void)                  /* 0x0181 */
{
    if ((CL & 0x38) == 0) { AL = 0xFF; sound_driver_hook(); }
    for (dh = 0, dl = 3; dl; dh++, dl--) {           /* 0x0193 */
        bit = CL & 1; CL >>= 1;
        if (!bit) { AL = ((dh*2+1) << 4) | 0x8F; sound_driver_hook(); }  /* SN "attenuate ch dh to 15" */
    }
}
void sfx_reset_opl(void)              /* 0x01B5 */
{
    if (CL) return;
    AX = 0xBD00; sound_driver_hook();                /* rhythm register off             */
    for (AH = 0xB0, cx = 9; cx; AH++, cx--) sound_driver_hook();   /* B0..B8 = 0 key-off */
}
void set_volume(void)                 /* 0x01D1 */
{
    FF76_volume = ~CL;
    AL = ~CL & 0x3F; AH = 7; sound_driver_hook();
}

/* 0x01E3: clear both channel structs (0x8D6..0x947) and FF21..FF25, FF26 = FF. */
void reset_state(void)                /* 0x01E3 */
{
    memset(0x08D6, 0, 0x72);
    FF21_sync[0] = FF21_sync[1] = FF21_sync[2] = 0;
    FF24_fade_rate = FF25_fade_level = 0;
    FF26_stopped = 0xFF;
}

/* AX=0.  DS:SI = blob B.  Header (docs/MUSIC.md §2): the STD build takes the two
 * Tandy tracks at +0D/+0F, skips +11/+13/+15, then the STD/JR patch table at +17
 * and the duration tables at +19. */
void music_start(void)                /* 0x020B */
{
    reset_state();
    score_off = SI; score_seg = DS;
    base = SI;
    SI += 0x0D;
    chan_init(&ch0, base);            /* 0x026C: pos = ret = base + *SI++ */
    chan_init(&ch1, base);
    SI += 6;
    patch_table = base + lodsw();     /* +17 */
    durtab_base = base + lodsw();     /* +19 */
    FF26_stopped = 0;
    vib_phase = 0; out_phase = 0;
    div2 = 1;                         /* first INT 8 does work                  */
    fade_wait = 1;
    tempo = 0x7F; tempo_acc = 0;
    tracks_ended = 0;
    outb(0x43, 0xB6);                 /* PIT channel 2, mode 3 (square wave)    */
    silence();
}

void chan_init(struct channel *c, uint16_t base)   /* 0x026C */
{
    c->pos = c->ret = base + lodsw();
    c->remaining = 1;                 /* first tick fetches                     */
    c->octave = 3;
    c->gate = 1;
    c->attenuation = 0x7F;            /* silent until the track sets E5/Cx       */
    c->flags &= ~1;                   /* not ended                              */
}

void music_stop(void)                 /* 0x0295 */
{
    silence();
    FF26_stopped = 0xFF;
}

/* 0x029F: speaker gate off, both voices' output level = 0. */
void silence(void)                    /* 0x029F */
{
    ch0.out_level = ch1.out_level = 0;
    speaker_on = 0;
    outb(0x61, inb(0x61) & 0xFC);
}

/* ------------------------------------------------------------------------- */
/* Tick                                                                      */
/* ------------------------------------------------------------------------- */

void tick(void)                       /* 0x02C3 */
{
    if (FF26_stopped) return;
    if (FF0B_pause)   return;
    if (--div2) return;               /* every 2nd INT 8                        */
    div2 = 2;
    ES = CS; DS = score_seg;
    if (FF24_fade_rate) {             /* 0x02EA */
        fade_step();                  /* 0x0328 */
        if (FF26_stopped) return;     /* fade reached silence                   */
    }
    vib_phase = ~vib_phase;                                  /* 0x02FE */
    skip_tick = (tempo_acc + tempo > 0xFF) ? 0xFF : 0;      /* 0x0303: add, sbb al,al */
    tempo_acc += tempo;
    chan_tick(&ch0); vibrato_envelope(&ch0);                 /* 0x0313..0x0322 */
    chan_tick(&ch1); vibrato_envelope(&ch1);
    output();                                                /* 0x0845 */
}

/* Fade-out (the game starts it by writing FF24): every FF24 ticks add 4 to the
 * global attenuation; when it wraps, stop the music. */
void fade_step(void)                  /* 0x0328 */
{
    if (--fade_wait) return;
    fade_wait = FF24_fade_rate;
    if (FF25_fade_level + 4 > 0xFF) { /* 0x033C */
        FF24_fade_rate = 0; FF26_stopped = 0xFF; silence();
        FF25_fade_level = 0xFF;
        return;
    }
    FF25_fade_level += 4;
}

/* ------------------------------------------------------------------------- */
/* Score interpreter                                                         */
/* ------------------------------------------------------------------------- */

void chan_tick(struct channel *c)     /* 0x0356 */
{
    if (c->flags & 1) return;                       /* track ended            */
    if (FF24_fade_rate) compute_volume(c);          /* fade: refresh level    */
    if (skip_tick) return;                          /* not a score tick       */
    if (--c->remaining != 0) {                      /* 0x0372 */
        if (c->gate >= c->remaining && !(c->nflags & 0x10))
            release(c);                             /* 0x0570                 */
        return;
    }
    /* 0x038E: fetch until a note byte */
    SI = c->pos;
    for (;;) {
        b = lodsb();
        if (b < 0x80) { note(c, b); return; }       /* 0x04B7                 */
        /* 0x0399: command; 0x0391 pushed as the return address = loop        */
        if (!(b & 0x40))        select_patch(c, b & 0x3F);          /* 80-BF, 0x0457 */
        else if (b < 0xD0)      volume_rel(c, b & 0x0F);            /* C0-CF, 0x046E */
        else if (b < 0xD8)      c->octave = b & 7;                  /* D0-D7, 0x049B */
        else if (b < 0xE0)      c->gate = score[c->durtab + (b & 7)]; /* D8-DF, 0x04A2 */
        else                    cmd[b & 0x1F](c);                   /* E0-FF, table 0x03C6 */
        if (c->flags & 1) return;                   /* FF: see end_track      */
    }
}

/* Note byte: high nibble = duration index, low nibble = pitch (0 rest, F hold). */
void note(struct channel *c, uint8_t b)            /* 0x04B7 */
{
    c->pos = SI;
    c->nflags &= ~0x10;
    if (score[SI] == 0xE7) c->nflags |= 0x10;       /* slurred into the next note: no release */
    c->remaining = score[c->durtab + (b >> 4)];     /* 0x04CB */
    p = b & 0x0F;
    if (p == 0)    { release(c); return; }          /* rest                   */
    if (p == 0x0F) return;                          /* hold (tie), new duration only */
    set_pitch(c, p);                                /* 0x0548                 */
    if (c->nflags & 0x20) { c->nflags &= ~0x20; return; }   /* legato: no retrigger */
    key_on(c);                                      /* 0x0500                 */
}

/* 0x0548: divisor = (pit_divisor[p-1] + detune) >> octave. */
void set_pitch(struct channel *c, uint8_t p)
{
    c->vib_note_scale = vib_scale[p - 1];
    c->divisor = (pit_divisor[p - 1] + (int16_t)c->detune) >> c->octave;
}

/* 0x0500: restart vibrato and the envelope from the patch (bytes +6..+13). */
void key_on(struct channel *c)
{
    c->vib_delay  = c->vib_start;
    c->vib_offset = 0; c->vib_frac = 0x80;
    c->nflags &= ~0x02;                             /* vibrato not started    */
    e = c->patch + 6;
    c->env_level = score[e + 7] << 4;               /* +13 low nibble = initial level */
    c->env_phase = 0x12;
    c->env_count = score[e + 1];                    /* +7  phase-1 length     */
    c->env_step  = score[e + 0];                    /* +6  phase-1 step       */
    c->env_wait  = 1;
    compute_volume(c);
}

/* 0x0570: release phase: drop by (patch+12 high nibble), then step with +10/+11. */
void release(struct channel *c)
{
    e = c->patch + 6;
    drop = score[e + 6] & 0xF0;
    c->env_level = (c->env_level >= drop) ? c->env_level - drop : 0;
    c->env_phase = 1;
    c->env_count = score[e + 5];
    c->env_step  = score[e + 4];
    compute_volume(c);
}

/* E0-FF command table at 0x03C6 (index = byte & 0x1F). */
const near_fn cmd[32] /* 0x03C6 */ = {
    cmd_tempo,        /* E0 0x0406  tempo = u8                                  */
    cmd_detune,       /* E1 0x040C  detune = i8                                 */
    cmd_vibrato,      /* E2 0x0412  u8 delay (0 = off) [+5 bytes]               */
    cmd_octave_down,  /* E3 0x0434                                              */
    cmd_octave_up,    /* E4 0x0439                                              */
    cmd_volume,       /* E5 0x043E  attenuation = u8                            */
    cmd_nop,          /* E6 0x044E                                              */
    cmd_legato,       /* E7 0x0448  nflags |= 0x20                              */
    cmd_nop,          /* E8 0x044E                                              */
    cmd_skip1,        /* E9 0x0446  (JR: noise control)                         */
    cmd_skip1,        /* EA 0x0446  (JR: channel noise flags)                   */
    cmd_skip1,        /* EB 0x0446                                              */
    cmd_skip1or2,     /* EC 0x044F  u8, +u8 if the first ≠ 0                    */
    cmd_nop,          /* ED 0x044E                                              */
    cmd_nop,          /* EE 0x044E                                              */
    cmd_nop,          /* EF 0x044E                                              */
    cmd_durtab,       /* F0 0x071C  durtab = durtab_base + 8*u8                 */
    cmd_sync_inc,     /* F1 0x072F  FF21[u8]++                                  */
    cmd_sync_dec,     /* F2 0x0739  FF21[u8]--                                  */
    cmd_sync_set,     /* F3 0x0743  FF21[u8] = u8                               */
    cmd_nop,          /* F4 0x074D  (ADLIB: jump if FF21[i] == v)               */
    cmd_set_counter,  /* F5 0x074E  counter[u8 & 3] = u8 >> 2                   */
    cmd_loop,         /* F6 0x075F  --counter[a>>14]; if ≠0 jump a&3FFF         */
    cmd_loop_z,       /* F7 0x077D  --counter[a>>14]; if ==0 jump a&3FFF        */
    cmd_jump_eq,      /* F8 0x079B  u8 v, u16 a: if counter[a>>14]==v jump      */
    cmd_counter_inc,  /* F9 0x07BC  counter[u8]++                               */
    cmd_counter_dec,  /* FA 0x07C6  counter[u8]--                               */
    cmd_jump,         /* FB 0x07D0  jump u16 (the song loop)                    */
    cmd_call,         /* FC 0x07D9  call u16                                    */
    cmd_call_eq,      /* FD 0x07EE  u8 v, u16 a: call if counter[a>>14]==v      */
    cmd_return,       /* FE 0x081B                                              */
    cmd_end,          /* FF 0x0828                                              */
};

void cmd_tempo(struct channel *c)        { tempo = lodsb(); }                 /* 0x0406 */
void cmd_detune(struct channel *c)       { c->detune = lodsb(); }             /* 0x040C */
void cmd_vibrato(struct channel *c)                                           /* 0x0412 */
{
    /* also called with SI = patch record by select_patch */
    c->flags &= ~0x40;
    d = lodsb();
    if (d == 0) return;                             /* vibrato off            */
    c->flags |= 0x40;
    c->vib_start = d;
    memcpy(&c->vib_mul_up, SI, 5); SI += 5;         /* +1E..+22: movsw movsw movsb */
    c->nflags &= ~0x02;
}
void cmd_octave_down(struct channel *c)  { c->octave--; }                     /* 0x0434 */
void cmd_octave_up(struct channel *c)    { c->octave++; }                     /* 0x0439 */
void cmd_volume(struct channel *c)       { c->attenuation = lodsb(); compute_volume(c); } /* 0x043E */
void cmd_skip1(struct channel *c)        { lodsb(); }                         /* 0x0446 */
void cmd_legato(struct channel *c)       { c->nflags |= 0x20; }               /* 0x0448 */
void cmd_nop(struct channel *c)          { }                                  /* 0x044E */
void cmd_skip1or2(struct channel *c)     { if (lodsb()) lodsb(); }            /* 0x044F */

/* 80-BF: patch = patch_table + 17*n; the record's first 6 bytes are vibrato. */
void select_patch(struct channel *c, uint8_t n)    /* 0x0457 */
{
    c->patch = patch_table + n * 17;
    save = SI; SI = c->patch; cmd_vibrato(c); SI = save;
}

/* C0-CF: signed nibble s.  s >= 0: louder by 4(s+1); s < 0: quieter by 4|s|. */
void volume_rel(struct channel *c, uint8_t nib)    /* 0x046E */
{
    s = (int8_t)(nib << 4) >> 2;                    /* = 4*s, s in -8..7       */
    if (s >= 0) { a = c->attenuation - (s + 4); c->attenuation = (a < 0) ? 0 : a; }
    else        { a = c->attenuation - s;       c->attenuation = (a > 0x7F) ? 0x7F : a; }
    compute_volume(c);
}

void cmd_durtab(struct channel *c)       { c->durtab = durtab_base + lodsb() * 8; }      /* 0x071C */
void cmd_sync_inc(struct channel *c)     { FF21_sync[lodsb()]++; }                        /* 0x072F */
void cmd_sync_dec(struct channel *c)     { FF21_sync[lodsb()]--; }                        /* 0x0739 */
void cmd_sync_set(struct channel *c)     { w = lodsw(); FF21_sync[w & 0xFF] = w >> 8; }   /* 0x0743 */
void cmd_set_counter(struct channel *c)  { b = lodsb(); c->counter[b & 3] = b >> 2; }     /* 0x074E */
void cmd_loop(struct channel *c)                                                          /* 0x075F */
{
    a = lodsw(); i = a >> 14;
    if (--c->counter[i] != 0) SI = score_off + (a & 0x3FFF);
}
void cmd_loop_z(struct channel *c)                                                        /* 0x077D */
{
    a = lodsw(); i = a >> 14;
    if (--c->counter[i] == 0) SI = score_off + (a & 0x3FFF);
}
void cmd_jump_eq(struct channel *c)                                                       /* 0x079B */
{
    v = lodsb(); a = lodsw();
    if (c->counter[a >> 14] == v) SI = score_off + (a & 0x3FFF);
}
void cmd_counter_inc(struct channel *c)  { c->counter[lodsb()]++; }                       /* 0x07BC */
void cmd_counter_dec(struct channel *c)  { c->counter[lodsb()]--; }                       /* 0x07C6 */
void cmd_jump(struct channel *c)         { SI = score_off + lodsw(); }                    /* 0x07D0 */
void cmd_call(struct channel *c)                                                          /* 0x07D9 */
{
    a = lodsw(); c->ret = SI; c->saved_durtab = c->durtab; SI = score_off + a;
}
void cmd_call_eq(struct channel *c)                                                       /* 0x07EE */
{
    v = lodsb(); a = lodsw();
    if (c->counter[a >> 14] == v) { c->ret = SI; c->saved_durtab = c->durtab; SI = score_off + (a & 0x3FFF); }
}
void cmd_return(struct channel *c)       { SI = c->ret; c->durtab = c->saved_durtab; }    /* 0x081B */
void cmd_end(struct channel *c)                                                           /* 0x0828 */
{
    pop return address;                             /* leave the fetch loop   */
    c->flags |= 1;
    if (++tracks_ended == 2) { FF26_stopped = 0x3F; silence(); }
}

/* ------------------------------------------------------------------------- */
/* Vibrato + envelope (every 2nd driver tick, 0x0938)                        */
/* ------------------------------------------------------------------------- */

void vibrato_envelope(struct channel *c)           /* 0x05A0 */
{
    if (!vib_phase) return;
    if (c->flags & 0x40) {                          /* vibrato enabled        */
        if (--c->vib_delay == 0) {                  /* 0x05B3                 */
            if (!(c->nflags & 0x02)) {              /* first step: set up     */
                c->vib_step_up   = (c->vib_mul_up   * c->vib_note_scale) >> c->octave;
                c->vib_step_down = (c->vib_mul_down * c->vib_note_scale) >> c->octave;
                c->vib_half = ((c->vib_ctl & 0x80) ? c->vib_len_down : c->vib_len_up) >> 1;
                c->vib_frac = 0x80;
                c->flags = (c->flags & 0x7F) | (c->vib_ctl & 0x80);
                c->nflags |= 0x02;
            }
            c->vib_delay = c->vib_ctl & 0x1F;       /* 0x060F: step period    */
            if (--c->vib_half == 0) {               /* reverse direction      */
                if (c->flags & 0x80) { c->vib_half = c->vib_len_up;   c->flags &= 0x7F; }
                else                 { c->vib_half = c->vib_len_down; c->flags |= 0x80; }
            }
            /* 16.8 fixed point add/sub of the step to vib_offset (0x0642..0x0675) */
            if (!(c->flags & 0x80)) { c->vib_frac += lo(vib_step_up);   c->vib_offset += hi + carry; }
            else                    { c->vib_frac -= lo(vib_step_down); c->vib_offset -= hi + borrow; }
        }
    }
    envelope(c);                                    /* 0x0677 */
}

/* Two-phase attack/decay + release, one step every (patch+12 & 0F) ticks. */
void envelope(struct channel *c)                   /* 0x0677 */
{
    if (--c->env_wait) return;
    e = c->patch + 6;
    c->env_wait = score[e + 6] & 0x0F;
    if ((c->env_phase & 0x0F) == 0) return;         /* idle                   */
    level = c->env_level; step = c->env_step;
    if (step & 1) level = (level >= (step & 0xFE)) ? level - (step & 0xFE) : 0;
    else          level = (level + step > 0xFF) ? 0xFF : level + step;
    if (--c->env_count == 0) {                      /* 0x06B5                 */
        if (--c->env_phase == 0x11) {               /* attack done -> phase 2 */
            c->env_count = score[e + 3];
            c->env_step  = score[e + 2];
            drop = score[e + 7] & 0xF0;             /* +13 high nibble        */
            level = (level >= drop) ? level - drop : 0;
        }
    }
    if (level != c->env_level) { c->env_level = level; compute_volume(c); }
}

/* out_level = (env_level >> 4) * ((~(FF25 + attenuation) >> 3) & 0xF), i.e. the
 * envelope's high nibble times a 0..15 volume nibble (0x0706..0x0717 is a 4-step
 * shift-and-add multiply). */
void compute_volume(struct channel *c)             /* 0x06EC */
{
    a = FF25_fade_level + c->attenuation;
    if (a > 0x7F) a = 0x7F;
    vol = (~a >> 3) & 0x0F;
    c->out_level = (c->env_level >> 4) * vol;
}

/* ------------------------------------------------------------------------- */
/* Output stage: one voice on PIT channel 2                                  */
/* ------------------------------------------------------------------------- */

void output(void)                     /* 0x0845 */
{
    if (FF28_music_off) return;
    if (sfx_active) return;                         /* SNDSTD owns the speaker */
    v = &ch0; other = &ch1;
    if (alternate) {                                /* 0x085D: 2-voice mode   */
        if (!out_phase) swap(v, other);             /* even ticks: voice 1    */
        if (v->out_level < 0x88) swap(v, other);    /* inaudible: take the other */
    }
    divisor = (v->divisor + v->vib_offset) << 3;    /* 0x0878                 */
    last_divisor = divisor;
    outb(0x42, lo(divisor)); outb(0x42, hi(divisor));
    speaker_on = (v->out_level >= 0x88) ? 0xFF : 0; /* 0x0893                 */
    outb(0x61, (inb(0x61) & 0xFC) | (speaker_on & 3));
    out_phase = ~out_phase;
}
