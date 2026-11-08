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
void        video_size(int mode, int *w, int *h);         /* the driver's framebuffer */
/* The aspect-corrected presentation size: the shape this driver's picture had
 * on the 4:3 monitor it was designed for.  The framebuffer is unchanged --
 * this is presentation only, and `make verify` never goes through it. */
void        video_display_size(int mode, int *w, int *h);

/* Convert the 320x200 pair buffer to this mode's screen.  `rgb` holds w*h*3. */
void video_to_rgb(int mode, const uint8_t *fb, uint8_t *rgb);

/* the per-driver stages (one file each) */
void video_mcga_to_rgb (const uint8_t *fb, uint8_t *rgb);              /* 320x200 */
void video_cga_to_rgb  (const uint8_t *fb, uint8_t *rgb);              /* 320x200, mode 5 */
void video_cga2_to_rgb (const uint8_t *fb, uint8_t *rgb);              /* 640x200, mode 6 */
void video_ega_to_rgb  (const uint8_t *fb, uint8_t *rgb);              /* 640x200 */
void video_hgc_to_rgb  (const uint8_t *fb, uint8_t *rgb);              /* 720x348 */
void video_tandy_to_rgb(const uint8_t *fb, uint8_t *rgb);              /* 320x200 */

/* the stages from a screen of the driver's own colour values (0..3 CGA / 0..15
 * Tandy), which is what the gd*.bin cutscene renderer produces */
void video_cga_idx_to_rgb  (const uint8_t *idx, uint8_t *rgb);
void video_cga2_idx_to_rgb (const uint8_t *idx, uint8_t *rgb);
void video_hgc_idx_to_rgb  (const uint8_t *idx, uint8_t *rgb);
void video_tandy_idx_to_rgb(const uint8_t *idx, uint8_t *rgb);
void video_ega_regs_to_rgb (const uint8_t *fb, const uint8_t regs[16], uint8_t *rgb);

/* the tables the drivers carry, exported so the tests can check them against
 * the binaries in zeliard/ */
extern const uint8_t VID_CGA_PAIR[64];      /* GMCGA.BIN @290B / GMHGC.BIN @2994 */
extern const uint8_t VID_TGA_PAIR[64];      /* GMTGA.BIN @2999 */
extern const uint8_t VID_EGA_PALETTE[17];   /* GAME.BIN @A409 (INT 10h AX=1002h) */

/* The town is drawn by gt*.bin, a renderer per mode, and what it colours it
 * colours with tables of its *own*, not the ones `[0x2044]` uses on the cavern's
 * cells (docs/VIDEO_DRIVERS.md section 3):
 *   - the tile and sprite cells go through a 64-entry pair table in the
 *     overlay (gtcga @3D0B, gthgc @3C24, gttga @3DCB; EGA has none) that is
 *     not the pair table of GMCGA/GMHGC (three entries differ on CGA/HGC);
 *   - the backdrop painters ympd.bin / ckpd.bin never see a pair at all: they
 *     read the art's two bit planes and colour each 4-bit (left, right) index
 *     through yet another 16-entry table per mode.
 * Neither is a function of the MCGA pair alone, so the town's painters tag the
 * pixels they write in bits 6 and 7, leaving the pair in bits 0..5 (and the
 * 4-bit index recoverable from it once the row says which painter wrote it).
 * MCGA and EGA mask the tag away; CGA / CGA2 / HGC / Tandy ask here.
 * Returns 1 and sets *out (CGA colour 0..3, or Tandy colour 0..15) for a
 * tagged pixel on game row `y` in `mode`, 0 for an untagged one. */
#define FB_TAG    0xC0
#define FB_GT     0x40      /* a gt*.bin tile or sprite pixel */
#define FB_BD_YM  0x80      /* ympd.bin's backdrop */
#define FB_BD_CK  0xC0      /* ckpd.bin's backdrop */
int video_town_pixel(int mode, int y, uint8_t v, uint8_t *out);
extern const uint8_t VID_GT_CGA[64];        /* gtcga.bin @3D0B == gthgc.bin @3C24 */
extern const uint8_t VID_GT_TGA[64];        /* gttga.bin @3DCB */
/* the backdrop painters' 16-entry tables (tests read them back out of the overlays) */
extern const uint8_t VID_YM_MTN_C[16], VID_YM_MTN_T[16], VID_YM_GND_C[16], VID_YM_GND_T0[16], VID_YM_GND_T1[16];
extern const uint8_t VID_CK_MTN_C[16], VID_CK_MTN_T[16], VID_CK_GND_C[16], VID_CK_GND_T[16];

/* The screen dissolve, [2040] (gmmcga 2130, issue #118): each driver has its own.
 * MCGA zeroes a pixel where the rotating mask bit is set; CGA / HGC (2124 / 2143)
 * AND the 2-bit colours of every byte with a row mask (FE EE EA AA A8 88 80 00),
 * Tandy (2124) AND the 4-bit colours with a 16-bit mask (FFFC .. C000 0000), EGA
 * (214E) writes colour 0 through the bit-mask register on the EGA's half-pixels
 * (01 11 15 55 57 77 7F FF).  The pair buffer holds a *pair*, so for the modes
 * whose result is a colour of its own the pass converts the pixel to the driver's
 * colour first and stores a pair that converts back to the masked colour (every
 * colour of both tables has a pair).  Pass n (0..7) is done in place; passes
 * are cumulative and idempotent, like the originals' masks (each is a subset of
 * the one before).  MCGA's is render_dissolve_pass(). */
void video_dissolve_pass(int mode, uint8_t *fb, int pass);
extern const uint8_t VID_DSV_CGA[8];        /* GMCGA.BIN @217F == GMHGC.BIN @21D6 */
extern const uint16_t VID_DSV_TGA[8];       /* GMTGA.BIN @21A7 (words) */
extern const uint8_t VID_DSV_EGA[8];        /* GMEGA.BIN @21C0 */
/* the video mode the engine is rendering in, for the renderers that have no
 * access to the App (render_dissolve_pass picks the driver's pass from it) */
void video_set_current(int mode);
int  video_current(void);

/* GMHGC.BIN @2E11: game row -> Hercules scan line (3 rows over 4 lines, +37) */
int video_hgc_line(int y);

#endif
