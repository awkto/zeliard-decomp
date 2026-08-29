#include "render.h"
#include "enemy.h"
#include <string.h>

static void blit8(uint8_t *fb, const Cell8 *c, int x, int y)
{
    for (int r = 0; r < 8; r++) {
        int yy = y + r;
        if (yy < PF_Y || yy >= PF_Y + PF_H) continue;
        memcpy(fb + yy * FB_W + x, c->px[r], 8);
    }
}

/* gfmcga 412F: a tile-bank cell blitted with colour 0 transparent (sprites) */
static void blit8t(uint8_t *fb, const Cell8 *c, int x, int y)
{
    for (int r = 0; r < 8; r++) {
        int yy = y + r;
        if (yy < PF_Y || yy >= PF_Y + PF_H) continue;
        for (int k = 0; k < 8; k++) {
            if (!c->px[r][k]) continue;
            int xx = x + k;
            if (xx < PF_X || xx >= PF_X + PF_W) continue;
            fb[yy * FB_W + xx] = c->px[r][k];
        }
    }
}

static void blit2(uint8_t *fb, const Cell2 *c, int x, int y, int flip)
{
    for (int r = 0; r < 8; r++) {
        int yy = y + r;
        if (yy < PF_Y || yy >= PF_Y + PF_H) continue;
        for (int k = 0; k < 8; k++) {
            int sx = flip ? 7 - k : k;
            if (!c->mask[r][sx]) continue;
            int xx = x + k;
            if (xx < PF_X || xx >= PF_X + PF_W) continue;
            fb[yy * FB_W + xx] = c->px[r][sx];
        }
    }
}

/* gfmcga @3CDC: draw `count` cells of frame map `fr` into 3x3 slots from `slot0` */
static void draw_hero_frame(uint8_t *fb, const HeroGfx *h, int fr, int count, int slot0, int hx, int hy)
{
    if (fr < 0 || fr >= HERO_FRAMES) return;
    for (int i = 0; i < count; i++) {
        uint8_t b = h->frame[fr][i];
        if (!b) continue;
        int cell = b & 0x7F;
        if (cell >= h->ncells) continue;
        int slot = slot0 + i;
        blit2(fb, &h->cell[cell], hx + (slot % 3) * 8, hy + (slot / 3) * 8, b & 0x80);
    }
}

static int shield_class(const Game *g)                  /* gfmcga @3D22 */
{
    if (!g->shield) return 0;
    return g->shield < 4 ? 1 : 2;
}

/* gfmcga @3A95: three passes — overlay behind, body, overlay in front */
static void draw_hero(uint8_t *fb, const Game *g, const HeroGfx *h)
{
    if (g->hero_hidden) return;
    int hx = PF_X + g->hero_scr_col * 8, hy = PF_Y + g->hero_scr_row * 8;
    int left = g->hero_flags & FACE_LEFT;
    int ovl = left ? 49 : 31;                           /* 61B9 / 6117 */
    int crouch_n = g->crouching ? 6 : 9, crouch_s = g->crouching ? 3 : 0;

    /* pass 1 (3ABE): behind the body */
    if (!(g->hero_dead || g->on_ladder || g->hero_entering)) {
        int sh = shield_class(g);
        if (sh && !left) {                              /* 3B18..3B3F: shield behind when facing right */
            int fr = ovl + 12 + (g->crouching ? 1 : 0) + (sh - 1 ? 3 : 0);
            draw_hero_frame(fb, h, fr, crouch_n, crouch_s, hx, hy);
        } else if (!sh || left) {                       /* 3B43 */
            if (!g->crouching && g->hero_anim != 0x80) {
                int a = (g->hero_anim + 2) & 3;
                if (!(a & 1)) draw_hero_frame(fb, h, ovl + a, 9, 0, hx, hy);
            }
        }
    }
    /* pass 2 (3B80): the body */
    int fr;
    if (g->hero_entering) fr = 30;
    else if (g->on_ladder) fr = 26 + (g->hero_anim & 3);
    else {
        fr = left ? 13 : 0;
        if (g->hero_dead) fr += 10 + (g->hero_anim & 3);
        else if (g->crouching) fr += 5;
        else if (g->vstate & 0x80) fr += 7;
        else if (g->conveyor == 1) fr += 8;
        else if (g->conveyor == 2) fr += 9;
        else if (g->vstate == V_FALL) fr += 6;
        else if (g->hero_anim == 0x80) fr += 4;
        else fr += g->hero_anim & 3;
    }
    draw_hero_frame(fb, h, fr, 9, 0, hx, hy);
    if (g->hero_dead) return;

    /* pass 3 (3C05): in front of the body */
    if (g->on_ladder || g->hero_entering) {
        int sh = shield_class(g);
        if (!sh) return;
        fr = ovl + 14 + (((sh - 1) & 1) ? 3 : 0);
        draw_hero_frame(fb, h, fr, crouch_n, crouch_s, hx, hy);
        return;
    }
    int sh = shield_class(g);
    if (left && sh) fr = ovl + 12 + (g->crouching ? 1 : 0) + (sh - 1 ? 3 : 0);     /* 3C86 */
    else if (g->crouching || g->hero_anim == 0x80) fr = ovl + 3;                    /* 3CA5 */
    else fr = ovl + (g->hero_anim & 3);
    draw_hero_frame(fb, h, fr, crouch_n, crouch_s, hx, hy);
}

/* gfmcga 3599/366E: one 2-bpp cell through the frame's 16-entry colour table */
static void blit2r(uint8_t *fb, const Cell2R *c, const uint8_t *pal16, int x, int y)
{
    for (int r = 0; r < 8; r++) {
        int yy = y + r;
        if (yy < PF_Y || yy >= PF_Y + PF_H) continue;
        for (int k = 0; k < 8; k++) {
            if (!c->mask[r][k]) continue;
            int xx = x + k;
            if (xx < PF_X || xx >= PF_X + PF_W) continue;
            fb[yy * FB_W + xx] = pal16[c->idx[r][k]];
        }
    }
}

/* gfmcga 3336/33AB: every live object is a 2x2-cell sprite at its ring
 * (row, rcol).  The 5-byte frame {palette, TL, TR, BL, BR} comes from the AI
 * overlay's A030/A070 tables; palette + 3 while the hit flash is on. */
static void draw_enemies(uint8_t *fb, const Game *g)
{
    if (!g->egfx || !g->ai) return;
    for (int i = 0; i < g->nobj; i++) {
        const MapObj *o = &g->obj[i];
        if ((o->col >> 8) == 0xFF || o->rcol == 0xFF) continue;
        const uint8_t *fr = ai_frame(g->ai, o->type, o->hit, o->phase);
        if (!fr) continue;
        int pal = fr[0];
        if ((o->hit & 0x20) && !g->boss_map) pal += 3;                  /* hit flash */
        if (pal < 0 || pal > 4) pal %= 5;
        int sc = (int)o->rcol - 4;
        int sr = (int)((o->row - g->scroll_row) & 0x3F);
        if (sr > 40) sr -= 64;                  /* a sprite hanging above the window's top row */
        if (sr < -1 || sr >= SCREEN_ROWS + 1) continue;
        for (int k = 0; k < 4; k++) {
            int cell = fr[1 + k];
            if (!cell || cell >= g->egfx->ncells) continue;
            int cx = sc + (k & 1), cy = sr + (k >> 1);
            if (cx < -1 || cx > SCREEN_COLS) continue;
            blit2r(fb, &g->egfx->cell[cell], PAL2BPP[pal], PF_X + cx * 8, PF_Y + cy * 8);
        }
    }
}

/* 0x8366  projectiles: one tile-bank cell drawn transparently at the shot's
 * ring cell, inside the window only (ring col 4..0x1F, screen row < 0x12). */
static void draw_shots(uint8_t *fb, const Game *g)
{
    for (int i = 0; i <= MAX_SHOTS; i++) {
        const Shot *s = &g->shots[i];
        if (s->col == 0xFF) break;
        if (!s->col) continue;
        int sc = (int)s->col - 4;
        if (sc < 0 || sc >= SCREEN_COLS) continue;
        int sr = (int)((s->row - g->scroll_row) & 0x3F);
        if (sr >= SCREEN_ROWS) continue;
        uint8_t cell = shot_draw_cell(s);
        if (!cell || !g->tiles->present[cell]) continue;
        blit8t(fb, &g->tiles->cell[cell], PF_X + sc * 8, PF_Y + sr * 8);
    }
}

/* 0x896E  the hero's spell sprites: 2x2 tile-bank cells at the record's cell. */
static void draw_magic(uint8_t *fb, const Game *g)
{
    if (!g->magic_active) return;
    static const int OFS[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};      /* 8C79 */
    for (int i = 0; i < 4; i++) {
        const Magic *m = &g->magic[i];
        uint8_t cells[4];
        if (!magic_sprite_cells(g, m, cells)) continue;
        uint8_t rcol;
        if (ai_map_col_to_ring(g, m->col, &rcol)) continue;
        int sc = (int)rcol - 4, sr = (int)((m->row - g->scroll_row) & 0x3F);
        for (int k = 0; k < 4; k++) {
            int cx = sc + OFS[k][0], cy = sr + OFS[k][1];
            if (cx < 0 || cx >= SCREEN_COLS || cy < 0 || cy >= SCREEN_ROWS) continue;
            uint8_t cell = cells[k];
            if (!cell || !g->tiles->present[cell]) continue;
            blit8t(fb, &g->tiles->cell[cell], PF_X + cx * 8, PF_Y + cy * 8);
        }
    }
}

void render_frame(uint8_t *fb, const Game *g, const HeroGfx *h)
{
    memset(fb, 0, FB_W * FB_H);
    if (g->walk_in) {                                   /* 7C6E: the walk-in cutscene */
        if (!h) return;
        int fr = (g->hero_flags & FACE_LEFT) ? 13 : 0;
        fr += g->hero_anim & 3;
        draw_hero_frame(fb, h, fr, 9, 0, g->walk_in_x, 110);
        return;
    }
    for (int sr = 0; sr < SCREEN_ROWS; sr++) {
        for (int sc = 0; sc < SCREEN_COLS; sc++) {
            uint8_t v = game_ring_cell(g, sc, sr);
            if (v & 0x80) {                             /* a sprite marker hides the tile it covers;
                                                         * the background buffer (E900) keeps it */
                int i = v & 0x7F;
                v = i < g->nobj ? g->under_sprite[i] : 0;
                if (v & 0x80) continue;
            }
            if (!v || !g->tiles->present[v]) continue;
            blit8(fb, &g->tiles->cell[v], PF_X + sc * 8, PF_Y + sr * 8);
        }
    }
    draw_enemies(fb, g);
    draw_shots(fb, g);
    draw_magic(fb, g);
    if (h) draw_hero(fb, g, h);
}

/* ------------------------------------------------------------------- HUD */
/* docs/VIDEO_DRIVERS.md [2006]/[2008]: the LIFE bar is 100 px at (84,163) —
 * a 6-row red bar of max/8 px with a 5-row bar of cur/8 px over it, the
 * overlap reading green.  Digits: [2016] GOLD 6 at (76,187), [2014] ALMAS 5 at
 * (152,187), 6x7 glyphs on a 6-px pitch.  The frame and the narrow-font labels
 * are not drawn. */
#define HUD_Y 158
#define HUD_BOX  0x28           /* dark-blue digit box */
#define HUD_TEXT 0x09           /* white glyph */
/* vid_draw_digits (MCGA 24A3): 6-px pitch, a 6x8 box per cell and the glyph
 * drawn 2 px into it (the CH&1 flag the GOLD/ALMAS callers pass). */
static void hud_digits(uint8_t *fb, const DigitFont *f, unsigned v, int x, int y, int n)
{
    for (int i = 0; i < n; i++)
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 6; c++) {
                int xx = x + i * 6 + c;
                if (xx < FB_W) fb[(y + r) * FB_W + xx] = HUD_BOX;
            }
    for (int i = n - 1; i >= 0; i--, v /= 10) {
        unsigned d = v % 10;
        if (v == 0 && i != n - 1) return;               /* blank-led */
        int gx = x + i * 6 + 2;
        for (int r = 0; r < 7; r++)
            for (int c = 0; c < 6; c++) {
                int on = f && f->loaded ? (f->glyph[d][r] >> (5 - c)) & 1
                                        : (r == 0 || r == 6 || c == 0 || c == 5);
                if (on && gx + c < FB_W) fb[(y + r) * FB_W + gx + c] = HUD_TEXT;
            }
    }
}

void render_hud(uint8_t *fb, const Game *g, const DigitFont *font)
{
    for (int y = HUD_Y; y < FB_H; y++) memset(fb + y * FB_W, 0, FB_W);
    unsigned wmax = g->max_hp / 8, wcur = g->hp / 8;
    if (wmax > 100) wmax = 100;
    if (wcur > wmax) wcur = wmax;
    for (unsigned x = 0; x < wmax; x++) {
        for (int r = 0; r < 6; r++) fb[(163 + r) * FB_W + 84 + x] = 0x12;   /* red */
        if (x < wcur) for (int r = 0; r < 5; r++) fb[(163 + r) * FB_W + 84 + x] = 0x1B;  /* green */
    }
    hud_digits(fb, font, (unsigned)g->gold, 76, 187, 6);
    hud_digits(fb, font, g->almas, 152, 187, 5);
}

void render_to_rgb(const uint8_t *fb, uint8_t *rgb)
{
    for (int i = 0; i < FB_W * FB_H; i++) {
        const uint8_t *p = PAL_RGB[fb[i] & 63];
        rgb[i * 3] = p[0]; rgb[i * 3 + 1] = p[1]; rgb[i * 3 + 2] = p[2];
    }
}
