/* text.h — the fonts and the drawing primitives the town and the shops use.
 *
 * The five video drivers all expose the same 35 slots (docs/VIDEO_DRIVERS.md);
 * the port implements the MCGA ones the shop overlays call:
 *   [2000] vid_window        [2022] vid_putchar     [202A] vid_puts
 *   [2010] vid_label_text    [2038] vid_label_asciiz
 *   [2030] vid_draw_digits   [2032] vid_to_decimal
 * plus gtmcga's [3016] GT_DRAW_CELL, [301A] GT_MENU_LINE, [301C] GT_MENU_BLIT
 * and [3018] GT_CURSOR.
 *
 * font.grp (ZELRES1[12]) is a 3-section container: section 0 = the 8x8 text
 * glyphs from char 0x20 ([F500]), section 1 = the 6x7 digits ([F502]), section
 * 2 = the 4-px-wide "narrow" label glyphs ([F504]).  The *proportional*
 * metrics live in town.bin (font_xoff 7B82, font_advance 7BE2) and are the
 * tables below. */
#ifndef ZEL_TEXT_H
#define ZEL_TEXT_H
#include <stdint.h>
#include "gfx.h"

#define TEXT_W 320
#define TEXT_H 200

/* PC-88 colour number -> MCGA blend-DAC index (video_mcga 24EA) */
extern const uint8_t PC88[8];

typedef struct TextFont {
    uint8_t glyph[96][8];      /* [F500] 8x8, chars 0x20..0x7F, 1 bit per pixel */
    uint8_t narrow[96][8];     /* [F504] top 4 bits of each row */
    uint8_t digit[10][7];      /* [F502] 6x7, bits 5..0 */
    int     loaded;
} TextFont;

int  text_load_font(TextFont *f, const char *dir);       /* font.grp = ZELRES1[12] */

/* town.bin 7B82 / 7BE2: proportional metrics for chars 0x20..0x7F */
extern const uint8_t FONT_XOFF[96];
extern const uint8_t FONT_ADVANCE[96];

/* [2000] vid_window: AL = 0 clears, AL != 0 draws the 2-px rounded frame.
 * BH is x in 1/80ths of the screen (4 px), BL the 0..199 row, CH the width in
 * the same 1/80ths, CL the height in rows. */
void vid_window(uint8_t *fb, int style, int x4, int y, int w4, int h);
/* [2022] one 8x8 glyph, set bits only (transparent), colour = PC-88 number */
void vid_putchar(uint8_t *fb, const TextFont *f, uint8_t ch, int colour, int x, int y);
/* [202A] a string: 0x0D = newline (+8 rows), >= 0x80 sets colour (b & 7), 0xFF ends */
void vid_puts(uint8_t *fb, const TextFont *f, const uint8_t *s, int x, int y);
/* [2038] narrow-font ASCIIZ label, white, no shadow, 5-px pitch */
void vid_label_asciiz(uint8_t *fb, const TextFont *f, const char *s, int x4, int y, int xoff);
/* the same with a shadow: [200E] (green on red) and [2010] (white on blue) */
void vid_label_narrow(uint8_t *fb, const TextFont *f, const char *s, int len, int x4, int y,
                      int xoff, int fg, int bg);
/* [2010] a positioned label record {u8 x4, u8 y, u8 xoff_px, u8 len, chars} */
void vid_label_text(uint8_t *fb, const TextFont *f, const uint8_t *rec);
/* [2030] 6x7 digits on a 6-px pitch; `box` fills a dark-blue background */
void vid_draw_digits(uint8_t *fb, const TextFont *f, unsigned value, int n, int x, int y, int box);
/* [2030] verbatim (video_mcga 24A3): `x4`/`y` are the driver's units, `nudge`
 * adds 2 px, `colour` is a PC-88 number and leading zeros are NOT suppressed —
 * select.bin's draw_number (A9B3) feeds it the last `n` of 7 raw digits. */
void vid_draw_digits_raw(uint8_t *fb, const TextFont *f, unsigned value, int n,
                         int x4, int y, int colour, int nudge, int box);
/* [2032] vid_to_decimal / town.bin 72C7 format_number: no leading zeros */
int  format_number(uint32_t v, char *out);
/* gtmcga [3016] GT_DRAW_CELL: one 48-byte cell of the shop portrait bank */
void gt_draw_cell(uint8_t *fb, const Cell8 *bank, const uint8_t *present, int cell, int x8, int y);
/* gtmcga [3018] GT_CURSOR: the 9-row red menu arrow */
void gt_cursor(uint8_t *fb, int x4, int y, int on);

#endif
