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

void map_free(Map *m) { free(m->raw); m->raw = NULL; m->rawlen = 0; }

int map_parse(Map *m, const uint8_t *d, size_t len)
{
    uint8_t *keep;                              /* keep the image itself for 6BFC */
    if (d == m->raw) { keep = m->raw; m->raw = NULL; }      /* re-parse after a patch */
    else {
        keep = malloc(len ? len : 1);
        if (keep) memcpy(keep, d, len);
        free(m->raw); m->raw = NULL;
        d = keep ? keep : d;
    }
    memset(m, 0, sizeof *m);
    m->raw = keep; m->rawlen = keep ? len : 0;
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
    /* fixture lists A (C004) / B (C006) / C (C008) — see struct Fixture */
    const struct { int ptr_off, size, cell; } FL[3] = {{4, 3, 0x40}, {6, 3, 0x43}, {8, 7, 0x46}};
    for (int k = 0; k < 3; k++) {
        size_t o = u16(d, FL[k].ptr_off) - BASE;
        while (o + (size_t)FL[k].size <= len && u16(d, o) != 0xFFFF && m->nfix < 256) {
            Fixture *f = &m->fix[m->nfix++];
            f->col = u16(d, o) & 0x3FFF; f->row = d[o + 2] & 0x3F; f->cell = (uint8_t)FL[k].cell;
            f->kind = (uint8_t)k;
            if (k == 2) {                                   /* 81AE / 8299 */
                f->var = (uint8_t)((u16(d, o) >> 14) & 3);
                f->state = (uint8_t)(d[o + 2] & 0xC0);
                f->lim_l = u16(d, o + 3); f->lim_r = u16(d, o + 5);
            }
            o += FL[k].size;
        }
    }
    m->patches = u16(d, 0xC);
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

/* 0x6BFC  the conditional poke list at [C00C].  Every record is
 * {u16 flag_ptr, u8 mask, {u16 addr, u16 val}... 0xFFFF}; when
 * (*flag_ptr & mask) != 0 each addr (a BASE offset) is set to val.  The
 * shipped lists poke the map image itself (door records, tile stream, object
 * table), so the port applies them to the raw .mdt and re-parses it. */
int map_apply_patches(Map *m, const uint8_t page[256])
{
    if (!m->raw || !m->patches) return 0;
    uint8_t *d = m->raw;
    size_t len = m->rawlen;
    size_t o = (size_t)(m->patches - BASE);
    int applied = 0, guard = 0;
    while (o + 3 <= len && ++guard < 256) {
        uint16_t fp = u16(d, o);
        if (fp == 0xFFFF) break;
        uint8_t mask = d[o + 2];
        o += 3;
        int on = page && (page[fp & 0xFF] & mask) != 0;
        while (o + 4 <= len) {
            uint16_t addr = u16(d, o);
            if (addr == 0xFFFF) { o += 2; break; }
            if (on) {
                uint16_t val = u16(d, o + 2);
                if (addr >= BASE && (size_t)(addr - BASE) + 1 < len) {
                    d[addr - BASE] = (uint8_t)val;
                    d[addr - BASE + 1] = (uint8_t)(val >> 8);
                    applied++;
                }
            }
            o += 4;
        }
    }
    if (applied) {
        char name[16];
        memcpy(name, m->name, sizeof name);
        map_parse(m, m->raw, m->rawlen);                    /* map_parse re-uses the image */
        memcpy(m->name, name, sizeof name);
    }
    return applied;
}
