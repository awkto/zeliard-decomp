/* autoatk.h — the attack shortcut keys' pulse state machine (combo / auto-fire).
 *
 * SDL-free so it can be unit-tested (test_config.c).  The game reads the sword
 * as an EDGE: set_buttons() latches btn1_edge on a 0->1 transition of INT 61h
 * bit 0, and fight.bin consumes it (6E81).  A held sword is therefore ONE swing,
 * so auto-fire has to release it between presses: sword high on one game frame,
 * low on the next, period AUTOATK_PERIOD frames.
 *
 * Why 1 on / 1 off (measured on the original, tests/scenarios/pulse_{sword,up,combo}.zs,
 * 1 on / 1, 2 and 3 off, all clean against the port): a swing is 3 boundary frames for
 * a slash and 2 for an upward slash (the combo's, Up held, 6ED6), because 3F1A ends it
 * inside the frame after the last visible one, and sword_input (6E81, before the
 * steps) still sees `attacking` set there.  A press during a swing only latches the
 * edge (FF1D, kept until 6EF7 consumes it) and never shortens the swing; the next swing
 * starts on the very next frame.  So the game's own cadence is 4 frames a slash and
 * 3 an upward slash, i.e. at the default speed (84 ms a frame) about 2.9 and 3.9
 * swings a second, and a press every other frame is already always pending when a swing
 * ends: a slower pulse (1 on / 2 or 3 off) gives the identical pattern, a faster one
 * does not exist.  Pressing "when `attacking` ends" would start the same frame.
 *
 *   combo   (A held, or the touch Combo button): UP+DOWN held, sword pulsing.
 *           Left/Right still steer.  Always pulses, even with Down held (the
 *           emulated build's combo does the same).
 *   autofire (S held): sword pulsing -- except while Down is held, where the
 *           sword is held steady instead: the downward piercing thrust (6E45)
 *           reads the LEVEL of the button, and a pulse would break it.
 *
 * It runs once per game frame, in the interactive cavern/town present only.
 * Headless, --trace and --script runs never call it: their inputs come from the
 * script tokens, so determinism is untouched. */
#ifndef ZEL_AUTOATK_H
#define ZEL_AUTOATK_H
#include <stdint.h>

#define AUTOATK_PERIOD 2                 /* frames per pulse: 1 high, 1 low */

typedef struct AutoAtk {
    unsigned phase;                      /* frames since the shortcut was pressed */
    int      active;                     /* was a shortcut held last frame? */
} AutoAtk;

/* Advance one game frame.  `dirs`/`btns` are what the player's own keys, pad
 * and touch produced (DIR_* bits, bit 0 sword); the result is written back into
 * them.  The player's own held sword stays held (level), as it always did. */
void autoatk_step(AutoAtk *s, int combo, int autofire, uint8_t *dirs, uint8_t *btns);
/* Forget the phase (pause, a dialog, a scene change). */
void autoatk_reset(AutoAtk *s);

#endif
