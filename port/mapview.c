/* mapview.c — see mapview.h. */
#include "mapview.h"

int mapview_wrap(int v, int n)
{
    if (n <= 0) return 0;
    v %= n;
    return v < 0 ? v + n : v;
}

void mapview_layout(MapViewLayout *L, int ax, int ay, int aw, int ah, int mw, int mh)
{
    L->mw = mw; L->mh = mh;
    if (mw < 1) mw = 1;
    if (mh < 1) mh = 1;
    if (aw < 1) aw = 1;
    if (ah < 1) ah = 1;
    int cs = aw / mw;
    if (ah / mh < cs) cs = ah / mh;
    if (cs > MAPVIEW_CS_MAX) cs = MAPVIEW_CS_MAX;
    if (cs < MAPVIEW_CS_MIN) cs = MAPVIEW_CS_MIN;
    L->cs = cs;
    L->vw = aw / cs; if (L->vw > mw) L->vw = mw; if (L->vw < 1) L->vw = 1;
    L->vh = ah / cs; if (L->vh > mh) L->vh = mh; if (L->vh < 1) L->vh = 1;
    L->pan_x = L->vw < mw;
    L->pan_y = L->vh < mh;
    L->x0 = ax + (aw - L->vw * cs) / 2;
    L->y0 = ay + (ah - L->vh * cs) / 2;
}

int mapview_segments(int origin, int vis, int n, MapViewSeg seg[2])
{
    if (n <= 0 || vis <= 0) { seg[0].src = seg[0].len = seg[0].dst = 0; return 1; }
    if (vis > n) vis = n;
    int o = mapview_wrap(origin, n);
    if (o + vis <= n) {
        seg[0].src = o; seg[0].len = vis; seg[0].dst = 0;
        return 1;
    }
    seg[0].src = o; seg[0].len = n - o; seg[0].dst = 0;
    seg[1].src = 0; seg[1].len = vis - (n - o); seg[1].dst = n - o;
    return 2;
}

void mapview_pan(const MapViewLayout *L, int *ox, int *oy, int dx, int dy)
{
    *ox = L->pan_x ? mapview_wrap(*ox + dx, L->mw) : 0;
    *oy = L->pan_y ? mapview_wrap(*oy + dy, L->mh) : 0;
}

void mapview_center(const MapViewLayout *L, int col, int row, int *ox, int *oy)
{
    *ox = L->pan_x ? mapview_wrap(col - L->vw / 2, L->mw) : 0;
    *oy = L->pan_y ? mapview_wrap(row - L->vh / 2, L->mh) : 0;
}

int mapview_cell_to_px(const MapViewLayout *L, int ox, int oy, int col, int row, int *px, int *py)
{
    int dx = mapview_wrap(col - ox, L->mw), dy = mapview_wrap(row - oy, L->mh);
    if (dx >= L->vw || dy >= L->vh) return 0;
    *px = L->x0 + dx * L->cs;
    *py = L->y0 + dy * L->cs;
    return 1;
}

int mapview_px_to_cell(const MapViewLayout *L, int ox, int oy, int px, int py, int *col, int *row)
{
    int cx = px - L->x0, cy = py - L->y0;
    if (cx < 0 || cy < 0 || cx >= L->vw * L->cs || cy >= L->vh * L->cs) return 0;
    *col = mapview_wrap(ox + cx / L->cs, L->mw);
    *row = mapview_wrap(oy + cy / L->cs, L->mh);
    return 1;
}

static void put_cell(uint32_t *out, int stride, int col, int row, const Cell8 *c)
{
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            uint8_t v = c->px[y][x] & 63;
            uint32_t rgb = v ? (uint32_t)PAL_RGB[v][0] << 16 | (uint32_t)PAL_RGB[v][1] << 8 | PAL_RGB[v][2] : 0u;
            out[(size_t)(row * 8 + y) * stride + col * 8 + x] = v ? 0xFF000000u | rgb : MAPVIEW_BG;
        }
}

int mapview_raster(uint32_t *out, const Map *m, const Tileset *t)
{
    if (!out || !m || !t || m->width < 1 || m->width > MAP_MAX_WIDTH) return -1;
    gfx_pal_init();
    int stride = m->width * 8;
    for (size_t i = 0; i < (size_t)stride * MAP_ROWS * 8; i++) out[i] = MAPVIEW_BG;
    for (int c = 0; c < m->width; c++)
        for (int r = 0; r < MAP_ROWS; r++) {
            uint8_t v = m->grid[c][r];
            if (v && v < BANK_CELLS && t->present[v]) put_cell(out, stride, c, r, &t->cell[v]);
        }
    for (int i = 0; i < m->nfix; i++) {         /* three DCHR cells side by side */
        const Fixture *f = &m->fix[i];
        int col = f->col & 0x3FFF, row = f->row & 0x3F;
        for (int k = 0; k < 3; k++) {
            int cell = f->cell + k, cc = (col + k) % m->width;
            if (cell < BANK_CELLS && t->present[cell]) put_cell(out, stride, cc, row, &t->cell[cell]);
        }
    }
    return 0;
}

/* ======================================================================== */
/* Annotations (#128): see mapview.h.  Pure over the game's own records.      */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const TOWN_NAME[10] = {
    "FELISHIKA", "MURALLA", "SATONO", "BOSQUE", "HELADA", "TUMBA", "DORADO", "LLAMA", "PUREZA", "ESCO" };
static const int TOWN_WIDTH[10] = { 114, 215, 215, 152, 227, 270, 215, 280, 320, 215 };   /* docs/TOWN.md */

const char *mapview_town_name(int town) { return town >= 0 && town < 10 ? TOWN_NAME[town] : NULL; }
int mapview_town_width(int town) { return town >= 0 && town < 10 ? TOWN_WIDTH[town] : 0; }

/* the boss overlay [ai] a boss room carries (boss.h's 9CBC request indices) */
const char *mapview_boss_name(int ai)
{
    switch (ai) {
    case 1: return "CANGREJO";  case 3: return "PULPO";   case 5: return "POLLO";
    case 7: return "AGAR";      case 9: return "VISTA";   case 11: return "TARSO";
    case 13: return "DRAGON";   case 15: return "ALGUIEN";
    case 16: case 17: return "JASHIIN";
    case 18: return "PAGURO";
    default: return NULL;
    }
}

/* ---------------------------------------------------------------- the world */
static int is_item_state(uint8_t type) { return (type & 0x10) != 0; }

void mapview_info_set(MapViewInfo *in, const Map *m)
{
    memset(in, 0, sizeof *in);
    in->ok = 1;
    snprintf(in->name, sizeof in->name, "%s", m->name);
    const uint8_t *p = map_place_record(m);
    if (p) {
        char tmp[48]; int n = p[3] < 47 ? p[3] : 47;
        memcpy(tmp, p + 4, (size_t)n); tmp[n] = 0;
        const char *s = tmp;
        if (!strncmp(s, "Cavern of ", 10)) s += 10;
        int k = 0;
        for (; *s && k < (int)sizeof in->place - 1; s++) {
            char c = *s == '\\' ? '\'' : *s;                /* the font's apostrophe escape */
            if (c >= 'a' && c <= 'z') c = (char)(c - 32);
            in->place[k++] = c;
        }
        in->place[k] = 0;
    }
    in->ai = m->ai; in->cavern = m->cavern; in->width = m->width;
    in->boss_room = (m->lvl_flags & 0x80) != 0;
    for (int i = 0; i < m->ndoors && i < MAPVIEW_MAX_DOORS; i++) in->doors[in->ndoors++] = m->doors[i];
    for (int i = 0; i < m->nobj && i < 256; i++) {
        const MapObj *o = &m->objs[i];
        if (!is_item_state(o->type) || o->col >= 0xFF00 || in->nitems >= MAPVIEW_MAX_ITEMS) continue;
        MapViewObjRec *r = &in->items[in->nitems++];
        r->col = o->col; r->row = o->row; r->type = o->type; r->phase = o->phase; r->next = o->next;
        r->flags = o->flags; r->idx = (uint8_t)i;
        if ((o->flags & 0x20) && o->home_col != 0xFFFF) { r->fbyte = (uint8_t)o->home_col; r->fmask = o->home_row; }
    }
}

/* A boss room on disk has no doors and no objects; the poke list of 72F1 gives it
 * both once the boss flag is set.  Try each record's flag on its own and keep the
 * state with the most doors and objects (the boss-dead record). */
static int map_score(const Map *m)
{
    int n = 0;
    for (int i = 0; i < m->nobj; i++) if (m->objs[i].col < 0xFF00) n++;
    return m->ndoors * 4 + n;
}

static void boss_after(Map *m)
{
    if (!m->raw || m->patches < 0xC000) return;
    static Map tmp;
    static uint8_t page[256];
    size_t len = m->rawlen, o = (size_t)(m->patches - 0xC000);
    int best = map_score(m), best_fp = -1, best_mask = 0, guard = 0;
    while (o + 3 <= len && ++guard < 64) {
        unsigned fp = (unsigned)(m->raw[o] | m->raw[o + 1] << 8), mask = m->raw[o + 2];
        if (fp == 0xFFFF) break;
        o += 3;
        while (o + 4 <= len && (m->raw[o] | m->raw[o + 1] << 8) != 0xFFFF) o += 4;
        o += 2;
        if (fp >= 0x100) continue;
        map_free(&tmp);
        if (map_parse(&tmp, m->raw, len)) continue;
        memset(page, 0, sizeof page); page[fp] = (uint8_t)mask;
        map_apply_patches(&tmp, page);
        int sc = map_score(&tmp);
        if (sc > best) { best = sc; best_fp = (int)fp; best_mask = (int)mask; }
    }
    map_free(&tmp);
    if (best_fp < 0) return;
    memset(page, 0, sizeof page); page[best_fp] = (uint8_t)best_mask;
    map_apply_patches(m, page);
}

int mapview_world_load(MapViewWorld *w, const char *dir)
{
    static Map m;
    int n = 0;
    memset(w, 0, sizeof *w);
    for (int i = 0; i < MAPVIEW_NMAPS; i++) {
        memset(&m, 0, sizeof m);
        if (map_load_system(&m, dir, i)) { map_free(&m); continue; }
        int boss = (m.lvl_flags & 0x80) != 0, ai = m.ai;
        if (boss) boss_after(&m);                    /* ...which clears the flag: remember it */
        mapview_info_set(&w->m[i], &m);
        w->m[i].boss_room = boss; w->m[i].ai = (uint8_t)ai;
        map_free(&m);
        n++;
    }
    return n;
}

void mapview_world_use_doors(MapViewWorld *w, int idx, const Map *live)
{
    if (idx < 0 || idx >= MAPVIEW_NMAPS || !w->m[idx].ok) return;
    MapViewInfo *in = &w->m[idx];
    in->ndoors = 0;
    for (int i = 0; i < live->ndoors && i < MAPVIEW_MAX_DOORS; i++) in->doors[in->ndoors++] = live->doors[i];
}

int mapview_world_find(const MapViewWorld *w, const char *name)
{
    for (int i = 0; i < MAPVIEW_NMAPS; i++) if (w->m[i].ok && !strcmp(w->m[i].name, name)) return i;
    return -1;
}

/* ------------------------------------------------------------------- doors */
static int iabs(int v) { return v < 0 ? -v : v; }

/* the door in the destination map that leads back to this one's coordinates:
 * the hero is put at dest, and the door he arrives by stands there (exactly, in
 * the shipped data; a row or two out is tolerated).  Closest wins. */
static int best_partner(const MapViewWorld *w, int map, int di, int *pm)
{
    const Door *d = &w->m[map].doors[di];
    if (d->dest_row == 0xFF) return -1;
    int t = d->dest_map;
    if (t < 0 || t >= MAPVIEW_NMAPS || !w->m[t].ok) return -1;
    int best = -1, bd = 1 << 20;
    for (int e = 0; e < w->m[t].ndoors; e++) {
        const Door *q = &w->m[t].doors[e];
        if (q->dest_row == 0xFF || q->dest_map != map) continue;
        int dist = iabs(q->col - d->dest_col) + 2 * iabs(q->row - d->dest_row)
                 + iabs(q->dest_col - d->col) + 2 * iabs(q->dest_row - d->row);
        if (dist < bd && dist <= 4) { bd = dist; best = e; }
    }
    if (best >= 0) *pm = t;
    return best;
}

int mapview_door_partner(const MapViewWorld *w, int map, int door, int *pmap, int *pdoor)
{
    if (map < 0 || map >= MAPVIEW_NMAPS || !w->m[map].ok || door < 0 || door >= w->m[map].ndoors) return 0;
    int t = 0;
    int e = best_partner(w, map, door, &t);
    if (e < 0) return 0;
    int back_m = 0;
    if (best_partner(w, t, e, &back_m) != door || back_m != map) return 0;       /* mutual, or no pair */
    if (pmap) *pmap = t;
    if (pdoor) *pdoor = e;
    return 1;
}

static int pair_rank(const MapViewWorld *w, int map, int door, int other)
{
    int n = 0;
    for (int j = 0; j <= door; j++) {
        int pm, pd;
        if (mapview_door_partner(w, map, j, &pm, &pd) && pm == other) n++;
    }
    return n;
}

static int letter_rank(const MapViewWorld *w, int map, int door, int pdoor)
{
    int lo = door < pdoor ? door : pdoor, n = 0;
    for (int j = 0; j <= lo; j++) {
        int pm, pd;
        if (mapview_door_partner(w, map, j, &pm, &pd) && pm == map && pd > j) n++;
    }
    return n - 1;
}

static const char *dest_name(const MapViewWorld *w, int from, int to)
{
    const MapViewInfo *t = &w->m[to];
    if (t->place[0] && strcmp(t->place, w->m[from].place)) return t->place;
    return t->name;
}

void mapview_door_labels(const MapViewWorld *w, int map, MapViewDoorLabel out[MAPVIEW_MAX_DOORS])
{
    memset(out, 0, sizeof out[0] * MAPVIEW_MAX_DOORS);
    if (map < 0 || map >= MAPVIEW_NMAPS || !w->m[map].ok) return;
    const MapViewInfo *in = &w->m[map];
    for (int i = 0; i < in->ndoors; i++) {
        const Door *d = &in->doors[i];
        MapViewDoorLabel *L = &out[i];
        L->pmap = L->pdoor = -1;
        if (d->dest_row == 0xFF) {                           /* a cave mouth: into a town */
            int town = d->dest_map & 0x7F;
            const char *nm = mapview_town_name(town);
            int tw = mapview_town_width(town);
            L->kind = MVD_TOWN;
            if (nm && tw) snprintf(L->text, sizeof L->text, "%s %c", nm, d->dest_col * 2 < tw ? 'W' : 'E');
            else snprintf(L->text, sizeof L->text, "TOWN %d", town);
            continue;
        }
        int t = d->dest_map;
        if (t >= 0 && t < MAPVIEW_NMAPS && w->m[t].ok && (w->m[t].boss_room || w->m[t].ai >= 16) &&
            mapview_boss_name(w->m[t].ai)) {
            L->kind = MVD_BOSS;
            int qm, qd;                                      /* arriving at the boss room's own exit door (dflags bit 7, the Tear's) */
            int exit_door = mapview_door_partner(w, map, i, &qm, &qd) && (w->m[qm].doors[qd].dflags & 0x80);
            snprintf(L->text, sizeof L->text, "%s %s", exit_door ? "EXIT" : "BOSS", mapview_boss_name(w->m[t].ai));
            continue;
        }
        int pm, pd;
        if (mapview_door_partner(w, map, i, &pm, &pd)) {
            L->pmap = pm; L->pdoor = pd;
            if (pm == map) {
                L->kind = MVD_LETTER; L->num = letter_rank(w, map, i, pd);
                snprintf(L->text, sizeof L->text, "%c", 'A' + (L->num % 26));
            } else {
                L->kind = MVD_NUM;
                L->num = map < pm ? pair_rank(w, map, i, pm) : pair_rank(w, pm, pd, map);
                snprintf(L->text, sizeof L->text, "%d %s", L->num, dest_name(w, map, pm));
            }
            continue;
        }
        L->kind = MVD_LINK;
        if (t == map) snprintf(L->text, sizeof L->text, "TO %d/%d", d->dest_col, d->dest_row);    /* a jump inside the map */
        else if (t >= 0 && t < MAPVIEW_NMAPS && w->m[t].ok) snprintf(L->text, sizeof L->text, "TO %s", dest_name(w, map, t));
        else snprintf(L->text, sizeof L->text, "TO MAP %02X", d->dest_map);
    }
}

/* ----------------------------------------------------------- air currents */
int mapview_cell_class(const Tileset *t, uint8_t v)
{
    static const struct { int off, cls; } L[] = {
        { 0x24, MVC_UP }, { 0x28, MVC_LEFT }, { 0x2C, MVC_RIGHT },      /* 76F6: updraft, current left, current right */
        { 0x18, MVC_BELT_L }, { 0x1C, MVC_BELT_R },                      /* 6A67: belts (kind 2 walks left, kind 1 right) */
    };
    if (!t || !v) return MVC_NONE;
    for (unsigned k = 0; k < sizeof L / sizeof L[0]; k++)
        for (int n = 0; n < 4 && t->lists[L[k].off + n]; n++)
            if (t->lists[L[k].off + n] == v) return L[k].cls;
    return MVC_NONE;
}

int mapview_runs(const Map *m, const Tileset *t, MapViewRun *out, int max)
{
    int n = 0;
    if (!m || !t || !out || max <= 0) return 0;
    for (int r = 0; r < MAP_ROWS; r++) {                         /* sideways kinds: runs along a row */
        for (int c = 0; c < m->width; ) {
            int k = mapview_cell_class(t, m->grid[c][r]);
            if (k < MVC_LEFT || k == MVC_UP) { c++; continue; }
            int c1 = c;
            while (c1 + 1 < m->width && mapview_cell_class(t, m->grid[c1 + 1][r]) == k) c1++;
            int merged = 0;
            for (int j = 0; j < n; j++)
                if (out[j].kind == k && out[j].c0 == c && out[j].c1 == c1 && out[j].r1 == r - 1) { out[j].r1 = r; merged = 1; break; }
            if (!merged && n < max) { out[n].kind = k; out[n].c0 = c; out[n].c1 = c1; out[n].r0 = out[n].r1 = r; n++; }
            c = c1 + 1;
        }
    }
    for (int c = 0; c < m->width; c++) {                         /* updrafts: runs down a column */
        for (int r = 0; r < MAP_ROWS; ) {
            if (mapview_cell_class(t, m->grid[c][r]) != MVC_UP) { r++; continue; }
            int r1 = r;
            while (r1 + 1 < MAP_ROWS && mapview_cell_class(t, m->grid[c][r1 + 1]) == MVC_UP) r1++;
            int merged = 0;
            for (int j = 0; j < n; j++)
                if (out[j].kind == MVC_UP && out[j].r0 == r && out[j].r1 == r1 && out[j].c1 == c - 1) { out[j].c1 = c; merged = 1; break; }
            if (!merged && n < max) { out[n].kind = MVC_UP; out[n].r0 = r; out[n].r1 = r1; out[n].c0 = out[n].c1 = c; n++; }
            r = r1 + 1;
        }
    }
    return n;
}

/* --------------------------------------------------------------- fixtures */
static int row_free(const Map *m, int col, int row)          /* the three cells a lift moves into */
{
    row &= 0x3F;
    for (int k = 0; k < 3; k++) if (m->grid[(col + k) % m->width][row]) return 0;
    return 1;
}

int mapview_fixture_span(const Map *m, const Fixture *f, MapViewSpan *s)
{
    if (!m || !f || m->width < 1) return 0;
    memset(s, 0, sizeof *s);
    s->kind = f->kind;
    if (f->kind == 2) {
        if (!f->var) return 0;                                   /* a static C platform just stands */
        int w = m->width, l = f->lim_l % w, r = f->lim_r % w;
        s->col = l; s->row = f->row & 0x3F; s->w = ((r - l + w) % w) + 3; s->h = 1;
        return 1;
    }
    int col = f->col % m->width, row = f->row & 0x3F, up = 0, down = 0;
    if (f->kind == 0) while (up < 62 && row_free(m, col, row - up - 1) && up + down < 62) up++;
    while (down < 62 && row_free(m, col, row + down + 1) && up + down < 62) down++;
    s->col = col; s->w = 3; s->row = (row - up) & 0x3F; s->h = up + down + 1;
    s->vertical = 1; s->oneway = f->kind == 1;
    return s->h > 1;
}

/* ------------------------------------------------------------------ items */
static int item_kind(int st)
{
    switch (st) {
    case 0x13: return MVI_CHEST;   case 0x16: return MVI_KEY;    case 0x17: return MVI_LION;
    case 0x18: return MVI_POTION;  case 0x19: return MVI_FULL;   case 0x1A: case 0x1E: return MVI_SHOES;
    case 0x1D: return MVI_CREST;   case 0x14: case 0x15: case 0x1B: return MVI_ALMAS;
    case 0x1C: return MVI_MSG;     case 0x10: return MVI_ROCK;   case 0x11: return MVI_DROP;
    default: return -1;
    }
}

static const char *item_name(int st, int cavern)
{
    switch (st) {
    case 0x16: return "KEY";            case 0x17: return "LION KEY";
    case 0x18: return "POTION";         case 0x19: return "FULL POTION";
    case 0x1D: return "HERO CREST";     case 0x1E: return "FERUZA SHOES";
    case 0x14: return "1 ALMA";         case 0x15: return "10 ALMAS";   case 0x1B: return "100 ALMAS";
    case 0x1C: return "MESSAGE";
    case 0x1A: return cavern == 4 ? "RUZERIA SHOES" : cavern == 5 ? "PIRIKA SHOES" : cavern == 6 ? "SILKARN SHOES" : "SHOES";
    default: return "";
    }
}

static void item_label(char *out, size_t n, int st, int phase, int next, int cavern)
{
    static const char *const PRIZE[7] = { "50G", "100G", "EMPTY", "500G", "1000G", "GLORY CREST", "ENCHANT SWORD" };
    out[0] = 0;
    if (st == 0x13) {                                            /* 8F23: prize = digit - 1; digit 0 turns it into `next` */
        int dg = phase & 0xF;
        if (dg >= 1 && dg <= 7) snprintf(out, n, "%s", PRIZE[dg - 1]);
        else if (dg == 0 && (next & 0x10)) snprintf(out, n, "%s", item_name(next & 0x1F, cavern));
        else snprintf(out, n, "BOX");
    } else if (st == 0x10) {
        if (next & 0x10) snprintf(out, n, "ROCK %s", item_name(next & 0x1F, cavern));
    } else snprintf(out, n, "%s", item_name(st, cavern));
}

int mapview_items(const MapViewInfo *in, const MapObj *live, int nlive, const uint8_t *page,
                  MapViewItem *out, int max)
{
    int n = 0;
    for (int i = 0; i < in->nitems && n < max; i++) {
        const MapViewObjRec *r = &in->items[i];
        int type = r->type, phase = r->phase, next = r->next, taken = 0;
        if (live && r->idx < nlive) {
            const MapObj *l = &live[r->idx];
            if (l->col >= 0xFF00) taken = 1;
            else if (item_kind(l->type & 0x1F) >= 0 && (l->type & 0x10) && (l->type & 0x1F) != (type & 0x1F)) {
                type = l->type; phase = l->phase; next = l->next;      /* a broken rock: what it dropped */
            }
        }
        if (r->fmask && page && (page[r->fbyte] & r->fmask)) taken = 1;
        int st = type & 0x1F, k = item_kind(st);
        if (k < 0) continue;
        MapViewItem *it = &out[n++];
        it->kind = k; it->col = r->col; it->row = r->row; it->taken = taken;
        item_label(it->label, sizeof it->label, st, phase, next, in->cavern);
    }
    return n;
}

/* ----------------------------------------------------------------- legend */
int mapview_legend_fit(const int *width, int n, int avail, int gap, int maxlines, int *line)
{
    int ln = 0, used = 0, i;
    for (i = 0; i < n; i++) {
        int need = width[i] + (used ? gap : 0);
        if (used + need > avail) {
            if (used == 0 || ln + 1 >= maxlines) break;           /* too wide for a whole line, or out of lines */
            ln++; used = 0; need = width[i];
            if (need > avail) break;
        }
        used += need; line[i] = ln;
    }
    return i;
}
