/* touch.c — on-screen touch controls (issue #54); see touch.h. */
#include <string.h>
#include "touch.h"
#include "physics.h"

#define TOUCH_DEF_SIZE 100
#define TOUCH_DEF_OPACITY 50
#define COMBO_PERIOD_MS 100              /* ~10 Hz auto-repeat, as the attack-keys patch */
#define COMBO_ON_MS 50

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void set_ctl(TouchLayout *L, int id, int shape, int x, int y, int w, int h, const char *label)
{
    TouchCtl *c = &L->c[id];
    c->shape = shape; c->x = x; c->y = y; c->w = w; c->h = h;
    strncpy(c->label, label, sizeof c->label - 1);
    c->label[sizeof c->label - 1] = 0;
}
static void set_circle(TouchLayout *L, int id, int cx, int cy, int r, const char *label)
{
    set_ctl(L, id, TSH_CIRCLE, cx - r, cy - r, 2 * r, 2 * r, label);
}

enum { M_OVERLAY, M_BELOW, M_PILLARS };

/* stick bottom-left; sword, magic and the combo above it bottom-right */
static void bottom_clusters(TouchLayout *L, int w, int h, int u)
{
    int m = u / 2;
    int R = u * 8 / 5;
    L->stick_r = R;
    L->stick_cx = m + R;
    L->stick_cy = h - m - R;
    set_circle(L, TC_STICK, L->stick_cx, L->stick_cy, R, "");
    int rs = u * 11 / 10, rm = u * 9 / 10, rc = u * 4 / 5, g = u * 3 / 10;
    int sx = w - m - rs, sy = h - m - rs;
    set_circle(L, TC_SWORD, sx, sy, rs, "SWORD");
    set_circle(L, TC_MAGIC, sx - rs - rm - g, h - m - rm, rm, "MAGIC");
    set_circle(L, TC_COMBO, sx, sy - rs - rc - g, rc, "COMBO");
}

/* the two rows 1..9 Y N and Esc/Items/Save/Load, centred in [xlo, xhi) from y0 */
static void key_rows(TouchLayout *L, int xlo, int xhi, int u, int y0)
{
    int m = u / 2, aw = xhi - xlo;
    int cell = u * 19 / 20;
    int maxcell = (aw - 2 * m) * 100 / (TC_NKEYS * 100 + (TC_NKEYS - 1) * 12);
    if (cell > maxcell) cell = maxcell;
    if (cell < 4) cell = 4;
    int gap = cell * 12 / 100;
    int rowW = TC_NKEYS * cell + (TC_NKEYS - 1) * gap;
    int x0 = xlo + (aw - rowW) / 2;
    for (int i = 0; i < TC_NKEYS; i++) {
        char lb[2] = { i < 9 ? (char)('1' + i) : i == 9 ? 'Y' : 'N', 0 };
        set_ctl(L, TC_KEY0 + i, TSH_RECT, x0 + i * (cell + gap), y0, cell, cell, lb);
    }
    int bw = u * 3 / 2, bh = u * 4 / 5, bg = u / 5;
    int mw = 4 * bw + 3 * bg;
    if (mw > aw - 2 * m) { bw = (aw - 2 * m - 3 * bg) / 4; mw = 4 * bw + 3 * bg; }
    int mx = xlo + (aw - mw) / 2, my = y0 + cell + gap + bg;
    static const char *const lab[4] = { "ESC", "ITEMS", "SAVE", "LOAD" };
    static const int ids[4] = { TC_ESC, TC_ITEMS, TC_QSAVE, TC_QLOAD };
    for (int i = 0; i < 4; i++)
        set_ctl(L, ids[i], TSH_RECT, mx + i * (bw + bg), my, bw, bh, lab[i]);
}

/* pillarbox mode: the digits as a 3-wide grid in the left column, the four menu
 * keys as a 2x2 grid in the right one, both from the top */
static void key_columns(TouchLayout *L, int w, int u)
{
    int m = u / 2, pl = L->cx, pr = w - L->cx - L->cw;
    int cell = u * 19 / 20, mc = (pl - 2 * m) * 100 / 324;     /* 3 cells + 2 gaps of 12% */
    if (cell > mc) cell = mc;
    if (cell < 4) cell = 4;
    int gap = cell * 12 / 100, x0 = (pl - (3 * cell + 2 * gap)) / 2;
    for (int i = 0; i < TC_NKEYS; i++) {
        char lb[2] = { i < 9 ? (char)('1' + i) : i == 9 ? 'Y' : 'N', 0 };
        set_ctl(L, TC_KEY0 + i, TSH_RECT, x0 + (i % 3) * (cell + gap), m + (i / 3) * (cell + gap), cell, cell, lb);
    }
    int bw = u * 3 / 2, bh = u * 4 / 5, bg = u / 5;
    if (2 * bw + bg > pr - 2 * m) bw = (pr - 2 * m - bg) / 2;
    int mx = L->cx + L->cw + (pr - (2 * bw + bg)) / 2;
    static const char *const lab[4] = { "ESC", "ITEMS", "SAVE", "LOAD" };
    static const int ids[4] = { TC_ESC, TC_ITEMS, TC_QSAVE, TC_QLOAD };
    for (int i = 0; i < 4; i++)
        set_ctl(L, ids[i], TSH_RECT, mx + (i % 2) * (bw + bg), m + (i / 2) * (bh + bg), bw, bh, lab[i]);
}

static int meets(const TouchCtl *c, int x, int y, int w, int h)
{
    return c->x < x + w && x < c->x + c->w && c->y < y + h && y < c->y + c->h;
}

/* Every control inside the window, none touching another or the picture? */
static int layout_ok(const TouchLayout *L)
{
    for (int i = 0; i < TC_COUNT; i++) {
        const TouchCtl *a = &L->c[i];
        if (a->w <= 0 || a->x < 0 || a->y < 0 || a->x + a->w > L->win_w || a->y + a->h > L->win_h) return 0;
        if (meets(a, L->cx, L->cy, L->cw, L->ch)) return 0;
        for (int j = i + 1; j < TC_COUNT; j++)
            if (meets(a, L->c[j].x, L->c[j].y, L->c[j].w, L->c[j].h)) return 0;
    }
    return 1;
}

static void build(TouchLayout *L, int w, int h, int u, int mode)
{
    L->unit = u;
    L->overlay = mode == M_OVERLAY;
    bottom_clusters(L, w, h, u);
    if (mode == M_PILLARS) key_columns(L, w, u);
    else key_rows(L, 0, w, u, mode == M_BELOW ? L->cy + L->ch + u / 4 : u / 4);
}

void touch_layout_in(TouchLayout *L, int w, int h, int size_pct, int cx, int cy, int cw, int ch)
{
    memset(L, 0, sizeof *L);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    size_pct = clampi(size_pct, TOUCH_SIZE_MIN, TOUCH_SIZE_MAX);
    L->win_w = w; L->win_h = h; L->portrait = h > w;
    L->cx = cx; L->cy = cy; L->cw = cw < 0 ? 0 : cw; L->ch = ch < 0 ? 0 : ch;
    int mn = w < h ? w : h;
    int u = mn * size_pct / 1000;                 /* 10% of the short side at 100 */
    if (u < 8) u = 8;

    int done = 0;
    if (L->cw > 0 && L->ch > 0) {
        if (L->portrait) {
            build(L, w, h, u, M_BELOW);
            done = layout_ok(L);
        } else {
            /* pillar columns: the bottom clusters need ~5.3 units each side; shrink
             * the unit to fit, but not below 70% of what was asked */
            int pw = L->cx < w - L->cx - L->cw ? L->cx : w - L->cx - L->cw;
            int ue = pw * 100 / 530;
            if (ue > u) ue = u;
            if (ue >= 8 && ue * 10 >= u * 7) {
                build(L, w, h, ue, M_PILLARS);
                done = layout_ok(L);
            }
        }
    }
    if (!done) {
        build(L, w, h, u, M_OVERLAY);
        L->overlay = 1;
    }
    for (int i = 0; i < TC_COUNT; i++)
        L->c[i].over = meets(&L->c[i], L->cx, L->cy, L->cw, L->ch);
}

void touch_layout(TouchLayout *L, int w, int h, int size_pct)
{
    touch_layout_in(L, w, h, size_pct, 0, 0, w, h);     /* the picture fills the window */
}

int touch_to_frame(const TouchLayout *L, int x, int y, int fw, int fh, int *fx, int *fy)
{
    if (L->cw <= 0 || L->ch <= 0 || x < L->cx || y < L->cy || x >= L->cx + L->cw || y >= L->cy + L->ch) return 0;
    if (fx) *fx = (int)((long long)(x - L->cx) * fw / L->cw);
    if (fy) *fy = (int)((long long)(y - L->cy) * fh / L->ch);
    return 1;
}

int touch_hit(const TouchLayout *L, int x, int y)
{
    /* the stick's zone is generous: 1.4x its ring */
    long dx = x - L->stick_cx, dy = y - L->stick_cy, zr = L->stick_r * 7 / 5;
    if (dx * dx + dy * dy <= zr * zr) return TC_STICK;
    for (int i = TC_SWORD; i < TC_COUNT; i++) {
        const TouchCtl *c = &L->c[i];
        if (c->shape == TSH_CIRCLE) {
            long cx = c->x + c->w / 2, cy = c->y + c->h / 2, r = c->w / 2;
            long ex = x - cx, ey = y - cy;
            if (ex * ex + ey * ey <= r * r) return i;
        } else if (x >= c->x && x < c->x + c->w && y >= c->y && y < c->y + c->h) return i;
    }
    return TC_NONE;
}

int touch_stick_dirs(int dx, int dy, int r)
{
    long dead = r / 4;
    if ((long)dx * dx + (long)dy * dy <= dead * dead) return 0;
    long ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    int d = 0;
    /* tan(22.5 deg) ~ 0.414 = 12/29: an axis is on when it is more than that
     * fraction of the other, which gives eight equal sectors */
    if (ax * 29 > ay * 12) d |= dx < 0 ? DIR_LEFT : DIR_RIGHT;
    if (ay * 29 > ax * 12) d |= dy < 0 ? DIR_UP : DIR_DOWN;
    return d;
}

int touch_mode_by_name(const char *s)
{
    if (!s) return -1;
    if (!strcmp(s, "auto")) return TOUCH_AUTO;
    if (!strcmp(s, "on") || !strcmp(s, "1") || !strcmp(s, "true")) return TOUCH_ON;
    if (!strcmp(s, "off") || !strcmp(s, "0") || !strcmp(s, "false")) return TOUCH_OFF;
    return -1;
}

/* ---------------------------------------------------------------- state */
void touch_init(Touch *t, int mode, int size_pct, int opacity_pct)
{
    memset(t, 0, sizeof *t);
    t->mode = mode;
    t->size = size_pct < 0 ? TOUCH_DEF_SIZE : clampi(size_pct, TOUCH_SIZE_MIN, TOUCH_SIZE_MAX);
    t->opacity = opacity_pct < 0 ? TOUCH_DEF_OPACITY : clampi(opacity_pct, TOUCH_OPACITY_MIN, TOUCH_OPACITY_MAX);
    t->visible = mode == TOUCH_ON;
    touch_layout(&t->L, 640, 480, t->size);
}

static void relabel(Touch *t)
{
    if (!t->dialog) return;
    static const struct { int id; const char *s; } d[] = {
        { TC_ESC, "CANCEL" }, { TC_ITEMS, "OK" }, { TC_SWORD, "PICK" }, { TC_MAGIC, "DEL" }, { TC_COMBO, "" } };
    for (unsigned i = 0; i < sizeof d / sizeof d[0]; i++) {
        strncpy(t->L.c[d[i].id].label, d[i].s, sizeof t->L.c[0].label - 1);
    }
}

void touch_resize(Touch *t, int w, int h)
{
    if (w == t->L.win_w && h == t->L.win_h) return;
    touch_layout_in(&t->L, w, h, t->size, 0, 0, w, h);
    relabel(t);
}

void touch_set_content(Touch *t, int w, int h, int cx, int cy, int cw, int ch)
{
    const TouchLayout *L = &t->L;
    if (w == L->win_w && h == L->win_h && cx == L->cx && cy == L->cy && cw == L->cw && ch == L->ch) return;
    touch_layout_in(&t->L, w, h, t->size, cx, cy, cw, ch);
    relabel(t);
}

void touch_set_dialog(Touch *t, int on)
{
    on = on != 0;
    if (on == t->dialog) return;
    t->dialog = on;
    const TouchLayout *L = &t->L;
    touch_layout_in(&t->L, L->win_w, L->win_h, t->size, L->cx, L->cy, L->cw, L->ch);   /* restores the labels */
    relabel(t);
}

int touch_visible(const Touch *t) { return t->visible && t->mode != TOUCH_OFF; }

static void release_all(Touch *t) { memset(t->f, 0, sizeof t->f); }

static TouchFinger *find(Touch *t, long long id)
{
    for (int i = 0; i < TOUCH_MAX_FINGERS; i++)
        if (t->f[i].active && t->f[i].id == id) return &t->f[i];
    return NULL;
}

static void push(Touch *t, int kind, char ch)
{
    if (t->nev < TOUCH_EVQ) { memset(&t->ev[t->nev], 0, sizeof t->ev[0]); t->ev[t->nev].kind = kind; t->ev[t->nev].ch = ch; t->nev++; }
}

void touch_finger_down(Touch *t, long long id, int x, int y)
{
    if (t->mode == TOUCH_OFF) return;
    if (t->mode == TOUCH_AUTO && !t->visible) t->visible = 1;   /* the first touch shows it */
    TouchFinger *f = find(t, id);
    if (!f) {
        for (int i = 0; i < TOUCH_MAX_FINGERS; i++) if (!t->f[i].active) { f = &t->f[i]; break; }
        if (!f) return;
    }
    f->active = 1; f->id = id; f->x = x; f->y = y;
    f->ctl = touch_hit(&t->L, x, y);
    switch (f->ctl) {
    case TC_ESC:   push(t, TEV_ESC, 0); break;
    case TC_ITEMS: push(t, TEV_ITEMS, 0); break;
    case TC_QSAVE: push(t, TEV_QSAVE, 0); break;
    case TC_QLOAD: push(t, TEV_QLOAD, 0); break;
    case TC_NONE:
        push(t, TEV_TAP, 0);
        if (t->nev) { t->ev[t->nev - 1].x = x; t->ev[t->nev - 1].y = y; }
        break;
    default:
        if (f->ctl >= TC_KEY0) push(t, TEV_CHAR, t->L.c[f->ctl].label[0]);
        break;
    }
}

void touch_finger_move(Touch *t, long long id, int x, int y)
{
    TouchFinger *f = find(t, id);
    if (f) { f->x = x; f->y = y; }          /* the binding never changes */
}

void touch_finger_up(Touch *t, long long id)
{
    TouchFinger *f = find(t, id);
    if (f) f->active = 0;
}

void touch_note_other_input(Touch *t)
{
    if (t->mode != TOUCH_AUTO || !t->visible) return;
    t->visible = 0;
    release_all(t);
}

int touch_ctl_down(const Touch *t, int ctl)
{
    for (int i = 0; i < TOUCH_MAX_FINGERS; i++)
        if (t->f[i].active && t->f[i].ctl == ctl) return 1;
    return 0;
}

static const TouchFinger *stick_finger(const Touch *t)
{
    for (int i = 0; i < TOUCH_MAX_FINGERS; i++)
        if (t->f[i].active && t->f[i].ctl == TC_STICK) return &t->f[i];
    return NULL;
}

uint8_t touch_dirs(const Touch *t)
{
    if (!touch_visible(t)) return 0;
    uint8_t d = 0;
    const TouchFinger *s = stick_finger(t);
    if (s) d |= (uint8_t)touch_stick_dirs(s->x - t->L.stick_cx, s->y - t->L.stick_cy, t->L.stick_r);
    if (touch_ctl_down(t, TC_COMBO)) d |= DIR_UP | DIR_DOWN;
    return d;
}

uint8_t touch_buttons(const Touch *t, unsigned ms)
{
    if (!touch_visible(t)) return 0;
    uint8_t b = 0;
    if (touch_ctl_down(t, TC_SWORD)) b |= 1;
    if (touch_ctl_down(t, TC_MAGIC)) b |= 2;
    if (touch_ctl_down(t, TC_COMBO) && (ms % COMBO_PERIOD_MS) < COMBO_ON_MS) b |= 1;
    return b;
}

int touch_menu_held(const Touch *t) { return touch_visible(t) && touch_ctl_down(t, TC_ITEMS); }

void touch_knob(const Touch *t, int *x, int *y)
{
    int kx = t->L.stick_cx, ky = t->L.stick_cy;
    const TouchFinger *s = stick_finger(t);
    if (s) {
        long dx = s->x - kx, dy = s->y - ky, r = t->L.stick_r;
        long d2 = dx * dx + dy * dy;
        if (d2 > r * r) {            /* clamp onto the ring: d = floor(sqrt(d2)) by bisection */
            long lo = 0, hi = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
            while (lo < hi) { long mid = (lo + hi + 1) / 2; if (mid * mid <= d2) lo = mid; else hi = mid - 1; }
            dx = dx * r / lo; dy = dy * r / lo;
        }
        kx += (int)dx; ky += (int)dy;
    }
    *x = kx; *y = ky;
}

int touch_next_event(Touch *t, TouchEvent *e)
{
    if (t->nev == 0) return 0;
    *e = t->ev[0];
    memmove(t->ev, t->ev + 1, (size_t)(t->nev - 1) * sizeof t->ev[0]);
    t->nev--;
    return 1;
}

#ifdef HAVE_SDL
#include "statekeys.h"
/* ---------------------------------------------------------------- SDL side */
int touch_handle_sdl(Touch *t, const SDL_Event *e, int out_w, int out_h)
{
    switch (e->type) {
    case SDL_FINGERDOWN:
        touch_resize(t, out_w, out_h);
        touch_finger_down(t, (long long)e->tfinger.fingerId, (int)(e->tfinger.x * (float)out_w), (int)(e->tfinger.y * (float)out_h));
        return 1;
    case SDL_FINGERMOTION:
        touch_finger_move(t, (long long)e->tfinger.fingerId, (int)(e->tfinger.x * (float)out_w), (int)(e->tfinger.y * (float)out_h));
        return 1;
    case SDL_FINGERUP:
        touch_finger_up(t, (long long)e->tfinger.fingerId);
        return 1;
    default: return 0;
    }
}

static void fill_rect(SDL_Renderer *r, int x, int y, int w, int h)
{
    SDL_Rect q = { x, y, w, h };
    SDL_RenderFillRect(r, &q);
}

/* a disc, or a ring when `inner` > 0, as one-pixel-tall spans */
static void ring(SDL_Renderer *r, int cx, int cy, int rad, int inner)
{
    for (int dy = -rad; dy <= rad; dy++) {
        int ox = 0;
        while ((ox + 1) * (ox + 1) + dy * dy <= rad * rad) ox++;
        if (dy * dy > rad * rad) continue;
        int ix = -1;
        if (inner > 0 && dy * dy < inner * inner) { ix = 0; while ((ix + 1) * (ix + 1) + dy * dy <= inner * inner) ix++; }
        if (ix < 0) fill_rect(r, cx - ox, cy + dy, 2 * ox + 1, 1);
        else {
            fill_rect(r, cx - ox, cy + dy, ox - ix, 1);
            fill_rect(r, cx + ix + 1, cy + dy, ox - ix, 1);
        }
    }
}

static void label(SDL_Renderer *r, const char *s, int cx, int cy, int maxw, int maxh)
{
    int sc = 1, w;
    while ((w = statekeys_text_width(s, sc + 1)) <= maxw && 7 * (sc + 1) <= maxh) sc++;
    w = statekeys_text_width(s, sc);
    int x = cx - w / 2, y = cy - 7 * sc / 2;
    for (const char *p = s; *p; p++, x += 6 * sc) {
        const uint8_t *gl = statekeys_glyph(*p);
        for (int col = 0; col < 5; col++)
            for (int row = 0; row < 7; row++)
                if (gl[col] & (1 << row)) fill_rect(r, x + col * sc, y + row * sc, sc, sc);
    }
}

void touch_draw(Touch *t, SDL_Renderer *ren)
{
    if (!touch_visible(t)) return;
    int ow = 0, oh = 0;
    if (SDL_GetRendererOutputSize(ren, &ow, &oh) != 0 || ow <= 0) return;
    touch_resize(t, ow, oh);
    const TouchLayout *L = &t->L;
    int a0 = 255 * t->opacity / 100;           /* over the picture: fainter */
    int a = L->c[TC_STICK].over ? a0 * 3 / 4 : a0;
    SDL_BlendMode old;
    SDL_GetRenderDrawBlendMode(ren, &old);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    /* the joystick: a ring with four wedge ticks and a knob */
    SDL_SetRenderDrawColor(ren, 255, 255, 255, a / 3);
    ring(ren, L->stick_cx, L->stick_cy, L->stick_r, 0);
    SDL_SetRenderDrawColor(ren, 255, 255, 255, a);
    ring(ren, L->stick_cx, L->stick_cy, L->stick_r, L->stick_r - (L->unit / 8 > 2 ? L->unit / 8 : 2));
    int tk = L->unit / 4 > 3 ? L->unit / 4 : 3;
    fill_rect(ren, L->stick_cx - tk / 2, L->stick_cy - L->stick_r + tk, tk, tk);
    fill_rect(ren, L->stick_cx - tk / 2, L->stick_cy + L->stick_r - 2 * tk, tk, tk);
    fill_rect(ren, L->stick_cx - L->stick_r + tk, L->stick_cy - tk / 2, tk, tk);
    fill_rect(ren, L->stick_cx + L->stick_r - 2 * tk, L->stick_cy - tk / 2, tk, tk);
    int kx, ky;
    touch_knob(t, &kx, &ky);
    SDL_SetRenderDrawColor(ren, 255, 255, 255, touch_ctl_down(t, TC_STICK) ? a : a * 2 / 3);
    ring(ren, kx, ky, L->unit * 7 / 10, 0);

    for (int i = TC_SWORD; i < TC_COUNT; i++) {
        const TouchCtl *c = &L->c[i];
        a = c->over ? a0 * 3 / 4 : a0;
        int down = touch_ctl_down(t, i);
        int cx = c->x + c->w / 2, cy = c->y + c->h / 2;
        /* a dark backing keeps the label legible over busy art */
        SDL_SetRenderDrawColor(ren, 0, 0, 0, (Uint8)(a / 2));
        if (c->shape == TSH_CIRCLE) ring(ren, cx, cy, c->w / 2, 0);
        else fill_rect(ren, c->x, c->y, c->w, c->h);
        /* tint: sword red, magic blue, combo amber, the rest neutral */
        int cr = 255, cg = 255, cb = 255;
        if (i == TC_SWORD) { cr = 255; cg = 110; cb = 100; }
        else if (i == TC_MAGIC) { cr = 120; cg = 160; cb = 255; }
        else if (i == TC_COMBO) { cr = 255; cg = 200; cb = 90; }
        SDL_SetRenderDrawColor(ren, (Uint8)cr, (Uint8)cg, (Uint8)cb, (Uint8)(down ? a : a / 3));
        if (c->shape == TSH_CIRCLE) ring(ren, cx, cy, c->w / 2, 0);
        else fill_rect(ren, c->x, c->y, c->w, c->h);
        SDL_SetRenderDrawColor(ren, (Uint8)cr, (Uint8)cg, (Uint8)cb, (Uint8)a);
        if (c->shape == TSH_CIRCLE) ring(ren, cx, cy, c->w / 2, c->w / 2 - (L->unit / 10 > 2 ? L->unit / 10 : 2));
        else {
            int b = L->unit / 12 > 1 ? L->unit / 12 : 1;
            fill_rect(ren, c->x, c->y, c->w, b); fill_rect(ren, c->x, c->y + c->h - b, c->w, b);
            fill_rect(ren, c->x, c->y, b, c->h); fill_rect(ren, c->x + c->w - b, c->y, b, c->h);
        }
        SDL_SetRenderDrawColor(ren, 255, 255, 255, (Uint8)(a > 255 ? 255 : (a * 5 / 4 > 255 ? 255 : a * 5 / 4)));
        label(ren, c->label, cx, cy, c->w * 7 / 10, c->h * 4 / 10 > 7 ? c->h * 4 / 10 : 7);
    }
    SDL_SetRenderDrawBlendMode(ren, old);
}
#endif
