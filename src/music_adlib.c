/*
 * Zeliard (DOS, 1990) — MSCADLIB.DRV, the OPL2 (AdLib) music driver: the parts that
 * differ from MSCSTD.DRV (src/music_std.c is the reference interpreter).
 *
 * Source: zeliard/MSCADLIB.DRV (3015 bytes, raw, loaded at (BASE+FF0):0100),
 * disasm/MSCADLIB.asm, Ghidra cross-check.  Original addresses on every function.
 * Readable pseudo-C, not buildable.
 *
 * Same program as MSCSTD: INT 60h dispatcher 0103/table 0127, FF-page variables,
 * div-by-2 tick prescaler (0CC6), tempo accumulator (0CB7/0CB8/0CBA = skip flag),
 * vibrato phase 0CBB, fade (0463), the E0-FF command table (0506, same order as
 * STD's 03C6 but E9-EC take no argument and F4 is implemented) and the note byte
 * handling (06E8).  Differences: 6 melodic channels + a rhythm channel, OPL patch
 * records, f-number/block pitch, per-channel volume through the carrier level.
 */

/* ------------------------------------------------------------------------- */
/* Data                                                                      */
/* ------------------------------------------------------------------------- */

/* Per-channel struct, 0x2C bytes, ch[i] at 0x0B99 + 0x2C*i, i = 0..5; the rhythm
 * channel at 0x0CA1 shares the layout (flags bit7 = rhythm). */
struct opl_channel {
    uint16_t pos;             /* +00 */
    uint16_t ret;             /* +02 */
    uint16_t saved_durtab;    /* +04 */
    uint8_t  counter[4];      /* +06 */
    uint16_t durtab;          /* +0A */
    uint8_t  opl_ch;          /* +0C  OPL channel number 0-5 (rhythm: 0x80)             */
    uint8_t  flags;           /* +0D  bit0 ended, bit6 vibrato on, bit7 vib direction    */
    uint8_t  remaining;       /* +0E */
    uint8_t  attenuation;     /* +0F  0..7F, >>1 is added to the carrier level; default 7F
                                      (rhythm: +0F..+13 = BD HH SD TT CY levels)         */
    uint8_t  block;           /* +10  OPL block (octave), default 3                     */
    uint8_t  nflags;          /* +11  bit1 vib started, bit4 no-release, bit5 legato,
                                      bit6 key-on                                        */
    uint8_t  gate;            /* +12  default 1                                          */
    uint8_t  conn;            /* +13  patch byte 14 & 0F: bit0 = additive connection     */
    uint8_t  default_dur;     /* +14  rhythm: default hit duration (A0-A7)               */
    uint8_t  bd_bits;         /* +15  rhythm: current BD register value                  */
    uint16_t fnum_block;      /* +16  f-number | block<<10 (A0/B0 without key-on)        */
    int8_t   detune;          /* +18  E1                                                 */
    int16_t  vib_offset;      /* +19 */
    uint8_t  vib_frac;        /* +1B */
    uint8_t  vib_delay;       /* +1C */
    uint16_t vib_step_up;     /* +1D */
    uint16_t vib_step_down;   /* +1F */
    uint8_t  vib_note_scale;  /* +21  vib_scale[pitch] (0x0B69)                          */
    uint8_t  vib_start;       /* +22  patch +0 .. +5 as in STD (+23..+27)                */
    uint8_t  vib_params[5];   /* +23 */
    uint8_t  vib_half;        /* +28 */
    uint16_t patch;           /* +29  15-byte OPL patch record                           */
    uint8_t  muted;           /* +2B  set by INT 60h AX=6 CL bits: sound driver owns it  */
};
struct opl_channel ch[6] /* 0x0B99 */, rhythm /* 0x0CA1 */;

/* Semitone -> OPL2 f-number (block 4: 0x156 = 259 Hz ≈ C4). */
const uint16_t fnum[12]     /* 0x0B51 */ =
    { 0x156, 0x16B, 0x181, 0x197, 0x1B0, 0x1C9, 0x1E4, 0x201, 0x220, 0x240, 0x263, 0x287 };
const uint8_t  vib_scale[12] /* 0x0B69 */ =
    { 0x13, 0x14, 0x15, 0x16, 0x18, 0x19, 0x1B, 0x1C, 0x1E, 0x20, 0x22, 0x24 };
/* Modulator operator register offset per OPL channel; carrier = +3. */
const uint8_t  op_offset[9] /* 0x0B75 */ = { 0x00, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x10, 0x11, 0x12 };
/* Driver-resident percussion patches for OPL channels 6, 7, 8 (9 bytes each:
 * 20/23, 40/43, 60/63, 80/83, then E0/E3 waveform + C0). */
const uint8_t  perc_patch[3][9] /* 0x0B7E */ = {
    { 0x00, 0x00, 0x0B, 0x40, 0xA8, 0xD6, 0xBC, 0xBF, 0x00 },   /* ch6: bass drum (both ops) */
    { 0x01, 0x0C, 0x00, 0x00, 0xD8, 0xC7, 0x68, 0x46, 0x0F },   /* ch7: HH (mod) / SD (car)  */
    { 0x02, 0x88, 0x00, 0x00, 0xC8, 0xF5, 0x67, 0x65, 0x00 },   /* ch8: TT (mod) / CY (car)  */
};

uint8_t  tempo        /* 0x0CB7 */, tempo_acc /* 0x0CB8 */, fade_wait /* 0x0CB9 */;
uint8_t  skip_tick    /* 0x0CBA */, vib_phase /* 0x0CBB */, fade_refresh /* 0x0CBC */;
uint16_t score_off    /* 0x0CBD */, score_seg /* 0x0CBF */;
uint16_t patch_table  /* 0x0CC1 */;      /* header +15: OPL patches                 */
uint16_t durtab_base  /* 0x0CC3 */;      /* header +19                              */
uint8_t  tracks_ended /* 0x0CC5 */;      /* 7 = all done                            */
uint8_t  div2         /* 0x0CC6 */;

/* ------------------------------------------------------------------------- */
/* OPL register access                                                       */
/* ------------------------------------------------------------------------- */

/* 0x0B11: out 388h = AH, 10 reads; out 389h = AL, 35 reads (the AdLib delay idiom). */
void opl_write(uint8_t reg /* AH */, uint8_t val /* AL */)   /* 0x0B11 */
{
    outb(0x388, reg); inb(0x388) x10;
    outb(0x389, val); inb(0x388) x35;
}
/* 0x0B08: skipped while music is off (FF28).  0x0B00: also skipped for a channel
 * the sound driver has claimed (INT 60h AX=6, [di+2B]). */
void opl_write_music(reg, val)                 /* 0x0B08 */ { if (!FF28_music_off) opl_write(reg, val); }
void opl_write_chan(struct opl_channel *c, reg, val) /* 0x0B00 */ { if (!c->muted) opl_write_music(reg, val); }

/* 0x03D2: BD = 0, B0..B8 = 0 (every voice key-off).  Used by stop/pause/enable-off. */
void silence(void)                             /* 0x03D2 */
{
    opl_write(0xBD, 0x00);
    for (r = 0xB0; r <= 0xB8; r++) opl_write(r, 0x00);
}

/* ------------------------------------------------------------------------- */
/* AX=0: music_start                                                         */
/* ------------------------------------------------------------------------- */

void music_start(void)                         /* 0x0247 */
{
    reset_state();                             /* 0x021F: clear 0B99..0CC6, FF21..FF25, FF26=FF */
    silence();
    opl_write(0x01, 0x20);                     /* enable waveform select        */
    score_off = SI; score_seg = DS; base = SI;
    SI = base + 1;                             /* header +01: 6 AdLib tracks    */
    for (i = 0; i < 6; i++) {
        chan_init(&ch[i], base);               /* 0x0350: pos = ret = base + *SI++, remaining 1,
                                                  block 3, opl_ch = i, gate 1, attenuation 7F */
    }
    SI += 6;                                   /* skip the 3 Tandy tracks       */
    rhythm.pos = rhythm.ret = base + lodsw();  /* +13, via 0x0376; remaining 1  */
    rhythm.opl_ch = 0x80;
    patch_table = base + lodsw();              /* +15  OPL patch table          */
    lodsw();                                   /* +17  (STD/JR patches, unused) */
    durtab_base = base + lodsw();              /* +19                           */
    opl_write(0xBD, 0x20);                     /* rhythm mode on, no drums      */
    FF24_fade_rate = 0; FF26_stopped = 0; vib_phase = 0;
    fade_refresh = 0; fade_wait = 1; div2 = 1;
    tempo = 0x7F; tempo_acc = 0; tracks_ended = 0;
    load_perc_patches();                       /* 0x02C3..0x030A: perc_patch[] into ops of
                                                  ch 6/7/8, then A6/B6 = 0x120 blk 1 (BD),
                                                  A7/B7 = 0x150 blk 1, A8/B8 = 0x3C0 blk 0 */
}

/* 0x030D: one 9-byte percussion patch -> operator registers of OPL channel `bl`. */
void load_perc_patch(uint8_t opl_ch, const uint8_t *p)
{
    m = op_offset[opl_ch];
    for (reg = 0x20, i = 0; i < 4; reg += 0x20, i += 2) {   /* 20,40,60,80 */
        opl_write(reg + m,     p[i]);
        opl_write(reg + m + 3, p[i + 1]);
    }
    opl_write(0xE0 + m,     rol(p[8], 2) & 3);            /* waveforms (bits7-6 / 5-4) */
    opl_write(0xE0 + m + 3, rol(p[8], 4) & 3);
    opl_write(0xC0 + opl_ch, p[8]);                       /* feedback/connection        */
}

/* ------------------------------------------------------------------------- */
/* AX=3 resume / AX=2 enable: re-send patches and volumes (0x0386)           */
/* ------------------------------------------------------------------------- */

void resume(void)                              /* 0x0386 */
{
    load_perc_patches();
    rhythm_levels(&rhythm);                    /* 0x0988 */
    for (i = 0; i < 6; i++) load_patch(&ch[i], ch[i].patch);   /* 0x03B1 -> 0x05AF */
}

/* ------------------------------------------------------------------------- */
/* Patches                                                                   */
/* ------------------------------------------------------------------------- */

/* 80-BF: patch n = patch_table + 15n (0x0598). */
void select_patch(struct opl_channel *c, uint8_t n) /* 0x0598 */
{
    c->patch = patch_table + n * 15;
    load_patch(c, c->patch);
}

/* 0x05AF: program both operators from the 15-byte record:
 *   +0..+5 vibrato (cmd_vibrato 0x0552, as STD)
 *   +6 mod 20h  +7 car 23h  |  +8 mod 40h  +9 car 43h (levels: see set_level)
 *   +10 mod 60h +11 car 63h |  +12 mod 80h +13 car 83h
 *   +14 bits7-6 mod WS, bits5-4 car WS, bits3-1 feedback, bit0 additive */
void load_patch(struct opl_channel *c, uint16_t p)
{
    SI = p; cmd_vibrato(c);                    /* 0x0552 */
    SI = p + 6;
    m = op_offset[c->opl_ch];
    opl_write_chan(c, 0x80 + m, 0xFF); opl_write_chan(c, 0x83 + m, 0xFF);   /* fast release */
    opl_write_chan(c, 0x40 + m, 0xFF); opl_write_chan(c, 0x43 + m, 0xFF);   /* mute        */
    opl_write_chan(c, 0x20 + m, lodsb()); opl_write_chan(c, 0x23 + m, lodsb());
    lodsb(); lodsb();                          /* +8/+9 levels: applied by set_level */
    opl_write_chan(c, 0x60 + m, lodsb()); opl_write_chan(c, 0x63 + m, lodsb());
    opl_write_chan(c, 0x80 + m, lodsb()); opl_write_chan(c, 0x83 + m, lodsb());
    b14 = lodsb();
    opl_write_chan(c, 0xE0 + m,     rol(b14, 2) & 3);
    opl_write_chan(c, 0xE3 + m,     rol(b14, 4) & 3);
    c->conn = b14 & 0x0F;
    opl_write_chan(c, 0xC0 + c->opl_ch, b14 & 0x0F);
    set_level(c);                              /* 0x0630 */
}

/* 0x0630: carrier 43h = KSL bits of patch+9 | min(3F, (patch+9 & 3F) + attenuation/2 +
 * FF25/4); the modulator 40h is scaled the same way only for additive patches. */
void set_level(struct opl_channel *c)          /* 0x0630 */
{
    m = op_offset[c->opl_ch];
    att = (c->attenuation >> 1) + (FF25_fade_level >> 2);
    mod = score[c->patch + 8];
    if (c->conn & 1) {
        l = (mod & 0x3F) + att; if (l > 0x3F) l = 0x3F;
        mod = (mod & 0xC0) | l;
    }
    opl_write_chan(c, 0x40 + m, mod);
    car = score[c->patch + 9];
    l = (car & 0x3F) + att; if (l > 0x3F) l = 0x3F;
    opl_write_chan(c, 0x43 + m, (car & 0xC0) | l);
}

/* E5 (0x057E): attenuation = u8; rhythm channel -> all five drum levels. */
void cmd_volume(struct opl_channel *c)
{
    c->attenuation = lodsb();
    if (c->opl_ch & 0x80) rhythm_levels(c); else set_level(c);
}

/* C0-CF (0x0699): as STD but the result is clamped to 0..3F (test bl,C0). */

/* ------------------------------------------------------------------------- */
/* Pitch                                                                     */
/* ------------------------------------------------------------------------- */

/* 0x074E: fnum_block = fnum[p-1] + detune | block << 10. */
void set_pitch(struct opl_channel *c, uint8_t p)
{
    c->vib_note_scale = vib_scale[p - 1];
    c->fnum_block = (fnum[p - 1] + (int16_t)c->detune) | (c->block << 10);
}

/* Note byte (0x06E8): identical to STD's 0x04B7 except key-on sets nflags bit6
 * (0x0747) and the frequency is written by write_freq. */
void key_on(struct opl_channel *c)             /* 0x072D */
{
    c->vib_delay = c->vib_start; c->vib_offset = 0; c->vib_frac = 0x80;
    c->nflags &= ~0x02;
    c->nflags |= 0x40;                         /* key-on                        */
    write_freq(c);                             /* 0x077D                        */
}
void release(struct opl_channel *c)            /* 0x0778 */
{
    c->nflags &= ~0x40;                        /* key-off, OPL release phase   */
    write_freq(c);
}
/* 0x077D: A0 = low byte, B0 = (high & 1F) | key-on<<5. */
void write_freq(struct opl_channel *c)
{
    v = (c->fnum_block + c->vib_offset) & 0x1FFF;
    if (c->nflags & 0x40) v |= 0x2000;
    opl_write_chan(c, 0xA0 + c->opl_ch, lo(v));
    opl_write_chan(c, 0xB0 + c->opl_ch, hi(v));
}

/* Vibrato (0x07A6..0x08A6) is STD's algorithm on the f-number; the step is only
 * shifted right by the block when flags bit5 of the channel (0x07D0: [di+0C] & 20 —
 * never set for channels 0-5) is on, so steps are in raw f-number units. Every
 * offset change re-writes A0/B0 through write_freq. */

/* ------------------------------------------------------------------------- */
/* Rhythm channel (0x08A9)                                                   */
/* ------------------------------------------------------------------------- */

void rhythm_tick(struct opl_channel *c)        /* 0x08A9 */
{
    if (c->flags & 1) return;
    if (FF24_fade_rate) rhythm_levels(c);      /* fade refresh (0x0583)         */
    if (skip_tick) return;
    if (--c->remaining) return;
    SI = c->pos;
    for (;;) {
        b = lodsb();
        if (b < 0x80) {                        /* 0x08F8: hit                    */
            mask = b & 0x1F;                   /* bit4 BD, 3 SD, 2 TT, 1 CY, 0 HH */
            if (mask) {
                opl_write_music(0xBD, (c->bd_bits & ~mask & 0x1F) | 0x20);   /* key-off those */
                c->bd_bits |= mask | 0x20;
                opl_write_music(0xBD, c->bd_bits);                            /* key-on        */
            }
            c->remaining = (b & 0x20) ? lodsb() : c->default_dur;
            c->pos = SI;
            return;
        }
        if (b < 0xA0) {                        /* 0x0932: key-off drums in bits 0-4 */
            c->bd_bits = (c->bd_bits & ~(b & 0x1F)) | 0x20;
            opl_write_music(0xBD, c->bd_bits);
        } else if (b < 0xC8) {                 /* 0x0945: default duration        */
            c->default_dur = score[c->durtab + (b & 7)];
        } else if (b < 0xCD) {                 /* 0x0956: drum level += i8, clamp 0..3F */
            i = b - 0xC8; v = (c->level[i] & 0x3F) + (int8_t)lodsb();
            c->level[i] = (v < 0) ? 0 : (v > 0x3F) ? 0x3F : v;
            rhythm_levels(c);
        } else if (b < 0xD2) {                 /* 0x0979: drum level = u8 * 4     */
            c->level[b - 0xCD] = lodsb() << 2;
            rhythm_levels(c);
        } else {
            cmd[0x10 + (b & 0x0F)](c);         /* 0x08EB: table 0x0526 = F0..FF   */
            if (c->flags & 1) return;          /* FF: tracks_ended == 7 stops     */
        }
    }
}

/* 0x0988: level[0..4] (+FF25/4, clamp 3F) -> 53h (BD car), 51h (HH), 54h (SD),
 * 52h (TT), 55h (CY). */
void rhythm_levels(struct opl_channel *c)
{
    f = FF25_fade_level >> 2;
    opl_write_music(0x53, min(0x3F, c->level[0] + f));
    opl_write_music(0x51, min(0x3F, c->level[1] + f));
    opl_write_music(0x54, min(0x3F, c->level[2] + f));
    opl_write_music(0x52, min(0x3F, c->level[3] + f));
    opl_write_music(0x55, min(0x3F, c->level[4] + f));
}

/* ------------------------------------------------------------------------- */
/* Command-table differences vs STD (table 0x0506)                           */
/* ------------------------------------------------------------------------- */
/*   E9, EA, EB, EC  -> 0x0597 `ret` (no argument byte, unlike STD/JR)
 *   F4 (0x09F7)     -> u8 i, u8 v, u16 a: if FF21[i] == v then jump a   (STD: nop)
 *   FF (0x0AE3)     -> flags |= 1; if ++tracks_ended == 7: FF26 = 3F, silence()
 * Everything else is byte-for-byte the STD logic with the field offsets above.
 */

/* ------------------------------------------------------------------------- */
/* INT 60h AX=6 (0x01A0): sound-driver hand-over for channels 4 and 5        */
/* ------------------------------------------------------------------------- */
void sfx_reset_opl(void)
{
    if (CL) {                                  /* 0x01F6: claim                  */
        ch[4].muted = (CL & 1) ? 0xFF : 0;
        ch[5].muted = (CL & 2) ? 0xFF : 0;
        return;
    }
    /* CL = 0: give back — key-off the two channels, then, if playing, re-send patch
     * and frequency for channels 4 and 5 (0x03B1 -> load_patch). */
    for (i = 4; i <= 5; i++) if (ch[i].muted) { ch[i].muted = 0; release(&ch[i]); }
    if (FF26_stopped || FF0B_pause || FF28_music_off) { silence(); return; }
    load_patch(&ch[4], ch[4].patch); load_patch(&ch[5], ch[5].patch);
}
