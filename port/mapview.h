/* mapview.h — the M-key map viewer's pure half: layout, pan/wrap arithmetic and
 * the rasteriser that paints the *current* cavern map from the game's own data
 * (Map.grid + the loaded Tileset, i.e. the real tile art, drawn 8x8 per cell).
 *
 * Nothing here knows about SDL or the engine framebuffer; main.c uploads the
 * raster as a texture and draws it, with the markers, in the presentation layer
 * only (like the save-state OSD), so traces and `make verify` cannot see it.
 * test_mapview.c covers this file.
 *
 * Geometry: the map is mw x mh cells (mh = 64) and wraps on both axes.  A view
 * shows vw x vh of them at `cs` window pixels per cell.  When the whole map
 * fits at a readable size (cs >= MAPVIEW_CS_MIN) that axis is not pannable and
 * the origin is 0; otherwise cs is pinned to the minimum and the view scrolls
 * with wrap-around. */
#ifndef ZEL_MAPVIEW_H
#define ZEL_MAPVIEW_H
#include <stdint.h>
#include "map.h"
#include "gfx.h"

#define MAPVIEW_CS_MIN 4            /* smallest readable cell, window pixels */
#define MAPVIEW_CS_MAX 16
#define MAPVIEW_CELL_PX 8           /* the raster is 8 px per map cell */

typedef struct {
    int mw, mh;                     /* map size in cells */
    int cs;                         /* window pixels per cell */
    int vw, vh;                     /* cells visible (<= mw, mh) */
    int x0, y0;                     /* window pixel of the view's top-left (centred in the area) */
    int pan_x, pan_y;               /* 1: the map is larger than the view on this axis */
} MapViewLayout;

/* Fit a mw x mh map into the area (ax, ay, aw, ah) of the window. */
void mapview_layout(MapViewLayout *L, int ax, int ay, int aw, int ah, int mw, int mh);

/* v wrapped into [0, n) (n > 0). */
int  mapview_wrap(int v, int n);

/* The visible cells along one axis as at most two runs of the wrapped map:
 * seg[i] = { first map cell, count, offset in cells from the view's edge }.
 * Returns the run count (1 or 2). */
typedef struct { int src, len, dst; } MapViewSeg;
int  mapview_segments(int origin, int vis, int n, MapViewSeg seg[2]);

/* Move the origin by (dx, dy) cells; an axis that does not pan stays at 0, one
 * that does wraps. */
void mapview_pan(const MapViewLayout *L, int *ox, int *oy, int dx, int dy);
/* Origin that puts map cell (col, row) at the middle of the view. */
void mapview_center(const MapViewLayout *L, int col, int row, int *ox, int *oy);
/* Window pixel of the top-left of map cell (col,row), or 0 when it is not in
 * view (wrap-aware).  A cell that straddles the seam is placed at its first run. */
int  mapview_cell_to_px(const MapViewLayout *L, int ox, int oy, int col, int row, int *px, int *py);
/* Inverse: the map cell under window pixel (px, py), or 0 outside the view. */
int  mapview_px_to_cell(const MapViewLayout *L, int ox, int oy, int px, int py, int *col, int *row);

/* Paint the whole map into out[(mw*8) * (mh*8)] as 0xAARRGGBB: each grid cell
 * through the tile bank (cell value 0 or a value the bank does not hold stays
 * the background colour), then the lift/gate/patrol fixtures at their start
 * positions.  Returns 0, or -1 on a bad argument. */
#define MAPVIEW_BG 0xFF05060Au
int  mapview_raster(uint32_t *out, const Map *m, const Tileset *t);

#endif
