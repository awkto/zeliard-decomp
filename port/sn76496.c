/* sn76496.c — the SN76496 core.  See sn76496.h. */
#include "sn76496.h"
#include <math.h>
#include <string.h>

void sn76496_set_lfsr(Sn76496 *s, int preset)
{
    if (preset == SN_LFSR_NCR) { s->fb_mask = 0x8000;  s->tap1 = 0x02; s->tap2 = 0x20; s->ncr = 1; }
    else                       { s->fb_mask = 0x10000; s->tap1 = 0x04; s->tap2 = 0x08; s->ncr = 0; }
    s->rng = s->fb_mask;
}

void sn76496_init(Sn76496 *s, double clock, double sample_rate)
{
    memset(s, 0, sizeof *s);
    for (int i = 1; i < 8; i += 2) s->reg[i] = 0x0F;          /* attenuation off */
    /* 0.25 per channel at full level, then -2 dB a step */
    double v = 0.25;
    for (int i = 0; i < 15; i++) { s->vol[i] = v; v /= 1.258925412; }
    s->vol[15] = 0.0;
    for (int i = 0; i < 4; i++) s->period[i] = i < 3 ? 0x400 : 32;
    sn76496_set_lfsr(s, SN_LFSR_TI);
    s->out[3] = (int)(s->rng & 1);
    s->ticks_per_sample = (clock / 16.0) / (sample_rate > 0 ? sample_rate : 44100.0);
}

void sn76496_write(Sn76496 *s, uint8_t data)
{
    int r;
    if (data & 0x80) {
        r = (data & 0x70) >> 4;
        s->latch = r;
        if (s->ncr && r == 6 && ((data & 4) != (s->reg[6] & 4))) s->rng = s->fb_mask;
        s->reg[r] = (uint16_t)((s->reg[r] & 0x3F0) | (data & 0x0F));
    } else {
        r = s->latch;
        if (s->ncr && ((r & 1) || r == 6)) return;
    }
    switch (r) {
    case 0: case 2: case 4:
        if (!(data & 0x80)) s->reg[r] = (uint16_t)((s->reg[r] & 0x0F) | ((data & 0x3F) << 4));
        s->period[r >> 1] = s->reg[r] ? s->reg[r] : 0x400;
        if (r == 4 && (s->reg[6] & 3) == 3) s->period[3] = s->period[2] << 1;
        break;
    case 1: case 3: case 5: case 7:
        if (!(data & 0x80)) s->reg[r] = (uint16_t)((s->reg[r] & 0x3F0) | (data & 0x0F));
        break;
    case 6:
        if (!(data & 0x80)) s->reg[6] = (uint16_t)((s->reg[6] & 0x3F0) | (data & 0x0F));
        s->period[3] = (s->reg[6] & 3) == 3 ? s->period[2] << 1 : 1 << (5 + (s->reg[6] & 3));
        if (!s->ncr) s->rng = s->fb_mask;
        break;
    }
}

double sn76496_tick(Sn76496 *s)
{
    for (int i = 0; i < 3; i++)
        if (--s->count[i] <= 0) { s->out[i] ^= 1; s->count[i] = s->period[i]; }
    if (--s->count[3] <= 0) {
        int white = (s->reg[6] & 4) != 0;
        int fb = ((s->rng & s->tap1) != 0) != (((s->rng & s->tap2) != (s->ncr ? s->tap2 : 0)) && white);
        s->rng >>= 1;
        if (fb) s->rng |= s->fb_mask;
        s->out[3] = (int)(s->rng & 1);
        s->count[3] = s->period[3];
    }
    double v = 0;
    for (int i = 0; i < 4; i++)
        if (s->out[i]) v += s->vol[s->reg[2 * i + 1] & 0x0F];
    return v;
}

double sn76496_sample(Sn76496 *s)
{
    s->tick_acc += s->ticks_per_sample;
    int n = (int)s->tick_acc;
    s->tick_acc -= n;
    double sum = 0;
    for (int i = 0; i < n; i++) sum += sn76496_tick(s);
    return sum / n;
}
