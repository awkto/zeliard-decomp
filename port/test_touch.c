/* test_touch.c — the on-screen touch controls (issue #54, port/touch.h).
 *
 * The layout, hit-test, joystick zones and multi-finger state are pure C, so
 * most of this runs without a window: it checks every control is reachable,
 * that two layouts (landscape 16:9 and portrait 9:16) keep every control inside
 * the window and non-overlapping, that fingers stay bound to the control they
 * began on and are independent of each other, and the stick's eight sectors
 * (the diagonals are the jump-walks).  Like test_present it is outside `make
 * test`.  With SDL it can also write the overlay over a game frame:
 *
 * usage: test_touch                           run the checks
 *        test_touch --shot OUTDIR [frame.ppm]  also write OUTDIR/overlay_{landscape,portrait}.png
 *                                             over a P6 PPM game frame (a gradient if none) */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "touch.h"
#include "physics.h"
#include "present.h"

static int checks, failures;
static void ck(int ok, const char *fmt, ...)
{
    va_list ap;
    checks++;
    if (ok) return;
    failures++;
    printf("  FAIL ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

static const int SIZES[2][2] = { { 1280, 720 }, { 720, 1280 } };

static void centre(const TouchLayout *L, int id, int *x, int *y)
{
    *x = L->c[id].x + L->c[id].w / 2;
    *y = L->c[id].y + L->c[id].h / 2;
}

static void t_layout(void)
{
    printf("layout          ");
    for (int s = 0; s < 2; s++) {
        TouchLayout L;
        touch_layout(&L, SIZES[s][0], SIZES[s][1], 100);
        ck(L.portrait == (s == 1), "%dx%d portrait flag", L.win_w, L.win_h);
        int inside = 1;
        for (int i = 0; i < TC_COUNT; i++) {
            const TouchCtl *c = &L.c[i];
            if (c->x < 0 || c->y < 0 || c->x + c->w > L.win_w || c->y + c->h > L.win_h || c->w <= 0) inside = 0;
        }
        ck(inside, "%dx%d: every control lies inside the window", L.win_w, L.win_h);
        /* the stick sits bottom-left, sword/magic bottom-right, the key row on top */
        ck(L.stick_cx < L.win_w / 2 && L.stick_cy > L.win_h / 2, "%dx%d: stick bottom-left", L.win_w, L.win_h);
        ck(L.c[TC_SWORD].x > L.win_w / 2 && L.c[TC_MAGIC].x > L.stick_cx + L.stick_r, "%dx%d: sword/magic right of the stick", L.win_w, L.win_h);
        ck(L.c[TC_KEY0].y < L.win_h / 4, "%dx%d: digit row on top", L.win_w, L.win_h);
        /* no two controls overlap (bounding boxes; the stick's drawn ring counts) */
        int clash = 0;
        for (int i = 0; i < TC_COUNT; i++)
            for (int j = i + 1; j < TC_COUNT; j++) {
                const TouchCtl *a = &L.c[i], *b = &L.c[j];
                if (a->x < b->x + b->w && b->x < a->x + a->w && a->y < b->y + b->h && b->y < a->y + a->h) clash++;
            }
        ck(clash == 0, "%dx%d: %d overlapping control pairs", L.win_w, L.win_h, clash);
        /* the digit row keeps its order: 1..9 Y N, left to right */
        int ordered = 1;
        for (int i = 1; i < TC_NKEYS; i++) if (L.c[TC_KEY0 + i].x <= L.c[TC_KEY0 + i - 1].x) ordered = 0;
        ck(ordered && L.c[TC_KEY0].label[0] == '1' && L.c[TC_KEY0 + 9].label[0] == 'Y' && L.c[TC_KEY0 + 10].label[0] == 'N',
           "%dx%d: digit row is 1..9 Y N", L.win_w, L.win_h);
    }
    /* size scales the controls; the extremes clamp */
    TouchLayout a, b, c;
    touch_layout(&a, 1280, 720, 100); touch_layout(&b, 1280, 720, 200); touch_layout(&c, 1280, 720, 9999);
    ck(b.stick_r > a.stick_r && c.stick_r == b.stick_r, "touch.size scales the stick (%d -> %d), clamped at 200%%", a.stick_r, b.stick_r);
    touch_layout(&a, 1, 1, 100);
    ck(a.unit >= 8, "a degenerate window does not crash or collapse to zero");
}

/* ---------------------------------------- layout round the game picture */
static void picture(int w, int h, int *cx, int *cy, int *cw, int *ch)
{
    present_content_rect_ex(FILTER_SMOOTH, w, h, 320, 200, 320, 240, 1, cx, cy, cw, ch);
}

static int meets_pic(const TouchCtl *c, const TouchLayout *L)
{
    return c->x < L->cx + L->cw && L->cx < c->x + c->w && c->y < L->cy + L->ch && L->cy < c->y + c->h;
}

static void t_layout_in(void)
{
    printf("layout in view  ");
    static const struct { const char *name; int w, h, overlay; } S[] = {
        { "16:9 landscape", 1280, 720, 1 },     /* 160 px pillars: too narrow, so overlay */
        { "20:9 landscape", 2400, 1080, 0 },    /* 480 px pillars: the controls fit beside the picture */
        { "phone portrait 9:19.5", 390, 844, 0 },
        { "tablet 4:3", 1024, 768, 1 },         /* the picture fills the window */
        { "tall portrait 9:16", 720, 1280, 0 },
    };
    for (unsigned s = 0; s < sizeof S / sizeof S[0]; s++) {
        int cx, cy, cw, ch;
        picture(S[s].w, S[s].h, &cx, &cy, &cw, &ch);
        TouchLayout L, nom;
        touch_layout_in(&L, S[s].w, S[s].h, 100, cx, cy, cw, ch);
        touch_layout(&nom, S[s].w, S[s].h, 100);
        int inside = 1, hits = 0, clash = 0, over = 0;
        for (int i = 0; i < TC_COUNT; i++) {
            const TouchCtl *c = &L.c[i];
            if (c->x < 0 || c->y < 0 || c->x + c->w > L.win_w || c->y + c->h > L.win_h || c->w <= 0) inside = 0;
            hits += meets_pic(c, &L);
            over += c->over;
            for (int j = i + 1; j < TC_COUNT; j++) {
                const TouchCtl *b = &L.c[j];
                if (c->x < b->x + b->w && b->x < c->x + c->w && c->y < b->y + b->h && b->y < c->y + c->h) clash++;
            }
        }
        ck(inside && clash == 0, "%s: controls inside the window, none overlapping (%d clashes)", S[s].name, clash);
        ck(L.overlay == S[s].overlay, "%s: overlay=%d, expected %d", S[s].name, L.overlay, S[s].overlay);
        if (!L.overlay) {
            ck(hits == 0 && over == 0, "%s: no control touches the picture (%d)", S[s].name, hits);
            ck(L.unit * 10 >= nom.unit * 7, "%s: controls not shrunk below 70%% (%d vs %d)", S[s].name, L.unit, nom.unit);
        } else {
            ck(over > 0 && L.unit == nom.unit, "%s: overlay keeps full size and flags what covers the picture (%d)", S[s].name, over);
        }
    }
    int cx, cy, cw, ch;
    TouchLayout L;
    /* portrait: the picture sits at the top, everything is below it, digits first */
    picture(390, 844, &cx, &cy, &cw, &ch);
    touch_layout_in(&L, 390, 844, 100, cx, cy, cw, ch);
    ck(cy == 0 && cw == 390 && ch == 292, "phone portrait: picture at the top, 390x292 (y=%d, %dx%d)", cy, cw, ch);
    ck(L.c[TC_KEY0].y >= ch && L.c[TC_ESC].y > L.c[TC_KEY0].y && L.stick_cy > L.c[TC_ESC].y, "phone portrait: digits, menu row, then stick, all under the picture");
    /* the legacy call (picture = whole window) is the plain overlay */
    touch_layout(&L, 1280, 720, 100);
    ck(L.overlay && L.c[TC_KEY0].over, "touch_layout() without a picture is the overlay layout");
    /* landscape beside the picture: stick in the left column, buttons and menu keys in the right */
    picture(2400, 1080, &cx, &cy, &cw, &ch);
    touch_layout_in(&L, 2400, 1080, 100, cx, cy, cw, ch);
    ck(L.c[TC_STICK].x + L.c[TC_STICK].w <= cx && L.c[TC_KEY0 + 10].x + L.c[TC_KEY0 + 10].w <= cx, "20:9: stick and digits in the left column");
    ck(L.c[TC_SWORD].x >= cx + cw && L.c[TC_MAGIC].x >= cx + cw && L.c[TC_ESC].x >= cx + cw && L.c[TC_QLOAD].x >= cx + cw, "20:9: sword, magic and menu keys in the right column");
    ck(touch_hit(&L, L.c[TC_KEY0 + 4].x + 2, L.c[TC_KEY0 + 4].y + 2) == TC_KEY0 + 4, "20:9: the gridded digits are hittable");
    /* a phone held sideways: 844x390 -> 520 px picture, 162 px columns, scaled to fit */
    picture(844, 390, &cx, &cy, &cw, &ch);
    touch_layout_in(&L, 844, 390, 100, cx, cy, cw, ch);
    ck(!L.overlay, "844x390 landscape phone: controls fit in the columns (unit %d)", L.unit);
    /* touch_to_frame maps the picture to the 320x200 frame */
    int fx, fy;
    picture(390, 844, &cx, &cy, &cw, &ch);
    touch_layout_in(&L, 390, 844, 100, cx, cy, cw, ch);
    ck(touch_to_frame(&L, 195, 146, 320, 200, &fx, &fy) && fx == 160 && fy == 100, "touch_to_frame: the centre of the picture is (160,100) (%d,%d)", fx, fy);
    ck(!touch_to_frame(&L, 195, 600, 320, 200, &fx, &fy), "touch_to_frame: below the picture is outside");
    /* Touch: content and dialog mode, and a tap on nothing is an event */
    Touch t;
    touch_init(&t, TOUCH_ON, -1, -1);
    touch_set_content(&t, 390, 844, cx, cy, cw, ch);
    ck(!t.L.overlay && t.L.win_h == 844, "touch_set_content lays out for the picture");
    touch_set_dialog(&t, 1);
    ck(!strcmp(t.L.c[TC_ITEMS].label, "OK") && !strcmp(t.L.c[TC_ESC].label, "CANCEL") && !strcmp(t.L.c[TC_SWORD].label, "PICK"), "dialog mode relabels Items/Esc/Sword");
    touch_resize(&t, 400, 800);
    ck(!strcmp(t.L.c[TC_ITEMS].label, "OK"), "...and keeps them across a resize");
    touch_set_dialog(&t, 0);
    ck(!strcmp(t.L.c[TC_ITEMS].label, "ITEMS"), "...and restores them");
    touch_set_content(&t, 390, 844, cx, cy, cw, ch);
    TouchEvent e;
    touch_finger_down(&t, 1, 100, 100);                 /* on the picture, on no control */
    ck(touch_next_event(&t, &e) && e.kind == TEV_TAP && e.x == 100 && e.y == 100, "a touch on the picture is a TEV_TAP with its position");
    touch_finger_up(&t, 1);
}

static void t_hit(void)
{
    printf("hit-test        ");
    for (int s = 0; s < 2; s++) {
        TouchLayout L;
        touch_layout(&L, SIZES[s][0], SIZES[s][1], 100);
        int all = 1;
        for (int i = 0; i < TC_COUNT; i++) {
            int x, y;
            centre(&L, i, &x, &y);
            if (touch_hit(&L, x, y) != i) { all = 0; printf("\n  %dx%d control %d centre hit %d", L.win_w, L.win_h, i, touch_hit(&L, x, y)); }
        }
        ck(all, "%dx%d: every control's centre hits itself", L.win_w, L.win_h);
        ck(touch_hit(&L, L.win_w / 2, L.win_h / 2) == TC_NONE, "%dx%d: the middle of the window is empty", L.win_w, L.win_h);
        ck((touch_hit(&L, 0, 0) == TC_NONE) && (touch_hit(&L, L.win_w - 1, L.win_h - 1) == TC_NONE),
           "%dx%d: the corners are empty", L.win_w, L.win_h);
        /* circles are circles: the bounding-box corner of Sword misses it */
        const TouchCtl *sw = &L.c[TC_SWORD];
        ck(touch_hit(&L, sw->x + 1, sw->y + 1) != TC_SWORD, "%dx%d: sword's box corner is outside its circle", L.win_w, L.win_h);
        /* the stick's zone reaches past its ring, but not as far as 2x */
        ck(touch_hit(&L, L.stick_cx + L.stick_r * 12 / 10, L.stick_cy) == TC_STICK, "%dx%d: just outside the ring still grabs the stick", L.win_w, L.win_h);
        ck(touch_hit(&L, L.stick_cx, L.stick_cy - L.stick_r * 2) != TC_STICK, "%dx%d: twice the radius is not the stick", L.win_w, L.win_h);
    }
}

static void t_stick(void)
{
    printf("stick zones     ");
    int r = 100;
    ck(touch_stick_dirs(0, 0, r) == 0 && touch_stick_dirs(15, -15, r) == 0, "the dead zone gives nothing");
    ck(touch_stick_dirs(80, 0, r) == DIR_RIGHT, "right walks right");
    ck(touch_stick_dirs(-80, 0, r) == DIR_LEFT, "left walks left");
    ck(touch_stick_dirs(0, -80, r) == DIR_UP, "up jumps");
    ck(touch_stick_dirs(0, 80, r) == DIR_DOWN, "down kneels");
    ck(touch_stick_dirs(60, -60, r) == (DIR_UP | DIR_RIGHT), "up-right is the right jump-walk");
    ck(touch_stick_dirs(-60, -60, r) == (DIR_UP | DIR_LEFT), "up-left is the left jump-walk");
    ck(touch_stick_dirs(60, 60, r) == (DIR_DOWN | DIR_RIGHT), "down-right: kneel and face right");
    ck(touch_stick_dirs(-60, 60, r) == (DIR_DOWN | DIR_LEFT), "down-left: kneel and face left");
    /* a slightly tilted horizontal stays a plain walk; sector edge at 22.5 deg */
    ck(touch_stick_dirs(90, -20, r) == DIR_RIGHT, "a small upward tilt is still a plain right");
    ck(touch_stick_dirs(90, -50, r) == (DIR_UP | DIR_RIGHT), "past 22.5 degrees it becomes a jump-walk");
    ck(touch_stick_dirs(-20, -90, r) == DIR_UP, "a small sideways tilt is still a plain jump");
    /* the eight sectors partition the circle: every direction outside the dead zone sets 1 or 2 bits */
    int ok = 1;
    for (int a = 0; a < 360; a += 5) {
        static const int cx[8] = { 100, 71, 0, -71, -100, -71, 0, 71 }, cy[8] = { 0, -71, -100, -71, 0, 71, 100, 71 };
        int k = a / 45, f = a % 45;
        int x = cx[k] + (cx[(k + 1) % 8] - cx[k]) * f / 45, y = cy[k] + (cy[(k + 1) % 8] - cy[k]) * f / 45;
        int d = touch_stick_dirs(x, y, r);
        if (d == 0 || ((d & DIR_LEFT) && (d & DIR_RIGHT)) || ((d & DIR_UP) && (d & DIR_DOWN))) ok = 0;
    }
    ck(ok, "no angle is dead and none sets opposite bits");
}

static void t_fingers(void)
{
    printf("multi-touch     ");
    Touch t;
    touch_init(&t, TOUCH_ON, -1, -1);
    touch_resize(&t, 1280, 720);
    const TouchLayout *L = &t.L;
    int x, y, ex, ey;

    ck(touch_dirs(&t) == 0 && touch_buttons(&t, 0) == 0 && !touch_menu_held(&t), "nothing held to begin with");

    /* finger 1 on the stick, pushed up-right; finger 2 on sword: independent */
    touch_finger_down(&t, 1, L->stick_cx, L->stick_cy);
    ck(touch_dirs(&t) == 0, "a finger on the stick centre is neutral");
    touch_finger_move(&t, 1, L->stick_cx + L->stick_r * 7 / 10, L->stick_cy - L->stick_r * 7 / 10);
    ck(touch_dirs(&t) == (DIR_UP | DIR_RIGHT), "stick finger pushed up-right");
    centre(L, TC_SWORD, &x, &y);
    touch_finger_down(&t, 2, x, y);
    ck(touch_buttons(&t, 0) == 1 && touch_dirs(&t) == (DIR_UP | DIR_RIGHT), "sword down while the stick is held: both register");
    centre(L, TC_MAGIC, &x, &y);
    touch_finger_down(&t, 3, x, y);
    ck(touch_buttons(&t, 0) == 3, "a third finger adds magic");
    touch_finger_up(&t, 2);
    ck(touch_buttons(&t, 0) == 2 && touch_dirs(&t) == (DIR_UP | DIR_RIGHT), "lifting sword leaves magic and the stick alone");
    touch_finger_up(&t, 1);
    ck(touch_dirs(&t) == 0 && touch_buttons(&t, 0) == 2, "lifting the stick finger stops the walk only");
    touch_finger_up(&t, 3);
    ck(touch_buttons(&t, 0) == 0, "all fingers up: nothing held");

    /* each finger stays bound to the control it began on */
    centre(L, TC_SWORD, &x, &y);
    touch_finger_down(&t, 4, x, y);
    centre(L, TC_MAGIC, &ex, &ey);
    touch_finger_move(&t, 4, ex, ey);
    ck(touch_buttons(&t, 0) == 1, "a finger that slides from sword onto magic is still sword");
    touch_finger_move(&t, 4, L->win_w / 2, L->win_h / 2);
    ck(touch_buttons(&t, 0) == 1, "...and sliding off the controls does not release it");
    touch_finger_up(&t, 4);
    /* a finger that starts on empty ground does nothing, even if it slides onto a button */
    touch_finger_down(&t, 5, L->win_w / 2, L->win_h / 2);
    centre(L, TC_SWORD, &x, &y);
    touch_finger_move(&t, 5, x, y);
    ck(touch_buttons(&t, 0) == 0, "a finger that starts on nothing never presses sword");
    touch_finger_up(&t, 5);
    /* a finger that starts on the stick and wanders outside the ring keeps steering */
    touch_finger_down(&t, 6, L->stick_cx, L->stick_cy);
    touch_finger_move(&t, 6, L->stick_cx - L->stick_r * 3, L->stick_cy);
    ck(touch_dirs(&t) == DIR_LEFT, "the stick keeps steering past the ring");
    int kx, ky;
    touch_knob(&t, &kx, &ky);
    ck(kx == L->stick_cx - L->stick_r && ky == L->stick_cy, "...and the drawn knob is clamped to the ring (%d,%d)", kx, ky);
    touch_finger_up(&t, 6);
    touch_knob(&t, &kx, &ky);
    ck(kx == L->stick_cx && ky == L->stick_cy, "the knob re-centres when released");

    /* the combo: UP + DOWN held, sword pulsing */
    centre(L, TC_COMBO, &x, &y);
    touch_finger_down(&t, 7, x, y);
    ck(touch_dirs(&t) == (DIR_UP | DIR_DOWN), "combo holds up and down");
    int on = 0, off = 0;
    for (unsigned ms = 0; ms < 1000; ms += 10) { if (touch_buttons(&t, ms) & 1) on++; else off++; }
    ck(on > 30 && off > 30, "combo pulses sword (%d on / %d off per second)", on, off);
    touch_finger_up(&t, 7);

    /* items is held, and queues an event */
    centre(L, TC_ITEMS, &x, &y);
    touch_finger_down(&t, 8, x, y);
    ck(touch_menu_held(&t), "items is held while touched");
    touch_finger_up(&t, 8);
    ck(!touch_menu_held(&t), "...and released");

    /* more fingers than slots: extra ones are dropped, not crashes */
    for (int i = 0; i < 20; i++) touch_finger_down(&t, 100 + i, L->win_w / 2, L->win_h / 2);
    for (int i = 0; i < 20; i++) touch_finger_up(&t, 100 + i);
    ck(touch_dirs(&t) == 0, "twenty fingers on nothing is harmless");
}

static void t_events(void)
{
    printf("one-shot keys   ");
    Touch t;
    touch_init(&t, TOUCH_ON, -1, -1);
    touch_resize(&t, 1280, 720);
    const TouchLayout *L = &t.L;
    int x, y;
    TouchEvent e;
    centre(L, TC_ESC, &x, &y);   touch_finger_down(&t, 1, x, y);
    centre(L, TC_QSAVE, &x, &y); touch_finger_down(&t, 2, x, y);
    centre(L, TC_QLOAD, &x, &y); touch_finger_down(&t, 3, x, y);
    centre(L, TC_KEY0 + 4, &x, &y); touch_finger_down(&t, 4, x, y);
    centre(L, TC_KEY0 + 9, &x, &y); touch_finger_down(&t, 5, x, y);
    centre(L, TC_KEY0 + 10, &x, &y); touch_finger_down(&t, 6, x, y);
    int ok = touch_next_event(&t, &e) && e.kind == TEV_ESC;
    ok &= touch_next_event(&t, &e) && e.kind == TEV_QSAVE;
    ok &= touch_next_event(&t, &e) && e.kind == TEV_QLOAD;
    ok &= touch_next_event(&t, &e) && e.kind == TEV_CHAR && e.ch == '5';
    ok &= touch_next_event(&t, &e) && e.kind == TEV_CHAR && e.ch == 'Y';
    ok &= touch_next_event(&t, &e) && e.kind == TEV_CHAR && e.ch == 'N';
    ck(ok, "esc, save, load, '5', Y, N arrive in order");
    ck(!touch_next_event(&t, &e), "the queue then drains");
    touch_finger_move(&t, 1, x, y);
    ck(!touch_next_event(&t, &e), "holding or sliding a key does not repeat it");
    for (int i = 1; i <= 6; i++) touch_finger_up(&t, i);
    for (int i = 0; i < 40; i++) { centre(L, TC_KEY0, &x, &y); touch_finger_down(&t, 9, x, y); touch_finger_up(&t, 9); }
    int n = 0; while (touch_next_event(&t, &e)) n++;
    ck(n == TOUCH_EVQ, "the queue is bounded (%d)", n);
}

static void t_visibility(void)
{
    printf("auto / on / off ");
    Touch t;
    int x, y;
    ck(touch_mode_by_name("auto") == TOUCH_AUTO && touch_mode_by_name("on") == TOUCH_ON && touch_mode_by_name("off") == TOUCH_OFF
       && touch_mode_by_name("maybe") < 0, "--touch spellings");
    touch_init(&t, TOUCH_AUTO, -1, -1);
    touch_resize(&t, 1280, 720);
    ck(!touch_visible(&t), "auto starts hidden");
    touch_note_other_input(&t);
    ck(!touch_visible(&t), "...keyboard use while hidden changes nothing");
    centre(&t.L, TC_SWORD, &x, &y);
    touch_finger_down(&t, 1, x, y);
    ck(touch_visible(&t) && touch_buttons(&t, 0) == 1, "the first touch shows it (and the touch counts)");
    touch_note_other_input(&t);
    ck(!touch_visible(&t) && touch_buttons(&t, 0) == 0 && touch_dirs(&t) == 0, "keyboard/controller use hides it and releases every finger");
    touch_finger_down(&t, 2, x, y);
    ck(touch_visible(&t), "a later touch brings it back");

    touch_init(&t, TOUCH_ON, -1, -1);
    touch_resize(&t, 1280, 720);
    ck(touch_visible(&t), "on starts shown");
    touch_note_other_input(&t);
    ck(touch_visible(&t), "...and keyboard use does not hide it");

    touch_init(&t, TOUCH_OFF, -1, -1);
    touch_resize(&t, 1280, 720);
    centre(&t.L, TC_SWORD, &x, &y);
    touch_finger_down(&t, 1, x, y);
    ck(!touch_visible(&t) && touch_buttons(&t, 0) == 0, "off ignores touches");

    touch_init(&t, TOUCH_AUTO, 9999, 3);
    ck(t.size == TOUCH_SIZE_MAX && t.opacity == TOUCH_OPACITY_MIN, "size and opacity clamp (%d, %d)", t.size, t.opacity);
    touch_init(&t, TOUCH_AUTO, -1, -1);
    ck(t.size == 100 && t.opacity == 50, "defaults 100%% size, 50%% opacity");
}

/* ------------------------------------------------------- the overlay shot */
#ifdef HAVE_SDL
#include <SDL.h>
#include "present.h"
#include "png.h"

static uint8_t *load_ppm(const char *path, int *w, int *h)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    int mx;
    if (fscanf(f, "P6 %d %d %d", w, h, &mx) != 3 || mx != 255) { fclose(f); return NULL; }
    fgetc(f);
    uint8_t *p = malloc((size_t)*w * *h * 3);
    if (fread(p, 3, (size_t)*w * *h, f) != (size_t)*w * *h) { free(p); p = NULL; }
    fclose(f);
    return p;
}

static int shot(const char *dir, const char *ppm)
{
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 0);
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { printf("  no SDL video (%s): skipping the shot\n", SDL_GetError()); return 0; }
    int sw = 320, sh = 200;
    uint8_t *rgb = ppm ? load_ppm(ppm, &sw, &sh) : NULL;
    if (!rgb) {
        sw = 320; sh = 200;
        rgb = malloc((size_t)sw * sh * 3);
        for (int y = 0; y < sh; y++) for (int x = 0; x < sw; x++) {
            uint8_t *p = rgb + ((size_t)y * sw + x) * 3;
            p[0] = (uint8_t)(x * 255 / sw); p[1] = (uint8_t)(y * 255 / sh); p[2] = 120;
        }
    }
    static const struct { const char *name; int w, h; } shots[] = {
        { "overlay_landscape_16x9.png", 1280, 720 }, { "overlay_landscape_20x9.png", 1200, 540 },
        { "overlay_portrait.png", 390, 844 }, { "overlay_tablet_4x3.png", 1024, 768 },
        { "overlay_phone_sideways.png", 844, 390 } };
    for (unsigned s = 0; s < sizeof shots / sizeof shots[0]; s++) {
        int w = shots[s].w, h = shots[s].h;
        present_set_portrait_top(1);
        SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
        SDL_Renderer *ren = SDL_CreateSoftwareRenderer(surf);
        if (!surf || !ren) { printf("  software renderer unavailable: skipping the shot\n"); return 0; }
        SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, sw, sh);
        SDL_UpdateTexture(tex, NULL, rgb, sw * 3);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        present_render(ren, tex, sw, sh, sw, sh * 6 / 5, FILTER_SMOOTH);

        Touch t;
        touch_init(&t, TOUCH_ON, -1, -1);
        { int cx, cy, cw, ch; picture(w, h, &cx, &cy, &cw, &ch); touch_set_content(&t, w, h, cx, cy, cw, ch); }
        int x, y;
        /* a thumb pushing up-right on the stick, sword held: shows the pressed look */
        touch_finger_down(&t, 1, t.L.stick_cx, t.L.stick_cy);
        touch_finger_move(&t, 1, t.L.stick_cx + t.L.stick_r * 3 / 4, t.L.stick_cy - t.L.stick_r * 3 / 4);
        centre(&t.L, TC_SWORD, &x, &y);
        touch_finger_down(&t, 2, x, y);
        touch_draw(&t, ren);
        SDL_RenderPresent(ren);

        uint8_t *out = malloc((size_t)w * h * 3);
        SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_RGB24, out, w * 3);
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dir, shots[s].name);
        int rc = png_write_rgb(path, out, w, h);
        printf("  wrote %s (%dx%d)%s\n", path, w, h, rc ? " FAILED" : "");
        if (rc) failures++;
        free(out);
        SDL_DestroyTexture(tex);
        present_shutdown();
        SDL_DestroyRenderer(ren);
        SDL_FreeSurface(surf);
    }
    free(rgb);
    SDL_Quit();
    return 0;
}
#endif

int main(int argc, char **argv)
{
    t_layout();     printf("ok\n");
    t_layout_in();  printf("ok\n");
    t_hit();        printf("ok\n");
    t_stick();      printf("ok\n");
    t_fingers();    printf("ok\n");
    t_events();     printf("ok\n");
    t_visibility(); printf("ok\n");
    printf("%d checks, %d failures\n", checks, failures);
    if (argc >= 3 && !strcmp(argv[1], "--shot")) {
#ifdef HAVE_SDL
        shot(argv[2], argc > 3 ? argv[3] : NULL);
#else
        printf("built without SDL2: no overlay shot\n");
#endif
    }
    return failures != 0;
}
