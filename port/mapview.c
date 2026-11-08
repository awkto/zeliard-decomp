/* mapview.c — see mapview.h. */
#include "mapview.h"

int mapview_wrap(int v, int n)
{
    if (n <= 0) return 0;
    v %= n;
    return v < 0 ? v + n : v;
}

void mapview_layout(MapViewLayout *L, int ax, int ay, int aw, int ah, int mw, int mh)
{
    L->mw = mw; L->mh = mh;
    if (mw < 1) mw = 1;
    if (mh < 1) mh = 1;
    if (aw < 1) aw = 1;
    if (ah < 1) ah = 1;
    int cs = aw / mw;
    if (ah / mh < cs) cs = ah / mh;
    if (cs > MAPVIEW_CS_MAX) cs = MAPVIEW_CS_MAX;
    if (cs < MAPVIEW_CS_MIN) cs = MAPVIEW_CS_MIN;
    L->cs = cs;
    L->vw = aw / cs; if (L->vw > mw) L->vw = mw; if (L->vw < 1) L->vw = 1;
    L->vh = ah / cs; if (L->vh > mh) L->vh = mh; if (L->vh < 1) L->vh = 1;
    L->pan_x = L->vw < mw;
    L->pan_y = L->vh < mh;
    L->x0 = ax + (aw - L->vw * cs) / 2;
    L->y0 = ay + (ah - L->vh * cs) / 2;
}

int mapview_segments(int origin, int vis, int n, MapViewSeg seg[2])
{
    if (n <= 0 || vis <= 0) { seg[0].src = seg[0].len = seg[0].dst = 0; return 1; }
    if (vis > n) vis = n;
    int o = mapview_wrap(origin, n);
    if (o + vis <= n) {
        seg[0].src = o; seg[0].len = vis; seg[0].dst = 0;
        return 1;
    }
    seg[0].src = o; seg[0].len = n - o; seg[0].dst = 0;
    seg[1].src = 0; seg[1].len = vis - (n - o); seg[1].dst = n - o;
    return 2;
}

void mapview_pan(const MapViewLayout *L, int *ox, int *oy, int dx, int dy)
{
    *ox = L->pan_x ? mapview_wrap(*ox + dx, L->mw) : 0;
    *oy = L->pan_y ? mapview_wrap(*oy + dy, L->mh) : 0;
}

void mapview_center(const MapViewLayout *L, int col, int row, int *ox, int *oy)
{
    *ox = L->pan_x ? mapview_wrap(col - L->vw / 2, L->mw) : 0;
    *oy = L->pan_y ? mapview_wrap(row - L->vh / 2, L->mh) : 0;
}

int mapview_cell_to_px(const MapViewLayout *L, int ox, int oy, int col, int row, int *px, int *py)
{
    int dx = mapview_wrap(col - ox, L->mw), dy = mapview_wrap(row - oy, L->mh);
    if (dx >= L->vw || dy >= L->vh) return 0;
    *px = L->x0 + dx * L->cs;
    *py = L->y0 + dy * L->cs;
    return 1;
}

int mapview_px_to_cell(const MapViewLayout *L, int ox, int oy, int px, int py, int *col, int *row)
{
    int cx = px - L->x0, cy = py - L->y0;
    if (cx < 0 || cy < 0 || cx >= L->vw * L->cs || cy >= L->vh * L->cs) return 0;
    *col = mapview_wrap(ox + cx / L->cs, L->mw);
    *row = mapview_wrap(oy + cy / L->cs, L->mh);
    return 1;
}

static void put_cell(uint32_t *out, int stride, int col, int row, const Cell8 *c)
{
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            uint8_t v = c->px[y][x] & 63;
            uint32_t rgb = v ? (uint32_t)PAL_RGB[v][0] << 16 | (uint32_t)PAL_RGB[v][1] << 8 | PAL_RGB[v][2] : 0u;
            out[(size_t)(row * 8 + y) * stride + col * 8 + x] = v ? 0xFF000000u | rgb : MAPVIEW_BG;
        }
}

int mapview_raster(uint32_t *out, const Map *m, const Tileset *t)
{
    if (!out || !m || !t || m->width < 1 || m->width > MAP_MAX_WIDTH) return -1;
    gfx_pal_init();
    int stride = m->width * 8;
    for (size_t i = 0; i < (size_t)stride * MAP_ROWS * 8; i++) out[i] = MAPVIEW_BG;
    for (int c = 0; c < m->width; c++)
        for (int r = 0; r < MAP_ROWS; r++) {
            uint8_t v = m->grid[c][r];
            if (v && v < BANK_CELLS && t->present[v]) put_cell(out, stride, c, r, &t->cell[v]);
        }
    for (int i = 0; i < m->nfix; i++) {         /* three DCHR cells side by side */
        const Fixture *f = &m->fix[i];
        int col = f->col & 0x3FFF, row = f->row & 0x3F;
        for (int k = 0; k < 3; k++) {
            int cell = f->cell + k, cc = (col + k) % m->width;
            if (cell < BANK_CELLS && t->present[cell]) put_cell(out, stride, cc, row, &t->cell[cell]);
        }
    }
    return 0;
}
