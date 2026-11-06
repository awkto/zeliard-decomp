/* sndjr.c — SNDJR.DRV in C.  See sndjr.h; `K xxxx` = address in disasm/SNDJR.asm. */
#include "sndjr.h"
#include "sar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DRV_BASE 0x1100
#define K_TAB    0x170E          /* effect table: {prio, trackA, trackB, durtab}, 7 bytes */
#define K_PATCH  0x2195          /* 10-byte patch records */
#define K_PERIOD 0x1632          /* tone periods, 12 words */
#define K_SCALE  0x164A          /* vibrato scale, 12 bytes */

static uint8_t  kb(const SndJr *s, unsigned a) { a -= DRV_BASE; return a < s->len ? s->drv[a] : 0xFF; }
static uint16_t kw(const SndJr *s, unsigned a) { return (uint16_t)(kb(s, a) | kb(s, a + 1) << 8); }
static uint8_t  kfetch(SndJr *s, SndJrChan *c) { return kb(s, c->pos++); }

static void out(SndJr *s, uint8_t b) { if (s->sn) s->sn(s->sn_u, b); }
static void claim(SndJr *s, uint8_t cl) { if (s->claim) s->claim(s->claim_u, cl); }

/* K 1623: tell the music driver which chip channels are ours */
static void k1623(SndJr *s) { claim(s, (uint8_t)(~s->mask & 0x3F)); }

/* K 1405: tone period + vibrato offset -> two bytes */
static void tone(SndJr *s, SndJrChan *c)
{
    uint16_t cx = (uint16_t)(c->base + (uint16_t)c->vib_off);
    if (cx & 0xFC00) return;
    out(s, (uint8_t)(0x80 | ((c->chip & 3) << 5) | (cx & 0x0F)));
    out(s, (uint8_t)(cx >> 4));
}

/* K 1590: envelope x attenuation -> attenuation nibble (no fade here) */
static void volume(SndJr *s, SndJrChan *c)
{
    uint8_t dl = (uint8_t)(~(c->att >> 3) & 0x0F);
    uint8_t al = (uint8_t)(c->level & 0xF0);
    for (int i = 0; i < 4; i++) {
        int cf = al >> 7;
        al = (uint8_t)(al << 1);
        if (cf) al = (uint8_t)(al + dl);
    }
    al = (uint8_t)(al + 0x0F);
    al = (uint8_t)(((al >> 4) & 0x0F) ^ 0x0F);
    if (!(c->pflags & 7)) out(s, (uint8_t)(al | (((2 * (c->chip & 3) + 1) << 4) & 0xFF) | 0x80));
    if (c->pflags & 0x38) return;
    out(s, (uint8_t)(al | 0xF0));
}

/* K 1439: release */
static void release(SndJr *s, SndJrChan *c)
{
    unsigned b = c->patch;
    uint8_t drop = (uint8_t)(kb(s, b + 6) & 0xF0);
    c->level = (uint8_t)(c->level >= drop ? c->level - drop : 0);
    c->phase = 1;
    c->count = kb(s, b + 5);
    c->step = kb(s, b + 4);
    volume(s, c);
}

static void flags_mask(SndJr *s, SndJrChan *c, uint8_t v)     /* K 12AE / 12DB */
{
    uint8_t keep = c->mask;
    s->mask = (uint8_t)((s->mask & keep) | (v & (uint8_t)~keep));
}

/* K 13CD: note -> period */
static void pitch(SndJr *s, SndJrChan *c, int p)
{
    int i = p - 1;
    if (i > 11) i = 11;
    c->scale = kb(s, K_SCALE + (unsigned)i);
    uint16_t ax = (uint16_t)(kw(s, K_PERIOD + 2u * (unsigned)i) + (int16_t)c->detune);
    if (c->octave > 1) {
        unsigned cl = (uint8_t)(c->octave - 1);
        c->base = cl >= 16 ? 0 : (uint16_t)(ax >> cl);
        return;
    }
    c->base = ax;
    if (i < 9) c->base = (uint16_t)(ax >> 1);
}

static void note(SndJr *s, SndJrChan *c, uint8_t b)           /* K 1349 */
{
    c->nflags &= (uint8_t)~0x10;
    if (kb(s, c->pos) == 0xE7) c->nflags |= 0x10;
    c->remaining = kb(s, (unsigned)(c->durtab + (b >> 4)));
    int p = b & 0x0F;
    if (p == 0) { release(s, c); return; }
    if (p == 0x0F) return;
    pitch(s, c, p);
    uint8_t n = c->nflags;
    c->nflags &= (uint8_t)~0x20;
    if (!(n & 0x20)) {
        c->vib_delay = c->vib_start;
        c->vib_off = 0; c->vib_frac = 0x80;
        c->nflags &= (uint8_t)~0x02;
        unsigned pb = c->patch;
        c->level = (uint8_t)(kb(s, pb + 7) << 4);
        c->phase = 0x12;
        c->count = kb(s, pb + 1);
        c->step = kb(s, pb);
        c->wait = 1;
        volume(s, c);
    }
    if (c->pflags & 7) return;
    tone(s, c);
}

static void end_track(SndJr *s, SndJrChan *c)                 /* K 15FB */
{
    c->flags |= 1;
    if (++s->ended != 2) return;
    s->idle = 0xFF; s->prio = 0; s->mask = 0x3F;
    claim(s, 0);
}

static void patch(SndJr *s, SndJrChan *c, int n)              /* K 12C3 */
{
    unsigned si = K_PATCH + 10u * (unsigned)n;
    c->patch = (uint16_t)si;
    c->pflags = kb(s, si + 8);
    flags_mask(s, c, (uint8_t)(c->pflags & 0x3F));
    if (!(c->pflags & 0x08)) out(s, (uint8_t)((kb(s, si + 9) / 11 + 4) | 0xE0));
    k1623(s);
}

static void command(SndJr *s, SndJrChan *c, uint8_t b)
{
    if (b < 0xC0) { patch(s, c, b & 0x3F); return; }
    if (b < 0xD0) {                                           /* K 1305 */
        int v = (int8_t)(uint8_t)(b << 4) >> 2;
        int bl = c->att;
        if (v >= 0) { bl = (bl - (v + 4)) & 0xFF; if (bl & 0x80) bl = 0; }
        else        { bl = (bl - v) & 0xFF;       if (bl & 0x80) bl = 0x7F; }
        c->att = (uint8_t)bl;
        volume(s, c);
        return;
    }
    if (b < 0xD8) { c->octave = (uint8_t)(b & 7); return; }
    if (b < 0xE0) { c->gate = kb(s, (unsigned)(c->durtab + (b & 7))); return; }
    switch (b) {
    case 0xE0: s->tempo = kfetch(s, c); return;
    case 0xE1: c->detune = (int8_t)((int8_t)kfetch(s, c) >> 3); return;
    case 0xE2: {                                              /* K 1265 */
        c->flags &= (uint8_t)~0x40;
        uint8_t d = kb(s, c->pos++);
        if (!d) return;
        c->flags |= 0x40;
        c->vib_start = d;
        c->mul_up = kb(s, c->pos); c->mul_down = kb(s, c->pos + 1);
        c->len_up = kb(s, c->pos + 2); c->len_down = kb(s, c->pos + 3);
        c->ctl = kb(s, c->pos + 4);
        c->pos += 5;
        c->nflags &= (uint8_t)~0x02;
        return; }
    case 0xE3: c->octave--; return;
    case 0xE4: c->octave++; return;
    case 0xE5: c->att = kfetch(s, c); volume(s, c); return;
    case 0xE7: c->nflags |= 0x20; return;
    case 0xE9: out(s, (uint8_t)((kfetch(s, c) / 11 + 4) | 0xE0)); return;
    case 0xEA: {                                              /* K 12A6 */
        uint8_t a = kfetch(s, c);
        c->pflags = (uint8_t)((c->pflags & 0xC0) | a);
        flags_mask(s, c, a);
        k1623(s);
        return; }
    case 0xF0: c->durtab = (uint16_t)(s->durtabs + 8u * kfetch(s, c)); return;
    case 0xFF: end_track(s, c); return;
    default: return;                                          /* E6 E8 EB-EF F1-FE */
    }
}

static void vibrato(SndJr *s, SndJrChan *c)                   /* K 1489 */
{
    if (!(c->nflags & 0x02)) {
        unsigned up = (unsigned)c->mul_up * c->scale, dn = (unsigned)c->mul_down * c->scale;
        if (c->octave > 1) {
            unsigned cl = (uint8_t)(c->octave - 1);
            up = cl >= 16 ? 0 : up >> cl; dn = cl >= 16 ? 0 : dn >> cl;
        }
        c->step_up = (uint16_t)up; c->step_down = (uint16_t)dn;
        c->half = (uint8_t)(((c->ctl & 0x80) ? c->len_down : c->len_up) >> 1);
        c->vib_frac = 0x80;
        c->flags = (uint8_t)((c->flags & 0x7F) | (c->ctl & 0x80));
        c->nflags |= 0x02;
    }
    c->vib_delay = (uint8_t)(c->ctl & 0x1F);
    if (--c->half == 0) {
        if (c->flags & 0x80) { c->half = c->len_up; c->flags &= (uint8_t)~0x80; }
        else                 { c->half = c->len_down; c->flags |= 0x80; }
    }
    if (!(c->flags & 0x80)) {
        unsigned f = c->vib_frac + (c->step_up & 0xFF);
        c->vib_frac = (uint8_t)f;
        uint8_t hi = (uint8_t)((c->step_up >> 8) + (f >> 8));
        if (!hi) return;
        c->vib_off = (int16_t)(c->vib_off + hi);
    } else {
        int f = (int)c->vib_frac - (int)(c->step_down & 0xFF);
        c->vib_frac = (uint8_t)f;
        uint8_t hi = (uint8_t)((c->step_down >> 8) + (f < 0 ? 1 : 0));
        if (!hi) return;
        c->vib_off = (int16_t)(c->vib_off - hi);
    }
    tone(s, c);
}

static void envelope(SndJr *s, SndJrChan *c)                  /* K 152B */
{
    if (--c->wait != 0) return;
    unsigned b = c->patch;
    c->wait = (uint8_t)(kb(s, b + 6) & 0x0F);
    if ((c->phase & 0x0F) == 0) return;
    int ch = c->level, cl = c->step;
    if (cl & 1) { cl &= 0xFE; ch = ch >= cl ? ch - cl : 0; }
    else        { ch = ch + cl > 0xFF ? 0xFF : ch + cl; }
    if (--c->count == 0) {
        if (--c->phase == 0x11) {
            c->count = kb(s, b + 3);
            c->step = kb(s, b + 2);
            int drop = kb(s, b + 7) & 0xF0;
            ch = ch >= drop ? ch - drop : 0;
        }
    }
    if (ch == c->level) return;
    c->level = (uint8_t)ch;
    volume(s, c);
}

static void tail(SndJr *s, SndJrChan *c)                      /* K 1460 */
{
    if (s->idle) return;
    if (c->pflags & 7) { envelope(s, c); return; }
    if (!c->phase) return;
    if ((c->flags & 0x40) && --c->vib_delay == 0) vibrato(s, c);
    envelope(s, c);
}

static void chan_tick(SndJr *s, SndJrChan *c)                 /* K 11B4 */
{
    if (c->flags & 1) return;
    if (!s->skip) {
        if (--c->remaining == 0) {
            for (int guard = 0; guard < 4096; guard++) {
                uint8_t b = kfetch(s, c);
                if (b < 0x80) { note(s, c, b); break; }
                command(s, c, b);
                if (c->flags & 1) break;
            }
        } else if (c->gate >= c->remaining && !(c->nflags & 0x10)) {
            release(s, c);
        }
    }
    tail(s, c);
}

static void start(SndJr *s, int id)                           /* K 110D */
{
    if (s->off || id <= 0 || (unsigned)id > s->neff) return;
    unsigned rec = K_TAB + 7u * (unsigned)(id - 1);
    uint8_t prio = kb(s, rec);
    if (prio < s->prio) return;
    s->prio = prio;
    uint8_t bh = 0xDB;
    for (int i = 0; i < 2; i++) {
        SndJrChan *c = &s->ch[i];
        c->pos = kw(s, rec + 1 + 2u * (unsigned)i);
        c->remaining = 1; c->octave = 3; c->gate = 1; c->att = 0x7F; c->nflags = 0; c->flags = 0;
        c->chip = (uint8_t)(2 - i);
        c->mask = bh;
        bh = (uint8_t)((bh >> 1) | (bh << 7));
    }
    s->durtabs = kw(s, rec + 5);
    s->idle = 0; s->tempo = 0x7F; s->ended = 0; s->mask = 0x3F;
    k1623(s);
}

void sndjr_tick(SndJr *s, int *request)                       /* K 1188 */
{
    if (*request) { int id = *request; *request = 0; start(s, id); }
    if (s->idle || !s->drv) return;
    unsigned a = (unsigned)s->acc + s->tempo;
    s->skip = (uint8_t)(a > 0xFF ? 0xFF : 0);
    s->acc = (uint8_t)a;
    chan_tick(s, &s->ch[0]);
    chan_tick(s, &s->ch[1]);
}

void sndjr_enable(SndJr *s, int on)
{
    s->off = (uint8_t)(on ? 0 : 0xFF);          /* STICK 0233 only flips FF27; a running effect plays on */
}

int sndjr_load(SndJr *s, const char *dir)
{
    FILE *f = game_fopen(dir, "SNDJR.DRV");
    if (!f) return -1;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 0x1100) { fclose(f); return -1; }
    uint8_t *d = malloc((size_t)n);
    if (!d || fread(d, 1, (size_t)n, f) != (size_t)n) { free(d); fclose(f); return -1; }
    fclose(f);
    void (*sn)(void *, uint8_t) = s->sn; void *snu = s->sn_u;
    void (*cl)(void *, uint8_t) = s->claim; void *clu = s->claim_u;
    free(s->drv);
    memset(s, 0, sizeof *s);
    s->sn = sn; s->sn_u = snu; s->claim = cl; s->claim_u = clu;
    s->drv = d; s->len = (size_t)n;
    s->idle = 0xFF; s->mask = 0x3F;
    unsigned first = kw(s, K_TAB + 1);
    s->neff = first > K_TAB ? (first - K_TAB) / 7 : 0;
    return 0;
}

void sndjr_free(SndJr *s) { free(s->drv); s->drv = NULL; s->len = 0; }

int sndjr_entry(const SndJr *s, int id, int *prio, unsigned *t0, unsigned *t1, unsigned *durtab)
{
    if (!s->drv || id <= 0 || (unsigned)id > s->neff) return -1;
    unsigned rec = K_TAB + 7u * (unsigned)(id - 1);
    if (prio) *prio = kb(s, rec);
    if (t0) *t0 = kw(s, rec + 1);
    if (t1) *t1 = kw(s, rec + 3);
    if (durtab) *durtab = kw(s, rec + 5);
    return 0;
}
