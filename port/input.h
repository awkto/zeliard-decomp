/* input.h — the port's key/controller remapping (issue #42, step 2).
 *
 * The player's `key.<action>` / `pad.<action>` lines in zeliard.ini are kept as
 * text by config.c (which stays SDL-free); this module resolves them into SDL
 * scancodes and controller buttons and answers the two questions the shell
 * asks: "is this action held?" (polled, for the movement and the two buttons)
 * and "did this key event match?" (for the one-shot hotkeys).  The defaults are
 * exactly the keys and buttons the port had hard-coded before, so an ini with
 * no bindings behaves identically.  Unknown action names, and unknown key or
 * button names, are ignored with a warning on stderr.
 *
 * SDL-only: this file is linked into `zeliard` alone, never into the engine
 * test binaries, and `main.c` uses it only under HAVE_SDL. */
#ifndef ZEL_INPUT_H
#define ZEL_INPUT_H
#include "config.h"
#ifdef HAVE_SDL
#include <SDL.h>

enum {
    ACT_LEFT, ACT_RIGHT, ACT_UP, ACT_DOWN,      /* the d-pad / movement */
    ACT_SWORD, ACT_MAGIC,                        /* cavern sword/magic; town talk/cancel */
    ACT_MENU,                                    /* Enter: the status screen */
    ACT_PAUSE,                                   /* Esc: pause */
    ACT_FILTER, ACT_FULLSCREEN, ACT_SCREENSHOT,  /* F3 / F11 / F12 */
    ACT_MUSIC, ACT_SFX,                          /* F1 / F2 */
    ACT_QUICKSAVE, ACT_QUICKLOAD,                /* Ctrl+. / Ctrl+\ (#52) */
    ACT_RESTORE,                                 /* F7: the original's Restore Game (#42) */
    ACT_SPEED,                                   /* F9: the original's Speed change (#116) */
    ACT_TURBO,                                   /* Shift (held): fast-forward, presentation only */
    ACT_MAP,                                     /* M: the cavern map viewer (mapview.h) */
    ACT_SAVE1,                                   /* Ctrl+1 .. Ctrl+9 */
    ACT_LOAD1 = ACT_SAVE1 + 9,                   /* Alt+1 .. Alt+9 */
    ACT_COUNT = ACT_LOAD1 + 9
};

#define INPUT_KEYS_MAX 6

typedef struct KeyBind {
    int          n;
    SDL_Scancode sc[INPUT_KEYS_MAX];
    SDL_Keymod   mod[INPUT_KEYS_MAX];            /* required modifier, 0 = none */
} KeyBind;

typedef struct PadBind {
    int n;
    SDL_GameControllerButton btn[INPUT_KEYS_MAX];
} PadBind;

typedef struct Input {
    KeyBind cav[ACT_COUNT];                      /* cavern defaults/overrides */
    KeyBind town[ACT_COUNT];                     /* town defaults/overrides */
    PadBind pad[ACT_COUNT];
} Input;

/* Fill `in` with the built-in defaults, then apply the config's `key.*` and
 * `pad.*` overrides (each replaces the whole action).  Returns the number of
 * overrides applied; unknown actions/names warn on stderr and are skipped. */
int input_init(Input *in, const Config *cfg);

/* Polled: is `act`'s key held?  `town` selects the town or cavern default set.
 * While `combo` is set (a Ctrl/Alt save-load key just fired) a bound modifier
 * key (Ctrl/Alt/Shift/Gui) does not count, so the same Ctrl/Alt does not also
 * swing the sword or cast the spell. */
int input_key_down(const Input *in, int act, int town, const uint8_t *keystate, int combo);
/* Polled: is `act`'s controller button held? */
int input_pad_down(const Input *in, int act, SDL_GameController *pad);
/* Event: does this controller BUTTONDOWN match `act`? */
int input_pad_event_match(const Input *in, int act, int button);
/* Event: does this KEYDOWN match `act` (scancode and required modifier)? */
int input_event_match(const Input *in, int act, const SDL_KeyboardEvent *k);
/* The action's display name ("sword", "save3", …), for messages. */
const char *input_action_name(int act);

#endif /* HAVE_SDL */
#endif /* ZEL_INPUT_H */
