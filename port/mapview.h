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

/* ------------------------------------------------------------------------
 * Annotations (#128).  Everything below is pure logic over the game's own
 * records -- door tables, the tile bank's classification lists, the fixture
 * and object records -- so the M viewer can say where a door goes, which way
 * the air moves, how far a lift travels and what lies in a chest, without a
 * single hand-entered coordinate.  main.c only draws what these return.
 * ------------------------------------------------------------------------ */

/* The doors and item records of every system map (STICK.BIN's 31-record table,
 * map.c SYSMAP), which pairing needs: a door's partner lives in another map. */
#define MAPVIEW_NMAPS 31
#define MAPVIEW_MAX_DOORS 64
#define MAPVIEW_MAX_ITEMS 128

typedef struct {
    uint16_t col;
    uint8_t  row, type, phase, next, flags;
    uint8_t  fbyte, fmask;          /* the +B/+D event pair: taking it ORs fmask into page[fbyte] (914C) */
    uint8_t  idx;                   /* its index in the map's object list (the live g->obj[] index) */
} MapViewObjRec;

typedef struct {
    int      ok;
    char     name[16];              /* "MP20" */
    char     place[24];             /* the HUD place name without "Cavern of ": "PELIGRO" */
    uint8_t  ai, cavern;
    int      width, boss_room;      /* boss_room: the level record's bit 7 (no doors until the boss is dead) */
    int      ndoors;  Door doors[MAPVIEW_MAX_DOORS];
    int      nitems;  MapViewObjRec items[MAPVIEW_MAX_ITEMS];
} MapViewInfo;

typedef struct { MapViewInfo m[MAPVIEW_NMAPS]; } MapViewWorld;

/* Fill one info from a parsed map (doors and every object record that is an
 * item state, i.e. type bit 4 set and still on the map). */
void mapview_info_set(MapViewInfo *in, const Map *m);
/* Load all 31 system maps from the game folder; returns how many loaded.  A
 * boss room is read in its *after-the-boss* state (the C00C patch record that
 * gives it its exit doors and its reward), because that is where its doors and
 * reward live; the maps are otherwise as the disk has them. */
int  mapview_world_load(MapViewWorld *w, const char *dir);
/* Replace one map's doors with the live ones (unlocked state, applied patches). */
void mapview_world_use_doors(MapViewWorld *w, int idx, const Map *live);
/* Index of the map called `name` ("MP20"), or -1. */
int  mapview_world_find(const MapViewWorld *w, const char *name);

/* Doors.  Every door gets a label from the records alone:
 *   TOWN    dest_row 0xFF: the town's name and which end of it the door opens on ("TUMBA W")
 *   BOSS    the destination is a boss room: "BOSS DRAGON"
 *   NUM     a door whose partner (the door in the destination map that leads straight
 *           back to this one's coordinates) is in another map: "3 RIZA".  The number
 *           ranks the pairs between those two maps by door order in the lower map, so
 *           both ends read the same.
 *   LETTER  the partner is in the same map: "A" at both ends
 *   LINK    no partner record: "TO PLACE" */
enum { MVD_TOWN, MVD_BOSS, MVD_NUM, MVD_LETTER, MVD_LINK };
typedef struct {
    int  kind;
    int  num;                       /* NUM: the pair number; LETTER: 0 for A, 1 for B ... */
    int  pmap, pdoor;               /* the partner, or -1 */
    char text[32];
} MapViewDoorLabel;
int  mapview_door_partner(const MapViewWorld *w, int map, int door, int *pmap, int *pdoor);
void mapview_door_labels(const MapViewWorld *w, int map, MapViewDoorLabel out[MAPVIEW_MAX_DOORS]);
const char *mapview_town_name(int town);        /* town = dest_map & 0x7F; NULL when unknown */
int  mapview_town_width(int town);
const char *mapview_boss_name(int ai);          /* the boss overlay index [ai] of a boss room, or NULL */

/* Air currents and belts: the tile bank's own classification lists (cell 0:
 * 76F6 updraft / left / right, 6A67 belts), grouped into rectangles -- runs of
 * one row for the sideways kinds, of one column for updrafts, equal neighbours
 * merged.  c0..c1 and r0..r1 are inclusive map cells; nothing joins across the
 * wrap seam. */
enum { MVC_NONE, MVC_UP, MVC_LEFT, MVC_RIGHT, MVC_BELT_L, MVC_BELT_R };
typedef struct { int kind, c0, r0, c1, r1; } MapViewRun;
int  mapview_cell_class(const Tileset *t, uint8_t v);
int  mapview_runs(const Map *m, const Tileset *t, MapViewRun *out, int max);

/* Lifts and platforms: the rectangle of cells a fixture sweeps.  An A elevator
 * moves a row at a time while the three cells it moves into are empty
 * (8024 / 80AF), so its range is read off the grid; a B gate only sinks, and
 * does not come back (oneway); a C platform patrols lim_l..lim_r (+2 for its
 * width).  w/h are cell counts from (col,row); col wraps with the map, row with
 * 64.  Returns 0 for a fixture that never moves. */
enum { MVF_ELEVATOR, MVF_GATE, MVF_PLATFORM };
typedef struct { int kind, col, row, w, h, vertical, oneway; } MapViewSpan;
int  mapview_fixture_span(const Map *m, const Fixture *f, MapViewSpan *s);

/* Items.  The object records whose state (type & 0x1F) is an item: chests with
 * their prize, Keys, potions, the key items, coins, breakable rocks and the
 * floor blocks that give way.  `taken` is set from the game's own state: the
 * event flag the record carries (page[fbyte] & fmask, which 914C sets and
 * the C00C patches key off, so it survives leaving the cavern) or, with a live
 * object list, a record that has been removed (col >= 0xFF00).  A rock that
 * has been broken shows what it dropped (the record's live type). */
enum { MVI_CHEST, MVI_KEY, MVI_LION, MVI_POTION, MVI_FULL, MVI_SHOES, MVI_CREST, MVI_ALMAS,
       MVI_MSG, MVI_ROCK, MVI_DROP };
typedef struct { int kind, col, row, taken; char label[24]; } MapViewItem;
int  mapview_items(const MapViewInfo *in, const MapObj *live, int nlive, const uint8_t *page,
                   MapViewItem *out, int max);

/* The footer legend: entries (widths in pixels) laid on at most `maxlines`
 * lines of `avail` pixels with `gap` between neighbours, in order; returns how
 * many fit (the rest are dropped, lowest priority last) and each one's line. */
int  mapview_legend_fit(const int *width, int n, int avail, int gap, int maxlines, int *line);

#endif
