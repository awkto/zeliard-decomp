/* tear.h — rokademo.bin (ZELRES3[0]): the "a Tear of Esmesanti" cutscene, and
 * GAME.BIN's Tear-slot row (docs/CUTSCENES.md §5, src/rokademo.c).
 *
 * Unlike opdemo/enddemo this is not a gd demo: it plays on the fight screen and
 * draws through **gfmcga** and the MCGA video driver, which is why it lives
 * beside the cavern engine and not in cutscene.c.  fight.bin loads it raw over
 * the spent boss AI and `call [cs:0xA000]`s it from the door/transition handler
 * (7C18) when the door record's byte +8 has bit 7 set — the exit door every
 * boss room's post-boss transition installs, so the scene plays as Garland
 * leaves with the crystal.  Afterwards fight.bin jumps to 7CF4, skipping the
 * ordinary 26-frame walk-in, because the cutscene did the walking itself. */
#ifndef ZEL_TEAR_H
#define ZEL_TEAR_H
#include <stdint.h>
#include "physics.h"
#include "text.h"

/* the three gfmcga slots and the video slot the cutscene draws through:
 *   [3022] GF_BLIT_CELL   an 8x8 converted dman.grp cell, opaque
 *   [3024] GF_DRAW_SWORD  a 16x24 2-bpp picture inside gfmcga.bin itself
 *   [3026] GF_SPARKLE     16x16 (4 frames) / 64x16 (2 frames), also in gfmcga
 *   [203E] VID_TEAR_ICON  a 16x13 byte-per-pixel picture inside GMMCGA.BIN */
typedef struct TearArt {
    uint8_t  cell[54][8][8];        /* dman.grp, converted by [3028] */
    int      ncells;
    uint8_t  sword[3][24][16];      /* gfmcga 4A31 / 4A91 / 4AF1, 2-bpp values */
    uint8_t  sword_tbl[6];          /* 4A25: sword [92] 1..6 -> picture 0..2 */
    uint8_t  spark[4][16][16];      /* gfmcga 4BDD + f*0x40 */
    uint8_t  bigspark[2][16][64];   /* gfmcga 4CDD + f*0x100, four 16x16 blocks */
    uint8_t  icon[2][13][16];       /* GMMCGA 2A61 / 2B31, 0x80 = transparent */
    uint8_t  frame[10][9];          /* rokademo A435, column-major 3x3 maps */
    uint8_t  slot_x4[9];            /* GAME.BIN A3D3 */
    int      loaded;
} TearArt;

int  tear_art_load(TearArt *a, const char *dir);

typedef struct Tear {
    Game    *g;
    TearArt *art;
    uint8_t  fb[320 * 200];
    unsigned frames;
} Tear;

struct Shell;
/* fight.bin 7C18: run the cutscene on the freshly painted destination map.
 * Returns 0 when it ran. */
int  tear_cutscene(struct Shell *s);
/* what the cutscene has on screen */
const uint8_t *tear_framebuffer(const Tear *t);

/* GAME.BIN A3A5: the Tear-slot row along the top border, one 16x13 icon per
 * collected Tear at the nine x positions of the table at A3D3, drawn only when
 * `[A0]` is non-zero.  The boot painter calls it unconditionally right after
 * mole.bin (A18E). */
void tear_draw_slots(uint8_t *fb, const Game *g, const TearArt *a);

#endif
