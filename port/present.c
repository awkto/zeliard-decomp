/* present.c — the display filters (see present.h, issue #53).
 *
 * The geometry and the name table are plain C so a headless build can still
 * parse `--filter`; only the actual blit needs SDL. */
#include "present.h"

/* -std=c11 hides POSIX strcasecmp, and the names are ASCII */
static int name_eq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        int x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return *a == *b;
}

int present_filter_by_name(const char *name)
{
    if (!name) return -1;
    if (name_eq(name, "integer")) return FILTER_INTEGER;
    if (name_eq(name, "sharp") || name_eq(name, "sharp-bilinear")) return FILTER_SHARP;
    if (name_eq(name, "smooth") || name_eq(name, "linear") || name_eq(name, "soft")) return FILTER_SMOOTH;
    if (name_eq(name, "scanlines")) return FILTER_SCANLINES;
    if (name_eq(name, "soft-scanlines")) return FILTER_SOFT_SCANLINES;
    if (name_eq(name, "crt")) return FILTER_CRT;
    if (name_eq(name, "crt-curved") || name_eq(name, "curved")) return FILTER_CRT_CURVED;
    return -1;
}

const char *present_filter_name(int filter)
{
    switch (filter) {
    case FILTER_INTEGER: return "integer";
    case FILTER_SHARP:   return "sharp-bilinear";
    case FILTER_SMOOTH:  return "smooth";
    case FILTER_SCANLINES:      return "scanlines";
    case FILTER_SOFT_SCANLINES: return "soft-scanlines";
    case FILTER_CRT:     return "crt";
    case FILTER_CRT_CURVED:     return "crt-curved";
    default:             return "?";
    }
}

void present_content_rect(int filter, int win_w, int win_h,
                          int sw, int sh, int dw, int dh,
                          int *x, int *y, int *w, int *h)
{
    present_content_rect_ex(filter, win_w, win_h, sw, sh, dw, dh, 0, x, y, w, h);
}

static int g_top;

void present_set_portrait_top(int on) { g_top = on != 0; }

void present_content_rect_ex(int filter, int win_w, int win_h,
                             int sw, int sh, int dw, int dh, int top,
                             int *x, int *y, int *w, int *h)
{
    int uw = (filter == FILTER_INTEGER) ? sw : dw;      /* the unit to scale/fit */
    int uh = (filter == FILTER_INTEGER) ? sh : dh;
    if (uw <= 0 || uh <= 0 || win_w <= 0 || win_h <= 0) {
        if (x) *x = 0;
        if (y) *y = 0;
        if (w) *w = 0;
        if (h) *h = 0;
        return;
    }
    int cw, ch;
    int n = (filter == FILTER_INTEGER) ? win_w / uw : 1;
    if (filter == FILTER_INTEGER && win_h / uh < n) n = win_h / uh;
    if (filter == FILTER_INTEGER && n >= 1) {
        cw = uw * n;
        ch = uh * n;
    } else if ((long long)win_w * uh <= (long long)win_h * uw) {
        cw = win_w; ch = win_w * uh / uw;           /* width-bound (or <1:1 fallback) */
    } else {
        ch = win_h; cw = win_h * uw / uh;           /* height-bound */
    }
    if (x) *x = (win_w - cw) / 2;
    if (y) *y = (top && win_h > win_w) ? 0 : (win_h - ch) / 2;
    if (w) *w = cw;
    if (h) *h = ch;
}

static int g_zoom;
void present_set_zoom(int on) { g_zoom = on != 0; }

void present_zoom_rect(int sw, int sh, int *x, int *y, int *w, int *h)
{
    int rx = 0, ry = 0, rw = 0, rh = 0;
    if (sw > 0 && sh > 0) {
        rx = PRESENT_ZOOM_X * sw / PRESENT_ZOOM_REF_W;
        ry = PRESENT_ZOOM_Y * sh / PRESENT_ZOOM_REF_H;
        rw = (PRESENT_ZOOM_X + PRESENT_ZOOM_W) * sw / PRESENT_ZOOM_REF_W - rx;
        rh = (PRESENT_ZOOM_Y + PRESENT_ZOOM_H) * sh / PRESENT_ZOOM_REF_H - ry;
    }
    if (x) *x = rx;
    if (y) *y = ry;
    if (w) *w = rw;
    if (h) *h = rh;
}

void present_view(int filter, int win_w, int win_h, int sw, int sh, int dw, int dh,
                  int top, int zoom,
                  int *x, int *y, int *w, int *h,
                  int *sx, int *sy, int *sw_, int *sh_)
{
    int cx = 0, cy = 0, cw = sw, ch = sh;
    if (zoom && sw > 0 && sh > 0) {
        present_zoom_rect(sw, sh, &cx, &cy, &cw, &ch);
        /* the display shape of the cropped picture keeps the pixel aspect */
        dw = dw * cw / sw;
        dh = dh * ch / sh;
    }
    present_content_rect_ex(filter, win_w, win_h, zoom ? cw : sw, zoom ? ch : sh, dw, dh, top,
                            x, y, w, h);
    if (sx) *sx = cx;
    if (sy) *sy = cy;
    if (sw_) *sw_ = cw;
    if (sh_) *sh_ = ch;
}

int present_prescale(int cw, int ch, int sw, int sh)
{
    if (sw <= 0 || sh <= 0) return 1;
    int n = cw / sw;
    if (ch / sh < n) n = ch / sh;
    return n < 1 ? 1 : n;
}

int present_crt_row_gain(int y, int ch, int sh)
{
    if (ch <= 0 || sh <= 0 || ch < 3 * sh || y < 0) return 255;
    int frac = (int)((long long)y * sh % ch * 256 / ch);    /* 0..255 across one source line */
    int d = frac < 128 ? 128 - frac : frac - 128;           /* 0 at the centre .. 128 at the seam */
    return 255 - PRESENT_CRT_SCAN_DARK * d / 128;
}

int present_crt_col_gain(int x, int c, int cw, int sw)
{
    if (cw <= 0 || sw <= 0 || cw < 3 * sw || x < 0) return 255;
    return (x % 3 == c) ? 255 : 255 - PRESENT_CRT_MASK_DARK;
}

uint32_t present_crt_mask(int x, int y, int cw, int ch, int sw, int sh)
{
    uint32_t g = (uint32_t)present_crt_row_gain(y, ch, sh);
    uint32_t r = g * (uint32_t)present_crt_col_gain(x, 0, cw, sw) / 255;
    uint32_t gg = g * (uint32_t)present_crt_col_gain(x, 1, cw, sw) / 255;
    uint32_t b = g * (uint32_t)present_crt_col_gain(x, 2, cw, sw) / 255;
    return 0xFF000000u | r << 16 | gg << 8 | b;
}

uint32_t present_scan_mask(int x, int y, int cw, int ch, int sw, int sh)
{
    uint32_t g = (uint32_t)present_crt_row_gain(y, ch, sh);
    (void)x; (void)cw; (void)sw;
    return 0xFF000000u | g << 16 | g << 8 | g;
}

void present_curve_map(float u, float v, float *x, float *y)
{
    float a = u * 2 - 1, b = v * 2 - 1;
    float s = (1 + PRESENT_CURVE_K) / (1 + PRESENT_CURVE_K * (a * a + b * b));
    if (x) *x = (a * s + 1) * 0.5f;
    if (y) *y = (b * s + 1) * 0.5f;
}

int present_vignette_alpha(float u, float v)
{
    float a = u * 2 - 1, b = v * 2 - 1;
    float r2 = (a * a + b * b) * 0.5f;          /* 0 centre .. 1 corner */
    float d = r2 * r2 * 0.75f;
    if (d > 1) d = 1;
    return (int)(d * 255 + 0.5f);
}

#ifdef HAVE_SDL
#include <SDL.h>
#include <stdlib.h>

static SDL_Texture *g_target;
static int g_tw, g_th;
static SDL_Texture *g_mask;          /* the CRT multiply mask, cached per geometry */
static int g_mw, g_mh, g_msw, g_msh, g_mgrille;
static SDL_Texture *g_vig;           /* the crt-curved vignette (small, stretched) */
static SDL_Texture *g_flat;          /* crt-curved: the flat picture before the warp */
static int g_fw, g_fh;

static SDL_Texture *mask_ensure(SDL_Renderer *ren, int cw, int ch, int sw, int sh, int grille)
{
    if (g_mask && g_mw == cw && g_mh == ch && g_msw == sw && g_msh == sh && g_mgrille == grille) return g_mask;
    if (g_mask) { SDL_DestroyTexture(g_mask); g_mask = NULL; }
    SDL_Texture *t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_STATIC, cw, ch);
    uint32_t *px = t ? malloc((size_t)cw * ch * 4) : NULL;
    if (!px) { if (t) SDL_DestroyTexture(t); return NULL; }
    for (int y = 0; y < ch; y++)
        for (int x = 0; x < cw; x++)
            px[(size_t)y * cw + x] = grille ? present_crt_mask(x, y, cw, ch, sw, sh)
                                            : present_scan_mask(x, y, cw, ch, sw, sh);
    SDL_UpdateTexture(t, NULL, px, cw * 4);
    free(px);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_MOD);
    SDL_SetTextureScaleMode(t, SDL_ScaleModeNearest);
    g_mask = t; g_mw = cw; g_mh = ch; g_msw = sw; g_msh = sh; g_mgrille = grille;
    return g_mask;
}

static int target_ensure(SDL_Renderer *ren, int w, int h)
{
    if (g_target && g_tw == w && g_th == h) return 0;
    if (g_target) { SDL_DestroyTexture(g_target); g_target = NULL; }
    g_target = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                 SDL_TEXTUREACCESS_TARGET, w, h);
    if (!g_target) return -1;
    g_tw = w; g_th = h;
    return 0;
}

/* Draw the filter's base picture (src through nearest-prescale+linear, or plain
 * linear) into `dst` of the current render target, then the multiply mask. */
static void draw_flat(SDL_Renderer *ren, SDL_Texture *src, int sw, int sh,
                      const SDL_Rect *dst, int sharp, int mask,   /* mask: 0 none, 1 scan, 2 scan+grille */
                      const SDL_Rect *crop)                       /* NULL = the whole source */
{
    int cw = crop ? crop->w : sw, ch = crop ? crop->h : sh;
    int done = 0;
    SDL_Texture *cur = SDL_GetRenderTarget(ren);
    if (sharp) {
        int n = present_prescale(dst->w, dst->h, cw, ch);
        if (n > 1 && target_ensure(ren, sw * n, sh * n) == 0 && cur != g_target) {
            if (SDL_SetRenderTarget(ren, g_target) == 0) {
                SDL_SetTextureScaleMode(src, SDL_ScaleModeNearest);
                SDL_RenderCopy(ren, src, NULL, NULL);    /* integer pre-scale */
                SDL_SetRenderTarget(ren, cur);
                SDL_SetTextureScaleMode(g_target, SDL_ScaleModeLinear);
                SDL_Rect cs = { 0, 0, 0, 0 };
                if (crop) { cs.x = crop->x * n; cs.y = crop->y * n; cs.w = crop->w * n; cs.h = crop->h * n; }
                SDL_RenderCopy(ren, g_target, crop ? &cs : NULL, dst);   /* to the output */
                done = 1;
            } else SDL_SetRenderTarget(ren, cur);
        }
        /* a 1x pre-scale (or no render-target support) is just smooth */
    }
    if (!done) {
        SDL_SetTextureScaleMode(src, SDL_ScaleModeLinear);
        SDL_RenderCopy(ren, src, crop, dst);
    }
    if (mask) {
        SDL_Texture *m = mask_ensure(ren, dst->w, dst->h, cw, ch, mask == 2);
        if (m) SDL_RenderCopy(ren, m, NULL, dst);
    }
}

#if SDL_VERSION_ATLEAST(2, 0, 18)
#define CURVE_NX 40
#define CURVE_NY 30

static SDL_Texture *vig_ensure(SDL_Renderer *ren)
{
    if (g_vig) return g_vig;
    enum { VW = 64, VH = 48 };
    uint32_t px[VW * VH];
    for (int y = 0; y < VH; y++)
        for (int x = 0; x < VW; x++)
            px[y * VW + x] = (uint32_t)present_vignette_alpha((x + 0.5f) / VW, (y + 0.5f) / VH) << 24;
    SDL_Texture *t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, VW, VH);
    if (!t) return NULL;
    SDL_UpdateTexture(t, NULL, px, VW * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
    g_vig = t;
    return t;
}

static int flat_ensure(SDL_Renderer *ren, int w, int h)
{
    if (g_flat && g_fw == w && g_fh == h) return 0;
    if (g_flat) { SDL_DestroyTexture(g_flat); g_flat = NULL; }
    g_flat = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
    if (!g_flat) return -1;
    SDL_SetTextureScaleMode(g_flat, SDL_ScaleModeLinear);
    g_fw = w; g_fh = h;
    return 0;
}

/* crt-curved: the flat crt picture into g_flat, then a (NX x NY) mesh whose
 * vertices sit at the barrel-mapped positions with the flat UVs, then the
 * vignette over the whole rectangle.  Returns 0, or -1 to fall back to flat. */
static int draw_curved(SDL_Renderer *ren, SDL_Texture *src, int sw, int sh, const SDL_Rect *dst,
                       const SDL_Rect *crop)
{
    if (flat_ensure(ren, dst->w, dst->h) != 0) return -1;
    SDL_Rect flat = { 0, 0, dst->w, dst->h };
    if (SDL_SetRenderTarget(ren, g_flat) != 0) return -1;
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    draw_flat(ren, src, sw, sh, &flat, 1, 2, crop);
    SDL_SetRenderTarget(ren, NULL);

    static SDL_Vertex vt[(CURVE_NX + 1) * (CURVE_NY + 1)];
    static int idx[CURVE_NX * CURVE_NY * 6];
    SDL_Color white = { 255, 255, 255, 255 };
    for (int j = 0; j <= CURVE_NY; j++)
        for (int i = 0; i <= CURVE_NX; i++) {
            float u = (float)i / CURVE_NX, v = (float)j / CURVE_NY, px, py;
            present_curve_map(u, v, &px, &py);
            SDL_Vertex *q = &vt[j * (CURVE_NX + 1) + i];
            q->position.x = dst->x + px * dst->w;
            q->position.y = dst->y + py * dst->h;
            q->color = white;
            q->tex_coord.x = u;
            q->tex_coord.y = v;
        }
    int n = 0;
    for (int j = 0; j < CURVE_NY; j++)
        for (int i = 0; i < CURVE_NX; i++) {
            int a = j * (CURVE_NX + 1) + i, b = a + 1, c = a + CURVE_NX + 1, d = c + 1;
            idx[n++] = a; idx[n++] = b; idx[n++] = c;
            idx[n++] = b; idx[n++] = d; idx[n++] = c;
        }
    if (SDL_RenderGeometry(ren, g_flat, vt, (CURVE_NX + 1) * (CURVE_NY + 1), idx, n) != 0) return -1;
    SDL_Texture *vg = vig_ensure(ren);
    if (vg) SDL_RenderCopy(ren, vg, NULL, dst);
    return 0;
}
#endif

int present_render(SDL_Renderer *ren, SDL_Texture *src,
                   int sw, int sh, int dw, int dh, int filter)
{
    int ow = 0, oh = 0;
    if (SDL_GetRendererOutputSize(ren, &ow, &oh) != 0) return -1;
    int x, y, w, h;
    SDL_Rect crop;
    present_view(filter, ow, oh, sw, sh, dw, dh, g_top, g_zoom, &x, &y, &w, &h,
                 &crop.x, &crop.y, &crop.w, &crop.h);
    if (w <= 0 || h <= 0) return -1;
    SDL_Rect dst = { x, y, w, h };
    const SDL_Rect *cp = g_zoom ? &crop : NULL;

    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);                            /* the letterbox bars */

    switch (filter) {
    case FILTER_INTEGER:
        SDL_SetTextureScaleMode(src, SDL_ScaleModeNearest);
        SDL_RenderCopy(ren, src, cp, &dst);
        break;
    case FILTER_SHARP:          draw_flat(ren, src, sw, sh, &dst, 1, 0, cp); break;
    case FILTER_SCANLINES:      draw_flat(ren, src, sw, sh, &dst, 1, 1, cp); break;
    case FILTER_SOFT_SCANLINES: draw_flat(ren, src, sw, sh, &dst, 0, 1, cp); break;
    case FILTER_CRT:            draw_flat(ren, src, sw, sh, &dst, 1, 2, cp); break;
    case FILTER_CRT_CURVED:
#if SDL_VERSION_ATLEAST(2, 0, 18)
        if (draw_curved(ren, src, sw, sh, &dst, cp) == 0) break;
        SDL_SetRenderTarget(ren, NULL);
        SDL_RenderClear(ren);
#endif
        draw_flat(ren, src, sw, sh, &dst, 1, 2, cp);
        break;
    default:                    draw_flat(ren, src, sw, sh, &dst, 0, 0, cp); break;
    }
    return 0;
}

void present_shutdown(void)
{
    if (g_target) { SDL_DestroyTexture(g_target); g_target = NULL; }
    g_tw = g_th = 0;
    if (g_mask) { SDL_DestroyTexture(g_mask); g_mask = NULL; }
    g_mw = g_mh = g_msw = g_msh = g_mgrille = 0;
    if (g_vig) { SDL_DestroyTexture(g_vig); g_vig = NULL; }
    if (g_flat) { SDL_DestroyTexture(g_flat); g_flat = NULL; }
    g_fw = g_fh = 0;
}
#endif
