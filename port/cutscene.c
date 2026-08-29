/* cutscene.c — opdemo.bin and enddemo.bin: the attract sequence and the ending
 * (docs/CUTSCENES.md §2 and §4; src/opdemo.c, src/enddemo.c).
 *
 * Hex tags are addresses in the overlay image, which is loaded raw at BASE:6000
 * in both cases, so a file offset is `addr - 0x6000`.  Every script, text block
 * and metric table is read out of that image at run time.
 *
 * The original polls the 236.7 Hz [FF1A] counter in a busy loop; the port turns
 * each of those waits into whole rendered frames of CS_TICKS_PER_FRAME ticks
 * and calls the front end's `present` for each, so SDL, the headless PNG dumper
 * and the scripted tests all work exactly as they do for a shop. */
#include <stdlib.h>
#include <string.h>
#include "cutscene.h"
#include "sar.h"
#include "audio.h"

#define IMG(c, addr) ((c)->img + ((addr) - 0x6000))
#define OK(c, addr)  ((size_t)((addr) - 0x6000) < (c)->imglen)

/* ---- resource numbers, from the request blocks at opdemo 953D / enddemo 813D.
 * The byte in a request block is the 1-based resource number, so the archive
 * index the port's sar_load() wants is one less (map.c does the same). */
enum {
    R_AME = 0x0E, R_DMAOU = 0x0F, R_HIME = 0x10, R_HIMP = 0x11, R_HOU = 0x12,
    R_ISI = 0x13, R_MAOP = 0x14, R_NE80 = 0x15, R_NE81 = 0x16, R_NEC = 0x17,
    R_NEW1 = 0x18, R_NEW2 = 0x19, R_OUI = 0x1A, R_OUP = 0x1B, R_SEI = 0x1C,
    R_SEIP = 0x1D, R_TTL1 = 0x1E, R_TTL2 = 0x1F, R_TTL3 = 0x20, R_WAKU = 0x21,
    R_YUU1 = 0x22, R_YUU2 = 0x23, R_YUU3 = 0x24, R_YUU4 = 0x25, R_YUUP = 0x26,
    R_ZEND = 0x27, R_ZOPN = 0x28,
    /* ZELRES2 */
    R_EN72 = 0x34, R_END4 = 0x35, R_END5 = 0x36, R_END6 = 0x37, R_END7 = 0x38,
    R_FIN = 0x39, R_ENDDEMO = 0x33,
};
enum { UNP_MASK = 0, UNP_RLE = 1, UNP_MASKRAW = 2, UNP_RAW = 3 };

/* ------------------------------------------------------------- the clock */

static void cs_frame(Cutscene *c)
{
    if (c->present) c->present(c);
    c->frames++;
    c->ticks += CS_TICKS_PER_FRAME;
    c->ff50 += CS_TICKS_PER_FRAME;
    /* [FF1D] (the Space latch) and [FF29] (the last key) are both set by the
     * kernel's keyboard ISR while the key is down, so this is a level, not an
     * edge — holding Space really does skip every act in a row. */
    if (c->key & 1) c->abort = 1;
    c->key_prev = c->key;
    if (c->max_frames && c->frames >= c->max_frames) c->quit = 1;
}

/* 63AB / 6456 / 6A07 — wait AL ticks, aborting on Space or Return */
static int cs_wait(Cutscene *c, int ticks)
{
    if (c->abort || c->quit) return 1;
    while ((int)c->ticks < ticks) {
        cs_frame(c);
        if (c->abort || c->quit) return 1;
    }
    c->ticks = 0;
    return 0;
}

/* the same, but deaf to the abort flag: the teardown dissolves still play */
static int cs_wait_hard(Cutscene *c, int ticks)
{
    while ((int)c->ticks < ticks) {
        cs_frame(c);
        if (c->quit) return 1;
    }
    c->ticks = 0;
    return 0;
}

/* gdmcga's own waits (31B4 between dissolve passes, 3437 between storm passes,
 * 38E6 between spiral rings) are plain `while ([FF1A] < n)` spins — they do
 * *not* look at the abort flags, so a Space during a dissolve is only honoured
 * at the demo's next wait_ticks.  The only thing that can cut them short here
 * is the front end asking to quit. */
static int gd_wait_thunk(void *user, int ticks)
{
    return cs_wait_hard(user, ticks);
}

/* ------------------------------------------------------------- resources */

size_t cutscene_load(Cutscene *c, int arc, int res, int mode, unsigned off)
{
    size_t len = 0;
    uint8_t *raw = sar_load(c->dir, arc, res - 1, 1, &len);
    if (!raw) return 0;
    size_t cap = off < 0x10000 ? 0x10000 - off : 0;
    size_t n;
    switch (mode) {
    case UNP_RLE:     n = gd_unpack_rle(raw, len, c->arena + off, cap); break;
    case UNP_MASKRAW: n = gd_unpack_mask(raw, len, c->arena + off, cap, 0); break;
    case UNP_RAW:     n = len < cap ? len : cap; memcpy(c->arena + off, raw, n); break;
    default:          n = gd_unpack_mask(raw, len, c->arena + off, cap, 1); break;
    }
    free(raw);
    return n;
}

/* the same into the 64 KB scratch, which is where the demos stage a picture
 * they are going to assemble by hand */
static size_t cutscene_load_scratch(Cutscene *c, int arc, int res, int mode, unsigned off)
{
    size_t len = 0;
    uint8_t *raw = sar_load(c->dir, arc, res - 1, 1, &len);
    if (!raw) return 0;
    size_t cap = off < GD_SCRATCH ? GD_SCRATCH - off : 0;
    size_t n;
    switch (mode) {
    case UNP_RLE:     n = gd_unpack_rle(raw, len, c->scratch + off, cap); break;
    case UNP_MASKRAW: n = gd_unpack_mask(raw, len, c->scratch + off, cap, 0); break;
    case UNP_RAW:     n = len < cap ? len : cap; memcpy(c->scratch + off, raw, n); break;
    default:          n = gd_unpack_mask(raw, len, c->scratch + off, cap, 1); break;
    }
    free(raw);
    return n;
}

/* ------------------------------------------------------------- lifecycle */

int cutscene_init(Cutscene *c, const char *dir, const TextFont *font)
{
    memset(c, 0, sizeof *c);
    c->dir = dir; c->font = font;
    c->scratch = calloc(1, GD_SCRATCH);
    c->arena = calloc(1, 0x10000);
    if (!c->scratch || !c->arena) { cutscene_free(c); return -1; }
    if (gd_init(&c->gd, dir, c->fb, c->scratch, font)) { cutscene_free(c); return -1; }
    c->gd.wait = gd_wait_thunk;
    c->gd.user = c;
    return 0;
}

void cutscene_free(Cutscene *c)
{
    gd_free(&c->gd);
    free(c->scratch); c->scratch = NULL;
    free(c->arena);   c->arena = NULL;
    free(c->img);     c->img = NULL;
}

const uint8_t *cutscene_framebuffer(const Cutscene *c) { return c->fb; }
void cutscene_to_rgb(const Cutscene *c, uint8_t *rgb) { gd_to_rgb(&c->gd, c->fb, rgb); }

/* ------------------------------------------------------- the scrollers */
/* 6358 / 6497 / 6D04 — clear the scratch, then for each line of the block
 * render it into gdmcga's 320x10 text buffer and scroll the scratch up ten
 * rows, compositing the window over the picture.  docs/CUTSCENES.md §2.4. */
static int scroll_block(Cutscene *c, unsigned addr, int x4, int y, int w4, int h)
{
    gd_clear_scratch(&c->gd);
    const uint8_t *p = IMG(c, addr), *end = c->img + c->imglen;
    for (;;) {
        const uint8_t *q = gd_text_line(&c->gd, p);
        int last = (q > p && q[-1] == 0xFF);
        p = q;
        for (int i = 0; i < 10; i++) {
            gd_text_scroll(&c->gd, i, x4, y, w4, h);
            if (cs_wait(c, 0x1C)) return 1;
        }
        if (last || p >= end) break;
    }
    /* 6391 / 64D0 / 6D3C: the flush count is the window height, so the epilogue
     * scroller (window 0x14..0xB4) flushes 0xA0 rows, not 0x78 */
    for (int i = 0; i < h; i++) {
        gd_text_scroll(&c->gd, 0, x4, y, w4, h);
        if (cs_wait(c, 0x1C)) return 1;
    }
    return 0;
}

/* --------------------------------------- the fixed-pitch typed-text player */
/* 62D1 — used only over the demon's face: one character every 0x14 ticks, a
 * red drop shadow one pixel down-right, a keyclick, and bytes 1..4 are inline
 * mouth-frame changes. */
static int play_typed_text(Cutscene *c, unsigned addr)
{
    const uint8_t *p = IMG(c, addr);
    c->text_y = 0x8A; c->text_x = 0;
    for (;;) {
        c->ticks = 0;
        uint8_t ch = *p++;
        if (ch == 0) return 0;
        if (ch < 5) {                       /* 62F3: mouth frame ch-1 */
            gd_face_mouth(&c->gd, c->arena + 0x97C0, ch - 1, 0x1F, 0x70);
            continue;
        }
        if (ch == 0xFF) {                   /* 62FD: line control */
            uint8_t op = *p++;
            if (op == 1) {
                c->text_x = *p++ * 8;                               /* 630C */
                c->text_y += 10;                                    /* 6318 */
            }
        } else {
            gd_putchar(&c->gd, ch, 2, c->text_x + 2, c->text_y + 1); /* 6331 */
            gd_putchar(&c->gd, ch, 7, c->text_x,     c->text_y);     /* 6341 */
            c->text_x += 8;
            if (ch != ' ') audio_sfx_request(0x3F);                  /* 6352 */
        }
        if (cs_wait(c, 0x14)) return 1;                              /* 62F6 */
    }
}

/* --------------------------------------------------- the narration engine */
/* 6A80 — the storm demo's proportional-font typewriter (docs/CUTSCENES.md
 * §2.3).  Returns 0 at 0xFD (end of beat), 1 at 0xFF (end of script) and 1 on
 * an abort as well; the caller checks c->abort to tell them apart. */

/* 6CC4 — the width of the word starting at *p, in pixels */
static int measure_word(Cutscene *c, const uint8_t *p)
{
    const uint8_t *adv = IMG(c, c->advance_addr);
    int w = 0;
    for (;;) {
        uint8_t ch = *p++;
        if (ch == ' ' || ch == 0xFF || ch == 0xFE || ch == 0xFD ||   /* 6CC7 */
            ch == 0xF7 || ch == 0xF3 || ch == 0xF2 || ch == 0xF1)    /* 6CDB */
            return w;
        if (ch & 0x80) continue;                                     /* 6CEF */
        if (ch < 0x20) continue;                                     /* 6CF3 */
        w += adv[ch - 0x20];                                         /* 6CFB */
    }
}

/* opdemo 6C28/6C77 and enddemo 64E3/6530/6568/658C/65D5 — the lip-sync sprite
 * sets.  Each is a three-plane picture drawn with gd_draw_3plane_fast; opdemo
 * has two speakers, enddemo five (two of whose frame banks live inside the
 * overlay image rather than in the arena — which is what the tails of
 * himp.grp / seip.grp turn out to be). */
static const GdSpeaker OP_SPEAKERS[] = {
    /* 6C28 oup.grp @arena:8000 */
    { 0x80, 6, 0, { { 0x98C0, 1344, 0x33, 0x50, 14, 32 },
                    { 0xB840,  528, 0x33, 0x38, 11, 16 } } },
    /* 6C77 yuup.grp @arena:4000 */
    { 0x90, 6, 0, { { 0x58C0,  864, 0x13, 0x50,  9, 32 },
                    { 0x6D00,  528, 0x12, 0x38, 11, 16 } } },
};
static const GdSpeaker ED_SPEAKERS[] = {
    { 0x80, 0x10, 0, { { 0x98C0, 504, 0x38, 0x50,  7, 24 } } },        /* 6568 */
    { 0x90, 6,    0, { { 0x58C0, 864, 0x13, 0x50,  9, 32 },            /* 64E3 */
                       { 0x6D00, 528, 0x12, 0x38, 11, 16 } } },
    { 0xA0, 3,    1, { { 0x7437, 165, 0x35, 0x48,  5, 11 },            /* 6530 */
                       { 0x7626, 168, 0x34, 0x3E,  7,  8 } } },
    { 0xB0, 6,    0, { { 0x98C0, 648, 0x34, 0x50,  9, 24 },            /* 658C */
                       { 0xA7F0, 720, 0x33, 0x38, 10, 24 } } },
    { 0xC0, 0x10, 1, { { 0x781E,  48, 0x38, 0x40,  2,  8 } } },        /* 65D5 */
};

/* returns 1 when `ch` was a lip-sync code (which costs no ticks) */
static int lip_sync(Cutscene *c, uint8_t ch)
{
    for (int i = 0; i < c->nspeakers; i++) {
        const GdSpeaker *sp = &c->speakers[i];
        if ((ch & 0xF0) != sp->group) continue;
        int n = ch & 0x0F, b = 0;
        if (n >= sp->split) { n -= sp->split; b = 1; }
        if (b && !sp->bank[1].base) return 1;
        const uint8_t *base = sp->image ? c->img + (sp->bank[b].base - 0x6000)
                                        : c->arena + sp->bank[b].base;
        gd_blit(&c->gd, base + (size_t)n * sp->bank[b].stride,
                sp->bank[b].x4, sp->bank[b].y, sp->bank[b].w, sp->bank[b].h, GD_W_P3, 3);
        return 1;
    }
    return 0;
}

int cutscene_narrate(Cutscene *c)                                    /* 6A80 */
{
    const uint8_t *bear = IMG(c, c->bearing_addr);
    const uint8_t *adv  = IMG(c, c->advance_addr);
    c->ticks = 0;
    int skip_wait = 0;
    for (;;) {
        /* 6A7B: every byte costs 0x10 ticks *except* a lip-sync code — 6C53 /
         * 6C74 / 6CA0 / 6CC1 jump back to the fetch at 6A80, not to 6A7B, which
         * is what lets the mouth track the syllables instead of the text. */
        if (!skip_wait && cs_wait(c, 0x10)) return 1;
        skip_wait = 0;
        if (!OK(c, c->narr_p)) return 1;
        uint8_t ch = *IMG(c, c->narr_p); c->narr_p++;
        if (ch & 0x80) {
            if (ch != 0xFF && ch != 0xFD && lip_sync(c, ch)) { skip_wait = 1; continue; }
            switch (ch) {
            case 0xEB: c->narr_click = 0x41; continue;
            case 0xEC: c->narr_click = 0x40; continue;
            case 0xED: c->narr_click = 0x3F; continue;
            case 0xEE: c->narr_click = 0x3E; continue;
            case 0xEF: c->narr_click = 0x3D; continue;
            case 0xF0: c->narr_click = 0;    continue;
            case 0xF1: c->narr_line = 3; c->narr_x = 0; continue;
            case 0xF2: c->narr_line = 2; c->narr_x = 0; continue;
            case 0xF3: c->narr_line = 1; c->narr_x = 0; continue;
            case 0xF7: c->narr_line = 0; c->narr_x = 0; continue;
            case 0xF5: if (cs_wait(c, 0xF0)) return 1; continue;
            case 0xF6: for (int i = 0; i < 3; i++) if (cs_wait(c, 0xF0)) return 1; continue;
            case 0xF9: c->narr_ink = 6; c->narr_shadow = 2; continue;
            case 0xFA: c->narr_ink = 7; c->narr_shadow = 0; continue;
            case 0xFB: c->narr_ink = 7; c->narr_shadow = 1; continue;
            case 0xFD: c->beat++; return 0;                          /* end of beat */
            case 0xFE: gd_window(&c->gd, 0, 0x00, 143, 0x50, 57);    /* clear the box */
                       c->narr_line = 0; c->narr_x = 0; continue;
            case 0xFF: c->beat++; return 1;                          /* end of script */
            default: continue;
            }
        }
        if (ch < 0x20 || ch > 0x7E) continue;
        if (ch != ' ' && ch != '.' && ch != ',' && ch != '"' && ch != '\'')
            audio_sfx_request(c->narr_click);                        /* 6AA6 */
        int x = c->narr_x + 4 - bear[ch - 0x20];                     /* 6AAF */
        int y = c->narr_line * 10 + 0x8F;                            /* 6AB9 */
        gd_putchar(&c->gd, ch, c->narr_shadow, x + 1, y + 1);        /* 6ADE */
        gd_putchar(&c->gd, ch, c->narr_ink,    x,     y);            /* 6AEA */
        c->narr_x += adv[ch - 0x20];                                 /* 6AFD */
        if (ch == ' ' && c->narr_x + measure_word(c, IMG(c, c->narr_p)) >= 0x138) {
            c->narr_x = 0; c->narr_line++;                           /* 6BF0 */
        }
    }
}

/* ------------------------------------------------- plane arithmetic (6E0F..) */
/* The demo builds three pictures gdmcga cannot draw directly; all of them are
 * plain plane-by-plane byte operations.  docs/CUTSCENES.md §6.4. */

/* 6E0F — assemble the act-1 demon face: a 34 x 112 three-plane image at
 * scratch:0 out of single planes of dmaou.grp.  The eyes (34x48) go into
 * planes 0+1, the nose (6x32, at +0x1200) into planes 0+1 at (col 15, row 48)
 * and the mouth (18x32) into plane 0 only at (col 8, row 80), so the three
 * parts come out in three different colours from one source plane each. */
static void cutscene_build_demon_face(Cutscene *c)
{
    const size_t W = 34, H = 112, P = W * H;
    uint8_t *d = c->scratch;
    memset(d, 0, P * 3);
    /* 6E4F copies source plane 0 -> dest plane 1 and source plane 1 -> dest
     * plane 0 (the planes are swapped); 6E5E copies them straight through.
     * Dest plane 2 is never written. */
    struct { unsigned src; int col, row, w, h, swap; } part[3] = {
        { 0xAB40, 0,    0, 34, 48, 1 },   /* 6E32 the eyes  */
        { 0xA9C0, 15,  48,  6, 32, 1 },   /* 6E3B the nose  */
        { 0x9C40,  8,  80, 18, 32, 0 },   /* 6E4A the mouth */
    };
    for (int i = 0; i < 3; i++) {
        const uint8_t *s = c->arena + part[i].src;
        size_t sp = (size_t)part[i].w * part[i].h;
        for (int r = 0; r < part[i].h; r++)
            for (int x = 0; x < part[i].w; x++) {
                uint8_t p0 = s[(size_t)r * part[i].w + x];
                uint8_t p1 = s[sp + (size_t)r * part[i].w + x];
                size_t di = (size_t)(part[i].row + r) * W + part[i].col + x;
                if (di >= P) continue;
                d[di]     |= part[i].swap ? p1 : p0;
                d[P + di] |= part[i].swap ? p0 : p1;
            }
    }
}

/* 6E8F / 6EB0 — eye frame `f` (two planes at arena:AB40 + f*0xCC0) becomes a
 * three-plane 34 x 48 picture at scratch:0; plane 2 is derived as ~p0 & p1 and
 * p1 is then XORed with it. */
static void cutscene_demon_eyes_to_scratch(Cutscene *c, int f)
{
    const size_t W = 34, H = 48, P = W * H;
    const uint8_t *s = c->arena + 0xAB40 + (size_t)f * 0xCC0;
    uint8_t *d = c->scratch;
    for (size_t i = 0; i < P; i++) {
        uint8_t p0 = s[i], p1 = s[P + i];
        uint8_t p2 = (uint8_t)(~p0 & p1);
        d[i] = p0;
        d[P + i] = (uint8_t)(p1 ^ p2);
        d[2 * P + i] = p2;
    }
}

/* 6ED8 — stencil the scratch picture (34 x 48, three planes) into the arena
 * picture at arena:4000 through a mask built from its own three planes. */
static void cutscene_mask_demon_over_picture(Cutscene *c)
{
    const size_t SW = 34, SH = 48, SP = SW * SH;
    const size_t DW = 72, DH = 104, DP = DW * DH;
    const size_t ORIGIN = 0;                     /* the face sits at the top left */
    uint8_t *dst = c->arena + 0x4000;
    const uint8_t *src = c->scratch;
    for (size_t r = 0; r < SH; r++)
        for (size_t x = 0; x < SW; x++) {
            size_t si = r * SW + x, di = ORIGIN + r * DW + x;
            if (di + 2 * DP >= 0x10000 - 0x4000) continue;
            uint8_t m = (uint8_t)(src[si] | src[SP + si] | src[2 * SP + si]);
            dst[di] = (uint8_t)((dst[di] & ~m) | src[si]);
            dst[DP + di] = (uint8_t)((dst[DP + di] & ~m) | src[SP + si]);
            dst[2 * DP + di] = (uint8_t)((dst[2 * DP + di] & ~m) | src[2 * SP + si]);
        }
}

/* 6FAC — yuu3.grp only *has* two planes (0x3000 bytes each); derive the third
 * the same way 6EB0 does. */
static void cutscene_synth_third_plane(Cutscene *c)
{
    const size_t P = 0x3000;
    uint8_t *d = c->arena + 0x4000;
    for (size_t i = 0; i < P; i++) {
        uint8_t p0 = d[i], p1 = d[P + i];
        uint8_t p2 = (uint8_t)(~p0 & p1);
        d[P + i] = (uint8_t)(p1 ^ p2);
        d[2 * P + i] = p2;
    }
}

/* 6F41 — stamp yuu4.grp (three planes, 21 x 160) into yuu3 at +0x819. */
static void cutscene_mask_yuu4_into_yuu3(Cutscene *c)
{
    const size_t SW = 21, SH = 160, SP = SW * SH;
    const size_t DW = 64, DP = 0x3000;
    uint8_t *dst = c->arena + 0x4000;
    const uint8_t *src = c->arena + 0xD000;
    for (size_t r = 0; r < SH; r++)
        for (size_t x = 0; x < SW; x++) {
            size_t si = r * SW + x, di = 0x819 + r * DW + x;
            if (di + 2 * DP >= 0x10000 - 0x4000) continue;
            uint8_t m = (uint8_t)(src[si] | src[SP + si] | src[2 * SP + si]);
            dst[di] = (uint8_t)((dst[di] & ~m) | src[si]);
            dst[DP + di] = (uint8_t)((dst[DP + di] & ~m) | src[SP + si]);
            dst[2 * DP + di] = (uint8_t)((dst[2 * DP + di] & ~m) | src[2 * SP + si]);
        }
}

/* ===================================================================== */
/* ACT 1 — 6002: the logo, the prologue over the pendant, the demon and the
 * title screen.                                                          */
/* ===================================================================== */
int cutscene_act1(Cutscene *c)
{
    Gd *g = &c->gd;
    c->act = 1; c->abort = 0; c->ticks = 0;
    memset(c->fb, 0, sizeof c->fb);                                  /* 6002 */

    /* a first glimpse of the logo over the copyright line ---------------- */
    cutscene_load(c, 0, R_TTL3, UNP_RLE, 0x4000);                    /* 601E */
    gd_set_palette(g, 4);
    gd_puts(g, IMG(c, OP_COPYRIGHT), 0, 0x96);                       /* 6041 */
    gd_draw_ao(g, c->arena + 0x4000, 0x07, 0x0F, 0x41, 0x70);

    /* the pendant, in two colours, with the prologue scrolling over ------ */
    cutscene_load(c, 0, R_NEC, UNP_MASK, 0x4000);                    /* 6060 */
    memset(c->fb, 0, sizeof c->fb);                                  /* 608A */
    c->abort = 0; c->key_prev = c->key;                              /* 608F/6095 */
    gd_set_palette(g, 1);                                            /* 609B */
    if (gd_draw(g, c->arena + 0x4000, 0x12, 0x20, 0x2C, 0x68, GD_W_P2H, 2, 0xFF)) goto teardown;
    if (scroll_block(c, OP_PROLOGUE_TEXT, 0x00, 0x20, 0x50, 0x78)) goto teardown;

    /* the same picture in full colour, then the storm ------------------- */
    gd_set_palette(g, 2);                                            /* 60BB */
    if (gd_draw(g, c->arena + 0x4000, 0x12, 0x20, 0x2C, 0x68, GD_W_P3, 3, 0xFF)) goto teardown;
    cutscene_load(c, 0, R_HOU, UNP_MASK, 0x9000);                    /* 60E3 */
    gd_blit(g, c->arena + 0x75A0, 0x20, 0x48, 0x10, 0x40, GD_W_P3, 3);
    audio_sfx_request(4);                                            /* 60F9 */
    if (gd_storm(g, IMG(c, OP_STORM_DROPS), c->arena + 0x9000)) goto teardown;

    /* the demon's face and his warning ---------------------------------- */
    cutscene_load(c, 0, R_DMAOU, UNP_MASK, 0x97C0);                  /* 6109 */
    cutscene_build_demon_face(c);                                    /* 6124 */
    if (gd_erase(g, 0x12, 0x20, 0x2C, 0x68)) goto teardown;          /* 6127 */
    gd_set_palette(g, 3);
    if (gd_draw(g, c->scratch, 0x17, 0x20, 0x22, 0x70, GD_W_P3, 3, 0xFF)) goto teardown;

    for (const uint8_t *p = IMG(c, OP_DEMON_EYES); *p; p++) {        /* 6151 */
        c->ticks = 0;
        gd_face_eyes(g, c->arena + 0xAB40, *p - 1, 0x17, 0x20);
        if (cs_wait(c, 0x14)) goto teardown;
    }
    if (cs_wait(c, 0xF0)) goto teardown;
    if (play_typed_text(c, OP_DEMON_SPEECH)) goto teardown;          /* 617E */
    if (cs_wait(c, 0xF0)) goto teardown;
    gd_face_eyes(g, c->arena + 0xAB40, 2, 0x17, 0x20);               /* 618B AL=2 */
    if (cs_wait(c, 0x0F)) goto teardown;
    gd_face_eyes(g, c->arena + 0xAB40, 3, 0x17, 0x20);               /* 619F AL=3 */
    if (cs_wait(c, 0xF0)) goto teardown;
    gd_window(g, 0, 0x00, 0x94, 0x50, 0x1E);                         /* 61BB */

    /* the title screen -------------------------------------------------- */
    cutscene_load(c, 0, R_TTL1, UNP_RLE, 0x4000);                    /* 61CA */
    if (gd_erase(g, 0x17, 0x20, 0x22, 0x70)) goto teardown;          /* 620B */
    gd_set_palette(g, 4);
    c->ticks = 0;                                                    /* 621E */
    audio_music_play_res(0, R_ZOPN - 1);                             /* 622E */
    gd_sky_dither(g);                                                /* 6231 */
    if (cs_wait(c, 0xF0)) goto teardown;
    if (gd_draw(g, c->arena + 0x4000, 0x0B, 0x48, 0x31, 0x80, GD_W_P3, 3, 0)) goto teardown;
    c->ticks = 0;
    cutscene_load(c, 0, R_TTL3, UNP_RLE, 0x4000);                    /* 6260 */
    if (cs_wait(c, 0xF0)) goto teardown;
    gd_draw_ao(g, c->arena + 0x4000, 0x07, 0x0F, 0x41, 0x70);     /* 6276 */
    c->ticks = 0;
    cutscene_load(c, 0, R_TTL2, UNP_RLE, 0x4000);                    /* 628B */
    gd_tile_map(g, IMG(c, OP_TTL2_MAP), c->arena + 0x4000);          /* 6291 */
    if (cs_wait(c, 0xF0)) goto teardown;

    {                                                                /* 629B */
        int lo = 0xC7, hi = 0x00;
        for (int n = 100; n; n--, lo -= 2, hi += 2) {
            c->ticks = 0;
            gd_ornament_row(g, lo & 0xFF);
            gd_ornament_row(g, hi & 0xFF);
            if (cs_wait(c, 0x50)) goto teardown;
        }
    }
    while (!audio_music_stopped()) { cs_frame(c); if (c->abort || c->quit) break; }

teardown:                                                            /* 63E5 */
    audio_music_fade(8);
    gd_erase(g, 0x00, 0x00, 0x50, 0xC8);
    c->abort = 0; c->key_prev = c->key;
    return c->quit;
}

/* ===================================================================== */
/* ACT 2 — 640C: the STAFF credits, over black, to zend.msd.              */
/* ===================================================================== */
int cutscene_act2(Cutscene *c)
{
    c->act = 2; c->abort = 0; c->ticks = 0;
    memset(c->fb, 0, sizeof c->fb);                                  /* 640C */
    audio_music_play_res(0, R_ZEND - 1);                             /* 643A */
    gd_set_palette(&c->gd, 1);
    scroll_block(c, OP_STAFF_TEXT, 0x00, 0x20, 0x50, 0x78);          /* 6497 */
    audio_music_fade(8);                                             /* 6477 */
    memset(c->fb, 0, sizeof c->fb);
    c->abort = 0; c->key_prev = c->key;
    return c->quit;
}

/* ===================================================================== */
/* ACT 3 — 6540: the storm demo.  Fifteen pictures in waku.grp's frame with
 * the story typed underneath; the whole act is "set up the next picture,
 * then let the script talk".                                             */
/* ===================================================================== */
#define NARR() do { if (cutscene_narrate(c)) goto teardown; } while (0)

int cutscene_act3(Cutscene *c)
{
    Gd *g = &c->gd;
    c->act = 3; c->abort = 0; c->ticks = 0;
    c->narr_p = OP_NARRATION;                                        /* 6551 */
    c->bearing_addr = OP_GLYPH_BEARING; c->advance_addr = OP_GLYPH_WIDTH;
    c->speakers = OP_SPEAKERS; c->nspeakers = 2;
    c->narr_x = 0; c->narr_line = 0; c->narr_ink = 7; c->narr_shadow = 0;
    c->narr_click = 0; c->beat = 0;
    gd_set_palette(g, 5);

    cutscene_load_scratch(c, 0, R_WAKU, UNP_MASK, 0x0000);           /* 656A */
    cutscene_load(c, 0, R_AME, UNP_MASK, 0x4000);                    /* 6589 */
    gd_blit(g, c->scratch, 0x00, 0x00, 0x50, 0x88, GD_W_P3, 3);      /* 65AC */
    gd_blit(g, c->arena + 0x4000, 0x04, 0x10, 0x48, 0x68, GD_W_P3, 3); /* 65B1 */
    NARR();

    gd_set_palette(g, 9);
    gd_blit(g, c->arena + 0x4000, 0x04, 0x10, 0x48, 0x68, GD_W_P3, 3);
    cutscene_load(c, 0, R_HIME, UNP_MASK, 0x4000);                   /* 65EC */
    NARR();
    if (gd_fx_sand(g, 0)) goto teardown;                             /* 6604 */
    gd_set_palette(g, 6);
    gd_blit(g, c->arena + 0x4000, 0x04, 0x10, 0x48, 0x68, GD_W_P3, 3);

    cutscene_load(c, 0, R_DMAOU, UNP_MASK, 0x97C0);                  /* 662E */
    NARR();
    cutscene_demon_eyes_to_scratch(c, 4);                            /* 6646 */
    cutscene_mask_demon_over_picture(c);                             /* 6653 */
    gd_blit(g, c->arena + 0x4000, 0x04, 0x10, 0x48, 0x68, GD_W_P3, 3);
    NARR(); NARR();
    gd_draw_masked(g, 7, c->scratch, 0x17, 0x28, 0x22, 0x30);     /* 6681 bx=1728 */
    NARR(); NARR();
    cutscene_demon_eyes_to_scratch(c, 2);                            /* 668E */
    gd_blit(g, c->scratch, 0x17, 0x28, 0x22, 0x30, GD_W_P3, 3);      /* 66A1 */
    c->ticks = 0; if (cs_wait(c, 0x0F)) goto teardown;
    cutscene_demon_eyes_to_scratch(c, 3);                            /* 66B3 */
    gd_blit(g, c->scratch, 0x17, 0x28, 0x22, 0x30, GD_W_P3, 3);      /* 66C6 */

    cutscene_load(c, 0, R_ISI, UNP_MASK, 0x4000);                    /* 66D5 */
    if (gd_erase(g, 0x04, 0x10, 0x48, 0x68)) goto teardown;          /* 66EE */
    NARR();
    gd_set_palette(g, 7);
    if (gd_draw(g, c->arena + 0x4000, 0x04, 0x10, 0x48, 0x68, GD_W_P3, 3, 0xFF)) goto teardown;
    NARR();

    cutscene_load(c, 0, R_OUI, UNP_MASK, 0x4000);                    /* 6720 */
    if (gd_draw(g, c->arena + 0x4000, 0x04, 0x10, 0x48, 0x68, GD_W_P3, 3, 0)) goto teardown;  /* 6743 AL=0 */
    NARR(); NARR();

    cutscene_load(c, 0, R_SEI, UNP_MASK, 0x4000);                    /* 6758 */
    gd_draw_masked(g, 5, c->arena + 0x4000, 0x16, 0x10, 0x24, 0x68);  /* 6776 bx=1610 */
    NARR();
    if (gd_fx_sand(g, 0)) goto teardown;
    NARR();

    cutscene_load(c, 0, R_YUU1, UNP_MASK, 0x4000);                   /* 6792 */
    if (gd_draw(g, c->arena + 0x4000, 0x04, 0x10, 0x48, 0x68, GD_W_P3, 3, 0xFF)) goto teardown;

    /* the throne-room dialogue: two talking heads in boxes -------------- */
    cutscene_load(c, 0, R_YUUP, UNP_MASK, 0x4000);                   /* 67C4 */
    cutscene_load(c, 0, R_OUP,  UNP_MASK, 0x8000);                   /* 67E1 */
    NARR(); NARR();
    if (gd_fx_sand(g, 0)) goto teardown;
    gd_set_palette(g, 6);
    gd_picture_box(g, 0x0A, 0x15, 0x1A, 0x5D);                       /* 680F */
    gd_blit(g, c->arena + 0x4000, 0x0B, 0x18, 0x18, 0x58, GD_W_P3, 3);  /* 6822 */
    gd_picture_box(g, 0x2C, 0x15, 0x1A, 0x5D);                       /* 682D */
    gd_blit(g, c->arena + 0x8000, 0x2D, 0x18, 0x18, 0x58, GD_W_P3, 3);  /* 6840 */
    NARR(); NARR();

    cutscene_load(c, 0, R_MAOP, UNP_MASK, 0x8000);                   /* 6855 */
    if (gd_fx_sand(g, 0)) goto teardown;
    gd_set_palette(g, 8);
    gd_picture_box(g, 0x15, 0x15, 0x31, 0x5D);                       /* 687D */
    gd_fx_recolour(g, c->arena + 0x8000, 0x16, 0x18);                /* 688D */
    NARR(); NARR();
    /* 68A1: BH += 1 and CH -= 1 a step, so the right edge stays put and the
     * wide box closes down onto the right-hand picture box */
    for (int n = 0x18, x4 = 0x15, w4 = 0x31; n; n--, x4++, w4--) {
        c->ticks = 0;
        gd_picture_box(g, x4, 0x15, w4, 0x5D);
        if (cs_wait(c, 0x0F)) goto teardown;
    }
    gd_picture_box(g, 0x2C, 0x15, 0x1A, 0x5D);                       /* 68C5 */
    gd_picture_box(g, 0x0A, 0x15, 0x1A, 0x5D);
    gd_blit(g, c->arena + 0x4000, 0x0B, 0x18, 0x18, 0x58, GD_W_P3, 3);
    NARR(); NARR();
    for (int n = 0x18, x4 = 0x2C, w4 = 0x1A; n; n--, x4++, w4--) {   /* 68F7 */
        c->ticks = 0;
        gd_picture_box(g, x4, 0x15, w4, 0x5D);
        if (cs_wait(c, 0x0F)) goto teardown;
    }
    if (gd_fx_sand(g, 0)) goto teardown;
    gd_set_palette(g, 7);

    cutscene_load(c, 0, R_YUU2, UNP_MASK, 0x4000);                   /* 692E */
    gd_blit(g, c->arena + 0x4000, 0x10, 0x10, 0x31, 0x60, GD_W_P3, 3);  /* 694F */
    NARR();

    /* the last picture: yuu3 (two planes) with yuu4 stencilled into it --- */
    cutscene_load(c, 0, R_YUU3, UNP_MASK, 0x4000);                   /* 697E */
    if (gd_erase(g, 0x00, 0x00, 0x50, 0xC8)) goto teardown;          /* 6987 */
    cutscene_synth_third_plane(c);                                   /* 6997 -> 6FAC */
    if (gd_draw(g, c->arena + 0x4000, 0x08, 0x08, 0x40, 0xC0, GD_W_P3, 3, 0xFF)) goto teardown;
    cutscene_load(c, 0, R_YUU4, UNP_MASK, 0xD000);                   /* 69A5 */
    cutscene_mask_yuu4_into_yuu3(c);                                 /* 69B3 */
    if (gd_draw(g, c->arena + 0x4000, 0x08, 0x08, 0x40, 0xC0, GD_W_P3, 3, 0xFF)) goto teardown;
    c->ticks = 0; if (cs_wait(c, 0xF0)) goto teardown;
    if (gd_draw(g, c->arena + 0x4000, 0x08, 0x08, 0x40, 0xC0, GD_W_P2H, 2, 0xFF)) goto teardown;
    gd_set_palette(g, 1);
    if (scroll_block(c, OP_EPILOGUE_TEXT, 0x00, 0x14, 0x50, 0xA0)) goto teardown;
    for (int n = 10; n; n--) if (cs_wait(c, 0xC8)) goto teardown;    /* 69FC */

teardown:                                                            /* 6A41 */
    gd_erase(g, 0x00, 0x00, 0x50, 0xC8);
    c->abort = 0; c->key_prev = c->key;
    return c->quit;
}

/* ===================================================================== */
int cutscene_intro(Cutscene *c)
{
    size_t len = 0;
    free(c->img);
    c->img = sar_load(c->dir, 0, 0, 1, &len);            /* ZELRES1[0] opdemo */
    if (!c->img) return -1;
    c->imglen = len;
    if (cutscene_act1(c)) return 0;
    if (cutscene_act2(c)) return 0;
    cutscene_act3(c);
    audio_music_stop();
    return 0;
}

/* =====================================================================
 * enddemo.bin (ZELRES2[50]) — the ending.  docs/CUTSCENES.md §4 and
 * src/enddemo.c, corrected against the disassembly: act 1 is NOT silent —
 * `6318` is a second copy of the narration engine (with five speaker groups
 * instead of two), and the seven `beat()` calls src/enddemo.c shows are calls
 * to it, playing the script at 6AA8.
 * ===================================================================== */

/* 6318 — the ending's own copy of the narration engine.  Unlike opdemo's, its
 * return value is discarded by the caller: 0xFF ends the *script*, not the act,
 * and act 1 still falls into the credits at 62EB. */
static int end_narrate(Cutscene *c)
{
    cutscene_narrate(c);
    return c->quit;
}

/* 6A1E — new1.grp is a 24 x 265 three-plane strip (plane stride 0x18D8);
 * copy an 87-row window starting at row*24 into the scratch, blanking the last
 * 24 bytes of each plane. */
static void end_scroll_strip(Cutscene *c, int row)
{
    const uint8_t *src = c->arena + 0x8000 + (size_t)row * 0x18;
    uint8_t *dst = c->scratch;
    for (int pl = 0; pl < 3; pl++) {
        memcpy(dst, src, 0x828);  dst += 0x828;
        memset(dst, 0, 0x18);     dst += 0x18;
        src += 0x18D8;
    }
}

/* 6A52 — stamp "FIN" into the finished landscape: the first single-plane
 * 38 x 53 stencil is ORed into all three planes at +0x4CE6 (plane stride
 * 0x29E0), the second is ANDed out again. */
static void end_stamp_fin(Cutscene *c)
{
    const size_t PL = 0x29E0;
    uint8_t *pic = c->arena + 0x4000;
    const uint8_t *s = c->arena + 0xBDA0;
    for (int pass = 0; pass < 2; pass++) {
        uint8_t *p = pic + 0x4CE6;
        for (int r = 0; r < 0x35; r++, p += 0x28)
            for (int col = 0; col < 0x13; col++, s++) {
                if (!pass) { p[col] |= *s; p[col + PL] |= *s; p[col + 2 * PL] |= *s; }
                else { p[col] &= (uint8_t)~*s; p[col + PL] &= (uint8_t)~*s;
                       p[col + 2 * PL] &= (uint8_t)~*s; }
            }
    }
}

/* --- the seven scene routines (the table at 6820) ---------------------- */
static void end_scene(Cutscene *c, int i)
{
    Gd *g = &c->gd;
    switch (i) {
    case 0:                                                          /* 682E */
        gd_unpack_mask(c->scratch + 0x0000, GD_SCRATCH, c->arena + 0x4000, 0xC000, 1);
        gd_draw(g, c->arena + 0x4000, 0x0B, 0x08, 0x39, 0x9A, GD_W_P3, 3, 0xFF);
        break;
    case 1:                                                          /* 685A */
        gd_unpack_mask(c->scratch + 0x3400, GD_SCRATCH - 0x3400, c->arena + 0x4000, 0xC000, 1);
        gd_erase(g, 0x0B, 0x08, 0x39, 0x9A);
        gd_draw(g, c->arena + 0x4000, 0x21, 0x14, 0x2F, 0x72, GD_W_P3, 3, 0xFF);
        break;
    case 2:                                                          /* 6891 */
        gd_unpack_mask(c->scratch + 0x5E00, GD_SCRATCH - 0x5E00, c->arena + 0x4000, 0xC000, 1);
        gd_end_open(g, c->arena + 0x4000);
        break;
    case 3: gd_end_close(g, c->arena + 0x4000); break;               /* 68B5 */
    case 4: gd_erase(g, 0x00, 0x00, 0x50, 0xC8); break;              /* 68C2 */
    case 5:                                                          /* 68CF */
        gd_unpack_mask(c->scratch + 0x8A00, GD_SCRATCH - 0x8A00, c->arena + 0x4000, 0xC000, 1);
        memcpy(c->arena + 0x93C0, c->scratch + 0xB800, 0x29E0);      /* 68E5 en72 */
        memset(c->arena + 0x4000, 0, 0x50);                          /* 68F4 */
        gd_draw(g, c->arena + 0x4000, 0x00, 0x00, 0x50, 0x86, GD_W_P3, 3, 0xFF);
        gd_unpack_mask(c->scratch + 0xE200, GD_SCRATCH - 0xE200, c->arena + 0xBDA0, 0x2000, 0);
        end_stamp_fin(c);                                            /* 692F */
        break;
    default:                                                         /* 6932 */
        gd_blit(g, c->arena + 0x4000, 0x00, 0x00, 0x50, 0x86, GD_W_P3, 3);
        break;
    }
}

/* 66CD — the credits typewriter.  Character cells are 8 x 14 at
 * (col*8, row*14 + 0x90); a solid 0xFF cursor block is left after every
 * character and erased before the next one. */
static void end_cursor(Cutscene *c, int colour)                      /* 67EA */
{
    gd_cursor_block(&c->gd, colour, c->cur_col * 2, c->cur_row * 14 + 0x90);
}

static int end_run_script(Cutscene *c)
{
    Gd *g = &c->gd;
    for (;;) {
        c->ticks = 0;                                                /* 66CD */
        if (!OK(c, c->script_p)) return 0;
        uint8_t b = *IMG(c, c->script_p); c->script_p++;
        switch (b) {
        case 0xF7:                                                   /* 6756 */
            /* wait for score opcode F1 to bump [FF21]; with the sound off
             * (--no-music, or any headless run) there is nothing to wait for */
            while (audio_active() && audio_music_sync0() == 0) { cs_frame(c); if (c->quit) return 1; }
            audio_music_sync0_clear();
            c->ff50 = 0;
            continue;
        case 0xF8:                                                   /* 676E */
            c->pause_ticks = (int)(*IMG(c, c->script_p) | (*IMG(c, c->script_p + 1) << 8));
            c->script_p += 2;
            continue;
        case 0xF9:                                                   /* 6779 */
            end_cursor(c, 0);
            while ((int)c->ff50 < c->pause_ticks) { cs_frame(c); if (c->quit) return 1; }
            c->ff50 = 0;
            continue;
        case 0xFA: c->char_delay = *IMG(c, c->script_p++); continue;  /* 6793 */
        case 0xFB:                                    /* 679E: row then column */
            c->cur_row = *IMG(c, c->script_p);
            c->cur_col = *IMG(c, c->script_p + 1);
            c->script_p += 2;
            continue;
        case 0xFC:                                                   /* 67AD */
            gd_window(g, 0, 0x00, 0x8C, 0x50, 0x3C);
            c->cur_col = 0; c->cur_row = 0;
            continue;
        case 0xFD: end_cursor(c, 0); c->cur_col = 0; c->cur_row++; continue;  /* 67C7 */
        case 0xFE:                                                   /* 6808 */
            end_cursor(c, 0);
            end_scene(c, c->scene_i);
            c->scene_i++;
            continue;
        case 0xFF: end_cursor(c, 0); return 0;                       /* 6802 */
        case 0x09:                                                   /* 67D8 */
            end_cursor(c, 0);
            c->cur_col = (c->cur_col + 4) & 0xFC;
            break;
        default:                                                     /* 671E */
            if (b < 0x20 || b > 0x7E) continue;
            end_cursor(c, 0);
            gd_putchar(g, b, 7, c->cur_col * 8, c->cur_row * 14 + 0x90);
            c->cur_col++;
            break;
        }
        end_cursor(c, 0xFF);                                         /* 6748 */
        if (cs_wait_hard(c, c->char_delay)) return 1;
        if (c->quit) return 1;
    }
}

int cutscene_ending(Cutscene *c)
{
    Gd *g = &c->gd;
    size_t len = 0;
    free(c->img);
    c->img = sar_load(c->dir, 1, R_ENDDEMO - 1, 1, &len);   /* ZELRES2[50] */
    if (!c->img) return -1;
    c->imglen = len;
    c->act = 4; c->abort = 0; c->ticks = 0; c->beat = 0;
    c->narr_p = 0x6AA8;                                              /* 6007 */
    c->bearing_addr = 0x807D; c->advance_addr = 0x80DD;
    c->speakers = ED_SPEAKERS; c->nspeakers = 5;
    c->narr_x = 0; c->narr_line = 0; c->narr_ink = 0; c->narr_shadow = 0; c->narr_click = 0;
    gd_set_palette(g, 6);                                            /* 6011 */

    /* --- act 1, 6002: nine tableaux, each followed by a narration beat --- */
    cutscene_load(c, 0, R_YUUP, UNP_MASK, 0x4000);
    cutscene_load(c, 0, R_NEW1, UNP_MASK, 0x8000);
    if (gd_draw(g, c->arena + 0x4000, 0x0B, 0x18, 0x18, 0x58, GD_W_P3, 3, 0xFF)) goto done;
    end_scroll_strip(c, 0xB2);                                       /* 6078 */
    if (gd_draw(g, c->scratch, 0x2D, 0x71, 0x18, 0x58, GD_W_P3, 3, 0xFF)) goto done;
    c->ticks = 0; if (cs_wait(c, 0xFF)) goto done;
    for (int n = 0x59; n; n--) {                                     /* 609D */
        end_scroll_strip(c, (n - 1) * 2);
        gd_blit(g, c->scratch, 0x2D, (uint8_t)(n + 0x17), 0x18, 0x58, GD_W_P3, 3);
        if (cs_wait(c, 0x0A)) goto done;
    }

    cutscene_load_scratch(c, 0, R_WAKU, UNP_MASK, 0x0000);           /* 60E0 */
    if (gd_wipe(g, c->scratch)) goto done;                           /* 60F8 */
    if (end_narrate(c)) goto done;                                   /* 60FD */

    cutscene_load(c, 0, R_NEW2, UNP_MASK, 0x4000);
    if (gd_fx_sand(g, 1)) goto done;                                 /* 6120 */
    gd_set_palette(g, 7);
    if (gd_draw(g, c->arena + 0x4000, 0x1D, 0x12, 0x1C, 0x64, GD_W_P3, 3, 0xFF)) goto done;
    if (end_narrate(c)) goto done;

    cutscene_load(c, 0, R_SEI, UNP_MASK, 0x4000);
    if (gd_draw_masked(g, 5, c->arena + 0x4000, 0x16, 0x10, 0x24, 0x68)) goto done;  /* 616D */
    if (end_narrate(c)) goto done;

    cutscene_load(c, 0, R_YUUP, UNP_MASK, 0x4000);
    cutscene_load(c, 0, R_SEIP, UNP_MASK, 0x8000);
    if (gd_fx_sand(g, 0)) goto done;
    gd_set_palette(g, 6);
    gd_picture_box(g, 0x0A, 0x15, 0x1A, 0x5D);                       /* 61C4 */
    gd_blit(g, c->arena + 0x4000, 0x0B, 0x18, 0x18, 0x58, GD_W_P3, 3);
    gd_picture_box(g, 0x2C, 0x15, 0x1A, 0x5D);                       /* 61E2 */
    gd_blit(g, c->arena + 0x8000, 0x2D, 0x18, 0x18, 0x58, GD_W_P3, 3);
    if (end_narrate(c)) goto done;

    cutscene_load(c, 0, R_HIMP, UNP_MASK, 0x8000);
    if (gd_draw(g, c->arena + 0x8000, 0x2D, 0x18, 0x18, 0x58, GD_W_P3, 3, 0xFF)) goto done;
    if (end_narrate(c)) goto done;

    cutscene_load(c, 0, R_NE80, UNP_MASK, 0x4000);
    cutscene_load(c, 0, R_NE81, UNP_MASK, 0x8000);
    if (gd_fx_sand(g, 2)) goto done;                                 /* 626F */
    gd_set_palette(g, 7);
    if (gd_draw(g, c->arena + 0x4000, 0x0B, 0x12, 0x1A, 0x64, GD_W_P3, 3, 0xFF)) goto done;
    if (gd_draw(g, c->arena + 0x8000, 0x33, 0x25, 0x12, 0x51, GD_W_P3, 3, 0xFF)) goto done;
    if (end_narrate(c)) goto done;

    memset(c->arena + 0x4000, 0, 0x1E78);                            /* 62B1 */
    for (int n = 0x64, v = 0x55; n; n--, v = ((v >> 1) | (v << 7)) & 0xFF)
        memset(c->arena + 0x4000 + (size_t)(0x64 - n) * 0x1A, (uint8_t)v, 0x1A);
    if (gd_draw(g, c->arena + 0x4000, 0x0B, 0x12, 0x1A, 0x64, GD_W_P3, 3, 0)) goto done;
    if (end_narrate(c)) goto done;
    gd_erase(g, 0x00, 0x00, 0x50, 0xC8);                             /* 62E6 */

    /* --- act 2, 6638: the credits, typed to zend.msd -------------------- */
    c->act = 5;
    c->scene_i = 0; c->cur_col = 0; c->cur_row = 0;
    c->char_delay = 0; c->pause_ticks = 0; c->ff50 = 0;
    c->script_p = ED_SCRIPT;
    /* the six ending pictures are staged, still packed, in the 64 KB scratch */
    cutscene_load_scratch(c, 1, R_END5, UNP_RAW, 0x0000);            /* 6663 */
    cutscene_load_scratch(c, 1, R_END4, UNP_RAW, 0x3400);
    cutscene_load_scratch(c, 1, R_END6, UNP_RAW, 0x5E00);
    cutscene_load_scratch(c, 1, R_END7, UNP_RAW, 0x8A00);
    cutscene_load_scratch(c, 1, R_EN72, UNP_RAW, 0xB800);
    cutscene_load_scratch(c, 1, R_FIN,  UNP_RAW, 0xE200);
    gd_set_palette(g, 7);
    audio_music_play_res(0, R_ZEND - 1);                             /* 66BC */
    end_run_script(c);                                               /* 66C5 */
done:
    audio_music_stop();
    return 0;
}
