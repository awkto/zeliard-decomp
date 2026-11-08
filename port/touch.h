/* touch.h — on-screen touch controls drawn by the port itself (issue #54).
 *
 * Two halves.  The first is pure C with no SDL: the layout (where each control
 * sits for a given window size), the hit-test, the joystick's direction zones,
 * and the multi-finger state machine that binds every finger to the control it
 * started on.  test_touch.c exercises it without a window.  The second half
 * (HAVE_SDL) turns SDL_FINGER* events into calls on the first and draws the
 * overlay with SDL_Render primitives after the presented frame.
 *
 * The layout is what the wasm/Android builds taught the player: a movement
 * joystick bottom-left (left/right walk, up jumps, down kneels, the diagonals
 * jump-walk), Sword and Magic bottom-right with an auto-repeating combo button
 * (UP + DOWN + Sword pulsed, the DOSBox-X attack-keys behaviour), a top row of
 * 1..9 / Y / N for the game's prompts, and a second row with Esc (pause), Items
 * (Enter), quick save and quick load.  Everything is positioned relative to the
 * window, so it fits landscape and portrait.
 *
 * The result goes down the same paths keyboard and controller use: direction
 * bits, button bits, the held menu bit, and one-shot events (pause, quick
 * save/load, typed characters).  Nothing here touches the engine. */
#ifndef ZEL_TOUCH_H
#define ZEL_TOUCH_H
#include <stdint.h>

enum {                                   /* control ids; TC_NONE = nothing there */
    TC_NONE = -1,
    TC_STICK = 0, TC_SWORD, TC_MAGIC, TC_COMBO,
    TC_ITEMS, TC_ESC, TC_QSAVE, TC_QLOAD,
    TC_TURBO,                            /* held: fast-forward (host feature) */
    TC_KEY0,                             /* TC_KEY0 + i: i = 0..8 -> '1'..'9', 9 -> 'Y', 10 -> 'N' */
    TC_NKEYS = 11,
    TC_COUNT = TC_KEY0 + TC_NKEYS
};

enum { TSH_CIRCLE = 0, TSH_RECT };

typedef struct {
    int  shape;                          /* TSH_* */
    int  x, y, w, h;                     /* bounding box, window pixels */
    char label[8];
    int  over;                           /* overlaps the game picture (drawn fainter) */
} TouchCtl;

typedef struct {
    int win_w, win_h, portrait;
    int cx, cy, cw, ch;                  /* where the game picture is drawn */
    int overlay;                         /* 1: no free space, the controls sit over the picture */
    int unit;                            /* the size everything is a multiple of */
    int stick_cx, stick_cy, stick_r;     /* the joystick base; its hit zone is larger */
    TouchCtl c[TC_COUNT];
} TouchLayout;

#define TOUCH_SIZE_MIN 50                /* touch.size, percent */
#define TOUCH_SIZE_MAX 200
#define TOUCH_OPACITY_MIN 10             /* touch.opacity, percent */
#define TOUCH_OPACITY_MAX 100

/* Lay the controls out for a w x h window.  `size_pct` scales every control
 * (100 = default), clamped to the allowed range. */
void touch_layout(TouchLayout *L, int w, int h, int size_pct);
/* The same, knowing where the game picture is drawn (cx, cy, cw, ch; present.c's
 * content rect).  Controls go into the free space round the picture: in
 * portrait below it (digits and menu keys right under the picture, stick and
 * buttons at the bottom), in landscape into the pillarbox columns (digits and
 * menu keys above the stick / buttons) when those are wide enough.  When the
 * space is not there they overlay the picture and `overlay` is set. */
void touch_layout_in(TouchLayout *L, int w, int h, int size_pct, int cx, int cy, int cw, int ch);
/* A window pixel -> the fw x fh game frame under the picture; 0 when it is outside. */
int  touch_to_frame(const TouchLayout *L, int x, int y, int fw, int fh, int *fx, int *fy);

/* Which control is under window pixel (x, y)?  TC_NONE when none.  The stick's
 * zone reaches past its drawn ring so a thumb that starts slightly off still
 * grabs it. */
int  touch_hit(const TouchLayout *L, int x, int y);

/* The joystick's direction for a finger at offset (dx, dy) from the base
 * centre, radius `r`: DIR_* bits (physics.h).  Inside the dead zone nothing;
 * outside it eight 45-degree sectors, so the four diagonals set two bits
 * (up+right and up+left are the jump-walks). */
int  touch_stick_dirs(int dx, int dy, int r);

/* ---------------------------------------------------------------- state */
enum { TOUCH_AUTO = 0, TOUCH_ON, TOUCH_OFF };   /* --touch auto|on|off */
int  touch_mode_by_name(const char *s);         /* -1 unknown */

/* TEV_TAP: a finger came down on no control (x, y = window pixel) */
enum { TEV_NONE = 0, TEV_ESC, TEV_ITEMS, TEV_QSAVE, TEV_QLOAD, TEV_CHAR, TEV_TAP };
typedef struct { int kind; char ch; int x, y; } TouchEvent;

#define TOUCH_MAX_FINGERS 10
#define TOUCH_EVQ 16
typedef struct {
    int       active;
    long long id;
    int       ctl;                       /* the control it began on, for good */
    int       x, y;                      /* last position, window pixels */
} TouchFinger;

typedef struct {
    TouchLayout L;
    TouchFinger f[TOUCH_MAX_FINGERS];
    TouchEvent  ev[TOUCH_EVQ];
    int         nev;
    int         mode;                    /* TOUCH_AUTO / ON / OFF */
    int         visible;                 /* shown (and listening) right now */
    int         size, opacity;           /* percent */
    int         dialog;                  /* a dialog (F7 restore) owns the screen: relabelled buttons */
} Touch;

/* size_pct / opacity_pct < 0 take the defaults (100 / 50). */
void touch_init(Touch *t, int mode, int size_pct, int opacity_pct);
/* Re-lay out for a new window size (cheap when unchanged). */
void touch_resize(Touch *t, int w, int h);
int  touch_visible(const Touch *t);
/* Tell it the window size and where the game picture is (window pixels); re-lays out on a change. */
void touch_set_content(Touch *t, int w, int h, int cx, int cy, int cw, int ch);
/* Dialog mode: Esc/Items/Sword/Magic read CANCEL/OK/PICK/DEL. */
void touch_set_dialog(Touch *t, int on);

/* A finger touched down / moved / lifted at window pixel (x, y).  Ignored while
 * the overlay is hidden in `off`.  In `auto` the first touch shows it. */
void touch_finger_down(Touch *t, long long id, int x, int y);
void touch_finger_move(Touch *t, long long id, int x, int y);
void touch_finger_up(Touch *t, long long id);
/* Keyboard or controller used: in `auto` this hides the overlay and releases
 * every finger. */
void touch_note_other_input(Touch *t);

/* Polled, like the keyboard: DIR_* bits, the sword/magic bits (1 = sword,
 * 2 = magic, as the engine's button byte), and whether Items is held.  The
 * combo button holds UP and DOWN and pulses sword at ~10 Hz; `ms` is any
 * monotonic millisecond clock. */
uint8_t touch_dirs(const Touch *t);
uint8_t touch_buttons(const Touch *t, unsigned ms);
int     touch_menu_held(const Touch *t);
int     touch_turbo_held(const Touch *t);            /* the TURBO button is down */
int     touch_ctl_down(const Touch *t, int ctl);     /* is a finger on it? */
/* The joystick knob position (window pixels), clamped to the ring; the base
 * centre when no finger holds the stick. */
void    touch_knob(const Touch *t, int *x, int *y);

/* Pop the next one-shot event; 0 when none. */
int  touch_next_event(Touch *t, TouchEvent *e);

#ifdef HAVE_SDL
#include <SDL.h>
/* Feed an SDL event; `out_w`/`out_h` is the renderer's output size.  Returns 1
 * when it was a finger event. */
int  touch_handle_sdl(Touch *t, const SDL_Event *e, int out_w, int out_h);
/* Draw the overlay (semi-transparent, over whatever is already rendered). */
void touch_draw(Touch *t, SDL_Renderer *ren);
#endif

#endif
