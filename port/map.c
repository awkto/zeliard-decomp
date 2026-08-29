#include "map.h"
#include "sar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BASE 0xC000
static uint16_t u16(const uint8_t *d, size_t o) { return (uint16_t)(d[o] | d[o + 1] << 8); }

/* STICK.BIN @0F68: 11-byte {archive, res# (1-based), name} records */
static const struct { uint8_t archive, res; const char *name; } SYSMAP[] = {
    {2, 21, "MP10"}, {2, 22, "MP1D"}, {2, 23, "MP20"}, {2, 24, "MP21"}, {2, 25, "MP2D"}, {2, 26, "MP30"},
    {2, 27, "MP31"}, {2, 28, "MP3D"}, {2, 29, "MP40"}, {2, 30, "MP41"}, {2, 31, "MP4D"}, {2, 32, "MP50"},
    {2, 33, "MP51"}, {2, 34, "MP5D"}, {2, 35, "MP60"}, {2, 36, "MP61"}, {2, 37, "MP62"}, {2, 38, "MP6D"},
    {2, 39, "MP70"}, {2, 40, "MP71"}, {2, 41, "MP72"}, {2, 42, "MP73"}, {2, 43, "MP7D"}, {2, 44, "MP80"},
    {2, 45, "MP81"}, {2, 46, "MP82"}, {2, 47, "MP83"}, {2, 48, "MP84"}, {2, 49, "MP8D"}, {2, 50, "MP90"},
    {2, 51, "MPA0"}, {1, 0, ""}, {1, 37, "CMAP"}, {1, 38, "MRMP"}, {1, 39, "STMP"}, {1, 40, "BSMP"},
    {1, 41, "HLMP"}, {1, 42, "TMMP"}, {1, 43, "DRMP"}, {1, 44, "LLMP"}, {1, 45, "PRMP"}, {1, 46, "ESMP"},
};

int map_parse(Map *m, const uint8_t *d, size_t len)
{
    memset(m, 0, sizeof *m);
    if (len < 0x1B) return -1;
    m->width = u16(d, 2);
    if (m->width < 1 || m->width > MAP_MAX_WIDTH) return -1;
    size_t lv = u16(d, 0) - BASE;
    if (lv + 5 <= len) {
        m->lvl_flags = d[lv]; m->tileset = d[lv + 2]; m->ai = d[lv + 3]; m->enemies = d[lv + 4];
    }
    m->cavern = d[0x12]; m->start_col = u16(d, 0x13); m->start_row = d[0x15]; m->row_bias = d[0x16];

    /* tile stream: column-major RLE, 64 rows per column (fight.bin 6CED/6D57) */
    size_t si = 0x1B;
    for (int c = 0; c < m->width; c++) {
        int row = 0;
        while (row < MAP_ROWS) {
            if (si >= len) return -1;
            uint8_t b = d[si];
            int n, v;
            switch (b >> 6) {
            case 0: if (si + 1 >= len) return -1; n = b + 1; v = d[si + 1]; si += 2; break;
            case 1: n = ((b >> 4) & 3) + 2; v = (b & 15) + 1; si++; break;
            case 2: n = b & 0x3F; v = 0; si++; break;
            default: n = 1; v = b & 0x3F; si++; break;
            }
            while (n-- && row < MAP_ROWS) m->grid[c][row++] = (uint8_t)v;
        }
    }
    /* fixture lists A/B/C */
    const struct { int ptr_off, size, cell; } FL[3] = {{4, 3, 0x40}, {6, 3, 0x43}, {8, 7, 0x46}};
    for (int k = 0; k < 3; k++) {
        size_t o = u16(d, FL[k].ptr_off) - BASE;
        while (o + 3 <= len && u16(d, o) != 0xFFFF && m->nfix < 256) {
            Fixture *f = &m->fix[m->nfix++];
            f->col = u16(d, o) & 0x3FFF; f->row = d[o + 2] & 0x3F; f->cell = (uint8_t)FL[k].cell;
            o += FL[k].size;
        }
    }
    /* doors */
    size_t o = u16(d, 0xA) - BASE;
    while (o + 12 <= len && u16(d, o) != 0xFFFF && m->ndoors < 64) {
        Door *dr = &m->doors[m->ndoors++];
        dr->col = u16(d, o); dr->row = d[o + 2]; dr->letter = d[o + 3]; dr->dest_map = d[o + 4];
        dr->dest_col = u16(d, o + 5); dr->dest_row = d[o + 7]; dr->dflags = d[o + 8];
        dr->flag_ptr = u16(d, o + 9); dr->flag_mask = d[o + 11];
        o += 12;
    }
    /* objects */
    o = u16(d, 0x10) - BASE;
    while (o + 16 <= len && u16(d, o) != 0xFFFF && m->nobj < 256) {
        MapObj *ob = &m->objs[m->nobj++];
        ob->col = u16(d, o); ob->row = d[o + 2]; ob->rcol = 0xFF; ob->type = d[o + 4]; ob->hit = d[o + 5];
        ob->phase = d[o + 6]; ob->flags = d[o + 7]; ob->hp = d[o + 8]; ob->next = d[o + 9]; ob->link = d[o + 10];
        ob->home_col = u16(d, o + 11); ob->home_row = d[o + 13]; ob->home_type = d[o + 14]; ob->timer = d[o + 15];
        o += 16;
    }
    return 0;
}

int map_load_system(Map *m, const char *dir, int sys_index)
{
    int n = (int)(sizeof SYSMAP / sizeof SYSMAP[0]);
    if (sys_index < 0 || sys_index >= n || SYSMAP[sys_index].res == 0) return -1;
    size_t len;
    uint8_t *d = sar_load(dir, SYSMAP[sys_index].archive, SYSMAP[sys_index].res - 1, 1, &len);
    if (!d) return -1;
    int r = map_parse(m, d, len);
    free(d);
    if (r == 0) snprintf(m->name, sizeof m->name, "%s", SYSMAP[sys_index].name);
    return r;
}

void map_from_text(Map *m, int width, const char *const rows[MAP_ROWS])
{
    memset(m, 0, sizeof *m);
    m->width = width; m->cavern = 1; m->row_bias = 10; m->start_col = 0xFFFF;
    snprintf(m->name, sizeof m->name, "test");
    for (int r = 0; r < MAP_ROWS; r++) {
        const char *s = rows[r] ? rows[r] : "";
        for (int c = 0; c < width; c++) {
            char ch = s[c] ? s[c] : '.';
            int v = 0;
            if (ch >= '0' && ch <= '9') v = ch - '0';
            else if (ch >= 'A' && ch <= 'Z') v = ch - 'A' + 10;
            else if (ch >= 'a' && ch <= 'z') v = ch - 'a' + 0x40;   /* DCHR cells */
            m->grid[c][r] = (uint8_t)v;
        }
    }
}
