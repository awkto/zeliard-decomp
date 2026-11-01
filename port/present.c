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
    if (name_eq(name, "smooth") || name_eq(name, "linear")) return FILTER_SMOOTH;
    return -1;
}

const char *present_filter_name(int filter)
{
    switch (filter) {
    case FILTER_INTEGER: return "integer";
    case FILTER_SHARP:   return "sharp-bilinear";
    case FILTER_SMOOTH:  return "smooth";
    default:             return "?";
    }
}

void present_content_rect(int filter, int win_w, int win_h,
                          int sw, int sh, int dw, int dh,
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
    if (y) *y = (win_h - ch) / 2;
    if (w) *w = cw;
    if (h) *h = ch;
}

int present_prescale(int cw, int ch, int sw, int sh)
{
    if (sw <= 0 || sh <= 0) return 1;
    int n = cw / sw;
    if (ch / sh < n) n = ch / sh;
    return n < 1 ? 1 : n;
}

#ifdef HAVE_SDL
#include <SDL.h>

static SDL_Texture *g_target;
static int g_tw, g_th;

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

int present_render(SDL_Renderer *ren, SDL_Texture *src,
                   int sw, int sh, int dw, int dh, int filter)
{
    int ow = 0, oh = 0;
    if (SDL_GetRendererOutputSize(ren, &ow, &oh) != 0) return -1;
    int x, y, w, h;
    present_content_rect(filter, ow, oh, sw, sh, dw, dh, &x, &y, &w, &h);
    if (w <= 0 || h <= 0) return -1;
    SDL_Rect dst = { x, y, w, h };

    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);                            /* the letterbox bars */

    if (filter == FILTER_INTEGER) {
        SDL_SetTextureScaleMode(src, SDL_ScaleModeNearest);
        SDL_RenderCopy(ren, src, NULL, &dst);
        return 0;
    }
    if (filter == FILTER_SHARP) {
        int n = present_prescale(w, h, sw, sh);
        if (n > 1 && target_ensure(ren, sw * n, sh * n) == 0) {
            if (SDL_SetRenderTarget(ren, g_target) == 0) {
                SDL_SetTextureScaleMode(src, SDL_ScaleModeNearest);
                SDL_RenderCopy(ren, src, NULL, NULL);    /* integer pre-scale */
                SDL_SetRenderTarget(ren, NULL);
                SDL_SetTextureScaleMode(g_target, SDL_ScaleModeLinear);
                SDL_RenderCopy(ren, g_target, NULL, &dst);   /* to the output */
                return 0;
            }
            SDL_SetRenderTarget(ren, NULL);
        }
        /* a 1x pre-scale (or no render-target support) is just smooth */
    }
    SDL_SetTextureScaleMode(src, SDL_ScaleModeLinear);
    SDL_RenderCopy(ren, src, NULL, &dst);
    return 0;
}

void present_shutdown(void)
{
    if (g_target) { SDL_DestroyTexture(g_target); g_target = NULL; }
    g_tw = g_th = 0;
}
#endif
