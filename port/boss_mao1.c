/* boss_mao1.c — cavern 9 "Jashiin" APPEARANCE (MAO1.BIN = ZELRES3[18], 1437
 * bytes), map mp90.  Ported from src/ai/boss_mao1.c and the image's own
 * listing.
 *
 *   [A002] = A581: col 0x10 (0x0D from pose 3), row 1, HP 250 (never used),
 *   EXP 200 (never awarded), camera column 5, knockback always left, name
 *   "Jashiin", gold 0.  Contact A010 = 0 for every class.
 *
 * This is not a fight but a 135-frame cutscene.  The entry never looks at the
 * hit bits and nothing decrements HP; each frame it advances the script at
 * A3BB by one byte:
 *
 *   0x00..0x0A  the pose to draw (boss_col = pose < 3 ? 0x10 : 0x0D)
 *   0x80 | n    show text n — {u16 x, chars, 0xFF} at [A442 + 2n]
 *   0xC0        clear the two text rows of the screen copy
 *   0xE0        sound 0x38
 *   0xFF        [E6] = 0: leave boss mode and let the map run as a normal level
 *
 * Image (A2A1): six columns at TWO-cell spacing, eight rows at two-cell
 * spacing (A2EA doubles the row counter), one bitmap byte per column
 * ([A52F + 2*pose], bit 7 = row 0) and one `class << 4 | frame` byte per set
 * bit ([A495 + 2*pose]).  Parts are {col + 2c, row + 2r, type = class,
 * phase = byte & 0x0F, hit = 0} — no solid bit and no contact damage.  Poses
 * 0..2 are class 0 only (6-10 parts, the human figure), 3..6 add classes 1-2
 * and 7..0x0A classes 3..6 (12-17 parts): Jashiin grows into the demon.
 */
#include "boss.h"
#include <string.h>

enum { S_INIT, S_POSE, S_TEXT };
#define ST(n) (g->boss.st[n])
#define SPOS  (g->boss.sw[0])                   /* A59C, the script cursor */

#define M1_SCRIPT 0xA3BB
#define M1_TEXTS  0xA442                        /* u16[3] -> {u16 x, chars, FF} */
#define M1_LIST   0xA495                        /* u16[11] */
#define M1_BM     0xA52F

#define M1_W 6
#define M1_H 8

/* A376: the video [2000] text box is cleared and [202A] prints the string at
 * its own x.  The port has no cavern text box of its own, so the line goes
 * through the message box fight.bin 73E0 uses. */
static void mao1_text(Game *g, unsigned n)
{
    unsigned p = boss_img16(g, M1_TEXTS + 2u * n);
    if (!p) return;
    char buf[80];
    unsigned i = 0;
    for (unsigned a = p + 2; i < sizeof buf - 1; a++) {
        uint8_t c = boss_img8(g, a);
        if (c == 0xFF) break;
        buf[i++] = (char)(c == '\\' ? '\'' : c);        /* the font's apostrophe */
    }
    buf[i] = 0;
    game_message(g, buf);
}

/* A2A1 */
static void mao1_draw(Game *g)
{
    Boss *b = &g->boss;
    uint8_t buf[M1_W * M1_H];
    unsigned pose = ST(S_POSE) > 10 ? 10 : ST(S_POSE);
    memset(buf, 0xFF, sizeof buf);
    boss_paste(g, buf, M1_W, M1_H, 0, 0, 6, 1,
               boss_img16(g, M1_LIST + 2 * pose), boss_img16(g, M1_BM + 2 * pose));
    boss_parts_begin(g);
    for (int c = 0; c < M1_W; c++)
        for (int r = 0; r < M1_H; r++) {
            uint8_t v = buf[c * M1_H + r];
            if (v == 0xFF) continue;
            boss_part(g, (uint16_t)(b->col + 2 * c), (uint8_t)((b->row + 2 * r) & 0x3F),
                      (uint8_t)((v >> 4) & 0x0F), (uint8_t)(v & 0x0F));
        }
    boss_parts_end(g);
}

/* 0xA23C  Frame entry. */
void boss_mao1_entry(Game *g)
{
    boss_readback(g, NULL);                             /* A240: restore only */
    if (!ST(S_INIT)) { ST(S_INIT) = 1; SPOS = 0; }

    uint8_t c = boss_img8(g, M1_SCRIPT + (unsigned)(++SPOS));    /* A27B */
    if (!(c & 0x80)) ST(S_POSE) = c;
    else switch (c & 0xF0) {
    case 0x80: mao1_text(g, c & 0x0F); break;           /* A376 */
    case 0xC0: break;                                   /* A3A2: force a redraw */
    case 0xE0: g->sfx_request = 0x38; break;            /* A370 */
    default:
        if (c == 0xFF) {                                /* A36A: [E6] = 0 */
            g->boss_room = 0;
            g->boss.active = 0;
            boss_parts_begin(g); boss_parts_end(g);
            return;
        }
        break;
    }
    g->boss.col = (uint16_t)(ST(S_POSE) < 3 ? 0x10 : 0x0D);      /* A290 */
    mao1_draw(g);
}
