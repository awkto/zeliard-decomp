/* syncdlg.c -- see syncdlg.h. */
#include "syncdlg.h"
#include "synclife.h"
#include "text.h"
#include <stdio.h>
#include <string.h>

void syncdlg_open(SyncDlg *d, const char *key)
{
    memset(d, 0, sizeof *d);
    snprintf(d->key, sizeof d->key, "%s", key ? key : "");
}

void syncdlg_key(SyncDlg *d, int key)
{
    if (d->choice) return;
    switch (key) {
    case RK_UP: case RK_LEFT:    d->sel = 0; break;
    case RK_DOWN: case RK_RIGHT: d->sel = 1; break;
    case RK_ACCEPT: case RK_TAKE: d->choice = d->sel ? SLC_LOCAL : SLC_CLOUD; break;
    case RK_CANCEL: d->choice = SLC_CANCEL; break;
    default: break;
    }
}

static void line(uint8_t *fb, const TextFont *f, const char *s, int x, int y)
{
    uint8_t b[64]; size_t n = strlen(s);
    if (n > 62) n = 62;
    memcpy(b, s, n); b[n] = 0xFF;                       /* the engine's strings end in FF */
    vid_puts(fb, f, b, x, y);
}

void syncdlg_draw(const SyncDlg *d, const Shell *sh, uint8_t *fb)
{
    const TextFont *f = &sh->tfont;
    char t[64];
    vid_window(fb, 0xFF, 0x08, 0x20, 0x40, 0x68);               /* x 32..288, y 32..136 */
    snprintf(t, sizeof t, "Two saves found for key %s", d->key);
    line(fb, f, t, 0x28, 0x28);
    line(fb, f, "This device and the cloud both have", 0x28, 0x3A);
    line(fb, f, "a save.  The one you drop is replaced.", 0x28, 0x44);
    line(fb, f, "Keep cloud save", 0x40, 0x5C);
    line(fb, f, "Keep this device's save", 0x40, 0x6E);
    gt_cursor(fb, 0x40 / 4 - 3, 0x5C + d->sel * 0x12, 1);       /* the selection arrow, as in the menus */
    line(fb, f, "Enter: choose   Esc: sync off", 0x28, 0x80);
}
