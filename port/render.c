#include "render.h"
#include "gfx.h"
#include "video.h"
#include "text.h"
#include "enemy.h"
#include <string.h>

/* Smooth scrolling (#127): NULL on the engine's own path */
static const SmoothOfs *s_sm;
void render_set_smooth(const SmoothOfs *ofs) { s_sm = ofs && ofs->active ? ofs : NULL; }

/* The view geometry of the frame being drawn (#126).  The stock cavern view is the 28-column window
 * (ring cols 4..31) at x 48 of a 320-wide framebuffer.  The wide view (render_frame_view) shows `lo`
 * / `hi` more ring columns on the left / right (at most the 4 each the ring keeps live beyond the
 * window) in a framebuffer 8*(lo+hi) wider, so every x below is derived from these four values and
 * with the defaults every expression is the stock one.  They are set for the length of one
 * render_frame_view call only; everything else in this file (HUD, dissolve, fade-in, burst) is the
 * 320-wide screen. */
static int v_w = FB_W;          /* framebuffer stride */
static int v_x = PF_X;          /* x of the playfield's left edge */
static int v_lo, v_hi;          /* extra ring columns shown on the left / right */
#define V_COLS (SCREEN_COLS + v_lo + v_hi)
#define V_PW (V_COLS * 8)       /* playfield width in pixels */
#define VX(sc) (v_x + ((sc) + v_lo) * 8)    /* x of screen cell `sc` (0 = ring col 4, negative into the left margin) */

static void blit8(uint8_t *fb, const Cell8 *c, int x, int y)
{
    /* x is clipped to the playfield too: a tile drawn part-way through a smooth
     * slide straddles its edge (an aligned tile never does, so nothing changes) */
    int x0 = x < v_x ? v_x : x, x1 = x + 8 > v_x + V_PW ? v_x + V_PW : x + 8;
    if (x1 <= x0) return;
    for (int r = 0; r < 8; r++) {
        int yy = y + r;
        if (yy < PF_Y || yy >= PF_Y + PF_H) continue;
        memcpy(fb + yy * v_w + x0, c->px[r] + (x0 - x), (size_t)(x1 - x0));
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
            if (xx < v_x || xx >= v_x + V_PW) continue;
            fb[yy * v_w + xx] = c->px[r][k];
        }
    }
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
            if (xx < v_x || xx >= v_x + V_PW) continue;
            fb[yy * v_w + xx] = pal16[c->idx[r][k]];
        }
    }
}

/* gfmcga @3CDC: draw `count` cells of frame map `fr` into 3x3 slots from `slot0`.
 * 3CE8 is `mov ch,0x20 / mul ch`, an 8-bit multiply of the *whole* frame byte,
 * so the byte is a plain cell index 0..229 -- bit 7 is not a mirror flag.  The
 * port used to mask it off and flip, which was invisible for every frame the
 * verify boxes covered (the walk and idle maps all use cells < 0x80) and wrong
 * for exactly the ones that do not: the attack and cast overlays (35..42,
 * 53..60, cells 0x88..0x9F) and the door-entry frame 30. */
static void draw_hero_frame(uint8_t *fb, const HeroGfx *h, const uint8_t *pal16, int fr, int count, int slot0, int hx, int hy)
{
    if (fr < 0 || fr >= HERO_FRAMES) return;
    for (int i = 0; i < count; i++) {
        int cell = h->frame[fr][i];                     /* 3CDD/3CE0: 0 = empty slot */
        if (!cell || cell >= h->ncells) continue;
        int slot = slot0 + i;
        blit2r(fb, &h->cell[cell], pal16, hx + (slot % 3) * 8, hy + (slot / 3) * 8);
    }
}

static int shield_class(const Game *g)                  /* gfmcga @3D22 */
{
    if (!g->shield) return 0;
    return g->shield < 4 ? 1 : 2;
}

/* fight.bin 7094, immediately before GF_DRAW_HERO: the renderer is handed
 *   [FF40] anim_flag = attacking || casting
 *   [FF41] anim_kind = attack_type while attacking (0 slash, 1 up, 2 thrust),
 *                      1 while casting
 *   [FF3F] anim_arg  = [FF46] attack_var / [9F2B] cast_timer
 * and both overlay passes take a different branch when [FF40] is set (3ACF and
 * 3C36).  `anim_arg >> 1` is the overlay variant, so a six-frame slash cycles
 * three poses.  The port never set any of this, so Garland kept his idle arms
 * through every swing -- half of issue #35, the other half being the blade. */
static int hero_anim_flag(const Game *g) { return g->attacking || g->casting; }
static int hero_anim_kind(const Game *g) { return g->attacking ? (g->attack_type <= 2 ? g->attack_type : 0) : 1; }
static int hero_anim_var(const Game *g)  { return (g->attacking ? g->attack_var : g->cast_timer) >> 1; }

/* The sword-arm overlay, from the facing's own overlay base (31 right / 49
 * left): +4 slash (3B00/3C64 `add ax,0x24`), +8 upward slash or cast
 * (3B08/3C6F `add ax,0x24` again) and +11 for the down-thrust, which has no
 * variant (3B11/3C78 `mov ax,0x63`).  All three offsets are exact multiples of
 * the 9-byte frame map. */
static int hero_blade_frame(const Game *g, int base)
{
    int kind = hero_anim_kind(g);
    if (kind == 2) return base + 11;
    return base + (kind == 1 ? 8 : 4) + hero_anim_var(g);
}

/* The shield-arm overlay while attacking: frame 79 facing right (3AEE
 * `add si,0x62C7`) and 67 facing left (3C55 `add si,0x625B`), each plus four
 * frames per shield class and the same variant. */
static int hero_guard_frame(const Game *g, int base) { return base + shield_class(g) * 4 + hero_anim_var(g); }

/* gfmcga @3A95: three passes — overlay behind, body, overlay in front.
 * 3A95..3AA5 first picks the colour table every hero cell goes through (366E
 * `add bx,[4FE9]`): word [4F8C + 2*([FF36] & 3)].  The hit flash is 0 or 0xFF,
 * so a hit frame draws Garland with table 3 (4FC8, the yellow/blue/magenta
 * one enemies use for their own hit flash) instead of table 0. */
static void draw_hero_at(uint8_t *fb, const Game *g, const HeroGfx *h, int hx, int hy, int entering)
{
    if (g->hero_hidden) return;
    const uint8_t *pal = PAL2BPP[g->hero_hit_flash & 3];
    int left = g->hero_flags & FACE_LEFT;
    int ovl = left ? 49 : 31;                           /* 61B9 / 6117 */
    int crouch_n = g->crouching ? 6 : 9, crouch_s = g->crouching ? 3 : 0;

    /* pass 1 (3ABE): behind the body */
    if (!(g->hero_dead || g->on_ladder || entering)) {
        int sh = shield_class(g);
        if (hero_anim_flag(g)) {                        /* 3ACF: the attack/cast overlays */
            /* facing right the shield arm is the far one and goes behind
             * (3ADA); facing left it is the sword arm (3AF4). */
            int fr = left ? hero_blade_frame(g, 49) : hero_guard_frame(g, 79);
            draw_hero_frame(fb, h, pal, fr, crouch_n, crouch_s, hx, hy);
        } else if (sh && !left) {                       /* 3B18..3B3F: shield behind when facing right */
            int fr = ovl + 12 + (g->crouching ? 1 : 0) + (sh - 1 ? 3 : 0);
            draw_hero_frame(fb, h, pal, fr, crouch_n, crouch_s, hx, hy);
        } else if (!sh || left) {                       /* 3B43 */
            if (!g->crouching && g->hero_anim != 0x80) {
                int a = (g->hero_anim + 2) & 3;
                if (!(a & 1)) draw_hero_frame(fb, h, pal, ovl + a, 9, 0, hx, hy);
            }
        }
    }
    /* pass 2 (3B80): the body */
    int fr;
    if (entering) fr = 30;
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
    draw_hero_frame(fb, h, pal, fr, 9, 0, hx, hy);
    if (g->hero_dead) return;

    /* pass 3 (3C05): in front of the body */
    if (g->on_ladder || entering) {
        int sh = shield_class(g);
        if (!sh) return;
        fr = ovl + 14 + (((sh - 1) & 1) ? 3 : 0);
        draw_hero_frame(fb, h, pal, fr, crouch_n, crouch_s, hx, hy);
        return;
    }
    int sh = shield_class(g);
    if (hero_anim_flag(g))                                                          /* 3C36 */
        fr = left ? hero_guard_frame(g, 67) : hero_blade_frame(g, 31);
    else if (left && sh) fr = ovl + 12 + (g->crouching ? 1 : 0) + (sh - 1 ? 3 : 0); /* 3C86 */
    else if (g->crouching || g->hero_anim == 0x80) fr = ovl + 3;                    /* 3CA5 */
    else fr = ovl + (g->hero_anim & 3);
    draw_hero_frame(fb, h, pal, fr, crouch_n, crouch_s, hx, hy);
}


static void draw_hero(uint8_t *fb, const Game *g, const HeroGfx *h)
{
    draw_hero_at(fb, g, h, VX(g->hero_scr_col) + (s_sm ? s_sm->hx : 0),
                 PF_Y + g->hero_scr_row * 8 + (s_sm ? s_sm->hy : 0), g->hero_entering);
}

/* gfmcga 3E34/3FD0 compute the blade's pixel origin [4FF7] = [4FED] + the
 * frame's {row,col} cell offset.  3F42/3F64 save and restore a 32x32 block
 * there (0x20 rows of 0x20 bytes, stride 0x140: `rep movsw` advances 0x20 then
 * `add si,0x120`), and GF_ERASE_SPRITES (3F29, fight.bin 7136, *after* the
 * sparks) restores it, so a death spark under the blade's block is erased and
 * the blade redrawn (3E34 at 7147) -- the cell clipping the port was missing. */
static int sword_origin_at(const Game *g, const HeroGfx *h, int f, int hsr, int hsc,
                           int *x0, int *y0)
{
    if (!g->attacking || g->hero_hidden || g->hero_dead) return 0;       /* 3FD0: test [FF43] */
    const SwordGfx *sg = &h->sword;
    if (!sg->loaded || !g->sword) return 0;                              /* [92] = 0: no blade */
    int sw = g->sword > 6 ? 6 : g->sword;
    int sec = SWORD_SECTION[sw - 1];                                     /* kernel mode 4 */
    int kind = g->attack_type <= 2 ? g->attack_type : 0;                 /* [FF45] */
    int grp = kind + ((g->hero_flags & FACE_LEFT) ? 3 : 0);              /* 3E6C: test [C2],1 */
    if (f >= SWORD_FRAMES[kind]) f = SWORD_FRAMES[kind] - 1;
    if (f < 0) f = 0;
    *x0 = VX(hsc) + sg->delta[sec][grp][f][1] * 8;
    *y0 = PF_Y + (hsr + sg->delta[sec][grp][f][0] + (g->crouching ? 1 : 0)) * 8;
    return 1;
}

/* gfmcga 3FD0: the sword block.  The blade is a 4x4-cell (32x32 px) sprite
 * whose origin is the hero's top-left plus the frame's own {row, col} cell
 * offset (3ED9/3EEE, one cell row lower while crouching, `test [FF38]` at
 * 3EF4).  The 16 cell ids run column-major (the outer loop at 4013 is the four
 * columns, `add di,8`; the inner one the four rows, `add di,0xA00`), 0xFF is an
 * empty cell, and 4092 paints pixel value 0 transparent, 1 and 2 in `[4FF5]`
 * and 3 in `[4FF6]`.
 *
 * Draw order: gfmcga's row hook calls 40F0 (the hero) at screen row [84]-5 and
 * 3FD0 (the sword) at [84]+5 with the row counter *rising* (3E1D returns while
 * cl < [84]-5), so the blade goes on top of the 3x3 fman frame -- which is why
 * a wind-up frame's cells over the body are visible at all.  Nothing in the
 * port drew this: `sword_apply` used the shapes for the hit test only, so
 * Garland fought bare-handed (issue #35). */
static void draw_sword_at(uint8_t *fb, const Game *g, const HeroGfx *h, int f, int hsr, int hsc)
{
    int x0, y0;
    if (!sword_origin_at(g, h, f, hsr, hsc, &x0, &y0)) return;
    if (s_sm) { x0 += s_sm->hx; y0 += s_sm->hy; }
    const SwordGfx *sg = &h->sword;
    int sw = g->sword > 6 ? 6 : g->sword;
    int sec = SWORD_SECTION[sw - 1];                                     /* kernel mode 4 */
    int kind = g->attack_type <= 2 ? g->attack_type : 0;                 /* [FF45] */
    int grp = kind + ((g->hero_flags & FACE_LEFT) ? 3 : 0);              /* 3E6C: test [C2],1 */
    if (f >= SWORD_FRAMES[kind]) f = SWORD_FRAMES[kind] - 1;
    if (f < 0) f = 0;
    const uint8_t *blk = sg->block[sec][grp][f];
    uint8_t c12 = sg->colour[sw - 1][0], c3 = sg->colour[sw - 1][1];
    for (int cc = 0; cc < 4; cc++)
        for (int cr = 0; cr < 4; cr++) {
            uint8_t cell = blk[cc * 4 + cr];
            if (cell == 0xFF || cell >= sg->ncells[sec]) continue;        /* 401A: 0xFF skips */
            for (int r = 0; r < 8; r++) {
                int yy = y0 + cr * 8 + r;
                if (yy < PF_Y || yy >= PF_Y + PF_H) continue;
                for (int k = 0; k < 8; k++) {
                    unsigned v = sg->idx[sec][cell][r][k];
                    if (!v) continue;                                     /* 40A0: transparent */
                    int xx = x0 + cc * 8 + k;
                    if (xx < v_x || xx >= v_x + V_PW) continue;
                    fb[yy * v_w + xx] = v == 3 ? c3 : c12;               /* 40AB / 40CB */
                }
            }
        }
}

/* 3E34 draws frame [FF46] - 1 at each [3006]; the screen the 717F boundary leaves is 7147's, [FF46] + 1 - 1
 * with attack_var the post-7047 value (the front end's trace boundary and present both hold it there):
 * the second half of the frame, at the hero's current cell. */
static void draw_sword(uint8_t *fb, const Game *g, const HeroGfx *h)
{
    draw_sword_at(fb, g, h, g->attack_var, g->hero_scr_row, g->hero_scr_col);
}

/* gfmcga 3336/33AB: every live object is a 2x2-cell sprite at its ring
 * (row, rcol).  The 5-byte frame {palette, TL, TR, BL, BR} comes from the AI
 * overlay's A030/A070 tables; palette + 3 while the hit flash is on. */
static void draw_enemies(uint8_t *fb, const Game *g)
{
    if (!g->egfx || !g->ai) return;
    /* gfmcga 302C scans the visible ring cells row-major and draws each sprite
     * marker (0x80|i) the moment the scan reaches it, through one of four
     * routines.  Each one first swaps the E900 screen-copy bytes under the
     * sprite for sentinels (a word xchg, so the write is unconditional), saving
     * the old bytes, and 356E then draws a cell only when its saved byte is not
     * 0xFF/0xFC (3573/3575; the call at 356B runs 356E twice, TL then TR through
     * the saved bytes [5010]/[5011], and again at 34CB for BL/BR through
     * [5012]/[5013]).  The word xchg at 3472 swaps ax = FFFE with [E900+cell-1..cell]
     * (little endian: TL <- FE, TR <- FF) and the one at 347A swaps FFFF with the
     * row below ([di+1B]: the screen is 28 cells wide), so the swap is
     * unconditional and a cell already claimed by an earlier sprite saves FF:
     *   3468 (marker at screen col 0..26)  TL<-FE, TR<-FF, BL<-FF, BR<-FF; saved
     *        TL,TR,BL,BR each gate their own cell; row 17 skips the bottom pair
     *   34E3 (col 27)   TL<-FE, BL<-FF; only the left column is on screen
     *   33E3 (col -1)   TR<-FF, BR<-FF; only the right column is on screen
     *   3363 (row -1, pre-scan)  E900[c], E900[c+1] <- FF; draws BL (gated by its
     *        saved byte) and BR (never gated) at row 0;  3326 (col -1): [0] <- FF
     * Every sentinel is gone by the end of the frame (the scan then visits the
     * cell: a plain cell turns FF into FE via 3124, a marker cell writes its own
     * FE), so the old bytes a sprite sees are the FFs written by sprites drawn
     * earlier in the same scan: a later sprite's cell is skipped exactly when an
     * earlier sprite's TR/BL/BR (or a row -1 sprite's pair) covers it.  Hence
     * the draw order here is the scan order (row, then column), not the object
     * index order.  (dragon_kill f306: the flame part, the last record, sits above the head part whose 2x2 it overlaps; crab_fight f149: body part 9's BL covers the falling shot
     * 0x35's TL and TR, whose row-7 sprite edge is the residue.) */
    /* The same rules hold at the edges of a wider view: `u` is the screen cell counted from the view's
     * left edge (u = rcol - 4 + lo; 0 .. cols-1), `cols` the view width, and the 3468 / 34E3 / 33E3
     * routines are the interior, the right edge and the left edge of that view.  A sprite in the margin
     * columns is a plain 3468 sprite there, because the ring holds its marker like any other. */
    const int FFW = V_COLS, cols = V_COLS;
    static uint8_t ff[(SCREEN_ROWS + 3) * (SCREEN_COLS + 8) + 2];
    memset(ff, 0, sizeof ff);
    int order[256], key[256], n = 0;
    for (int i = 0; i < g->nobj && n < 256; i++) {
        const MapObj *o = &g->obj[i];
        if ((o->col >> 8) == 0xFF || o->rcol == 0xFF) continue;
        /* gfmcga 302C draws the ring's sprite markers (0x80|i), not the C010
         * records: an object whose marker is gone (overwritten, or a stale
         * record left by TORI's A647 death pass) is not on screen. */
        int hp = game_ring_index(g, o->row, o->rcol);
        if (g->ring[hp] != (uint8_t)(0x80 | i)) continue;
        int sc = (int)o->rcol - 4 + v_lo;
        int sr = (int)((o->row - g->scroll_row) & 0x3F);
        if (sr > 40) sr -= 64;                  /* a sprite hanging above the window's top row */
        if (sr < -1 || sr >= SCREEN_ROWS + 1) continue;
        int k = n++;
        while (k > 0 && key[k - 1] > (sr + 1) * 64 + sc + 1) {       /* insertion sort, stable */
            key[k] = key[k - 1]; order[k] = order[k - 1]; k--;
        }
        key[k] = (sr + 1) * 64 + sc + 1; order[k] = i;
    }
    for (int j = 0; j < n; j++) {
        int i = order[j];
        const MapObj *o = &g->obj[i];
        const uint8_t *fr = ai_frame(g->ai, o->type, o->hit, o->phase);
        if (!fr) continue;
        int pal = fr[0];
        if ((o->hit & 0x20) && !g->boss_map) pal += 3;                  /* hit flash */
        if (pal < 0 || pal > 4) pal %= 5;
        int sc = (int)o->rcol - 4 + v_lo;
        int sr = (int)((o->row - g->scroll_row) & 0x3F);
        if (sr > 40) sr -= 64;
        uint8_t skip[4] = {0, 0, 0, 0};                 /* saved byte was FF: that cell is not drawn */
        if (sr == -1) {
            if (sc == -1) { if (ff[0]) continue; ff[0] = 1; skip[0] = skip[1] = skip[2] = 1; }
            else if (sc >= 0 && sc < cols) {
                skip[2] = ff[sc];
                ff[sc] = 1; ff[sc + 1] = 1;
            }
        } else if (sr >= 0 && sr < 18) {
            uint8_t *t = &ff[sr * FFW + sc];
            if (sc >= 0 && sc <= cols - 2) {                           /* 3468 */
                skip[0] = t[0]; skip[1] = t[1]; skip[2] = t[FFW]; skip[3] = t[FFW + 1];
                t[0] = 0; t[1] = 1; t[FFW] = 1; t[FFW + 1] = 1;
            } else if (sc == cols - 1) {                               /* 34E3 */
                skip[0] = t[0]; skip[2] = t[FFW];
                t[0] = 0; t[FFW] = 1;
            } else if (sc == -1) {                                     /* 33E3 */
                t = &ff[sr * FFW];
                skip[1] = t[0]; skip[3] = t[FFW];
                t[0] = 1; t[FFW] = 1;
            }
            if (sr == 17) skip[2] = skip[3] = 1;                       /* 3468: [4FF1]==1, no bottom pair */
        }
        if (sr < -1 || sr >= SCREEN_ROWS + 1) continue;
        for (int k = 0; k < 4; k++) {
            int cell = fr[1 + k];
            if (skip[k]) continue;
            if (!cell || cell >= g->egfx->ncells) continue;
            int cx = sc + (k & 1), cy = sr + (k >> 1);
            if (cx < -1 || cx > cols) continue;
            blit2r(fb, &g->egfx->cell[cell], PAL2BPP[pal], v_x + cx * 8 + (s_sm ? s_sm->ox[i] : 0),
                   PF_Y + cy * 8 + (s_sm ? s_sm->oy[i] : 0));
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
        int sc = (int)s->col - 4 + v_lo;         /* screen cell from the view's left edge */
        int sr = (int)((s->row - g->scroll_row) & 0x3F);
        if (s_sm) {                              /* a slide reveals the ring column / row just outside the window */
            if (sc < -1 || sc > V_COLS) continue;
            if (sr == 0x3F) sr = -1;
        } else if (sc < 0 || sc >= V_COLS) continue;
        if (sr >= SCREEN_ROWS) continue;
        uint8_t cell = shot_draw_cell(s);
        if (!cell || !g->tiles->present[cell]) continue;
        blit8t(fb, &g->tiles->cell[cell], v_x + sc * 8 + (s_sm ? s_sm->sx[i] : 0),
               PF_Y + sr * 8 + (s_sm ? s_sm->sy[i] : 0));
    }
}

/* Does an enemy sprite's 2x2 block cover the screen cell (sc, sr)?  (the ring marker sits on the top-left cell) */
int render_sprite_cell(const Game *g, int sc, int sr)
{
    for (int i = 0; i < g->nobj; i++) {
        const MapObj *o = &g->obj[i];
        if ((o->col >> 8) == 0xFF || o->rcol == 0xFF) continue;
        if (g->ring[game_ring_index(g, o->row, o->rcol)] != (uint8_t)(0x80 | i)) continue;
        int c = (int)o->rcol - 4, r = (int)((o->row - g->scroll_row) & 0x3F);
        if (r > 40) r -= 64;
        if (sc >= c && sc <= c + 1 && sr >= r && sr <= r + 1) return 1;
    }
    return 0;
}

/* The projectile cells at the half-way wait (712D): 8366 drew them at 713B of the previous frame and its next pass (the
 * erase at 83F3) is after the wait, so the screen still holds the old cells at the old places -- unless something wrote
 * over them since (sword-frame, from the oracle's pictures at the draw points).  302C's cell pass (710F) redraws a
 * background cell when the shadow at E900 differs from the ring: a different cell (a scroll, an animated one), a sprite
 * block, or a cell a sprite held (its shadow was FE); the hero is drawn over its 3x3 cells, now and where it was. */
static int ghost_in_block(int sc, int sr, int c0, int r0, int w, int h)
{
    return sc >= c0 && sc < c0 + w && sr >= r0 && sr < r0 + h;
}

static void draw_shots_mid(uint8_t *fb, const Game *g)
{
    for (int i = 0; i < g->shot_pics; i++) {
        int sc = g->shot_pic[i].sc, sr = g->shot_pic[i].sr;
        int rp = (game_win(g) + sr * RING_W + sc + 4) % RING_SIZE; if (rp < 0) rp += RING_SIZE;
        uint8_t v = g->bg_ring[rp];
        if (v != g->shot_pic[i].bgv || (v & 0x80) || g->shot_pic[i].spr || render_sprite_cell(g, sc, sr)) continue;
        if (ghost_in_block(sc, sr, g->hero_scr_col, g->hero_scr_row, 3, 3)) continue;
        if (g->hero_drawn_ok && ghost_in_block(sc, sr, g->hero_drawn_col, g->hero_drawn_row, 3, 3)) continue;
        uint8_t cell = g->shot_pic[i].cell;
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
        int sc = (int)rcol - 4 + v_lo, sr = (int)((m->row - g->scroll_row) & 0x3F);
        for (int k = 0; k < 4; k++) {
            int cx = sc + OFS[k][0], cy = sr + OFS[k][1];
            if (s_sm ? (cx < -1 || cx > V_COLS || cy < -1 || cy >= SCREEN_ROWS)
                     : (cx < 0 || cx >= V_COLS || cy < 0 || cy >= SCREEN_ROWS)) continue;
            uint8_t cell = cells[k];
            if (!cell || !g->tiles->present[cell]) continue;
            blit8t(fb, &g->tiles->cell[cell], v_x + cx * 8 + (s_sm ? s_sm->mx[i] : 0),
                   PF_Y + cy * 8 + (s_sm ? s_sm->my[i] : 0));
        }
    }
}

/* video slot [0x2000] vid_window — text.c owns the implementation; this
 * wrapper keeps the pixel arguments the encounter card is written with. */
/* gfmcga 379B: each live spark is a 16x16 picture at screen (48 + 8x, 14 + 8y),
 * {x, y, life, kind}, shape life & 3, drawn straight to the screen (no clip, so
 * x = 26 spills 8 px past the playfield).  4092 turns a pixel pair into
 * mask/colour: value 3 is [4FF6], 1 and 2 are [4FF5], 0 leaves the screen alone. */
static void draw_sparks(uint8_t *fb, const Game *g, const HeroGfx *h)
{
    if (!h || !h->spark.loaded || !g->boss_dying) return;
    for (const uint8_t *e = g->spark_drawn; *e != 0xFF && e + 4 <= g->spark_drawn + sizeof g->spark_drawn; e += 4) {
        int x0 = PF_X + e[0] * 8, y0 = PF_Y + e[1] * 8;
        const uint8_t *col = h->spark.col[e[3] & 3];
        for (int r = 0; r < 16; r++)
            for (int k = 0; k < 16; k++) {
                int v = h->spark.shape[e[2] & 3][r][k];
                if (!v) continue;
                int c = v == 3 ? col[1] : col[0];
                int x = x0 + k, y = y0 + r;
                if (x < FB_W && y < FB_H) fb[y * FB_W + x] = (uint8_t)c;
            }
    }
}

void render_window(uint8_t *fb, int x, int y, int w, int h, int framed)
{
    vid_window(fb, framed, x / 4, y, w / 4, h);
}

void render_screen_frame(uint8_t *fb, const struct ScreenFrame *f, int y0, int y1)
{
    if (!f || !f->loaded) return;
    if (y0 < 0) y0 = 0;
    if (y1 > FB_H) y1 = FB_H;
    for (int y = y0; y < y1; y++)
        for (int x = 0; x < FB_W; x++) {
            int i = y * FB_W + x;
            if (f->on[i]) fb[i] = f->px[i];
        }
}

/* fight.bin 73E0 / 7210: the cavern message box.  video [2000] (BX=0x0E1E,
 * CX=0x3410, AL=0xFF) clears the interior and draws the 208x16 white frame at
 * (56,30); [202A] (BX=record x + 0x3A, CL=0x22) prints the string at y 34.
 * 7210 keeps it up for 32 frames (the original marks the cells 0xFC in the E900
 * copy so gfmcga 312E leaves the box's pixels alone); the port paints it every
 * frame on top of the playfield and sprites, which is what those markers do. */
static void draw_message_box(uint8_t *fb, const Game *g)
{
    Game *gm = (Game *)g;                       /* the frozen region is a cache */
    const int bx0 = 56, by = 30, bw = 208, bh = 16;
    /* In a wider view the box is centred on the playfield: the drawing routines below only know the
     * 320-wide screen, so it is composed there (a scratch screen) and the box rows copied across. */
    const int wide = v_w != FB_W;
    const int bx = bx0 + (v_lo + v_hi) * 4;     /* where the box lands in `fb` */
    static uint8_t scr[FB_W * FB_H];
    uint8_t *d = wide ? scr : fb;               /* the 320-wide screen the box is drawn on */
    if (!g->msg_box && !g->boss_text) {                          /* keep the box-free frame's region */
        for (int r = 0; r < bh; r++)
            memcpy(gm->msg_bg + r * bw, fb + (by + r) * v_w + bx, bw);
        return;
    }
    for (int r = 0; r < bh; r++)                /* restore it: the box cells are frozen */
        memcpy(d + (by + r) * FB_W + bx0, gm->msg_bg + r * bw, bw);
    vid_window(d, 0xFF, 0x0E, 0x1E, 0x34, 0x10);                        /* 73E2..73EA */
    if (g->font && g->font->loaded && g->message[0]) {
        /* vid_puts wants the original's 0xFF-terminated records, not a C string */
        uint8_t line[sizeof g->message + 1];
        size_t n = strlen(g->message);
        if (n > sizeof g->message) n = sizeof g->message;
        memcpy(line, g->message, n); line[n] = 0xFF;
        vid_puts(d, g->font, line, g->msg_x, 0x22);                     /* 73FF..7407 */
    }
    if (wide)
        for (int r = 0; r < bh; r++)
            memcpy(fb + (by + r) * v_w + bx, d + (by + r) * FB_W + bx0, bw);
}

/* gfmcga 4192 (the fade-in redraw, [4FFF] counting 8 -> 1; boss maps, 625B / 7395): pass `c` redraws, in every
 * background cell that is not a sprite or the hero's, the pixels of a 4 x 8 pattern: the left half's rows
 * 3-(c&3) and +4, the right half's rows (c&3) and +4, the odd columns for c <= 4 and the even ones above
 * (41F8..42C9, [4FFD]).  An empty cell (42A5) blacks the same pixels out, so the picture under it (the
 * encounter card) goes away pass by pass.  Eight passes make every pixel exactly once. */
void render_fadein_pass(uint8_t *fb, const Game *g, int c)
{
    int r1 = 3 - (c & 3), r2 = c & 3, par = (c - 1) < 4 ? 1 : 0;
    for (int sr = 0; sr < 18; sr++)                     /* 41B3: 0x12 cell rows */
        for (int sc = 0; sc < SCREEN_COLS; sc++) {
            if (sc >= g->hero_scr_col && sc < g->hero_scr_col + 3 && sr >= g->hero_scr_row && sr < g->hero_scr_row + 3) continue;
            uint8_t v = g->ring[game_ring_add(game_win(g), sr * RING_W + sc + 4)];   /* the ring as 4192 reads it (the frame's bg_ring is the last frame's) */
            if (v & 0x80) continue;
            const Cell8 *cell = (v && g->tiles->present[v]) ? &g->tiles->cell[v] : NULL;
            for (int half = 0; half < 2; half++) {
                int rr = half ? r2 : r1;
                for (int k = 0; k < 2; k++) {
                    int y = rr + 4 * k;
                    for (int x = half * 4; x < half * 4 + 4; x++) {
                        if ((x & 1) != par) continue;
                        fb[(PF_Y + sr * 8 + y) * FB_W + PF_X + sc * 8 + x] = cell ? cell->px[y][x] : 0;
                    }
                }
            }
        }
}

/* ---- spell 7 (gfmcga 42F7..4471): all writes go straight to A000, offset 0x11B0 + y*320 + x, so the playfield origin is (48,14) */
static void burst_put(uint8_t *fb, unsigned di, uint8_t v) { di &= 0xFFFF; if (di < (unsigned)(FB_W * FB_H)) fb[di] = v; }

static void burst_flash(uint8_t *fb)                    /* 447D: 8 x 18 x 0x70 words XOR 0x1212 = the 224 x 144 playfield */
{
    for (int y = 0; y < 18 * 8; y++)
        for (int x = 0; x < 224; x++) fb[(PF_Y + y) * FB_W + PF_X + x] ^= 0x12;
}

static void burst_hline(uint8_t *fb, uint8_t bl, uint8_t dl, uint8_t bh, uint8_t col)   /* 4404 */
{
    if (dl < bl) { uint8_t t = bl; bl = dl; dl = t; }
    if (bh == 0 || bh >= 0x8F) return;
    if (bl == 0) bl = 1;
    if (dl >= 0xDF) dl = 0xDE;
    unsigned di = 0x11B0u + (unsigned)bh * 320u + bl;
    for (unsigned n = (uint8_t)(dl - bl); n; n--, di++) burst_put(fb, di, col);
}

static void burst_vline(uint8_t *fb, uint8_t x, uint8_t bh, uint8_t dh, uint8_t col)   /* 43C3 */
{
    if (dh < bh) { uint8_t t = bh; bh = dh; dh = t; }
    if (x == 0 || x >= 0xDF) return;
    if (bh == 0) bh = 1;
    if (dh >= 0x8F) dh = 0x8E;
    unsigned di = 0x11B0u + (unsigned)bh * 320u + x;
    for (unsigned n = (uint8_t)(dh - bh + 1); n; n--, di += 320) burst_put(fb, di, col);
}

static void burst_rect(uint8_t *fb, uint8_t bl, uint8_t bh, uint8_t dl, uint8_t dh, uint8_t col)   /* 43A1 */
{
    burst_hline(fb, bl, dl, bh, col);
    burst_hline(fb, bl, dl, dh, col);
    burst_vline(fb, bl, bh, dh, col);
    burst_vline(fb, dl, bh, dh, col);
}

static void burst_set(uint8_t *fb, uint8_t hx, uint8_t hy, int back, uint8_t col, BurstWaitFn wait, void *user)   /* 4327 sets / 436F */
{
    /* the three sets: x-1 .. +0x19, x-5 .. +0x21, x-9 .. +0x29 (byte arithmetic, no clamp until the passes) */
    int span = back == 1 ? 0x19 : back == 5 ? 0x21 : 0x29;
    uint8_t bl = (uint8_t)(hx - back), bh = (uint8_t)(hy - back);
    uint8_t dl = (uint8_t)(bl + span), dh = (uint8_t)(bh + span);
    for (int pass = 0; pass < 9; pass++) {
        burst_rect(fb, bl, bh, dl, dh, col);
        bl = bl >= 12 ? (uint8_t)(bl - 12) : 0;
        bh = bh >= 12 ? (uint8_t)(bh - 12) : 0;
        dl = dl + 12 > 255 ? 255 : (uint8_t)(dl + 12);
        dh = dh + 12 > 255 ? 255 : (uint8_t)(dh + 12);
        if (wait) wait(user);                           /* 444A: [FF33]/2+1 ticks, exit 4471 */
    }
}

void render_burst(uint8_t *fb, const Game *g, BurstWaitFn wait, void *user)
{
    uint8_t hx = (uint8_t)(g->hero_scr_col * 8), hy = (uint8_t)(g->hero_scr_row * 8);   /* 42F7: [83] * 8, [84] * 8 */
    burst_flash(fb);                                    /* 4311 */
    for (int c = 0; c < 2; c++) {                       /* 4314: colour 0x36, then 0 (431C) */
        uint8_t col = c ? 0 : 0x36;
        burst_set(fb, hx, hy, 1, col, wait, user);
        burst_set(fb, hx, hy, 5, col, wait, user);
        burst_set(fb, hx, hy, 9, col, wait, user);
    }
    burst_flash(fb);                                    /* 4324: jmp 447D */
}

/* The stone frame and HUD panel for a framebuffer `w` wide (w = 320 + 8*(lo+hi), #126, docs/WIDESCREEN.md
 * 4.2): the original 48-px stone sides stay at the very edges, the strip above the playfield (224 px) is
 * tiled across the wider top, and the HUD panel (x 48..271, y >= 158) stays at its size, centred, its edge
 * columns repeated out to the stone.  The playfield rows between them are not furniture (`on` = 0). */
static struct { uint8_t px[RENDER_WIDE_MAX_W * FB_H], on[RENDER_WIDE_MAX_W * FB_H]; const struct ScreenFrame *src; int w; } wf;

void render_wide_frame(const struct ScreenFrame *sf, int w, const uint8_t **px, const uint8_t **on)
{
    if (wf.src != sf || wf.w != w) {
        wf.src = sf; wf.w = w;
        memset(wf.px, 0, sizeof wf.px); memset(wf.on, 0, sizeof wf.on);
        if (sf && sf->loaded && w >= FB_W && w <= RENDER_WIDE_MAX_W) {
            const int sx0 = 48, sw = 224, ho = (w - FB_W) / 2;
            for (int y = 0; y < FB_H; y++)
                for (int x = 0; x < w; x++) {
                    int sxp;
                    if (x < sx0) sxp = x;                                   /* left stone */
                    else if (x >= w - sx0) sxp = x - (w - FB_W);            /* right stone */
                    else if (y < PF_Y) sxp = sx0 + ((x - sx0 - ho) % sw + sw) % sw;   /* the strip above, tiled in phase with the centred copy */
                    else if (y >= 158) {                                    /* the HUD panel, centred */
                        sxp = x - ho;
                        if (sxp < sx0) sxp = sx0;
                        if (sxp >= sx0 + sw) sxp = sx0 + sw - 1;
                    } else continue;                                        /* the playfield */
                    if (!sf->on[y * FB_W + sxp]) continue;
                    wf.on[y * w + x] = 1; wf.px[y * w + x] = sf->px[y * FB_W + sxp];
                }
        }
    }
    *px = wf.px; *on = wf.on;
}

/* The 320-wide screen's centre pieces (x 48..271) into a wide framebuffer, centred: the HUD band that render_hud
 * and itemp_hud draw (rows 158..199) and the strip above the playfield (rows 0..12) where the Tear icons sit. */
void render_wide_hud(uint8_t *wfb, int w, const uint8_t *fb320)
{
    const int ho = (w - FB_W) / 2;
    for (int y = 0; y < FB_H; y++) {
        if (y >= 13 && y < 158) continue;
        memcpy(wfb + y * w + 48 + ho, fb320 + y * FB_W + 48, 224);
    }
}

static void render_frame_impl(uint8_t *fb, const Game *g, const HeroGfx *h)
{
    /* mole.bin's furniture is painted once at boot and never redrawn (gfx.h);
     * the original's VGA keeps it, so a draw on it (select.bin's stray magic
     * name, #103) survives into later frames.  The port keeps it in the
     * persistent fb too: clear only what the furniture does not cover and
     * paint the furniture only where the fb is still blank.  A wide view has the
     * furniture laid out for its width (wide_frame, below). */
    {
        const struct ScreenFrame *sf = g->screen;
        const uint8_t *fpx = NULL, *fon = NULL;
        if (v_w != FB_W) { if (sf && sf->loaded) render_wide_frame(sf, v_w, &fpx, &fon); }
        else if (sf && sf->loaded) { fpx = sf->px; fon = sf->on; }
        for (int i = 0; i < v_w * FB_H; i++) {
            if (!fon || !fon[i]) fb[i] = 0;
            else if (!fb[i]) fb[i] = fpx[i];
        }
    }
    if (g->walk_in) {                                   /* 7C6E: the walk-in cutscene */
        /* gfmcga [301E] (7BBF / 7BF5 / 7A71) paints the cave-mouth picture over
         * the whole playfield, remapped by [4FFF] (the door letter & 7; 0 from
         * a town).  Each loop pass (7C80..7CA6) draws the hero's walk frame at
         * the next 8-pixel step (3020), waits a frame (7F82: this is the picture
         * on screen) and then blanks the 8 x 24 strip he just left (2000, al 0),
         * so a black trail stays on the picture behind him. */
        if (g->mouth && g->mouth->loaded) {
            unsigned var = g->walk_variant;
            for (int y = 0; y < MOUTH_H; y++)
                for (int x = 0; x < MOUTH_W; x++)
                    fb[(PF_Y + y) * FB_W + PF_X + x] = gfx_mouth_remap(var, g->mouth->px[y * MOUTH_W + x]);
        }
        int left = (g->hero_flags & FACE_LEFT) != 0;
        int step = g->walk_in_dir, k = 0x1A - g->walk_in;
        for (int i = 0; i < k; i++) {                   /* 2000 at the 8-px strip behind pass i's hero */
            int hx = g->walk_in_x + step - (k - i) * step + (left ? 16 : 0);
            for (int y = 110; y < 134; y++)
                for (int x = hx; x < hx + 8; x++)
                    if (x >= 0 && x < FB_W) fb[y * FB_W + x] = 0;
        }
        if (!h) return;
        /* 3016 composes the hero as the frame loop does (shield and sword arm included) with [FF3A] = 0
         * (7C02), at the 8-pixel step 3020 blits it to: 7C8C adds 2 to bh first */
        draw_hero_at(fb, g, h, g->walk_in_x + step, 110, 0);
        return;
    }
    if (g->encounter_frames || g->fade_card) {          /* 60E6: the encounter card (fade_card: the card as the fade-in starts over it) */
        /* 60BC draws the card, then six {wait, clear, wait, draw} pairs, so
         * the twelve half-flashes start with the card up. */
        if (!(g->encounter_frames & 1)) {
            if (g->encnt && g->encnt->loaded)
                for (int y = 0; y < ENCNT_H; y++)
                    memcpy(fb + (40 + y) * FB_W + 48, g->encnt->px + y * ENCNT_W, ENCNT_W);
            else render_window(fb, 48, 40, 224, 40, 1);
        }
        if (h) draw_hero(fb, g, h);                     /* the hero stands under the card (oracle side pictures w60EF / w6108, #119) */
        return;
    }
    /* Smooth scrolling draws the tiles at a pixel offset and so needs the ring
     * cell just beyond the window on the side being revealed (the ring is 36
     * wide: 4 spare columns each side, and its rows wrap) */
    int wx = s_sm ? s_sm->wx : 0, wy = s_sm ? s_sm->wy : 0;
    int sc0 = -v_lo + (wx > 0 ? -((wx + 7) / 8) : 0), sc1 = SCREEN_COLS + v_hi + (wx < 0 ? (-wx + 7) / 8 : 0);
    int sr0 = wy > 0 ? -((wy + 7) / 8) : 0, sr1 = SCREEN_ROWS;
    for (int sr = sr0; sr < sr1; sr++) {
        for (int sc = sc0; sc < sc1; sc++) {
            uint8_t v;
            if (sc + 4 < 0 || sc + 4 >= RING_W) {
                /* A slide in a view that already shows the whole ring reveals the column just beyond it.  The
                 * ring does not hold it (a ring index there is the neighbouring row's), so it is read from the
                 * map itself: the ring is the map's columns scroll_col.. wrapped, row for row. */
                int mw = g->map->width, mc = ((g->scroll_col + sc + 4) % mw + mw) % mw;
                v = g->map->grid[mc][(g->scroll_row + sr) & 0x3F];
                if (v & 0x80) continue;
            } else {
            int rp = (game_win(g) + sr * RING_W + sc + 4) % RING_SIZE;
            if (rp < 0) rp += RING_SIZE;
            v = g->bg_ring[rp];                         /* 3159: the cell before 315C animates it (game_bg_cell, wrapped both ways) */
            if (v & 0x80) {                             /* a sprite marker hides the tile it covers;
                                                         * the background buffer (E900) keeps it */
                int i = v & 0x7F;
                v = i < g->nobj ? g->under_sprite[i] : 0;
                if (v & 0x80) continue;
            }
            }
            if (!v || !g->tiles->present[v]) continue;
            blit8(fb, &g->tiles->cell[v], VX(sc) + wx, PF_Y + sr * 8 + wy);
        }
    }
    /* fight.bin's frame order: the hero (gfmcga 39B9/40F0) is drawn at 70D4,
     * the enemies in 302C's row pass at 710F, then the death sparks (gfmcga
     * 379B) at 711B, the projectiles (8366) at 713B, the spell sprites (896E) at
     * 7141 and the blade (gfmcga 3FD0) at 7147.  The port draws the enemies
     * first and the hero over them (the original's 747C marks the hero's 3x3
     * cells 0xFF so 302C's 3124 skips them; the port has no screen copy, so it
     * emulates that by drawing the hero later).  The hero is *before* the
     * sparks, matching 70D4 < 711B, and before the blade's save block below.
     * The shots land over the hero (8366 only skips 0xFF/0xFC), so they are
     * drawn after it (#104). */
    if (g->mid_pic) draw_shots_mid(fb, g);      /* 713B of the previous frame, under everything this frame draws */
    draw_enemies(fb, g);
    if (h) draw_hero(fb, g, h);                 /* 70D4 */
    /* 3F42 saves the blade's 32x32 block before 302C's row pass draws the blade
     * into it; 7136's GF_ERASE_SPRITES restores that block (erasing a spark that
     * landed on it), then 7147 redraws the blade.  Save here, restore below. */
    int sx0 = 0, sy0 = 0, have_sword = 0;
    static uint8_t sword_bg[32 * 32];
    /* The 7047 save runs *before* the hero is drawn (70D4), so [4FED] still
     * holds the previous frame's hero screen cells: the block is at the
     * previous frame's hero position, while the 7147 blade uses the current
     * one.  (vista_fight f99/f102 differ by exactly this cell.) */
    int shr = g->sword_blk_ok ? g->sword_blk_row : g->hero_scr_row;
    int shc = g->sword_blk_ok ? g->sword_blk_col : g->hero_scr_col;
    if (g->mid_pic) {
        /* The picture at 712D, the half-way wait (sword-frame): 7047 drew the swing's first step, frame [FF46]-1 at the
         * hero's previous cell, and 302C's row pass (710F) drew it again with the hero; the sparks (711B) lie over it,
         * and 7136's erase, the shots (7138), the spell sprites (7141) and 7147's second step are all still to come. */
        if (h) draw_sword_at(fb, g, h, g->attack_var - 1, shr, shc);
        draw_sparks(fb, g, h);
        draw_message_box(fb, g);
        return;
    }
    if (h && (have_sword = sword_origin_at(g, h, g->attack_var - 1, shr, shc, &sx0, &sy0))) {
        for (int r = 0; r < 32; r++) {
            int yy = sy0 + r;
            if (yy < 0 || yy >= FB_H) { memset(sword_bg + r * 32, 0, 32); continue; }
            for (int k = 0; k < 32; k++) {
                int xx = sx0 + k;
                sword_bg[r * 32 + k] = (xx >= 0 && xx < v_w) ? fb[yy * v_w + xx] : 0;
            }
        }
    }
    draw_sparks(fb, g, h);
    if (have_sword) {                           /* 7136 GF_ERASE_SPRITES: restore the block */
        for (int r = 0; r < 32; r++) {
            int yy = sy0 + r;
            if (yy < 0 || yy >= FB_H) continue;
            for (int k = 0; k < 32; k++) {
                int xx = sx0 + k;
                if (xx >= 0 && xx < v_w) fb[yy * v_w + xx] = sword_bg[r * 32 + k];
            }
        }
    }
    draw_shots(fb, g);                          /* 713B */
    draw_magic(fb, g);                          /* 7141 */
    if (h) draw_sword(fb, g, h);                /* 7147 */
    draw_message_box(fb, g);
    if (g->dissolve) render_dissolve(fb, g->dissolve);   /* 99A8: the death dissolve (#118) */
}

int render_wide_ok(const Game *g)
{
    return !g->boss_map && !g->walk_in && !g->encounter_frames && !g->fade_card && !g->dissolve &&
           !g->dsv_pic && !g->status && !g->tear && !g->boss_dying;
}

int render_frame_view(uint8_t *fb, const Game *g, const HeroGfx *h, const RenderView *v)
{
    int lo = v ? v->lo : 0, hi = v ? v->hi : 0;
    if (lo < 0 || hi < 0 || lo > 4 || hi > 4) return 0;
    if ((lo || hi) && !render_wide_ok(g)) return 0;
    v_lo = lo; v_hi = hi; v_w = FB_W + 8 * (lo + hi);
    render_frame_impl(fb, g, h);
    v_lo = v_hi = 0; v_w = FB_W;
    return 1;
}

void render_frame(uint8_t *fb, const Game *g, const HeroGfx *h) { render_frame_impl(fb, g, h); }

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

/* town.bin 6C72 / fight.bin 6C33: the four positioned narrow-font labels, and
 * the ENEMY label fight.bin 6C8F puts where PLACE goes in a boss room.  The
 * bytes are the records' own ({x4, y, xoff, len, chars} — town.bin 6C93..6CB4,
 * fight.bin 6C44/6C4C/6C8F); [200E] draws them green on a red shadow. */
static const uint8_t HUD_LIFE[]  = {0x0E, 0xA3, 0x00, 4, 'L','I','F','E'};
static const uint8_t HUD_ALMAS[] = {0x1E, 0xBB, 0x03, 5, 'A','L','M','A','S'};
static const uint8_t HUD_GOLD[]  = {0x0D, 0xBB, 0x01, 4, 'G','O','L','D'};
static const uint8_t HUD_PLACE[] = {0x0D, 0xAF, 0x01, 5, 'P','L','A','C','E'};
static const uint8_t HUD_ENEMY[] = {0x0D, 0xAF, 0x02, 5, 'E','N','E','M','Y'};

static void hud_label(uint8_t *fb, const struct TextFont *tf, const uint8_t *r)
{   /* [200E] vid_label_hud: PC-88 green fg, red shadow */
    vid_label_narrow(fb, tf, (const char *)r + 4, r[3], r[0], r[1], r[2], 3, 2);
}

/* [2004] vid_gauge_bar (MCGA 2195): `w` one-pixel columns starting one column
 * right of 48 + bh, ten rows from 158 + bl — row 0 black, rows 1..8 the dark
 * blue trough and row 9 the bright blue lip.  The four HUD boxes are
 *   LIFE  (town 60CF: bh 02 bl 04 w 21)   PLACE/ENEMY ([2012]: 02 / 10 / 88)
 *   GOLD  (town 60DB / fight 6C7B: 02 1C 42)   ALMAS (town 60E7: 48 1C 42)
 * fight.bin only redraws the ENEMY/GOLD ones — the other two survive from the
 * town, because nothing ever clears the HUD strip. */
void vid_gauge_bar(uint8_t *fb, int bh, int bl, int w)
{
    int x0 = 48 + bh + 1, y0 = 158 + bl;
    /* "the column before x is cleared first" (docs/VIDEO_DRIVERS.md [2004]):
     * the trough is drawn on the grey panel mole.bin painted, so the blank
     * column to its left has to be blacked out or the panel shows through. */
    for (int r = 0; r <= 9; r++)
        if (x0 - 1 >= 0 && y0 + r < FB_H) fb[(y0 + r) * FB_W + x0 - 1] = 0x00;
    for (int x = x0; x < x0 + w && x < FB_W; x++) {
        if (y0 >= 0 && y0 < FB_H) fb[y0 * FB_W + x] = 0x00;
        for (int r = 1; r <= 8; r++) if (y0 + r < FB_H) fb[(y0 + r) * FB_W + x] = 0x05;
        if (y0 + 9 < FB_H) fb[(y0 + 9) * FB_W + x] = 0x2D;
    }
}

void render_hud(uint8_t *fb, const Game *g, const DigitFont *font,
                const struct TextFont *tf, const uint8_t *place)
{
    /* the walk-in (7C6E) runs before the loop restart whose 6185 / 6C55 draws the new map's PLACE
     * (or a boss room's ENEMY gauge): the HUD still shows the map just left (#117) */
    if (g->walk_in || g->tear) place = g->walk_place;     /* the Tear scene too: the HUD is the map the door was taken in (#119) */
    for (int y = HUD_Y; y < FB_H; y++) memset(fb + y * FB_W, 0, FB_W);
    render_screen_frame(fb, g->screen, HUD_Y, FB_H);
    vid_gauge_bar(fb, 0x02, 0x04, 0x21);            /* LIFE */
    vid_gauge_bar(fb, 0x02, 0x10, 0x88);            /* PLACE / ENEMY */
    vid_gauge_bar(fb, 0x02, 0x1C, 0x42);            /* GOLD */
    vid_gauge_bar(fb, 0x48, 0x1C, 0x42);            /* ALMAS */
    /* [E6]-only MP90 (FF34 = 0) takes 6179's ordinary HUD: PLACE, GOLD (jashiin_fight f27) */
    int boss = g->boss_map && g->boss.active && !g->walk_in;
    if (tf) hud_label(fb, tf, HUD_LIFE);
    if (boss) {
        /* fight.bin 6C55, on a boss-room entry (6082/6237) in place of [2012]:
         * the ENEMY trough is only 0x21 columns (bx 0x0210 — the 0x88 one under
         * it is the same up to x83), 6C61 [2004] style 0x80 bx 0x2310 ch 0x67
         * clears x84..186 of its ten rows to black (2215: the first, ax=0
         * column at x83 is and-ed with FF, untouched), and 6C76 [202C] (MCGA
         * 296F: BX is the source, DX the destination) copies x80..191 rows
         * 169..171 — the LIFE line's bottom, its scale ticks — to rows
         * 181..183.  Nothing else draws there in the fight: [200A]/[200C]
         * cover rows 175..180 only. */
        for (int y = 174; y <= 183; y++) memset(fb + y * FB_W + 84, 0, 0x67);
        for (int r = 0; r < 3; r++)
            memcpy(fb + (181 + r) * FB_W + 80, fb + (169 + r) * FB_W + 80, 14 * 8);
    }
    unsigned wmax = g->max_hp / 8, wcur = g->hp / 8;
    if (wmax > 100) wmax = 100;
    if (wcur > wmax) wcur = wmax;
    for (unsigned x = 0; x < wmax; x++) {
        for (int r = 0; r < 6; r++) fb[(163 + r) * FB_W + 84 + x] = 0x12;   /* red */
        if (x < wcur) for (int r = 0; r < 5; r++) fb[(163 + r) * FB_W + 84 + x] = 0x1B;  /* green */
    }
    /* docs/VIDEO_DRIVERS.md [2012]/[200A]/[200C]: in a boss room the ENEMY
     * line carries the boss HP — the same red/white pair at (84,175) on the
     * black 6C55 left, [A002]+3 full scale. */
    if (boss && g->boss.hp0 && !g->encounter_frames) {          /* the gauge comes after the card (6150) */
        unsigned bmax = g->boss.hp0 / 8, bcur = g->boss.hp / 8;
        if (bmax > 100) bmax = 100;
        if (bcur > bmax) bcur = bmax;
        for (unsigned x = 0; x < bmax; x++) {
            for (int r = 0; r < 6; r++) fb[(175 + r) * FB_W + 84 + x] = 0x12;
            if (x < bcur) for (int r = 0; r < 5; r++) fb[(175 + r) * FB_W + 84 + x] = 0x1B;
        }
    }
    /* 6C55/6150: a boss room has no GOLD line — the boss's [A002]+9 name record
     * sits in the GOLD box (its own y is 0xBB) and the PLACE label becomes
     * ENEMY (6C8F).  ALMAS and the LIFE bar are the same either way. */
    if (!boss) hud_digits(fb, font, (unsigned)g->gold, 76, 187, 6);
    hud_digits(fb, font, g->almas, 152, 187, 5);
    if (tf) {
        hud_label(fb, tf, HUD_ALMAS);
        if (!boss) hud_label(fb, tf, HUD_GOLD);
        hud_label(fb, tf, boss ? HUD_ENEMY : HUD_PLACE);
        /* [2010] vid_label_text: white on a blue shadow */
        if (boss && g->boss.name[0] && !g->encounter_frames)
            vid_label_narrow(fb, tf, g->boss.name, (int)strlen(g->boss.name),
                             g->boss.name_x4, g->boss.name_y, g->boss.name_xoff, 1, 5);
        else if (place && !(boss && g->encounter_frames))   /* the name box is still blank under the card (side w60EF, #119) */
            vid_label_narrow(fb, tf, (const char *)place + 4, place[3],
                             place[0], place[1], place[2], 1, 5);
    }
}

void render_to_rgb(const uint8_t *fb, uint8_t *rgb)
{
    for (int i = 0; i < FB_W * FB_H; i++) {
        const uint8_t *p = PAL_RGB[fb[i] & 63];
        rgb[i * 3] = p[0]; rgb[i * 3 + 1] = p[1]; rgb[i * 3 + 2] = p[2];
    }
}

/* ---- gmmcga 2130 vid_dissolve_playfield, one pass ---- */
void render_dissolve_pass(uint8_t *fb, int pass)
{
    if (video_current() != VID_MCGA) { video_dissolve_pass(video_current(), fb, pass); return; }   /* the driver's own 2124 / 214E / 2143 */
    static const uint8_t masks[8] = { 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF };   /* 218D */
    unsigned al = masks[pass & 7];
    uint8_t *p = fb + 0x11B0;                           /* 213C: row 14, x 48 */
    for (int r = 0; r < 72; r++) {                      /* 2144: rows 14, 16, .. 156 */
        for (int x = 0; x < 224; x++, p++) {
            al = ((al << 1) | (al >> 7)) & 0xFF;        /* 2148 rol al,1 */
            if (al & 1) *p = 0;                         /* the carry: bit 7 before the rotate = bit 0 after */
        }
        al = ((al >> 3) | (al << 5)) & 0xFF;            /* 2153 ror al,1 x3 */
        p += 0x1A0;                                     /* 215A */
    }
    p = fb + 0x11B0 + 0x140;                            /* 2161: row 15 */
    for (int r = 0; r < 72; r++) {                      /* 2168: rows 15, 17, .. 157 */
        for (int x = 0; x < 224; x++, p++) {
            al = ((al >> 1) | (al << 7)) & 0xFF;        /* 216C ror al,1 */
            if (al & 0x80) *p = 0;                      /* the carry: bit 0 before the rotate = bit 7 after */
        }
        al = ((al << 3) | (al >> 5)) & 0xFF;            /* 2177 rol al,1 x3 */
        p += 0x1A0;                                     /* 217E */
    }
}

void render_dissolve(uint8_t *fb, int passes)
{
    for (int i = 0; i < passes && i < 8; i++) render_dissolve_pass(fb, i);
}
