#include "render.h"
#include <string.h>

static void blit8(uint8_t *fb, const Cell8 *c, int x, int y)
{
    for (int r = 0; r < 8; r++) {
        int yy = y + r;
        if (yy < PF_Y || yy >= PF_Y + PF_H) continue;
        memcpy(fb + yy * FB_W + x, c->px[r], 8);
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

void render_frame(uint8_t *fb, const Game *g, const HeroGfx *h)
{
    memset(fb, 0, FB_W * FB_H);
    for (int sr = 0; sr < SCREEN_ROWS; sr++) {
        for (int sc = 0; sc < SCREEN_COLS; sc++) {
            uint8_t v = game_ring_cell(g, sc, sr);
            if (v & 0x80) continue;                     /* sprite marker: enemies not drawn yet */
            if (!v || !g->tiles->present[v]) continue;
            blit8(fb, &g->tiles->cell[v], PF_X + sc * 8, PF_Y + sr * 8);
        }
    }
    if (h) draw_hero(fb, g, h);
}

void render_to_rgb(const uint8_t *fb, uint8_t *rgb)
{
    for (int i = 0; i < FB_W * FB_H; i++) {
        const uint8_t *p = PAL_RGB[fb[i] & 63];
        rgb[i * 3] = p[0]; rgb[i * 3 + 1] = p[1]; rgb[i * 3 + 2] = p[2];
    }
}
