/* enemy.c — the C010 object table: the AI overlay's data tables, the per-frame
 * enemy pass (8D19), the death animation (90E6), respawning (94FF) and the
 * item/drop state machine (8E14).  docs/FIGHT.md §7-§8, src/fight.c. */
#include "enemy.h"
#include "boss.h"
#include "sar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------- the overlay */
static uint16_t ov16(const AiOverlay *o, unsigned addr)
{
    unsigned off = addr - AI_BASE;
    if (off + 1 >= o->len) return 0;
    return (uint16_t)(o->img[off] | o->img[off + 1] << 8);
}

/* fight.bin 7EBB loads the AI overlay named by the level record's byte +3
 * through the request table at 9CBC: 0 EAI1, 1 CRAB, 2 EAI2, 3 TAKO, 4 EAI3,
 * 5 TORI, 6 EAI4, 7 ZELA, 8 EAI5, 9 MEDA, 10 EAI6, 11 LEGA, 12 EAI7, 13 DRGN,
 * 14 EAI8, 15 AKMA, 16 MAO1, 17 MAO2, 18 ZEL2 — raw to BASE:A000.  The values
 * below are the 0-based ZELRES3 indices (the table's res# minus 1). */
static const int AI_RES[19] = {1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 16, 8, 17, 18, 19, 15};

int ai_load(AiOverlay *o, const char *dir, int ai_index)
{
    memset(o, 0, sizeof *o);
    if (ai_index < 0 || ai_index > 18) return -1;
    size_t len;
    uint8_t *img = sar_load(dir, 2, AI_RES[ai_index], 1, &len);
    if (!img || len < 0xB0) { free(img); return -1; }
    o->img = img; o->len = len; o->index = ai_index;
    memcpy(o->exp, img + 0x08, 8);                                      /* A008 */
    memcpy(o->contact, img + 0x10, 16);                                 /* A010 */
    for (int i = 0; i < 32; i++) {
        o->frame_l[i] = (uint16_t)(img[0x30 + 2 * i] | img[0x31 + 2 * i] << 8);
        o->frame_r[i] = (uint16_t)(img[0x70 + 2 * i] | img[0x71 + 2 * i] << 8);
    }
    o->drops = ov16(o, 0xA006);
    o->loaded = 1;
    return 0;
}

void ai_unload(AiOverlay *o) { free(o->img); memset(o, 0, sizeof *o); }

/* Frame index = type & 0x1F (0-7 live classes, 8-F the same class dying,
 * 0x10-0x1F items); the list is picked by hit bit 7 (facing); the drawn entry
 * is phase & 0xF; 5 bytes {palette, TL, TR, BL, BR}. */
const uint8_t *ai_frame(const AiOverlay *o, uint8_t type, uint8_t hit, uint8_t phase)
{
    if (!o || !o->loaded) return NULL;
    uint16_t ptr = (hit & 0x80) ? o->frame_r[type & 0x1F] : o->frame_l[type & 0x1F];
    if (!ptr) return NULL;
    unsigned off = (unsigned)(ptr - AI_BASE) + 5u * (phase & 0xF);
    if (off + 5 > o->len) return NULL;
    return o->img + off;
}

/* 97E2: [[A006] + class*2] -> 4 drop ids */
const uint8_t *ai_drop_list(const AiOverlay *o, int cls)
{
    if (!o || !o->loaded || !o->drops) return NULL;
    uint16_t ptr = ov16(o, (unsigned)(o->drops + 2 * (cls & 7)));
    if (!ptr) return NULL;
    unsigned off = ptr - AI_BASE;
    if (off + 4 > o->len) return NULL;
    return o->img + off;
}

/* -------------------------------------------------------------- the table */
void enemies_load(Game *g)
{
    memset(g->obj, 0, sizeof g->obj);
    memset(g->under_sprite, 0, sizeof g->under_sprite);
    int n = g->map->nobj;
    if (n > MAX_OBJS) n = MAX_OBJS;
    for (int i = 0; i < n; i++) {
        g->obj[i] = g->map->objs[i];
        g->obj[i].rcol = 0xFF;                      /* recomputed by 8D38 */
    }
    g->nobj = n;
}

static inline int obj_ring(const Game *g, const MapObj *o) { return game_ring_index(g, o->row, o->rcol); }

/* 0x914C */
void enemy_remove(Game *g, MapObj *o)
{
    (void)g;
    o->col = 0xFF00;
    if ((o->flags & 0x20) && o->home_col != 0xFFFF) o->home_col = 0xFFFF;   /* the story flag is not modelled */
}

/* 0x90E6  Dying: the phase advances every other frame; at 3 the object becomes
 * its drop (type 0x70|id, flags 0x80, timer 4) or vanishes. */
void enemy_dying(Game *g, MapObj *o)
{
    unsigned t = (unsigned)o->phase + 0x80;
    o->phase = (uint8_t)t;
    if (!(t & 0x100)) return;
    if (++o->phase != 3) return;
    o->timer = 0;
    if (o->flags & 0x40) {                                              /* 90FB */
        o->flags &= (uint8_t)~0x40;
        if (o->link < g->nobj) g->obj[o->link].row = 0;
    }
    if ((o->flags & 0x10) && !(o->type & 1)) { enemy_remove(g, o); return; }  /* 9116 */
    o->phase = 0; o->type = 0x72;                                       /* 9122 */
    uint8_t id = o->flags & 0xF;
    if (id == 0) return;
    if (id == 1) { enemy_remove(g, o); return; }
    o->type = (uint8_t)(0x70 | id); o->flags |= 0x80; o->timer = 4;     /* 9136 */
    o->hit &= 0x80; o->flags &= 0xF0;
}

/* 0x9190  Pickup overlap: the hero's top-left row within o->row-2..o->row+1
 * and his ring column within o->rcol-2..o->rcol+1 (a 2x2 sprite overlapping
 * the 3x3 hero). */
static int hero_overlaps_item(const Game *g, const MapObj *o)
{
    uint8_t hr = (uint8_t)((g->hero_scr_row + g->scroll_row) & 0x3F);
    int hc = g->hero_scr_col + 4;
    if ((uint8_t)((hr - o->row + 2) & 0x3F) > 3) return 0;
    int dc = hc - (int)o->rcol + 2;
    return dc >= 0 && dc <= 3;
}

/* Item states, table 8E14: index = (type & 0x1F) - 0x10.  The port implements
 * the pickups that cavern 1 can produce plus the simple ones; the shop/story
 * items (0x1A-0x1E) only log. */
void item_update(Game *g, MapObj *o)
{
    int st = (o->type & 0x1F) - 0x10;
    /* animation: every other frame (the original uses each state's own rate) */
    if (g->frame_no & 1) o->phase = (uint8_t)((o->phase + 1) & 3);

    if (st == 0) {                                                      /* 8E32 corpse fade */
        if (++o->timer >= 8) enemy_remove(g, o);
        return;
    }
    if (st == 2) {                                                      /* 8EF6 3-frame flash */
        if (++o->timer >= 3) enemy_remove(g, o);
        return;
    }
    if (!hero_overlaps_item(g, o)) { o->flags &= (uint8_t)~0x80; return; }
    o->flags |= 0x80;       /* 9190's "already overlapping" latch; the original's
                             * 8-frame repeat rule only matters for repeatable
                             * pickups, which cavern 1 has none of */
    switch (st) {
    case 3: {                                                           /* 8EF6 treasure box */
        static const unsigned box[5] = {50, 100, 0, 500, 1000};
        unsigned n = box[(o->phase >> 4) & 3];
        static const int msg[5] = {MSG_GOLD50, MSG_GOLD100, MSG_BOX_EMPTY, MSG_GOLD500, MSG_GOLD1000};
        if (n) gold_add(g, n);
        game_message(g, fight_message(msg[(o->phase >> 4) & 3]));       /* 9A1E table */
        g->sfx_request = 0x11;
        enemy_remove(g, o);
        return; }
    case 4: case 5: case 6: {                                           /* 8FAB coins */
        unsigned n = st == 4 ? 1 : (st == 5 ? 10 : 100);
        gold_add(g, n); g->sfx_request = 0x10;
        snprintf(g->message, sizeof g->message, "%u G", n);
        enemy_remove(g, o);
        return; }
    case 7: g->keys++;      g->sfx_request = 0x14; game_message(g, fight_message(MSG_KEY)); enemy_remove(g, o); return;
    case 8: g->lion_keys++; g->sfx_request = 0x14; game_message(g, fight_message(MSG_LION_KEY)); enemy_remove(g, o); return;
    case 9: g->hp_regen_pending += 10;                          /* 9008: +80 HP */
        game_message(g, fight_message(MSG_RECOVERED)); g->sfx_request = 0x13; enemy_remove(g, o); return;
    case 0xA: g->hp_regen_pending = (uint16_t)(g->hp_regen_pending + g->max_hp / 8 + 1);   /* 901C */
        game_message(g, fight_message(MSG_RECOVERED_FULL)); g->sfx_request = 0x13; enemy_remove(g, o); return;
    case 0xB: case 0xC: {                                       /* 909D/9090: shoes by cavern (table 90CA) */
        static const uint8_t by_cavern[10] = {0, 0, 0, 0, 4, 2, 3, 0, 0, 0};
        static const int msg[6] = {0, 0, MSG_PIRIKA, MSG_SILKARN, MSG_RUZERIA, 0};
        int sh = st == 0xC ? 1 : by_cavern[g->map->cavern < 10 ? g->map->cavern : 0];
        if (sh) { g->shoes = (uint8_t)sh; game_message(g, fight_message(sh == 1 ? MSG_FERUZA : msg[sh])); }
        g->sfx_request = 0x13; enemy_remove(g, o); return; }
    case 0xE: g->hero_crest = 0xFF; game_message(g, fight_message(MSG_HERO_CREST)); enemy_remove(g, o); return;
    default:
        fprintf(stderr, "[item] state %X at (%u,%u) picked up (not implemented)\n", st, o->col, o->row);
        enemy_remove(g, o);
        return;
    }
}

/* 0x94FF  Respawn attempt: inactive, a home column inside the ring but not at
 * ring col 0/35, off screen (row more than 24 rows below the window top, or
 * ring col outside 3..0x1F), and no sprite in the 3x3 around it. */
void enemy_spawn(Game *g, MapObj *o)
{
    int idx = (int)(o - g->obj);
    if ((o->col >> 8) != 0xFF) return;
    if ((o->flags & 0x10) && idx + 1 < g->nobj && (o[1].col >> 8) != 0xFF) return;
    if (o->home_col == 0xFFFF) return;
    uint8_t r;
    if (ai_map_col_to_ring(g, o->home_col, &r)) return;
    if (r == 0 || r == 0x23) return;
    uint8_t dy = (uint8_t)((o->home_row - (g->scroll_row - 2)) & 0x3F);
    if (dy < 0x18 && r >= 3 && r < 0x20) return;                        /* 953B: would pop up on screen */
    int p = game_ring_index(g, o->home_row, r);
    int rows = (o->flags & 0x10) ? 5 : 1;                               /* a tall enemy is 4 rows */
    for (int dr = -1; dr < rows + 1; dr++)
        for (int dc = -1; dc <= 1; dc++)
            if (g->ring[game_ring_add(p, dr * RING_W + dc)] & 0x80) return;
    o->rcol = r;
    o->col = o->home_col; o->row = o->home_row; o->type = o->home_type;
    o->phase = 0x10; o->hit = 0; o->next = 0; o->link = 0; o->hp = 0;
    g->under_sprite[idx] = g->ring[p];
    g->ring[p] = (uint8_t)(0x80 | idx);
    if ((o->flags & 0x10) && idx + 1 < g->nobj) {                       /* the lower half of a 2x4 sprite */
        MapObj *lo = &o[1];
        lo->col = o->col; lo->rcol = r;
        lo->row = (uint8_t)((o->row + 2) & 0x3F);
        lo->type = (uint8_t)(o->home_type + 1);
        lo->phase = 0x10; lo->hit = 0; lo->next = 0; lo->link = 0; lo->hp = 0;
        int q = game_ring_add(p, 2 * RING_W);
        g->under_sprite[idx + 1] = g->ring[q];
        g->ring[q] = (uint8_t)(0x80 | (idx + 1));
    }
}

/* 8DF7 jmp [cs:A000].  Only EAI1 (cavern 1) is ported so far; in the other
 * caverns the enemies stand still but can still be hit and killed. */
static void ai_entry(Game *g, MapObj *o)
{
    /* fight.bin 9CBC request table: 0 EAI1, 1 CRAB, 2 EAI2, 3 TAKO, 4 EAI3,
     * 5 TORI, 6 EAI4, 7 ZELA, 8 EAI5, 9 MEDA, 10 EAI6, 11 LEGA, 12 EAI7,
     * 13 DRGN, 14 EAI8, 15 AKMA, 16 MAO1, 17 MAO2, 18 ZEL2. */
    switch (g->ai ? g->ai->index : -1) {
    case 0:  eai1_entry(g, o); return;
    case 2:  eai2_entry(g, o); return;
    case 4:  eai3_entry(g, o); return;
    case 6:  eai4_entry(g, o); return;
    case 8:  eai5_entry(g, o); return;
    case 10: eai6_entry(g, o); return;
    case 12: eai7_entry(g, o); return;
    case 14: eai8_entry(g, o); return;
    default: break;
    }
    static int warned = -1;
    if (warned != (g->ai ? g->ai->index : -1)) {
        warned = g->ai ? g->ai->index : -1;
        fprintf(stderr, "[ai] overlay %d is a boss overlay and is not ported: enemies are inert\n", warned);
    }
    if (!o->hp) o->hp = 1;
    if (o->hit & 0x20) enemy_take_damage(g, o);
}

/* 0x8DAE  One object: restore the cell under its marker, expose the pending
 * hit, then run the AI (live enemies) or the built-in state machine. */
static void enemy_step(Game *g, MapObj *o)
{
    int idx = (int)(o - g->obj);
    int p = obj_ring(g, o);
    uint8_t h = (uint8_t)(o->hit & ~0x20);
    if (h & 0x40) {                                                     /* 8DB9 */
        if (!(o->type & 0x20)) h |= 0x20;
        h &= (uint8_t)~0x40;
    }
    o->hit = h;
    g->ring[p] = g->under_sprite[idx];                                  /* 8DCA */
    if (!(o->type & 0x11) && (o->flags & 0x10) && idx + 1 < g->nobj)
        g->ring[game_ring_add(p, 2 * RING_W)] = g->under_sprite[idx + 1];
    if (!(o->type & 0x18)) { ai_entry(g, o); return; }                  /* 8DF7 */
    if ((o->type & 0x1F) < 0x10) { enemy_dying(g, o); return; }         /* 8E0B */
    item_update(g, o);                                                  /* 8E10 */
}

/* 0x8D19  The per-frame enemy pass. */
void enemies_update(Game *g)
{
    if (g->boss_map || g->boss_room) { boss_update(g); return; }        /* 8D1D: the boss AI does it all */
    for (int i = 0; i < g->nobj; i++) {
        MapObj *o = &g->obj[i];
        g->obj_index = (uint8_t)i;
        o->rcol = 0xFF;
        if ((o->col >> 8) != 0xFF) {                                    /* 0xFFxx = disabled */
            uint8_t r;
            if (!ai_map_col_to_ring(g, o->col, &r)) {
                o->rcol = r;
                enemy_step(g, o);
                if ((o->col >> 8) != 0xFF) {                            /* still alive: place the marker */
                    int p = obj_ring(g, o);
                    g->under_sprite[i] = g->ring[p];
                    g->ring[p] = (uint8_t)(0x80 | i);
                    if (!(o->type & 0x11) && (o->flags & 0x10) && i + 1 < g->nobj) {
                        int q = game_ring_add(p, 2 * RING_W);
                        g->under_sprite[i + 1] = g->ring[q];
                        g->ring[q] = (uint8_t)(0x80 | (i + 1));
                    }
                }
            }
        }
        if (!(o->flags & 0x20)) { if (++o->timer == 0) enemy_spawn(g, o); }   /* 8D90 */
    }
}
