/* tear.c — ROKADEMO.BIN (ZELRES3[0], image A000..A5A8): "a Tear of Esmesanti"
 * (docs/CUTSCENES.md §5, src/rokademo.c), plus GAME.BIN's Tear-slot HUD row
 * (A3A5).  Hex tags are addresses in the overlay the line comes from.
 *
 * Every piece of art the scene needs is read out of the original images at run
 * time: the hero cells from dman.grp (ZELRES3[53]) converted the way gfmcga's
 * [3028] converts them, the raised sword and the sparkle frames from *inside*
 * gfmcga.bin (ZELRES2[6]), the crystal icon from inside GMMCGA.BIN and the
 * nine slot positions from GAME.BIN. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tear.h"
#include "sar.h"
#include "gfx.h"
#include "render.h"
#include "shell.h"
#include "enemy.h"
#include "audio.h"

#define GF(a)  ((unsigned)(a) - 0x3000)          /* gfmcga.bin is parked at 3000 */

/* --------------------------------------------------------------- loading */

/* gfmcga 4B51 / 4990 read 2-bpp art out of their own image: two big-endian
 * plane words per row, MSB = leftmost pixel, value 0 transparent. */
static void load_2bpp(const uint8_t *src, int w, int rows, uint8_t *out, int stride)
{
    int words = w / 16;
    for (int r = 0; r < rows; r++)
        for (int k = 0; k < words; k++) {
            unsigned a = ((unsigned)src[0] << 8) | src[1];
            unsigned b = ((unsigned)src[2] << 8) | src[3];
            src += 4;
            for (int i = 0; i < 16; i++)
                out[r * stride + k * 16 + i] =
                    (uint8_t)((((b >> (15 - i)) & 1) << 1) | ((a >> (15 - i)) & 1));
        }
}

int tear_art_load(TearArt *a, const char *dir)
{
    memset(a, 0, sizeof *a);
    size_t len = 0;

    /* dman.grp (ZELRES3[53]) -> 54 cells, converted exactly as [3028] does */
    uint8_t *dman = sar_load(dir, 2, 53, 1, &len);                       /* A014 */
    if (dman) {
        a->ncells = (int)(len / 32);
        if (a->ncells > 54) a->ncells = 54;
        for (int i = 0; i < a->ncells; i++) {
            Cell2 c;
            gfx_decode32(dman + (size_t)i * 32, PAL2BPP[0], &c);         /* A026 */
            memcpy(a->cell[i], c.px, 64);
        }
        free(dman);
    }

    /* gfmcga.bin (ZELRES2[6]): the sword pictures and the sparkle frames */
    uint8_t *gf = sar_load(dir, 1, 6, 1, &len);
    if (gf && len > GF(0x4E00)) {
        for (int i = 0; i < 6; i++) {                                    /* 4A25 */
            unsigned p = (unsigned)gf[GF(0x4A25) + i * 2] | ((unsigned)gf[GF(0x4A25) + i * 2 + 1] << 8);
            a->sword_tbl[i] = (uint8_t)(p == 0x4A31 ? 0 : p == 0x4A91 ? 1 : 2);
        }
        for (int i = 0; i < 3; i++)
            load_2bpp(gf + GF(0x4A31) + (size_t)i * 0x60, 16, 24, &a->sword[i][0][0], 16);
        for (int f = 0; f < 4; f++)                                      /* 4BDD */
            load_2bpp(gf + GF(0x4BDD) + (size_t)f * 0x40, 16, 16, &a->spark[f][0][0], 16);
        for (int f = 0; f < 2; f++)                                      /* 4CDD */
            for (int blk = 0; blk < 4; blk++)
                load_2bpp(gf + GF(0x4CDD) + (size_t)f * 0x100 + (size_t)blk * 0x40, 16, 16,
                          &a->bigspark[f][0][blk * 16], 64);
    }
    free(gf);

    /* GMMCGA.BIN @2A5D: two 16x13 byte-per-pixel icons, 0x80 = transparent */
    char path[512];
    snprintf(path, sizeof path, "%s/GMMCGA.BIN", dir);
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(path, sizeof path, "%s/gmmcga.bin", dir); f = fopen(path, "rb"); }
    if (f) {
        for (int i = 0; i < 2; i++) {
            long off = (i ? 0x2B31 : 0x2A61) - 0x2000;
            if (fseek(f, off, SEEK_SET) == 0)
                if (fread(a->icon[i], 1, 208, f) != 208) memset(a->icon[i], 0x80, 208);
        }
        fclose(f);
    } else memset(a->icon, 0x80, sizeof a->icon);

    /* rokademo A435: ten column-major 3x3 frame maps (src/rokademo.c lists
     * only nine; A435 + 10*9 = A48F, which is wait_frame's first instruction) */
    uint8_t *rk = sar_load(dir, 2, 0, 1, &len);
    if (rk && len >= 0xA48F - 0xA000) memcpy(a->frame, rk + (0xA435 - 0xA000), 90);
    free(rk);

    /* GAME.BIN A3D3: nine {u8 x4, u8 y=0} slot positions */
    snprintf(path, sizeof path, "%s/GAME.BIN", dir);
    f = fopen(path, "rb");
    if (!f) { snprintf(path, sizeof path, "%s/game.bin", dir); f = fopen(path, "rb"); }
    if (f) {
        uint8_t buf[18];
        if (fseek(f, 0xA3D3 - 0xA000, SEEK_SET) == 0 && fread(buf, 1, 18, f) == 18)
            for (int i = 0; i < 9; i++) a->slot_x4[i] = buf[i * 2 + 1];   /* BH of the word */
        fclose(f);
    }
    a->loaded = 1;
    return 0;
}

/* ------------------------------------------------------------- primitives */

static inline void px(uint8_t *fb, int x, int y, uint8_t v)
{ if (x >= 0 && x < 320 && y >= 0 && y < 200) fb[y * 320 + x] = v; }

/* [3022] 4933 — one 8x8 cell at (x4*4, y).  The blit is fully opaque: gfmcga
 * writes all 64 bytes with plain stosb, which is what erases the trailing
 * column behind the walking hero. */
static void blit_cell(Tear *t, int cell, int x4, int y)
{
    if (cell < 0 || cell >= t->art->ncells) return;
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++) px(t->fb, x4 * 4 + c, y + r, t->art->cell[cell][r][c]);
}

/* A407 — hero frame [E7] as a 3x3 block; the maps are COLUMN-major (the inner
 * loop steps y, the outer steps x), unlike fman.grp's row-major ones. */
static void draw_hero(Tear *t, int frame, int x4, int y)
{
    const uint8_t *m = t->art->frame[frame % 10];
    for (int col = 0; col < 3; col++)
        for (int row = 0; row < 3; row++) blit_cell(t, *m++, x4 + col * 2, y + row * 8);
}

/* the 2-bpp colour rule 4092 uses with [4FF5] = 0x0908: 0 transparent,
 * 1 and 2 -> DAC 0x08 (grey), 3 -> DAC 0x09 (white) */
static inline uint8_t twobpp(uint8_t v) { return (uint8_t)(v == 3 ? 0x09 : 0x08); }

/* [3024] 4990 — the raised sword, 16x24 at (144,86), picture chosen by [92] */
static void draw_sword(Tear *t)
{
    int sw = t->g->sword ? t->g->sword - 1 : 0;
    if (sw > 5) sw = 5;
    int pic = t->art->sword_tbl[sw];
    for (int r = 0; r < 24; r++)
        for (int c = 0; c < 16; c++) {
            uint8_t v = t->art->sword[pic][r][c];
            if (v) px(t->fb, 144 + c, 86 + r, twobpp(v));
        }
}

/* [3026] 4B51 — AL bit 7 selects the 64x16 "big" frames, otherwise 16x16 */
static void draw_spark(Tear *t, int al, int x, int y)
{
    if (al & 0x80) {
        int f = al & 1;
        for (int r = 0; r < 16; r++)
            for (int c = 0; c < 64; c++) {
                uint8_t v = t->art->bigspark[f][r][c];
                if (v) px(t->fb, x + c, y + r, twobpp(v));
            }
    } else {
        int f = al & 3;
        for (int r = 0; r < 16; r++)
            for (int c = 0; c < 16; c++) {
                uint8_t v = t->art->spark[f][r][c];
                if (v) px(t->fb, x + c, y + r, twobpp(v));
            }
    }
}

/* [203E] GMMCGA 2A1C — a 16x13 icon at (x4*4, y); 0x80 is transparent */
static void draw_icon(Tear *t, int which, int x4, int y)
{
    for (int r = 0; r < 13; r++)
        for (int c = 0; c < 16; c++) {
            uint8_t v = t->art->icon[which & 1][r][c];
            if (v != 0x80) px(t->fb, x4 * 4 + c, y + r, v);
        }
}

void tear_draw_slots(uint8_t *fb, const Game *g, const TearArt *a)     /* A3A5 */
{
    int n = g->page[0xA0];                                             /* A3AD */
    if (!a || !a->loaded || n <= 0) return;                            /* A3AA */
    if (n > 9) n = 9;
    for (int i = 0; i < n; i++) {
        int which = (i == 8);                                          /* A3C1 */
        int x = a->slot_x4[i] * 4;
        for (int r = 0; r < 13; r++)
            for (int c = 0; c < 16; c++) {
                uint8_t v = a->icon[which][r][c];
                if (v != 0x80) px(fb, x + c, r, v);
            }
    }
}

/* [2026]/[2028] vid_save_rect / vid_restore: `x8` cells across, `rows` down */
static void save_rect(Tear *t, uint8_t *buf, int x8, int y, int w8, int rows)
{
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < w8 * 8; c++) {
            int xx = x8 * 8 + c, yy = y + r;
            buf[r * (w8 * 8) + c] = (xx >= 0 && xx < 320 && yy >= 0 && yy < 200)
                                  ? t->fb[yy * 320 + xx] : 0;
        }
}
static void restore_rect(Tear *t, const uint8_t *buf, int x8, int y, int w8, int rows)
{
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < w8 * 8; c++)
            px(t->fb, x8 * 8 + c, y + r, buf[r * (w8 * 8) + c]);
}

/* [2000] vid_window AL=0: clear a rect */
static void clear_rect(Tear *t, int x4, int y, int w4, int rows)
{
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < w4 * 4; c++) px(t->fb, x4 * 4 + c, y + r, 0);
}

/* ------------------------------------------------------- the frame clock */
/* A48F — one animation frame: [FF1A] counts to [FF33]*4, so the scene runs at
 * the game's own speed. */
static void wait_frame(Tear *t)
{
    Game *g = t->g;
    t->frames++;
    if (g->present) g->present(g);
}

/* A4A3 / A50A — the byte-precision Bresenham walk of the crystal */
typedef struct { int x, y, tx, ty, sx, sy, dx, dy, err, major; } Fly;

static void fly_setup(Fly *f, int tx, int ty)
{
    f->x = 0x94; f->y = 0x50; f->tx = tx; f->ty = ty;                    /* A4A5 */
    f->sx = 0; f->dx = tx - f->x;
    if (f->dx) { if (tx < f->x) { f->dx = -f->dx; f->sx = -1; } else f->sx = 1; }
    f->sy = 0; f->dy = ty - f->y;
    if (f->dy) { if (ty < f->y) { f->dy = -f->dy; f->sy = -1; } else f->sy = 1; }
    if (f->dy > f->dx) { f->err = f->dy >> 1; f->major = 1; }            /* A4FD */
    else               { f->err = f->dx >> 1; f->major = 0; }
}

static int fly_step(Fly *f)                                             /* A50A */
{
    if (f->major) {
        if (f->err < f->dx) { f->err += f->dy; f->x += f->sx; }
        f->err -= f->dx;
        f->y += f->sy;
        return f->ty == f->y;
    }
    if (f->err < f->dy) { f->err += f->dx; f->y += f->sy; }
    f->err -= f->dy;
    f->x += f->sx;
    return f->tx == f->x;
}

const uint8_t *tear_framebuffer(const Tear *t) { return t->fb; }

/* =====================================================================
 * A002 — the cutscene.  The caller has already loaded, patched and painted
 * the destination map and hidden the hero (fight.bin 7B32..7C11), so the
 * scene plays over the finished screen.
 * ===================================================================== */
int tear_cutscene(struct Shell *s)
{
    static TearArt art;
    static Tear T;
    Game *g = &s->g;
    if (!art.loaded && tear_art_load(&art, s->dir)) return -1;
    memset(&T, 0, sizeof T);
    T.g = g; T.art = &art;

    /* the screen fight.bin leaves behind: the destination map with the hero
     * still invisible ([FF37]) */
    uint8_t saved_hidden = g->hero_hidden;
    g->hero_hidden = 0xFF;
    render_frame(T.fb, g, &s->hero);
    render_hud(T.fb, g, &s->font, &s->tfont, map_place_record(g->map));
    /* A3A5 painted the row once at boot and nothing redraws it, so the scene
     * starts with the Tears the player already had; the new one is painted by
     * the scene itself at A2FD when the crystal lands. */
    tear_draw_slots(T.fb, g, &art);
    g->tear = &T;

    static uint8_t bg_small[3 * 8 * 16], bg_big[0x11 * 8 * 16];

    unsigned tears = g->page[0xA0];                                      /* A03B */
    tears++;
    int icon = 0;
    if (tears >= 9) { tears = 9; icon = 1; }                             /* A041 */
    g->page[0xA0] = (uint8_t)tears;
    draw_icon(&T, icon, 0x25, 0x52);                     /* A052: (148,82) */
    g->hero_flags &= (uint8_t)~FACE_LEFT;                                /* A05A */

    int frame = 0, bx = 0x0C;
    for (int n = 13; n; n--) {                                           /* A05F */
        if ((n & 1) == 0) { g->sfx_request = 0x1A; sound_request(g); }
        frame = (frame + 1) & 3;
        draw_hero(&T, frame, bx, 0x6E);
        wait_frame(&T);
        if (bx != 0x24) { clear_rect(&T, bx, 0x6E, 0x02, 0x18); bx += 2; }
    }
    frame = 4; draw_hero(&T, frame, 0x24, 0x6E);                         /* A099 */
    for (int n = 5; n; n--) wait_frame(&T);
    for (frame = 5; frame < 9; frame++) {                                /* A0AE */
        draw_hero(&T, frame, 0x24, 0x6E);
        wait_frame(&T); wait_frame(&T);
    }
    frame = 9; draw_hero(&T, frame, 0x24, 0x6E);                         /* A0CA */
    draw_sword(&T);                                                      /* A0D0 */

    Fly f;
    fly_setup(&f, art.slot_x4[tears - 1] * 4, 2);                        /* A0D5 */
    save_rect(&T, bg_small, f.x >> 3, f.y, 3, 16);
    for (int i = 0; i < 2; i++) {                                        /* A0FE */
        draw_spark(&T, i, f.x, f.y);
        wait_frame(&T);
        restore_rect(&T, bg_small, f.x >> 3, f.y, 3, 16);
    }
    save_rect(&T, bg_big, (f.x >> 3) - 6, f.y, 0x11, 16);                /* A13E */
    g->sfx_request = 0x1B; sound_request(g);
    for (int i = 0; i < 2; i++) {
        draw_spark(&T, i | 0x80, f.x - 0x18, f.y);
        wait_frame(&T); wait_frame(&T);
        restore_rect(&T, bg_big, (f.x >> 3) - 6, f.y, 0x11, 16);
    }
    clear_rect(&T, 0x25, 0x52, 0x04, 0x10);                              /* A1A4 */
    draw_sword(&T);                                                      /* A1B1 */

    save_rect(&T, bg_small, f.x >> 3, f.y, 3, 16);                       /* A1B6 */
    for (int i = 0; i < 4; i++) {                                        /* A1D2 */
        draw_spark(&T, i, f.x, f.y);
        wait_frame(&T);
        restore_rect(&T, bg_small, f.x >> 3, f.y, 3, 16);
    }

    int aj = 0, ai = 0, sfx_phase = 200, done = 0;                       /* A209 */
    do {
        if ((++aj & 1) == 0) {
            ai++;
            if (++sfx_phase > 2) { sfx_phase = 0; g->sfx_request = 0x1C; sound_request(g); }
        }
        restore_rect(&T, bg_small, f.x >> 3, f.y, 3, 16);
        done = fly_step(&f);                                             /* A249 */
        save_rect(&T, bg_small, f.x >> 3, f.y, 3, 16);
        draw_spark(&T, (ai & 1) + 2, f.x, f.y);
        wait_frame(&T);
    } while (!done);                                                     /* A27E */
    restore_rect(&T, bg_small, f.x >> 3, f.y, 3, 16);

    save_rect(&T, bg_big, (f.x >> 3) - 6, f.y, 0x11, 16);                /* A297 */
    g->sfx_request = 0x1B; sound_request(g);
    for (int i = 0; i < 2; i++) {
        draw_spark(&T, i | 0x80, f.x - 0x18, f.y);
        wait_frame(&T); wait_frame(&T);
        restore_rect(&T, bg_big, (f.x >> 3) - 6, f.y, 0x11, 16);
    }
    draw_icon(&T, icon, art.slot_x4[tears - 1], 0);                      /* A2FD */

    save_rect(&T, bg_small, f.x >> 3, f.y, 3, 16);                       /* A313 */
    for (int i = 4; i; i--) {                                            /* A32F */
        draw_spark(&T, i - 1, f.x, f.y);
        wait_frame(&T);
        restore_rect(&T, bg_small, f.x >> 3, f.y, 3, 16);
    }

    /* A363: play mfan.msd and wait for it to END — [FF26] is the music
     * driver's "score stopped" flag, not a keypress (src/rokademo.c and
     * docs/CUTSCENES.md §5 both call it "wait for Return", which is wrong). */
    audio_music_play_res(2, 94);                                         /* mfan.msd */
    for (int guard = 0; guard < 2000 && !audio_music_stopped(); guard++) wait_frame(&T);
    audio_music_stop();                                                  /* A37B */

    clear_rect(&T, 0x24, 0x56, 0x06, 0x18);                              /* A37D */
    for (frame = 8; frame >= 5; frame--) {                               /* A38A */
        draw_hero(&T, frame, 0x24, 0x6E);
        wait_frame(&T); wait_frame(&T);
    }
    draw_hero(&T, frame, 0x24, 0x6E);
    for (int n = 5; n; n--) wait_frame(&T);
    clear_rect(&T, 0x24, 0x6E, 0x02, 0x18);                              /* A3B6 */

    bx = 0x26;                                                           /* A3C3 */
    for (int n = 13; n; n--) {                                           /* A3C9 */
        if ((n & 1) == 0) { g->sfx_request = 0x1A; sound_request(g); }
        frame = (frame + 1) & 3;
        draw_hero(&T, frame, bx, 0x6E);
        wait_frame(&T);
        if (bx != 0x3E) { clear_rect(&T, bx, 0x6E, 0x02, 0x18); bx += 2; }
    }
    clear_rect(&T, 0x3E, 0x6E, 0x06, 0x18);                              /* A3FD */

    g->tear = NULL;
    g->hero_hidden = saved_hidden;
    return 0;
}
