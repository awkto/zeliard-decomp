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
#ifdef HAVE_SDL
#include <SDL.h>
#endif

#define FRAME_MS_DEFAULT 84.5           /* 20 ticks of 236.7 Hz at speed 5 */

typedef struct {
    const char *dir;
    Map      maps[2];
    Tileset  tiles[2];
    int      cur;                       /* which maps[]/tiles[] slot is live */
    HeroGfx  hero;
    uint8_t  fb[FB_W * FB_H];
    int      headless;
    unsigned shot_frame;
    unsigned max_frames;                /* --frames N: stop after N rendered frames */
    const char *shot_path;
    const char *script;                 /* headless key script */
    int      script_pos, script_left;
    uint8_t  script_dirs;
    double   frame_ms;
    int      scale;
    int      quit;
    int      verbose;
#ifdef HAVE_SDL
    SDL_Window *win; SDL_Renderer *ren; SDL_Texture *tex;
#endif
} App;

static void usage(void)
{
    fprintf(stderr,
        "usage: zeliard [--dir GAMEDIR] [--map N] [--pos COL ROW] [--headless] [--screenshot N out.png]\n"
        "               [--script SCRIPT] [--scale N] [--speed N] [--verbose]\n"
        "  --dir       directory holding ZELRES1-3.SAR (default: zeliard/, ../zeliard/)\n"
        "  --map N     system map index (0 = MP10 .. 0x1E = MPA0; default 0)\n"
        "  --pos C R   hero top-left map cell (default: the MURALLA door in MP10, 61 7)\n"
        "  --screenshot N FILE   dump the framebuffer after N rendered frames (implies --headless)\n"
        "  --script S  headless input: tokens like R10 (hold Right 10 frames), UR2, D3, .5 (idle 5),\n"
        "              letters U D L R, separated by spaces/commas\n"
        "  --speed N   FF33 speed (frame = 4*N ticks; default 5 = 84.5 ms)\n"
        "  --scale N   window scale (default 3)\n"
        "  --frames N  quit after N rendered frames (SDL and headless)\n");
}

static int script_next(App *a)
{
    while (a->script_left == 0) {
        const char *s = a->script;
        if (!s) return 0;
        while (s[a->script_pos] == ' ' || s[a->script_pos] == ',') a->script_pos++;
        if (!s[a->script_pos]) return 0;
        uint8_t d = 0;
        for (;;) {
            char c = s[a->script_pos];
            if (c == 'U' || c == 'u') d |= DIR_UP;
            else if (c == 'D' || c == 'd') d |= DIR_DOWN;
            else if (c == 'L' || c == 'l') d |= DIR_LEFT;
            else if (c == 'R' || c == 'r') d |= DIR_RIGHT;
            else if (c == '.') ;
            else break;
            a->script_pos++;
        }
        int n = 0;
        while (s[a->script_pos] >= '0' && s[a->script_pos] <= '9') n = n * 10 + (s[a->script_pos++] - '0');
        a->script_dirs = d; a->script_left = n ? n : 1;
    }
    a->script_left--;
    return 1;
}

static void dump_png(App *a, Game *g)
{
    static uint8_t rgb[FB_W * FB_H * 3];
    render_frame(a->fb, g, &a->hero);
    render_to_rgb(a->fb, rgb);
    if (png_write_rgb(a->shot_path, rgb, FB_W, FB_H)) fprintf(stderr, "cannot write %s\n", a->shot_path);
    else fprintf(stderr, "wrote %s (frame %u, hero map (%d,%d) scr (%d,%d) scroll (%d,%d))\n", a->shot_path,
                 g->frame_no, game_hero_map_col(g), game_hero_map_row(g), g->hero_scr_col, g->hero_scr_row,
                 g->scroll_col, g->scroll_row);
}

static void present(Game *g)
{
    App *a = g->user;
    if (a->verbose)
        fprintf(stderr, "frame %4u  hero map (%3d,%2d) scr (%2d,%2d) scroll (%3d,%2d) v=%02x anim=%02x flags=%02x crouch=%02x ladder=%02x dirs=%x%s\n",
                g->frame_no, game_hero_map_col(g), game_hero_map_row(g), g->hero_scr_col, g->hero_scr_row,
                g->scroll_col, g->scroll_row, g->vstate, g->hero_anim, g->hero_flags, g->crouching, g->on_ladder,
                g->dirs, g->on_hazard ? " HAZARD" : "");
    if (a->max_frames && g->frame_no >= a->max_frames) a->quit = 1;
    if (a->headless) {
        if (a->shot_path && g->frame_no == a->shot_frame) dump_png(a, g);
        if (!script_next(a)) { if (!a->shot_path || g->frame_no >= a->shot_frame) a->quit = 1; g->dirs = 0; }
        else g->dirs = a->script_dirs;
        return;
    }
#ifdef HAVE_SDL
    static uint8_t rgb[FB_W * FB_H * 3];
    render_frame(a->fb, g, &a->hero);
    render_to_rgb(a->fb, rgb);
    SDL_UpdateTexture(a->tex, NULL, rgb, FB_W * 3);
    SDL_RenderClear(a->ren);
    SDL_RenderCopy(a->ren, a->tex, NULL, NULL);
    SDL_RenderPresent(a->ren);
    char title[128];
    snprintf(title, sizeof title, "Zeliard port — %s  hero (%d,%d)  %s", g->map->name, game_hero_map_col(g),
             game_hero_map_row(g), g->message);
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
#endif
}

static int on_door(Game *g, const Door *d)
{
    App *a = g->user;
    if (d->dest_row == 0xFF) {
        fprintf(stderr, "[door] town map %02x is outside the cavern engine — staying put\n", d->dest_map | 0x80);
        return 0;
    }
    int slot = a->cur ^ 1;
    if (map_load_system(&a->maps[slot], a->dir, d->dest_map)) {
        fprintf(stderr, "[door] cannot load system map %02x\n", d->dest_map);
        return 0;
    }
    if (gfx_load_tileset(&a->tiles[slot], a->dir, a->maps[slot].tileset)) return 0;
    a->cur = slot;
    game_enter(g, &a->maps[slot], &a->tiles[slot], d->dest_col, d->dest_row, (d->letter & 0x40) != 0);
    fprintf(stderr, "[door] entered %s (cavern %d, %d cols, tileset MPP%c) at hero (%d,%d)\n", a->maps[slot].name,
            a->maps[slot].cavern, a->maps[slot].width, "123456789AB"[a->maps[slot].tileset],
            game_hero_map_col(g), game_hero_map_row(g));
    return 1;
}

static const char *find_dir(const char *hint)
{
    static const char *cands[] = {"zeliard", "../zeliard", "./", NULL};
    char path[1024];
    if (hint) return hint;
    for (int i = 0; cands[i]; i++) {
        snprintf(path, sizeof path, "%s/ZELRES1.SAR", cands[i]);
        FILE *f = fopen(path, "rb");
        if (f) { fclose(f); return cands[i]; }
    }
    return "zeliard";
}

int main(int argc, char **argv)
{
    App a; memset(&a, 0, sizeof a);
    a.frame_ms = FRAME_MS_DEFAULT; a.scale = 3;
    const char *dir = NULL;
    int map_idx = 0, pos_col = -1, pos_row = -1;
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
        else if (!strcmp(argv[i], "--verbose") || !strcmp(argv[i], "-v")) a.verbose = 1;
        else { usage(); return 2; }
    }
    a.dir = find_dir(dir);
    if (map_load_system(&a.maps[0], a.dir, map_idx)) { fprintf(stderr, "cannot load map %d from %s\n", map_idx, a.dir); return 1; }
    if (gfx_load_tileset(&a.tiles[0], a.dir, a.maps[0].tileset)) { fprintf(stderr, "cannot load tileset\n"); return 1; }
    if (gfx_load_hero(&a.hero, a.dir)) { fprintf(stderr, "cannot load fman.grp\n"); return 1; }

    Game g;
    game_init(&g, &a.maps[0], &a.tiles[0]);
    g.user = &a; g.present = present; g.on_door = on_door;
    if (pos_col < 0) {
        if (map_idx == 0) { pos_col = 61; pos_row = 7; }                 /* mrmp.mdt cavern entry (61,7): the MURALLA door */
        else if (a.maps[0].start_col != 0xFFFF) { pos_col = a.maps[0].start_col; pos_row = a.maps[0].start_row; }
        else { pos_col = 16; pos_row = a.maps[0].row_bias; }
    }
    game_place(&g, pos_col, pos_row, 0);
    fprintf(stderr, "%s: cavern %d, %d columns, tileset MPP%c; hero at (%d,%d), scroll (%d,%d)\n", a.maps[0].name,
            a.maps[0].cavern, a.maps[0].width, "123456789AB"[a.maps[0].tileset], game_hero_map_col(&g),
            game_hero_map_row(&g), g.scroll_col, g.scroll_row);

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

    game_first_frame(&g);
    while (!a.quit) game_step(&g);

    fprintf(stderr, "stopped after %u frames: hero map (%d,%d), %u hazard frames\n", g.frame_no, game_hero_map_col(&g),
            game_hero_map_row(&g), g.hazard_frames);
#ifdef HAVE_SDL
    if (a.tex) SDL_DestroyTexture(a.tex);
    if (a.ren) SDL_DestroyRenderer(a.ren);
    if (a.win) SDL_DestroyWindow(a.win);
    if (!a.headless) SDL_Quit();
#endif
    return 0;
}
