/* statekeys.c — see statekeys.h.  SDL-free: only the request queue, the slot
 * paths, the OSD text and the tiny 5x7 font live here. */
#include "statekeys.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <sys/stat.h>
#include <sys/types.h>
#define SK_MKDIR(d) _mkdir(d)
#else
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#define SK_MKDIR(d) mkdir((d), 0777)
#endif

void statekeys_init(StateKeys *k)
{
    memset(k, 0, sizeof *k);
}

int statekeys_dir(StateKeys *k, const char *override, const char *pref, const char *gamedir)
{
    k->dir[0] = 0;
    k->dir_ok = 0;
    if (override && *override)
        snprintf(k->dir, sizeof k->dir, "%s", override);
    else if (pref && *pref) {
        size_t n = strlen(pref);
        snprintf(k->dir, sizeof k->dir, "%s%sstates", pref, (n && pref[n - 1] == '/') ? "" : "/");
    } else if (gamedir && *gamedir)
        snprintf(k->dir, sizeof k->dir, "%s/states", gamedir);
    else
        return -1;
    /* create it (the parent -- the pref dir or the game dir -- already exists);
     * EEXIST is success */
    if (SK_MKDIR(k->dir) != 0) {
#ifdef _WIN32
        struct _stat st;
        if (_stat(k->dir, &st) != 0 || !(st.st_mode & _S_IFDIR)) return -1;
#else
        if (errno != EEXIST) return -1;
#endif
    }
    k->dir_ok = 1;
    return 0;
}

int statekeys_path(const StateKeys *k, int slot, char *out, size_t n)
{
    if (!k->dir_ok || !out || n == 0) return -1;
    if (slot < 0) snprintf(out, n, "%s/quick.zst", k->dir);
    else          snprintf(out, n, "%s/slot%d.zst", k->dir, slot);
    return 0;
}

/* a shop, the status screen, a dialogue box or the Tear cutscene owns the frame
 * loop; step 1 cannot serialise the loop's own cursor, so both save and load
 * are refused rather than corrupting it */
static int overlay_owns_loop(const Shell *s)
{
    if (!s) return 0;
    if (s->in_town) return s->town.status || s->town.shop || s->town.dlg.active;
    return s->g.status || s->g.tear;
}

void statekeys_queue(StateKeys *k, int kind, int slot)
{
    if (k->q_n >= STATEKEYS_QUEUE) return;
    k->q_kind[k->q_n] = kind;
    k->q_slot[k->q_n] = slot;
    k->q_n++;
}

void statekeys_request(StateKeys *k, const Shell *s, int kind, int slot)
{
    if (overlay_owns_loop(s)) {
        statekeys_osd(k, kind == STATE_REQ_SAVE ? "CANNOT SAVE HERE" : "CANNOT LOAD HERE");
        k->refused++;
        return;
    }
    statekeys_queue(k, kind, slot);
}

/* --------------------------------------------------------------- the OSD */
void statekeys_osd(StateKeys *k, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(k->osd, sizeof k->osd, fmt, ap);
    va_end(ap);
    k->osd_frames = STATEKEYS_OSD_FRAMES;
}

void statekeys_osd_tick(StateKeys *k)
{
    if (k->osd_frames > 0 && --k->osd_frames == 0) k->osd[0] = 0;
}

const char *statekeys_osd_text(const StateKeys *k)
{
    return k->osd_frames > 0 ? k->osd : "";
}

/* 5x7, uppercase, column-major, bit 0 = top row.  The front end draws each set
 * bit as one scale x scale square. */
static const struct { char c; uint8_t col[5]; } OSD_FONT[] = {
    { ' ', { 0x00, 0x00, 0x00, 0x00, 0x00 } },
    { 'A', { 0x7E, 0x09, 0x09, 0x09, 0x7E } },
    { 'B', { 0x7F, 0x49, 0x49, 0x49, 0x36 } },
    { 'C', { 0x3E, 0x41, 0x41, 0x41, 0x22 } },
    { 'D', { 0x7F, 0x41, 0x41, 0x41, 0x3E } },
    { 'E', { 0x7F, 0x49, 0x49, 0x49, 0x41 } },
    { 'F', { 0x7F, 0x09, 0x09, 0x09, 0x01 } },
    { 'G', { 0x3E, 0x41, 0x49, 0x49, 0x3A } },
    { 'H', { 0x7F, 0x08, 0x08, 0x08, 0x7F } },
    { 'I', { 0x41, 0x41, 0x7F, 0x41, 0x41 } },
    { 'J', { 0x20, 0x40, 0x41, 0x3F, 0x01 } },
    { 'K', { 0x7F, 0x08, 0x14, 0x22, 0x41 } },
    { 'L', { 0x7F, 0x40, 0x40, 0x40, 0x40 } },
    { 'M', { 0x7F, 0x02, 0x04, 0x02, 0x7F } },
    { 'N', { 0x7F, 0x02, 0x04, 0x08, 0x7F } },
    { 'O', { 0x3E, 0x41, 0x41, 0x41, 0x3E } },
    { 'P', { 0x7F, 0x09, 0x09, 0x09, 0x06 } },
    { 'Q', { 0x3E, 0x41, 0x51, 0x21, 0x5E } },
    { 'R', { 0x7F, 0x09, 0x19, 0x29, 0x46 } },
    { 'S', { 0x46, 0x49, 0x49, 0x49, 0x31 } },
    { 'T', { 0x01, 0x01, 0x7F, 0x01, 0x01 } },
    { 'U', { 0x3F, 0x40, 0x40, 0x40, 0x3F } },
    { 'V', { 0x1F, 0x20, 0x40, 0x20, 0x1F } },
    { 'W', { 0x7F, 0x20, 0x18, 0x20, 0x7F } },
    { 'X', { 0x63, 0x14, 0x08, 0x14, 0x63 } },
    { 'Y', { 0x03, 0x04, 0x78, 0x04, 0x03 } },
    { 'Z', { 0x61, 0x51, 0x49, 0x45, 0x43 } },
    { '0', { 0x3E, 0x51, 0x49, 0x45, 0x3E } },
    { '1', { 0x40, 0x42, 0x7F, 0x40, 0x40 } },
    { '2', { 0x42, 0x61, 0x51, 0x49, 0x46 } },
    { '3', { 0x41, 0x49, 0x49, 0x49, 0x36 } },
    { '4', { 0x18, 0x14, 0x12, 0x7F, 0x10 } },
    { '5', { 0x27, 0x45, 0x45, 0x45, 0x39 } },
    { '6', { 0x3E, 0x49, 0x49, 0x49, 0x32 } },
    { '7', { 0x01, 0x71, 0x09, 0x05, 0x03 } },
    { '8', { 0x36, 0x49, 0x49, 0x49, 0x36 } },
    { '9', { 0x26, 0x49, 0x49, 0x49, 0x3E } },
    { '\'', { 0x00, 0x00, 0x07, 0x00, 0x00 } },
    { '-', { 0x08, 0x08, 0x08, 0x08, 0x08 } },
    { '/', { 0x40, 0x30, 0x08, 0x06, 0x01 } },
    { ':', { 0x00, 0x00, 0x12, 0x00, 0x00 } },
};

const uint8_t *statekeys_glyph(char c)
{
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    for (size_t i = 0; i < sizeof OSD_FONT / sizeof OSD_FONT[0]; i++)
        if (OSD_FONT[i].c == c) return OSD_FONT[i].col;
    return OSD_FONT[0].col;         /* unknown -> space */
}

int statekeys_text_width(const char *s, int scale)
{
    if (!s || !*s) return 0;
    return (int)(strlen(s) * (size_t)(6 * scale) - (size_t)scale);   /* no trailing gap */
}
