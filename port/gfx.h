/* gfx.h — MCGA palette, 48-byte (PC-88 3-plane) and 32-byte (2-plane) cell
 * decoding, tileset banks and the fman.grp hero frames.  Ports tools/palette.py,
 * tools/cellsheet.py and the "hero" branch of tools/grp2png.py. */
#ifndef ZEL_GFX_H
#define ZEL_GFX_H
#include <stdint.h>

extern uint8_t PAL_RGB[64][3];            /* VGA index (left*8+right) -> 8-bit RGB */
extern const uint8_t PAL2BPP[5][16];          /* gfmcga @4F98.. sprite colour tables */

typedef struct { uint8_t px[8][8]; } Cell8;   /* VGA indices, 0 = black/transparent */
typedef struct { uint8_t px[8][8]; uint8_t mask[8][8]; } Cell2;  /* 2bpp sprite: mask 1 = drawn */

void gfx_decode48(const uint8_t *src, Cell8 *out);
/* 2bpp cell with the gfmcga vec_20 outline mask (horizontal dilation). */
void gfx_decode32(const uint8_t *src, const uint8_t *pal16, Cell2 *out);

/* Cavern tile bank as at arena:8000: MPPx cells 1..N and DCHR.GRP at 0x40..
 * Cell 0 holds the 8 classification lists (docs/FIGHT.md §3). */
#define BANK_CELLS 0x80
typedef struct {
    Cell8   cell[BANK_CELLS];
    uint8_t present[BANK_CELLS];
    uint8_t lists[48];                        /* cell 0: passable[24] conv_l[4] conv_r[4] hazard[4] updraft[4] cur_l[4] cur_r[4] */
    int     mpp_index;                        /* 0..10 = MPP1..MPPB */
} Tileset;
int gfx_load_tileset(Tileset *t, const char *dir, int mpp_index);

/* fman.grp: 91 frame maps x 9 bytes, then the 2bpp bank at 0x333. */
#define HERO_FRAMES 91
typedef struct {
    uint8_t frame[HERO_FRAMES][9];            /* 1-based cell index, bit7 = flip, 0 = empty */
    Cell2   cell[256];
    int     ncells;
} HeroGfx;
int gfx_load_hero(HeroGfx *h, const char *dir);

#endif
