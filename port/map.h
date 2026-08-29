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

typedef struct { uint16_t col; uint8_t row; uint8_t cell; } Fixture;   /* cells cell..cell+2 at col..col+2 */

typedef struct {                /* 16-byte C010 record */
    uint16_t col; uint8_t row; uint8_t rcol; uint8_t type; uint8_t hit; uint8_t phase; uint8_t flags;
    uint8_t hp; uint8_t next; uint8_t link; uint16_t home_col; uint8_t home_row; uint8_t home_type; uint8_t timer;
} MapObj;

typedef struct {
    int      width;
    uint8_t  grid[MAP_MAX_WIDTH][MAP_ROWS];   /* [col][row], values as in the tile stream */
    uint8_t  cavern, row_bias, tileset, ai, enemies, lvl_flags;
    uint16_t start_col; uint8_t start_row;
    Door     doors[64];   int ndoors;
    Fixture  fix[256];    int nfix;
    MapObj   objs[256];   int nobj;
    char     name[16];
} Map;

/* decode a raw (decompressed) .mdt image */
int map_parse(Map *m, const uint8_t *d, size_t len);
/* load system map #idx (STICK.BIN cs:0xF68 table: 0 = MP10 .. 0x1E = MPA0; 0x20.. town maps) */
int map_load_system(Map *m, const char *dir, int sys_index);
/* build a synthetic map for tests: rows[0..63] strings of width chars, '.' = 0, else hex digit / 'A'-'Z' = 10.. */
void map_from_text(Map *m, int width, const char *const rows[MAP_ROWS]);

#endif
