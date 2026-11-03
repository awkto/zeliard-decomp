/* syncdlg.h -- the ask-before-link dialog (#55 step 2), drawn with the same
 * window / text primitives as restore.c.  Same two choices as the web app's
 * "Two saves found for this key" modal: keep the cloud save, or keep this
 * device's.  Esc cancels (sync off for the session).  SDL-free. */
#ifndef ZEL_SYNCDLG_H
#define ZEL_SYNCDLG_H
#include <stdint.h>
#include "restore.h"            /* the RK_* key codes */
#include "shell.h"

typedef struct SyncDlg {
    int  sel;                   /* 0 = keep cloud, 1 = keep this device */
    int  choice;                /* 0 = still open, else SLC_* (synclife.h) */
    char key[24];
} SyncDlg;

void syncdlg_open(SyncDlg *d, const char *key);
/* RK_UP/DOWN/LEFT/RIGHT move, RK_ACCEPT/RK_TAKE choose, RK_CANCEL cancels */
void syncdlg_key(SyncDlg *d, int key);
void syncdlg_draw(const SyncDlg *d, const Shell *sh, uint8_t *fb);

#endif
