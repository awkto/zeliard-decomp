/* smooth.h -- opt-in smooth scrolling (issue #127).  Presentation only; SDL-free.
 *
 * The game steps ~12 times a second and moves the hero, the scroll, enemies and
 * shots by whole 8 px cells, so the picture jumps.  Smooth mode keeps input
 * sampling and the step exactly where they are and shows the new state at once,
 * but draws it *displaced back to where the previous step had it* and slides it
 * to its true place over the step's interval:
 *
 *      offset(t) = (previous position - current position) * (1 - t)      t = 0..1
 *
 * t runs from the step to the next one's due time.  The first picture of a step
 * is the old position (so motion starts the moment the step happens, with no
 * latency over the unsmoothed game) and the last is the settled one.  This is
 * deliberately NOT previous->current interpolation of two states shown late: the
 * newest state is on screen from the first present, only its 8 px landing is
 * spread out.  A held key scrolls at constant speed because the slide is linear.
 *
 * What slides: the world (background tiles, from the scroll delta, wrapping at
 * the map seam and the 64-row ring), the hero (his own screen-cell delta, which
 * is nonzero where the camera does not follow him), and each enemy, shot and
 * spell sprite (its screen-cell delta since the previous step, matched by record
 * index).  An object with no usable history rides the world's slide, and a jump
 * of more than SMOOTH_MAX_PX is a teleport, not a slide, and is left alone.
 * The HUD and frame never move: only the playfield is redrawn.
 *
 * Offsets are whole framebuffer pixels, so they work through every filter and
 * zoom with no change to present.c (a 320x200 source pixel is the unit those
 * sample in anyway).  The engine's framebuffer is never involved: render.c only
 * honours the offsets while render_set_smooth() holds some. */
#ifndef ZEL_SMOOTH_H
#define ZEL_SMOOTH_H
#include "render.h"

#define SMOOTH_MAX_PX 24        /* a per-object move larger than this (3 cells) is a jump */

typedef struct SmoothPos { int8_t ok; int8_t type; int16_t x, y; } SmoothPos;   /* screen cell */

typedef struct Smooth {
    int           have;                     /* prev holds a step */
    const void   *map;
    int           scroll_col, scroll_row, hero_c, hero_r;
    SmoothPos     obj[MAX_OBJS], shot[MAX_SHOTS + 1], mag[4];
    SmoothOfs     start;                    /* the offsets at t = 0 of the current step */
} Smooth;

/* wrapped signed difference a - b on a ring of n (n = 64 for rows, the map width for columns) */
int  smooth_wrap_delta(int a, int b, int n);
/* the part of a move of `px` pixels still to go at progress t (0 = just stepped, 1 = settled),
 * rounded to the nearest pixel, ties away from zero; t outside 0..1 is clamped */
int  smooth_remaining(int px, double t);
/* progress through a step that began `elapsed_ms` ago and lasts `period_ms` */
double smooth_progress(double elapsed_ms, double period_ms);
/* is a per-object move of (dx, dy) pixels a slide (else a jump)? */
int  smooth_is_slide(int dx, int dy);

void smooth_reset(Smooth *s);               /* forget history: a map change, a menu, fast-forward */
/* Call once per game step, after the step: derives s->start from the previous step's
 * snapshot, then keeps this step as the next one's history. */
void smooth_step(Smooth *s, const Game *g);
/* s->start scaled to progress t. */
void smooth_at(const Smooth *s, double t, SmoothOfs *out);
/* do two offset sets draw identically? */
int  smooth_same(const SmoothOfs *a, const SmoothOfs *b);

#endif
