/* boss.c — the boss protocol: the [A002] info block, the once-per-frame call
 * (8D1D), the part-list rebuild, the ENEMY HP bar, the rewards (71CC) and
 * post_boss_transition (72F1).  docs/ENEMIES.md §1/§3, src/fight.c. */
#include "boss.h"
#include "audio.h"
#include <stdio.h>
#include <string.h>

/* fight.bin 9CBC: the odd indices (plus 16/17/18) are boss overlays. */
static const struct { int idx; const char *name; } BOSSES[] = {
    {BOSS_CRAB, "CRAB"}, {BOSS_TAKO, "TAKO"}, {BOSS_TORI, "TORI"}, {BOSS_ZELA, "ZELA"},
    {BOSS_MEDA, "MEDA"}, {BOSS_LEGA, "LEGA"}, {BOSS_DRGN, "DRGN"}, {BOSS_AKMA, "AKMA"},
    {BOSS_MAO1, "MAO1"}, {BOSS_MAO2, "MAO2"}, {BOSS_ZEL2, "ZEL2"},
};

int boss_overlay_p(int ai_index)
{
    for (unsigned i = 0; i < sizeof BOSSES / sizeof BOSSES[0]; i++)
        if (BOSSES[i].idx == ai_index) return 1;
    return 0;
}

const char *boss_overlay_name(int ai_index)
{
    for (unsigned i = 0; i < sizeof BOSSES / sizeof BOSSES[0]; i++)
        if (BOSSES[i].idx == ai_index) return BOSSES[i].name;
    return "?";
}

/* --------------------------------------------------------- image accessors */
uint8_t boss_img8(const Game *g, unsigned addr)
{
    const AiOverlay *o = g->ai;
    unsigned off = addr - AI_BASE;
    if (!o || !o->loaded || off >= o->len) return 0;
    return o->img[off];
}

uint16_t boss_img16(const Game *g, unsigned addr)
{
    return (uint16_t)(boss_img8(g, addr) | boss_img8(g, addr + 1) << 8);
}

uint8_t  boss_info_u8(const Game *g, unsigned off)  { return boss_img8(g, g->boss.info + off); }
uint16_t boss_info_u16(const Game *g, unsigned off) { return boss_img16(g, g->boss.info + off); }

/* ------------------------------------------------------------------- init */
/* 6150/6162: [A002] -> 9F01 (knock left), video [2010] (name), [200A]/[200C]
 * (the ENEMY bar).  docs/ENEMIES.md §1 corrects FIGHT.md on +3/+9/+B. */
int boss_init(Game *g)
{
    Boss *b = &g->boss;
    memset(b, 0, sizeof *b);
    g->boss_cutscene = g->boss_dying = g->boss_defeated = 0;
    g->post_boss_pending = 0;
    g->boss_state = 0xFF;                                          /* EDA0 (6058) */
    g->boss_knock_left = 0;
    if (!g->ai || !g->ai->loaded) return -1;
    b->index = g->ai->index;
    if (!boss_overlay_p(b->index)) return -1;
    b->info = boss_img16(g, 0xA002);
    if (!b->info) return -1;
    b->active = 1;
    b->start_col = boss_info_u16(g, 0);
    b->start_row = boss_info_u8(g, 2);
    b->hp0       = boss_info_u16(g, 3);
    b->exp       = boss_info_u16(g, 5);
    b->cam_col   = boss_info_u8(g, 7);
    b->knock_left= boss_info_u8(g, 8);
    b->name_ptr  = boss_info_u16(g, 9);
    b->gold      = boss_info_u16(g, 0xB);
    /* The name record is a positioned label {u8 x4, u8 y, u8 xoff_px, u8 len,
     * chars} exactly like every other video [2010] argument (docs/
     * VIDEO_DRIVERS.md §1.1) — docs/ENEMIES.md §1 and src/ai/ai_common.h call
     * the second field a u16 y, which only works for the five bosses whose
     * x offset happens to be 0. */
    if (b->name_ptr) {
        b->name_x4   = boss_img8(g, b->name_ptr);
        b->name_y    = boss_img8(g, b->name_ptr + 1);
        b->name_xoff = boss_img8(g, b->name_ptr + 2);
        unsigned len = boss_img8(g, b->name_ptr + 3);
        if (len > sizeof b->name - 1) len = sizeof b->name - 1;
        for (unsigned i = 0; i < len; i++) b->name[i] = (char)boss_img8(g, b->name_ptr + 4 + i);
        b->name[len] = 0;
    }
    b->col = b->start_col; b->row = b->start_row; b->hp = b->hp0;
    g->boss_knock_left = b->knock_left;                            /* 6150 -> 9F01 */
    b->ported = 1;                  /* all eleven overlays are ported */
    /* 60E6: six flashes of the ENCNT.GRP encounter card, 0x41 ticks apart */
    g->encounter_frames = 12;
    g->boss.parts = 0;
    g->nobj = 0;
    return 0;
}

/* ------------------------------------------------------ the part machinery */
uint16_t boss_hero_col(const Game *g, int n)
{
    unsigned c = (unsigned)g->scroll_col + (unsigned)n;
    if (g->map && (int)c > g->map->width) c = (unsigned)g->map->width;   /* A37E: min(.., width) */
    return (uint16_t)c;
}

uint8_t boss_readback(Game *g, int (*weak)(uint8_t type))
{
    uint8_t hit = 0;
    for (int i = 0; i < g->nobj; i++) {
        MapObj *o = &g->obj[i];
        uint8_t r;
        if ((o->col >> 8) == 0xFF) continue;
        if (ai_map_col_to_ring(g, o->col, &r)) continue;
        o->rcol = r;
        g->ring[game_ring_index(g, o->row, r)] = g->under_sprite[i];
        if ((o->hit & 0x40) && !(hit & 0x80))
            hit = (uint8_t)((o->hit & 0x1F) | ((weak && weak(o->type)) ? 0x80 : 0));
    }
    g->nobj = 0;                                       /* A349: MAP_OBJECTS[0].col = 0xFFFF */
    if (hit & 0x7F) g->boss.hits_taken++;
    return hit;
}

void boss_shot_template(const Game *g, unsigned addr, Shot *out)
{
    memset(out, 0, sizeof *out);
    out->cell   = boss_img8(g, addr + 2);
    out->age    = boss_img8(g, addr + 3);
    out->life   = boss_img8(g, addr + 4);
    out->flags  = boss_img8(g, addr + 5);
    out->damage = boss_img8(g, addr + 6);
}

void boss_paste(Game *g, uint8_t *buf, int bw, int bh, int x, int y,
                int cols, int bpc, unsigned list, unsigned bm)
{
    unsigned k = 0;
    for (int c = 0; c < cols; c++)
        for (int j = 0; j < bpc; j++) {
            uint8_t bits = boss_img8(g, bm++);
            for (int i = 0; i < 8; i++) {
                if (!(bits & (0x80 >> i))) continue;
                uint8_t v = boss_img8(g, list + k++);
                int bx = x + c, by = y + j * 8 + i;
                if (bx >= 0 && bx < bw && by >= 0 && by < bh) buf[bx * bh + by] = v;
            }
        }
}

void boss_parts_begin(Game *g) { g->nobj = 0; g->boss.parts = 0; }

void boss_part(Game *g, uint16_t col, uint8_t row, uint8_t type, uint8_t phase)
{
    if (g->nobj >= MAX_OBJS) return;
    if (g->map && g->map->width > 0) col = (uint16_t)(col % (unsigned)g->map->width);
    uint8_t r;
    if (ai_map_col_to_ring(g, col, &r)) return;         /* outside the ring: not placed */
    int i = g->nobj++;
    MapObj *o = &g->obj[i];
    memset(o, 0, sizeof *o);
    o->col = col; o->row = (uint8_t)(row & 0x3F); o->rcol = r;
    o->type = type; o->hit = 0; o->phase = phase;
    o->home_col = 0xFFFF; o->flags = 0x20;              /* never respawned by 8D90 */
    int p = game_ring_index(g, o->row, r);
    g->under_sprite[i] = g->ring[p];
    g->ring[p] = (uint8_t)(0x80 | i);
    g->boss.parts++;
}

void boss_parts_end(Game *g) { (void)g; }

/* A796 / A503 / A5BA / A56C: HP -= d, redraw the bar (video [200C]), and at
 * zero start the death cutscene. */
void boss_damage(Game *g, unsigned d)
{
    Boss *b = &g->boss;
    b->hp = (uint16_t)(b->hp > d ? b->hp - d : 0);
    if (b->hp == 0 && !g->boss_cutscene) { b->death_cnt = 0; g->boss_cutscene = 0xFF; }
}

/* the shared 40-frame death shell: returns the counter *before* the tick and
 * sets boss_defeated at 0x28. */
uint8_t boss_death_tick(Game *g)
{
    Boss *b = &g->boss;
    uint8_t t = b->death_cnt;
    if (t >= 0x28) { g->boss_defeated = 0xFF; return t; }
    if (!g->boss_dying) audio_music_fade(10);         /* fight.bin 1123: FF24 = 0x0A */
    g->boss_dying = 0xFF;
    b->death_cnt++;
    return t;
}

/* ---------------------------------------------------------------- dispatch */
void boss_update(Game *g)
{
    Boss *b = &g->boss;
    if (!b->active) return;
    switch (b->index) {
    case BOSS_CRAB: boss_crab_entry(g); return;
    case BOSS_TAKO: boss_tako_entry(g); return;
    case BOSS_TORI: boss_tori_entry(g); return;
    case BOSS_ZELA: case BOSS_ZEL2: boss_zela_entry(g); return;
    case BOSS_MEDA: boss_meda_entry(g); return;
    case BOSS_LEGA: boss_lega_entry(g); return;
    case BOSS_DRGN: boss_drgn_entry(g); return;
    case BOSS_AKMA: boss_akma_entry(g); return;
    case BOSS_MAO1: boss_mao1_entry(g); return;
    case BOSS_MAO2: boss_mao2_entry(g); return;
    default: boss_generic_entry(g); return;
    }
}

/* 0x71CC  EXP + gold once the death animation has finished. */
void boss_rewards(Game *g)
{
    if (!(g->boss_map && g->boss_defeated && g->boss_state == 0xFF)) return;
    exp_add(g, g->boss.exp);                                        /* 71E8 -> 9715 */
    gold_add(g, g->boss.gold);                                      /* 71F1 -> 917C */
    g->post_boss_pending = 0xFF;                                    /* 9F1E */
}

void boss_set_post_hook(Game *g, PostBossFn fn) { g->post_boss = fn; }

/* 0x72F1  Post-boss transition. */
int post_boss_transition(Game *g)
{
    if (g->hero_dead) return 0;                                     /* 72F1 */
    Map *m = (Map *)(void *)g->map;
    if (!m || !m->raw) return 0;
    int pai = m->post_ai, pen = m->post_enemies;
    g->post_boss_pending = 0;
    if (g->post_boss) g->post_boss(g, pai, pen);                    /* 7305/731F: reload both banks */
    g->boss_map = 0; g->boss_room = 0;                              /* 734C: [FF34] = 0 */
    g->boss.active = 0;
    g->boss_cutscene = g->boss_dying = g->boss_defeated = 0;

    /* 7351: the level record's {u16 ptr, u16 val} pokes, terminated by FFFF.
     * They hit the map image (>= C000) or the player record page (< 0x100). */
    size_t o = m->lvl_off + 8;
    int npoke = 0;
    while (o + 4 <= m->rawlen) {
        uint16_t addr = (uint16_t)(m->raw[o] | m->raw[o + 1] << 8);
        if (addr == 0xFFFF) break;
        uint16_t val = (uint16_t)(m->raw[o + 2] | m->raw[o + 3] << 8);
        if (addr >= 0xC000 && (size_t)(addr - 0xC000) + 1 < m->rawlen) {
            m->raw[addr - 0xC000] = (uint8_t)val;
            m->raw[addr - 0xC000 + 1] = (uint8_t)(val >> 8);
        } else if (addr + 1 < 0x100) {
            g->page[addr] = (uint8_t)val;
            g->page[addr + 1] = (uint8_t)(val >> 8);
        }
        npoke++;
        o += 4;
    }
    /* 7365: the exit door appears where the hero is standing (+9 when the
     * ring cell 5 columns to his left is not empty). */
    unsigned col = (unsigned)g->scroll_col + g->hero_scr_col;
    if (g->ring[game_ring_add(game_hero_cell(g), -5)]) col += 9;    /* 7373 */
    if ((int)col >= m->width) col -= (unsigned)m->width;
    size_t dl = (size_t)((m->raw[0xA] | m->raw[0xB] << 8) - 0xC000);
    if (dl + 1 < m->rawlen) { m->raw[dl] = (uint8_t)col; m->raw[dl + 1] = (uint8_t)(col >> 8); }

    char name[16];
    memcpy(name, m->name, sizeof name);
    map_parse(m, m->raw, m->rawlen);
    memcpy(m->name, name, sizeof name);
    enemies_load(g);                                                /* 73B0 -> 6042 */
    g->boss_state = 0xFF;
    fprintf(stderr, "[boss] defeated: %d pokes applied, exit door at column %u, "
                    "post-boss AI %d / bank %d\n", npoke, col, pai, pen);
    return 1;
}
