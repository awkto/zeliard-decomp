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
#include "png.h"
#include "enemy.h"
#include "boss.h"
#include "town.h"
#include "text.h"
#include "player.h"
#include "shop.h"
#include "status.h"
#include "shell.h"
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
    int      quit;
    int      verbose;
#ifdef HAVE_SDL
    SDL_Window *win; SDL_Renderer *ren; SDL_Texture *tex;
#endif
} App;

/* the engines carry the Shell in ->user; the front end hangs off shell->user */
static App *app_of(const void *shell_user) { return ((const Shell *)shell_user)->user; }

static void usage(void)
{
    fprintf(stderr,
        "usage: zeliard [--dir GAMEDIR] [--map N] [--pos COL ROW] [--town N] [--headless]\n"
        "               [--screenshot N out.png] [--script SCRIPT] [--scale N] [--speed N]\n"
        "               [--frames N] [--sound] [--verbose]\n"
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
        "  --frames N  quit after N rendered frames (SDL and headless)\n"
        "  --sound     log every FF75 sound request the engine produces\n"
        "  --sword N --shield N --level N --life N --gold N\n"
        "              set the player record directly (the shops do it properly)\n"
        "  --potions L comma list of drug ids 0..7 into the five [A6..AA] slots (Enter -> USE:)\n"
        "  --spells L  comma list of spell numbers 1..7 to mark learned at [BB..C1]\n"
        "  --name NAME the NAME.USR the sage's \"Record Experience\" writes\n"
        "  --load NAME restore NAME.USR (town.bin 7592) before starting\n");
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
    static uint8_t rgb[FB_W * FB_H * 3];
    if (g->status) memcpy(a->fb, status_framebuffer(g->status), FB_W * FB_H);
    else render_frame(a->fb, g, &a->sh.hero);
    render_hud(a->fb, g, &a->sh.font);
    itemp_hud(a->fb, &a->sh.pics, &a->sh.tfont, g);
    render_to_rgb(a->fb, rgb);
    if (png_write_rgb(a->shot_path, rgb, FB_W, FB_H)) fprintf(stderr, "cannot write %s\n", a->shot_path);
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

static void present(Game *g)
{
    App *a = app_of(g->user);
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
    static uint8_t rgb[FB_W * FB_H * 3];
    if (g->status) memcpy(a->fb, status_framebuffer(g->status), FB_W * FB_H);
    else render_frame(a->fb, g, &a->sh.hero);
    render_hud(a->fb, g, &a->sh.font);
    itemp_hud(a->fb, &a->sh.pics, &a->sh.tfont, g);
    render_to_rgb(a->fb, rgb);
    SDL_UpdateTexture(a->tex, NULL, rgb, FB_W * 3);
    SDL_RenderClear(a->ren);
    SDL_RenderCopy(a->ren, a->tex, NULL, NULL);
    SDL_RenderPresent(a->ren);
    char title[256];
    snprintf(title, sizeof title, "Zeliard — %s  (%d,%d)  LIFE %u/%u  EXP %u  GOLD %u  %s", g->map->name,
             game_hero_map_col(g), game_hero_map_row(g), g->hp, g->max_hp, g->exp, (unsigned)g->gold, g->message);
    SDL_SetWindowTitle(a->win, title);

    /* wait out the frame (4*speed ticks), pumping events */
    static Uint64 next = 0;
    Uint64 now = SDL_GetPerformanceCounter(), freq = SDL_GetPerformanceFrequency();
    if (next == 0 || now > next + freq) next = now;
    next += (Uint64)(a->frame_ms / 1000.0 * (double)freq);
    for (;;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) a->quit = 1;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) a->quit = 1;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F12 && a->shot_path) dump_png(a, g);
        }
        now = SDL_GetPerformanceCounter();
        if (now >= next || a->quit) break;
        Uint64 rem = (next - now) * 1000 / freq;
        SDL_Delay(rem > 2 ? 1 : 0);
    }
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    uint8_t d = 0;
    if (k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W] || k[SDL_SCANCODE_Z] || k[SDL_SCANCODE_SPACE]) d |= DIR_UP;
    if (k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S]) d |= DIR_DOWN;
    if (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A]) d |= DIR_LEFT;
    if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) d |= DIR_RIGHT;
    g->dirs = d;
    uint8_t b = 0;
    if (k[SDL_SCANCODE_X] || k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_RCTRL]) b |= 1;
    if (k[SDL_SCANCODE_C] || k[SDL_SCANCODE_LALT]) b |= 2;
    set_buttons(g, b);
    g->menu_key = (uint8_t)(k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER]);   /* FF18 bit0 */
#endif
}

/* ---------------------------------------------------------------- town */
static void town_present(Town *t)
{
    App *a = app_of(t->user);
    if (a->verbose)
        fprintf(stderr, "town frame %4u  hero map col %3d scr %3d scroll %3d anim %u flags %02x%s%s\n",
                t->frame_no, town_hero_col(t), t->hero_scr_col, t->scroll_col, t->hero_anim, t->hero_flags,
                t->message[0] ? "  msg: " : "", t->message);
    if (a->max_frames && t->frame_no >= a->max_frames) a->quit = 1;
    if (a->quit && t->status) t->status->done = 1;
    if (a->quit) t->quit = 1;
    if (a->headless) {
        if (a->shot_path && t->frame_no == a->shot_frame) {
            static uint8_t rgb[FB_W * FB_H * 3];
            if (t->status) memcpy(a->fb, status_framebuffer(t->status), FB_W * FB_H);
            else if (t->shop) memcpy(a->fb, shop_framebuffer(t->shop), FB_W * FB_H);
            else town_render(a->fb, t);
            render_hud(a->fb, t->g, &a->sh.font);
            itemp_hud(a->fb, &a->sh.pics, &a->sh.tfont, t->g);
            render_to_rgb(a->fb, rgb);
            if (png_write_rgb(a->shot_path, rgb, FB_W, FB_H)) fprintf(stderr, "cannot write %s\n", a->shot_path);
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
    static uint8_t rgb[FB_W * FB_H * 3];
    if (t->status) memcpy(a->fb, status_framebuffer(t->status), FB_W * FB_H);
    else if (t->shop) memcpy(a->fb, shop_framebuffer(t->shop), FB_W * FB_H);
    else town_render(a->fb, t);
    render_hud(a->fb, t->g, &a->sh.font);
    itemp_hud(a->fb, &a->sh.pics, &a->sh.tfont, t->g);
    render_to_rgb(a->fb, rgb);
    SDL_UpdateTexture(a->tex, NULL, rgb, FB_W * 3);
    SDL_RenderClear(a->ren);
    SDL_RenderCopy(a->ren, a->tex, NULL, NULL);
    SDL_RenderPresent(a->ren);
    char title[768];
    snprintf(title, sizeof title, "Zeliard - %s  col %d  LIFE %u/%u  GOLD %u  %.400s", t->map->label,
             town_hero_col(t), t->g->hp, t->g->max_hp, (unsigned)t->g->gold, t->message);
    SDL_SetWindowTitle(a->win, title);
    static Uint64 next = 0;
    Uint64 now = SDL_GetPerformanceCounter(), freq = SDL_GetPerformanceFrequency();
    if (next == 0 || now > next + freq) next = now;
    next += (Uint64)(a->frame_ms / 1000.0 * (double)freq);
    for (;;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) a->quit = 1;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) a->quit = 1;
        }
        now = SDL_GetPerformanceCounter();
        if (now >= next || a->quit) break;
        Uint64 rem = (next - now) * 1000 / freq;
        SDL_Delay(rem > 2 ? 1 : 0);
    }
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    uint8_t d = 0;
    if (k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W] || k[SDL_SCANCODE_Z]) d |= DIR_UP;
    if (k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S]) d |= DIR_DOWN;
    if (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A]) d |= DIR_LEFT;
    if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) d |= DIR_RIGHT;
    t->dirs = d;
    uint8_t b = 0;
    if (k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_X]) b |= 1;
    if (k[SDL_SCANCODE_C] || k[SDL_SCANCODE_LALT] || k[SDL_SCANCODE_RALT] || k[SDL_SCANCODE_BACKSPACE]) b |= 2;
    if ((b & 1) && !(t->buttons & 1)) t->btn1_edge = 0xFF;
    if ((b & 2) && !(t->buttons & 2)) t->btn2_edge = 0xFF;
    t->buttons = b;
    t->menu_key = (uint8_t)(k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER]);   /* FF18 bit0 */
#endif
}

int main(int argc, char **argv)
{
    static App a; memset(&a, 0, sizeof a);
    a.frame_ms = FRAME_MS_DEFAULT; a.scale = 3;
    Shell *sh = &a.sh;
    const char *dir = NULL;
    int map_idx = 0, pos_col = -1, pos_row = -1, start_in_town = -1, town_col = -1, town_scr = -1, town_anim = -1, town_face = 0, npc_i = -1, npc_f = 0;
    int dbg_sword = -1, dbg_shield = -1, dbg_level = -1, dbg_life = -1;
    long dbg_gold = -1;
    const char *dbg_potions = NULL, *dbg_spells = NULL;
    const char *save_name = "ZELIARD", *load_name = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dir") && i + 1 < argc) dir = argv[++i];
        else if (!strcmp(argv[i], "--map") && i + 1 < argc) map_idx = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--pos") && i + 2 < argc) { pos_col = atoi(argv[++i]); pos_row = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--headless")) a.headless = 1;
        else if (!strcmp(argv[i], "--screenshot") && i + 2 < argc) { a.shot_frame = (unsigned)atoi(argv[++i]); a.shot_path = argv[++i]; a.headless = 1; }
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) a.script = argv[++i];
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) a.scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) a.max_frames = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--speed") && i + 1 < argc) a.frame_ms = FRAME_MS_DEFAULT * atoi(argv[++i]) / 5.0;
        else if (!strcmp(argv[i], "--town") && i + 1 < argc) start_in_town = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--town-col") && i + 1 < argc) town_col = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--town-scr") && i + 1 < argc) town_scr = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--town-npc") && i + 2 < argc) { npc_i = atoi(argv[++i]); npc_f = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--town-anim") && i + 2 < argc) { town_anim = atoi(argv[++i]); town_face = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--sound")) sound_set_log(1);
        else if (!strcmp(argv[i], "--sword") && i + 1 < argc) dbg_sword = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shield") && i + 1 < argc) dbg_shield = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--level") && i + 1 < argc) dbg_level = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--life") && i + 1 < argc) dbg_life = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--gold") && i + 1 < argc) dbg_gold = atol(argv[++i]);
        else if (!strcmp(argv[i], "--potions") && i + 1 < argc) dbg_potions = argv[++i];
        else if (!strcmp(argv[i], "--spells") && i + 1 < argc) dbg_spells = argv[++i];
        else if (!strcmp(argv[i], "--name") && i + 1 < argc) save_name = argv[++i];
        else if (!strcmp(argv[i], "--load") && i + 1 < argc) load_name = argv[++i];
        else if (!strcmp(argv[i], "--verbose") || !strcmp(argv[i], "-v")) a.verbose = 1;
        else { usage(); return 2; }
    }
    sh->user = &a; sh->present = present; sh->town_present = town_present;
    if (shell_init(sh, dir, map_idx)) return 1;
    Game *g = &sh->g;

    snprintf(g->player_name, sizeof g->player_name, "%s", save_name);    /* FF6C..FF73 */
    if (load_name && player_load_usr(g, sh->dir, load_name) == 0)        /* town.bin 7592 restore_game */
        fprintf(stderr, "[save] %s.usr restored: LIFE %u/%u, level %u, EXP %u, GOLD %u\n",
                load_name, g->hp, g->max_hp, g->level, g->exp, (unsigned)g->gold);
    else if (load_name) fprintf(stderr, "[save] cannot read %s.usr\n", load_name);
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
    if (pos_col < 0) {
        if (map_idx == 0) { pos_col = 61; pos_row = 7; }                 /* mrmp.mdt cavern entry (61,7): the MURALLA door */
        else if (sh->maps[0].start_col != 0xFFFF) { pos_col = sh->maps[0].start_col; pos_row = sh->maps[0].start_row; }
        else { pos_col = 16; pos_row = sh->maps[0].row_bias; }
    }
    game_place(g, pos_col, pos_row, 0);
    fprintf(stderr, "%s: cavern %d, %d columns, tileset MPP%c; hero at (%d,%d), scroll (%d,%d)\n", sh->maps[0].name,
            sh->maps[0].cavern, sh->maps[0].width, "123456789AB"[sh->maps[0].tileset], game_hero_map_col(g),
            game_hero_map_row(g), g->scroll_col, g->scroll_row);

#ifdef HAVE_SDL
    if (!a.headless) {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
        a.win = SDL_CreateWindow("Zeliard port", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, FB_W * a.scale, FB_H * a.scale, SDL_WINDOW_SHOWN);
        a.ren = SDL_CreateRenderer(a.win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!a.ren) a.ren = SDL_CreateRenderer(a.win, -1, 0);
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        SDL_RenderSetLogicalSize(a.ren, FB_W, FB_H);
        a.tex = SDL_CreateTexture(a.ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, FB_W, FB_H);
    }
#else
    if (!a.headless) { fprintf(stderr, "built without SDL2: running headless (use --screenshot / --script)\n"); a.headless = 1; }
#endif
    if (a.headless && a.shot_path && a.shot_frame == 0) a.shot_frame = 1;

    if (start_in_town >= 0) {
        if (!shell_enter_town(g, start_in_town, town_col, 0)) return 1;
        if (town_scr >= 0) {                       /* exact scroll/hero placement for make verify */
            sh->town.scroll_col = town_scr;
            if (town_col >= 0) sh->town.hero_scr_col = town_col - 4 - town_scr;
            town_npc_markers_reset(&sh->town);
        }
        if (town_anim >= 0) { sh->town.hero_anim = (uint8_t)town_anim; sh->town.hero_flags = (uint8_t)town_face; }
        if (npc_i >= 0 && npc_i < sh->tmap.nnpcs) {
            sh->tmap.npcs[npc_i].anim = (uint8_t)(npc_f & 3);
            sh->tmap.npcs[npc_i].sprite = (uint8_t)(((npc_f >> 3) & 7) | ((npc_f & 4) ? 0 : 0x80));
            sh->tmap.npcs[npc_i].type = 7;
        }
    } else {
        game_first_frame(g);
    }
    while (!a.quit) shell_frame(sh);

    fprintf(stderr, "stopped after %u frames: hero map (%d,%d), LIFE %u/%u, EXP %u, GOLD %u, %u hazard frames, %u deaths\n",
            g->frame_no, game_hero_map_col(g), game_hero_map_row(g), g->hp, g->max_hp, g->exp, (unsigned)g->gold,
            g->hazard_frames, g->deaths);
#ifdef HAVE_SDL
    if (a.tex) SDL_DestroyTexture(a.tex);
    if (a.ren) SDL_DestroyRenderer(a.ren);
    if (a.win) SDL_DestroyWindow(a.win);
    if (!a.headless) SDL_Quit();
#endif
    return 0;
}
