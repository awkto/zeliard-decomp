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
    b->almas     = boss_info_u16(g, 0xB);
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
    b->ported = 1;                  /* all eleven overlays are ported */
    if (!g->boss_map) {
        /* A [E6]-only room (MP90, FF34 = 0): the door path 6042 -> 6078 `jz 6179` skips both the
         * 60E6 card and 6150, so [9F01] stays 0 until the boss map itself (MPA0) is loaded
         * through 61DD (oracle: jashiin_fight frame 27) */
        g->boss.parts = 0;
        return 0;
    }
    g->boss_knock_left = b->knock_left;                            /* 6150 -> 9F01 */
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

void boss_hit_flash(Game *g, int on) { g->boss.part_hit = (uint8_t)(on ? 0x20 : 0); }

uint8_t boss_readback(Game *g, int (*weak)(uint8_t type))
{
    uint8_t hit = 0;
    g->boss.part_hit = 0;                              /* A2F3: cleared every frame */
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

int boss_ring_col(const Game *g, uint16_t col, uint8_t *bl)
{
    uint16_t scroll = (uint16_t)g->scroll_col, d;
    if (col >= scroll) d = (uint16_t)(col - scroll);                      /* 96A3 */
    else if (col > 0x23) { *bl = (uint8_t)col; return 1; }                /* 96A9..96B0 */
    else d = (uint16_t)((g->map ? g->map->width : 0) - scroll + col);     /* 96B1 */
    *bl = (uint8_t)d;                                                       /* 96BA */
    return d > 0x23;                                                        /* 96BB */
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
    o->type = type; o->hit = g->boss.part_hit; o->phase = phase;
    /* +7..+F: the overlay never writes them (CRAB A69B..A6CA, A57E..A594), so
     * they are whatever BASE:C000+ held there -- in a boss room the tail of
     * the previous map (#71).  Nothing reads them while FF34 is set: the
     * enemy pass 8D19 (respawn, tall records, items) is skipped at 8D1D and
     * the slash probe 6E99 does not run. */
    unsigned a = (unsigned)g->c000_objs + 16u * (unsigned)i;
    if (a + 16 <= sizeof g->c000) {
        const uint8_t *q = g->c000 + a;
        o->flags = q[7]; o->hp = q[8]; o->next = q[9]; o->link = q[10];
        o->home_col = (uint16_t)(q[11] | q[12] << 8); o->home_row = q[13]; o->home_type = q[14]; o->timer = q[15];
    }
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

/* gfmcga 3729: one spawn attempt for a sprite whose top-left cell is drawn
 * at viewport (x, y).  The list lives at EDA0: {x, y, life 3, kind}, FF-ended,
 * at most 32. */
static void spark_spawn(Game *g, uint8_t *list, uint8_t x, uint8_t y);
/* 302C's row loop reaches 3124 -> 3468 for every sprite marker it meets, in
 * the same scan as the 315C cell animation (enemy.c cell_anim), so the two
 * draw KRN_RANDOM interleaved, cell by cell: cell_anim calls this for each
 * marker.  (Run as a second pass after the animation instead, a cavern-6
 * boss's first spark drew the value a lava cell further down the screen had
 * in the original: tarso_fight f2982, #90.)  The rows and columns that spawn:
 * 3468 spawns unless it is the last row (34C0), 3729 only for rows < 0x10;
 * the inner columns 0..26 only -- the edge paths 33E3 / 34E3 never do. */
void boss_spark_cell(Game *g, int c, int r)
{
    if (!(g->boss_map && g->boss_dying)) return;
    if (r >= 0x10 || c < 0 || c >= 27) return;
    spark_spawn(g, g->spark, (uint8_t)c, (uint8_t)r);
}
static void spark_spawn(Game *g, uint8_t *list, uint8_t x, uint8_t y)
{
    if ((krn_random(g) & 0xF) < 0xE) return;                       /* 3733..373E */
    unsigned n = 0;
    while (list[4 * n] != 0xFF) n++;                                /* 3744 */
    if (n >= 0x20) return;                                          /* 3750 */
    uint8_t *p = list + 4 * n, r;
    do r = (uint8_t)(krn_random(g) & 3); while (r == 3);            /* 3756 */
    uint8_t v = (uint8_t)(r - 1 + x);
    if (v == 0xFF) v = 4;                                           /* 3767: sic, not 0 */
    if (v >= 0x1B) v = 0x1A;
    p[0] = v;
    do r = (uint8_t)(krn_random(g) & 3); while (r == 3);            /* 3774 */
    v = (uint8_t)(r - 1 + y);
    if (v == 0xFF) v = 0;
    p[1] = v;
    p[2] = 3;                                                       /* 378C */
    p[3] = (uint8_t)(krn_random(g) & 3);                            /* 378F */
    p[4] = 0xFF;
}

void boss_sparks(Game *g)
{
    uint8_t *list = g->spark;
    /* the spawns were made in the draw's own scan (boss_spark_cell, from
     * cell_anim: 302C's row loop, [4FF1] = 0x12..1, [4FF3] = 0x12-[4FF1] =
     * the row; E900 holds FE under a sprite, so a marker always differs) */
    if (g->boss_dying) {                                            /* 7114 -> [3010] = 379B */
        memcpy(g->spark_drawn, list, sizeof g->spark_drawn);        /* what render.c draws */
        unsigned si = 0, di = 0;
        while (list[si] != 0xFF && si + 4 < sizeof g->spark) {
            /* (the spark is drawn here and its 2x2 E900 cells set to FE) */
            if (--list[si + 2] != 0xFF) { memmove(list + di, list + si, 4); di += 4; }   /* 387F */
            si += 4;
        }
        list[di] = 0xFF;
    } else g->spark_drawn[0] = 0xFF;
    g->boss_state = list[0];
}

/* 0x71CC  EXP + almas once the death animation has finished (71F2 -> 917C). */
void boss_rewards(Game *g)
{
    if (!(g->boss_map && g->boss_defeated && g->boss_state == 0xFF)) return;
    exp_add(g, g->boss.exp);                                        /* 71E8 -> 9715 */
    almas_add(g, g->boss.almas);  /* 71F2 -> 917C: the award is ALMAS, not gold */
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
    if (g->side) {                                                  /* the screen the fade-in at 7395 starts from (#119): the blade as drawn */
        uint8_t sa = g->attacking, sv = (uint8_t)g->attack_var;
        g->attacking = g->drawn_attacking; g->attack_var = g->drawn_attack_var;
        g->side(g, SIDE_SNAP, 0, 0);
        g->attacking = sa; g->attack_var = sv;
    }
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
    enemies_load(g);                                                /* 9659's count into FF4A */
    game_message_tick(g);                                           /* 738F: 7210 once (a box up during the kill, e.g. cavern 7's TOO HOT, ages a frame) */
    /* 73B0 `jmp 6042`: fight.bin restarts on the same map.  6049..6073 clear
     * 9F20..9F22, the shot/magic/spark lists and the boss flags; FF34 is 0
     * now, so 6179 redraws the HUD and 623D refills the ring (6C98), then
     * 628A drops both press edges before the main loop starts again at 629C
     * -- the rest of the pass that called 6F9B never runs. */
    g->ice_slide = g->ice_steps = g->walk_dir = 0;                  /* 6049..6053 */
    g->shots[0].col = 0xFF;                                         /* 605B: EB80 */
    g->boss_state = 0xFF;                                           /* 605E: EDA0 */
    for (int i = 0; i < 4; i++) g->magic[i].live = 0;               /* 6061: EB15 = FFFF */
    g->boss_knock_left = 0;                                         /* 6073: 9F01 */
    game_ring_refill(g);                                            /* 623D -> 6C98 */
    g->gf_frame = 0;            /* 7395 gfmcga [3012] 4192 (the fade-in redraw) counts [4FFF] 8 -> 0 */
    if (g->side) { game_signs_draw(g); g->side(g, SIDE_FADEIN, 0x739A, 0); }               /* its 8 passes over the old picture (#119) */
    audio_music_stop();                                             /* 73A9: int 60h AX=1 -- the boss score, fading since 7120, is cut */
    g->btn1_edge = g->btn2_edge = 0;                                /* 628A: FF1D / FF1E */
    g->loop_restart = 1;
    fprintf(stderr, "[boss] defeated: %d pokes applied, exit door at column %u, "
                    "post-boss AI %d / bank %d\n", npoke, col, pai, pen);
    return 1;
}
