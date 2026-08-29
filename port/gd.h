/* gd.h — gdmcga (ZELRES1[5] @BASE:3000), the intro/ending art renderer.
 *
 * The `gd*` family is the second renderer in the game: the cutscene overlays
 * (opdemo, enddemo) draw through it and through nothing else.  Its pictures are
 * plain PC-8801 planar bitmaps — `nplanes` consecutive planes of `wbytes*rows`
 * bytes, MSB = leftmost pixel — and its palette is the 16x16 *blend* palette:
 * the packer turns every PC-88 pixel into a 4-bit value and every pair of
 * neighbours into one MCGA byte `left<<4 | right`, which is directly a DAC
 * index in `DAC[l*16+r] = C[l] + C[r]` over a 16-entry base record.
 * See docs/CUTSCENES.md §1, §3 and §6.
 *
 * Everything here is addressed exactly as gdmcga is: `BH` is x in 1/80ths of
 * the screen (4 px on MCGA), `BL` is y (0..199), `CH` is the width in *plane
 * bytes* (8 PC-88 px = 4 MCGA px) and `CL` is the row count.  So a picture of
 * `wbytes` plane bytes is `wbytes*4` screen bytes wide.
 *
 * The two unpackers (mask + lag-2 XOR delta, and the 6/14-bit RLE that only
 * ttl1-3.grp use) live in the demos, not in gdmcga, but they are here because
 * they belong to the same format; they are the C port of `tools/grp2png.py`'s
 * `gd_unpack_mask` / `gd_unpack_rle`. */
#ifndef ZEL_GD_H
#define ZEL_GD_H
#include <stddef.h>
#include <stdint.h>
#include "text.h"

#define GD_W 320
#define GD_H 200
#define GD_SCRATCH 0x10000              /* the 64 KB at (CS+0x2000) */

struct Gd;
/* the demos poll [FF1A] between dissolve passes and scroll steps; the port
 * hands that back to the caller so it can present a frame and read the keys.
 * Returns non-zero when the act was aborted (Space / Return). */
typedef int (*GdWaitFn)(void *user, int ticks);

typedef struct Gd {
    uint8_t *img;  size_t imglen;       /* gdmcga.bin, addressed from 0x3000 */
    uint8_t  base[10][16][3];           /* 4289: ten 16-entry 5-bit RGB records */
    uint8_t  dac[256][3];               /* the DAC as programmed, 8-bit RGB */
    int      pal_rec;
    uint8_t *fb;                        /* A000:0000, GD_W*GD_H, caller-owned */
    uint8_t *scratch;                   /* (CS+0x2000), GD_SCRATCH, caller-owned */
    uint8_t  textbuf[GD_W * 10];        /* 4511: gd_text_line's 320x10 buffer */
    uint8_t  orn[GD_W];                 /* 5191: gd_draw_ornament_row's buffer */
    const TextFont *font;
    GdWaitFn wait;                      /* NULL = never wait (tests) */
    void    *user;
    int      loaded;
} Gd;

/* --- the format -------------------------------------------------------- */
/* opdemo 6D5E / enddemo 696D: `u16 nmask` + nmask mask bytes + payload; each
 * mask bit (MSB first) emits one payload byte when set and a zero when clear,
 * then (with `delta`) the whole buffer is un-delta'd, every byte holding four
 * 2-bit fields XORed against the same field two pixels earlier.  Returns the
 * number of bytes written (nmask*8), clamped to `cap`. */
size_t gd_unpack_mask(const uint8_t *src, size_t len, uint8_t *dst, size_t cap, int delta);
/* opdemo 6DE1: bit 6 = 16-bit big-endian count word (0xFFFF ends), else a
 * 6-bit count; bit 7 = run (one byte repeated) vs literal. */
size_t gd_unpack_rle(const uint8_t *src, size_t len, uint8_t *dst, size_t cap);

/* --- the renderer ------------------------------------------------------ */
/* loads gdmcga.bin (ZELRES1[5]) and reads the palette base table out of it */
int  gd_init(Gd *g, const char *dir, uint8_t *fb, uint8_t *scratch, const TextFont *font);
void gd_free(Gd *g);
/* [3008] 4221: DAC[l*16+r] = base[rec][l] + base[rec][r], per component */
void gd_set_palette(Gd *g, int rec);
/* one 6-bit DAC component -> 8 bits, the way the VGA does it (docs/CUTSCENES.md §3) */
uint8_t gd_dac8(uint8_t v);
/* the whole framebuffer through the current DAC */
void gd_to_rgb(const Gd *g, const uint8_t *fb, uint8_t *rgb);

/* plane -> bit-weight maps, one per gdmcga entry point */
extern const uint8_t GD_W_P3[3];        /* [3004]/[3010] 1,2,4 */
extern const uint8_t GD_W_P2H[2];       /* [3002]        1,8   */
extern const uint8_t GD_W_P12[2];       /* [3016]        1,2   */
extern const uint8_t GD_W_P21[2];       /* [3014]        2,1   */

/* expand one row of a planar picture into `wbytes*4` screen bytes */
void gd_expand_row(const uint8_t *src, size_t planesz, int wbytes, int row,
                   const uint8_t *weights, int nplanes, uint8_t *out);

/* [3010] gd_draw_3plane_fast — a straight copy, no dissolve */
void gd_blit(Gd *g, const uint8_t *src, int x4, int y, int wbytes, int rows,
             const uint8_t *weights, int nplanes);
/* [3004] / [3002] — the same through the dissolve (3189 + 31B4).  `al` is the
 * driver's AL: 0 runs an additive OR dissolve first and then the copy one
 * (16 passes), anything else runs only the copy dissolve (8). */
int  gd_draw(Gd *g, const uint8_t *src, int x4, int y, int wbytes, int rows,
             const uint8_t *weights, int nplanes, int al);
/* [301A] gd_draw_2plane_ao: 8 (both) / 10 (plane 0) / 12 (plane 1) / 0, colour 0
 * transparent; AL is forced to 0, so it is always two dissolves */
int  gd_draw_ao(Gd *g, const uint8_t *src, int x4, int y, int wbytes, int rows);
/* [3022] gd_draw_masked: AL names which of the three planes are present, packed
 * consecutively, and the blit is *not* the ordinary dissolve — it is a private
 * 8-pass interlace over the fixed 288x104 picture window at screen (16,16),
 * which it clears everywhere the picture does not cover (3C1C / 3CFF). */
int  gd_draw_masked(Gd *g, int mask, const uint8_t *src, int x4, int y,
                    int wbytes, int rows);
/* [3026] gd_fx_recolour: rewrite a 47x88 three-plane picture in place (colour
 * 3 -> 0, colour 4 -> 3) and redraw it with gd_blit */
void gd_fx_recolour(Gd *g, uint8_t *pic, int x4, int y);
/* [3006] gd_erase: the same dissolve, writing black */
int  gd_erase(Gd *g, int x4, int y, int wbytes, int rows);

/* [3018] the title screen's night-sky checkerboard */
void gd_sky_dither(Gd *g);
/* [3024] the white picture-box outline */
void gd_picture_box(Gd *g, int x4, int y, int w4, int rows);
/* [302E] an 8x8 solid block (the ending's typewriter cursor) */
void gd_cursor_block(Gd *g, int colour, int x4, int y);
/* [3030] -> [2022] vid_putchar with [FF77] set: the ink byte is `c<<4 | c` */
void gd_putchar(Gd *g, uint8_t ch, int colour, int x, int y);
/* [2000] vid_window with [FF77] set: AL=0 clears, else a 0xFF frame */
void gd_window(Gd *g, int style, int x4, int y, int w4, int rows);
/* [202A] vid_puts with [FF77] set (initial colour 7) */
void gd_puts(Gd *g, const uint8_t *s, int x, int y);

/* [300A] zero the 64 KB scratch */
void gd_clear_scratch(Gd *g);
/* [300C] render one line of a 0x0D/0xFF-terminated block into the text buffer;
 * returns the pointer just past the terminator */
const uint8_t *gd_text_line(Gd *g, const uint8_t *s);
/* [300E] scroll the scratch up one row, copy text-buffer row `row` into scratch
 * row BL+CL, then composite: screen &= 0x99, screen |= src & 0x66 */
void gd_text_scroll(Gd *g, int row, int x4, int y, int w4, int rows);

/* [3014] / [3016] the demon's eye and mouth frame banks */
void gd_face_eyes(Gd *g, const uint8_t *bank, int frame, int x4, int y);
void gd_face_mouth(Gd *g, const uint8_t *bank, int frame, int x4, int y);

/* [301C] assemble the title screen's corner scrollwork out of ttl2.grp's
 * 40-column tile array through a 25 x 34 map, into the scratch */
void gd_tile_map(Gd *g, const uint8_t *map, const uint8_t *ttl2);
/* [301E] draw scratch row `row` and its bit-reversed mirror */
void gd_ornament_row(Gd *g, int row);

/* [3028] the 0x44-step horizontal aperture wipe of a full-screen picture */
int  gd_wipe(Gd *g, const uint8_t *src);
/* [302A] / [302C] the ending's iris open / close over a 47 x 114 picture */
int  gd_end_open(Gd *g, const uint8_t *src);
int  gd_end_close(Gd *g, const uint8_t *src);

/* [3012] the rain/lightning storm: `drops` is 9 x 6 bytes, `bolts` the unpacked
 * hou.grp at arena:9000 */
int  gd_storm(Gd *g, const uint8_t *drops, const uint8_t *bolts);
/* [3020] the built-in full-screen dither effect (0, 1 or 2) */
int  gd_fx_sand(Gd *g, int variant);


/* --- the resource table (docs/CUTSCENES.md §6.3) ------------------------ */
/* Geometry is what the demo passes to gdmcga; `unp` is which unpacker the demo
 * knows the resource needs (it is not self-describing) and `mode` the plane ->
 * bit-weight map of the entry point the demo calls. */
typedef struct GdArt {
    const char *name;
    uint8_t archive, res;          /* res = the 1-based resource number */
    uint8_t unp;                   /* 0 mask+delta, 1 RLE, 2 mask only, 3 raw */
    uint8_t pal;
    struct { unsigned off; uint8_t wbytes; uint16_t rows; uint8_t mode;
             uint8_t count; unsigned stride; } part[3];
    uint8_t nparts;
} GdArt;
enum { GDM_P3 = 0, GDM_P2H, GDM_P12, GDM_P21, GDM_P14, GDM_P1, GDM_SPR, GDM_AO };
extern const GdArt GD_ART[];
extern const int GD_ART_N;
const GdArt *gd_art_find(const char *name);
/* render one sub-image of a gd resource into `out` (wbytes*4 bytes a row) */
void gd_art_rows(const uint8_t *buf, unsigned off, int wbytes, int rows, int mode, uint8_t *out);

#endif
