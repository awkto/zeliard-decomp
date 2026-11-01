/* savestate.c — the engine state blob (issue #52).
 *
 * Layout (all integers little-endian, no struct dumps, no padding):
 *
 *   offset 0   8   magic "ZELSTATE"
 *   offset 8   4   version (SAVESTATE_VERSION)
 *   offset 12  4   payload length in bytes
 *   offset 16  N   payload
 *   offset 16+N 4  CRC-32 (IEEE) of the payload
 *
 * The payload is written field by field through the IO* macros below, which
 * write little-endian and read with a bounds-checked cursor; save and load run
 * the *same* sequence, so the two cannot drift.  Pointers are never written:
 * on load the shell is re-entered into the saved map/town (which rebuilds the
 * resource pointers) before the dynamic fields are overwritten.
 *
 * Only `Shot.script` is an engine pointer that is live across frames (the
 * eai2 arc-shot path).  It is written as its bytes and restored into a static
 * pool, because its two source tables are file-static in ai_eai2.c and their
 * addresses differ between processes. */
#include "savestate.h"
#include "player.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------- plumbing */
static void put32le(uint8_t *p, uint32_t v)
{ p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t get32le(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(c & 1)));
    }
    return c ^ 0xFFFFFFFFu;
}

/* writer: a growable memory buffer, flushed to the file once the CRC is known */
typedef struct { uint8_t *p; size_t n, cap; int err; } Wr;
static void w8(Wr *w, uint8_t v)
{
    if (w->err) return;
    if (w->n == w->cap) {
        size_t nc = w->cap ? w->cap * 2 : 8192;
        uint8_t *np = realloc(w->p, nc);
        if (!np) { w->err = 1; return; }
        w->p = np; w->cap = nc;
    }
    w->p[w->n++] = v;
}
static void w16(Wr *w, uint16_t v) { w8(w, (uint8_t)v); w8(w, (uint8_t)(v >> 8)); }
static void w32(Wr *w, uint32_t v) { w8(w, (uint8_t)v); w8(w, (uint8_t)(v >> 8)); w8(w, (uint8_t)(v >> 16)); w8(w, (uint8_t)(v >> 24)); }
static void wmem(Wr *w, const void *p, size_t n)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n && !w->err; i++) w8(w, b[i]);
}

/* reader: a bounds-checked cursor over the payload; any overrun sets `err` and
 * leaves the destination zeroed rather than reading past the buffer */
typedef struct { const uint8_t *p; size_t n, off; int err; } Rd;
static uint8_t r8(Rd *r)
{ if (r->off + 1 > r->n) { r->err = 1; return 0; } return r->p[r->off++]; }
static uint16_t r16(Rd *r)
{ if (r->off + 2 > r->n) { r->err = 1; return 0; } uint16_t v = (uint16_t)(r->p[r->off] | r->p[r->off + 1] << 8); r->off += 2; return v; }
static uint32_t r32(Rd *r)
{ if (r->off + 4 > r->n) { r->err = 1; return 0; } uint32_t v = get32le(r->p + r->off); r->off += 4; return v; }
static void rmem(Rd *r, void *d, size_t n)
{ if (r->off + n > r->n) { r->err = 1; memset(d, 0, n); return; } memcpy(d, r->p + r->off, n); r->off += n; }

/* the restored arc-shot scripts (ai_eai2's arc_r / arc_l); one slot per shot */
static uint8_t script_pool[MAX_SHOTS + 1][256];
static int     script_pool_next;

/* ------------------------------------------------------------- the state */
/* One sequence for both directions.  `w` is the writer (save), `r` the reader
 * (load); exactly one is non-NULL.  On load the shell is re-entered after the
 * page and the mode are known, so the resource pointers are valid before the
 * rest of the fields are read. */
#define IO8(v)   do { if (w) w8(w, (uint8_t)(v));    else (v) = (uint8_t)r8(r); } while (0)
#define IO16(v)  do { if (w) w16(w, (uint16_t)(v));  else (v) = (uint16_t)r16(r); } while (0)
#define IO32(v)  do { if (w) w32(w, (uint32_t)(v));  else (v) = (uint32_t)r32(r); } while (0)
#define IOI(v)   do { if (w) w32(w, (uint32_t)(int32_t)(v)); else (v) = (int)r32(r); } while (0)
#define IOU(v)   do { if (w) w32(w, (uint32_t)(v));  else (v) = (unsigned)r32(r); } while (0)
#define IOM(p, n) do { if (w) wmem(w, (p), (n));     else rmem(r, (p), (n)); } while (0)

static int state_io(Shell *s, Wr *w, Rd *r, SavestateMeta *meta)
{
    Game *g = &s->g;
    Town *t = &s->town;
    int loading = (w == NULL);

    /* ---- meta: the trace frame this boundary is, and the RNG base ---- */
    IOU(meta->frame);
    IOU(meta->rng_calls);

    /* ---- the shell's mode and the identity needed to rebuild resources ---- */
    uint8_t in_town   = loading ? 0 : (uint8_t)(s->in_town ? 1 : 0);
    uint8_t cur_map   = loading ? 0 : g->cur_map;
    uint8_t have_tmap = loading ? 0 : (uint8_t)(t->map != NULL);
    uint8_t town_idx  = loading ? 0 : (uint8_t)(t->map ? t->map->index : 0);
    int     town_entry = loading ? 0 : s->town_entry;
    int     town_walk  = loading ? 0 : s->town_walk_in;
    IO8(in_town); IO8(cur_map); IO8(have_tmap); IO8(town_idx);
    IOI(town_entry); IOI(town_walk);

    /* ---- the page: the re-entry applies its C00C patches from it ---- */
    uint8_t pagebuf[256];
    if (w) memcpy(pagebuf, g->page, 256);
    IOM(pagebuf, 256);
    if (loading) {
        memcpy(g->page, pagebuf, 256);
        int ok = in_town ? shell_enter_town(g, town_idx, -1, 0)
                         : shell_enter_cavern(s, cur_map, 0, 0, 0, 0);
        if (!ok) return -1;
        s->in_town = in_town;
        s->town_entry = town_entry;
        s->town_walk_in = town_walk;
        memcpy(g->page, pagebuf, 256);      /* the entry writes [C4]/[C8] */
    }

    /* ---- the live cavern map's image, pokes included ----
     * post_boss_transition (72F1) rewrites the level record's {ptr,val} pokes
     * into the map image and moves the exit door, then re-parses; the shell's
     * entry points reload the map from disk, so without the poked image the
     * exit door comes back closed.  Save the whole decompressed .mdt and
     * re-parse it on load (the town's mutable grid is saved separately). */
    {
        uint32_t rawlen = loading ? 0
                        : (!in_town && g->map && g->map->raw ? (uint32_t)g->map->rawlen : 0);
        IO32(rawlen);
        if (rawlen) {
            if (rawlen > (1u << 20)) return -1;
            uint8_t *mb = malloc(rawlen);
            if (!mb) return -1;
            if (w) memcpy(mb, g->map->raw, rawlen);
            IOM(mb, rawlen);
            if (loading) {
                Map *mm = (Map *)(void *)g->map;    /* the shell's own slot; the poke is in-place */
                char name[16];
                memcpy(name, mm->name, sizeof name);
                map_parse(mm, mb, rawlen);
                memcpy(mm->name, name, sizeof name);
            }
            free(mb);
        }
    }

    /* ---- the per-overlay mutable AI statics (cavern 2 / 7 shot ranges) ---- */
    {
        uint8_t dr, dl;
        if (w) ai_priv_get(g, &dr, &dl);
        IO8(dr); IO8(dl);
        if (loading) ai_priv_set(g, dr, dl);
    }

    /* ---- Game: render bookkeeping ---- */
    IO8(g->hero_drawn_ok); IOI(g->hero_drawn_row); IOI(g->hero_drawn_col);
    IO8(g->sword_blk_ok);  IOI(g->sword_blk_row);  IOI(g->sword_blk_col);

    /* ---- Game: the two rings ---- */
    IOM(g->ring, RING_SIZE);
    IOM(g->bg_ring, RING_SIZE);

    /* ---- Game: camera and the hero record ---- */
    IOI(g->scroll_col);
    IO8(g->scroll_row); IO8(g->hero_scr_col); IO8(g->hero_scr_row);
    IO8(g->hero_flags); IO8(g->hero_anim); IO8(g->hero_dead);
    IO8(g->shoes); IO8(g->shield);
    IO16(g->hp); IO16(g->max_hp);

    /* ---- Game: the FF00 page ---- */
    IO8(g->hero_map_row); IO8(g->hero_hidden); IO8(g->crouching); IO8(g->on_ladder);
    IO8(g->hero_entering); IO8(g->vstate); IO8(g->conveyor); IO8(g->hero_hit_flash);

    /* ---- Game: fight.bin locals 9F00.. ---- */
    IO8(g->hero_home_row); IO8(g->fall_rows); IO8(g->rise_rows); IO8(g->crouch_release);
    IO8(g->diag_jump); IO8(g->conveyor_kick); IO8(g->max_rise);
    IO8(g->hero_hit); IO8(g->on_updraft); IO8(g->conveyor_phase); IO8(g->on_hazard);
    IO8(g->regen_tick); IO8(g->door_msg_latch);
    IO8(g->ice_slide); IO8(g->ice_steps); IO8(g->walk_dir); IO8(g->slide_dir); IO8(g->prev_facing);

    /* ---- Game: the live [C010] object table ---- */
    for (int i = 0; i < MAX_OBJS; i++) {
        MapObj *e = &g->obj[i];
        IO16(e->col); IO8(e->row); IO8(e->rcol); IO8(e->type); IO8(e->hit);
        IO8(e->phase); IO8(e->flags); IO8(e->hp); IO8(e->next); IO8(e->link);
        IO16(e->home_col); IO8(e->home_row); IO8(e->home_type); IO8(e->timer);
    }
    IOI(g->nobj);
    IOM(g->under_sprite, sizeof g->under_sprite);
    IO8(g->obj_index);

    /* ---- Game: BASE:C000-DFFF and the object-table offset ---- */
    IOM(g->c000, sizeof g->c000);
    IO16(g->c000_objs);

    /* ---- Game: the player record ---- */
    IO8(g->level); IO16(g->exp); IO32(g->gold); IO16(g->almas); IO8(g->sword);
    IO16(g->shield_hp); IO8(g->keys); IO8(g->lion_keys); IO8(g->hero_crest);
    IO16(g->hp_regen_pending); IO8(g->attack_bonus); IO8(g->hud_panels);

    /* ---- Game: combat ---- */
    IO8(g->attacking); IO8(g->attack_type); IO8(g->attack_var); IO8(g->thrust_latch);
    IO8(g->buttons); IO8(g->btn1_edge); IO8(g->btn2_edge);
    IOM(g->hit_side, sizeof g->hit_side); IO16(g->contact_damage); IO8(g->sfx_request);

    /* ---- Game: projectiles / magic / orbs ---- */
    for (int i = 0; i <= MAX_SHOTS; i++) {
        Shot *sh = &g->shots[i];
        IO8(sh->col); IO8(sh->row); IO8(sh->cell); IO8(sh->age); IO8(sh->life);
        IO8(sh->flags); IO8(sh->damage); IO16(sh->drawn);
        IO8(sh->last_col); IO8(sh->last_row);
        /* the script path (eai2 arc shots): its bytes, restored into the pool */
        if (w) {
            if (sh->script) {
                size_t len = 1;
                while (len < 255 && sh->script[len - 1] != 0xFF) len++;
                w8(w, 1); w8(w, (uint8_t)len); wmem(w, sh->script, len);
            } else w8(w, 0);
        } else {
            uint8_t present = r8(r);
            if (present) {
                uint8_t len = r8(r);
                if (len == 0) { r->err = 1; sh->script = NULL; }
                else {
                    if (script_pool_next > MAX_SHOTS) script_pool_next = MAX_SHOTS;
                    uint8_t *slot = script_pool[script_pool_next++];
                    memset(slot, 0, 256);
                    rmem(r, slot, len);
                    sh->script = slot;
                }
            } else sh->script = NULL;
        }
    }
    IO8(g->projectile_count);
    for (int i = 0; i < 4; i++) {
        Magic *m = &g->magic[i];
        IO16(m->col); IO8(m->row); IO8(m->dir); IO8(m->age); IO8(m->anim);
        IO8(m->rcol); IO8(m->srow); IO8(m->live);
    }
    for (int i = 0; i < 4; i++) { IO8(g->orbs[i].phase); IO8(g->orbs[i].speed); IO8(g->orbs[i].hits); }
    IO8(g->magic_sel); IOM(g->magic_count, 7); IOM(g->magic_max, 7);
    IO8(g->casting); IO8(g->magic_active); IO8(g->cast_timer); IO8(g->magic_hit_any);
    IOU(g->magic_casts); IOU(g->shots_fired);

    /* ---- Game: heat / animation / boss flags ---- */
    IO8(g->heat_timer); IO8(g->gf_frame);
    IO8(g->boss_map); IO8(g->boss_room); IO8(g->boss_cutscene);
    IO8(g->boss_dying); IO8(g->boss_defeated);
    IO8(g->boss_state);
    IOM(g->spark, sizeof g->spark);
    IOM(g->spark_drawn, sizeof g->spark_drawn);
    IO8(g->boss_knock_left); IO8(g->post_boss_pending); IO8(g->loop_restart); IO8(g->boss_intro);
    IOU(g->encounter_frames);

    /* ---- Game: the boss protocol ---- */
    IOI(g->boss.active); IOI(g->boss.index); IO16(g->boss.info);
    IO16(g->boss.start_col); IO8(g->boss.start_row); IO16(g->boss.hp0); IO16(g->boss.exp);
    IO8(g->boss.cam_col); IO8(g->boss.knock_left); IO16(g->boss.name_ptr);
    IO8(g->boss.name_x4); IO8(g->boss.name_y); IO8(g->boss.name_xoff); IO16(g->boss.almas);
    IOM(g->boss.name, sizeof g->boss.name);
    IO16(g->boss.col); IO8(g->boss.row); IO16(g->boss.hp);
    IO8(g->boss.death_cnt); IO8(g->boss.parts); IO8(g->boss.part_hit); IO8(g->boss.ported);
    IO8(g->boss.pose); IO8(g->boss.walk_dir); IO8(g->boss.parity);
    IO8(g->boss.u1); IO8(g->boss.u2); IO8(g->boss.u3); IO8(g->boss.u4); IO8(g->boss.u5);
    IO8(g->boss.u6); IO8(g->boss.u7); IO8(g->boss.u8_); IO8(g->boss.u9);
    IOM(g->boss.st, sizeof g->boss.st);
    for (int i = 0; i < 4; i++) IO16(g->boss.sw[i]);
    IOU(g->boss.hits_taken);

    /* ---- Game: RNG, deaths, entry point, name ---- */
    IO16(g->rng); IOU(g->rng_calls); IO8(g->harness_rng);
    IOU(g->deaths); IO8(g->death_anim); IO8(g->death_anim_b); IO8(g->death_phase); IO8(g->death_blink);
    IOI(g->entry_col); IOI(g->entry_row); IO8(g->entry_face);
    IOM(g->player_name, sizeof g->player_name);
    IO8(g->town_map); IO8(g->cur_map); IO8(g->jashiin_defeated);

    /* ---- Game: fixtures ---- */
    for (int i = 0; i < 256; i++) {
        Fixture *f = &g->fix[i];
        IO16(f->col); IO8(f->row); IO8(f->cell); IO8(f->kind); IO8(f->var); IO8(f->state);
        IO16(f->lim_l); IO16(f->lim_r); IO16(f->drawn_col); IO8(f->drawn_row); IO8(f->drawn);
    }
    IOI(g->nfix); IO8(g->fixture_anim);

    /* ---- Game: the status-screen glue and the message box ---- */
    IO8(g->menu_key); IO8(g->menu_debounce); IO8(g->menu_result);
    IO8(g->msg_timer); IO8(g->msg_box); IO16(g->msg_x);
    IOM(g->msg_bg, sizeof g->msg_bg);

    /* ---- Game: the map-transition walk-in ---- */
    IOI(g->walk_in); IOI(g->walk_in_x); IOI(g->walk_in_dir);

    /* ---- Game: input state and the port's own counters ---- */
    IO8(g->dirs); IOU(g->frame_no); IOU(g->hazard_frames);
    IOM(g->message, sizeof g->message);

    /* ---- Town ---- */
    IOI(t->scroll_col); IOI(t->back_steps); IOI(t->hero_scr_col);
    IO8(t->hero_flags); IO8(t->hero_anim); IO8(t->was_idle);
    IO8(t->dirs); IO8(t->buttons); IO8(t->btn1_edge); IO8(t->btn2_edge); IO8(t->sfx_request);
    IOU(t->frame_no);
    IOM(t->message, sizeof t->message);
    IOI(t->action); IOI(t->action_arg); IOI(t->dlg_forced); IOI(t->quit);
    IOM(t->scr_copy, sizeof t->scr_copy);
    IO8(t->menu_key); IO8(t->last_ascii); IO8(t->menu_debounce);

    /* ---- the town map's mutable bytes: the grid and the NPC records ---- */
    if (have_tmap) {
        if (loading && !t->map) return -1;
        int nnpcs = loading ? 0 : t->map->nnpcs;
        IOM(t->map->grid, sizeof t->map->grid);
        IOI(nnpcs);
        for (int i = 0; i < 32; i++) {
            TownNpc *n = &t->map->npcs[i];
            IO16(n->col); IO8(n->sprite); IO8(n->saved); IO8(n->anim);
            IO8(n->type); IO8(n->flags); IO8(n->script);
        }
        if (loading) t->map->nnpcs = nnpcs;
    }

    return (w ? w->err : r->err) ? -1 : 0;
}
#undef IO8
#undef IO16
#undef IO32
#undef IOI
#undef IOU
#undef IOM

/* ------------------------------------------------------------- public */
int savestate_save(Shell *s, const char *path, unsigned frame, unsigned rng_calls)
{
    if (s->in_town) {
        if (s->town.status || s->town.shop || s->town.dlg.active) {
            fprintf(stderr, "[state] cannot save: a town overlay owns the loop\n");
            return -1;
        }
    } else if (s->g.status || s->g.tear) {
        fprintf(stderr, "[state] cannot save: a cavern overlay owns the loop\n");
        return -1;
    }
    Wr w = {0, 0, 0, 0};
    SavestateMeta meta = { frame, rng_calls };
    if (state_io(s, &w, NULL, &meta) || w.err) { free(w.p); return -1; }

    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "[state] cannot write %s\n", path); free(w.p); return -1; }
    uint8_t hdr[16];
    memcpy(hdr, SAVESTATE_MAGIC, 8);
    put32le(hdr + 8, SAVESTATE_VERSION);
    put32le(hdr + 12, (uint32_t)w.n);
    int ok = fwrite(hdr, 1, 16, f) == 16 && fwrite(w.p, 1, w.n, f) == w.n;
    uint8_t c4[4];
    put32le(c4, crc32(w.p, w.n));
    ok = ok && fwrite(c4, 1, 4, f) == 4;
    if (fclose(f)) ok = 0;
    free(w.p);
    if (!ok) { fprintf(stderr, "[state] cannot write %s\n", path); return -1; }
    return 0;
}

int savestate_load(Shell *s, const char *path, SavestateMeta *meta)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "[state] cannot read %s\n", path); return -1; }
    size_t cap = 8192, n = 0;
    uint8_t *buf = malloc(cap);
    if (!buf) { fclose(f); return -1; }
    for (;;) {
        if (n == cap) {
            if (cap >= (4u << 20)) { free(buf); fclose(f); fprintf(stderr, "[state] %s: too large\n", path); return -1; }
            cap *= 2;
            uint8_t *nb = realloc(buf, cap);
            if (!nb) { free(buf); fclose(f); return -1; }
            buf = nb;
        }
        size_t rd = fread(buf + n, 1, cap - n, f);
        if (rd == 0) break;
        n += rd;
    }
    fclose(f);

    if (n < 20 || memcmp(buf, SAVESTATE_MAGIC, 8) != 0) {
        free(buf); fprintf(stderr, "[state] %s: not a state file\n", path); return -1;
    }
    uint32_t ver = get32le(buf + 8);
    if (ver != SAVESTATE_VERSION) {
        free(buf); fprintf(stderr, "[state] %s: version %u unsupported (want %u)\n", path, ver, SAVESTATE_VERSION); return -1;
    }
    uint32_t plen = get32le(buf + 12);
    if ((size_t)plen + 20 != n) {
        free(buf); fprintf(stderr, "[state] %s: truncated or corrupt (%zu bytes, payload %u)\n", path, n, plen); return -1;
    }
    if (crc32(buf + 16, plen) != get32le(buf + 16 + plen)) {
        free(buf); fprintf(stderr, "[state] %s: checksum mismatch\n", path); return -1;
    }

    script_pool_next = 0;
    Rd r = { buf + 16, plen, 0, 0 };
    int rc = state_io(s, NULL, &r, meta);
    if (rc || r.err) { free(buf); fprintf(stderr, "[state] %s: corrupt payload\n", path); return -1; }
    free(buf);
    return 0;
}
