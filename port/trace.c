/* trace.c — serialise the port's state under tools/harness/fields.tsv names
 * (docs/HARNESS.md §4, issue #48).  The schema is the contract; this table is
 * the port's half of it.  `zeliard --trace-fields` lists every name emitted
 * here and tools/tracediff.py refuses names the schema does not know, so the
 * two cannot drift silently.
 *
 * Three sources:
 *   PAGE  the BASE:0000-00FF player record, rebuilt from the engine's named
 *         members exactly as player_page_push / town_page_push would write it
 *         (on a scratch copy: tracing must not change the run);
 *   GAME  a Game member (fight.bin's FF page and 9EED.. locals);
 *   TOWN  a Town member (town.bin's own locals and the town-side input state).
 * plus the four record lists (C010 objects, EB80 shots, EB15 magic, EB60 orbs). */
#include <stddef.h>
#include <string.h>
#include "trace.h"
#include "audio.h"
#include "player.h"

enum { M_FIGHT = 1, M_TOWN = 2, M_ANY = 3 };
enum { S_PAGE, S_GAME, S_TOWN, S_FN };

typedef struct {
    const char *name;
    uint16_t addr;              /* documentation only (fields.tsv is the decoder's source) */
    uint16_t size;              /* bytes in BASE */
    uint8_t  mode;              /* which loop this provider is valid in */
    uint8_t  src;
    uint16_t off, fsz;          /* GAME/TOWN: member offset and size */
    uint32_t (*fn)(const Game *g, const Town *t);
} TField;

/* --- the odd ones: values the port keeps in a different shape ----------- */
/* INT 61h AH: the port's bit 2 is the K chord, which the kernel keeps in FF18 */
static uint32_t f_btns_fight(const Game *g, const Town *t) { (void)t; return g->buttons & 3; }
static uint32_t f_btns_town(const Game *g, const Town *t)  { (void)g; return t->buttons & 3; }
/* FF18: bit0 = Enter (menu); the K chord is the 0x0286 select.bin tests */
static uint32_t f_keys_fight(const Game *g, const Town *t)
{ (void)t; return (g->menu_key ? 1u : 0u) | ((g->buttons & 4) ? 0x0286u : 0u); }
static uint32_t f_keys_town(const Game *g, const Town *t)
{ (void)g; return (t->menu_key ? 1u : 0u) | ((t->buttons & 4) ? 0x0286u : 0u); }
/* FF37: fight.bin 708F clears it before the draw, and gfmcga's flush sets it
 * again when it draws the hero (40F0/40F8, reached from 3E14/3E27), so at
 * 717F it reads 0xFF whenever the hero was drawn.  The port's renderer keeps
 * no such latch.  While dying, 7081 skips the clear and 98FC's loops write
 * it *after* each frame (994B / 9984 / 9998): 0 = the flush draws the hero
 * and sets it, FF = not drawn, already FF -- so it is FF at every boundary
 * then too (oracle: FF on all 63 dying frames).  It is 0 only when the hero
 * was not drawn although not hidden, which the port does not model. */
static uint32_t f_hero_hidden(const Game *g, const Town *t)
{ (void)g; (void)t; return 0xFF; }

/* the game's view of the music driver (audio.c M): [C8], FF24..FF26, [9F02] */
static uint32_t f_mus_c8(const Game *g, const Town *t)      { (void)g; (void)t; return audio_model_c8(); }
static uint32_t f_mus_fade(const Game *g, const Town *t)    { (void)g; (void)t; return audio_model_fade(); }
static uint32_t f_mus_stopped(const Game *g, const Town *t) { (void)g; (void)t; return audio_model_stopped(); }
static uint32_t f_mus_restart(const Game *g, const Town *t) { (void)g; (void)t; return audio_model_restart(); }

#define G(n, a, s, m)  {n, a, s, M_FIGHT, S_GAME, offsetof(Game, m), sizeof(((Game *)0)->m), NULL}
#define T(n, a, s, m)  {n, a, s, M_TOWN,  S_TOWN, offsetof(Town, m), sizeof(((Town *)0)->m), NULL}
#define P(n, a, s, md) {n, a, s, md, S_PAGE, 0, 0, NULL}
#define F(n, a, s, md, fn) {n, a, s, md, S_FN, 0, 0, fn}

static const TField FIELDS[] = {
    /* ---- FF00 page: input state (written by the oracle at the boundary) */
    F("kbd_buttons", 0xFF16, 1, M_FIGHT, f_btns_fight),
    F("kbd_buttons", 0xFF16, 1, M_TOWN,  f_btns_town),
    G("kbd_dirs",    0xFF17, 1, dirs),
    T("kbd_dirs",    0xFF17, 1, dirs),
    F("key_mask",    0xFF18, 2, M_FIGHT, f_keys_fight),
    F("key_mask",    0xFF18, 2, M_TOWN,  f_keys_town),
    G("btn1_edge",   0xFF1D, 1, btn1_edge),
    T("btn1_edge",   0xFF1D, 1, btn1_edge),
    G("btn2_edge",   0xFF1E, 1, btn2_edge),
    T("btn2_edge",   0xFF1E, 1, btn2_edge),
    /* ---- the music driver as the game sees it */
    F("music_idx",     0x00C8, 1, M_ANY,   f_mus_c8),
    F("music_fade",    0xFF24, 1, M_ANY,   f_mus_fade),
    F("music_stopped", 0xFF26, 1, M_ANY,   f_mus_stopped),
    F("music_restart", 0x9F02, 1, M_FIGHT, f_mus_restart),
    /* ---- FF00 page: fight.bin state */
    G("boss_cutscene",  0xFF2E, 1, boss_cutscene),
    G("boss_dying",     0xFF2F, 1, boss_dying),
    G("boss_defeated",  0xFF30, 1, boss_defeated),
    G("boss_map",       0xFF34, 1, boss_map),
    G("hero_map_row",   0xFF35, 1, hero_map_row),
    G("hero_hit_flash", 0xFF36, 1, hero_hit_flash),
    F("hero_hidden",    0xFF37, 1, M_FIGHT, f_hero_hidden),
    G("crouching",      0xFF38, 1, crouching),
    G("on_ladder",      0xFF39, 1, on_ladder),
    G("hero_entering",  0xFF3A, 1, hero_entering),
    G("casting",        0xFF3C, 1, casting),
    G("vstate",         0xFF3D, 1, vstate),
    G("magic_active",   0xFF3E, 1, magic_active),
    G("conveyor",       0xFF42, 1, conveyor),
    G("attacking",      0xFF43, 1, attacking),
    G("attack_type",    0xFF45, 1, attack_type),
    G("attack_var",     0xFF46, 1, attack_var),
    G("thrust_latch",   0xFF47, 1, thrust_latch),
    G("obj_index",      0xFF4A, 1, obj_index),
    G("menu_result",    0xFF4B, 1, menu_result),
    {"player_name", 0xFF6C, 8, M_ANY, S_GAME, offsetof(Game, player_name), 8, NULL},
    G("sfx_request",    0xFF75, 1, sfx_request),
    T("sfx_request",    0xFF75, 1, sfx_request),
    /* ---- fight.bin locals 9EED..9F2D */
    G("msg_timer_a",       0x9EED, 1, msg_timer),
    G("msg_box_a",         0x9EEF, 1, msg_box),
    G("menu_debounce",     0x9EF5, 1, menu_debounce),
    G("hero_home_row",     0x9F00, 1, hero_home_row),
    G("boss_knock_left",   0x9F01, 1, boss_knock_left),
    G("fixture_anim",      0x9F07, 1, fixture_anim),
    G("gf_frame",          0x4FFF, 1, gf_frame),
    G("fall_rows",         0x9F08, 1, fall_rows),
    G("rise_rows",         0x9F09, 1, rise_rows),
    G("crouch_release",    0x9F0A, 1, crouch_release),
    G("diag_jump",         0x9F0B, 1, diag_jump),
    G("conveyor_kick",     0x9F0C, 1, conveyor_kick),
    G("max_rise",          0x9F0D, 1, max_rise),
    G("hit_side",          0x9F0E, 4, hit_side),
    G("contact_damage",    0x9F12, 2, contact_damage),
    G("hero_hit",          0x9F14, 1, hero_hit),
    G("on_updraft",        0x9F15, 1, on_updraft),
    G("conveyor_phase",    0x9F16, 1, conveyor_phase),
    G("on_hazard",         0x9F17, 1, on_hazard),
    G("regen_tick",        0x9F18, 1, regen_tick),
    G("door_msg_latch",    0x9F19, 1, door_msg_latch),
    G("post_boss_pending", 0x9F1E, 1, post_boss_pending),
    G("projectile_count",  0x9F1F, 1, projectile_count),
    G("ice_slide",         0x9F20, 1, ice_slide),
    G("ice_steps",         0x9F21, 1, ice_steps),
    G("walk_dir",          0x9F22, 1, walk_dir),
    G("slide_dir",         0x9F23, 1, slide_dir),
    G("prev_facing",       0x9F24, 1, prev_facing),
    G("heat_timer",        0x9F25, 1, heat_timer),
    G("boss_intro",        0x9F26, 1, boss_intro),
    G("death_anim_a",      0x9F28, 1, death_anim),
    G("death_anim_b",      0x9F29, 1, death_anim_b),
    G("magic_hit_any",     0x9F2A, 1, magic_hit_any),
    G("cast_timer",        0x9F2B, 1, cast_timer),
    /* ---- work areas */
    G("ring",              0xE000, RING_SIZE, ring),
    G("boss_state",        0xEDA0, 1, boss_state),
    /* ---- town.bin locals */
    T("town_was_idle",     0x7C4B, 1, was_idle),
    /* ---- player record (BASE:0049-00E8), from the rebuilt page */
    P("force_death",      0x49, 1, M_ANY),
    P("immortal",         0x7F, 1, M_ANY),
    P("scroll_col",       0x80, 2, M_ANY),
    P("scroll_row",       0x82, 1, M_FIGHT),
    P("hero_scr_col",     0x83, 1, M_ANY),
    P("hero_scr_row",     0x84, 1, M_FIGHT),
    P("gold_hi",          0x85, 1, M_ANY),
    P("gold_lo",          0x86, 2, M_ANY),
    P("bank_hi",          0x88, 1, M_ANY),
    P("bank_lo",          0x89, 2, M_ANY),
    P("almas",            0x8B, 2, M_ANY),
    P("level",            0x8D, 1, M_ANY),
    P("exp",              0x8E, 2, M_ANY),
    P("hp",               0x90, 2, M_ANY),
    P("sword",            0x92, 1, M_ANY),
    P("shield",           0x93, 1, M_ANY),
    P("shield_hp",        0x94, 2, M_ANY),
    P("shield_hp_max",    0x96, 2, M_ANY),
    P("keys",             0x98, 1, M_ANY),
    P("lion_keys",        0x99, 1, M_ANY),
    P("glory_crest",      0x9B, 1, M_ANY),
    P("hero_crest",       0x9C, 1, M_ANY),
    P("magic_sel",        0x9D, 1, M_ANY),
    P("worn_key_item",    0x9E, 1, M_ANY),
    P("tears",            0xA0, 1, M_ANY),
    P("inventory",        0xA1, 5, M_ANY),
    P("potion_slots",     0xA6, 5, M_ANY),
    P("magic_count",      0xAB, 7, M_ANY),
    P("max_hp",           0xB2, 2, M_ANY),
    P("magic_max",        0xB4, 7, M_ANY),
    P("spells_learned",   0xBB, 7, M_ANY),
    P("hero_flags",       0xC2, 1, M_ANY),
    P("door_side",        0xC3, 1, M_ANY),
    P("cur_map",          0xC4, 1, M_ANY),
    P("town_map",         0xC5, 1, M_ANY),
    P("hp_regen_pending", 0xC6, 2, M_ANY),
    P("drug_stock",       0xC9, 9, M_ANY),
    P("sword_stock",      0xD2, 9, M_ANY),
    P("shield_stock",     0xDB, 9, M_ANY),
    P("attack_bonus",     0xE4, 1, M_ANY),
    P("sages",            0xE5, 1, M_ANY),
    P("boss_room",        0xE6, 1, M_ANY),
    P("hero_anim",        0xE7, 1, M_ANY),
    P("hero_dead",        0xE8, 1, M_ANY),
};
#define NFIELDS (sizeof FIELDS / sizeof FIELDS[0])
#define STORY_FLAGS 0x49                /* BASE:0000-0048 */

/* ------------------------------------------------------------ the page */
static void put16(uint8_t *p, int o, unsigned v) { p[o] = (uint8_t)v; p[o + 1] = (uint8_t)(v >> 8); }

/* BASE:0000-00FF as the original would hold it at this boundary */
static void build_page(Game *g, const Town *t, uint8_t img[256])
{
    uint8_t save[256];
    memcpy(save, g->page, 256);
    player_page_push(g);                        /* the named members -> page (scratch) */
    memcpy(img, g->page, 256);
    memcpy(g->page, save, 256);
    if (!t) {                                   /* fight.bin keeps these live in the page */
        put16(img, 0x80, (unsigned)g->scroll_col);
        img[0x82] = g->scroll_row;
        img[0x83] = g->hero_scr_col;
        img[0x84] = g->hero_scr_row;
        img[0xC2] = g->hero_flags;
        put16(img, 0xC6, g->hp_regen_pending);
        img[0xE6] = g->boss_room;
        img[0xE7] = g->hero_anim;
    } else {                                    /* town.bin: the town_page_push set */
        put16(img, 0x80, (unsigned)t->scroll_col);
        img[0x83] = (uint8_t)t->hero_scr_col;
        img[0xC2] = t->hero_flags;
        img[0xE7] = t->hero_anim;
        img[0xC4] = (uint8_t)(0x80 | t->map->index);                  /* [C5] stays g->town_map */
    }
}

/* ------------------------------------------------------------- writers */
typedef struct { FILE *f; int first; } Out;

static void key(Out *o, const char *name)
{
    fprintf(o->f, "%s\"%s\":", o->first ? "" : ",", name);
    o->first = 0;
}
static void emit_int(Out *o, const char *name, uint32_t v) { key(o, name); fprintf(o->f, "%u", v); }
static void emit_hex(Out *o, const char *name, const uint8_t *p, size_t n)
{
    static const char hx[] = "0123456789abcdef";
    key(o, name);
    fputc('"', o->f);
    for (size_t i = 0; i < n; i++) { fputc(hx[p[i] >> 4], o->f); fputc(hx[p[i] & 15], o->f); }
    fputc('"', o->f);
}
/* `size` bytes of BASE whose port copy is `n` bytes at p */
static void emit_bytes(Out *o, const char *name, const uint8_t *p, size_t n, unsigned size)
{
    if (size > 2) {
        uint8_t buf[RING_SIZE];
        size_t k = size < sizeof buf ? size : sizeof buf;
        memset(buf, 0, k);
        memcpy(buf, p, n < k ? n : k);
        emit_hex(o, name, buf, k);
        return;
    }
    uint32_t v = 0;
    if (n == 1) v = p[0];
    else if (n == 2) { uint16_t w; memcpy(&w, p, 2); v = w; }
    else if (n >= 4) { uint32_t w; memcpy(&w, p, 4); v = w; }
    if (size == 1) v &= 0xFF; else v &= 0xFFFF;
    emit_int(o, name, v);
}

static void emit_lists(Out *o, const Game *g)
{
    char nm[48];
    /* [C010] object table, 16-byte records, 0xFFFF-terminated */
    emit_int(o, "obj.n", (uint32_t)g->nobj);
    for (int i = 0; i < g->nobj; i++) {
        const MapObj *e = &g->obj[i];
#define OBJ(fld, v) do { snprintf(nm, sizeof nm, "obj[%d]." fld, i); emit_int(o, nm, (uint32_t)(v)); } while (0)
        OBJ("col", e->col); OBJ("row", e->row); OBJ("rcol", e->rcol); OBJ("type", e->type);
        OBJ("hit", e->hit); OBJ("phase", e->phase); OBJ("flags", e->flags); OBJ("hp", e->hp);
        OBJ("next", e->next); OBJ("link", e->link); OBJ("home_col", e->home_col);
        OBJ("home_row", e->home_row); OBJ("home_type", e->home_type); OBJ("timer", e->timer);
#undef OBJ
    }
    /* EB80 shots, 13-byte records, list ends at col == 0xFF */
    int ns = 0;
    while (ns <= MAX_SHOTS && g->shots[ns].col != 0xFF) ns++;
    emit_int(o, "shot.n", (uint32_t)ns);
    for (int i = 0; i < ns; i++) {
        const Shot *s = &g->shots[i];
#define SH(fld, v) do { snprintf(nm, sizeof nm, "shot[%d]." fld, i); emit_int(o, nm, (uint32_t)(v)); } while (0)
        SH("col", s->col); SH("row", s->row); SH("cell", s->cell); SH("age", s->age);
        SH("life", s->life); SH("flags", s->flags); SH("damage", s->damage);
        SH("last_col", s->last_col); SH("last_row", s->last_row);
#undef SH
    }
    /* EB15 magic sprites: up to 4 x 16 bytes, a list ended by col == 0xFFFF
     * (6061/7E87 empty it; 884D writes the terminator after a bolt).  A spell
     * that ends marks record 0 (8BB5) and the 896E walker, which runs before
     * 717F in the same frame, turns that into the terminator: at the boundary
     * the list is exactly the port's leading live records. */
    int nm_live = 0;
    while (nm_live < 4 && g->magic[nm_live].live) nm_live++;
    emit_int(o, "magic.n", (uint32_t)nm_live);
    for (int i = 0; i < nm_live; i++) {
        const Magic *m = &g->magic[i];
        snprintf(nm, sizeof nm, "magic[%d].live", i); emit_int(o, nm, 1);
#define MG(fld, v) do { snprintf(nm, sizeof nm, "magic[%d]." fld, i); emit_int(o, nm, (uint32_t)(v)); } while (0)
        MG("col", m->col); MG("row", m->row); MG("dir", m->dir); MG("age", m->age);
        MG("anim", m->anim); MG("rcol", m->rcol); MG("srow", m->srow);
#undef MG
    }
    /* EB60 orbs, 4 x 7 bytes; off = phase 0xFF */
    emit_int(o, "orb.n", 4);
    for (int i = 0; i < 4; i++) {
        const Orb *b = &g->orbs[i];
        snprintf(nm, sizeof nm, "orb[%d].live", i); emit_int(o, nm, b->phase != 0xFF);
        if (b->phase == 0xFF) continue;
        snprintf(nm, sizeof nm, "orb[%d].phase", i); emit_int(o, nm, b->phase);
        snprintf(nm, sizeof nm, "orb[%d].speed", i); emit_int(o, nm, b->speed);
        snprintf(nm, sizeof nm, "orb[%d].hits", i);  emit_int(o, nm, b->hits);
    }
}

void trace_input_token(char out[12], uint8_t dirs, uint8_t buttons, uint8_t menu_key)
{
    int n = 0;
    if (dirs & DIR_UP) out[n++] = 'U';
    if (dirs & DIR_DOWN) out[n++] = 'D';
    if (dirs & DIR_LEFT) out[n++] = 'L';
    if (dirs & DIR_RIGHT) out[n++] = 'R';
    if (buttons & 1) out[n++] = 'X';
    if (buttons & 2) out[n++] = 'M';
    if (menu_key) out[n++] = 'E';
    if (buttons & 4) out[n++] = 'K';
    if (!n) out[n++] = '.';
    out[n] = 0;
}

void trace_write_exit(FILE *f, unsigned n, unsigned rng_calls, const char *input, unsigned code)
{
    fprintf(f, "{\"f\":%u,\"site\":\"exit\",\"rng_calls\":%u,\"input\":\"%s\",\"code\":%u}\n",
            n, rng_calls, input, code);
}

void trace_write_site(FILE *f, unsigned n, const char *site, unsigned rng_calls, const char *input)
{
    fprintf(f, "{\"f\":%u,\"site\":\"%s\",\"rng_calls\":%u,\"input\":\"%s\"}\n", n, site, rng_calls, input);
}

void trace_write_frame(FILE *f, unsigned n, const char *site, unsigned rng_calls, const char *input, Game *g, const Town *t)
{
    int mode = t ? M_TOWN : M_FIGHT;
    uint8_t page[256];
    build_page(g, t, page);
    fprintf(f, "{\"f\":%u,\"site\":\"%s\",\"rng_calls\":%u,\"input\":\"%s\",\"fields\":{",
            n, site, rng_calls, input);
    Out o = {f, 1};
    char nm[16];
    for (int i = 0; i < STORY_FLAGS; i++) { snprintf(nm, sizeof nm, "flag_%02x", i); emit_int(&o, nm, page[i]); }
    for (size_t i = 0; i < NFIELDS; i++) {
        const TField *d = &FIELDS[i];
        if (!(d->mode & mode)) continue;
        switch (d->src) {
        case S_PAGE: emit_bytes(&o, d->name, page + d->addr, d->size, d->size); break;
        case S_GAME: emit_bytes(&o, d->name, (const uint8_t *)g + d->off, d->fsz, d->size); break;
        case S_TOWN: emit_bytes(&o, d->name, (const uint8_t *)t + d->off, d->fsz, d->size); break;
        case S_FN:   emit_int(&o, d->name, d->fn(g, t) & (d->size == 1 ? 0xFFu : 0xFFFFu)); break;
        }
    }
    if (!t) emit_lists(&o, g);
    else {                                      /* [C00F] town NPCs, 8-byte records */
        char nm2[32];
        emit_int(&o, "npc.n", (uint32_t)t->map->nnpcs);
        for (int i = 0; i < t->map->nnpcs; i++) {
            const TownNpc *n = &t->map->npcs[i];
#define NP(fld, v) do { snprintf(nm2, sizeof nm2, "npc[%d]." fld, i); emit_int(&o, nm2, (uint32_t)(v)); } while (0)
            NP("col", n->col); NP("sprite", n->sprite); NP("saved", n->saved); NP("anim", n->anim);
            NP("type", n->type); NP("flags", n->flags); NP("script", n->script);
#undef NP
        }
    }
    fputs("}}\n", f);
}

void trace_list_fields(FILE *f)
{
    for (int i = 0; i < STORY_FLAGS; i++) fprintf(f, "flag_%02x\n", i);
    for (size_t i = 0; i < NFIELDS; i++) {
        int dup = 0;
        for (size_t j = 0; j < i; j++) if (!strcmp(FIELDS[j].name, FIELDS[i].name)) dup = 1;
        if (!dup) fprintf(f, "%s\n", FIELDS[i].name);
    }
    static const char *const lists[] = {
        "obj.n", "obj[].col", "obj[].row", "obj[].rcol", "obj[].type", "obj[].hit", "obj[].phase",
        "obj[].flags", "obj[].hp", "obj[].next", "obj[].link", "obj[].home_col", "obj[].home_row",
        "obj[].home_type", "obj[].timer",
        "shot.n", "shot[].col", "shot[].row", "shot[].cell", "shot[].age", "shot[].life",
        "shot[].flags", "shot[].damage", "shot[].last_col", "shot[].last_row",
        "magic.n", "magic[].live", "magic[].col", "magic[].row", "magic[].dir", "magic[].age",
        "magic[].anim", "magic[].rcol", "magic[].srow",
        "orb.n", "orb[].live", "orb[].phase", "orb[].speed", "orb[].hits",
        "npc.n", "npc[].col", "npc[].sprite", "npc[].saved", "npc[].anim", "npc[].type",
        "npc[].flags", "npc[].script", NULL};
    for (int i = 0; lists[i]; i++) fprintf(f, "%s\n", lists[i]);
}
