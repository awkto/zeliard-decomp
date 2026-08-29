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
    AiOverlay ai;
    EnemyGfx  egfx;
    DigitFont font;
    /* the town side of the loop (docs/TOWN.md) */
    TownMap     tmap;
    TownTiles   ttiles;
    TownSprites tspr;
    TownHero    thero;
    Town        town;
    TextFont    tfont;
    ItemPics    pics;
    int      in_town;                   /* 0 = fight.bin, 1 = town.bin */
    int      town_tiles_idx, town_spr_idx;
    int      ai_index, enp_index;
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
    else render_frame(a->fb, g, &a->hero);
    render_hud(a->fb, g, &a->font);
    itemp_hud(a->fb, &a->pics, &a->tfont, g);
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
    App *a = g->user;
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
    else render_frame(a->fb, g, &a->hero);
    render_hud(a->fb, g, &a->font);
    itemp_hud(a->fb, &a->pics, &a->tfont, g);
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
    App *a = t->user;
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
            render_hud(a->fb, t->g, &a->font);
            itemp_hud(a->fb, &a->pics, &a->tfont, t->g);
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
    render_hud(a->fb, t->g, &a->font);
    itemp_hud(a->fb, &a->pics, &a->tfont, t->g);
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

/* town.bin 601E: (re)load the map's tile bank and NPC sprite set */
static int town_load_banks(App *a)
{
    if (a->town_tiles_idx != a->tmap.tileset) {
        if (town_load_tiles(&a->ttiles, a->dir, a->tmap.tileset)) { fprintf(stderr, "cannot load the town tile bank\n"); return -1; }
        a->town_tiles_idx = a->tmap.tileset;
    }
    if (a->town_spr_idx != a->tmap.gfx) {
        if (town_load_sprites(&a->tspr, a->dir, a->tmap.gfx)) { fprintf(stderr, "cannot load the NPC sprites\n"); return -1; }
        a->town_spr_idx = a->tmap.gfx;
    }
    return 0;
}

/* fight.bin 7B79 / 99E0: leave the cavern for town `idx`, hero at map column
 * `col` (< 0 = the map's own C013 start column, the death return). */
static int enter_town(Game *g, int idx, int col, int died)
{
    App *a = g->user;
    if (town_load_map(&a->tmap, a->dir, idx)) { fprintf(stderr, "[town] cannot load town map %d\n", idx); return 0; }
    int np = town_apply_patches(&a->tmap, g->page);                     /* 6AED */
    if (town_load_banks(a)) return 0;
    town_init(&a->town, &a->tmap, &a->ttiles, &a->tspr, &a->thero, g);
    a->town.user = a; a->town.present = town_present;
    a->town.font = &a->tfont; a->town.dir = a->dir; a->town.pics = &a->pics;
    town_place(&a->town, col < 0 ? a->tmap.start_col : col, 0);
    g->cur_map = (uint8_t)(0x80 | idx);
    g->town_map = g->cur_map;
    a->in_town = 1;
    fprintf(stderr, "[town] %s (%d columns, %s, %d NPCs, %d patches)%s: hero at column %d\n",
            a->tmap.label, a->tmap.width, a->tmap.tileset == 0 ? "cpat" : a->tmap.tileset == 1 ? "mpat" : "dpat",
            a->tmap.nnpcs, np, died ? " - the Sage revives you" : "", town_hero_col(&a->town));
    if (died) snprintf(a->town.message, sizeof a->town.message,
                       "While you were unconscious, the spirits brought you here...");
    return 1;
}

/* fight.bin 7EBB: (re)load the AI overlay and the enemy sprite bank named by
 * the map's level record (+3 AI request index, +4 ENPn). */
static void load_banks_by_index(App *a, Game *g, int ai_index, int enp_index)
{
    if (!a->ai.loaded || a->ai_index != ai_index) {
        ai_unload(&a->ai);
        if (ai_load(&a->ai, a->dir, ai_index)) fprintf(stderr, "cannot load AI overlay %d\n", ai_index);
        a->ai_index = ai_index;
    }
    if (a->egfx.ncells == 0 || a->enp_index != enp_index) {
        if (gfx_load_enemy_cells(&a->egfx, a->dir, enp_index)) fprintf(stderr, "cannot load enemy bank %d\n", enp_index);
        a->enp_index = enp_index;
    }
    g->ai = a->ai.loaded ? &a->ai : NULL;
    g->egfx = a->egfx.ncells ? &a->egfx : NULL;
}

static void load_enemy_banks(App *a, Game *g, const Map *m)
{
    /* 6117: in a boss room the level record's +5 bank replaces +4 (0xFF =
     * "keep"), and the AI overlay named by +3 is the boss overlay. */
    int enp = m->enemies;
    if (enp == 0xFF || boss_overlay_p(m->ai)) enp = m->boss_bank;
    load_banks_by_index(a, g, m->ai, enp);
    g->boss_map  = (uint8_t)((m->lvl_flags & 0x80) ? 0xFF : 0);         /* -> FF34 */
    g->boss_room = (uint8_t)((m->lvl_flags & 0x40) ? 0xFF : 0);         /* -> [E6] */
    if (g->boss_map || g->boss_room) {
        if (boss_init(g) == 0)
            fprintf(stderr, "[boss] %s \"%s\": HP %u, EXP %u, gold %u, camera column %u%s\n",
                    boss_overlay_name(m->ai), g->boss.name, g->boss.hp0, g->boss.exp,
                    g->boss.gold, g->boss.cam_col, g->boss.knock_left ? ", knocks left" : "");
        else fprintf(stderr, "[boss] overlay %d has no [A002] block\n", m->ai);
    } else {
        g->boss.active = 0; g->boss_knock_left = 0; g->encounter_frames = 0;
    }
}

/* 7305/731F: post_boss_transition swaps in the level record's +6/+7 banks. */
static int post_boss_banks(Game *g, int post_ai, int post_enemies)
{
    App *a = g->user;
    load_banks_by_index(a, g, post_ai, post_enemies);
    return 1;
}

static int enter_cavern(App *a, Game *g, int sys_map, int col, int row, int face_left);

static int on_door(Game *g, const Door *d)
{
    App *a = g->user;
    if (d->dest_row == 0xFF)                                    /* 7B76: dest_map | 0x80 = a town */
        return enter_town(g, d->dest_map & 0x7F, d->dest_col, 0);
    int slot = a->cur ^ 1;
    if (map_load_system(&a->maps[slot], a->dir, d->dest_map)) {
        fprintf(stderr, "[door] cannot load system map %02x\n", d->dest_map);
        return 0;
    }
    int np = map_apply_patches(&a->maps[slot], g->page);        /* 6BFC */
    if (np) fprintf(stderr, "[map] %d C00C patches applied\n", np);
    if (gfx_load_tileset(&a->tiles[slot], a->dir, a->maps[slot].tileset)) return 0;
    a->cur = slot;
    load_enemy_banks(a, g, &a->maps[slot]);
    game_enter(g, &a->maps[slot], &a->tiles[slot], d->dest_col, d->dest_row, (d->letter & 0x40) != 0);
    game_start_walk_in(g, (d->letter & 0x40) != 0);             /* 7C6E */
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

/* town.bin 6FF8 goto_cavern: the MAP_CAVES record hands the hero to fight.bin. */
static int enter_cavern(App *a, Game *g, int sys_map, int col, int row, int face_left)
{
    int slot = a->cur ^ 1;
    if (map_load_system(&a->maps[slot], a->dir, sys_map)) {
        fprintf(stderr, "[town] cannot load cavern map %d\n", sys_map);
        return 0;
    }
    int np = map_apply_patches(&a->maps[slot], g->page);
    if (gfx_load_tileset(&a->tiles[slot], a->dir, a->maps[slot].tileset)) return 0;
    a->cur = slot;
    load_enemy_banks(a, g, &a->maps[slot]);
    g->map = &a->maps[slot]; g->tiles = &a->tiles[slot];
    game_place(g, col, row, face_left);
    game_start_walk_in(g, face_left);                                   /* 7C6E */
    g->cur_map = (uint8_t)sys_map;
    a->in_town = 0;
    fprintf(stderr, "[cavern] %s (cavern %d, %d cols, %d patches): hero at (%d,%d)\n",
            a->maps[slot].name, a->maps[slot].cavern, a->maps[slot].width, np,
            game_hero_map_col(g), game_hero_map_row(g));
    return 1;
}

/* town.bin 61FC: run one town frame and act on what it asked for. */
static void town_frame(App *a, Game *g)
{
    Town *t = &a->town;
    t->action = 0;
    town_step(t);
    if (!t->action) return;
    switch (t->action) {
    case TOWN_TO_CAVERN: {                                              /* 6FF8 */
        int i = t->action_arg;
        if (i < 0 || i >= a->tmap.ncaves) { fprintf(stderr, "[town] no cave record %d\n", i); break; }
        const TownCave *c = &a->tmap.caves[i];
        g->page[6] = 0xFF;                                              /* 702E: [06] entered a cavern */
        enter_cavern(a, g, c->map, c->col, c->row, c->side & 1);
        break; }
    case TOWN_TO_TOWN: {                                                /* 6CE1 */
        int left = (t->action_arg & 0x100) != 0, dest = t->action_arg & 0xFF;
        if (town_load_map(&a->tmap, a->dir, dest)) break;
        town_apply_patches(&a->tmap, g->page);
        if (town_load_banks(a)) break;
        town_init(t, &a->tmap, &a->ttiles, &a->tspr, &a->thero, g);
        t->user = a; t->present = town_present;
        t->font = &a->tfont; t->dir = a->dir;
        /* 6CE4 / 6D22: reappear at the far end of the new map */
        t->scroll_col = left ? a->tmap.width - 0x24 : 0;
        t->hero_scr_col = left ? 0x1A : 0;
        t->hero_flags = (uint8_t)(left ? 1 : 0);
        town_npc_markers_reset(t);
        g->cur_map = (uint8_t)(0x80 | dest);
        g->town_map = g->cur_map;
        fprintf(stderr, "[town] -> %s (%d columns)\n", a->tmap.label, a->tmap.width);
        break; }
    case TOWN_SHOP: {                                                   /* 6E7E run_shop */
        int dest = t->action_arg & 7;
        fprintf(stderr, "[shop] entering %s (town id %u)\n",
                (const char *[]){"king", "omoya", "sage", "armour", "drug", "church", "bank", "inn"}[dest],
                a->tmap.town_id);
        if (shop_run(t, dest)) fprintf(stderr, "[shop] cannot load the overlay\n");
        town_apply_patches(&a->tmap, g->page);                          /* 6EAF */
        town_npc_markers_reset(t);
        fprintf(stderr, "[shop] left: GOLD %u, sword %u, shield %u/%u, LIFE %u/%u, EXP %u, level %u\n",
                (unsigned)g->gold, g->sword, g->shield, g->shield_hp, g->hp, g->max_hp, g->exp, g->level);
        break; }
    case TOWN_PAST_DOOR:
        fprintf(stderr, "[town] the doorway to the past is not implemented\n");
        break;
    }
    t->action = 0;
}

int main(int argc, char **argv)
{
    App a; memset(&a, 0, sizeof a);
    a.frame_ms = FRAME_MS_DEFAULT; a.scale = 3;
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
    a.dir = find_dir(dir);
    if (map_load_system(&a.maps[0], a.dir, map_idx)) { fprintf(stderr, "cannot load map %d from %s\n", map_idx, a.dir); return 1; }
    { static uint8_t page0[256]; int np = map_apply_patches(&a.maps[0], page0);
      if (np) fprintf(stderr, "[map] %d C00C patches applied\n", np); }
    if (gfx_load_tileset(&a.tiles[0], a.dir, a.maps[0].tileset)) { fprintf(stderr, "cannot load tileset\n"); return 1; }
    if (gfx_load_hero(&a.hero, a.dir)) { fprintf(stderr, "cannot load fman.grp\n"); return 1; }

    if (gfx_load_digits(&a.font, a.dir)) fprintf(stderr, "note: no HUD digit font (font.grp)\n");
    if (text_load_font(&a.tfont, a.dir)) fprintf(stderr, "note: no proportional font (font.grp)\n");
    if (itemp_load(&a.pics, a.dir)) fprintf(stderr, "note: no itemp.grp (no item/magic/sword pictures)\n");

    Game g;
    game_init(&g, &a.maps[0], &a.tiles[0]);
    g.user = &a; g.present = present; g.on_door = on_door;
    g.font = &a.tfont; g.pics = &a.pics;                                /* select.bin needs both */
    /* STDPLY.BIN is the fresh player record: HP, the training sword and the
     * per-town shop stock masks (docs/TOWN.md §7).  Without it the shops have
     * nothing to sell. */
    if (player_load_stdply(&g, a.dir)) fprintf(stderr, "note: STDPLY.BIN not found: the shops will have no stock\n");
    snprintf(g.player_name, sizeof g.player_name, "%s", save_name);      /* FF6C..FF73 */
    if (load_name && player_load_usr(&g, a.dir, load_name) == 0)         /* town.bin 7592 restore_game */
        fprintf(stderr, "[save] %s.usr restored: LIFE %u/%u, level %u, EXP %u, GOLD %u\n",
                load_name, g.hp, g.max_hp, g.level, g.exp, (unsigned)g.gold);
    else if (load_name) fprintf(stderr, "[save] cannot read %s.usr\n", load_name);
    if (dbg_sword >= 0)  g.sword = (uint8_t)dbg_sword;                   /* [92] */
    if (dbg_shield >= 1 && dbg_shield <= 6) {                            /* [93]/[94]/[96] */
        g.shield = (uint8_t)dbg_shield;
        g.shield_hp = SHIELD_HP[dbg_shield - 1];                        /* armrpro A6BF */
        g.page[P_SHIELD_MAX] = (uint8_t)g.shield_hp;
        g.page[P_SHIELD_MAX + 1] = (uint8_t)(g.shield_hp >> 8);
    } else if (dbg_shield == 0) g.shield = 0;
    if (dbg_level >= 0)  g.level = (uint8_t)dbg_level;                   /* [8D] */
    if (dbg_life >= 0)   { g.max_hp = (uint16_t)dbg_life; g.hp = g.max_hp; }       /* [B2]/[90] */
    if (dbg_gold >= 0)   g.gold = (uint32_t)dbg_gold;                    /* [85..87] */
    if (dbg_potions) {                                                  /* [A6..AA] = drug id + 1 */
        int n = 0;
        for (const char *c = dbg_potions; *c && n < 5; ) {
            if (*c >= '0' && *c <= '9') { g.page[0xA6 + n++] = (uint8_t)(*c - '0' + 1); }
            while (*c && *c != ',') c++;
            if (*c) c++;
        }
    }
    if (dbg_spells) {                                                   /* [BB..C1] */
        for (const char *c = dbg_spells; *c; ) {
            if (*c >= '1' && *c <= '7') g.page[0xBB + (*c - '1')] = 0xFF;
            while (*c && *c != ',') c++;
            if (*c) c++;
        }
    }
    player_page_push(&g);
    a.ai_index = a.enp_index = -1;
    g.post_boss = post_boss_banks;
    load_enemy_banks(&a, &g, &a.maps[0]);
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

    a.town_tiles_idx = a.town_spr_idx = -1;
    if (town_load_hero(&a.thero, a.dir)) fprintf(stderr, "note: no tman.grp (town hero sprites)\n");
    g.on_town = enter_town;
    if (start_in_town >= 0) {
        if (!enter_town(&g, start_in_town, town_col, 0)) return 1;
        if (town_scr >= 0) {                       /* exact scroll/hero placement for make verify */
            a.town.scroll_col = town_scr;
            if (town_col >= 0) a.town.hero_scr_col = town_col - 4 - town_scr;
            town_npc_markers_reset(&a.town);
        }
        if (town_anim >= 0) { a.town.hero_anim = (uint8_t)town_anim; a.town.hero_flags = (uint8_t)town_face; }
        if (npc_i >= 0 && npc_i < a.tmap.nnpcs) {
            a.tmap.npcs[npc_i].anim = (uint8_t)(npc_f & 3);
            a.tmap.npcs[npc_i].sprite = (uint8_t)(((npc_f >> 3) & 7) | ((npc_f & 4) ? 0 : 0x80));
            a.tmap.npcs[npc_i].type = 7;
        }
    } else {
        game_first_frame(&g);
    }
    while (!a.quit) { if (a.in_town) town_frame(&a, &g); else game_step(&g); }

    fprintf(stderr, "stopped after %u frames: hero map (%d,%d), LIFE %u/%u, EXP %u, GOLD %u, %u hazard frames, %u deaths\n",
            g.frame_no, game_hero_map_col(&g), game_hero_map_row(&g), g.hp, g.max_hp, g.exp, (unsigned)g.gold,
            g.hazard_frames, g.deaths);
#ifdef HAVE_SDL
    if (a.tex) SDL_DestroyTexture(a.tex);
    if (a.ren) SDL_DestroyRenderer(a.ren);
    if (a.win) SDL_DestroyWindow(a.win);
    if (!a.headless) SDL_Quit();
#endif
    return 0;
}
