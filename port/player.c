/* player.c — STDPLY.BIN, the page <-> Game mapping and the NAME.USR save file
 * (docs/TOWN.md §8, docs/STATE_PAGE.md). */
#include "sar.h"
#include "player.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t pw(const uint8_t *p, int o) { return (uint16_t)(p[o] | p[o + 1] << 8); }
static void spw(uint8_t *p, int o, uint16_t v) { p[o] = (uint8_t)v; p[o + 1] = (uint8_t)(v >> 8); }

void player_page_push(Game *g)
{
    uint8_t *p = g->page;
    page_set_gold24(p, 0x85, g->gold);
    spw(p, 0x8B, g->almas);
    p[0x8D] = g->level;      spw(p, 0x8E, g->exp);
    spw(p, 0x90, g->hp);     p[0x92] = g->sword;   p[0x93] = g->shield;
    spw(p, 0x94, g->shield_hp);
    p[0x98] = g->keys;       p[0x99] = g->lion_keys;
    p[0x9C] = g->hero_crest; p[0x9D] = g->magic_sel; p[0x9E] = g->shoes;
    memcpy(p + 0xAB, g->magic_count, 7);
    spw(p, 0xB2, g->max_hp);
    memcpy(p + 0xB4, g->magic_max, 7);
    p[0xC4] = g->cur_map;    p[0xC5] = g->town_map;
    p[0x49] = g->jashiin_defeated;
    p[0xE4] = g->attack_bonus;
    p[0xE8] = g->hero_dead;
}

void player_page_pull(Game *g)
{
    const uint8_t *p = g->page;
    g->gold = page_gold24(p, 0x85);
    g->almas = pw(p, 0x8B);
    g->level = p[0x8D];      g->exp = pw(p, 0x8E);
    g->hp = pw(p, 0x90);     g->sword = p[0x92];   g->shield = p[0x93];
    g->shield_hp = pw(p, 0x94);
    g->keys = p[0x98];       g->lion_keys = p[0x99];
    g->hero_crest = p[0x9C]; g->magic_sel = p[0x9D]; g->shoes = p[0x9E];
    memcpy(g->magic_count, p + 0xAB, 7);
    g->max_hp = pw(p, 0xB2);
    memcpy(g->magic_max, p + 0xB4, 7);
    g->cur_map = p[0xC4];    g->town_map = p[0xC5];
    g->jashiin_defeated = p[0x49];
    g->attack_bonus = p[0xE4];
}

int player_load_stdply(Game *g, const char *dir)
{
    FILE *f = game_fopen(dir, "STDPLY.BIN");
    if (!f) return -1;
    uint8_t buf[256];
    memset(buf, 0, sizeof buf);
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    if (n < 0xE9) return -1;
    memcpy(g->page, buf, 256);
    player_page_pull(g);
    return 0;
}

/* kenjpro A862: <name>.usr = a raw copy of the 256-byte page, no header. */
int player_save_usr(Game *g, const char *dir, const char *name)
{
    char base[16], path[1024];
    int n = 0;
    for (; n < 8 && name[n]; n++) base[n] = name[n];
    if (n == 0) return -1;
    base[n] = 0;
    player_page_push(g);
    snprintf(path, sizeof path, "%s/%s.usr", dir, base);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t w = fwrite(g->page, 1, 256, f);
    if (fclose(f) || w != 256) return -1;
    return 0;
}

int player_load_usr(Game *g, const char *dir, const char *name)
{
    char base[16], path[1024];
    int n = 0;
    for (; n < 8 && name[n]; n++) base[n] = name[n];
    base[n] = 0;
    snprintf(path, sizeof path, "%s/%s.usr", dir, base);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint8_t buf[256];
    memset(buf, 0, sizeof buf);
    size_t r = fread(buf, 1, sizeof buf, f);
    fclose(f);
    if (r != 256) return -1;
    memcpy(g->page, buf, 256);
    player_page_pull(g);
    return 0;
}
