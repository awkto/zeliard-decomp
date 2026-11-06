/* restore.h -- the in-game F7 "Restore Game" (issue #42, step 2b).
 *
 * The original has two screens and one load:
 *   1. the kernel's [11E] (STICK 092D) puts up "Restore Game / Sure?(Y/N)" over
 *      the frozen frame; N takes it down and the game goes on;
 *   2. Y runs town.bin 7592 restore_game, whose 7695 choose_save_file draws the
 *      "Input name:" window: an 8-slot name box with a caret and a list of the
 *      *.usr files in the game folder, "Re-Start" first.  Up/Down move the
 *      list, Space copies the highlighted name into the box, letters /
 *      Backspace / Left / Right edit it, Enter accepts it (an empty box turns
 *      into "Re-Start" first);
 *   3. "Re-Start" reloads STDPLY.BIN, any other name NAME.USR; a file that is
 *      not there shows "User File / Not Found" (7667), waits for Space and
 *      asks for a name again; otherwise GAME.BIN restarts in "restored" mode
 *      and the town the page names is entered at the saved column.
 * Both screens are honoured wherever the frame loops poll [11E]: town.bin's
 * 68DC (every town frame) and 704E (the shops' wait loops), fight.bin 716E
 * (every cavern frame).  A restore from a cavern leaves it for the town.
 *
 * This module owns the dialog state and its drawing (into the engine's 320x200
 * frame, with the same window / text primitives the shops use) and the commit.
 * It is SDL-free; main.c feeds it keys and runs it as a modal loop over the
 * frozen frame. */
#ifndef ZEL_RESTORE_H
#define ZEL_RESTORE_H
#include <stdint.h>
#include "shell.h"

enum { RS_CLOSED = 0, RS_CONFIRM, RS_PICK, RS_NOTFOUND };
enum { RK_NONE = 0, RK_YES, RK_NO, RK_UP, RK_DOWN, RK_LEFT, RK_RIGHT,
       RK_TAKE,        /* Space / sword: copy the highlighted name into the box */
       RK_ACCEPT,      /* Enter */
       RK_BACKSPACE,
       RK_CANCEL,      /* port courtesy: the original's name box has no way out but Enter */
       RK_CHAR };      /* + `ch` (upper-case A-Z, 0-9) */

#define RESTORE_MAX 63              /* *.usr names listed (town.bin lists up to 254) */

typedef struct Restore {
    int      state;
    int      apply;                 /* a name was accepted: restore_commit() is due */
    char     files[RESTORE_MAX + 1][12];    /* [0] = "Re-Start" */
    int      nfiles, sel, top;      /* highlighted row, first visible row */
    uint8_t  name[9];               /* the 8 name slots, '`' (0x60) = empty, then 0xFF */
    int      cur, used;             /* caret column, name_len_used */
    int      restart;               /* the name holds a '-': "Re-Start" (restart_chosen) */
    char     pick[16];              /* accepted name, upper case; "" = Re-Start */
} Restore;

/* F7: open the confirm box (kernel dialog_open: sfx #2, music paused) */
void restore_open(Restore *r);
/* one key to the dialog.  RS_CLOSED afterwards = cancelled or accepted (`apply`). */
void restore_key(Restore *r, Shell *sh, int key, char ch);
/* touch: the file row (0..4) under a point of the 320x200 frame, or -1; a tap
 * selects/fills a row, a second tap on it accepts, any tap clears "Not Found" */
int  restore_row_at(const Restore *r, int fx, int fy);
void restore_tap(Restore *r, Shell *sh, int fx, int fy);
/* draw the dialog over `fb` (the frozen frame the engine last drew) */
void restore_draw(const Restore *r, const Shell *sh, uint8_t *fb);
/* the accepted name -> the engine: loads STDPLY.BIN / NAME.USR over the player
 * page and enters the town it names at the saved column.  0 on success. */
int  restore_commit(Restore *r, Shell *sh);

#endif
