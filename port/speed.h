/* speed.h -- F9 "Speed change / Select 0-9:" (issue #116; kernel svc_hotkey_speed,
 * STICK 07B6, polled at fight.bin 715F, town.bin 68C8, select.bin AA5D ...).
 *
 * The original: on F9 (FF18 bit 15) dialog_open (099C: sfx #2, the 1A46/1E28
 * box), the two lines of str_speed (0845) at (0x74, 0x52), wait for F9 to be
 * released, then read_digit (085E) loops on FF29 until a digit 0-9 is typed.
 * The digit is drawn at (0xCC, 0x5A) in colour 1, FF33 = 10 - digit (so 9 is the
 * fastest, FF33 = 1, and 0 the slowest, FF33 = 10), FF75 = 1, and the box stays
 * until any key, direction or fire press, when the screen is restored.  A frame
 * is 4 * FF33 ticks of the 236.7 Hz timer.  The original does not save FF33
 * (GAME.BIN reloads the default 5 on every start), and neither does the port.
 *
 * This module owns the dialog state and its drawing; SDL-free.  main.c feeds it
 * keys and applies `speed` to the real-time loop. */
#ifndef ZEL_SPEED_H
#define ZEL_SPEED_H
#include <stdint.h>
#include "shell.h"

enum { SP_CLOSED = 0, SP_DIGIT, SP_ANY };      /* read_digit / wait for any key */
enum { SPK_NONE = 0, SPK_DIGIT, SPK_ANY, SPK_CANCEL };

#define SPEED_DEFAULT 5                         /* the loader's FF33 */
#define SPEED_FRAME_MS(ff33) (16.9 * (ff33))    /* 4 * FF33 ticks of 236.7 Hz */

typedef struct SpeedDlg {
    int state;
    int speed;                  /* FF33, 1..10 */
    int digit;                  /* typed digit, valid in SP_ANY */
} SpeedDlg;

/* F9: dialog_open (sfx #2).  `ff33` is the current speed. */
void speed_open(SpeedDlg *s, int ff33);
/* one key: SPK_DIGIT + `digit` (read_digit), SPK_ANY (any key/direction/fire),
 * SPK_CANCEL (Esc; a port courtesy: read_digit has no way out).  Returns the
 * new FF33 when this call changed it, else 0. */
int  speed_key(SpeedDlg *s, int key, int digit);
void speed_draw(const SpeedDlg *s, const Shell *sh, uint8_t *fb);

#endif
