/* shell.c — the resource / transition layer shared by every front end.
 * Lifted verbatim out of main.c; the addresses in the comments are GAME.BIN,
 * fight.bin and town.bin offsets. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sar.h"
#include "shell.h"
#include "render.h"
#include "player.h"
#include "shop.h"
#include "status.h"
#include "boss.h"
#include "audio.h"

#define LOG(s, ...) do { if (!(s)->quiet) fprintf(stderr, __VA_ARGS__); } while (0)

/* #42: the folder config.c remembered, consulted between the --dir hint and
 * the built-in probes. */
static char shell_cfg_dir[1024];
void shell_set_config_dir(const char *dir)
{
    if (dir) snprintf(shell_cfg_dir, sizeof shell_cfg_dir, "%s", dir);
    else shell_cfg_dir[0] = 0;
}

/* #56: the per-user drop folder (SDL_GetPrefPath/game), the one place an
 * installed copy, which has no useful working directory, can be given its
 * game files without a command line. */
static char shell_data_dir[1024];
void shell_set_data_dir(const char *dir)
{
    if (dir) snprintf(shell_data_dir, sizeof shell_data_dir, "%s", dir);
    else shell_data_dir[0] = 0;
}

/* A folder is the game folder only when all three ZELRES*.SAR archives are
 * there.  game_fopen (sar.c) tries the name as given and all-lower-case, which
 * is the port's one case-insensitive helper (#57). */
static int dir_has_game(const char *dir)
{
    static const char *names[] = {"ZELRES1.SAR", "ZELRES2.SAR", "ZELRES3.SAR"};
    for (int i = 0; i < 3; i++) {
        FILE *f = game_fopen(dir, names[i]);
        if (!f) return 0;
        fclose(f);
    }
    return 1;
}

const char *shell_find_dir(const char *hint)
{
    static const char *cands[] = {"zeliard", "../zeliard", "./", NULL};
    if (hint && dir_has_game(hint)) return hint;
    if (shell_cfg_dir[0] && dir_has_game(shell_cfg_dir)) return shell_cfg_dir;
    if (shell_data_dir[0] && dir_has_game(shell_data_dir)) return shell_data_dir;
    for (int i = 0; cands[i]; i++)
        if (dir_has_game(cands[i])) return cands[i];
    return NULL;
}

/* town.bin 601E: (re)load the map's tile bank and NPC sprite set */
static int town_load_banks(Shell *s)
{
    if (s->town_tiles_idx != s->tmap.tileset) {
        if (town_load_tiles(&s->ttiles, s->dir, s->tmap.tileset)) { fprintf(stderr, "cannot load the town tile bank\n"); return -1; }
        s->town_tiles_idx = s->tmap.tileset;
    }
    if (s->town_spr_idx != s->tmap.gfx) {
        if (town_load_sprites(&s->tspr, s->dir, s->tmap.gfx)) { fprintf(stderr, "cannot load the NPC sprites\n"); return -1; }
        s->town_spr_idx = s->tmap.gfx;
    }
    /* town.bin 60xx: the backdrop painter the level record's bit 0 names —
     * ympd.bin above ground, ckpd.bin underground (docs/TOWN.md §4.3) */
    {
        int ug = (s->tmap.town_flags & 1) != 0;
        if (s->town_back_idx != ug) {
            if (town_load_backdrop(&s->tback, s->dir, ug))
                fprintf(stderr, "note: no %s (flat sky)\n", ug ? "ckpd.bin" : "ympd.bin");
            s->town_back_idx = ug;
        }
    }
    return 0;
}

/* fight.bin 7B79 / 99E0: leave the cavern for town `idx`, hero at map column
 * `col` (< 0 = the map's own C013 start column, the death return). */
int shell_enter_town(Game *g, int idx, int col, int died)
{
    Shell *s = g->user;
    if (town_load_map(&s->tmap, s->dir, idx)) { fprintf(stderr, "[town] cannot load town map %d\n", idx); return 0; }
    int np = town_apply_patches(&s->tmap, g->page);                     /* 6AED */
    if (town_load_banks(s)) return 0;
    audio_music(s->tmap.music);                                         /* town level record +0 bits 1-4 */
    town_init(&s->town, &s->tmap, &s->ttiles, &s->tspr, &s->thero, g);   /* 7B7F/99EF/6AAF load via [10C] and 60C3/60C6: the memset clears both edges */
    /* the town HUD is the non-boss one (GOLD, PLACE): a death in a boss room
     * left the boss flags and name plate set (issue #102) */
    g->boss_map = g->boss_room = 0; g->boss.active = 0;
    hud_town_panels(g);                                                 /* 610F/6127 */
    s->town.user = s; s->town.present = s->town_present; s->town.side = s->town_side; s->town.frame_end = s->town_frame_end;
    s->town.font = &s->tfont; s->town.dir = s->dir; s->town.pics = &s->pics;
    s->town.back = &s->tback;
    /* facing: a door's way in runs fight.bin 7C6E (the cavern walk-in code,
     * reached for a town too): [C3] set -> 7CB4 `or [C2],1`, else 7C75 clears
     * it.  The death return jumps past it (99E0 -> 7D85): [C2] stays as the
     * hero died.  [C8]: 7E93 / 7D9E store the loaded map's music index. */
    int face_left = died ? (g->hero_flags & FACE_LEFT) != 0 : g->page[0xC3] != 0;
    g->page[0xC8] = s->tmap.music;
    town_place(&s->town, col < 0 ? s->tmap.start_col : col, face_left);
    /* the rest of [C2] carries over too (die f125: the walking bit 2 stays
     * set in town until something rewrites it; town.bin only touches bit 0) */
    s->town.hero_flags = (uint8_t)((g->hero_flags & ~FACE_LEFT) | (face_left ? FACE_LEFT : 0));
    s->town.hero_anim = 0;                                              /* 6050: both entries zero [E7] */
    /* 6055: with Jashiin's flag set the death return enters the town "alive" ([E8] = 0): no
     * Sage, the ordinary 61B1 entry, and 6084 arms the walk-in of an underground town as for
     * any entry from a cavern -- the ending's castle (#111) */
    int ending = died && g->jashiin_defeated;
    s->town_entry = (died && !ending) ? 2 : 1;                          /* 616B: [E8] -> kenjpro, else 61B1 */
    s->town_walk_in = ending ? (s->tmap.town_flags & 1) != 0 : 0;   /* 6078; on_door sets it (a restore/boot comes in at 601E: [7C43]) */
    g->cur_map = (uint8_t)(0x80 | idx);
    /* [C5] town_map is NOT the town just entered: nothing in town.bin or
     * fight.bin writes it but the ending (99B4); it is STDPLY's 0x81 or what
     * the save file holds, and only the death return (99E5) reads it */
    s->in_town = 1;
    s->transitions++;
    LOG(s, "[town] %s (%d columns, %s, %d NPCs, %d patches)%s: hero at column %d\n",
        s->tmap.label, s->tmap.width, s->tmap.tileset == 0 ? "cpat" : s->tmap.tileset == 1 ? "mpat" : "dpat",
        s->tmap.nnpcs, np, (died && !ending) ? " - the Sage revives you" : "", town_hero_col(&s->town));
    if (died && !ending) snprintf(s->town.message, sizeof s->town.message,
                       "While you were unconscious, the spirits brought you here...");
    return 1;
}

/* fight.bin 7EBB: (re)load the AI overlay and the enemy sprite bank named by
 * the map's level record (+3 AI request index, +4 ENPn). */
static void load_banks_by_index(Shell *s, Game *g, int ai_index, int enp_index)
{
    if (!s->ai.loaded || s->ai_index != ai_index) {
        ai_unload(&s->ai);
        if (ai_load(&s->ai, s->dir, ai_index)) fprintf(stderr, "cannot load AI overlay %d\n", ai_index);
        s->ai_index = ai_index;
    }
    if (s->egfx.ncells == 0 || s->enp_index != enp_index) {
        if (gfx_load_enemy_cells(&s->egfx, s->dir, enp_index)) fprintf(stderr, "cannot load enemy bank %d\n", enp_index);
        s->enp_index = enp_index;
    }
    g->ai = s->ai.loaded ? &s->ai : NULL;
    g->egfx = s->egfx.ncells ? &s->egfx : NULL;
}

void shell_load_enemy_banks(Shell *s, const Map *m)
{
    Game *g = &s->g;
    /* 6117: entering a boss map copies the level record's +5 bank over +4 and
     * loads it.  +4 == 0xFF means the ordinary load at 7EBB found nothing, so
     * the boss bank is the one that ends up in arena:4000; when +4 IS a real
     * bank (mp90's 16, mpa0's 17) it is already loaded and 6117's request for
     * bank 0xFF simply fails, leaving it in place.  Taking +5 unconditionally
     * for every boss overlay left MPA0 (the final boss) with no sprite bank at
     * all — "cannot load enemy bank 255" — and Jashiin invisible. */
    int enp = (m->enemies != 0xFF) ? m->enemies : m->boss_bank;
    load_banks_by_index(s, g, m->ai, enp);
    audio_music((m->lvl_flags >> 1) & 0x0F);                            /* fight.bin 7E93 -> the 9E53 table */
    g->boss_map  = (uint8_t)((m->lvl_flags & 0x80) ? 0xFF : 0);         /* -> FF34 */
    g->boss_room = (uint8_t)((m->lvl_flags & 0x40) ? 0xFF : 0);         /* -> [E6] */
    if (g->boss_map) g->gf_frame = 0;   /* 6254/625B: [FF34] -> gfmcga [3012] 4192, the fade-in redraw, counts [4FFF] 8 -> 0 */
    if (g->boss_map || g->boss_room) {
        if (boss_init(g) == 0)
            LOG(s, "[boss] %s \"%s\": HP %u, EXP %u, almas %u, camera column %u%s\n",
                boss_overlay_name(m->ai), g->boss.name, g->boss.hp0, g->boss.exp,
                g->boss.almas, g->boss.cam_col, g->boss.knock_left ? ", knocks left" : "");
        else fprintf(stderr, "[boss] overlay %d has no [A002] block\n", m->ai);
    } else {
        g->boss.active = 0; g->boss_knock_left = 0; g->encounter_frames = 0;
        g->boss_state = 0xFF;                   /* EDA0: every fight.bin entry (6058) and level change (7E84) */
    }
}

/* 7305/731F: post_boss_transition swaps in the level record's +6/+7 banks. */
static int post_boss_banks(Game *g, int post_ai, int post_enemies)
{
    load_banks_by_index((Shell *)g->user, g, post_ai, post_enemies);
    return 1;
}

/* fight.bin 7E93 on every cavern entry (61F3 from town, 7A43/7BFF through a
 * door): [C8] = the level record's score index, (+0 >> 1) & 0x0F (7EB0) */
static void cavern_score_idx(Game *g, const Map *m) { g->page[0xC8] = (uint8_t)((m->lvl_flags >> 1) & 0x0F); }

/* town.bin 6FF8 goto_cavern: the MAP_CAVES record hands the hero to fight.bin.
 *
 * `from_cave_record` is the difference between the two ways in.  A **door**
 * (fight.bin 7DC1) computes `[82] = dest_row + 1 - [C016]`, and 7D2D then pins
 * the hero's screen row at that same `[C016]`, so his map row is exactly
 * `dest_row + 1`.  A **cave record** (town.bin 7005) computes `[82] = row - 10`
 * with the 10 hard-coded, and 7D2D still pins the screen row at `[C016]` -- so
 * the hero's map row is `row - 10 + row_bias`, and the record's row is only
 * where he lands on a map whose row_bias *is* 10.
 *
 * Every cavern proper has row_bias 10; the boss rooms have 12 (MPA0 13).  The
 * one cave record in the game that names a boss room is Llama Town's (27,13)
 * for MP73, and 13 - 10 + 12 = 15 puts the hero on that room's floor -- which
 * is why the DOSBox capture has him standing at (27,15) and not falling from
 * (27,13).  The port used to read the record's row as the map row, drop him
 * the two rows, and give him 69CB's extra step in his facing on the way, so he
 * ended a column to the right of the capture (port/README.md, "The MP73 entry
 * column"). */
int shell_enter_cavern(Shell *s, int sys_map, int col, int row, int face_left,
                       int from_cave_record)
{
    Game *g = &s->g;
    int slot = s->cur ^ 1;
    /* the walk-in's HUD still has the place of the screen the hero came from (#117) */
    const uint8_t *left_place = s->in_town ? town_place_record(&s->tmap) : map_place_record(g->map);
    if (map_load_system(&s->maps[slot], s->dir, sys_map)) {
        fprintf(stderr, "[town] cannot load cavern map %d\n", sys_map);
        return 0;
    }
    int np = map_apply_patches(&s->maps[slot], g->page);
    if (gfx_load_tileset(&s->tiles[slot], s->dir, s->maps[slot].tileset)) return 0;
    s->cur = slot;
    shell_load_enemy_banks(s, &s->maps[slot]);
    cavern_score_idx(g, &s->maps[slot]);
    g->map = &s->maps[slot]; g->tiles = &s->tiles[slot];
    if (s->in_town && s->tmap.raw) {
        /* the town map was the last thing loaded to C000 (town.bin), so the
         * bytes past a short cavern map's end are the town's: what a boss
         * room entered straight from a town (MP73 from Llama) shows in its
         * part records' +7..+F (paguro_fight; #71 for the cavern case) */
        size_t n = s->tmap.rawlen < sizeof g->c000 ? s->tmap.rawlen : sizeof g->c000;
        memcpy(g->c000, s->tmap.raw, n);
        g->nobj = 0;
    }
    if (from_cave_record) row = row - 10 + s->maps[slot].row_bias;       /* 7005 / 7D2D */
    game_place(g, col, row, face_left);
    /* 7D61 -> 6042 .. 6266: with [49] set the hero dies on the restart's first pass -- after the
     * walk-in (the oracle's walk pictures, cave_forced_death f4; hero_entering is 0 in its first
     * frame, #113), whose HUD still has his LIFE (#117) */
    if (g->jashiin_defeated && g->boss_room) game_force_death_check(g);
    else {
        if (g->boss_room) game_boss_room_intro(g);                      /* 61A8 */
        else game_start_walk_in(g, face_left);                          /* 7C6E */
        if (g->jashiin_defeated) game_force_death_check(g);             /* 6266 -> 98FC */
    }
    g->walk_place = left_place;
    g->walk_variant = 0;                                                /* 7A6F: xor al,al; [301E] */
    g->cur_map = (uint8_t)sys_map;
    s->in_town = 0;
    s->transitions++;
    LOG(s, "[cavern] %s (cavern %d, %d cols, %d patches): hero at (%d,%d)\n",
        s->maps[slot].name, s->maps[slot].cavern, s->maps[slot].width, np,
        game_hero_map_col(g), game_hero_map_row(g));
    return 1;
}

/* fight.bin 7A83 -> 7B32: a door leads to another cavern map or to a town. */
static int on_door(Game *g, const Door *d)
{
    Shell *s = g->user;
    if ((d->dflags & 0x80) && g->side) g->side(g, SIDE_SNAP, 0, 0);     /* the Tear scene plays over the screen as it is now (#119) */
    if (d->dest_row == 0xFF)                                    /* 7B76: dest_map | 0x80 = a town */
    {
        if (!(d->dflags & 0x80)) {                              /* 7C11: not the Tear's exit door */
            g->walk_place = map_place_record(g->map);           /* the HUD is still the cavern's */
            game_start_walk_in(g, (d->letter & 0x40) != 0);     /* 7C6E */
            game_run_walk_in(g);
        }
        if (!shell_enter_town(g, d->dest_map & 0x7F, d->dest_col, 0)) return 0;
        /* fight.bin -> town.bin [6000] = 6026 ([7C43] = 0): 6084..6092 arm the
         * 61B4 walk-in when the town is underground (town_flags bit 0) */
        s->town_walk_in = (s->tmap.town_flags & 1) != 0;
        return 1;
    }
    int slot = s->cur ^ 1;
    if (map_load_system(&s->maps[slot], s->dir, d->dest_map)) {
        fprintf(stderr, "[door] cannot load system map %02x\n", d->dest_map);
        return 0;
    }
    uint8_t flag49 = g->page[0x49];
    int np = map_apply_patches(&s->maps[slot], g->page);        /* 6BFC */
    if (g->page[0x49] != flag49) g->jashiin_defeated = g->page[0x49];   /* MPA0's patch list sets [49] */
    if (np) LOG(s, "[map] %d C00C patches applied\n", np);
    if (gfx_load_tileset(&s->tiles[slot], s->dir, s->maps[slot].tileset)) return 0;
    s->cur = slot;
    shell_load_enemy_banks(s, &s->maps[slot]);
    cavern_score_idx(g, &s->maps[slot]);
    uint8_t keep_cnt = g->obj_index;
    g->walk_place = map_place_record(g->map);                   /* still on screen during the walk-in (#117) */
    game_enter(g, &s->maps[slot], &s->tiles[slot], d->dest_col, d->dest_row, (d->letter & 0x40) != 0);
    /* a [E6] room (MP90) goes 6179 -> 619E -> 61A8, past 623D/6263's 9659: the enemy count in
     * [FF4A] is still the map just left (jashiin_fight frame 27) */
    if (g->boss_room) g->obj_index = keep_cnt;
    g->cur_map = d->dest_map;                                   /* 7B6D..7B79: [C4] = record +4 */
    g->hero_entering = 0; g->menu_debounce = 0xFF;              /* 7C02 [FF3A] / 7C07 [9EF5] */
    /* 7C11: the door record's byte +8 bit 7 (cached at [9F1D]) is set on every
     * boss room's exit door, the one post_boss_transition installs — load
     * ROKADEMO.BIN and call it, then jump to 7CF4, skipping the walk-in. */
    if (d->dflags & 0x80) {                                     /* 7C18 */
        LOG(s, "[tear] a Tear of Esmesanti (%u of 9)\n", (unsigned)(g->page[0xA0] + 1));
        tear_cutscene(s);
    } else {
        game_start_walk_in(g, (d->letter & 0x40) != 0);         /* 7C6E */
        if (g->boss_room) g->walk_then_intro = 1;               /* the restart's 6179 -> 61A8 comes after it */
    }
    /* every branch reaches 7CF4 and ends in 7D61 `jmp 6042`, the loop
     * restart: 6049..6053 zero 9F20..9F22 (the counter rise() bumps on the
     * very Up that took the door) and 628A both press edges (#75:
     * pulpo_fight f73 / pollo_fight f81, ice_slide oracle 0, port 2) */
    g->ice_slide = g->ice_steps = g->walk_dir = 0;
    g->btn1_edge = g->btn2_edge = 0;
    s->transitions++;
    game_force_death_check(g);                                   /* 6266, reached through 7D61 -> 6042 */
    LOG(s, "[door] entered %s (cavern %d, %d cols, tileset MPP%c) at hero (%d,%d)\n", s->maps[slot].name,
        s->maps[slot].cavern, s->maps[slot].width, "123456789AB"[s->maps[slot].tileset],
        game_hero_map_col(g), game_hero_map_row(g));
    return 1;
}

int shell_init(Shell *s, const char *dir_hint, int map_idx)
{
    s->dir = shell_find_dir(dir_hint);
    if (!s->dir) {
        fprintf(stderr, "zeliard: no game files found (ZELRES1-3.SAR in %s)\n",
                dir_hint ? dir_hint : "zeliard/, ../zeliard/ or ./");
        return -1;
    }
    if (map_load_system(&s->maps[0], s->dir, map_idx)) {
        fprintf(stderr, "cannot load map %d from %s\n", map_idx, s->dir);
        return -1;
    }
    { static uint8_t page0[256]; int np = map_apply_patches(&s->maps[0], page0);
      if (np) LOG(s, "[map] %d C00C patches applied\n", np); }
    if (gfx_load_tileset(&s->tiles[0], s->dir, s->maps[0].tileset)) { fprintf(stderr, "cannot load tileset\n"); return -1; }
    if (gfx_load_hero(&s->hero, s->dir)) { fprintf(stderr, "cannot load fman.grp\n"); return -1; }
    if (gfx_load_digits(&s->font, s->dir)) fprintf(stderr, "note: no HUD digit font (font.grp)\n");
    if (text_load_font(&s->tfont, s->dir)) fprintf(stderr, "note: no proportional font (font.grp)\n");
    if (itemp_load(&s->pics, s->dir)) fprintf(stderr, "note: no itemp.grp (no item/magic/sword pictures)\n");
    if (town_load_hero(&s->thero, s->dir)) fprintf(stderr, "note: no tman.grp (town hero sprites)\n");
    /* GAME.BIN A185: the boot-time far call into mole.bin paints the stone
     * frame, the strip above the playfield and the grey HUD panel once. */
    if (gfx_load_screen_frame(&s->frame, s->dir)) fprintf(stderr, "note: no mole.bin (no screen frame)\n");
    if (gfx_load_encounter(&s->encnt, s->dir)) fprintf(stderr, "note: no encnt.grp (blank encounter card)\n");
    if (gfx_load_cavemouth(&s->mouth, s->dir)) fprintf(stderr, "note: no roka.grp (black walk-in backdrop)\n");
    /* rokademo's hero cells + gfmcga's sword/sparkle art + GMMCGA's Tear icon
     * + GAME.BIN's nine slot positions (docs/CUTSCENES.md §5) */
    tear_art_load(&s->tear_art, s->dir);

    Game *g = &s->g;
    game_init(g, &s->maps[0], &s->tiles[0]);
    g->user = s; g->present = s->present; g->side = s->side; g->frame_end = s->frame_end; g->on_door = on_door; g->on_town = shell_enter_town;
    g->font = &s->tfont; g->pics = &s->pics;                            /* select.bin needs both */
    g->screen = &s->frame; g->encnt = &s->encnt; g->mouth = &s->mouth;
    g->post_boss = post_boss_banks;
    /* STDPLY.BIN is the fresh player record: HP, the training sword and the
     * per-town shop stock masks (docs/TOWN.md §7). */
    if (player_load_stdply(g, s->dir)) fprintf(stderr, "note: STDPLY.BIN not found: the shops will have no stock\n");
    s->ai_index = s->enp_index = -1;
    s->town_tiles_idx = s->town_spr_idx = -1;
    s->town_back_idx = -1;
    return 0;
}

/* Release everything shell_init and the map/town/AI/bank loads acquired.  The
 * original never needs this — DOS exits and the arena is gone — but the port
 * outlives a game (#44: Emscripten and Android do not simply exit the
 * process), and the tests re-init the same Shell dozens of times.  Everything
 * else in the struct is embedded arrays; these four are the only owners of
 * heap memory.  Safe on a zeroed Shell and idempotent. */
void shell_free(Shell *s)
{
    map_free(&s->maps[0]);
    map_free(&s->maps[1]);
    town_free_map(&s->tmap);
    ai_unload(&s->ai);
    itemp_free(&s->pics);
}

/* town.bin 61FC: run one town frame and act on what it asked for. */
static void town_frame(Shell *s)
{
    Game *g = &s->g;
    Town *t = &s->town;
    t->action = 0;
    if (s->town_entry) {
        int kind = s->town_entry;
        s->town_entry = 0;
        if (kind == 1) {                                                /* 61B1 */
            town_entry_frame(t);
            if (s->town_walk_in) town_walk_in(t);                       /* 61B4 */
            return;
        }
        /* 617A: the death return runs kenjpro's [A004] (the sage's text) and
         * comes back through 6EAF: a 68AF frame, edges cleared, [E7] = 1,
         * then 61FC */
        if (shop_run_death(t)) fprintf(stderr, "[shop] cannot load kenjpro: no sage text\n");
        if (t->quit) return;
        town_force_redraw(t);                                           /* 6ED4 */
        town_tail_frame(t);                                             /* 6EE6 */
        t->btn1_edge = t->btn2_edge = 0;
        t->hero_anim = 1;
        audio_music_restart(s->tmap.music);                             /* 6EF8: the death entry stops it too (6196) and returns through the same restart */
        return;
    }
    town_step(t);
    if (!t->action) return;
    switch (t->action) {
    case TOWN_TO_CAVERN: {                                              /* 6FF8 */
        int i = t->action_arg;
        if (i < 0 || i >= s->tmap.ncaves) { fprintf(stderr, "[town] no cave record %d\n", i); break; }
        const TownCave *c = &s->tmap.caves[i];
        g->page[6] = 0xFF;                                              /* 702E: [06] entered a cavern */
        g->page[0xC3] = (c->side & 1) ? 0xFF : 0;                       /* 700D..7011: shr / sbb */
        g->hero_flags = t->hero_flags;      /* [C2] is one byte: the town's value goes in (#69) */
        game_town_entry_reset(g);                                       /* [6002] = 79DC */
        /* 701C loads the map and 703D fight.bin through [10C]: STICK 0CDC
         * clears FF1D on each open (FF1E stays).  The port keeps FF1D twice
         * (town / cavern); a Space the town saw -- paging a dialogue -- must
         * not reach the first cavern frame as a swing (#75, pollo_fight f11) */
        g->btn1_edge = t->btn1_edge = 0;
        town_dissolve(t);                                               /* 7033 [2040] over the town screen */
        t->dissolve = 0;
        shell_enter_cavern(s, c->map, c->col, c->row, c->side & 1, 1);
        break; }
    case TOWN_TO_TOWN: {                                                /* 6CE1 */
        int left = (t->action_arg & 0x100) != 0, dest = t->action_arg & 0xFF;
        if (town_load_map(&s->tmap, s->dir, dest)) break;
        town_apply_patches(&s->tmap, g->page);
        if (town_load_banks(s)) break;
        audio_music(s->tmap.music);
        /* 6CE1/6D1F -> change_town_map (6D30) re-enters town.bin at 60B7, not
         * at 601E, so load_backdrop_module / GT_CAPTURE_BACKDROP never run: the
         * ympd panorama and the parallax strips stay on screen exactly as the
         * old map left them.  The phase therefore *carries over* (measured:
         * Muralla's entry frame is 8 columns along, which is cmap's 78-30). */
        int carried_steps = t->back_steps;
        uint8_t carried_anim = t->hero_anim;                            /* 60B7 is past 6050: [E7] stays */
        /* 6D3D/6D58 load through [10C] (0CDC clears FF1D) and 60C3/60C6 clear
         * both edges: town_init's memset is both */
        town_init(t, &s->tmap, &s->ttiles, &s->tspr, &s->thero, g);
        hud_town_panels(g);                                             /* 610F/6127 */
        t->back_steps = carried_steps;
        t->hero_anim = carried_anim;
        s->town_entry = 1;                                              /* 60B7 .. 61B1 */
        t->user = s; t->present = s->town_present; t->side = s->town_side; t->frame_end = s->town_frame_end;
        t->font = &s->tfont; t->dir = s->dir; t->pics = &s->pics;
        t->back = &s->tback;                                            /* still painted: #38 */
        /* 6CE4 / 6D22: reappear at the far end of the new map */
        t->scroll_col = left ? s->tmap.width - 0x24 : 0;
        t->hero_scr_col = left ? 0x1A : 0;
        t->hero_flags = (uint8_t)(left ? 1 : 0);
        town_npc_markers_reset(t);
        g->cur_map = (uint8_t)(0x80 | dest);                            /* 6D30: [C4] only */
        s->transitions++;
        LOG(s, "[town] -> %s (%d columns)\n", s->tmap.label, s->tmap.width);
        break; }
    case TOWN_SHOP: {                                                   /* 6E7E run_shop */
        int dest = t->action_arg & 7;
        LOG(s, "[shop] entering %s (town id %u)\n",
            (const char *[]){"king", "omoya", "sage", "armour", "drug", "church", "bank", "inn"}[dest],
            s->tmap.town_id);
        /* 6E7E run_shop (shop_run): FF24 = 4 (fade), overlay load, 6E9B the dissolve
         * with the score fading under it, 6EA0 `int 60h AX=1` -- the music is
         * *stopped* for the whole visit (no shop overlay touches INT 60h; src/shops.c) */
        if (shop_run(t, dest)) fprintf(stderr, "[shop] cannot load the overlay\n");
        t->btn1_edge = 0;   /* 6ECA 6AA2 reloads through [10C]: STICK 0CDC clears FF1D (FF1E stays) */
        town_apply_patches(&s->tmap, g->page);                          /* 6EDE */
        /* 6EAF..6F06: shop_return never re-marks the NPCs (6C2B).  The door's
         * entry frame lifted them (enter_door 6E61 -> 6C4E), so this return
         * frame_tail draws every NPC cell as the plain tile; the next 61FC
         * frame's npc_update (6B1C -> 6C2B) puts the markers back (#107). */
        if (t->quit) break;
        town_force_redraw(t);                                           /* 6ED4 */
        town_tail_frame(t);                                             /* 6EE1/6EE6: FF1A=28, 68AF */
        t->btn1_edge = t->btn2_edge = 0;                                /* 6EE9/6EEE */
        t->hero_anim = 1;                                               /* 6EF3 */
        LOG(s, "[shop] left: GOLD %u, sword %u, shield %u/%u, LIFE %u/%u, EXP %u, level %u\n",
            (unsigned)g->gold, g->sword, g->shield, g->shield_hp, g->hp, g->max_hp, g->exp, g->level);
        break; }
    case TOWN_PAST_DOOR:
        fprintf(stderr, "[town] the doorway to the past is not implemented\n");
        break;
    }
    t->action = 0;
}

void shell_frame(Shell *s)
{
    /* #52: frame_done runs at the true end of the frame, after any door, gate,
     * shop or transition the frame asked for, so a save state captured here
     * resumes with the next frame's own step.  (A frame that opened a shop or
     * the status screen has run their wait boundaries by now, so the trace
     * counter no longer matches and the save is refused as non-resumable.) */
    if (s->in_town) { town_frame(s); if (s->frame_done) s->frame_done(s); return; }
    int was_intro = s->g.boss_room != 0;
    int clock = s->g.score_clock;
    game_step(&s->g);
    if (clock) { audio_clock_frame(); s->g.ff21 = (uint8_t)audio_clock_sync0(); }   /* [FF21]: F1 */
    /* 61BE/61DB: the [E6] intro ends when the boss overlay clears [E6]
     * (boss_mao1 A36A); the main loop then loads system map 0x1E and puts the
     * hero at (0x18,0x0D) for the last fight. */
    if (was_intro && !s->g.boss_room && !s->g.hero_dead) {
        LOG(s, "[boss] the [E6] walk-in is over: loading MPA0 at (0x18,0x0D)\n");
        /* 61DD loads MPA0 through [10C]: STICK 0CDC drops a Space latched at
         * the last intro boundary (FF1E stays; 628A after 624A's frame is not modelled) */
        s->g.btn1_edge = 0;
        /* 61DD..6234 writes only [80]/[82] (7DC1), [83], [9F00], [9F1A], [9F1C] and loads the map:
         * the hero's screen row [84] (MP90's 0x0D), the enemy count [FF4A] and gfmcga's frame
         * counter [4FFF] carry over.  6FB8 in the frame then walks [84] to [9F00] = 0x0C, which
         * scrolls the map one row (oracle: scroll_row 3, hero row 15 at the first MPA0 frame) */
        uint8_t keep_row = s->g.hero_scr_row, keep_cnt = s->g.obj_index, keep_gf = s->g.gf_frame;
        shell_enter_cavern(s, 0x1E, 0x18, 0x0D + 1, 0, 0);   /* 7DC1 puts him a row below the entry cell */
        s->g.hero_scr_row = keep_row; s->g.obj_index = keep_cnt; s->g.gf_frame = keep_gf;
        s->g.cur_map = 0x1D;            /* 61DD loads MPA0 through [10C] only: [C4] still names MP90 */
        s->g.boss_intro = 0xFF;         /* [9F26] stays set until 624D, after MPA0's first frame (game_step) */
        /* 61F3..6234 is not the door/cave entry: no 7C6E walk-in, and 6150 is reached by `jmp`,
         * past the 60E6 encounter card */
        s->g.walk_in = 0; s->g.hero_entering = 0; s->g.encounter_frames = 0;
        /* 61F3 7E93 restarts the score; F1 (sync 0, [FF21]) comes 1625 driver ticks in.  The load
         * between the music start and the first MPA0 frame's boss call took ~40 of them (oracle: the boss first
         * sees FF21 in frame 159 counted from that first frame, so P = 35..44; 40 is the middle) */
        s->g.ff21 = 0;
        s->g.score_clock = audio_clock_start(s->dir, s->g.page[0xC8], 40) == 0;
        if (s->g.score_clock) s->g.ff21 = (uint8_t)audio_clock_sync0();
    }
    if (s->frame_done) s->frame_done(s);                /* #52: the frame is complete */
}
