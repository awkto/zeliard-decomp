/* status.h — select.bin (ZELRES2[1] @A000): the status / inventory screen and
 * the potion effects.  Sources: src/select.c, docs/TOWN.md §12.
 *
 * The original is parked at arena:C000 and swapped into BASE:A000 by whichever
 * engine is running (town.bin 6938 / fight.bin 72D9), which is why it draws
 * only through the video driver at BASE:2000 and never through the town or
 * cavern renderers.  The port keeps the same shape: `Status` owns a 320x200
 * framebuffer and drives the caller's `present` callback itself, exactly like
 * `Shop` does, so SDL, the headless PNG dumper and the test scripts all work.
 *
 * Entry points mirror the two vectors:
 *   status_run_town(Town *)   = [A002] A00B, in_town = 0xFF (potions disabled)
 *   status_run_fight(Game *)  = [A000] A004, in_town = 0
 * Both return the value the original leaves in menu_result [FF4B]: the potion
 * slot value (drug id + 1) of whatever was drunk, 0 if nothing was.  fight.bin
 * warps to town when it is 8 (Kioku Feather). */
#ifndef ZEL_STATUS_H
#define ZEL_STATUS_H
#include <stddef.h>
#include <stdint.h>
#include "physics.h"
#include "text.h"

/* ---------------------------------------------------------------- itemp.grp */
/* ZELRES2[27]: seven u16 section offsets then the records.  Section 0 holds the
 * 40x18 PC-88 sword pictures (270 bytes, 20x18 on screen), sections 1..6 the
 * 32x16 icons (192 bytes, 16x16 on screen) — docs/VIDEO_DRIVERS.md §2.1.
 *   1 shield   2 crest   3 magic   4 key   5 potion   6 worn item          */
typedef struct ItemPics {
    uint8_t *raw; size_t len;
    unsigned sec[7];
    uint8_t  blank[192];        /* GMMCGA.BIN @2658: the empty-slot picture */
    int      have_blank;
    int      loaded;
} ItemPics;

int  itemp_load(ItemPics *p, const char *dir);
void itemp_free(ItemPics *p);
/* [2020]/[203C]/[201E]/[203A]/[2036]/[2034]: 16x16 at (x4*4+2, y).  `index` is
 * 0-based into the section; < 0 draws the driver's built-in blank slot. */
void itemp_icon(uint8_t *fb, const ItemPics *p, int section, int index, int x4, int y);
/* [201C] vid_icon_sword: 20x18 at (x8*8, y), `index` 0-based */
void itemp_sword(uint8_t *fb, const ItemPics *p, int index, int x8, int y);
/* the HUD's three boxes (GAME.BIN A19C/A1AE/A1C0): sword, magic, shield */
void itemp_hud(uint8_t *fb, const ItemPics *p, const TextFont *f, const Game *g);

/* ------------------------------------------------------------------ Status */
struct Town;
typedef struct Status Status;
typedef void (*StatusPresentFn)(Status *s);

struct Status {
    Game     *g;
    const TextFont *font;
    const ItemPics *pics;
    uint8_t   fb[TEXT_W * TEXT_H];

    uint8_t   in_town;              /* ADF8 */
    uint8_t   pane;                 /* ADF9 */
    uint8_t   magic_list[8];  int n_magic;    /* AE03 / ADFA */
    int       magic_cursor;         /* ADFB */
    uint8_t   item_list[8];   int n_items;    /* AE0A / ADFC */
    int       item_cursor;          /* ADFD */
    uint8_t   potion_list[8]; int n_potions;  /* AE10 / ADFE */
    int       potion_cursor;        /* AE00 */
    uint8_t   potion_sel;           /* ADFF */
    int       box_open;             /* AE02 */
    uint8_t   box_save[320 * 40];   /* the [2026] staging rect */
    uint8_t   menu_key_held;        /* AE01 */
    uint8_t   menu_result;          /* FF4B */

    /* port side */
    uint8_t   dirs, buttons, menu_key;
    int       done;
    unsigned  frames;
    int       frame_guard;          /* tests: give up after this many frames */
    StatusPresentFn present;
    void     *user;
    char      last_used[40];        /* the "I have used …" line, for the tests */
};

/* town.bin 68F3 / fight.bin 7202 — run the screen to completion */
int status_run_town(struct Town *t);
int status_run_fight(Game *g);
const uint8_t *status_framebuffer(const Status *s);

/* --- exposed for the tests --------------------------------------------- */
void status_open(Status *s, Game *g, const TextFont *f, const ItemPics *p, int in_town);
void status_loop(Status *s);
/* A40D use_potion: drink `potion_list[potion_cursor]`.  Returns the potion slot
 * value it wrote to menu_result, or 0 when the "NO USE" slot was selected. */
int  status_use_potion(Status *s);
/* A135 / A228: commit the row cursor to the record */
void status_select_magic(Status *s);
void status_select_item(Status *s);
/* A643 / A033 / A052: (re)pack the three lists out of the player record */
void status_build_lists(Status *s);

/* select.bin A520: Holy Water of Acero restores this much, by shield 1..6 */
extern const uint16_t HOLY_WATER[6];
/* AAB8 / AAF3 / AC32 / ACD9 / AD67 — the overlay's own name tables */
extern const char *const MAGIC_NAME[7];
extern const char *const ITEM_NAME[6][2];
extern const char *const POTION_NAME[9][2];
extern const char *const POTION_USED[8];
extern const char *const SWORD_NAME[6][2];
extern const char *const SHIELD_NAME[6][2];

#endif
