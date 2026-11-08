/* main.c — SDL2 shell (or headless PNG dumper) for the Zeliard port scaffold. */
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include "sar.h"
#include "gfx.h"
#include "map.h"
#include "physics.h"
#include "render.h"
#include "video.h"
#include "png.h"
#include "enemy.h"
#include "boss.h"
#include "town.h"
#include "text.h"
#include "player.h"
#include "shop.h"
#include "status.h"
#include "shell.h"
#include "audio.h"
#include "cutscene.h"
#include "tear.h"
#include "trace.h"
#include "savestate.h"
#include "statekeys.h"
#include "playthrough.h"
#include "present.h"
#include "restore.h"
#include "speed.h"
#include "config.h"
#include "sync.h"
#include "synclife.h"
#include "syncdlg.h"
#ifdef HAVE_SDL
#include "input.h"
#endif
#include "touch.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <errno.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(d) _mkdir(d)
#else
#include <sys/stat.h>
#define MKDIR(d) mkdir((d), 0777)
#endif
#ifdef HAVE_SDL
#include <SDL.h>
#endif

#define FRAME_MS_DEFAULT 84.5           /* 20 ticks of 236.7 Hz at speed 5 */
#define TRACE_IDLE_MAX 2000             /* --trace: rendered frames allowed between two boundaries */

typedef struct {
    Shell    sh;                        /* the two-engine shell (shell.c) */
    uint8_t  fb[FB_W * FB_H];
    int      headless;
    unsigned shot_frame;
    unsigned max_frames;                /* --frames N: stop after N rendered frames */
    const char *shot_path;
    const char *script;                 /* headless key script */
    int      script_pos, script_left;
    uint8_t  script_dirs, script_btns;
    uint8_t  script_char;               /* 'C token: the key's ASCII, written to FF29 once */
    int      script_char_new;           /*   set when the token starts, cleared when applied */
    uint8_t  trace_char;                /* the FF29 key the current trace frame was played with */
    /* F7 "Restore Game" (#42 step 2b): restore.c owns the dialog; `restore_req`
     * (the key, or the F token) opens it at the next frame's present, as a
     * modal loop over the frozen frame; an accepted name is committed at the
     * true end of the frame (state_frame_done), like a loaded save state */
    Restore  rest;
    int      restore_req;
#define REST_Q_TAP 0x20000000           /* rest_q entry: a touch tap, (fy << 10) | fx in the 320x200 frame */
    int      touch_prev_dirs, touch_prev_btn;   /* #54: edge detection for the dialogs */
    int      rest_q[16], rest_qn;       /* SDL keys the dialog has not consumed yet */
    int      sync_dlg;                  /* the ask-before-link dialog (#55) owns the keyboard */
    double   frame_ms;                  /* SPEED_FRAME_MS(speed): follows FF33 live (F9, --speed) */
    int      speed;                     /* FF33: 1 fastest .. 10 slowest, loader default 5 */
    SpeedDlg spd;                       /* F9 "Speed change" (#116) */
    int      speed_req;
    int      scale;
    int      square;                /* --aspect 1:1: present the framebuffer unscaled in shape */
    int      filter;                /* --filter: FILTER_INTEGER / SHARP / SMOOTH / CRT (#53) */
    int      quit;
    int      verbose;
    int      dump_audio;
    /* the cutscenes (opdemo / enddemo).  `intro` = -1 auto (a plain interactive
     * launch), 0 never, 1 always; `intro_act` runs one act on its own. */
    int      intro, intro_act, ending, intro_only;
    int      video;                     /* --video: which of the five drivers renders */
    unsigned cs_frames;
    Cutscene cs;
    /* set by --fullscreen in the arg parser, which is compiled either way;
     * only the SDL window path reads it (a headless build has no window) */
    int      fullscreen;
    /* --trace DIR: the differential harness (docs/HARNESS.md, trace.c).  One
     * frames.jsonl line per frame boundary; the script advances only there. */
    const char *trace_dir;
    FILE    *trace_f;
    unsigned trace_n;                   /* the next trace frame number */
    unsigned walk_n;                    /* walk-in pictures written so far (#117) */
    unsigned dsv_n;                     /* dissolve pictures written so far (#118) */
    unsigned side_n;                    /* side pictures written so far (#119) */
    uint8_t  side_snap[FB_W * FB_H];    /* the screen SIDE_SNAP kept */
    unsigned png_every;                 /* --png-every K */
    unsigned rng_mark;                  /* g->rng_calls at the previous boundary */
    int      trace_done;                /* script exhausted: the run is stopping */
    unsigned trace_idle;                /* presents since the last boundary (shops, status) */
    /* --state-save-at N FILE / --state-load FILE: realtime save states (#52).
     * The save fires once, at trace frame boundary N, once that frame's
     * post-boundary tail has run (shell.frame_done); the load re-enters the
     * saved map/town and continues the trace numbering at N+1. */
    const char *state_save;             /* --state-save-at FILE (NULL = off) */
    unsigned  state_save_at;            /* --state-save-at N */
    int       state_save_done;
    int       state_save_pending;       /* boundary N seen; save when its frame ends */
    unsigned  state_save_rng;           /* g->rng_calls at boundary N (the trace delta base) */
    const char *state_load;             /* --state-load FILE (NULL = off) */
    /* #52 step 2: the player-facing save states.  `sk` owns the request queue,
     * the slot file names and the OSD text (port/statekeys.c); a key press or an
     * injected event queues a request and state_frame_done runs it at the true
     * end of the frame, which is the only resumable point. */
    StateKeys sk;
    const char *state_dir_opt;          /* --state-dir DIR (tests, headless) */
    int       sk_combo;                 /* a Ctrl/Alt save-load key fired: don't also attack/cast */
    /* --state-inject save:N[:slot] / load:N[:slot]: queue a request at trace
     * boundary N -- the headless equivalent of pressing the key, for the proof */
    struct { int kind; unsigned frame; int slot; } st_ev[8];
    int       n_st_ev;
    /* --pilot ROUTE: the autopilot (playthrough.c) answers the input at each
     * boundary instead of --script; --emit-script FILE writes what it fed,
     * run-length coded, as a --script that replays it exactly */
    Play    *pilot;
    int      pilot_state;               /* 0 flying, 1 route done (tail), -1 failed */
    unsigned pilot_tail;                /* --pilot-tail N: idle frames after the route */
    FILE    *emit_f;
    char     emit_tok[16];
    unsigned emit_n, emit_count;
#ifdef HAVE_SDL
    SDL_Window *win; SDL_Renderer *ren; SDL_Texture *tex;
    SDL_GameController *pad;            /* controller 0, opened on the fly */
    Input    in;                        /* #42: the key/button bindings, one table */
    Touch    touch;                     /* #54: the on-screen controls */
    int      paused;                    /* Esc: the original pauses, it does not quit */
    int      bg_paused;                 /* Android: paused by the app going to the background, so coming back resumes */
    int      bg_flush;                  /* Android: finish the frame so the queued background save runs, before SDL blocks us */
    int      esc_edge;                  /* Esc was pressed; what that means is the caller's */
    char     title_base[900];           /* the engine's own title, re-applied when pause flips */
#endif
    int yield_test;                     /* web only: --yield-test yields to the browser at every present, even headless (ASYNCIFY coverage under node) */
} App;

/* --yield-test (web only): yield to the browser at every present, even headless,
 * so tools/web/run_scenarios.sh exercises the ASYNCIFY paths under node */
#ifdef __EMSCRIPTEN__
#define YIELD_TEST(a) do { if ((a)->yield_test) emscripten_sleep(0); } while (0)
#else
#define YIELD_TEST(a) do { (void)(a); } while (0)
#endif

/* STICK's F1 / F2 hotkeys (docs/SERVICES.md 01E3).  Kept outside the SDL block
 * because the config remembers them (#42): the exit path reads them whichever
 * build this is. */
static int music_on = 1, sfx_on = 1;

#ifdef HAVE_SDL
static int f1_was, f2_was;

static void dump_png(App *a, Game *g);
static void title_apply(App *a);

/* ------------------------------------------------------------ the shell */
/* The cavern and the town each ran their own copy of this; they now share it,
 * so a feature added here reaches both.  Nothing below touches the engine --
 * `make test` and `make verify` never enter this file. */

static void toggle_fullscreen(App *a)
{
    a->fullscreen = !a->fullscreen;
    if (SDL_SetWindowFullscreen(a->win, a->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0)
        fprintf(stderr, "[video] fullscreen: %s\n", SDL_GetError());
    else if (a->verbose)
        fprintf(stderr, "[video] %s\n", a->fullscreen ? "fullscreen" : "windowed");
}

/* #52 step 2: the save/load confirmation, drawn over the picture in the
 * presentation layer only.  The engine framebuffer is never touched, so
 * `make verify` and the traces cannot see it. */
static void osd_draw(App *a)
{
    const char *msg = statekeys_osd_text(&a->sk);
    if (!msg || !*msg) return;
    int ow = 0, oh = 0;
    if (SDL_GetRendererOutputSize(a->ren, &ow, &oh) != 0 || ow <= 0) return;
    int s = ow / 320;
    if (s < 2) s = 2;
    if (s > 6) s = 6;
    int w = statekeys_text_width(msg, s);
    int x0 = (ow - w) / 2, y0 = oh / 10;
    SDL_Rect bar = { x0 - 4 * s, y0 - 3 * s, w + 8 * s, 7 * s + 6 * s };
    SDL_SetRenderDrawBlendMode(a->ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(a->ren, 0, 0, 0, 190);
    SDL_RenderFillRect(a->ren, &bar);
    SDL_SetRenderDrawColor(a->ren, 255, 235, 120, 255);
    int x = x0;
    for (const char *p = msg; *p; p++, x += 6 * s) {
        const uint8_t *gl = statekeys_glyph(*p);
        for (int col = 0; col < 5; col++)
            for (int row = 0; row < 7; row++)
                if (gl[col] & (1 << row)) {
                    SDL_Rect px = { x + col * s, y0 + row * s, s, s };
                    SDL_RenderFillRect(a->ren, &px);
                }
    }
    SDL_SetRenderDrawBlendMode(a->ren, SDL_BLENDMODE_NONE);
}

/* Blit the mode's RGB image (already uploaded to a->tex) through the selected
 * display filter and present it.  The content shape is the aspect-corrected
 * display size, or the framebuffer itself under --aspect 1:1; present.c owns
 * the sampling.  Presentation only: `make verify` never reaches this path. */
static void present_frame(App *a, int sw, int sh)
{
    int dw = sw, dh = sh;
    if (!a->square) video_display_size(a->video, &dw, &dh);
    /* #54: where the picture goes decides where the controls can; in a portrait
     * window the picture rides at the top once the overlay is up (the layout is
     * computed as if it were, so the first touch finds the controls in place) */
    {
        int ow = 0, oh = 0, x, y, w, h;
        if (a->touch.mode != TOUCH_OFF && SDL_GetRendererOutputSize(a->ren, &ow, &oh) == 0) {
            present_content_rect_ex(a->filter, ow, oh, sw, sh, dw, dh, 1, &x, &y, &w, &h);
            touch_set_content(&a->touch, ow, oh, x, y, w, h);
        }
        present_set_portrait_top(touch_visible(&a->touch));
        touch_set_dialog(&a->touch, a->rest.state || a->spd.state || a->sync_dlg);
    }
    if (present_render(a->ren, a->tex, sw, sh, dw, dh, a->filter) != 0)
        fprintf(stderr, "[video] present: %s\n", SDL_GetError());
    osd_draw(a);
    touch_draw(&a->touch, a->ren);           /* #54: over the frame, after the filter */
    SDL_RenderPresent(a->ren);
}

static void pad_open(App *a, int which)
{
    if (a->pad || !SDL_IsGameController(which)) return;
    a->pad = SDL_GameControllerOpen(which);
    if (a->pad) fprintf(stderr, "[input] controller: %s\n", SDL_GameControllerName(a->pad));
}

static void pad_closed(App *a, SDL_JoystickID id)
{
    if (!a->pad) return;
    SDL_Joystick *j = SDL_GameControllerGetJoystick(a->pad);
    if (j && SDL_JoystickInstanceID(j) == id) { SDL_GameControllerClose(a->pad); a->pad = NULL; }
}

/* One event pump for both engines.  Returns with a->quit set if the window was
 * closed or the pause screen was answered "quit". */
static void pump_events(App *a, Game *g)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT: a->quit = 1; break;
#ifdef __ANDROID__
        /* Android lifecycle.  SDL blocks the next pump once DIDENTERBACKGROUND
         * has been delivered, so the save is queued here and the frame is let
         * finish (frame_wait, bg_flush): state_frame_done runs it at the true
         * frame end, before we block.  Pause only; the engine is not touched. */
        case SDL_APP_WILLENTERBACKGROUND:
            if (!a->paused) { a->paused = 1; a->bg_paused = 1; }
            break;
        case SDL_APP_DIDENTERBACKGROUND:
            if (g) statekeys_request(&a->sk, &a->sh, STATE_REQ_SAVE, -1);
            a->bg_flush = 1;
            break;
        case SDL_APP_DIDENTERFOREGROUND:
            if (a->bg_paused) { a->bg_paused = 0; a->paused = 0; }
            break;
#endif
        case SDL_CONTROLLERDEVICEADDED:   pad_open(a, e.cdevice.which); break;
        case SDL_CONTROLLERDEVICEREMOVED: pad_closed(a, e.cdevice.which); break;
        case SDL_FINGERDOWN: case SDL_FINGERMOTION: case SDL_FINGERUP: {
            int ow = 0, oh = 0;
            if (SDL_GetRendererOutputSize(a->ren, &ow, &oh) == 0) touch_handle_sdl(&a->touch, &e, ow, oh);
            break;
        }
        case SDL_CONTROLLERBUTTONDOWN:
            touch_note_other_input(&a->touch);
            if (input_pad_event_match(&a->in, ACT_PAUSE, e.cbutton.button)) { a->paused = !a->paused; title_apply(a); }
            break;
        case SDL_KEYDOWN:
#ifdef __ANDROID__
            if (e.key.keysym.sym == SDLK_AC_BACK) { a->esc_edge = 1; break; }   /* Back = pause, not quit */
#endif
            touch_note_other_input(&a->touch);       /* #54: keyboard use hides the overlay */
            /* the F7 dialog owns the keyboard while it is up */
            if (a->rest.state || a->spd.state || a->sync_dlg) {
                if (!e.key.repeat && a->rest_qn < 16) a->rest_q[a->rest_qn++] = (int)e.key.keysym.sym;
                break;
            }
            if (input_event_match(&a->in, ACT_RESTORE, &e.key)) { if (!e.key.repeat) a->restore_req = 1; break; }
            if (input_event_match(&a->in, ACT_SPEED, &e.key)) { if (!e.key.repeat) a->speed_req = 1; break; }
            /* #42: every hotkey is looked up in the one binding table
             * (input.c); the defaults are the keys this used to hard-code.
             * Esc used to quit outright, with no confirmation -- one reflex
             * keypress and the session was gone, which is neither what the
             * original does nor what anyone expects.  What it *should* do
             * differs by screen, so record the press and let the caller say:
             * the engines pause, a cutscene skips. */
            if (input_event_match(&a->in, ACT_PAUSE, &e.key)) a->esc_edge = 1;
            else if (e.key.keysym.sym == SDLK_q) { if (a->paused) a->quit = 1; }
            else if (input_event_match(&a->in, ACT_FULLSCREEN, &e.key)) toggle_fullscreen(a);
            /* the filter key cycles the display filter: integer -> sharp-bilinear -> smooth -> crt */
            else if (input_event_match(&a->in, ACT_FILTER, &e.key)) {
                a->filter = (a->filter + 1) % FILTER_COUNT;
                fprintf(stderr, "[video] filter: %s\n", present_filter_name(a->filter));
            }
            else if (input_event_match(&a->in, ACT_MENU, &e.key)) {
                if (e.key.keysym.mod & KMOD_ALT) toggle_fullscreen(a);   /* Alt+Enter */
                else if (a->paused) { a->paused = 0; title_apply(a); }
            }
            else if (input_event_match(&a->in, ACT_SCREENSHOT, &e.key)) { if (a->shot_path && g) dump_png(a, g); }
            /* #52 step 2: realtime save states, the same keys as the wasm
             * builds.  Ctrl+. quick-saves and Ctrl+\ quick-loads; Ctrl+1..9
             * saves to a numbered slot and Alt+1..9 loads one.  The combo is
             * remembered so the same Ctrl/Alt does not also swing the sword or
             * cast the spell this frame.  `g` is NULL in a cutscene, where there
             * is no engine state to save yet. */
            else if (g && input_event_match(&a->in, ACT_QUICKSAVE, &e.key)) {
                statekeys_request(&a->sk, &a->sh, STATE_REQ_SAVE, -1); a->sk_combo = 1;
            }
            else if (g && input_event_match(&a->in, ACT_QUICKLOAD, &e.key)) {
                statekeys_request(&a->sk, &a->sh, STATE_REQ_LOAD, -1); a->sk_combo = 1;
            }
            else if (g) {
                for (int n = 0; n < 9; n++) {
                    if (input_event_match(&a->in, ACT_SAVE1 + n, &e.key)) {
                        statekeys_request(&a->sk, &a->sh, STATE_REQ_SAVE, n + 1); a->sk_combo = 1; break;
                    }
                    if (input_event_match(&a->in, ACT_LOAD1 + n, &e.key)) {
                        statekeys_request(&a->sk, &a->sh, STATE_REQ_LOAD, n + 1); a->sk_combo = 1; break;
                    }
                }
            }
            {   /* FF29 (STICK 04D0): upper-case letters, digits, Backspace, Enter */
                SDL_Keycode k = e.key.keysym.sym;
                uint8_t c = 0;
                if (k >= SDLK_a && k <= SDLK_z) c = (uint8_t)('A' + (k - SDLK_a));
                else if (k >= SDLK_0 && k <= SDLK_9) c = (uint8_t)('0' + (k - SDLK_0));
                else if (k == SDLK_BACKSPACE) c = 0x08;
                else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) c = 0x0D;
                if (c && !a->sk_combo) a->sh.town.last_ascii = c;   /* not the slot digit */
            }
            break;
        default: break;
        }
    }
    /* #54: the touch overlay's one-shot controls go down the keyboard's paths */
    TouchEvent te;
    int tdirs = touch_dirs(&a->touch);
    while (touch_next_event(&a->touch, &te)) {
        if (a->rest.state || a->spd.state || a->sync_dlg) {      /* the F7 / F9 / sync dialogs take touch as keys */
            int sym = 0;
            switch (te.kind) {
            case TEV_ESC:   sym = SDLK_ESCAPE; break;
            case TEV_ITEMS: sym = SDLK_RETURN; break;
            case TEV_QSAVE: case TEV_QLOAD: break;
            case TEV_CHAR:  sym = (te.ch >= 'A' && te.ch <= 'Z') ? te.ch + 32 : te.ch; break;
            case TEV_TAP: {
                int fx, fy;
                if (touch_to_frame(&a->touch.L, te.x, te.y, 320, 200, &fx, &fy)) sym = REST_Q_TAP | (fy << 10) | fx;
                break;
            }
            default: break;
            }
            if (sym && a->rest_qn < 16) a->rest_q[a->rest_qn++] = sym;
            continue;
        }
        switch (te.kind) {
        case TEV_ESC:   a->esc_edge = 1; break;
        case TEV_ITEMS: if (a->paused) { a->paused = 0; title_apply(a); } break;
        case TEV_QSAVE: if (g) { statekeys_request(&a->sk, &a->sh, STATE_REQ_SAVE, -1); } break;
        case TEV_QLOAD: if (g) { statekeys_request(&a->sk, &a->sh, STATE_REQ_LOAD, -1); } break;
        case TEV_CHAR:  a->sh.town.last_ascii = (uint8_t)te.ch; break;
        default: break;
        }
    }
    if (a->rest.state || a->spd.state || a->sync_dlg) {          /* stick edges and Sword/Magic: list, caret, Space, Backspace */
        static const struct { int dir, sym; } dm[] = { { DIR_UP, SDLK_UP }, { DIR_DOWN, SDLK_DOWN }, { DIR_LEFT, SDLK_LEFT }, { DIR_RIGHT, SDLK_RIGHT } };
        int nb = touch_buttons(&a->touch, 0) & ~a->touch_prev_btn;
        for (int i = 0; i < 4; i++) if ((tdirs & dm[i].dir) && !(a->touch_prev_dirs & dm[i].dir) && a->rest_qn < 16) a->rest_q[a->rest_qn++] = dm[i].sym;
        if ((nb & 1) && a->rest_qn < 16) a->rest_q[a->rest_qn++] = SDLK_SPACE;
        if ((nb & 2) && a->rest_qn < 16) a->rest_q[a->rest_qn++] = SDLK_BACKSPACE;
    }
    a->touch_prev_dirs = tdirs;
    a->touch_prev_btn = touch_buttons(&a->touch, 0);
}

/* The window title's pause hint, so the state is discoverable without a font:
 * the engines own the framebuffer and this file must not draw into it.  The
 * base title is kept because pause flips *inside* frame_wait, long after the
 * engine set it -- without this the hint never actually appeared. */
static void title_apply(App *a)
{
    char t[960];
    snprintf(t, sizeof t, "%s%s", a->paused ? "[PAUSED - Esc/Enter resume, Q quit]  " : "", a->title_base);
    SDL_SetWindowTitle(a->win, t);
}
static void title_paused(App *a, const char *base)
{
    snprintf(a->title_base, sizeof a->title_base, "%s", base);
    title_apply(a);
}

/* Wait out one frame, pumping events; while paused, keep pumping and hold the
 * frame clock so resuming does not fast-forward. */
static void frame_wait(App *a, Game *g, Uint64 *next)
{
    Uint64 freq = SDL_GetPerformanceFrequency(), now = SDL_GetPerformanceCounter();
    if (*next == 0 || now > *next + freq) *next = now;
    *next += (Uint64)(a->frame_ms / 1000.0 * (double)freq);
#ifdef __EMSCRIPTEN__
    int slept = 0;
#endif
    for (;;) {
        pump_events(a, g);
#ifdef __ANDROID__
        if (a->bg_flush) { a->bg_flush = 0; break; }   /* let this frame end: the background save runs in frame_done */
#endif
        if (a->esc_edge) { a->esc_edge = 0; a->paused = !a->paused; title_apply(a); }
        if (a->quit) break;
        if (a->paused) { SDL_Delay(16); *next = SDL_GetPerformanceCounter(); continue; }
        now = SDL_GetPerformanceCounter();
        if (now >= *next) break;
        Uint64 rem = (*next - now) * 1000 / freq;
#ifdef __EMSCRIPTEN__
        slept = 1;
        emscripten_sleep((unsigned)(rem ? rem : 1));    /* ASYNCIFY: sleep to the frame deadline in the browser instead of spinning */
#else
        SDL_Delay(rem > 2 ? 1 : 0);
#endif
    }
#ifdef __EMSCRIPTEN__
    if (!slept) emscripten_sleep(0);   /* ASYNCIFY: still hand the thread back once a frame */
#endif
}

/* The analogue stick counts as a direction past half deflection. */
#define PAD_DEAD 16000
static uint8_t pad_dirs(App *a)
{
    if (!a->pad) return 0;
    SDL_GameController *c = a->pad;
    const Input *in = &a->in;
    uint8_t d = 0;
    if (input_pad_down(in, ACT_UP, c)    || SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY) < -PAD_DEAD) d |= DIR_UP;
    if (input_pad_down(in, ACT_DOWN, c)  || SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY) >  PAD_DEAD) d |= DIR_DOWN;
    if (input_pad_down(in, ACT_LEFT, c)  || SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX) < -PAD_DEAD) d |= DIR_LEFT;
    if (input_pad_down(in, ACT_RIGHT, c) || SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX) >  PAD_DEAD) d |= DIR_RIGHT;
    return d;
}
static uint8_t pad_btns(App *a)
{
    if (!a->pad) return 0;
    uint8_t b = 0;
    if (input_pad_down(&a->in, ACT_SWORD, a->pad)) b |= 1;   /* sword */
    if (input_pad_down(&a->in, ACT_MAGIC, a->pad)) b |= 2;   /* magic */
    return b;
}
static int pad_menu(App *a)
{
    return input_pad_down(&a->in, ACT_MENU, a->pad) || touch_menu_held(&a->touch);
}

/* Keyboard movement from the binding table; `town` picks the town defaults. */
static uint8_t key_dirs(const App *a, const Uint8 *k, int town)
{
    uint8_t d = 0;
    if (input_key_down(&a->in, ACT_UP, town, k, 0))    d |= DIR_UP;
    if (input_key_down(&a->in, ACT_DOWN, town, k, 0))  d |= DIR_DOWN;
    if (input_key_down(&a->in, ACT_LEFT, town, k, 0))  d |= DIR_LEFT;
    if (input_key_down(&a->in, ACT_RIGHT, town, k, 0)) d |= DIR_RIGHT;
    return d;
}
static uint8_t key_btns(const App *a, const Uint8 *k, int town, int combo)
{
    uint8_t b = 0;
    if (input_key_down(&a->in, ACT_SWORD, town, k, combo)) b |= 1;
    if (input_key_down(&a->in, ACT_MAGIC, town, k, combo)) b |= 2;
    return b;
}
static int key_menu(const App *a, const Uint8 *k, int town)
{
    return input_key_down(&a->in, ACT_MENU, town, k, 0);
}

/* F1 / F2, STICK.BIN's own music and sound-effect hotkeys */
static void audio_hotkeys(const App *a, const Uint8 *k)
{
    int f1 = input_key_down(&a->in, ACT_MUSIC, 0, k, 0), f2 = input_key_down(&a->in, ACT_SFX, 0, k, 0);
    if (f1 && !f1_was) { music_on = !music_on; audio_music_enable(music_on); }
    if (f2 && !f2_was) { sfx_on = !sfx_on; audio_sfx_enable(sfx_on); }
    f1_was = f1; f2_was = f2;
}
#endif

/* the engines carry the Shell in ->user; the front end hangs off shell->user */
static App *app_of(const void *shell_user) { return ((const Shell *)shell_user)->user; }

/* Run the 320x200 pair buffer through the selected video driver.  Every mode
 * but MCGA has a screen of its own size (640x200 on EGA/cga2, 720x348 on
 * Hercules), so the caller has to take the geometry from here too. */
static const uint8_t *video_out(App *a, int *w, int *h)
{
    static uint8_t rgb[VIDEO_MAX_W * VIDEO_MAX_H * 3];
    video_size(a->video, w, h);
    video_to_rgb(a->video, a->fb, rgb);
    return rgb;
}

static void usage(void)
{
    fprintf(stderr,
        "usage: zeliard [--dir GAMEDIR] [--map N] [--pos COL ROW] [--town N] [--headless]\n"
        "               [--screenshot N out.png] [--script SCRIPT] [--scale N] [--aspect A] [--filter F] [--fullscreen] [--speed N]\n"
        "               [--frames N] [--sound] [--verbose]\n"
        "               [--no-music] [--speaker | --tandy | --audio adlib|speaker|tandy] [--music N] [--dump-audio FILE.wav]\n"
        "  --dir       directory holding ZELRES1-3.SAR (default: the folder the config\n"
        "              remembered, else zeliard/, ../zeliard/, ./)\n"
        "  --map N     system map index (0 = MP10 .. 0x1E = MPA0; default 0)\n"
        "  --pos C R   hero top-left map cell (default: the MURALLA door in MP10, 61 7)\n"
        "  --town N    start in town N (0 cmap .. 9 esmp; 1 = Muralla) instead of a cavern\n"
        "  --cave I,COL,ROW,SIDE,MAP   harness start option: town cave record I now leads to system map\n"
        "              MAP at COL,ROW (oracle: ORACLE_CAVE, docs/HARNESS.md section 6)\n"
        "  --town-col C / --town-scr S / --town-anim F FACE / --town-npc I F\n"
        "              exact town placement, used by `make verify`\n"
        "  --screenshot N FILE   dump the framebuffer after N rendered frames (implies --headless)\n"
        "  --script S  headless input: tokens like R10 (hold Right 10 frames), UR2, D3, .5 (idle 5),\n"
        "              letters U D L R, X (sword), M (magic), E (Enter = status screen) and\n"
        "              K (the LEVEL/EXP chord), 'C types one key (A-Z, 0-9, '< Backspace),\n"
        "              separated by spaces/commas\n"
        "  --speed N   FF33 speed (frame = 4*N ticks; default 5 = 84.5 ms)\n"
        "  --scale N   window scale (default 3)\n"
        "  --aspect A  4:3 (default: the shape the driver's monitor had) or 1:1\n"
        "  --filter F  display scaling: integer, sharp (sharp-bilinear, default), smooth or crt\n"
        "              (scanlines + aperture grille);\n"
        "              F3 cycles them (presentation only, the framebuffer is untouched)\n"
        "  --touch M   on-screen touch controls: auto (default: shown on the first touch,\n"
        "              hidden again by keyboard or controller use), on or off; size and opacity\n"
        "              are touch.size / touch.opacity in zeliard.ini\n"
        "  --fullscreen  start full-screen (F11 or Alt+Enter toggles; Esc pauses, Q quits)\n"
        "  --video M   render through one of the five original drivers:\n"
        "              mcga (default) cga cga2 ega hgc tandy — the RESOURCE.CFG\n"
        "              `videoDrv` set, at each driver's own screen size\n"
        "  --frames N  quit after N rendered frames (SDL and headless)\n"
        "  --sound     log every FF75 sound request the engine produces\n"
        "  --sword N --shield N --level N --life N --gold N --bank N --keys N\n"
        "              set the player record directly (the shops do it properly)\n"
        "  --worn N    wear key item N ([9E], also put in the [A1..A5] bag): 1 Feruza shoes\n"
        "              (jump 4), 2 Pirika, 3 Silkarn, 4 Ruzeria shoes, 5 Asbestos cape\n"
        "  --potions L comma list of drug ids 0..7 into the five [A6..AA] slots (Enter -> USE:)\n"
        "  --magic S[=N]  select spell S (1..7) with N charges (max too), marked learned\n"
        "  --spells L  comma list of spell numbers 1..7 to mark learned at [BB..C1]\n"
        "  --name NAME the NAME.USR the sage's \"Record Experience\" writes\n"
        "  --load NAME restore NAME.USR (town.bin 7592) before starting; the page's\n"
        "              [C4] picks the town and [80]/[83] the column\n"
        "  --save NAME write NAME.usr for the state just set up and exit (kenjpro A862)\n"
        "  --sync status|pull|push [--force]   save sync of the town saves (.usr) with a\n"
        "              zeliard-wasm server (docs/SYNC.md); runs once and exits.  --sync-key KEY\n"
        "              picks/remembers the key, --sync-base URL the server (else zeliard.ini)\n"
        "  --sync status|pull|push [--force]   sync the town saves (.usr) with a zeliard-wasm\n"
        "              server (docs/SYNC.md); runs once and exits.  --sync-key KEY picks and\n"
        "              remembers the key, --sync-base URL the server (else sync_base in zeliard.ini)\n"
        "  --intro / --no-intro   run (or skip) the opening demo; the default is to run it\n"
        "              only on a plain interactive launch, as GAME.BIN does when it was\n"
        "              given no command-line argument\n"
        "  --intro-act N          run only act N of opdemo (1 prologue+title, 2 credits,\n"
        "              3 the storm demo) and exit\n"
        "  --ending    run enddemo (the ending and the credits roll) and exit\n"
        "  --cutscene-frames N    stop a cutscene after N rendered frames\n"
        "  --gd-art NAME OUT.png  render one intro/ending resource (ame.grp, ttl3.grp, ...)\n"
        "              exactly as tools/grp2png.py's render_gd does\n"
        "  --trace DIR the differential harness (docs/HARNESS.md): headless; one line per\n"
        "              frame boundary (fight 717F / town 68ED) to DIR/frames.jsonl, the\n"
        "              harness RNG, and --script advanced one token-frame per boundary.\n"
        "              --frames N stops after N traced frames\n"
        "  --png-every K          with --trace: DIR/frame_NNNNN.png every K traced frames\n"
        "  --state-save-at N FILE with --trace: write a realtime engine state (#52) at trace\n"
        "              frame boundary N (a fight/town frame, not a menu poll) to FILE\n"
        "  --state-load FILE      start from a state written by --state-save-at; the trace\n"
        "              numbering continues at N+1 and the script resumes from that token\n"
        "  --state-dir DIR        where the quick/slot states live (default: SDL_GetPrefPath\n"
        "              \"awkto\"/\"zeliard\"/states, else <gamedir>/states)\n"
        "  --state-inject SPEC    with --trace: queue save:N[:slot] or load:N[:slot] at trace\n"
        "              boundary N (the key path without a keyboard); repeatable\n"
        "  --flag N[=V] set story-flag byte [N] (hex 0..48, docs/STATE_PAGE.md) to V (hex,\n"
        "              default FF) before the save is made / the game starts; repeatable\n"
        "  --pilot ROUTE          with --trace: the autopilot plays ROUTE instead of a\n"
        "              --script, deciding the input at every boundary.  ROUTE is ';'-separated:\n"
        "              town-cave C | town-shop C [MENU] | town-edge 0|1 | door C,R | door -1\n"
        "              (the exit door 72F1 puts down) | cell C,R | break C,R | boss\n"
        "              | keys TOKENS (fed as --script tokens, for a move it cannot find)\n"
        "  --pilot-tail N         idle frames after the route is done (default 10)\n"
        "  --emit-script FILE     with --pilot: write the input it fed as a --script\n"
        "  --trace-fields         list every field name --trace can emit and exit\n"
        "\n"
        "Settings are remembered between runs in zeliard.ini (SDL_GetPrefPath, or\n"
        "$XDG_CONFIG_HOME / $HOME/.config without SDL); command-line flags override it.\n"
        "Headless runs take only the game folder from it.\n");
}

static int script_next(App *a)
{
    while (a->script_left == 0) {
        const char *s = a->script;
        if (!s) return 0;
        while (s[a->script_pos] == ' ' || s[a->script_pos] == ',') a->script_pos++;
        if (!s[a->script_pos]) return 0;
        uint8_t d = 0, b = 0;
        a->script_char = 0; a->script_char_new = 0;
        if (s[a->script_pos] == 'S' || s[a->script_pos] == 's') {
            /* S presses F9 (#116): the kernel's [114] opens "Speed change /
             * Select 0-9:" at the next frame; the tokens that follow are the
             * dialog's keys (see speed_modal).  Port-only, like F. */
            a->speed_req = 1;
            a->script_pos++;
        } else
        if (s[a->script_pos] == 'F' || s[a->script_pos] == 'f') {
            /* F presses F7 (docs/HARNESS.md §2): the kernel's [11E] opens
             * "Restore Game / Sure?(Y/N)" at the next frame; nothing is held.
             * The dialog then reads the tokens that follow (see restore_modal) */
            a->restore_req = 1;
            a->script_pos++;
        } else
        if (s[a->script_pos] == '\'') {
            /* 'C types one key (docs/HARNESS.md §2): FF29 = its ASCII on the
             * token's first frame, nothing held.  A-Z (either case), 0-9, and
             * '< for Backspace (08) -- what STICK's INT 9 table can produce
             * for the sage's name entry (kenjpro A627) */
            char c = s[a->script_pos + 1];
            if (c >= 'a' && c <= 'z') c = (char)(c - 32);
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) a->script_char = (uint8_t)c;
            else if (c == '<') a->script_char = 0x08;
            a->script_char_new = a->script_char != 0;
            a->script_pos += c ? 2 : 1;
        } else
        for (;;) {
            char c = s[a->script_pos];
            if (c == 'U' || c == 'u') d |= DIR_UP;
            else if (c == 'D' || c == 'd') d |= DIR_DOWN;
            else if (c == 'L' || c == 'l') d |= DIR_LEFT;
            else if (c == 'R' || c == 'r') d |= DIR_RIGHT;
            else if (c == 'X' || c == 'x') b |= 1;
            else if (c == 'M' || c == 'm') b |= 2;
            else if (c == 'E' || c == 'e') b |= 4;              /* Enter: the status screen */
            else if (c == 'K' || c == 'k') b |= 8;              /* the FF18 == 0x0286 chord */
            else if (c == '.') ;
            else break;
            a->script_pos++;
        }
        int n = 0;
        while (s[a->script_pos] >= '0' && s[a->script_pos] <= '9') n = n * 10 + (s[a->script_pos++] - '0');
        a->script_dirs = d; a->script_btns = b; a->script_left = n ? n : 1;
    }
    a->script_left--;
    return 1;
}

/* the whole 320x200 screen of a cavern / town frame into a->fb */
static void fb_fight(App *a, Game *g)
{
    if (g->status) memcpy(a->fb, status_framebuffer(g->status), FB_W * FB_H);
    else if (g->tear) memcpy(a->fb, tear_framebuffer(g->tear), FB_W * FB_H);
    else render_frame(a->fb, g, &a->sh.hero);
    render_hud(a->fb, g, &a->sh.font, &a->sh.tfont, map_place_record(g->map));
    itemp_hud(a->fb, &a->sh.pics, &a->sh.tfont, g);
    if (!g->tear) tear_draw_slots(a->fb, g, &a->sh.tear_art);   /* GAME.BIN A18E -> A3A5 */
}
static void fb_town(App *a, Town *t)
{
    if (t->status) memcpy(a->fb, status_framebuffer(t->status), FB_W * FB_H);
    else if (t->shop) memcpy(a->fb, shop_framebuffer(t->shop), FB_W * FB_H);
    else town_render(a->fb, t);
    render_hud(a->fb, t->g, &a->sh.font, &a->sh.tfont, town_place_record(t->map));
    if (t->shop && !t->status) shop_hud_place(a->fb, &a->sh.tfont, t->shop);   /* #78 */
    itemp_hud(a->fb, &a->sh.pics, &a->sh.tfont, t->g);
    tear_draw_slots(a->fb, t->g, &a->sh.tear_art);
}
static int fb_write_png(App *a, const char *path)
{
    int vw, vh;
    const uint8_t *rgb = video_out(a, &vw, &vh);
    if (png_write_rgb(path, rgb, vw, vh)) { fprintf(stderr, "cannot write %s\n", path); return -1; }
    return 0;
}

static void dump_png(App *a, Game *g)
{
    fb_fight(a, g);
    if (fb_write_png(a, a->shot_path) == 0) fprintf(stderr, "wrote %s (frame %u, hero map (%d,%d) scr (%d,%d) scroll (%d,%d))\n", a->shot_path,
                 g->frame_no, game_hero_map_col(g), game_hero_map_row(g), g->hero_scr_col, g->hero_scr_row,
                 g->scroll_col, g->scroll_row);
}

/* ------------------------------------------- F7 "Restore Game" (#42) */
/* The original polls the kernel's [11E] in the town frame (68DC) and the
 * cavern frame (716E); on F7 the screen freezes under restore.c's two dialogs.
 * Here the same: a modal loop over the frame the engine just drew, fed by the
 * keyboard (SDL) or by --script tokens (headless: Y / N as 'Y 'N, letters and
 * '< as for the sage's name entry, U D L R move, X = Space, E = Enter, M = Esc).
 * Presentation and input only; the load itself runs at the frame's end
 * (state_frame_done -> restore_commit), the --load path.  Not honoured under
 * --trace (the harness input has no F7), nor while a shop, the status screen
 * or a dialogue box owns the frame: those wait loops do not return here. */
#ifdef HAVE_SDL
static int restore_sdl_key(int sym, char *ch)
{
    *ch = 0;
    switch (sym) {
    case SDLK_UP: return RK_UP;       case SDLK_DOWN: return RK_DOWN;
    case SDLK_LEFT: return RK_LEFT;   case SDLK_RIGHT: return RK_RIGHT;
    case SDLK_SPACE: return RK_TAKE;
    case SDLK_RETURN: case SDLK_KP_ENTER: return RK_ACCEPT;
    case SDLK_BACKSPACE: return RK_BACKSPACE;
    case SDLK_ESCAPE: return RK_CANCEL;
    default: break;
    }
    if (sym >= SDLK_a && sym <= SDLK_z) { *ch = (char)('A' + (sym - SDLK_a)); return RK_CHAR; }
    if (sym >= SDLK_0 && sym <= SDLK_9) { *ch = (char)('0' + (sym - SDLK_0)); return RK_CHAR; }
    return RK_NONE;
}
#endif

static void restore_modal(App *a, Game *g, Town *t)
{
    Restore *r = &a->rest;
    a->restore_req = 0;
    if (a->trace_f || a->quit) return;
    if (t ? (t->shop || t->status || t->dlg.active) : (g->status || g->tear)) return;
    restore_open(r);
    a->rest_qn = 0;
#ifdef HAVE_SDL
    static Uint64 next = 0;
    next = 0;
#endif
    while (r->state && !a->quit) {
        if (t) fb_town(a, t); else fb_fight(a, g);
        restore_draw(r, &a->sh, a->fb);
        int key = RK_NONE; char ch = 0;
        if (a->headless) {
            if (a->shot_path) fb_write_png(a, a->shot_path);    /* the dialog's last picture stays */
            if (!script_next(a)) { r->state = RS_CLOSED; audio_music_pause(0); break; }
            if (a->script_char_new) {
                ch = (char)a->script_char; a->script_char_new = 0;
                key = ch == 0x08 ? RK_BACKSPACE : RK_CHAR;
            }
            else if (a->script_btns & 4)                    key = RK_ACCEPT;
            else if (a->script_btns & 1)                    key = RK_TAKE;
            else if (a->script_dirs & DIR_UP)               key = RK_UP;
            else if (a->script_dirs & DIR_DOWN)             key = RK_DOWN;
            else if (a->script_dirs & DIR_LEFT)             key = RK_LEFT;
            else if (a->script_dirs & DIR_RIGHT)            key = RK_RIGHT;
            else if (a->script_btns & 2)                    key = RK_CANCEL;
            a->restore_req = 0;                             /* an F inside the dialog is nothing */
        }
#ifdef HAVE_SDL
        else {
            int vw, vh;
            const uint8_t *rgb = video_out(a, &vw, &vh);
            SDL_UpdateTexture(a->tex, NULL, rgb, vw * 3);
            present_frame(a, vw, vh);
            frame_wait(a, g, &next);                        /* pumps events into rest_q */
            if (a->rest_qn) {
                int sym = a->rest_q[0];
                memmove(a->rest_q, a->rest_q + 1, (size_t)(--a->rest_qn) * sizeof a->rest_q[0]);
                if (sym & REST_Q_TAP) restore_tap(r, &a->sh, sym & 1023, (sym >> 10) & 1023);
                else key = restore_sdl_key(sym, &ch);
            }
        }
#endif
        if (key != RK_NONE) restore_key(r, &a->sh, key, ch);
    }
    if (a->quit && r->state) { r->state = RS_CLOSED; audio_music_pause(0); }
    a->restore_req = 0;
}

/* ------------------------------------------- F9 "Speed change" (#116) */
/* Kernel [114] (STICK 07B6), polled next to [11E] in the same frame chain
 * (fight.bin 715F, town.bin 68C8).  Same modal shape as restore_modal: the
 * frozen frame under speed.c's box, keys from SDL or --script tokens (headless:
 * 'C a digit, any other token is "any key", M closes).  A digit sets FF33,
 * which paces every later frame (a->frame_ms), including the dialog's own. */
#ifdef HAVE_SDL
static int speed_sdl_key(int sym, int *digit)
{
    *digit = -1;
    if (sym >= SDLK_0 && sym <= SDLK_9) { *digit = sym - SDLK_0; return SPK_DIGIT; }
    if (sym >= SDLK_KP_1 && sym <= SDLK_KP_9) { *digit = sym - SDLK_KP_1 + 1; return SPK_DIGIT; }
    if (sym == SDLK_KP_0) { *digit = 0; return SPK_DIGIT; }
    if (sym == SDLK_ESCAPE) return SPK_CANCEL;
    return sym ? SPK_ANY : SPK_NONE;
}
#endif

static void speed_apply(App *a, int ff33)
{
    a->speed = ff33;
    a->frame_ms = SPEED_FRAME_MS(ff33);
    fprintf(stderr, "[speed] FF33 = %d (%.1f ms per frame)\n", ff33, a->frame_ms);
}

static void speed_modal(App *a, Game *g, Town *t)
{
    SpeedDlg *d = &a->spd;
    a->speed_req = 0;
    if (a->trace_f || a->quit) return;
    if (t ? (t->shop || t->status || t->dlg.active) : (g->status || g->tear)) return;
    speed_open(d, a->speed);
    a->rest_qn = 0;
#ifdef HAVE_SDL
    static Uint64 next = 0;
    next = 0;
#endif
    while (d->state && !a->quit) {
        if (t) fb_town(a, t); else fb_fight(a, g);
        speed_draw(d, &a->sh, a->fb);
        int key = SPK_NONE, digit = -1;
        if (a->headless) {
            if (a->shot_path) fb_write_png(a, a->shot_path);    /* the dialog's last picture stays */
            if (!script_next(a)) { d->state = SP_CLOSED; break; }
            if (a->script_char_new) {
                a->script_char_new = 0;
                if (a->script_char >= '0' && a->script_char <= '9') { key = SPK_DIGIT; digit = a->script_char - '0'; }
                else key = SPK_ANY;
            }
            else if (a->script_btns & 2) key = d->state == SP_DIGIT ? SPK_CANCEL : SPK_ANY;
            else if (a->script_dirs || a->script_btns) key = SPK_ANY;
            a->speed_req = 0; a->restore_req = 0;
        }
#ifdef HAVE_SDL
        else {
            int vw, vh;
            const uint8_t *rgb = video_out(a, &vw, &vh);
            SDL_UpdateTexture(a->tex, NULL, rgb, vw * 3);
            present_frame(a, vw, vh);
            frame_wait(a, g, &next);
            if (a->rest_qn) {
                int sym = a->rest_q[0];
                memmove(a->rest_q, a->rest_q + 1, (size_t)(--a->rest_qn) * sizeof a->rest_q[0]);
                if (sym & REST_Q_TAP) key = SPK_ANY;        /* a tap is a fire press */
                else key = speed_sdl_key(sym, &digit);
            }
        }
#endif
        int v = speed_key(d, key, digit);
        if (v) speed_apply(a, v);
    }
    if (a->quit) d->state = SP_CLOSED;
    a->speed_req = 0;
}

/* ------------------------------------------------- save sync lifecycle (#55 step 2) */
/* synclife.c owns the decisions; this is the thread, the hook and the dialog.
 * Interactive SDL runs only: a headless / scripted / traced run never builds one. */
static SyncLife g_sl;
static void on_usr_saved(const char *dir, const char *name)
{
    (void)dir; (void)name;
    synclife_request_push(&g_sl);                       /* returns at once: the push is on its own thread */
}
#ifdef HAVE_SDL
static int sync_thread_main(void *p)
{
    void **v = p; ((void (*)(void *))v[0])(v[1]); return 0;
}
static void *sync_spawn(void (*fn)(void *), void *arg)
{
    void **v = malloc(2 * sizeof *v);
    if (!v) return NULL;
    v[0] = (void *)fn; v[1] = arg;
    SDL_Thread *t = SDL_CreateThread(sync_thread_main, "zeliard-sync", v);
    if (!t) { free(v); return NULL; }
    /* v is leaked into the thread: freed by sync_join once the thread is done */
    void **h = malloc(2 * sizeof *h);
    if (!h) { SDL_WaitThread(t, NULL); free(v); return NULL; }
    h[0] = t; h[1] = v;
    return h;
}
static void sync_join(void *handle)
{
    void **h = handle;
    SDL_WaitThread(h[0], NULL);
    free(h[1]); free(h);
}
static const SyncRunner sync_runner = { sync_spawn, sync_join };

/* the "Two saves found for this key" modal, over a black frame, before the
 * first town loads.  Keys come through the same SDL pump as the F7 dialog. */
static int sync_ask_modal(App *a)
{
    SyncDlg d;
    syncdlg_open(&d, synclife_key(&g_sl));
    a->sync_dlg = 1; a->rest_qn = 0;
    Uint64 next = 0;
    while (!d.choice && !a->quit) {
        memset(a->fb, 0, sizeof a->fb);
        syncdlg_draw(&d, &a->sh, a->fb);
        int vw, vh;
        const uint8_t *rgb = video_out(a, &vw, &vh);
        SDL_UpdateTexture(a->tex, NULL, rgb, vw * 3);
        present_frame(a, vw, vh);
        frame_wait(a, &a->sh.g, &next);
        while (a->rest_qn) {
            int sym = a->rest_q[0], key = RK_NONE; char ch;
            memmove(a->rest_q, a->rest_q + 1, (size_t)(--a->rest_qn) * sizeof a->rest_q[0]);
            key = restore_sdl_key(sym, &ch);
            syncdlg_key(&d, key);
        }
    }
    a->sync_dlg = 0;
    return d.choice ? d.choice : SLC_CANCEL;            /* closing the window = cancel */
}
#endif

/* INT 61h AH: bit0 sword, bit1 magic.  The kernel latches the press edge in
 * FF1D (btn1_edge) and fight.bin clears it when it consumes it (6EF7). */
static void set_buttons(Game *g, uint8_t b)
{
    if ((b & 1) && !(g->buttons & 1)) g->btn1_edge = 0xFF;
    if ((b & 2) && !(g->buttons & 2)) g->btn2_edge = 0xFF;
    g->buttons = b;
}

/* ------------------------------------------------------------ the harness */
/* docs/HARNESS.md §2/§3.  At boundary n the line for frame n is written (its
 * `input` is what frame n read, i.e. what boundary n-1 applied), then the
 * script's next token-frame is applied for frame n+1 -- to the cavern AND the
 * town input state, because the original keeps one copy (FF16/FF17/FF1D/FF1E)
 * and the next boundary may be in the other engine.  Trace frame 0 resets the
 * harness RNG to 0 and the call counter. */
static void trace_set_input(App *a, uint8_t dirs, uint8_t btns, uint8_t menu)
{
    Game *g = &a->sh.g; Town *t = &a->sh.town;
    g->dirs = dirs; set_buttons(g, btns); g->menu_key = menu;
    if ((btns & 1) && !(t->buttons & 1)) t->btn1_edge = 0xFF;
    if ((btns & 2) && !(t->buttons & 2)) t->btn2_edge = 0xFF;
    t->dirs = dirs; t->buttons = btns; t->menu_key = menu;
}

/* --emit-script: one token-frame more, run-length coded ("R13 .3 X1") */
static void emit_flush(App *a)
{
    if (a->emit_f && a->emit_count) {
        fprintf(a->emit_f, "%s%s%u", a->emit_n++ ? " " : "", a->emit_tok, a->emit_count);
        a->emit_count = 0;
    }
}
static void emit_token(App *a, uint8_t dirs, uint8_t btns)
{
    char tok[12];
    trace_input_token(tok, dirs, btns, 0);
    if (a->emit_count && strcmp(tok, a->emit_tok)) emit_flush(a);
    snprintf(a->emit_tok, sizeof a->emit_tok, "%s", tok);
    a->emit_count++;
}

/* --pilot: the next token-frame from the autopilot (then --pilot-tail idle
 * frames), into the same fields script_next fills */
static int pilot_next(App *a)
{
    a->script_char = 0; a->script_char_new = 0;
    a->script_dirs = 0; a->script_btns = 0;
    if (a->pilot_state == 0) {
        uint8_t d, b;
        int rc = play_pilot(a->pilot, &d, &b);
        if (rc < 0) { a->pilot_state = -1; fprintf(stderr, "[pilot] FAILED: %s\n", a->pilot->msg); return 0; }
        if (rc > 0) { a->pilot_state = 1; fprintf(stderr, "[pilot] route done after %u frames\n", a->trace_n); }
        else { a->script_dirs = d; a->script_btns = b; }
    }
    if (a->pilot_state == 1) {
        if (a->pilot_tail == 0) return 0;
        a->pilot_tail--;
    }
    emit_token(a, a->script_dirs, a->script_btns);
    return 1;
}

/* Take the next token-frame from --script / --pilot and install it as the
 * input for the frame after this boundary.  Also used by --state-load, which
 * resumes mid-script and must apply the boundary's own token before its first
 * frame runs. */
static void harness_advance(App *a)
{
    int more = a->pilot ? pilot_next(a) : script_next(a);
    if (!more && a->max_frames) { a->script_dirs = a->script_btns = 0; more = 1; }   /* --frames: idle on */
    if ((a->max_frames && a->trace_n >= a->max_frames) || !more) {
        a->trace_done = 1; a->quit = 1;
        Town *tw = &a->sh.town;
        tw->quit = 1;                               /* out of any blocking box, shop or menu */
        if (tw->status) tw->status->done = 1;
        if (tw->shop) tw->shop->leave = 1;
        if (a->sh.g.status) a->sh.g.status->done = 1;
        trace_set_input(a, 0, 0, 0);
        return;
    }
    trace_set_input(a, a->script_dirs, (uint8_t)((a->script_btns & 3) | (a->script_btns & 8 ? 4 : 0)),
                    (uint8_t)(a->script_btns & 4 ? 1 : 0));
    a->trace_char = 0;
    if (a->script_char_new) {                       /* STICK INT 9 kbd_scan_to_ascii -> FF29 */
        a->sh.town.last_ascii = a->trace_char = a->script_char;
        a->script_char_new = 0;
    }
}

/* Run one queued save/load at the true end of a frame.  `frame` is the trace
 * boundary the state is the boundary of (or the engine's frame counter when not
 * tracing).  Returns 0 on success. */
static int state_execute(App *a, Shell *s, int kind, int slot, unsigned frame)
{
    char path[1200];
    if (statekeys_path(&a->sk, slot, path, sizeof path) != 0) {
        statekeys_osd(&a->sk, "NO STATE DIRECTORY");
        a->sk.refused++;
        return -1;
    }
    if (kind == STATE_REQ_SAVE) {
        unsigned rng = a->sk.rng_set ? a->sk.rng : s->g.rng_calls;
        if (savestate_save(s, path, frame, rng) == 0) {
            statekeys_osd(&a->sk, slot < 0 ? "SAVED QUICK SAVE" : "SAVED SLOT %d", slot);
            a->sk.saved++;
            fprintf(stderr, "[state] saved %s (frame %u)\n", path, frame);
            return 0;
        }
        statekeys_osd(&a->sk, "CANNOT SAVE HERE");
        a->sk.refused++;
        return -1;
    }
    SavestateMeta meta;
    if (savestate_load(s, path, &meta) == 0) {
        a->sk.last_rng = meta.rng_calls;
        /* the score's position is not part of the state: restart the one the
         * loaded map or town names, so the music agrees with the picture again */
        audio_music_stop();
        int idx = s->in_town ? s->town.map->music : (int)((s->g.map->lvl_flags >> 1) & 0x0F);
        audio_music(idx);
        statekeys_osd(&a->sk, slot < 0 ? "LOADED QUICK SAVE" : "LOADED SLOT %d", slot);
        a->sk.loaded++;
        fprintf(stderr, "[state] loaded %s (frame %u)\n", path, meta.frame);
        return 0;
    }
    /* tell "there is no such slot" apart from "the file is not a state we can
     * read", so the OSD says which */
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); statekeys_osd(&a->sk, "SAVE INCOMPATIBLE"); }
    else statekeys_osd(&a->sk, slot < 0 ? "NO QUICK SAVE" : "NO SAVE IN SLOT %d", slot);
    a->sk.refused++;
    return -1;
}

/* shell.frame_done (#52): the frame whose boundary was N has fully run, so the
 * engine is at a resumable point.  Step 1's --state-save-at blob is written
 * here, and so is any queued interactive save/load (step 2). */
static void state_frame_done(Shell *s)
{
    App *a = s->user;
    if (a->rest.apply) restore_commit(&a->rest, s);     /* #42: F7's accepted name -> the town it names */
    if (a->state_save && !a->state_save_done && a->state_save_pending) {
        a->state_save_pending = 0;
        if (a->trace_n != a->state_save_at + 1) {       /* the frame opened a blocking screen (a shop/status poll): not resumable here */
            fprintf(stderr, "[state] frame %u opened a blocking screen: not saved\n", a->state_save_at);
        } else {
            a->state_save_done = 1;
            unsigned n = a->state_save_at;              /* the boundary just traced */
            if (savestate_save(s, a->state_save, n, a->state_save_rng) == 0)
                fprintf(stderr, "[state] saved frame %u -> %s\n", n, a->state_save);
            else
                fprintf(stderr, "[state] frame %u not saved\n", n);
        }
    }
    if (a->sk.q_n) {
        int any_load = 0;
        unsigned frame = a->trace_f ? a->trace_n - 1
                       : (s->in_town ? s->town.frame_no : s->g.frame_no);
        for (int i = 0; i < a->sk.q_n; i++) {
            if (state_execute(a, s, a->sk.q_kind[i], a->sk.q_slot[i], frame) == 0
                && a->sk.q_kind[i] == STATE_REQ_LOAD)
                any_load = 1;
        }
        a->sk.q_n = 0;
        a->sk.rng_set = 0;
        if (any_load && a->trace_f) {
            /* the boundary already applied the next frame's script token to the
             * pre-load state; re-apply it to the loaded state, and rebase the
             * RNG delta on the loaded state's own counter */
            a->rng_mark = a->sk.last_rng;
            trace_set_input(a, a->script_dirs,
                            (uint8_t)((a->script_btns & 3) | (a->script_btns & 8 ? 4 : 0)),
                            (uint8_t)(a->script_btns & 4 ? 1 : 0));
            if (a->trace_char) a->sh.town.last_ascii = a->trace_char;
        }
    }
}

/* `site`: "717F" / "68ED" for the frame boundaries, else a wait loop's poll
 * site (docs/HARNESS.md §1); `t` NULL = fight.bin's field set */
static void trace_boundary_at(App *a, Game *g, Town *t, const char *site)
{
    if (a->trace_done) return;
    /* --state-save-at: mark boundary N.  The blob is written by frame_done,
     * once this frame's post-boundary tail has run, so the shell is at a point
     * a fresh game_step can resume from; the trace delta base is the RNG
     * counter as this boundary saw it.  Only a plain 717F/68ED frame, not a
     * dialogue/shop/status poll, is eligible. */
    if (a->state_save && !a->state_save_done && !a->state_save_pending && a->trace_n == a->state_save_at) {
        int clean = (strcmp(site, "717F") == 0 || strcmp(site, "68ED") == 0)
                    && !(t && (t->dlg.active || t->status || t->shop))
                    && !g->status && !g->tear;
        if (!clean)
            fprintf(stderr, "[state] frame %u (%s): not a plain frame boundary, not saved\n", a->trace_n, site);
        else { a->state_save_pending = 1; a->state_save_rng = g->rng_calls; }
    }
    wait_reset();                                   /* any boundary restarts the wait-loop clock */
    if (a->trace_n == 0) { g->rng = 0; a->rng_mark = g->rng_calls; }    /* HARNESS §3: seed 0 */
    char in[12];
    if (t) trace_input_token(in, t->dirs, t->buttons, t->menu_key);
    else   trace_input_token(in, g->dirs, g->buttons, g->menu_key);
    if (a->trace_char) {                            /* a 'C token's key: "'A", "'<" */
        size_t n = in[0] == '.' ? 0 : strlen(in);
        in[n] = '\''; in[n + 1] = a->trace_char == 0x08 ? '<' : (char)a->trace_char; in[n + 2] = 0;
    }
    unsigned k = g->rng_calls - a->rng_mark;
    a->rng_mark = g->rng_calls;
    /* #52 step 2: fire any --state-inject events scheduled for this boundary
     * (the headless key path), then record the RNG base the queued request will
     * save with -- the same value a keyboard press seen this frame gets. */
    for (int i = 0; i < a->n_st_ev; i++)
        if (a->st_ev[i].frame == a->trace_n)
            statekeys_queue(&a->sk, a->st_ev[i].kind, a->st_ev[i].slot);
    if (a->sk.q_n && !a->sk.rng_set) { a->sk.rng = g->rng_calls; a->sk.rng_set = 1; }
    trace_write_frame(a->trace_f, a->trace_n, site, k, in, g, t);
    if (a->png_every && a->trace_n % a->png_every == 0) {
        char path[1100];
        snprintf(path, sizeof path, "%s/frame_%05u.png", a->trace_dir, a->trace_n);
        /* the screen at 717F holds the blade gfmcga drew at 7147, frame
         * [FF46]-1; attack_var has been stepped past it since (combat.c) */
        int sw = !t && g->attacking && g->attack_var;
        if (sw) g->attack_var--;
        if (t) fb_town(a, t); else fb_fight(a, g);   /* render.c draws the background from bg_ring (302C's 3159 cells) */
        if (sw) g->attack_var++;
        fb_write_png(a, path);
    }
    a->trace_n++;
    a->trace_idle = 0;
    harness_advance(a);
}
static void trace_fight_end(Game *g) { trace_boundary_at(app_of(g->user), g, NULL, "717F"); }
static void trace_town_end(Town *t)  { trace_boundary_at(app_of(t->user), t->g, t, "68ED"); }
/* a wait-loop boundary (town.h wait_poll): shops, menus, the status screen */
static void trace_wait(Game *g, Town *t, const char *site) { trace_boundary_at(app_of(g->user), g, t, site); }

/* ------------------------------------------------------------- cutscenes */
/* opdemo / enddemo own the frame loop exactly as the shops do; this is their
 * `present`.  They draw into their own 320x200 buffer with their own 256-entry
 * DAC, so the conversion to RGB goes through gd_to_rgb, not render_to_rgb. */
static void cutscene_present(Cutscene *c)
{
    App *a = c->user;
    YIELD_TEST(a);
    static uint8_t rgb[VIDEO_MAX_W * VIDEO_MAX_H * 3];
    int vw, vh;
    cutscene_size(c, &vw, &vh);
    audio_advance_ms(CS_FRAME_MS);
    a->restore_req = 0; a->speed_req = 0;   /* no F7 / F9 in a cutscene */
    if (a->shot_path && c->frames == a->shot_frame) {
        cutscene_to_rgb(c, rgb);
        if (png_write_rgb(a->shot_path, rgb, vw, vh)) fprintf(stderr, "cannot write %s\n", a->shot_path);
        else fprintf(stderr, "wrote %s (cutscene act %d, frame %u, beat %d)\n",
                     a->shot_path, c->act, c->frames, c->beat);
    }
    if (a->headless && a->cs.boundary) return;      /* --trace: the harness tokens are taken at the boundaries */
    if (a->headless) {
        /* --script drives the two abort keys: any X / E / Space token aborts
         * the act, exactly as [FF1D] / [FF29] do */
        c->key = 0;
        if (script_next(a)) c->key = (uint8_t)((a->script_btns & (1 | 4)) ? 1 : 0);
        if (a->quit) c->quit = 1;
        return;
    }
#ifdef HAVE_SDL
    cutscene_to_rgb(c, rgb);
    SDL_UpdateTexture(a->tex, NULL, rgb, vw * 3);
    present_frame(a, vw, vh);
    static Uint64 next = 0;
    Uint64 now = SDL_GetPerformanceCounter(), freq = SDL_GetPerformanceFrequency();
    if (next == 0 || now > next + freq) next = now;
    next += (Uint64)(CS_FRAME_MS / 1000.0 * (double)freq);
#ifdef __EMSCRIPTEN__
    int slept = 0;
#endif
    for (;;) {
        pump_events(a, NULL);
        /* Esc skips the rest of the cutscene; it used to quit the game, which
         * is a harsh answer to "I have seen the intro".  Closing the window
         * still quits.  Space / Return skip a single act, as opdemo does. */
        if (a->esc_edge) { a->esc_edge = 0; c->quit = 1; }
        if (a->quit) c->quit = 1;
        now = SDL_GetPerformanceCounter();
        if (now >= next || a->quit || c->quit) break;
        Uint64 rem = (next - now) * 1000 / freq;
#ifdef __EMSCRIPTEN__
        slept = 1;
        emscripten_sleep((unsigned)(rem ? rem : 1));    /* see frame_wait */
#else
        SDL_Delay(rem > 2 ? 1 : 0);
#endif
    }
#ifdef __EMSCRIPTEN__
    if (!slept) emscripten_sleep(0);        /* ASYNCIFY: yield to the browser (see frame_wait) */
#endif
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    c->key = (uint8_t)(k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER]);
#endif
}

/* --trace inside the ending (#113): a wait ran to its end.  Same bookkeeping as
 * trace_boundary_at without the game's fields (the cutscene has none): the
 * line, the picture, then the next token-frame of the script. */
static void cutscene_boundary(Cutscene *c)
{
    App *a = c->user;
    if (!a->trace_f || a->trace_done) return;
    Town *t = &a->sh.town;
    char in[12];
    trace_input_token(in, t->dirs, t->buttons, t->menu_key);
    if (a->trace_char) {
        size_t n = in[0] == '.' ? 0 : strlen(in);
        in[n] = '\''; in[n + 1] = a->trace_char == 0x08 ? '<' : (char)a->trace_char; in[n + 2] = 0;
    }
    trace_write_site(a->trace_f, a->trace_n, a->ending ? "end:wait" : "intro:wait", 0, in);
    if (a->png_every && a->trace_n % a->png_every == 0) {
        static uint8_t rgb[VIDEO_MAX_W * VIDEO_MAX_H * 3];
        int vw, vh;
        cutscene_size(c, &vw, &vh);
        char path[1100];
        snprintf(path, sizeof path, "%s/frame_%05u.png", a->trace_dir, a->trace_n);
        cutscene_to_rgb(c, rgb);
        if (png_write_rgb(path, rgb, vw, vh)) fprintf(stderr, "cannot write %s\n", path);
    }
    a->trace_n++;
    harness_advance(a);
    if (a->trace_done) c->quit = 1;
}

/* GAME.BIN at boot with no command-line argument: `jmp [0x6002]` into opdemo,
 * which hands back with AX = 0xFFFF when the demo has run (opdemo 6A41). */
static int run_cutscenes(App *a, const char *dir)
{
    if (cutscene_init(&a->cs, dir, &a->sh.tfont)) {
        fprintf(stderr, "cannot load gdmcga (ZELRES1[5]): no intro\n");
        return -1;
    }
    if (cutscene_set_video(&a->cs, a->video))
        fprintf(stderr, "cannot read the %s gd tables: the cutscenes stay MCGA\n", video_mode_name(a->video));
    a->cs.user = a; a->cs.present = cutscene_present;
    a->cs.max_frames = a->cs_frames;
    if (a->trace_f && (a->ending || !a->intro_act)) a->cs.boundary = cutscene_boundary;   /* the ending (#113), the opening (#34) */
    if (a->ending) cutscene_ending(&a->cs);
    else if (a->intro_act) {
        size_t len = 0;
        free(a->cs.img);
        a->cs.img = sar_load(dir, 0, 0, 1, &len);       /* ZELRES1[0] opdemo */
        a->cs.imglen = len;
        if (a->cs.img) {
            if (a->intro_act == 1) cutscene_act1(&a->cs);
            else if (a->intro_act == 2) cutscene_act2(&a->cs);
            else cutscene_act3(&a->cs);
        }
        audio_music_stop();
    } else cutscene_intro(&a->cs);
    fprintf(stderr, "[intro] %u frames\n", a->cs.frames);
    cutscene_free(&a->cs);
    return 0;
}

/* ---------------------------------------------------- side pictures (#119) */
/* A loop of the original that waits (timer or CPU delay) and draws without a trace boundary.  With --trace
 * --png-every the screen goes to side_<next boundary>_<k>_<tag>.png, named as the oracle names it
 * (zel_oracle.cpp side_check); in real time it is shown for `ms`.  a->fb holds the screen. */
static void side_show(App *a, const char *tag, unsigned ms)
{
    if (a->trace_f) {
        if (a->png_every && !a->trace_done) {      /* the oracle stops at its last boundary; so does the picture list */
            char path[1100];
            snprintf(path, sizeof path, "%s/side_%05u_%03u_%s.png", a->trace_dir, a->trace_n, a->side_n++, tag);
            fb_write_png(a, path);
        }
        return;
    }
#ifdef HAVE_SDL
    if (a->headless || !ms) return;
    int vw, vh;
    const uint8_t *rgb = video_out(a, &vw, &vh);
    SDL_UpdateTexture(a->tex, NULL, rgb, vw * 3);
    present_frame(a, vw, vh);
    Uint32 until = SDL_GetTicks() + ms;
    while (!a->quit && SDL_GetTicks() < until) {
        pump_events(a, NULL);
#ifdef __EMSCRIPTEN__
        emscripten_sleep(1);
#else
        SDL_Delay(2);
#endif
    }
#else
    (void)tag; (void)ms;
#endif
}

static void side_fight(Game *g, int kind, unsigned arg, unsigned ticks)
{
    App *a = app_of(g->user);
    if (kind == SIDE_SNAP) { fb_fight(a, g); memcpy(a->side_snap, a->fb, sizeof a->fb); g->old_screen = a->side_snap; return; }
    if (!a->trace_f && a->headless) return;
    if (kind == SIDE_FADEIN) {                  /* 4192: 8 passes, 0x10 ticks each, over the card the flashes left up / the old screen */
        if (arg == 0x739A) memcpy(a->fb, a->side_snap, sizeof a->fb);
        else { g->fade_card = 1; fb_fight(a, g); g->fade_card = 0; }
        for (int c = 8; c >= 1; c--) {
            render_fadein_pass(a->fb, g, c);
            side_show(a, "w41D9", 16 * 1000 / 237);
        }
        return;
    }
    fb_fight(a, g);
    char tag[24];
    if (kind == SIDE_WAIT) { snprintf(tag, sizeof tag, "w%04X", arg); side_show(a, tag, ticks * 1000 / 237); }
}
static void side_town(Town *t, int kind, unsigned arg, unsigned ticks)
{
    App *a = app_of(t->user);
    if (!a->trace_f && a->headless) return;
    fb_town(a, t);
    char tag[24];
    if (kind == SIDE_WAIT) { snprintf(tag, sizeof tag, "w%04X", arg); side_show(a, tag, ticks * 1000 / 237); }
}

static void present(Game *g)
{
    App *a = app_of(g->user);
    YIELD_TEST(a);
    a->sk_combo = 0;                    /* a stale save/load combo must not suppress input */
    statekeys_osd_tick(&a->sk);
    audio_advance_ms(a->frame_ms);
    if (a->speed_req) speed_modal(a, g, NULL);          /* kernel [114] at fight.bin 715F */
    if (a->restore_req) restore_modal(a, g, NULL);          /* kernel [11E] at fight.bin 716E */
    if (a->verbose)
        fprintf(stderr, "frame %4u  hero map (%3d,%2d) scr (%2d,%2d) scroll (%3d,%2d) v=%02x anim=%02x flags=%02x "
                "crouch=%02x ladder=%02x dirs=%x hp=%u atk=%02x/%u%s%s\n",
                g->frame_no, game_hero_map_col(g), game_hero_map_row(g), g->hero_scr_col, g->hero_scr_row,
                g->scroll_col, g->scroll_row, g->vstate, g->hero_anim, g->hero_flags, g->crouching, g->on_ladder,
                g->dirs, g->hp, g->attacking, g->attack_var, g->on_hazard ? " HAZARD" : "",
                g->hero_hit ? " HIT" : "");
    if (a->trace_f) {                                /* inputs move only at the boundaries */
        if (g->dsv_pic && a->png_every && !a->trace_done) {   /* #118: the death dissolve (99A8), dsv_<next boundary>_<k>.png */
            char path[1100];
            snprintf(path, sizeof path, "%s/dsv_%05u_%03u.png", a->trace_dir, a->trace_n, a->dsv_n++);
            fb_fight(a, g);
            fb_write_png(a, path);
        }
        if (g->walk_in && a->png_every) {            /* #117: the walk-in (7C6E) has no boundary; its pictures go to */
            char path[1100];                         /* walk_<next boundary>_<k>.png, diffed by pixdiff.py */
            snprintf(path, sizeof path, "%s/walk_%05u_%03u.png", a->trace_dir, a->trace_n, a->walk_n++);
            fb_fight(a, g);
            fb_write_png(a, path);
        }
        if (++a->trace_idle > TRACE_IDLE_MAX && !a->quit) {
            fprintf(stderr, "[trace] %u frames without a boundary (a shop or menu waiting on input?): stopping\n", a->trace_idle);
            a->quit = 1; a->trace_done = 1;
        }
        if (a->quit && g->status) g->status->done = 1;
        return;
    }
    if (a->max_frames && g->frame_no >= a->max_frames) a->quit = 1;
    if (a->quit && g->status) g->status->done = 1;   /* let the status screen out */
    if (a->headless) {
        if (g->dsv_pic) return;                         /* a dissolve pass: the script waits */
        if (a->shot_path && g->frame_no == a->shot_frame) dump_png(a, g);
        if (!script_next(a)) {
            if (!a->shot_path || g->frame_no >= a->shot_frame) a->quit = 1;
            g->dirs = 0; set_buttons(g, 0); g->menu_key = 0;
            if (a->quit && g->status) g->status->done = 1;
        }
        else {
            g->dirs = a->script_dirs;
            set_buttons(g, (uint8_t)((a->script_btns & 3) | (a->script_btns & 8 ? 4 : 0)));
            g->menu_key = (uint8_t)(a->script_btns & 4 ? 1 : 0);
        }
        return;
    }
#ifdef HAVE_SDL
    if (g->status) memcpy(a->fb, status_framebuffer(g->status), FB_W * FB_H);
    else if (g->tear) memcpy(a->fb, tear_framebuffer(g->tear), FB_W * FB_H);
    else render_frame(a->fb, g, &a->sh.hero);
    render_hud(a->fb, g, &a->sh.font, &a->sh.tfont, map_place_record(g->map));
    itemp_hud(a->fb, &a->sh.pics, &a->sh.tfont, g);
    if (!g->tear) tear_draw_slots(a->fb, g, &a->sh.tear_art);   /* GAME.BIN A18E -> A3A5 */
    int vw, vh;
    const uint8_t *rgb = video_out(a, &vw, &vh);
    SDL_UpdateTexture(a->tex, NULL, rgb, vw * 3);
    present_frame(a, vw, vh);
    char title[256];
    snprintf(title, sizeof title, "Zeliard - %s  (%d,%d)  LIFE %u/%u  EXP %u  GOLD %u  %s", g->map->name,
             game_hero_map_col(g), game_hero_map_row(g), g->hp, g->max_hp, g->exp, (unsigned)g->gold, g->message);
    title_paused(a, title);

    /* wait out the frame (4*speed ticks), pumping events */
    static Uint64 next = 0;
    frame_wait(a, g, &next);
    if (g->dsv_pic) return;                             /* a dissolve pass reads no keys */
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    audio_hotkeys(a, k);
    /* Paused: hand the engine a frame of nothing rather than the keys the
     * player is using to talk to the pause screen. */
    if (a->paused) { g->dirs = 0; set_buttons(g, 0); g->menu_key = 0; return; }
    uint8_t d = pad_dirs(a);
    d |= key_dirs(a, k, 0);
    d |= touch_dirs(&a->touch);
    g->dirs = d;
    uint8_t b = pad_btns(a);
    int combo = a->sk_combo;
    b |= key_btns(a, k, 0, combo);
    b |= touch_buttons(&a->touch, SDL_GetTicks());
    set_buttons(g, b);
    g->menu_key = (uint8_t)(key_menu(a, k, 0) || pad_menu(a));   /* FF18 bit0 */
#endif
}

/* ---------------------------------------------------------------- town */
static void town_present(Town *t)
{
    App *a = app_of(t->user);
    YIELD_TEST(a);
    a->sk_combo = 0;                    /* a stale save/load combo must not suppress input */
    statekeys_osd_tick(&a->sk);
    audio_advance_ms(a->frame_ms);
    if (a->speed_req) speed_modal(a, t->g, t);          /* kernel [114] at town.bin 68CD */
    if (a->restore_req) restore_modal(a, t->g, t);          /* kernel [11E] at town.bin 68DC */
    if (a->verbose)
        fprintf(stderr, "town frame %4u  hero map col %3d scr %3d scroll %3d anim %u flags %02x%s%s\n",
                t->frame_no, town_hero_col(t), t->hero_scr_col, t->scroll_col, t->hero_anim, t->hero_flags,
                t->message[0] ? "  msg: " : "", t->message);
    if (a->trace_f) {
        if (t->dsv_pic && a->png_every && !a->trace_done) {   /* #118: [2040]'s passes have no boundary either; the oracle writes */
            char path[1100];                         /* dsv_<next boundary>_<k>.png after each, pixdiff.py diffs them */
            snprintf(path, sizeof path, "%s/dsv_%05u_%03u.png", a->trace_dir, a->trace_n, a->dsv_n++);
            fb_town(a, t);
            fb_write_png(a, path);
        }
        if (++a->trace_idle > TRACE_IDLE_MAX && !a->quit) {
            fprintf(stderr, "[trace] %u frames without a boundary (a shop or menu waiting on input?): stopping\n", a->trace_idle);
            a->quit = 1; a->trace_done = 1;
        }
        if (a->quit) { t->quit = 1; if (t->status) t->status->done = 1; }
        return;
    }
    if (a->max_frames && t->frame_no >= a->max_frames) a->quit = 1;
    if (a->quit && t->status) t->status->done = 1;
    if (a->quit) t->quit = 1;
    if (a->headless) {
        if (t->dsv_pic) return;                         /* a dissolve pass: the script waits */
        if (a->shot_path && t->frame_no == a->shot_frame) {
            fb_town(a, t);
            if (fb_write_png(a, a->shot_path) == 0) fprintf(stderr, "wrote %s (town frame %u, hero col %d)\n", a->shot_path, t->frame_no, town_hero_col(t));
        }
        if (!script_next(a)) {
            if (!a->shot_path || t->frame_no >= a->shot_frame) a->quit = 1;
            t->dirs = 0; t->buttons = 0; t->menu_key = 0;
            if (a->quit) { t->quit = 1; if (t->status) t->status->done = 1; }
        }
        else {
            if ((a->script_btns & 1) && !(t->buttons & 1)) t->btn1_edge = 0xFF;
            if ((a->script_btns & 2) && !(t->buttons & 2)) t->btn2_edge = 0xFF;
            t->dirs = a->script_dirs;
            t->buttons = (uint8_t)((a->script_btns & 3) | (a->script_btns & 8 ? 4 : 0));
            t->menu_key = (uint8_t)(a->script_btns & 4 ? 1 : 0);
            if (a->script_char_new) { t->last_ascii = a->script_char; a->script_char_new = 0; }
        }
        return;
    }
#ifdef HAVE_SDL
    if (t->status) memcpy(a->fb, status_framebuffer(t->status), FB_W * FB_H);
    else if (t->shop) memcpy(a->fb, shop_framebuffer(t->shop), FB_W * FB_H);
    else town_render(a->fb, t);
    render_hud(a->fb, t->g, &a->sh.font, &a->sh.tfont, town_place_record(t->map));
    itemp_hud(a->fb, &a->sh.pics, &a->sh.tfont, t->g);
    tear_draw_slots(a->fb, t->g, &a->sh.tear_art);
    int vw, vh;
    const uint8_t *rgb = video_out(a, &vw, &vh);
    SDL_UpdateTexture(a->tex, NULL, rgb, vw * 3);
    present_frame(a, vw, vh);
    char title[768];
    snprintf(title, sizeof title, "Zeliard - %s  col %d  LIFE %u/%u  GOLD %u  %.400s", t->map->label,
             town_hero_col(t), t->g->hp, t->g->max_hp, (unsigned)t->g->gold, t->message);
    title_paused(a, title);
    static Uint64 next = 0;
    frame_wait(a, NULL, &next);
    if (t->dsv_pic) return;                             /* a dissolve pass reads no keys */
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    audio_hotkeys(a, k);
    if (a->paused) { t->dirs = 0; t->buttons = 0; t->menu_key = 0; return; }
    uint8_t d = pad_dirs(a);
    d |= key_dirs(a, k, 1);
    d |= touch_dirs(&a->touch);
    t->dirs = d;
    uint8_t b = pad_btns(a);
    int combo = a->sk_combo;
    b |= key_btns(a, k, 1, combo);
    b |= touch_buttons(&a->touch, SDL_GetTicks());
    if ((b & 1) && !(t->buttons & 1)) t->btn1_edge = 0xFF;
    if ((b & 2) && !(t->buttons & 2)) t->btn2_edge = 0xFF;
    t->buttons = b;
    t->menu_key = (uint8_t)(key_menu(a, k, 1) || pad_menu(a));   /* FF18 bit0 */
#endif
}

/* ------------------------------------------------------------- the config */
/* #42: the settings file.  SDL_GetPrefPath is the player's data directory
 * ("awkto"/"zeliard"), and its own fallback is a dotfile (config_default_path).
 * Both are pure path computations; no SDL subsystem needs to be up. */
static void config_resolve_path(char *out, size_t n, const char *argv0)
{
#ifdef HAVE_SDL
    char *pref = SDL_GetPrefPath("awkto", "zeliard");
    if (pref) { snprintf(out, n, "%szeliard.ini", pref); SDL_free(pref); return; }
#endif
    config_default_path(out, n, argv0);
}

/* Fold what the run actually used back into the config, so the in-game
 * toggles (F3 filter, F11 fullscreen, F1/F2 audio) survive the exit.  The game
 * folder is only remembered when it was named (--dir) or already known, not
 * when one of the built-in probes happened to find a relative one. */
static void config_capture(Config *c, const App *a, const char *dir, int backend,
                           int remember_dir)
{
    if (remember_dir && dir) snprintf(c->dir, sizeof c->dir, "%s", dir);
    c->scale = a->scale;
    c->fullscreen = a->fullscreen;
    snprintf(c->video, sizeof c->video, "%s", video_mode_name(a->video));
    snprintf(c->filter, sizeof c->filter, "%s", present_filter_name(a->filter));
    snprintf(c->aspect, sizeof c->aspect, "%s", a->square ? "1:1" : "4:3");
    c->music = music_on; c->sfx = sfx_on;
    c->speaker = backend == AUDIO_SPEAKER ? 1 : 0;
    snprintf(c->audio, sizeof c->audio, "%s", audio_backend_key(backend));
}

/* #42 first run: the game files are copyrighted and stay user-supplied, so say
 * so and say where.  SDL builds get a real dialog; headless stays on stderr. */
static void no_game_files(const char *cfgpath, const char *datadir, int headless)
{
    char msg[2048];
    snprintf(msg, sizeof msg,
        "Zeliard's game data files were not found.\n\n"
        "They are copyrighted and are not included with this port.  Put the\n"
        "original files (ZELRES1.SAR, ZELRES2.SAR, ZELRES3.SAR, GAME.BIN, ...)\n"
        "in one of these folders:\n"
        "    zeliard/    ../zeliard/    ./\n"
        "or, for an installed copy, the per-user folder\n    %s\n"
        "or start it with  --dir /path/to/the/files\n\n"
        "The folder that works is remembered in:\n    %s", datadir[0] ? datadir : "(SDL_GetPrefPath)/game", cfgpath);
#ifdef __ANDROID__
    snprintf(msg, sizeof msg,
        "Zeliard's game data files were not found.\n\n"
        "They are copyrighted and are not included with this app.  Copy the\n"
        "original files (ZELRES1.SAR, ZELRES2.SAR, ZELRES3.SAR, GAME.BIN, ...)\n"
        "into this folder on the phone (a file manager or 'adb push'):\n\n    %s\n\n"
        "or long-press the app icon and choose \"Choose game files\" to pick\n"
        "the folder or a .zip.  Then start the app again.", datadir[0] ? datadir : "Android/data/com.awkto.zeliard.port/files/game");
#endif
    fprintf(stderr, "%s\n", msg);
#ifdef HAVE_SDL
    if (!headless && SDL_Init(SDL_INIT_VIDEO) == 0) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Zeliard - game files not found", msg, NULL);
        SDL_Quit();
    }
#else
    (void)headless;
#endif
}

int main(int argc, char **argv)
{
    static App a; memset(&a, 0, sizeof a);
    a.frame_ms = FRAME_MS_DEFAULT; a.speed = SPEED_DEFAULT; a.scale = 3; a.video = VID_MCGA;
    a.filter = FILTER_SHARP;                    /* #53: the fidelity-first default */
    Config cfg; char cfgpath[CONFIG_PATH_MAX];
    config_resolve_path(cfgpath, sizeof cfgpath, argv[0]);
    config_load(&cfg, cfgpath);                 /* #42: absent = all defaults */
#ifdef HAVE_SDL
    input_init(&a.in, &cfg);                    /* #42 step 2: key.* / pad.* over the defaults */
#endif
    Shell *sh = &a.sh;
    const char *dir = NULL;
    int map_idx = 0, pos_col = -1, pos_row = -1, start_in_town = -1, town_col = -1, town_scr = -1, town_anim = -1, town_face = 0, npc_i = -1, npc_f = 0;
    int touch_mode = 0;                         /* #54: --touch auto|on|off (auto = first touch shows it) */
    int dbg_sword = -1, dbg_shield = -1, dbg_level = -1, dbg_life = -1;
    long dbg_gold = -1, dbg_keys = -1, dbg_bank = -1, dbg_worn = -1;
    const char *dbg_potions = NULL, *dbg_spells = NULL, *cave_poke = NULL;
    int dbg_magic_sel = 0, dbg_magic_n = 0;
    const char *save_name = "ZELIARD", *load_name = NULL, *save_now = NULL;
    const char *sync_action = NULL, *sync_key_opt = NULL, *sync_base_opt = NULL;   /* #55 */
    int sync_force = 0;
    const char *pilot_route = NULL, *emit_path = NULL;
    int nflags = 0; uint8_t flag_at[0x4A], flag_val[0x4A];
    a.pilot_tail = 10;
    /* the intro runs on a plain interactive launch, the way GAME.BIN runs
     * opdemo when it was given no command-line argument; anything that names a
     * start state or a scripted/headless run suppresses it. */
    int explicit_start = 0;
    a.intro = -1;
    const char *wav_path = NULL;
    const char *gd_art_name = NULL, *gd_art_path = NULL;
    int want_audio = 1, audio_backend = AUDIO_ADLIB, music_force = -2;
    /* #42: which settings the command line named, so the config can fill in
     * only the rest (a flag always wins). */
    int have_scale = 0, have_fullscreen = 0, have_video = 0, have_filter = 0,
        have_aspect = 0, have_music = 0, have_speaker = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dir") && i + 1 < argc) dir = argv[++i];
        else if (!strcmp(argv[i], "--map") && i + 1 < argc) { map_idx = (int)strtol(argv[++i], NULL, 0); explicit_start = 1; }
        else if (!strcmp(argv[i], "--pos") && i + 2 < argc) { pos_col = atoi(argv[++i]); pos_row = atoi(argv[++i]); explicit_start = 1; }
        else if (!strcmp(argv[i], "--headless")) { a.headless = 1; explicit_start = 1; }
        else if (!strcmp(argv[i], "--screenshot") && i + 2 < argc) { a.shot_frame = (unsigned)atoi(argv[++i]); a.shot_path = argv[++i]; a.headless = 1; explicit_start = 1; }
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) { a.script = argv[++i]; explicit_start = 1; }
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) { a.scale = atoi(argv[++i]); have_scale = 1; }
        /* the scaling filter (F3 cycles it); default sharp-bilinear */
        else if (!strcmp(argv[i], "--filter") && i + 1 < argc) {
            a.filter = present_filter_by_name(argv[++i]);
            if (a.filter < 0) { fprintf(stderr, "--filter: expected integer, sharp, smooth or crt\n"); return 2; }
            have_filter = 1;
        }
        else if (!strcmp(argv[i], "--touch") && i + 1 < argc) {
            touch_mode = touch_mode_by_name(argv[++i]);
            if (touch_mode < 0) { fprintf(stderr, "--touch: expected on, off or auto\n"); return 2; }
        }
        else if (!strcmp(argv[i], "--fullscreen")) { a.fullscreen = 1; have_fullscreen = 1; }
        /* the drivers all drew for a 4:3 monitor, so their pixels are not
         * square; --aspect 1:1 turns the correction off for pixel-peeping
         * against docs/screenshots/, which are native framebuffer grabs */
        else if (!strcmp(argv[i], "--aspect") && i + 1 < argc) {
            const char *v = argv[++i];
            if (!strcmp(v, "1:1") || !strcmp(v, "square")) a.square = 1;
            else if (!strcmp(v, "4:3") || !strcmp(v, "crt")) a.square = 0;
            else { fprintf(stderr, "--aspect: expected 4:3 or 1:1\n"); return 1; }
            have_aspect = 1;
        }
        else if (!strcmp(argv[i], "--video") && i + 1 < argc) {
            a.video = video_mode_by_name(argv[++i]);
            if (a.video < 0) { fprintf(stderr, "unknown --video %s (mcga cga cga2 ega hgc tandy)\n", argv[i]); return 2; }
            have_video = 1;
        }
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) { a.max_frames = (unsigned)atoi(argv[++i]); explicit_start = 1; }
        else if (!strcmp(argv[i], "--speed") && i + 1 < argc) { int v = atoi(argv[++i]); a.speed = v < 1 ? 1 : v > 10 ? 10 : v; a.frame_ms = SPEED_FRAME_MS(a.speed); }
        else if (!strcmp(argv[i], "--town") && i + 1 < argc) { start_in_town = (int)strtol(argv[++i], NULL, 0); explicit_start = 1; }
        else if (!strcmp(argv[i], "--town-col") && i + 1 < argc) town_col = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--town-scr") && i + 1 < argc) town_scr = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--town-npc") && i + 2 < argc) { npc_i = atoi(argv[++i]); npc_f = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--town-anim") && i + 2 < argc) { town_anim = atoi(argv[++i]); town_face = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--sound")) sound_set_log(1);
        else if (!strcmp(argv[i], "--no-music")) { want_audio = 0; have_music = 1; }
        else if (!strcmp(argv[i], "--speaker")) { audio_backend = AUDIO_SPEAKER; have_speaker = 1; }
        else if (!strcmp(argv[i], "--tandy")) { audio_backend = AUDIO_TANDY; have_speaker = 1; }
        else if (!strcmp(argv[i], "--audio") && i + 1 < argc) {
            int b = audio_backend_by_name(argv[++i]);
            if (b < 0) { fprintf(stderr, "unknown --audio %s (adlib speaker tandy)\n", argv[i]); return 2; }
            audio_backend = b; have_speaker = 1;
        }
        else if (!strcmp(argv[i], "--dump-audio") && i + 1 < argc) wav_path = argv[++i];
        else if (!strcmp(argv[i], "--music") && i + 1 < argc) music_force = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--sword") && i + 1 < argc) dbg_sword = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shield") && i + 1 < argc) dbg_shield = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--level") && i + 1 < argc) dbg_level = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--life") && i + 1 < argc) dbg_life = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--gold") && i + 1 < argc) dbg_gold = atol(argv[++i]);
        else if (!strcmp(argv[i], "--bank") && i + 1 < argc) dbg_bank = atol(argv[++i]);
        else if (!strcmp(argv[i], "--keys") && i + 1 < argc) dbg_keys = atol(argv[++i]);
        else if (!strcmp(argv[i], "--worn") && i + 1 < argc) dbg_worn = atol(argv[++i]);
        else if (!strcmp(argv[i], "--potions") && i + 1 < argc) dbg_potions = argv[++i];
        else if (!strcmp(argv[i], "--spells") && i + 1 < argc) dbg_spells = argv[++i];
        else if (!strcmp(argv[i], "--cave") && i + 1 < argc) cave_poke = argv[++i];
        else if (!strcmp(argv[i], "--magic") && i + 1 < argc) {     /* SEL=COUNT: select spell SEL, COUNT charges */
            const char *v = argv[++i];
            dbg_magic_sel = atoi(v); const char *e = strchr(v, '='); dbg_magic_n = e ? atoi(e + 1) : 9;
            if (dbg_magic_sel < 1 || dbg_magic_sel > 7) { fprintf(stderr, "--magic: want SEL[=COUNT], SEL 1..7\n"); return 2; }
        }
        else if (!strcmp(argv[i], "--name") && i + 1 < argc) save_name = argv[++i];
        else if (!strcmp(argv[i], "--load") && i + 1 < argc) { load_name = argv[++i]; explicit_start = 1; }
        else if (!strcmp(argv[i], "--save") && i + 1 < argc) { save_now = argv[++i]; explicit_start = 1; }
        else if (!strcmp(argv[i], "--sync") && i + 1 < argc) sync_action = argv[++i];
        else if (!strcmp(argv[i], "--sync-key") && i + 1 < argc) sync_key_opt = argv[++i];
        else if (!strcmp(argv[i], "--sync-base") && i + 1 < argc) sync_base_opt = argv[++i];
        else if (!strcmp(argv[i], "--force")) sync_force = 1;
        else if (!strcmp(argv[i], "--gd-art") && i + 2 < argc) { gd_art_name = argv[++i]; gd_art_path = argv[++i]; }
        else if (!strcmp(argv[i], "--intro")) a.intro = 1;
        else if (!strcmp(argv[i], "--no-intro")) a.intro = 0;
        else if (!strcmp(argv[i], "--intro-act") && i + 1 < argc) { a.intro_act = atoi(argv[++i]); a.intro = 1; a.intro_only = 1; }
        else if (!strcmp(argv[i], "--ending")) { a.ending = 1; a.intro = 1; a.intro_only = 1; }
        else if (!strcmp(argv[i], "--cutscene-frames") && i + 1 < argc) a.cs_frames = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--yield-test")) a.yield_test = 1;
        else if (!strcmp(argv[i], "--verbose") || !strcmp(argv[i], "-v")) a.verbose = 1;
        else if (!strcmp(argv[i], "--trace") && i + 1 < argc) { a.trace_dir = argv[++i]; a.headless = 1; explicit_start = 1; }
        else if (!strcmp(argv[i], "--state-save-at") && i + 2 < argc) { a.state_save_at = (unsigned)atoi(argv[++i]); a.state_save = argv[++i]; explicit_start = 1; }
        else if (!strcmp(argv[i], "--state-load") && i + 1 < argc) { a.state_load = argv[++i]; explicit_start = 1; }
        else if (!strcmp(argv[i], "--state-dir") && i + 1 < argc) a.state_dir_opt = argv[++i];
        else if (!strcmp(argv[i], "--state-inject") && i + 1 < argc) {
            /* #52 step 2: queue a save/load at trace boundary N, the headless
             * equivalent of pressing the key.  save:N[:slot] / load:N[:slot]. */
            char spec[64]; snprintf(spec, sizeof spec, "%s", argv[++i]);
            char *p = spec, *e = NULL;
            int kind = 0, slot = -1;
            if (!strncmp(p, "save:", 5)) { kind = STATE_REQ_SAVE; p += 5; }
            else if (!strncmp(p, "load:", 5)) { kind = STATE_REQ_LOAD; p += 5; }
            unsigned frame = (unsigned)strtoul(p, &e, 10);
            if (e && *e == ':') slot = (int)strtol(e + 1, NULL, 10);
            if (!kind || !e || e == p || a.n_st_ev >= 8) {
                fprintf(stderr, "--state-inject %s: want save:N[:slot] or load:N[:slot]\n", spec);
                return 2;
            }
            a.st_ev[a.n_st_ev].kind = kind; a.st_ev[a.n_st_ev].frame = frame;
            a.st_ev[a.n_st_ev].slot = slot; a.n_st_ev++;
        }
        else if (!strcmp(argv[i], "--png-every") && i + 1 < argc) a.png_every = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--trace-fields")) { trace_list_fields(stdout); return 0; }
        else if (!strcmp(argv[i], "--flag") && i + 1 < argc) {
            /* the story-flag array BASE:0000-0049 ([49] = Jashiin's end-game flag), e.g.
             * `--flag 12=08`: bsmp's sentry steps aside for the Hero's Crest */
            char *e; const char *v = argv[++i];
            long n = strtol(v, &e, 16), val = 0xFF;
            if (*e == '=') { const char *w = e + 1; val = strtol(w, &e, 16); if (e == w) n = -1; }
            if (*e || e == v || n < 0 || n > 0x49 || val < 0 || val > 0xFF || nflags >= 0x4A) {
                fprintf(stderr, "--flag %s: want N[=V], hex, N 0..49, V 0..FF\n", v); return 2;
            }
            flag_at[nflags] = (uint8_t)n; flag_val[nflags++] = (uint8_t)val;
        }
        else if (!strcmp(argv[i], "--pilot") && i + 1 < argc) { pilot_route = argv[++i]; explicit_start = 1; }
        else if (!strcmp(argv[i], "--pilot-tail") && i + 1 < argc) a.pilot_tail = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--emit-script") && i + 1 < argc) emit_path = argv[++i];
        else { usage(); return 2; }
    }
    /* #42: the config fills in whatever the command line did not name.  It is a
     * player's window/audio preference, so a headless run (--screenshot,
     * --script, --trace: the harness) ignores it -- there only the game folder
     * is taken from it, keeping `make verify` independent of the developer's
     * saved settings. */
    if (!a.headless) {
        if (!have_scale && cfg.scale > 0) a.scale = cfg.scale;
        if (!have_fullscreen && cfg.fullscreen >= 0) a.fullscreen = cfg.fullscreen;
        if (!have_video && cfg.video[0])   { int m = video_mode_by_name(cfg.video); if (m >= 0) a.video = m; }
        if (!have_filter && cfg.filter[0]) { int f = present_filter_by_name(cfg.filter); if (f >= 0) a.filter = f; }
        if (!have_aspect && cfg.aspect[0]) a.square = !strcmp(cfg.aspect, "1:1") || !strcmp(cfg.aspect, "square");
        if (!have_music && cfg.music >= 0) music_on = cfg.music;
        if (cfg.sfx >= 0) sfx_on = cfg.sfx;
        if (!have_speaker && cfg.speaker >= 0) audio_backend = cfg.speaker ? AUDIO_SPEAKER : AUDIO_ADLIB;
        if (!have_speaker && cfg.audio[0]) { int b = audio_backend_by_name(cfg.audio); if (b >= 0) audio_backend = b; }
    }
#ifdef HAVE_SDL
    touch_init(&a.touch, touch_mode, a.headless ? -1 : cfg.touch_size, a.headless ? -1 : cfg.touch_opacity);
#endif
    video_set_current(a.video);
    if (a.intro < 0) a.intro = !explicit_start;
    /* GAME.BIN comes back from the demo and starts a new game in the castle */
    if (a.intro && !a.intro_only && !explicit_start && start_in_town < 0) start_in_town = 0;
    /* #42: resolve the game folder once, so a first run can say what is missing
     * before anything tries to open it.  Order: --dir, the config's remembered
     * folder, then zeliard/ ../zeliard/ ./. */
    shell_set_config_dir(cfg.dir);
    char datadir[CONFIG_PATH_MAX]; datadir[0] = 0;
#ifdef HAVE_SDL
    /* #56: an installed copy (.deb/AppImage) has no useful working directory,
     * so also look in <pref path>/game (~/.local/share/awkto/zeliard/game) */
    if (!a.headless) {
#ifdef __ANDROID__
        /* app-specific external storage: reachable from a file manager / adb
         * push without any storage permission (Android/data/<pkg>/files/game) */
        const char *ext = SDL_AndroidGetExternalStoragePath();
        if (ext) {
            snprintf(datadir, sizeof datadir, "%s/game", ext);
            MKDIR(datadir);
            shell_set_data_dir(datadir);
        }
#else
        char *p = SDL_GetPrefPath("awkto", "zeliard");
        if (p) { snprintf(datadir, sizeof datadir, "%sgame", p); SDL_free(p); shell_set_data_dir(datadir); }
#endif
    }
#endif
    const char *gdir = shell_find_dir(dir);
    if (!gdir) { no_game_files(cfgpath, datadir, a.headless); return 1; }
    /* --sync is a tool run (#55): it reads/writes the .usr files and the sync
     * link record, never the player's config, and exits before any engine start. */
    if (sync_action) return sync_cli(&cfg, cfgpath, gdir, sync_action, sync_force, sync_key_opt, sync_base_opt);
    if (sync_force || sync_key_opt || sync_base_opt) { fprintf(stderr, "--force/--sync-key/--sync-base need --sync status|pull|push\n"); return 2; }
    /* --gd-art NAME OUT.png: render one intro/ending resource the way
     * tools/grp2png.py's render_gd does, so the two decoders can be diffed
     * (port/tools/compare_gdart.py, `make verify`). */
    if (gd_art_name) {
        const GdArt *art = gd_art_find(gd_art_name);
        if (!art) { fprintf(stderr, "unknown gd resource %s\n", gd_art_name); return 2; }
        size_t rlen = 0;
        uint8_t *raw = sar_load(gdir, art->archive, art->res - 1, 1, &rlen);
        if (!raw) { fprintf(stderr, "cannot load %s\n", art->name); return 1; }
        static uint8_t buf[0x30000];
        size_t n;
        if (art->unp == 1)      n = gd_unpack_rle(raw, rlen, buf, sizeof buf);
        else if (art->unp == 2) n = gd_unpack_mask(raw, rlen, buf, sizeof buf, 0);
        else if (art->unp == 3) { n = rlen < sizeof buf ? rlen : sizeof buf; memcpy(buf, raw, n); }
        else                    n = gd_unpack_mask(raw, rlen, buf, sizeof buf, 1);
        free(raw);
        static uint8_t fbb[GD_W * GD_H], scr[GD_SCRATCH];
        Gd gd;
        if (gd_init(&gd, gdir, fbb, scr, NULL)) { fprintf(stderr, "no gdmcga\n"); return 1; }
        gd_set_palette(&gd, art->pal);
        int w = 0, h = 0;
        for (int i = 0; i < art->nparts; i++) {
            if (art->part[i].wbytes * 4 > w) w = art->part[i].wbytes * 4;
            h += (art->part[i].rows + 1) * art->part[i].count;
        }
        h -= 1;
        /* the same background grp2png leaves between sub-images */
        uint8_t *rgb = malloc((size_t)w * h * 3);
        for (int i = 0; i < w * h; i++) { rgb[i*3] = 20; rgb[i*3+1] = 20; rgb[i*3+2] = 60; }
        static uint8_t rowbuf[0x20000];
        int y0 = 0;
        for (int i = 0; i < art->nparts; i++)
            for (int f = 0; f < art->part[i].count; f++) {
                gd_art_rows(buf, art->part[i].off + (unsigned)f * art->part[i].stride,
                            art->part[i].wbytes, art->part[i].rows, art->part[i].mode, rowbuf);
                for (int r = 0; r < art->part[i].rows; r++)
                    for (int x = 0; x < art->part[i].wbytes * 4; x++) {
                        const uint8_t *cc = gd.dac[rowbuf[(size_t)r * art->part[i].wbytes * 4 + x]];
                        uint8_t *p = rgb + (((size_t)(y0 + r) * w) + x) * 3;
                        p[0] = cc[0]; p[1] = cc[1]; p[2] = cc[2];
                    }
                y0 += art->part[i].rows + 1;
            }
        int rc = png_write_rgb(gd_art_path, rgb, w, h);
        fprintf(stderr, "%s: %zu bytes unpacked, %dx%d -> %s\n", art->name, n, w, h, gd_art_path);
        free(rgb); gd_free(&gd);
        return rc ? 1 : 0;
    }
    sh->user = &a; sh->present = present; sh->town_present = town_present; sh->side = side_fight; sh->town_side = side_town;
    if (a.trace_dir) {
        if (MKDIR(a.trace_dir) && errno != EEXIST) { fprintf(stderr, "cannot create %s\n", a.trace_dir); return 1; }
        char path[1100];
        snprintf(path, sizeof path, "%s/frames.jsonl", a.trace_dir);
        a.trace_f = fopen(path, "w");
        if (!a.trace_f) { fprintf(stderr, "cannot write %s\n", path); return 1; }
        sh->frame_end = trace_fight_end; sh->town_frame_end = trace_town_end;
        wait_clock.fn = trace_wait;
    }
    if (a.state_save && !a.trace_f) { fprintf(stderr, "--state-save-at needs --trace DIR\n"); return 2; }
    if (a.n_st_ev && !a.trace_f) { fprintf(stderr, "--state-inject needs --trace DIR\n"); return 2; }
    /* #52 step 2: the frame_done hook runs at the true end of every frame; it
     * writes a --state-save-at blob (step 1) and any queued interactive save or
     * load (the keys, or --state-inject).  Installed whichever front end this is. */
    sh->frame_done = state_frame_done;
    /* audio before shell_init: loading a map starts its score (shell.c).  A
     * headless run without --dump-audio never opens it, so `make verify` and
     * the scripted runs stay silent and do no extra work. */
    if (want_audio && (!a.headless || wav_path)) {
        audio_init(gdir, audio_backend, wav_path == NULL && !a.headless);
        if (!music_on) audio_music_enable(0);       /* #42: the config's F1 state */
        if (!sfx_on)   audio_sfx_enable(0);         /* ... and F2 */
        if (wav_path) {
            if (audio_dump_open(wav_path)) { fprintf(stderr, "cannot write %s\n", wav_path); return 1; }
            a.dump_audio = 1;
            fprintf(stderr, "[audio] %s -> %s (%.1f ms per frame)\n", audio_backend_name(), wav_path, a.frame_ms);
        }
    }
    /* GAME.BIN A080: given no command-line argument the boot path jumps
     * straight into opdemo, *before* it reads any level record, which is why
     * the prologue is silent until the title starts zopn.msd (docs/CUTSCENES.md
     * §2).  The port has to build its scaffold Game first, and the map record's
     * score (shell_load_enemy_banks -> fight.bin 7E93) would then play over the
     * demo (#37) -- hold it until the real level entry below. */
    audio_music_hold(1);
    /* #55 step 2: save sync, interactive SDL runs only (a --script/--trace/--save/
     * --screenshot/harness run never syncs and never shows the dialog).  The pull
     * comes before shell_init, so nothing reads the game folder first. */
    {
        int sl_want = 0;
        const SyncRunner *sl_run = NULL;
#ifdef HAVE_SDL
        sl_want = !a.headless && !a.script && !save_now && !a.trace_dir && !pilot_route
                  && !a.state_save && !a.state_load && !a.n_st_ev;
        sl_run = &sync_runner;
#endif
        if (synclife_begin(&g_sl, sl_want, &cfg, cfgpath, gdir, sl_run) != SL_OFF) player_usr_saved_hook = on_usr_saved;
    }
    if (shell_init(sh, gdir, map_idx)) return 1;
    Game *g = &sh->g;
    if (a.trace_f) g->harness_rng = 1;                                  /* HARNESS §3: no +20 stir */

    snprintf(g->player_name, sizeof g->player_name, "%s", save_name);    /* FF6C..FF73 */
    if (load_name && player_load_usr(g, sh->dir, load_name) == 0) {      /* town.bin 7592 restore_game */
        /* 7695 leaves the chosen file's name in FF6C (75C6 builds NAME.USR
         * from it, NUL-padded to 8): DOS's directory listing gives it upper
         * case.  Any later "Record Experience" writes back to that name. */
        const char *b = load_name;
        for (const char *c = load_name; *c; c++) if (*c == '/' || *c == '\\') b = c + 1;
        memset(g->player_name, 0, sizeof g->player_name);
        for (int i = 0; i < 8 && b[i] && b[i] != '.'; i++)
            g->player_name[i] = (char)toupper((unsigned char)b[i]);
        fprintf(stderr, "[save] %s.usr restored: LIFE %u/%u, level %u, EXP %u, GOLD %u\n",
                load_name, g->hp, g->max_hp, g->level, g->exp, (unsigned)g->gold);
    }
    else if (load_name) fprintf(stderr, "[save] cannot read %s.usr\n", load_name);
    /* town.bin 7592 restarts GAME.BIN, which loads the map [C4] names (A1CB)
     * and jumps into town.bin's boot entry: a restore always resumes in the
     * town the page records, at the column it records. */
    int restored_town = (load_name && start_in_town < 0 && (g->page[0xC4] & 0x80)) ? (g->page[0xC4] & 0x7F) : -1;
    if (restored_town >= 0) start_in_town = restored_town;
    if (dbg_sword >= 0)  g->sword = (uint8_t)dbg_sword;                  /* [92] */
    if (dbg_shield >= 1 && dbg_shield <= 6) {                            /* [93]/[94]/[96] */
        g->shield = (uint8_t)dbg_shield;
        g->shield_hp = SHIELD_HP[dbg_shield - 1];                        /* armrpro A6BF */
        g->page[P_SHIELD_MAX] = (uint8_t)g->shield_hp;
        g->page[P_SHIELD_MAX + 1] = (uint8_t)(g->shield_hp >> 8);
    } else if (dbg_shield == 0) g->shield = 0;
    if (dbg_level >= 0)  g->level = (uint8_t)dbg_level;                  /* [8D] */
    if (dbg_life >= 0)   { g->max_hp = (uint16_t)dbg_life; g->hp = g->max_hp; }   /* [B2]/[90] */
    if (dbg_gold >= 0)   g->gold = (uint32_t)dbg_gold;                   /* [85..87] */
    if (dbg_bank >= 0)   page_set_gold24(g->page, P_BANK_HI, (uint32_t)dbg_bank & 0xFFFFFF);   /* [88..8A] */
    if (dbg_keys >= 0)   g->keys = (uint8_t)dbg_keys;                    /* [98] */
    if (dbg_worn >= 1 && dbg_worn <= 5) {                               /* [9E] worn, [A1..A5] the bag */
        int k, have = 0;
        g->shoes = (uint8_t)dbg_worn;
        for (k = 0; k < 5; k++) if (g->page[0xA1 + k] == dbg_worn) have = 1;
        for (k = 0; k < 5 && !have; k++) if (!g->page[0xA1 + k]) { g->page[0xA1 + k] = (uint8_t)dbg_worn; have = 1; }
    }
    if (dbg_potions) {                                                  /* [A6..AA] = drug id + 1 */
        int n = 0;
        for (const char *c = dbg_potions; *c && n < 5; ) {
            if (*c >= '0' && *c <= '9') { g->page[0xA6 + n++] = (uint8_t)(*c - '0' + 1); }
            while (*c && *c != ',') c++;
            if (*c) c++;
        }
    }
    if (dbg_spells) {                                                   /* [BB..C1] */
        for (const char *c = dbg_spells; *c; ) {
            if (*c >= '1' && *c <= '7') g->page[0xBB + (*c - '1')] = 0xFF;
            while (*c && *c != ',') c++;
            if (*c) c++;
        }
    }
    if (dbg_magic_sel) {                                                /* [9D] selected, [AB..], [B4..], learned [BB..] */
        int k = dbg_magic_sel - 1;
        g->magic_sel = (uint8_t)dbg_magic_sel; g->page[0x9D] = (uint8_t)dbg_magic_sel;
        g->magic_count[k] = g->magic_max[k] = (uint8_t)dbg_magic_n;
        g->page[0xAB + k] = g->page[0xB4 + k] = (uint8_t)dbg_magic_n; g->page[0xBB + k] = 0xFF;
    }
    for (int f = 0; f < nflags; f++) {                                   /* --flag */
        g->page[flag_at[f]] = flag_val[f];
        if (flag_at[f] == 0x49) g->jashiin_defeated = flag_val[f];
    }
    player_page_push(g);
    shell_load_enemy_banks(sh, &sh->maps[0]);
    int explicit_pos = pos_col >= 0;
    if (pos_col < 0) {
        if (map_idx == 0) { pos_col = 61; pos_row = 7; }                 /* mrmp.mdt cavern entry (61,7): the MURALLA door */
        else if (sh->maps[0].start_col != 0xFFFF) { pos_col = sh->maps[0].start_col; pos_row = sh->maps[0].start_row; }
        else { pos_col = 16; pos_row = sh->maps[0].row_bias; }
    }
    game_place(g, pos_col, pos_row, 0);
    if (g->boss_room && !explicit_pos) game_boss_room_intro(g);          /* 61A8 */
    fprintf(stderr, "%s: cavern %d, %d columns, tileset MPP%c; hero at (%d,%d), scroll (%d,%d)\n", sh->maps[0].name,
            sh->maps[0].cavern, sh->maps[0].width, "123456789AB"[sh->maps[0].tileset], game_hero_map_col(g),
            game_hero_map_row(g), g->scroll_col, g->scroll_row);

#ifdef HAVE_SDL
    if (!a.headless) {
#ifdef __ANDROID__
        SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");   /* Back pauses (SDLK_AC_BACK) instead of closing the app */
#endif
        if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
        /* a controller is a nicety, not a requirement: if the subsystem will
         * not start, the keyboard path is untouched */
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0)
            for (int j = 0; j < SDL_NumJoysticks(); j++) pad_open(&a, j);
        int vw, vh, vscale = a.scale;
        video_size(a.video, &vw, &vh);
        /* the 640- and 720-px-wide modes are already twice as wide as MCGA, so
         * --scale keeps meaning "how big is a game pixel", not "how big is the
         * window": halve it for them so the default 3 is not a 2160-px window */
        if (vw > FB_W && vscale > 1) vscale = (vscale + 1) / 2;
        /* Present at the shape the driver's monitor had, not at the shape of
         * its framebuffer.  320x200 on a 4:3 screen was 1:1.2 pixels, so a 1:1
         * blit -- which is what this did until now, and for every mode at
         * 320x200's logical size at that -- is about 20% too wide.  SDL
         * letterboxes the difference itself.  The framebuffer is untouched:
         * `make verify` renders headlessly through video_to_rgb and never
         * reaches this path. */
        int lw = vw, lh = vh;
        if (!a.square) video_display_size(a.video, &lw, &lh);
        Uint32 wflags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
#ifdef __EMSCRIPTEN__
        wflags &= ~(Uint32)SDL_WINDOW_RESIZABLE;     /* the page scales the canvas with CSS; SDL would resize its buffer to the browser window */
#endif
        if (a.fullscreen) wflags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
        a.win = SDL_CreateWindow("Zeliard port", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 lw * vscale, lh * vscale, wflags);
#ifdef __EMSCRIPTEN__
        a.ren = SDL_CreateRenderer(a.win, -1, SDL_RENDERER_ACCELERATED);    /* no vsync: frame_wait paces, ASYNCIFY yields */
#else
        a.ren = SDL_CreateRenderer(a.win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
#endif
        if (!a.ren) a.ren = SDL_CreateRenderer(a.win, -1, 0);
        /* no SDL_RenderSetLogicalSize: present.c computes the letterbox rect
         * itself so the integer and sharp-bilinear filters can differ (#53) */
        a.tex = SDL_CreateTexture(a.ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, vw, vh);
    }
#else
    if (!a.headless) { fprintf(stderr, "built without SDL2: running headless (use --screenshot / --script)\n"); a.headless = 1; }
#endif
    /* #52 step 2: where the slot files live.  SDL builds use the player's data
     * dir (SDL_GetPrefPath("awkto","zeliard")); a headless build, or one where
     * SDL cannot name it, falls back to a states/ folder beside the game files.
     * --state-dir overrides both (the tests use it). */
    {
        char pref[CONFIG_PATH_MAX]; pref[0] = 0;
#ifdef HAVE_SDL
        if (!a.headless) {
            char *p = SDL_GetPrefPath("awkto", "zeliard");
            if (p) { snprintf(pref, sizeof pref, "%s", p); SDL_free(p); }
        }
#endif
        /* only touch the filesystem when save/load is actually in play: a plain
         * headless run (make verify, a scenario) must not create directories */
        if (!a.headless || a.state_dir_opt || a.n_st_ev) {
            if (statekeys_dir(&a.sk, a.state_dir_opt, pref[0] ? pref : NULL, sh->dir) != 0)
                fprintf(stderr, "[state] no states directory (save/load disabled)\n");
            else if (a.verbose)
                fprintf(stderr, "[state] states directory: %s\n", a.sk.dir);
        }
    }
#ifdef HAVE_SDL
    if (g_sl.state == SL_ASK) {          /* the key is unlinked and both sides have a save: ask, before the town loads */
        synclife_resolve(&g_sl, sync_ask_modal(&a));
        if (g_sl.state == SL_OFF) player_usr_saved_hook = NULL;
    }
#endif
    if (a.headless && a.shot_path && a.shot_frame == 0) a.shot_frame = 1;
    if (music_force >= -1) audio_music_force(music_force);     /* --music N: pin one score (-1 = effects only) */

    /* GAME.BIN A080: no command-line argument -> [FF77] = 0xFF and jmp into
     * opdemo, which reloads GAME.BIN when it is done (opdemo 6A41). */
    if (a.intro && !save_now) {
        run_cutscenes(&a, sh->dir);
        if (a.trace_f && !a.intro_only) a.intro_only = 1;       /* --intro --trace: the opening is the whole trace (#34) */
        if (a.intro_only) { audio_shutdown(); shell_free(sh);
            if (a.trace_f) { fclose(a.trace_f); fprintf(stderr, "[trace] %u frames -> %s/frames.jsonl\n", a.trace_n, a.trace_dir); }
            /* --intro-act is a tool run: it never writes the player's config */
#ifdef HAVE_SDL
            present_shutdown();
            if (a.tex) SDL_DestroyTexture(a.tex);
            if (a.ren) SDL_DestroyRenderer(a.ren);
            if (a.win) SDL_DestroyWindow(a.win);
            if (!a.headless) SDL_Quit();
#endif
            return 0; }
        a.quit = 0; a.shot_path = a.intro_act ? a.shot_path : NULL;
    }

    /* the demo has handed back (opdemo 6A41): from here on a level entry may
     * start its score, exactly as fight.bin 6085/60D8 and town.bin 60A9 do. */
    audio_music_hold(0);

    if (start_in_town >= 0) {
        if (!shell_enter_town(g, start_in_town, town_col, 0)) return 1;   /* -> audio_music */
        if (cave_poke) {                           /* harness start option, the oracle's ORACLE_CAVE */
            long v[5]; char *e = NULL; const char *q = cave_poke; int n = 0;
            for (; n < 5; n++) { v[n] = strtol(q, &e, 0); if (e == q) break; q = (*e == ',') ? e + 1 : e; }
            TownMap *tm = &sh->tmap;
            if (n != 5 || v[0] < 0 || v[0] >= tm->ncaves || !tm->raw) {
                fprintf(stderr, "--cave: want I,COL,ROW,SIDE,MAP naming one of this town's %d cave records\n", tm->ncaves);
                return 2;
            }
            TownCave *c = &tm->caves[v[0]];
            c->col = (uint16_t)v[1]; c->row = (uint8_t)v[2]; c->side = (uint8_t)v[3]; c->map = (uint8_t)v[4];
            size_t o = (size_t)((tm->raw[0xB] | tm->raw[0xC] << 8) - 0xC000) + 5 * (size_t)v[0];   /* the C000 image too */
            if (o + 5 <= tm->rawlen) {
                tm->raw[o] = (uint8_t)v[1]; tm->raw[o + 1] = (uint8_t)(v[1] >> 8); tm->raw[o + 2] = (uint8_t)v[2];
                tm->raw[o + 3] = (uint8_t)v[3]; tm->raw[o + 4] = (uint8_t)v[4];
            }
            fprintf(stderr, "[cave] record %ld := col %ld row %ld side %ld map %02lX\n", v[0], v[1], v[2], v[3], v[4]);
        }
        if (town_scr >= 0) {                       /* exact scroll/hero placement for make verify */
            sh->town.scroll_col = town_scr;
            /* the Muralla captures --town-scr reproduces were all reached by
             * holding Right from Felishika's Castle, and the backdrop painter
             * last ran there, at the castle's own entry scroll of 30 (STDPLY
             * [80]).  So the strips have been rotated once per column for the
             * 48 columns of cmap left of its right edge (114 - 0x24 - 30) plus
             * `town_scr` columns of Muralla -- town.c's blit_strip. */
            sh->town.back_steps = 48 + town_scr;
            if (town_col >= 0) sh->town.hero_scr_col = town_col - 4 - town_scr;
            town_npc_markers_reset(&sh->town);
        }
        /* GAME.BIN A1CB jumps into town.bin at 601E with the page already
         * holding the hero's town position -- [80] scroll_col, [83]
         * hero_scr_col -- which is STDPLY.BIN's 30 / 10 on a new game and the
         * saved pair after an F7 restore.  The map's own C013 start column is
         * the *death* return (99F4), not this one.  The page only speaks for
         * the town its [C4] names (0x80 = Felishika's Castle in STDPLY), so
         * `--town N` for any other town still uses that map's C013. */
        if (town_col < 0 && town_scr < 0 && (g->page[0xC4] & 0x80)
            && (g->page[0xC4] & 0x7F) == start_in_town) {
            town_page_pull(&sh->town);
            fprintf(stderr, "[town] the page places the hero at column %d (scroll %d)\n",
                    town_hero_col(&sh->town), sh->town.scroll_col);
        }
        if (town_anim >= 0) { sh->town.hero_anim = (uint8_t)town_anim; sh->town.hero_flags = (uint8_t)town_face; }
        if (npc_i >= 0 && npc_i < sh->tmap.nnpcs) {
            sh->tmap.npcs[npc_i].anim = (uint8_t)(npc_f & 3);
            sh->tmap.npcs[npc_i].sprite = (uint8_t)(((npc_f >> 3) & 7) | ((npc_f & 4) ? 0 : 0x80));
            sh->tmap.npcs[npc_i].type = 7;
        }
    } else {
        audio_music((sh->maps[0].lvl_flags >> 1) & 0x0F);        /* fight.bin 7E93 */
        game_first_frame(g);
    }
    if (save_now) {                     /* kenjpro A862 without walking to a Sage */
        if (sh->in_town) town_page_push(&sh->town); else player_page_push(g);
        if (player_save_usr(g, sh->dir, save_now)) { fprintf(stderr, "[save] cannot write %s.usr\n", save_now); return 1; }
        fprintf(stderr, "[save] %s/%s.usr: map %02X, town column %d, LIFE %u/%u, level %u, EXP %u, GOLD %u\n",
                sh->dir, save_now, g->page[0xC4], sh->in_town ? town_hero_col(&sh->town) : -1,
                g->hp, g->max_hp, g->level, g->exp, (unsigned)g->gold);
        /* --save is a tool run (scenario start saves): it never writes the player's config */
        audio_shutdown();
        shell_free(sh);
        return 0;
    }
    static Play pilot;                  /* --pilot: big (the nav graph), not on the stack */
    static PStep pilot_steps[64];
    if (pilot_route) {
        char err[128];
        if (!a.trace_f) { fprintf(stderr, "--pilot needs --trace DIR (it drives the harness boundaries)\n"); return 2; }
        if (a.script) { fprintf(stderr, "--pilot and --script are exclusive\n"); return 2; }
        if (play_parse_route(pilot_route, pilot_steps, 64, err, sizeof err) < 0) { fprintf(stderr, "--pilot: %s\n", err); return 2; }
        pilot.verbose = a.verbose;
        if (play_attach(&pilot, sh, pilot_steps, 0) < 0) { fprintf(stderr, "[pilot] FAILED: %s\n", pilot.msg); return 1; }
        a.pilot = &pilot;
        if (emit_path && !(a.emit_f = fopen(emit_path, "w"))) { fprintf(stderr, "cannot write %s\n", emit_path); return 1; }
    }
    /* --state-load: rebuild the saved engine state, continue the trace at N+1
     * and apply the boundary's own token before the first continued frame. */
    if (a.state_load) {
        if (!a.trace_f) { fprintf(stderr, "--state-load needs --trace (it continues the trace numbering)\n"); return 2; }
        SavestateMeta meta;
        if (savestate_load(sh, a.state_load, &meta)) return 1;
        a.trace_n = meta.frame + 1;
        a.rng_mark = meta.rng_calls;
        fprintf(stderr, "[state] loaded %s: continues at frame %u\n", a.state_load, a.trace_n);
        harness_advance(&a);
    }
    while (!a.quit) {
        shell_frame(sh);
        if (shop_exit_request) a.quit = 1;      /* sage "No" (kenjpro A934) / the ending (omoypro A0A9) */
    }
    if (shop_exit_request == SHOP_EXIT_ENDING && !(a.trace_f && a.trace_done)) {      /* enddemo.bin, never to return */
        a.quit = 0; a.ending = 1;
        run_cutscenes(&a, sh->dir);
    }
    int rc = 0;
    if (a.pilot) {                      /* --pilot / --emit-script */
        emit_flush(&a);
        if (a.emit_f) { fputc('\n', a.emit_f); fclose(a.emit_f); }
        if (a.pilot_state != 1) {
            if (a.pilot_state == 0) fprintf(stderr, "[pilot] stopped before the route was done (step %d)\n", pilot.step);
            rc = 1;
        } else if (a.emit_f) fprintf(stderr, "[pilot] %u token-frames -> %s\n", a.trace_n - 1, emit_path);
    }

    if (a.trace_f && shop_exit_request == SHOP_EXIT_GAME && !a.trace_done) {
        /* the game exited to DOS (INT 21h/4Ch, AL 0) in the frame being played:
         * the oracle's last line; the input is the token that frame was played with */
        Town *tw = &a.sh.town;
        char in[12];
        trace_input_token(in, tw->dirs, tw->buttons, tw->menu_key);
        if (a.trace_char) {
            size_t n = in[0] == '.' ? 0 : strlen(in);
            in[n] = '\''; in[n + 1] = a.trace_char == 0x08 ? '<' : (char)a.trace_char; in[n + 2] = 0;
        }
        trace_write_exit(a.trace_f, a.trace_n, g->rng_calls - a.rng_mark, in, 0);
        a.trace_n++;
    }
    if (a.trace_f) {
        fclose(a.trace_f);
        fprintf(stderr, "[trace] %u frames -> %s/frames.jsonl\n", a.trace_n, a.trace_dir);
    }
    fprintf(stderr, "stopped after %u frames: hero map (%d,%d), LIFE %u/%u, EXP %u, GOLD %u, %u hazard frames, %u deaths\n",
            g->frame_no, game_hero_map_col(g), game_hero_map_row(g), g->hp, g->max_hp, g->exp, (unsigned)g->gold,
            g->hazard_frames, g->deaths);
    synclife_finish(&g_sl);             /* #55: wait for a push in flight, then push what is still unsent */
    if (!a.headless) { config_capture(&cfg, &a, sh->dir, audio_backend, dir != NULL || cfg.dir[0] != 0); config_save(&cfg); }
    audio_shutdown();
    shell_free(sh);
#ifdef HAVE_SDL
    present_shutdown();
    if (a.tex) SDL_DestroyTexture(a.tex);
    if (a.ren) SDL_DestroyRenderer(a.ren);
    if (a.win) SDL_DestroyWindow(a.win);
    if (!a.headless) SDL_Quit();
#endif
    return rc;
}
