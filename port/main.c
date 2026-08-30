/* main.c — SDL2 shell (or headless PNG dumper) for the Zeliard port scaffold. */
#include <stdio.h>
#include <stdlib.h>
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
#ifdef HAVE_SDL
#include <SDL.h>
#endif

#define FRAME_MS_DEFAULT 84.5           /* 20 ticks of 236.7 Hz at speed 5 */

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
    double   frame_ms;
    int      scale;
    int      square;                /* --aspect 1:1: present the framebuffer unscaled in shape */
    int      quit;
    int      verbose;
    int      dump_audio;
    /* the cutscenes (opdemo / enddemo).  `intro` = -1 auto (a plain interactive
     * launch), 0 never, 1 always; `intro_act` runs one act on its own. */
    int      intro, intro_act, ending, intro_only;
    int      video;                     /* --video: which of the five drivers renders */
    unsigned cs_frames;
    Cutscene cs;
#ifdef HAVE_SDL
    SDL_Window *win; SDL_Renderer *ren; SDL_Texture *tex;
    SDL_GameController *pad;            /* controller 0, opened on the fly */
    int      fullscreen;
    int      paused;                    /* Esc: the original pauses, it does not quit */
    int      esc_edge;                  /* Esc was pressed; what that means is the caller's */
    char     title_base[900];           /* the engine's own title, re-applied when pause flips */
#endif
} App;

#ifdef HAVE_SDL
static int f1_was, f2_was, music_on = 1, sfx_on = 1;   /* the F1 / F2 hotkeys */

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
        case SDL_CONTROLLERDEVICEADDED:   pad_open(a, e.cdevice.which); break;
        case SDL_CONTROLLERDEVICEREMOVED: pad_closed(a, e.cdevice.which); break;
        case SDL_CONTROLLERBUTTONDOWN:
            if (e.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) { a->paused = !a->paused; title_apply(a); }
            break;
        case SDL_KEYDOWN:
            switch (e.key.keysym.sym) {
            /* Esc used to quit outright, with no confirmation -- one reflex
             * keypress and the session was gone, which is neither what the
             * original does nor what anyone expects.  What it *should* do
             * differs by screen, so record the press and let the caller say:
             * the engines pause, a cutscene skips. */
            case SDLK_ESCAPE: a->esc_edge = 1; break;
            case SDLK_q:      if (a->paused) a->quit = 1; break;
            case SDLK_F11:    toggle_fullscreen(a); break;
            case SDLK_RETURN: case SDLK_KP_ENTER:
                if (e.key.keysym.mod & KMOD_ALT) toggle_fullscreen(a);   /* Alt+Enter */
                else if (a->paused) { a->paused = 0; title_apply(a); }
                break;
            case SDLK_F12:    if (a->shot_path && g) dump_png(a, g); break;
            default: break;
            }
            break;
        default: break;
        }
    }
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
    for (;;) {
        pump_events(a, g);
        if (a->esc_edge) { a->esc_edge = 0; a->paused = !a->paused; title_apply(a); }
        if (a->quit) break;
        if (a->paused) { SDL_Delay(16); *next = SDL_GetPerformanceCounter(); continue; }
        now = SDL_GetPerformanceCounter();
        if (now >= *next) break;
        Uint64 rem = (*next - now) * 1000 / freq;
        SDL_Delay(rem > 2 ? 1 : 0);
    }
}

/* The analogue stick counts as a direction past half deflection. */
#define PAD_DEAD 16000
static uint8_t pad_dirs(App *a)
{
    if (!a->pad) return 0;
    SDL_GameController *c = a->pad;
    uint8_t d = 0;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_UP)    ||
        SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_A)          ||   /* A jumps */
        SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY) < -PAD_DEAD) d |= DIR_UP;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_DOWN)  ||
        SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY) >  PAD_DEAD) d |= DIR_DOWN;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_LEFT)  ||
        SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX) < -PAD_DEAD) d |= DIR_LEFT;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) ||
        SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX) >  PAD_DEAD) d |= DIR_RIGHT;
    return d;
}
static uint8_t pad_btns(App *a)
{
    if (!a->pad) return 0;
    uint8_t b = 0;
    if (SDL_GameControllerGetButton(a->pad, SDL_CONTROLLER_BUTTON_X)) b |= 1;   /* sword */
    if (SDL_GameControllerGetButton(a->pad, SDL_CONTROLLER_BUTTON_Y)) b |= 2;   /* magic */
    return b;
}
static int pad_menu(App *a)
{
    return a->pad && SDL_GameControllerGetButton(a->pad, SDL_CONTROLLER_BUTTON_START);
}

/* F1 / F2, STICK.BIN's own music and sound-effect hotkeys */
static void audio_hotkeys(const Uint8 *k)
{
    int f1 = k[SDL_SCANCODE_F1], f2 = k[SDL_SCANCODE_F2];
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
        "               [--screenshot N out.png] [--script SCRIPT] [--scale N] [--aspect A] [--fullscreen] [--speed N]\n"
        "               [--frames N] [--sound] [--verbose]\n"
        "               [--no-music] [--speaker] [--music N] [--dump-audio FILE.wav]\n"
        "  --dir       directory holding ZELRES1-3.SAR (default: zeliard/, ../zeliard/)\n"
        "  --map N     system map index (0 = MP10 .. 0x1E = MPA0; default 0)\n"
        "  --pos C R   hero top-left map cell (default: the MURALLA door in MP10, 61 7)\n"
        "  --town N    start in town N (0 cmap .. 9 esmp; 1 = Muralla) instead of a cavern\n"
        "  --town-col C / --town-scr S / --town-anim F FACE / --town-npc I F\n"
        "              exact town placement, used by `make verify`\n"
        "  --screenshot N FILE   dump the framebuffer after N rendered frames (implies --headless)\n"
        "  --script S  headless input: tokens like R10 (hold Right 10 frames), UR2, D3, .5 (idle 5),\n"
        "              letters U D L R, X (sword), M (magic), E (Enter = status screen) and\n"
        "              K (the LEVEL/EXP chord),\n"
        "              separated by spaces/commas\n"
        "  --speed N   FF33 speed (frame = 4*N ticks; default 5 = 84.5 ms)\n"
        "  --scale N   window scale (default 3)\n"
        "  --aspect A  4:3 (default: the shape the driver's monitor had) or 1:1\n"
        "  --fullscreen  start full-screen (F11 or Alt+Enter toggles; Esc pauses, Q quits)\n"
        "  --video M   render through one of the five original drivers:\n"
        "              mcga (default) cga cga2 ega hgc tandy — the RESOURCE.CFG\n"
        "              `videoDrv` set, at each driver's own screen size\n"
        "  --frames N  quit after N rendered frames (SDL and headless)\n"
        "  --sound     log every FF75 sound request the engine produces\n"
        "  --sword N --shield N --level N --life N --gold N\n"
        "              set the player record directly (the shops do it properly)\n"
        "  --potions L comma list of drug ids 0..7 into the five [A6..AA] slots (Enter -> USE:)\n"
        "  --spells L  comma list of spell numbers 1..7 to mark learned at [BB..C1]\n"
        "  --name NAME the NAME.USR the sage's \"Record Experience\" writes\n"
        "  --load NAME restore NAME.USR (town.bin 7592) before starting; the page's\n"
        "              [C4] picks the town and [80]/[83] the column\n"
        "  --save NAME write NAME.usr for the state just set up and exit (kenjpro A862)\n"
        "  --intro / --no-intro   run (or skip) the opening demo; the default is to run it\n"
        "              only on a plain interactive launch, as GAME.BIN does when it was\n"
        "              given no command-line argument\n"
        "  --intro-act N          run only act N of opdemo (1 prologue+title, 2 credits,\n"
        "              3 the storm demo) and exit\n"
        "  --ending    run enddemo (the ending and the credits roll) and exit\n"
        "  --cutscene-frames N    stop a cutscene after N rendered frames\n"
        "  --gd-art NAME OUT.png  render one intro/ending resource (ame.grp, ttl3.grp, ...)\n"
        "              exactly as tools/grp2png.py's render_gd does\n");
}

static int script_next(App *a)
{
    while (a->script_left == 0) {
        const char *s = a->script;
        if (!s) return 0;
        while (s[a->script_pos] == ' ' || s[a->script_pos] == ',') a->script_pos++;
        if (!s[a->script_pos]) return 0;
        uint8_t d = 0, b = 0;
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

static void dump_png(App *a, Game *g)
{
    if (g->status) memcpy(a->fb, status_framebuffer(g->status), FB_W * FB_H);
    else if (g->tear) memcpy(a->fb, tear_framebuffer(g->tear), FB_W * FB_H);
    else render_frame(a->fb, g, &a->sh.hero);
    render_hud(a->fb, g, &a->sh.font, &a->sh.tfont, map_place_record(g->map));
    itemp_hud(a->fb, &a->sh.pics, &a->sh.tfont, g);
    if (!g->tear) tear_draw_slots(a->fb, g, &a->sh.tear_art);   /* GAME.BIN A18E -> A3A5 */
    int vw, vh;
    const uint8_t *rgb = video_out(a, &vw, &vh);
    if (png_write_rgb(a->shot_path, rgb, vw, vh)) fprintf(stderr, "cannot write %s\n", a->shot_path);
    else fprintf(stderr, "wrote %s (frame %u, hero map (%d,%d) scr (%d,%d) scroll (%d,%d))\n", a->shot_path,
                 g->frame_no, game_hero_map_col(g), game_hero_map_row(g), g->hero_scr_col, g->hero_scr_row,
                 g->scroll_col, g->scroll_row);
}

/* INT 61h AH: bit0 sword, bit1 magic.  The kernel latches the press edge in
 * FF1D (btn1_edge) and fight.bin clears it when it consumes it (6EF7). */
static void set_buttons(Game *g, uint8_t b)
{
    if ((b & 1) && !(g->buttons & 1)) g->btn1_edge = 0xFF;
    if ((b & 2) && !(g->buttons & 2)) g->btn2_edge = 0xFF;
    g->buttons = b;
}

/* ------------------------------------------------------------- cutscenes */
/* opdemo / enddemo own the frame loop exactly as the shops do; this is their
 * `present`.  They draw into their own 320x200 buffer with their own 256-entry
 * DAC, so the conversion to RGB goes through gd_to_rgb, not render_to_rgb. */
static void cutscene_present(Cutscene *c)
{
    App *a = c->user;
    static uint8_t rgb[FB_W * FB_H * 3];
    audio_advance_ms(CS_FRAME_MS);
    if (a->shot_path && c->frames == a->shot_frame) {
        cutscene_to_rgb(c, rgb);
        if (png_write_rgb(a->shot_path, rgb, FB_W, FB_H)) fprintf(stderr, "cannot write %s\n", a->shot_path);
        else fprintf(stderr, "wrote %s (cutscene act %d, frame %u, beat %d)\n",
                     a->shot_path, c->act, c->frames, c->beat);
    }
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
    SDL_UpdateTexture(a->tex, NULL, rgb, FB_W * 3);
    SDL_RenderClear(a->ren);
    SDL_RenderCopy(a->ren, a->tex, NULL, NULL);
    SDL_RenderPresent(a->ren);
    static Uint64 next = 0;
    Uint64 now = SDL_GetPerformanceCounter(), freq = SDL_GetPerformanceFrequency();
    if (next == 0 || now > next + freq) next = now;
    next += (Uint64)(CS_FRAME_MS / 1000.0 * (double)freq);
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
        SDL_Delay(rem > 2 ? 1 : 0);
    }
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    c->key = (uint8_t)(k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER]);
#endif
}

/* GAME.BIN at boot with no command-line argument: `jmp [0x6002]` into opdemo,
 * which hands back with AX = 0xFFFF when the demo has run (opdemo 6A41). */
static int run_cutscenes(App *a, const char *dir)
{
    if (cutscene_init(&a->cs, dir, &a->sh.tfont)) {
        fprintf(stderr, "cannot load gdmcga (ZELRES1[5]): no intro\n");
        return -1;
    }
    a->cs.user = a; a->cs.present = cutscene_present;
    a->cs.max_frames = a->cs_frames;
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

static void present(Game *g)
{
    App *a = app_of(g->user);
    audio_advance_ms(a->frame_ms);
    if (a->verbose)
        fprintf(stderr, "frame %4u  hero map (%3d,%2d) scr (%2d,%2d) scroll (%3d,%2d) v=%02x anim=%02x flags=%02x "
                "crouch=%02x ladder=%02x dirs=%x hp=%u atk=%02x/%u%s%s\n",
                g->frame_no, game_hero_map_col(g), game_hero_map_row(g), g->hero_scr_col, g->hero_scr_row,
                g->scroll_col, g->scroll_row, g->vstate, g->hero_anim, g->hero_flags, g->crouching, g->on_ladder,
                g->dirs, g->hp, g->attacking, g->attack_var, g->on_hazard ? " HAZARD" : "",
                g->hero_hit ? " HIT" : "");
    if (a->max_frames && g->frame_no >= a->max_frames) a->quit = 1;
    if (a->quit && g->status) g->status->done = 1;   /* let the status screen out */
    if (a->headless) {
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
    SDL_RenderClear(a->ren);
    SDL_RenderCopy(a->ren, a->tex, NULL, NULL);
    SDL_RenderPresent(a->ren);
    char title[256];
    snprintf(title, sizeof title, "Zeliard - %s  (%d,%d)  LIFE %u/%u  EXP %u  GOLD %u  %s", g->map->name,
             game_hero_map_col(g), game_hero_map_row(g), g->hp, g->max_hp, g->exp, (unsigned)g->gold, g->message);
    title_paused(a, title);

    /* wait out the frame (4*speed ticks), pumping events */
    static Uint64 next = 0;
    frame_wait(a, g, &next);
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    audio_hotkeys(k);
    /* Paused: hand the engine a frame of nothing rather than the keys the
     * player is using to talk to the pause screen. */
    if (a->paused) { g->dirs = 0; set_buttons(g, 0); g->menu_key = 0; return; }
    uint8_t d = pad_dirs(a);
    if (k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W] || k[SDL_SCANCODE_Z] || k[SDL_SCANCODE_SPACE]) d |= DIR_UP;
    if (k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S]) d |= DIR_DOWN;
    if (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A]) d |= DIR_LEFT;
    if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) d |= DIR_RIGHT;
    g->dirs = d;
    uint8_t b = pad_btns(a);
    if (k[SDL_SCANCODE_X] || k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_RCTRL]) b |= 1;
    if (k[SDL_SCANCODE_C] || k[SDL_SCANCODE_LALT]) b |= 2;
    set_buttons(g, b);
    g->menu_key = (uint8_t)(k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER] || pad_menu(a));   /* FF18 bit0 */
#endif
}

/* ---------------------------------------------------------------- town */
static void town_present(Town *t)
{
    App *a = app_of(t->user);
    audio_advance_ms(a->frame_ms);
    if (a->verbose)
        fprintf(stderr, "town frame %4u  hero map col %3d scr %3d scroll %3d anim %u flags %02x%s%s\n",
                t->frame_no, town_hero_col(t), t->hero_scr_col, t->scroll_col, t->hero_anim, t->hero_flags,
                t->message[0] ? "  msg: " : "", t->message);
    if (a->max_frames && t->frame_no >= a->max_frames) a->quit = 1;
    if (a->quit && t->status) t->status->done = 1;
    if (a->quit) t->quit = 1;
    if (a->headless) {
        if (a->shot_path && t->frame_no == a->shot_frame) {
            if (t->status) memcpy(a->fb, status_framebuffer(t->status), FB_W * FB_H);
            else if (t->shop) memcpy(a->fb, shop_framebuffer(t->shop), FB_W * FB_H);
            else town_render(a->fb, t);
            render_hud(a->fb, t->g, &a->sh.font, &a->sh.tfont, town_place_record(t->map));
            itemp_hud(a->fb, &a->sh.pics, &a->sh.tfont, t->g);
            tear_draw_slots(a->fb, t->g, &a->sh.tear_art);
            int vw, vh;
            const uint8_t *rgb = video_out(a, &vw, &vh);
            if (png_write_rgb(a->shot_path, rgb, vw, vh)) fprintf(stderr, "cannot write %s\n", a->shot_path);
            else fprintf(stderr, "wrote %s (town frame %u, hero col %d)\n", a->shot_path, t->frame_no, town_hero_col(t));
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
    SDL_RenderClear(a->ren);
    SDL_RenderCopy(a->ren, a->tex, NULL, NULL);
    SDL_RenderPresent(a->ren);
    char title[768];
    snprintf(title, sizeof title, "Zeliard - %s  col %d  LIFE %u/%u  GOLD %u  %.400s", t->map->label,
             town_hero_col(t), t->g->hp, t->g->max_hp, (unsigned)t->g->gold, t->message);
    title_paused(a, title);
    static Uint64 next = 0;
    frame_wait(a, NULL, &next);
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    audio_hotkeys(k);
    if (a->paused) { t->dirs = 0; t->buttons = 0; t->menu_key = 0; return; }
    uint8_t d = pad_dirs(a);
    if (k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W] || k[SDL_SCANCODE_Z]) d |= DIR_UP;
    if (k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S]) d |= DIR_DOWN;
    if (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A]) d |= DIR_LEFT;
    if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) d |= DIR_RIGHT;
    t->dirs = d;
    uint8_t b = pad_btns(a);
    if (k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_X]) b |= 1;
    if (k[SDL_SCANCODE_C] || k[SDL_SCANCODE_LALT] || k[SDL_SCANCODE_RALT] || k[SDL_SCANCODE_BACKSPACE]) b |= 2;
    if ((b & 1) && !(t->buttons & 1)) t->btn1_edge = 0xFF;
    if ((b & 2) && !(t->buttons & 2)) t->btn2_edge = 0xFF;
    t->buttons = b;
    t->menu_key = (uint8_t)(k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER] || pad_menu(a));   /* FF18 bit0 */
#endif
}

int main(int argc, char **argv)
{
    static App a; memset(&a, 0, sizeof a);
    a.frame_ms = FRAME_MS_DEFAULT; a.scale = 3; a.video = VID_MCGA;
    Shell *sh = &a.sh;
    const char *dir = NULL;
    int map_idx = 0, pos_col = -1, pos_row = -1, start_in_town = -1, town_col = -1, town_scr = -1, town_anim = -1, town_face = 0, npc_i = -1, npc_f = 0;
    int dbg_sword = -1, dbg_shield = -1, dbg_level = -1, dbg_life = -1;
    long dbg_gold = -1;
    const char *dbg_potions = NULL, *dbg_spells = NULL;
    const char *save_name = "ZELIARD", *load_name = NULL, *save_now = NULL;
    /* the intro runs on a plain interactive launch, the way GAME.BIN runs
     * opdemo when it was given no command-line argument; anything that names a
     * start state or a scripted/headless run suppresses it. */
    int explicit_start = 0;
    a.intro = -1;
    const char *wav_path = NULL;
    const char *gd_art_name = NULL, *gd_art_path = NULL;
    int want_audio = 1, audio_backend = AUDIO_ADLIB, music_force = -2;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dir") && i + 1 < argc) dir = argv[++i];
        else if (!strcmp(argv[i], "--map") && i + 1 < argc) { map_idx = (int)strtol(argv[++i], NULL, 0); explicit_start = 1; }
        else if (!strcmp(argv[i], "--pos") && i + 2 < argc) { pos_col = atoi(argv[++i]); pos_row = atoi(argv[++i]); explicit_start = 1; }
        else if (!strcmp(argv[i], "--headless")) { a.headless = 1; explicit_start = 1; }
        else if (!strcmp(argv[i], "--screenshot") && i + 2 < argc) { a.shot_frame = (unsigned)atoi(argv[++i]); a.shot_path = argv[++i]; a.headless = 1; explicit_start = 1; }
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) { a.script = argv[++i]; explicit_start = 1; }
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) a.scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fullscreen")) a.fullscreen = 1;
        /* the drivers all drew for a 4:3 monitor, so their pixels are not
         * square; --aspect 1:1 turns the correction off for pixel-peeping
         * against docs/screenshots/, which are native framebuffer grabs */
        else if (!strcmp(argv[i], "--aspect") && i + 1 < argc) {
            const char *v = argv[++i];
            if (!strcmp(v, "1:1") || !strcmp(v, "square")) a.square = 1;
            else if (!strcmp(v, "4:3") || !strcmp(v, "crt")) a.square = 0;
            else { fprintf(stderr, "--aspect: expected 4:3 or 1:1\n"); return 1; }
        }
        else if (!strcmp(argv[i], "--video") && i + 1 < argc) {
            a.video = video_mode_by_name(argv[++i]);
            if (a.video < 0) { fprintf(stderr, "unknown --video %s (mcga cga cga2 ega hgc tandy)\n", argv[i]); return 2; }
        }
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) { a.max_frames = (unsigned)atoi(argv[++i]); explicit_start = 1; }
        else if (!strcmp(argv[i], "--speed") && i + 1 < argc) a.frame_ms = FRAME_MS_DEFAULT * atoi(argv[++i]) / 5.0;
        else if (!strcmp(argv[i], "--town") && i + 1 < argc) { start_in_town = (int)strtol(argv[++i], NULL, 0); explicit_start = 1; }
        else if (!strcmp(argv[i], "--town-col") && i + 1 < argc) town_col = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--town-scr") && i + 1 < argc) town_scr = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--town-npc") && i + 2 < argc) { npc_i = atoi(argv[++i]); npc_f = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--town-anim") && i + 2 < argc) { town_anim = atoi(argv[++i]); town_face = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--sound")) sound_set_log(1);
        else if (!strcmp(argv[i], "--no-music")) want_audio = 0;
        else if (!strcmp(argv[i], "--speaker")) audio_backend = AUDIO_SPEAKER;
        else if (!strcmp(argv[i], "--dump-audio") && i + 1 < argc) wav_path = argv[++i];
        else if (!strcmp(argv[i], "--music") && i + 1 < argc) music_force = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--sword") && i + 1 < argc) dbg_sword = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shield") && i + 1 < argc) dbg_shield = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--level") && i + 1 < argc) dbg_level = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--life") && i + 1 < argc) dbg_life = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--gold") && i + 1 < argc) dbg_gold = atol(argv[++i]);
        else if (!strcmp(argv[i], "--potions") && i + 1 < argc) dbg_potions = argv[++i];
        else if (!strcmp(argv[i], "--spells") && i + 1 < argc) dbg_spells = argv[++i];
        else if (!strcmp(argv[i], "--name") && i + 1 < argc) save_name = argv[++i];
        else if (!strcmp(argv[i], "--load") && i + 1 < argc) { load_name = argv[++i]; explicit_start = 1; }
        else if (!strcmp(argv[i], "--save") && i + 1 < argc) { save_now = argv[++i]; explicit_start = 1; }
        else if (!strcmp(argv[i], "--gd-art") && i + 2 < argc) { gd_art_name = argv[++i]; gd_art_path = argv[++i]; }
        else if (!strcmp(argv[i], "--intro")) a.intro = 1;
        else if (!strcmp(argv[i], "--no-intro")) a.intro = 0;
        else if (!strcmp(argv[i], "--intro-act") && i + 1 < argc) { a.intro_act = atoi(argv[++i]); a.intro = 1; a.intro_only = 1; }
        else if (!strcmp(argv[i], "--ending")) { a.ending = 1; a.intro = 1; a.intro_only = 1; }
        else if (!strcmp(argv[i], "--cutscene-frames") && i + 1 < argc) a.cs_frames = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--verbose") || !strcmp(argv[i], "-v")) a.verbose = 1;
        else { usage(); return 2; }
    }
    if (a.intro < 0) a.intro = !explicit_start;
    /* GAME.BIN comes back from the demo and starts a new game in the castle */
    if (a.intro && !a.intro_only && !explicit_start && start_in_town < 0) start_in_town = 0;
    /* --gd-art NAME OUT.png: render one intro/ending resource the way
     * tools/grp2png.py's render_gd does, so the two decoders can be diffed
     * (port/tools/compare_gdart.py, `make verify`). */
    if (gd_art_name) {
        const GdArt *art = gd_art_find(gd_art_name);
        if (!art) { fprintf(stderr, "unknown gd resource %s\n", gd_art_name); return 2; }
        const char *gdir = shell_find_dir(dir);
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
    sh->user = &a; sh->present = present; sh->town_present = town_present;
    /* audio before shell_init: loading a map starts its score (shell.c).  A
     * headless run without --dump-audio never opens it, so `make verify` and
     * the scripted runs stay silent and do no extra work. */
    if (want_audio && (!a.headless || wav_path)) {
        const char *gdir = shell_find_dir(dir);
        audio_init(gdir, audio_backend, wav_path == NULL && !a.headless);
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
    if (shell_init(sh, dir, map_idx)) return 1;
    Game *g = &sh->g;

    snprintf(g->player_name, sizeof g->player_name, "%s", save_name);    /* FF6C..FF73 */
    if (load_name && player_load_usr(g, sh->dir, load_name) == 0)        /* town.bin 7592 restore_game */
        fprintf(stderr, "[save] %s.usr restored: LIFE %u/%u, level %u, EXP %u, GOLD %u\n",
                load_name, g->hp, g->max_hp, g->level, g->exp, (unsigned)g->gold);
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
        if (a.fullscreen) wflags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
        a.win = SDL_CreateWindow("Zeliard port", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 lw * vscale, lh * vscale, wflags);
        a.ren = SDL_CreateRenderer(a.win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!a.ren) a.ren = SDL_CreateRenderer(a.win, -1, 0);
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        SDL_RenderSetLogicalSize(a.ren, lw, lh);
        a.tex = SDL_CreateTexture(a.ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, vw, vh);
    }
#else
    if (!a.headless) { fprintf(stderr, "built without SDL2: running headless (use --screenshot / --script)\n"); a.headless = 1; }
#endif
    if (a.headless && a.shot_path && a.shot_frame == 0) a.shot_frame = 1;
    if (music_force >= -1) audio_music_force(music_force);     /* --music N: pin one score (-1 = effects only) */

    /* GAME.BIN A080: no command-line argument -> [FF77] = 0xFF and jmp into
     * opdemo, which reloads GAME.BIN when it is done (opdemo 6A41). */
    if (a.intro && !save_now) {
        run_cutscenes(&a, sh->dir);
        if (a.intro_only) { audio_shutdown(); shell_free(sh);
#ifdef HAVE_SDL
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
        audio_shutdown();
        shell_free(sh);
        return 0;
    }
    while (!a.quit) shell_frame(sh);

    fprintf(stderr, "stopped after %u frames: hero map (%d,%d), LIFE %u/%u, EXP %u, GOLD %u, %u hazard frames, %u deaths\n",
            g->frame_no, game_hero_map_col(g), game_hero_map_row(g), g->hp, g->max_hp, g->exp, (unsigned)g->gold,
            g->hazard_frames, g->deaths);
    audio_shutdown();
    shell_free(sh);
#ifdef HAVE_SDL
    if (a.tex) SDL_DestroyTexture(a.tex);
    if (a.ren) SDL_DestroyRenderer(a.ren);
    if (a.win) SDL_DestroyWindow(a.win);
    if (!a.headless) SDL_Quit();
#endif
    return 0;
}
