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
            LOG(s, "[boss] %s \"%s\": HP %u, EXP %u, gold %u, camera column %u%s\n",
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
    load_banks_by_index((Shell *)g->user, g, post_ai, post_enemies);
    return 1;
}

/* town.bin 6FF8 goto_cavern: the MAP_CAVES record hands the hero to fight.bin. */
int shell_enter_cavern(Shell *s, int sys_map, int col, int row, int face_left)
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
    game_place(g, col, row, face_left);
    game_start_walk_in(g, face_left);                                   /* 7C6E */
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
    game_start_walk_in(g, (d->letter & 0x40) != 0);             /* 7C6E */
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

    Game *g = &s->g;
    game_init(g, &s->maps[0], &s->tiles[0]);
    g->user = s; g->present = s->present; g->on_door = on_door; g->on_town = shell_enter_town;
    g->font = &s->tfont; g->pics = &s->pics;                            /* select.bin needs both */
    g->post_boss = post_boss_banks;
    /* STDPLY.BIN is the fresh player record: HP, the training sword and the
     * per-town shop stock masks (docs/TOWN.md §7). */
    if (player_load_stdply(g, s->dir)) fprintf(stderr, "note: STDPLY.BIN not found: the shops will have no stock\n");
    s->ai_index = s->enp_index = -1;
    s->town_tiles_idx = s->town_spr_idx = -1;
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
        shell_enter_cavern(s, c->map, c->col, c->row, c->side & 1);
        break; }
    case TOWN_TO_TOWN: {                                                /* 6CE1 */
        int left = (t->action_arg & 0x100) != 0, dest = t->action_arg & 0xFF;
        if (town_load_map(&s->tmap, s->dir, dest)) break;
        town_apply_patches(&s->tmap, g->page);
        if (town_load_banks(s)) break;
        audio_music(s->tmap.music);
        town_init(t, &s->tmap, &s->ttiles, &s->tspr, &s->thero, g);
        t->user = s; t->present = s->town_present;
        t->font = &s->tfont; t->dir = s->dir; t->pics = &s->pics;
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
    if (s->in_town) town_frame(s);
    else game_step(&s->g);
}
