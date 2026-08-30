/* shell.c — the resource / transition layer shared by every front end.
 * Lifted verbatim out of main.c; the addresses in the comments are GAME.BIN,
 * fight.bin and town.bin offsets. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shell.h"
#include "render.h"
#include "player.h"
#include "shop.h"
#include "boss.h"
#include "audio.h"

#define LOG(s, ...) do { if (!(s)->quiet) fprintf(stderr, __VA_ARGS__); } while (0)

const char *shell_find_dir(const char *hint)
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
    town_init(&s->town, &s->tmap, &s->ttiles, &s->tspr, &s->thero, g);
    s->town.user = s; s->town.present = s->town_present;
    s->town.font = &s->tfont; s->town.dir = s->dir; s->town.pics = &s->pics;
    s->town.back = &s->tback;
    town_place(&s->town, col < 0 ? s->tmap.start_col : col, 0);
    g->cur_map = (uint8_t)(0x80 | idx);
    g->town_map = g->cur_map;
    s->in_town = 1;
    s->transitions++;
    LOG(s, "[town] %s (%d columns, %s, %d NPCs, %d patches)%s: hero at column %d\n",
        s->tmap.label, s->tmap.width, s->tmap.tileset == 0 ? "cpat" : s->tmap.tileset == 1 ? "mpat" : "dpat",
        s->tmap.nnpcs, np, died ? " - the Sage revives you" : "", town_hero_col(&s->town));
    if (died) snprintf(s->town.message, sizeof s->town.message,
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
    if (g->boss_map || g->boss_room) {
        if (boss_init(g) == 0)
            LOG(s, "[boss] %s \"%s\": HP %u, EXP %u, almas %u, camera column %u%s\n",
                boss_overlay_name(m->ai), g->boss.name, g->boss.hp0, g->boss.exp,
                g->boss.almas, g->boss.cam_col, g->boss.knock_left ? ", knocks left" : "");
        else fprintf(stderr, "[boss] overlay %d has no [A002] block\n", m->ai);
    } else {
        g->boss.active = 0; g->boss_knock_left = 0; g->encounter_frames = 0;
    }
}

/* 7305/731F: post_boss_transition swaps in the level record's +6/+7 banks. */
static int post_boss_banks(Game *g, int post_ai, int post_enemies)
{
    load_banks_by_index((Shell *)g->user, g, post_ai, post_enemies);
    return 1;
}

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
    if (map_load_system(&s->maps[slot], s->dir, sys_map)) {
        fprintf(stderr, "[town] cannot load cavern map %d\n", sys_map);
        return 0;
    }
    int np = map_apply_patches(&s->maps[slot], g->page);
    if (gfx_load_tileset(&s->tiles[slot], s->dir, s->maps[slot].tileset)) return 0;
    s->cur = slot;
    shell_load_enemy_banks(s, &s->maps[slot]);
    g->map = &s->maps[slot]; g->tiles = &s->tiles[slot];
    if (from_cave_record) row = row - 10 + s->maps[slot].row_bias;       /* 7005 / 7D2D */
    game_place(g, col, row, face_left);
    if (g->boss_room) game_boss_room_intro(g);                          /* 61A8 */
    else game_start_walk_in(g, face_left);                              /* 7C6E */
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
    if (d->dest_row == 0xFF)                                    /* 7B76: dest_map | 0x80 = a town */
        return shell_enter_town(g, d->dest_map & 0x7F, d->dest_col, 0);
    int slot = s->cur ^ 1;
    if (map_load_system(&s->maps[slot], s->dir, d->dest_map)) {
        fprintf(stderr, "[door] cannot load system map %02x\n", d->dest_map);
        return 0;
    }
    int np = map_apply_patches(&s->maps[slot], g->page);        /* 6BFC */
    if (np) LOG(s, "[map] %d C00C patches applied\n", np);
    if (gfx_load_tileset(&s->tiles[slot], s->dir, s->maps[slot].tileset)) return 0;
    s->cur = slot;
    shell_load_enemy_banks(s, &s->maps[slot]);
    game_enter(g, &s->maps[slot], &s->tiles[slot], d->dest_col, d->dest_row, (d->letter & 0x40) != 0);
    /* 7C11: the door record's byte +8 bit 7 (cached at [9F1D]) is set on every
     * boss room's exit door, the one post_boss_transition installs — load
     * ROKADEMO.BIN and call it, then jump to 7CF4, skipping the walk-in. */
    if (d->dflags & 0x80) {                                     /* 7C18 */
        LOG(s, "[tear] a Tear of Esmesanti (%u of 9)\n", (unsigned)(g->page[0xA0] + 1));
        tear_cutscene(s);
    } else if (g->boss_room)
        game_boss_room_intro(g);                                /* 61A8 */
    else
        game_start_walk_in(g, (d->letter & 0x40) != 0);         /* 7C6E */
    s->transitions++;
    LOG(s, "[door] entered %s (cavern %d, %d cols, tileset MPP%c) at hero (%d,%d)\n", s->maps[slot].name,
        s->maps[slot].cavern, s->maps[slot].width, "123456789AB"[s->maps[slot].tileset],
        game_hero_map_col(g), game_hero_map_row(g));
    return 1;
}

int shell_init(Shell *s, const char *dir_hint, int map_idx)
{
    s->dir = shell_find_dir(dir_hint);
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
    /* rokademo's hero cells + gfmcga's sword/sparkle art + GMMCGA's Tear icon
     * + GAME.BIN's nine slot positions (docs/CUTSCENES.md §5) */
    tear_art_load(&s->tear_art, s->dir);

    Game *g = &s->g;
    game_init(g, &s->maps[0], &s->tiles[0]);
    g->user = s; g->present = s->present; g->on_door = on_door; g->on_town = shell_enter_town;
    g->font = &s->tfont; g->pics = &s->pics;                            /* select.bin needs both */
    g->screen = &s->frame; g->encnt = &s->encnt;
    g->post_boss = post_boss_banks;
    /* STDPLY.BIN is the fresh player record: HP, the training sword and the
     * per-town shop stock masks (docs/TOWN.md §7). */
    if (player_load_stdply(g, s->dir)) fprintf(stderr, "note: STDPLY.BIN not found: the shops will have no stock\n");
    s->ai_index = s->enp_index = -1;
    s->town_tiles_idx = s->town_spr_idx = -1;
    s->town_back_idx = -1;
    return 0;
}

/* town.bin 61FC: run one town frame and act on what it asked for. */
static void town_frame(Shell *s)
{
    Game *g = &s->g;
    Town *t = &s->town;
    t->action = 0;
    town_step(t);
    if (!t->action) return;
    switch (t->action) {
    case TOWN_TO_CAVERN: {                                              /* 6FF8 */
        int i = t->action_arg;
        if (i < 0 || i >= s->tmap.ncaves) { fprintf(stderr, "[town] no cave record %d\n", i); break; }
        const TownCave *c = &s->tmap.caves[i];
        g->page[6] = 0xFF;                                              /* 702E: [06] entered a cavern */
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
        town_init(t, &s->tmap, &s->ttiles, &s->tspr, &s->thero, g);
        t->back_steps = carried_steps;
        t->user = s; t->present = s->town_present;
        t->font = &s->tfont; t->dir = s->dir; t->pics = &s->pics;
        t->back = &s->tback;                                            /* still painted: #38 */
        /* 6CE4 / 6D22: reappear at the far end of the new map */
        t->scroll_col = left ? s->tmap.width - 0x24 : 0;
        t->hero_scr_col = left ? 0x1A : 0;
        t->hero_flags = (uint8_t)(left ? 1 : 0);
        town_npc_markers_reset(t);
        g->cur_map = (uint8_t)(0x80 | dest);
        g->town_map = g->cur_map;
        s->transitions++;
        LOG(s, "[town] -> %s (%d columns)\n", s->tmap.label, s->tmap.width);
        break; }
    case TOWN_SHOP: {                                                   /* 6E7E run_shop */
        int dest = t->action_arg & 7;
        LOG(s, "[shop] entering %s (town id %u)\n",
            (const char *[]){"king", "omoya", "sage", "armour", "drug", "church", "bank", "inn"}[dest],
            s->tmap.town_id);
        if (shop_run(t, dest)) fprintf(stderr, "[shop] cannot load the overlay\n");
        town_apply_patches(&s->tmap, g->page);                          /* 6EAF */
        town_npc_markers_reset(t);
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
    if (s->in_town) { town_frame(s); return; }
    int was_intro = s->g.boss_room != 0;
    game_step(&s->g);
    /* 61BE/61DB: the [E6] intro ends when the boss overlay clears [E6]
     * (boss_mao1 A36A); the main loop then loads system map 0x1E and puts the
     * hero at (0x18,0x0D) for the last fight. */
    if (was_intro && !s->g.boss_room && !s->g.hero_dead) {
        s->g.boss_intro = 0;
        LOG(s, "[boss] the [E6] walk-in is over: loading MPA0 at (0x18,0x0D)\n");
        shell_enter_cavern(s, 0x1E, 0x18, 0x0D + 1, 0, 0);   /* 7DC1 puts him a row below the entry cell */
    }
}
