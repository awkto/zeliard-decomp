/* player.h — the STDPLY player record (BASE:0000..00E8) and the NAME.USR save.
 *
 * docs/STATE_PAGE.md and docs/TOWN.md §8/§10.  `Game.page[256]` *is* that
 * page: the map-patch engine (6BFC / 6AED) and the shop overlays read and
 * write it directly, while the engine keeps hot fields (gold, hp, sword, ...)
 * in named members.  These two calls keep the two views in step, exactly as
 * the original's single copy in memory does. */
#ifndef ZEL_PLAYER_H
#define ZEL_PLAYER_H
#include "physics.h"

/* zeliard/STDPLY.BIN — 233 bytes of the fresh player record (a new game). */
int  player_load_stdply(Game *g, const char *dir);
void player_page_push(Game *g);     /* the named members -> page[] */
void player_page_pull(Game *g);     /* page[] -> the named members */

/* kenjpro A862 "Record Experience": DOS create + write 256 bytes of BASE:0000
 * + close.  `name` is the ≤ 8-character player name; the file is <name>.usr in
 * `dir`.  Returns 0 on success. */
int  player_save_usr(Game *g, const char *dir, const char *name);
int  player_load_usr(Game *g, const char *dir, const char *name);   /* town.bin 7592 */

/* shop-only fields of the page (docs/TOWN.md §10) */
#define P_KING_GIFT   0x05          /* the 1000-gold gift has been paid */
#define P_ENTERED     0x06          /* the hero has been in a cavern */
#define P_CREST_FLAGS 0x24          /* bit1 = the Crest of Glory was traded */
#define P_BANK_HI     0x88          /* bank balance: u8 hi + u16 lo */
#define P_BANK_LO     0x89
#define P_SHIELD_MAX  0x96          /* u16 shield hp max */
#define P_GLORY_CREST 0x9B
#define P_POTIONS     0xA6          /* 5 slots, drug item id + 1 */
#define P_SPELLS      0xBB          /* 7 "spell learned" flags */
#define P_DRUG_STOCK  0xC9          /* 9 per-town bitmasks */
#define P_SWORD_STOCK 0xD2
#define P_SHIELD_STOCK 0xDB
#define P_SAGES       0xE5          /* one bit per sage, 0x80 Marid .. 0x01 Indihar */

static inline uint32_t page_gold24(const uint8_t *p, int o)
{ return (uint32_t)p[o] << 16 | (uint32_t)(p[o + 1] | p[o + 2] << 8); }
static inline void page_set_gold24(uint8_t *p, int o, uint32_t v)
{ p[o] = (uint8_t)(v >> 16); p[o + 1] = (uint8_t)v; p[o + 2] = (uint8_t)(v >> 8); }

#endif
