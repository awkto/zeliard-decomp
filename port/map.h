/* map.h — .mdt cavern map decode (docs/ARCHITECTURE.md "Maps", tools/mdt2png.py). */
#ifndef ZEL_MAP_H
#define ZEL_MAP_H
#include <stdint.h>
#include <stddef.h>

#define MAP_ROWS 64
#define MAP_MAX_WIDTH 320

typedef struct {                /* 12-byte C00A record (struct door in src/fight.c) */
    uint16_t col;
    uint8_t  row;               /* map row of the door cell (0x4A); hero stands one row below */
    uint8_t  letter;            /* bits0-2 letter tile, bit6 enter facing left, bit7 unlocked */
    uint8_t  dest_map;          /* system map index; | 0x80 when dest_row == 0xFF (town) */
    uint16_t dest_col;
    uint8_t  dest_row;
    uint8_t  dflags;
    uint16_t flag_ptr;
    uint8_t  flag_mask;
} Door;

/* Fixture lists A (C004), B (C006) and C (C008): three DCHR cells side by side.
 * A = elevators (0x40-0x42, moved by "down"/"up", 7FDC/8074); B = gates
 * (0x43-0x45) that sink one row per frame under the hero's weight (818E);
 * C = 7-byte records (0x46-0x48) that patrol between two columns (81AE/8244). */
typedef struct {
    uint16_t col;               /* +0 map column of the leftmost cell (bits 0-13) */
    uint8_t  row;               /* +2 map/ring row (bits 0-5) */
    uint8_t  cell;              /* first DCHR cell: 0x40 (A), 0x43 (B), 0x46 (C) */
    uint8_t  kind;              /* 0 = A, 1 = B, 2 = C */
    uint8_t  var;               /* C: (col >> 14) & 3 — 0 static, 1 half speed, 2/3 full speed */
    uint8_t  state;             /* C: row bit7 = moving left, bit6 = pause this frame */
    uint16_t lim_l, lim_r;      /* C: +3 / +5 turn-around columns */
    uint16_t drawn_col;         /* port: where the cells were written last frame */
    uint8_t  drawn_row, drawn;
} Fixture;

typedef struct {                /* 16-byte C010 record */
    uint16_t col; uint8_t row; uint8_t rcol; uint8_t type; uint8_t hit; uint8_t phase; uint8_t flags;
    uint8_t hp; uint8_t next; uint8_t link; uint16_t home_col; uint8_t home_row; uint8_t home_type; uint8_t timer;
} MapObj;

typedef struct {
    int      width;
    uint8_t  grid[MAP_MAX_WIDTH][MAP_ROWS];   /* [col][row], values as in the tile stream */
    uint8_t  cavern, row_bias, tileset, ai, enemies, lvl_flags;
    /* boss rooms (mpNd) continue the level record (docs/ARCHITECTURE.md):
     * +5 boss sprite bank (copied over +4 when the boss appears, 6117),
     * +6/+7 the post-boss AI/enemy banks, +8.. {u16 ptr, u16 val} pokes
     * applied by 72F1.  `lvl_off` is the record's offset inside raw[]. */
    uint8_t  boss_bank, post_ai, post_enemies;
    size_t   lvl_off;
    uint16_t start_col; uint8_t start_row;
    Door     doors[64];   int ndoors;
    uint16_t patches;                         /* [C00C] offset of the conditional poke list */
    uint16_t vidinit;                         /* [C00E] the place-name record for video [2010] */
    Fixture  fix[256];    int nfix;
    MapObj   objs[256];   int nobj;
    char     name[16];
    uint8_t *raw;   size_t rawlen;            /* the decompressed .mdt, kept for 6BFC */
} Map;

/* the {x4, y, xoff, len, chars} place-name record video [2010] draws
 * (fight.bin 6185: si = [C00E]), or NULL */
const uint8_t *map_place_record(const Map *m);

/* decode a raw (decompressed) .mdt image (a copy is kept in m->raw) */
int map_parse(Map *m, const uint8_t *d, size_t len);
void map_free(Map *m);
/* 0x6BFC: the C00C list of {u16 flag_ptr, u8 mask, {u16 addr, u16 val}.. FFFF}
 * records; every poke whose (page[flag & 0xFF] & mask) holds is applied to the
 * raw image, which is then re-parsed.  Returns the number of pokes applied. */
int map_apply_patches(Map *m, const uint8_t page[256]);
/* load system map #idx (STICK.BIN cs:0xF68 table: 0 = MP10 .. 0x1E = MPA0; 0x20.. town maps) */
int map_load_system(Map *m, const char *dir, int sys_index);
/* build a synthetic map for tests: rows[0..63] strings of width chars, '.' = 0, else hex digit / 'A'-'Z' = 10.. */
void map_from_text(Map *m, int width, const char *const rows[MAP_ROWS]);

#endif
