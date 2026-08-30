#include "render.h"
#include "gfx.h"
#include "text.h"
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

static void blit2(uint8_t *fb, const Cell2 *c, int x, int y)
{
    for (int r = 0; r < 8; r++) {
        int yy = y + r;
        if (yy < PF_Y || yy >= PF_Y + PF_H) continue;
        for (int k = 0; k < 8; k++) {
            if (!c->mask[r][k]) continue;
            int xx = x + k;
            if (xx < PF_X || xx >= PF_X + PF_W) continue;
            fb[yy * FB_W + xx] = c->px[r][k];
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
static void draw_hero_frame(uint8_t *fb, const HeroGfx *h, int fr, int count, int slot0, int hx, int hy)
{
    if (fr < 0 || fr >= HERO_FRAMES) return;
    for (int i = 0; i < count; i++) {
        int cell = h->frame[fr][i];                     /* 3CDD/3CE0: 0 = empty slot */
        if (!cell || cell >= h->ncells) continue;
        int slot = slot0 + i;
        blit2(fb, &h->cell[cell], hx + (slot % 3) * 8, hy + (slot / 3) * 8);
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
        if (hero_anim_flag(g)) {                        /* 3ACF: the attack/cast overlays */
            /* facing right the shield arm is the far one and goes behind
             * (3ADA); facing left it is the sword arm (3AF4). */
            int fr = left ? hero_blade_frame(g, 49) : hero_guard_frame(g, 79);
            draw_hero_frame(fb, h, fr, crouch_n, crouch_s, hx, hy);
        } else if (sh && !left) {                       /* 3B18..3B3F: shield behind when facing right */
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
    if (hero_anim_flag(g))                                                          /* 3C36 */
        fr = left ? hero_guard_frame(g, 67) : hero_blade_frame(g, 31);
    else if (left && sh) fr = ovl + 12 + (g->crouching ? 1 : 0) + (sh - 1 ? 3 : 0); /* 3C86 */
    else if (g->crouching || g->hero_anim == 0x80) fr = ovl + 3;                    /* 3CA5 */
    else fr = ovl + (g->hero_anim & 3);
    draw_hero_frame(fb, h, fr, crouch_n, crouch_s, hx, hy);
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
static void draw_sword(uint8_t *fb, const Game *g, const HeroGfx *h)
{
    if (!g->attacking || g->hero_hidden || g->hero_dead) return;         /* 3FD0: test [FF43] */
    const SwordGfx *sg = &h->sword;
    if (!sg->loaded || !g->sword) return;                                /* [92] = 0: no blade */
    int sw = g->sword > 6 ? 6 : g->sword;
    int sec = SWORD_SECTION[sw - 1];                                     /* kernel mode 4 */
    int kind = g->attack_type <= 2 ? g->attack_type : 0;                 /* [FF45] */
    int grp = kind + ((g->hero_flags & FACE_LEFT) ? 3 : 0);              /* 3E6C: test [C2],1 */
    int f = g->attack_var;                                               /* [FF46] - 1 */
    if (f >= SWORD_FRAMES[kind]) f = SWORD_FRAMES[kind] - 1;
    if (f < 0) f = 0;
    const uint8_t *blk = sg->block[sec][grp][f];
    int x0 = PF_X + g->hero_scr_col * 8 + sg->delta[sec][grp][f][1] * 8;
    int y0 = PF_Y + (g->hero_scr_row + sg->delta[sec][grp][f][0] + (g->crouching ? 1 : 0)) * 8;
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
                    if (xx < PF_X || xx >= PF_X + PF_W) continue;
                    fb[yy * FB_W + xx] = v == 3 ? c3 : c12;               /* 40AB / 40CB */
                }
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

/* video slot [0x2000] vid_window — text.c owns the implementation; this
 * wrapper keeps the pixel arguments the encounter card is written with. */
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

void render_frame(uint8_t *fb, const Game *g, const HeroGfx *h)
{
    memset(fb, 0, FB_W * FB_H);
    render_screen_frame(fb, g->screen, 0, FB_H);
    if (g->encounter_frames) {                          /* 60E6: the encounter card */
        /* 60BC draws the card, then six {wait, clear, wait, draw} pairs, so
         * the twelve half-flashes start with the card up. */
        if (!(g->encounter_frames & 1)) {
            if (g->encnt && g->encnt->loaded)
                for (int y = 0; y < ENCNT_H; y++)
                    memcpy(fb + (40 + y) * FB_W + 48, g->encnt->px + y * ENCNT_W, ENCNT_W);
            else render_window(fb, 48, 40, 224, 40, 1);
        }
        return;
    }
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
    if (h) { draw_hero(fb, g, h); draw_sword(fb, g, h); }
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
static void vid_gauge_bar(uint8_t *fb, int bh, int bl, int w)
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
    for (int y = HUD_Y; y < FB_H; y++) memset(fb + y * FB_W, 0, FB_W);
    render_screen_frame(fb, g->screen, HUD_Y, FB_H);
    vid_gauge_bar(fb, 0x02, 0x04, 0x21);            /* LIFE */
    vid_gauge_bar(fb, 0x02, 0x10, 0x88);            /* PLACE / ENEMY */
    vid_gauge_bar(fb, 0x02, 0x1C, 0x42);            /* GOLD */
    vid_gauge_bar(fb, 0x48, 0x1C, 0x42);            /* ALMAS */
    unsigned wmax = g->max_hp / 8, wcur = g->hp / 8;
    if (wmax > 100) wmax = 100;
    if (wcur > wmax) wcur = wmax;
    for (unsigned x = 0; x < wmax; x++) {
        for (int r = 0; r < 6; r++) fb[(163 + r) * FB_W + 84 + x] = 0x12;   /* red */
        if (x < wcur) for (int r = 0; r < 5; r++) fb[(163 + r) * FB_W + 84 + x] = 0x1B;  /* green */
    }
    /* docs/VIDEO_DRIVERS.md [2012]/[200A]/[200C]: in a boss room the ENEMY
     * line carries the boss HP — a blue trough at (50,174) 136 px wide with
     * the same red/white pair at (84,175), [A002]+3 full scale. */
    if ((g->boss_map || g->boss_room) && g->boss.active && g->boss.hp0) {
        unsigned bmax = g->boss.hp0 / 8, bcur = g->boss.hp / 8;
        if (bmax > 100) bmax = 100;
        if (bcur > bmax) bcur = bmax;
        for (unsigned x = 0; x < bmax; x++) {
            for (int r = 0; r < 6; r++) fb[(175 + r) * FB_W + 84 + x] = 0x12;
            if (x < bcur) for (int r = 0; r < 5; r++) fb[(175 + r) * FB_W + 84 + x] = 0x1B;
        }
    }
    int boss = (g->boss_map || g->boss_room) && g->boss.active;
    /* 6C55/6150: a boss room has no GOLD line — the boss's [A002]+9 name record
     * sits in the GOLD box (its own y is 0xBB) and the PLACE label becomes
     * ENEMY (6C8F).  ALMAS and the LIFE bar are the same either way. */
    if (!boss) hud_digits(fb, font, (unsigned)g->gold, 76, 187, 6);
    hud_digits(fb, font, g->almas, 152, 187, 5);
    if (tf) {
        hud_label(fb, tf, HUD_LIFE);
        hud_label(fb, tf, HUD_ALMAS);
        if (!boss) hud_label(fb, tf, HUD_GOLD);
        hud_label(fb, tf, boss ? HUD_ENEMY : HUD_PLACE);
        /* [2010] vid_label_text: white on a blue shadow */
        if (boss && g->boss.name[0])
            vid_label_narrow(fb, tf, g->boss.name, (int)strlen(g->boss.name),
                             g->boss.name_x4, g->boss.name_y, g->boss.name_xoff, 1, 5);
        else if (place)
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
