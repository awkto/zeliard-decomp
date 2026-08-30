/* gfx.h — MCGA palette, 48-byte (PC-88 3-plane) and 32-byte (2-plane) cell
 * decoding, tileset banks and the fman.grp hero frames.  Ports tools/palette.py,
 * tools/cellsheet.py and the "hero" branch of tools/grp2png.py. */
#ifndef ZEL_GFX_H
#define ZEL_GFX_H
#include <stdint.h>

extern uint8_t PAL_RGB[64][3];            /* VGA index (left*8+right) -> 8-bit RGB */
void gfx_pal_init(void);                  /* fill PAL_RGB (GAME.BIN @A41B); idempotent */
extern const uint8_t PAL2BPP[5][16];          /* gfmcga @4F98.. sprite colour tables */

typedef struct { uint8_t px[8][8]; } Cell8;   /* VGA indices, 0 = black/transparent */
typedef struct { uint8_t px[8][8]; uint8_t mask[8][8]; } Cell2;  /* 2bpp sprite: mask 1 = drawn */
/* the same cell before the palette is applied: idx = (left<<2|right) pair index */
typedef struct { uint8_t idx[8][8]; uint8_t mask[8][8]; } Cell2R;

void gfx_decode48(const uint8_t *src, Cell8 *out);
/* 2bpp cell with the gfmcga vec_20 outline mask (horizontal dilation). */
void gfx_decode32(const uint8_t *src, const uint8_t *pal16, Cell2 *out);
/* the same, palette-free (enemy frames pick their colour table per frame) */
void gfx_decode32_raw(const uint8_t *src, Cell2R *out);

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

/* enp1..8.grp: a plain cells32 bank (arena:4000), 32 bytes per cell, cell 0 blank.
 * `enp_index` is the level record's byte +4 (0 = ENP1 = cavern 1). */
#define ENEMY_CELLS 256
typedef struct { Cell2R cell[ENEMY_CELLS]; int ncells; } EnemyGfx;
int gfx_load_enemy_cells(EnemyGfx *e, const char *dir, int enp_index);

/* mole.bin (ZELRES2[7]): the static screen furniture.  GAME.BIN loads it to
 * (BASE+0x3000):0000 and far-calls it once at boot with the video mode in AL
 * (A178..A18D), and nothing ever redraws it — which is why neither fight.bin
 * nor town.bin owns any of it.  Four RLE'd two-plane pictures (mole.bin 0x0E,
 * 0x34, 0x55, 0x80, painted by the MCGA blitter at 0x24B): the strip above the
 * playfield (48,0) 224x13, the stone frame down the left (0,0) 48x200 and the
 * right (272,0) 48x200, and the grey HUD panel with its three white item-slot
 * frames (48,158) 224x42; then 0x38C ORs two 8x5 cyan marks on at (8,47) and
 * (304,47).  `on[]` marks the pixels it covers, so a blit can be redone at any
 * point in a frame without touching the playfield. */
#define SCREEN_FRAME_W 320
#define SCREEN_FRAME_H 200
typedef struct ScreenFrame {
    uint8_t px[SCREEN_FRAME_W * SCREEN_FRAME_H];
    uint8_t on[SCREEN_FRAME_W * SCREEN_FRAME_H];
    int loaded;
} ScreenFrame;
int gfx_load_screen_frame(ScreenFrame *f, const char *dir);

/* encnt.grp (ZELRES3[55]) — the "!" encounter card fight.bin flashes when a
 * boss room is entered (6078: the overlay is loaded raw to arena:4000, then
 * gfmcga's [301C] at 0x4518 lays out 28 x 5 eight-pixel cells at (48,40)).
 * Each cell is 16 bytes, one 16-bit row of eight 2-bit pixels; 0x4092 turns a
 * pixel into 0 (black), [4FF5] = 0x10 for values 1 and 2 and [4FF6] = 0x12 for
 * 3, so the card is two reds on black.  The 140-byte cell map is read out of
 * gfmcga.bin itself at 0x4588. */
#define ENCNT_W 224
#define ENCNT_H 40
typedef struct EncounterCard { uint8_t px[ENCNT_W * ENCNT_H]; int loaded; } EncounterCard;
int gfx_load_encounter(EncounterCard *c, const char *dir);

/* font.grp (ZELRES1[12]) section 2: the 6x7 HUD digit glyphs (docs/VIDEO_DRIVERS.md
 * [F502]).  glyph[d][row] = bits 5..0, left to right. */
typedef struct { uint8_t glyph[10][7]; int loaded; } DigitFont;
int gfx_load_digits(DigitFont *f, const char *dir);

#endif
