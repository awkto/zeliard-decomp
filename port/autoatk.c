/* autoatk.c — see autoatk.h. */
#include "autoatk.h"
#include "physics.h"

void autoatk_reset(AutoAtk *s) { s->phase = 0; s->active = 0; }

void autoatk_step(AutoAtk *s, int combo, int autofire, uint8_t *dirs, uint8_t *btns)
{
    if (!combo && !autofire) { autoatk_reset(s); return; }
    if (!s->active) { s->phase = 0; s->active = 1; }        /* the first frame is a press */
    int high = (s->phase % AUTOATK_PERIOD) < (AUTOATK_PERIOD / 2);
    s->phase++;
    if (combo) { *dirs |= DIR_UP | DIR_DOWN; *btns |= high ? 1 : 0; return; }
    if (*dirs & DIR_DOWN) { *btns |= 1; return; }           /* Down+S: held piercing thrust */
    if (high) *btns |= 1;
}
