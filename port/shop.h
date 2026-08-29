/* shop.h — the eight shop overlays (src/shops.c, docs/TOWN.md §7) and the
 * presentation layer they run on: the proportional text box (town.bin 706C),
 * the menu widgets (7344 menu_select / 751A menu_draw_items / 7539
 * menu_draw_icons / 7469 cursor_draw / 74D3 yes_no_prompt) and gtmcga's
 * 3805/37CC menu-line renderer.
 *
 * The overlays are loaded raw to BASE:A000 exactly as town.bin 6E7E does it,
 * and every string, price table, name table and description in the port comes
 * straight out of that image at the addresses src/shops.c documents — so the
 * text and the numbers are the original's, byte for byte.  The *code* around
 * them is ported.
 *
 * Like the original the shop takes over the frame loop: town.bin does
 * `call [A000]` and only gets control back when the shop returns.  `shop_run`
 * drives `Town.present` itself, so SDL, the headless PNG dumper and the test
 * scripts all work unchanged. */
#ifndef ZEL_SHOP_H
#define ZEL_SHOP_H
#include "town.h"
#include "text.h"
#include "player.h"

/* door dest -> shop (docs/TOWN.md §3 C009) */
enum { SHOP_KING = 0, SHOP_OMOYA, SHOP_SAGE, SHOP_ARMOUR, SHOP_DRUG,
       SHOP_CHURCH, SHOP_BANK, SHOP_INN, SHOP_COUNT };

typedef struct Shop {
    Game    *g;
    Town    *t;
    const TextFont *font;
    int      dest;                  /* SHOP_* */
    int      town_id;               /* the map's [C006], 1..9 */
    uint8_t *img;  size_t imglen;   /* the overlay image, addressed from A000 */
    Cell8    cell[256];  uint8_t present[256];   /* the portrait bank at arena:8000 */
    uint8_t  fb[TEXT_W * TEXT_H];   /* what the shop has drawn (the driver's A000) */

    /* text box: FF4C text pointer, FF4E x, FF4F line */
    unsigned tp;
    int      tx, tline, lines, mute;

    /* menu state FF52..FF6A */
    int      mvis, mtot, mscroll, mx4, my, mw4, mprices, mpricex;
    uint8_t  ids[16];
    unsigned names_tbl;             /* image address of the name pointer table */
    uint32_t price[12];             /* this town's price table, already decoded */
    int      nprice;

    int      cursor_main, cursor_list, cursor_row;
    int      bought, leave, saved;
    const char *dir;
    unsigned frames;                /* port counter */
    int      headless_guard;        /* abort after this many frames (tests) */
    char     last_text[512];        /* everything printed since the last clear */
    int      last_len;
    /* port-side instrumentation, for the playthrough driver (nav/playthrough.c):
     * menu_select publishes the widget it is sitting in so a front end can
     * drive it without guessing at key timings. */
    int      in_menu;               /* 1 while 7344 menu_select owns the loop */
    int      menu_row, menu_n;      /* the cursor row and the visible row count */
} Shop;

/* town.bin 6E7E run_shop: load the overlay + portrait, run it, return when it
 * does.  `dest` is the door's dest byte 0..7.  Returns 0 when it ran. */
int  shop_run(Town *t, int dest);
/* what the shop has on screen (the town renderer shows this while it runs) */
const uint8_t *shop_framebuffer(const struct Shop *s);

/* --- exposed for the tests -------------------------------------------- */
int  shop_open(Shop *s, Town *t, int dest);     /* load, draw the first screen */
void shop_close(Shop *s);
void shop_loop(Shop *s);                        /* the text/action loop */
uint32_t shop_price(const Shop *s, int item);   /* this town's price of `item` */
/* armrpro A6BF: the durability of shields 1..6 */
extern const uint16_t SHIELD_HP[6];
/* kenjpro A28C / A2AC / A380 */
extern const uint16_t EXP_NEXT[16];
extern const uint8_t  SAGE_MAX_LEVEL[8];
uint16_t sage_level_hp(const Shop *s, int level);
void     sage_level_magic(const Shop *s, int level, uint8_t out[7]);
int      sage_assess(Shop *s);                  /* kenj_assess A22E: 0..4 */
void     sage_level_up(Shop *s);                /* kenj_level_up A2B4 */

#endif
