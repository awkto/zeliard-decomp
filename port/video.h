/* video.h — the five original video drivers as output stages (issue #32).
 *
 * The engine always renders into one 320x200 byte-per-pixel buffer whose values
 * are the **PC-88 colour pair** `left*8 + right` (0..63) — that is literally what
 * the MCGA driver puts in A000, because GAME.BIN @A41B builds the DAC as
 * `DAC[l*8+r] = BASE[l] + BASE[r]` (docs/VIDEO_DRIVERS.md §2.1).  Every other
 * driver starts from exactly the same pair: `[0x2044]` reads the same 48-byte
 * PC-88 cells and only differs in what it packs the pair into —
 *
 *   MCGA   the pair itself, 6 bits, 8 px per cell of 320   (48 B/cell)
 *   CGA    64-entry table @290B -> 2 bpp,  8 px of 320     (16 B/cell)
 *   cga2   the same 2-bit value shown as two mode-6 mono px, 16 px of 640
 *   EGA    nothing at all (`ret`): the two halves of the pair stay two
 *          separate 3-bit pixels, 16 px of 640                (48 B/cell)
 *   HGC    64-entry table @2994 (byte-identical to CGA's) -> 2 bits = two mono
 *          px, 16 px of 640, 3 game rows over 4 Hercules lines (16 B/cell)
 *   Tandy  64-entry table @2999 -> 4 bpp, 8 px of 320       (32 B/cell)
 *
 * so a mode is a pure function of the pair buffer and needs no second copy of
 * the engine.  Each `video_*.c` is one driver's colour tables and screen
 * geometry, taken from its own binary (the tables here were re-read out of
 * GM{CGA,HGC,TGA}.BIN and GAME.BIN, not copied from the doc).
 */
#ifndef ZEL_VIDEO_H
#define ZEL_VIDEO_H
#include <stdint.h>

enum { VID_MCGA = 0, VID_CGA, VID_CGA2, VID_EGA, VID_HGC, VID_TANDY, VID_COUNT };

/* the largest screen any mode produces (Hercules 720x348) */
#define VIDEO_MAX_W 720
#define VIDEO_MAX_H 348

/* RESOURCE.CFG `videoDrv` spellings: mcga cga cga2 ega hgc tga/tandy.  -1 = unknown. */
int         video_mode_by_name(const char *name);
const char *video_mode_name(int mode);              /* the --video spelling */
const char *video_mode_cfg(int mode);               /* the RESOURCE.CFG spelling */
void        video_size(int mode, int *w, int *h);

/* Convert the 320x200 pair buffer to this mode's screen.  `rgb` holds w*h*3. */
void video_to_rgb(int mode, const uint8_t *fb, uint8_t *rgb);

/* the per-driver stages (one file each) */
void video_mcga_to_rgb (const uint8_t *fb, uint8_t *rgb);              /* 320x200 */
void video_cga_to_rgb  (const uint8_t *fb, uint8_t *rgb);              /* 320x200, mode 5 */
void video_cga2_to_rgb (const uint8_t *fb, uint8_t *rgb);              /* 640x200, mode 6 */
void video_ega_to_rgb  (const uint8_t *fb, uint8_t *rgb);              /* 640x200 */
void video_hgc_to_rgb  (const uint8_t *fb, uint8_t *rgb);              /* 720x348 */
void video_tandy_to_rgb(const uint8_t *fb, uint8_t *rgb);              /* 320x200 */

/* the tables the drivers carry, exported so the tests can check them against
 * the binaries in zeliard/ */
extern const uint8_t VID_CGA_PAIR[64];      /* GMCGA.BIN @290B / GMHGC.BIN @2994 */
extern const uint8_t VID_TGA_PAIR[64];      /* GMTGA.BIN @2999 */
extern const uint8_t VID_EGA_PALETTE[17];   /* GAME.BIN @A409 (INT 10h AX=1002h) */

/* GMHGC.BIN @2E11: game row -> Hercules scan line (3 rows over 4 lines, +37) */
int video_hgc_line(int y);

#endif
