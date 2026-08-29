/* sound.c — the FF75 sound-effect requests.
 *
 * fight.bin writes a sound id to FF75 and STICK's INT 8 hands it to the sound
 * driver (SNDSTD / SNDADLIB / SNDJR) every tick.  The port does not synthesise
 * anything yet: it consumes the request, counts it and — with --sound — logs
 * the id and its name, so the request stream can be checked against the
 * original.  Names come from the call sites in src/fight.c, src/town.c and
 * src/shops.c (docs/FIGHT.md §6, docs/STATE_PAGE.md FF75). */
#include "enemy.h"
#include <stdio.h>

static unsigned counts[256];
static int log_on;

static const char *NAMES[] = {
    /* 00 */ NULL,
    /* 01 */ NULL, NULL,
    /* 03 */ "sword swing",           /* 6E96 */
    /* 04 */ "down-thrust",           /* 6E5C */
    /* 05 */ "text glyph",            /* town 706C */
    /* 06 */ "enemy hurt",            /* 97C6 */
    /* 07 */ "enemy killed",          /* 96FD */
    /* 08 */ "hit, shielded",         /* 75E2 */
    /* 09 */ "hero hurt",             /* 7685 / 850E */
    /* 0A */ "shot blocked",          /* 854F */
    /* 0B */ "menu open",             /* 7278 */
    /* 0C */ NULL, NULL, NULL, NULL,
    /* 10 */ "coin",                  /* 8FAB */
    /* 11 */ "treasure box",          /* 8F2A */
    /* 12 */ NULL,
    /* 13 */ "potion",                /* 9008 */
    /* 14 */ "key",                   /* 8FE8 */
    /* 15 */ "door unlocked",         /* 7E15 */
    /* 16 */ "door locked",           /* 7B0A */
    /* 17 */ "cast start",            /* 87EB */
    /* 18 */ "spell released",        /* 8824 */
    /* 19 */ "screen-wide spell",     /* 8959 */
    /* 1A */ NULL, NULL, NULL,
    /* 1D */ "dialogue page",         /* town 6553 */
};
#define NNAMES ((int)(sizeof NAMES / sizeof *NAMES))

const char *sound_name(uint8_t id) { return id < NNAMES ? NAMES[id] : NULL; }
unsigned sound_count(uint8_t id) { return counts[id]; }
void sound_set_log(int on) { log_on = on; }

/* consume FF75 once per half-frame, like STICK's INT 8 does */
void sound_request(Game *g)
{
    uint8_t id = g->sfx_request;
    if (!id) return;
    g->sfx_request = 0;
    counts[id]++;
    if (log_on) {
        const char *n = sound_name(id);
        fprintf(stderr, "[snd] frame %u: %02X %s\n", g->frame_no, id, n ? n : "(unnamed)");
    }
}
