/* smooth.c -- see smooth.h (#127). */
#include "smooth.h"
#include "enemy.h"
#include "town.h"
#include <math.h>
#include <string.h>

int smooth_wrap_delta(int a, int b, int n)
{
    if (n <= 0) return a - b;
    int d = (a - b) % n;
    if (d < 0) d += n;
    return d > n / 2 ? d - n : d;
}

int smooth_remaining(int px, double t)
{
    if (t <= 0) return px;
    if (t >= 1) return 0;
    double v = px * (1.0 - t);
    return (int)(v < 0 ? v - 0.5 : v + 0.5);
}

double smooth_progress(double elapsed_ms, double period_ms)
{
    if (period_ms <= 0) return 1;
    double t = elapsed_ms / period_ms;
    return t < 0 ? 0 : t > 1 ? 1 : t;
}

int smooth_is_slide(int dx, int dy)
{
    return dx >= -SMOOTH_MAX_PX && dx <= SMOOTH_MAX_PX && dy >= -SMOOTH_MAX_PX && dy <= SMOOTH_MAX_PX;
}

void smooth_vsync_init(SmoothVsync *v) { v->last = 0; v->period = 1000.0 / 60; v->cand = 0; v->n = v->cn = 0; }

void smooth_vsync_update(SmoothVsync *v, double ts)
{
    if (v->last > 0 && ts > v->last) {
        double d = ts - v->last, k = (double)(int)(d / v->period + 0.5);
        if (k >= 1 && k <= 6 && d > k * v->period * 0.8 && d < k * v->period * 1.2) {   /* a plain gap of k frames */
            double w = v->n < 16 ? 1.0 / (v->n + 2) : 0.05;
            v->period += (d / k - v->period) * w;
            if (v->period < 4) v->period = 4;
            if (v->period > 40) v->period = 40;
            v->n++;
            v->cn = 0;
        } else if (d >= 3 && d < v->period * 0.8) {         /* shorter than a frame: a faster display, once three agree */
            if (v->cn && fabs(d - v->cand) < 0.1 * d) v->cn++; else { v->cand = d; v->cn = 1; }
            if (v->cn >= 3) { v->period = v->cand; v->n = 0; v->cn = 0; }
        } else v->cn = 0;
    }
    v->last = ts;
}

int smooth_frame_due(double deadline, double ts, double eps) { return ts + eps >= deadline; }

int smooth_frames_to(double deadline, double ts0, double period, double eps)
{
    if (period <= 0) return 1;
    double x = (deadline - eps - ts0) / period;
    int k = (int)x;
    if ((double)k < x) k++;
    return k < 1 ? 1 : k;
}

double smooth_frame_progress(double ts, double ts0, int frames, double period)
{
    if (frames < 1 || period <= 0) return 1;
    return smooth_progress(ts - ts0, frames * period);
}

void smooth_reset(Smooth *s) { memset(s, 0, sizeof *s); }

int smooth_fixture_rcol(int col, int scroll_col, int map_width)
{
    int d = col - scroll_col;
    if (d < 0) d += map_width;
    return (d < 0 || d > 0x21) ? -1 : d;
}

/* a screen cell (x, y) as 302C/3336/8366 would place it; ok = within the ring's reach */
static SmoothPos pos_of(const Game *g, int rcol, int row, int type)
{
    SmoothPos p = {0, (int8_t)type, 0, 0};
    int sc = rcol - 4, sr = (row - g->scroll_row) & 0x3F;
    if (sr > 40) sr -= 64;
    p.x = (int16_t)sc; p.y = (int16_t)sr;
    p.ok = sc >= -4 && sc <= SCREEN_COLS + 3 && sr >= -4 && sr <= SCREEN_ROWS + 3;
    return p;
}

/* offset (in px) from where the previous step had the thing to where it is now;
 * `fx,fy` is the world's own slide, which an object with no history rides */
static void obj_ofs(const SmoothPos *prev, const SmoothPos *cur, int fx, int fy, int *ox, int *oy)
{
    *ox = fx; *oy = fy;
    if (!cur->ok) return;
    if (!prev->ok || prev->type != cur->type) return;
    int dx = (prev->x - cur->x) * 8, dy = (prev->y - cur->y) * 8;
    if (!smooth_is_slide(dx, dy)) { *ox = *oy = 0; return; }
    *ox = dx; *oy = dy;
}

void smooth_step(Smooth *s, const Game *g)
{
    Smooth cur;
    memset(&cur, 0, sizeof cur);
    cur.have = 1; cur.map = g->map;
    cur.scroll_col = g->scroll_col; cur.scroll_row = g->scroll_row & 0x3F;
    cur.hero_c = g->hero_scr_col; cur.hero_r = g->hero_scr_row;
    for (int i = 0; i < g->nobj && i < MAX_OBJS; i++) {
        const MapObj *o = &g->obj[i];
        if ((o->col >> 8) == 0xFF || o->rcol == 0xFF) continue;
        if (g->ring[game_ring_index(g, o->row, o->rcol)] != (uint8_t)(0x80 | i)) continue;   /* draw_enemies' rule */
        cur.obj[i] = pos_of(g, o->rcol, o->row, o->type);
    }
    for (int i = 0; i <= MAX_SHOTS; i++) {
        const Shot *sh = &g->shots[i];
        if (sh->col == 0xFF) break;
        if (!sh->col) continue;
        cur.shot[i] = pos_of(g, sh->col, sh->row, 0);
    }
    if (g->magic_active)
        for (int i = 0; i < 4; i++) {
            const Magic *m = &g->magic[i];
            uint8_t rc;
            if (!m->live || (m->col >> 8) == 0xFF || ai_map_col_to_ring(g, m->col, &rc)) continue;
            cur.mag[i] = pos_of(g, rc, m->row, 0);
        }

    for (int i = 0; i < g->nfix && i < SMOOTH_FIXES && g->map; i++) {
        const Fixture *f = &g->fix[i];
        int rc = smooth_fixture_rcol(f->col, g->scroll_col, g->map->width);
        if (rc >= 0) cur.fix[i] = pos_of(g, rc, f->row, f->kind);
    }

    SmoothOfs *o = &cur.start;
    memset(o, 0, sizeof *o);
    if (s->have && s->map == cur.map) {
        int wc = smooth_wrap_delta(cur.scroll_col, s->scroll_col, g->map ? g->map->width : 0);
        int wr = smooth_wrap_delta(cur.scroll_row, s->scroll_row, 64);
        /* the camera moves by one cell a step walking, but by up to three when it re-centres (a turn, a fall, a landing, a
         * lift): that is still a slide.  Anything further is a teleport (a door, a map seam jump) and is left alone (#131). */
        if (smooth_is_slide(wc * 8, 0)) o->wx = wc * 8;
        if (smooth_is_slide(0, wr * 8)) o->wy = wr * 8;
        int hx = (s->hero_c - cur.hero_c) * 8, hy = (s->hero_r - cur.hero_r) * 8;
        if (smooth_is_slide(hx, hy)) { o->hx = hx; o->hy = hy; }
        for (int i = 0; i < MAX_OBJS; i++) obj_ofs(&s->obj[i], &cur.obj[i], o->wx, o->wy, &o->ox[i], &o->oy[i]);
        for (int i = 0; i <= MAX_SHOTS; i++) obj_ofs(&s->shot[i], &cur.shot[i], o->wx, o->wy, &o->sx[i], &o->sy[i]);
        for (int i = 0; i < 4; i++) obj_ofs(&s->mag[i], &cur.mag[i], o->wx, o->wy, &o->mx[i], &o->my[i]);
        for (int i = 0; i < SMOOTH_FIXES; i++) obj_ofs(&s->fix[i], &cur.fix[i], o->wx, o->wy, &o->kx[i], &o->ky[i]);
        o->active = o->wx || o->wy || o->hx || o->hy;
        for (int i = 0; i < MAX_OBJS && !o->active; i++) o->active = o->ox[i] || o->oy[i];
        for (int i = 0; i <= MAX_SHOTS && !o->active; i++) o->active = o->sx[i] || o->sy[i];
        for (int i = 0; i < 4 && !o->active; i++) o->active = o->mx[i] || o->my[i];
        for (int i = 0; i < SMOOTH_FIXES && !o->active; i++) o->active = o->kx[i] || o->ky[i];
    }
    *s = cur;
}

void smooth_at(const Smooth *s, double t, SmoothOfs *out) { smooth_ofs_at(&s->start, t, out); }

void smooth_ofs_at(const SmoothOfs *a, double t, SmoothOfs *out)
{
    out->active = 0;
    out->wx = smooth_remaining(a->wx, t); out->wy = smooth_remaining(a->wy, t);
    out->hx = smooth_remaining(a->hx, t); out->hy = smooth_remaining(a->hy, t);
    for (int i = 0; i < MAX_OBJS; i++) { out->ox[i] = smooth_remaining(a->ox[i], t); out->oy[i] = smooth_remaining(a->oy[i], t); }
    for (int i = 0; i <= MAX_SHOTS; i++) { out->sx[i] = smooth_remaining(a->sx[i], t); out->sy[i] = smooth_remaining(a->sy[i], t); }
    for (int i = 0; i < 4; i++) { out->mx[i] = smooth_remaining(a->mx[i], t); out->my[i] = smooth_remaining(a->my[i], t); }
    for (int i = 0; i < SMOOTH_FIXES; i++) { out->kx[i] = smooth_remaining(a->kx[i], t); out->ky[i] = smooth_remaining(a->ky[i], t); }
    for (int i = 0; i < 3; i++) out->fx[i] = smooth_remaining(a->fx[i], t);
    for (int i = 0; i < SMOOTH_NPCS; i++) out->nx[i] = smooth_remaining(a->nx[i], t);
    out->active = a->active && (t < 1);
    if (out->active) {          /* all quantised to zero is the same picture as none */
        SmoothOfs z; memset(&z, 0, sizeof z);
        z.active = out->active;
        if (smooth_same(out, &z)) out->active = 0;
    }
}

int smooth_same(const SmoothOfs *a, const SmoothOfs *b)
{
    SmoothOfs x = *a, y = *b;
    x.active = y.active = 0;
    return memcmp(&x, &y, sizeof x) == 0;
}

/* ------------------------------------------------------------------ towns */
void smooth_town_reset(SmoothTown *s) { memset(s, 0, sizeof *s); }

void smooth_town_step(SmoothTown *s, const Town *t)
{
    SmoothTown cur;
    memset(&cur, 0, sizeof cur);
    cur.have = 1; cur.map = t->map; cur.index = t->map->index;
    cur.scroll_col = t->scroll_col; cur.back_steps = t->back_steps; cur.hero_c = t->hero_scr_col;
    for (int i = 0; i < t->map->nnpcs && i < SMOOTH_NPCS; i++) {
        int sc;
        if (!town_npc_screen_col(t, i, &sc)) continue;
        cur.npc_ok[i] = 1; cur.npc_type[i] = (int8_t)(t->map->npcs[i].sprite & 0x7F); cur.npc_x[i] = (int16_t)sc;
    }
    SmoothOfs *o = &cur.start;
    memset(o, 0, sizeof *o);
    if (s->have && s->map == cur.map && s->index == cur.index) {
        int dc = cur.scroll_col - s->scroll_col, db = cur.back_steps - s->back_steps;
        if (dc >= -1 && dc <= 1) o->wx = dc * 8;
        if (db >= -1 && db <= 1) { o->fx[0] = db * 4; o->fx[1] = db * 8; o->fx[2] = db * 16; }
        int hx = (s->hero_c - cur.hero_c) * 8;
        if (smooth_is_slide(hx, 0)) o->hx = hx;
        for (int i = 0; i < SMOOTH_NPCS; i++) {
            o->nx[i] = o->wx;                       /* no history: rides the map */
            if (!cur.npc_ok[i]) { o->nx[i] = 0; continue; }
            if (!s->npc_ok[i] || s->npc_type[i] != cur.npc_type[i]) continue;
            int dx = (s->npc_x[i] - cur.npc_x[i]) * 8;
            o->nx[i] = smooth_is_slide(dx, 0) ? dx : 0;      /* a jump is left alone */
        }
        o->active = o->wx || o->hx || o->fx[1];
        for (int i = 0; i < SMOOTH_NPCS && !o->active; i++) o->active = o->nx[i] != 0;
    }
    *s = cur;
}
