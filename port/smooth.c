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

const char *smooth_mode_name(int mode)
{
    switch (mode) {
    case SMOOTH_CAMERA: return "CAMERA";
    case SMOOTH_FULL:   return "FULL";
    case SMOOTH_SOFT:   return "SOFT";
    default:            return "OFF";
    }
}

int smooth_mode_next(int mode) { return mode >= SMOOTH_OFF && mode < SMOOTH_MODES - 1 ? mode + 1 : SMOOTH_OFF; }

double smooth_soft_curve(double t)
{
    if (t <= 0) return 0;
    if (t >= SMOOTH_SOFT_SPAN) return 1;
    double u = 1.0 - t / SMOOTH_SOFT_SPAN;
    return 1.0 - u * u;
}

/* what is left of a glide of g0 px over k steps after j of them (rounded as smooth_remaining rounds) */
static int glide_left(int g0, int k, int j)
{
    if (k < 1 || j >= k) return 0;
    return smooth_remaining(g0, (double)j / k);
}

void smooth_glide_step(SmoothGlide *gl, int hist, int dx, int dy, int *sx, int *sy, int *ex, int *ey)
{
    *sx = *sy = *ex = *ey = 0;
    if (!hist) { memset(gl, 0, sizeof *gl); gl->since = 0xFF; return; }
    if (gl->since < 0xFF) gl->since++;
    if (dx || dy) {
        /* the pace: the steps since the previous move, when it moved recently enough to have one */
        int k = gl->since <= SMOOTH_CADENCE_MAX ? gl->since : 1;
        /* carry what the glide under way had left at the end of the last step: the object is shown where it was */
        int cx = gl->live ? glide_left(gl->g0x, gl->k, gl->j + 1) : 0, cy = gl->live ? glide_left(gl->g0y, gl->k, gl->j + 1) : 0;
        int nx = cx + dx, ny = cy + dy;
        gl->since = 0;
        if (!smooth_is_slide(nx, ny) || !smooth_is_slide(dx, dy)) { gl->live = 0; gl->g0x = gl->g0y = 0; gl->k = gl->j = 0; return; }
        gl->g0x = (int16_t)nx; gl->g0y = (int16_t)ny; gl->k = (uint8_t)(k < 1 ? 1 : k); gl->j = 0; gl->live = 1;
    } else if (gl->live) {
        if (++gl->j >= gl->k) { gl->live = 0; gl->g0x = gl->g0y = 0; gl->k = gl->j = 0; return; }
    } else return;
    *sx = glide_left(gl->g0x, gl->k, gl->j);     *sy = glide_left(gl->g0y, gl->k, gl->j);
    *ex = glide_left(gl->g0x, gl->k, gl->j + 1); *ey = glide_left(gl->g0y, gl->k, gl->j + 1);
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

int smooth_fixture_rcol(int col, int scroll_col, int map_width, int ring_w)
{
    int d = col - scroll_col;
    if (d < 0) d += map_width;
    return (d < 0 || d > ring_w - 3) ? -1 : d;                         /* 82F8: 0x21 */
}

/* a screen cell (x, y) as 302C/3336/8366 would place it; ok = within the ring's reach (ring cols 0 .. ring_w - 1 at any E, #126) */
static SmoothPos pos_of(const Game *g, int rcol, int row, int type)
{
    SmoothPos p = {0, (int8_t)type, 0, 0};
    int sc = rcol - 4, sr = (row - g->scroll_row) & 0x3F;
    if (sr > 40) sr -= 64;
    p.x = (int16_t)sc; p.y = (int16_t)sr;
    p.ok = sc >= -4 && sc <= view_cols(g) + 3 && sr >= -SMOOTH_ROW_REACH && sr <= SCREEN_ROWS + SMOOTH_ROW_REACH;   /* the zoom-out views show rows well past the window (#126) */
    return p;
}

/* offset (in px) from where the previous step had the thing to where it is now;
 * `fx,fy` is the world's own slide, which an object with no history rides */
static int same_obj(int mode, const SmoothPos *prev, const SmoothPos *cur);
static void obj_ofs(int mode, const SmoothPos *prev, const SmoothPos *cur, int fx, int fy, int *ox, int *oy)
{
    *ox = fx; *oy = fy;
    if (!cur->ok) return;
    if (!same_obj(mode, prev, cur)) return;
    int dx = (prev->x - cur->x) * 8, dy = (prev->y - cur->y) * 8;
    if (!smooth_is_slide(dx, dy)) { *ox = *oy = 0; return; }
    *ox = dx; *oy = dy;
}

/* FULL and SOFT: is the slot the same object?  The AIs flip the 0x60 state bits (and 0x80, solid) of a live
 * enemy's type; the low five bits are what it is.  CAMERA keeps #127's exact match. */
static int same_obj(int mode, const SmoothPos *prev, const SmoothPos *cur)
{
    if (!prev->ok || !cur->ok) return 0;
    if (mode == SMOOTH_FULL || mode == SMOOTH_SOFT) return (prev->type & 0x1F) == (cur->type & 0x1F);
    return prev->type == cur->type;
}

/* FULL: an object's start / end offsets: the world's slide (which ends with the step) plus its own glide.
 * `wjump`: the camera jumped this step, so nothing has a trustworthy own move */
static void obj_full(SmoothGlide *gl, const SmoothPos *prev, const SmoothPos *cur, int fx, int fy, int wjump,
                     int *sx, int *sy, int *ex, int *ey)
{
    *ex = *ey = 0;
    if (!cur->ok) { smooth_glide_step(gl, 0, 0, 0, sx, sy, ex, ey); *sx = *sy = 0; return; }
    if (wjump || !same_obj(SMOOTH_FULL, prev, cur)) {      /* a spawn or a new identity: rides the world, no glide */
        smooth_glide_step(gl, 0, 0, 0, sx, sy, ex, ey);
        *sx = wjump ? 0 : fx; *sy = wjump ? 0 : fy;
        return;
    }
    int dx = (prev->x - cur->x) * 8, dy = (prev->y - cur->y) * 8;
    if (!smooth_is_slide(dx, dy)) { smooth_glide_step(gl, 0, 0, 0, sx, sy, ex, ey); *sx = *sy = 0; return; }
    int gx, gy;
    smooth_glide_step(gl, 1, dx - fx, dy - fy, &gx, &gy, ex, ey);
    if (!gl->live && (dx - fx || dy - fy)) { *sx = *sy = 0; return; }    /* the glide refused it: a jump */
    *sx = fx + gx; *sy = fy + gy;
}

void smooth_step(Smooth *s, const Game *g) { smooth_step_mode(s, g, SMOOTH_CAMERA); }

void smooth_step_mode(Smooth *s, const Game *g, int mode)
{
    Smooth cur;
    memset(&cur, 0, sizeof cur);
    if (mode != SMOOTH_FULL && mode != SMOOTH_SOFT) mode = SMOOTH_CAMERA;
    cur.mode = mode;
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
        int rc = smooth_fixture_rcol(f->col, g->scroll_col, g->map->width, ring_w(g));
        if (rc >= 0) cur.fix[i] = pos_of(g, rc, f->row, f->kind);
    }

    SmoothOfs *o = &cur.start, *e = &cur.end;
    memset(o, 0, sizeof *o);
    memset(e, 0, sizeof *e);
    if (s->have && s->map == cur.map) {
        int wc = smooth_wrap_delta(cur.scroll_col, s->scroll_col, g->map ? g->map->width : 0);
        int wr = smooth_wrap_delta(cur.scroll_row, s->scroll_row, 64);
        /* the camera moves by one cell a step walking, but by up to three when it re-centres (a turn, a fall, a landing, a
         * lift): that is still a slide.  Anything further is a teleport (a door, a map seam jump) and is left alone (#131). */
        if (smooth_is_slide(wc * 8, 0)) o->wx = wc * 8;
        if (smooth_is_slide(0, wr * 8)) o->wy = wr * 8;
        int wjump = !smooth_is_slide(wc * 8, wr * 8);
        int hx = (s->hero_c - cur.hero_c) * 8, hy = (s->hero_r - cur.hero_r) * 8;
        if (smooth_is_slide(hx, hy)) { o->hx = hx; o->hy = hy; }
        if (mode == SMOOTH_FULL) {
            /* carry each glide on (a history kept in another mode has none: every pace starts unknown) */
            int keep = s->mode == SMOOTH_FULL;
            for (int i = 0; i < MAX_OBJS; i++) { cur.gobj[i] = s->gobj[i]; if (!keep) memset(&cur.gobj[i], 0, sizeof cur.gobj[i]), cur.gobj[i].since = 0xFF; }
            for (int i = 0; i <= MAX_SHOTS; i++) { cur.gshot[i] = s->gshot[i]; if (!keep) memset(&cur.gshot[i], 0, sizeof cur.gshot[i]), cur.gshot[i].since = 0xFF; }
            for (int i = 0; i < 4; i++) { cur.gmag[i] = s->gmag[i]; if (!keep) memset(&cur.gmag[i], 0, sizeof cur.gmag[i]), cur.gmag[i].since = 0xFF; }
            for (int i = 0; i < MAX_OBJS; i++)
                obj_full(&cur.gobj[i] , &s->obj[i], &cur.obj[i], o->wx, o->wy, wjump, &o->ox[i], &o->oy[i], &e->ox[i], &e->oy[i]);
            for (int i = 0; i <= MAX_SHOTS; i++)
                obj_full(&cur.gshot[i], &s->shot[i], &cur.shot[i], o->wx, o->wy, wjump, &o->sx[i], &o->sy[i], &e->sx[i], &e->sy[i]);
            for (int i = 0; i < 4; i++)
                obj_full(&cur.gmag[i], &s->mag[i], &cur.mag[i], o->wx, o->wy, wjump, &o->mx[i], &o->my[i], &e->mx[i], &e->my[i]);
        } else {
            for (int i = 0; i < MAX_OBJS; i++) obj_ofs(mode, &s->obj[i], &cur.obj[i], o->wx, o->wy, &o->ox[i], &o->oy[i]);
            for (int i = 0; i <= MAX_SHOTS; i++) obj_ofs(mode, &s->shot[i], &cur.shot[i], o->wx, o->wy, &o->sx[i], &o->sy[i]);
            for (int i = 0; i < 4; i++) obj_ofs(mode, &s->mag[i], &cur.mag[i], o->wx, o->wy, &o->mx[i], &o->my[i]);
        }
        for (int i = 0; i < SMOOTH_FIXES; i++) obj_ofs(mode, &s->fix[i], &cur.fix[i], o->wx, o->wy, &o->kx[i], &o->ky[i]);
        o->active = !smooth_same(o, e) || o->wx || o->wy || o->hx || o->hy;
        for (int i = 0; i < MAX_OBJS && !o->active; i++) o->active = o->ox[i] || o->oy[i];
        for (int i = 0; i <= MAX_SHOTS && !o->active; i++) o->active = o->sx[i] || o->sy[i];
        for (int i = 0; i < 4 && !o->active; i++) o->active = o->mx[i] || o->my[i];
        for (int i = 0; i < SMOOTH_FIXES && !o->active; i++) o->active = o->kx[i] || o->ky[i];
        e->active = !smooth_same(e, &(SmoothOfs){0});
    }
    else if (mode == SMOOTH_FULL) {                 /* no history: every glide starts again */
        for (int i = 0; i < MAX_OBJS; i++) cur.gobj[i].since = 0xFF;
        for (int i = 0; i <= MAX_SHOTS; i++) cur.gshot[i].since = 0xFF;
        for (int i = 0; i < 4; i++) cur.gmag[i].since = 0xFF;
    }
    *s = cur;
}

void smooth_at(const Smooth *s, double t, SmoothOfs *out) { smooth_eval(s->mode, &s->start, &s->end, t, out); }

/* FULL: each offset runs from start to end (end + the part of start - end still to go) */
static int full_at(int s, int e, double t) { return e + smooth_remaining(s - e, t); }

void smooth_eval(int mode, const SmoothOfs *a, const SmoothOfs *e, double t, SmoothOfs *out)
{
    if (mode == SMOOTH_SOFT) { smooth_ofs_at(a, smooth_soft_curve(t), out); return; }
    if (mode != SMOOTH_FULL) { smooth_ofs_at(a, t, out); return; }
    out->active = 0;
    out->wx = full_at(a->wx, e->wx, t); out->wy = full_at(a->wy, e->wy, t);
    out->hx = full_at(a->hx, e->hx, t); out->hy = full_at(a->hy, e->hy, t);
    for (int i = 0; i < MAX_OBJS; i++) { out->ox[i] = full_at(a->ox[i], e->ox[i], t); out->oy[i] = full_at(a->oy[i], e->oy[i], t); }
    for (int i = 0; i <= MAX_SHOTS; i++) { out->sx[i] = full_at(a->sx[i], e->sx[i], t); out->sy[i] = full_at(a->sy[i], e->sy[i], t); }
    for (int i = 0; i < 4; i++) { out->mx[i] = full_at(a->mx[i], e->mx[i], t); out->my[i] = full_at(a->my[i], e->my[i], t); }
    for (int i = 0; i < SMOOTH_FIXES; i++) { out->kx[i] = full_at(a->kx[i], e->kx[i], t); out->ky[i] = full_at(a->ky[i], e->ky[i], t); }
    for (int i = 0; i < 3; i++) out->fx[i] = full_at(a->fx[i], e->fx[i], t);
    for (int i = 0; i < SMOOTH_NPCS; i++) out->nx[i] = full_at(a->nx[i], e->nx[i], t);
    if (a->active || e->active) {               /* anything still off its place? (all 0 is the plain picture) */
        SmoothOfs z; memset(&z, 0, sizeof z);
        out->active = !smooth_same(out, &z);
    }
}

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

void smooth_town_step(SmoothTown *s, const Town *t) { smooth_town_step_mode(s, t, SMOOTH_CAMERA); }

void smooth_town_step_mode(SmoothTown *s, const Town *t, int mode)
{
    SmoothTown cur;
    memset(&cur, 0, sizeof cur);
    if (mode != SMOOTH_FULL && mode != SMOOTH_SOFT) mode = SMOOTH_CAMERA;
    cur.mode = mode;
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
        int wjump = !(dc >= -1 && dc <= 1);
        for (int i = 0; i < SMOOTH_NPCS; i++) {
            cur.gnpc[i] = s->gnpc[i];
            if (s->mode != SMOOTH_FULL) { memset(&cur.gnpc[i], 0, sizeof cur.gnpc[i]); cur.gnpc[i].since = 0xFF; }
            int same = cur.npc_ok[i] && s->npc_ok[i] && s->npc_type[i] == cur.npc_type[i];
            int dx = same ? (s->npc_x[i] - cur.npc_x[i]) * 8 : 0;
            if (mode == SMOOTH_FULL) {              /* the NPC's own walk glides at its own pace, as an enemy's */
                int sx, sy, ex, ey;
                int hist = same && !wjump && smooth_is_slide(dx, 0);
                smooth_glide_step(&cur.gnpc[i], hist, hist ? dx - o->wx : 0, 0, &sx, &sy, &ex, &ey);
                if (!cur.npc_ok[i]) o->nx[i] = 0;
                else if (!same) o->nx[i] = wjump ? 0 : o->wx;
                else if (!hist) o->nx[i] = 0;
                else { o->nx[i] = o->wx + sx; cur.end.nx[i] = ex; }
                continue;
            }
            o->nx[i] = o->wx;                       /* no history: rides the map */
            if (!cur.npc_ok[i]) { o->nx[i] = 0; continue; }
            if (!same) continue;
            o->nx[i] = smooth_is_slide(dx, 0) ? dx : 0;      /* a jump is left alone */
        }
        o->active = o->wx || o->hx || o->fx[1];
        for (int i = 0; i < SMOOTH_NPCS && !o->active; i++) o->active = o->nx[i] != 0 || cur.end.nx[i] != 0;
        cur.end.active = !smooth_same(&cur.end, &(SmoothOfs){0});
    }
    else for (int i = 0; i < SMOOTH_NPCS; i++) cur.gnpc[i].since = 0xFF;
    *s = cur;
}
