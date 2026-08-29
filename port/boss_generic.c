/* boss_generic.c — placeholder overlay for the six bosses whose part
 * composition is not ported yet (MEDA, LEGA, DRGN, AKMA, MAO1, MAO2).
 *
 * It runs the *protocol* faithfully — the [A002] info block, the per-frame
 * readback of the pending-hit bits, the HP word, the 40-frame death and the
 * post-boss transition — but draws the boss as the parts of class 0 in a
 * block the size docs/ENEMIES.md §3 gives for it, and applies the documented
 * damage rule without the per-part weak points.  It is enough to fight and
 * kill the boss; it is NOT the original's animation.  src/ai/boss_meda.c,
 * boss_lega.c, boss_drgn.c, boss_akma.c, boss_mao1.c and boss_mao2.c hold the
 * behaviour that still has to be ported. */
#include "boss.h"
#include <string.h>

/* docs/ENEMIES.md §3: {parts across, parts down, damage numerator, denominator} */
static void geometry(int idx, int *w, int *h, int *num, int *den)
{
    switch (idx) {
    case BOSS_MEDA: *w = 6; *h = 7; *num = 1; *den = 8; break;      /* 14x12 cells */
    case BOSS_LEGA: *w = 8; *h = 8; *num = 2; *den = 1; break;      /* sword x2 */
    case BOSS_DRGN: *w = 8; *h = 5; *num = 1; *den = 2; break;
    case BOSS_AKMA: *w = 7; *h = 8; *num = 1; *den = 1; break;
    case BOSS_MAO1: *w = 6; *h = 8; *num = 0; *den = 1; break;      /* not a fight */
    default:        *w = 6; *h = 5; *num = 1; *den = 2; break;      /* MAO2 */
    }
}

void boss_generic_entry(Game *g)
{
    Boss *b = &g->boss;
    int w, h, num, den;
    geometry(b->index, &w, &h, &num, &den);
    uint8_t hit = boss_readback(g, NULL);

    if (!g->boss_cutscene && hit && num) {
        unsigned d = (unsigned)damage_for_source(g, (uint8_t)(hit & 0x1F)) * (unsigned)num / (unsigned)den;
        boss_damage(g, d ? d : 1);
        g->sfx_request = 0x22;
    }
    if (g->boss_cutscene) {
        if (boss_death_tick(g) >= 0x28) { boss_parts_begin(g); boss_parts_end(g); return; }
        b->pose = (uint8_t)((b->pose + 1) & 3);
    } else {
        /* the one movement every boss shares: shuffle toward the hero */
        if (!(++b->parity & 3)) {
            uint16_t hero = boss_hero_col(g, b->cam_col);
            if (b->col + w > hero + 2) b->col--;
            else if (b->col + w + 2 < hero) b->col++;
        }
    }
    boss_parts_begin(g);
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++)
            boss_part(g, (uint16_t)(b->col + 2 * c), (uint8_t)(b->row + 2 * r), 0, b->pose);
    boss_parts_end(g);
}
