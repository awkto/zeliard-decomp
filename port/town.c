/* town.c — the town engine (town.bin) and the town maps.
 *
 * Port of the walking / NPC / door / edge-exit part of src/town.c; the shops
 * (src/shops.c) and the status screen (select.bin, status.c) run on top of it;
 * the parallax backdrop painters are not implemented.  Hex tags are town.bin addresses; docs/TOWN.md is the spec.  */
#include "town.h"
#include "sar.h"
#include "render.h"
#include "status.h"
#include "text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BASE 0xC000
static uint16_t u16(const uint8_t *d, size_t o) { return (uint16_t)(d[o] | d[o + 1] << 8); }

/* ------------------------------------------------------------------- map */
/* docs/TOWN.md §3.  The ten town maps are ZELRES2[36..45] (kernel table
 * records 32..41, AH = 0x80 | index). */
/* parse every table out of an already-decompressed image the caller owns */
static int town_parse_raw(TownMap *m, uint8_t *d, size_t len, int index)
{
    memset(m, 0, sizeof *m);
    m->raw = d; m->rawlen = len; m->index = index;
    m->width = u16(d, 2);
    if (m->width < 0x24 || m->width > TOWN_MAX_W) return -1;
    if ((size_t)(0x17 + m->width * TOWN_ROWS) > len) return -1;
    size_t lv = u16(d, 0) - BASE;
    if (lv + 5 <= len) {
        m->music = (uint8_t)((d[lv] >> 1) & 0xF); m->gfx = d[lv + 1];
        m->town_flags = d[lv + 3]; m->tileset = d[lv + 4];
    }
    size_t lab = u16(d, 4) - BASE;
    if (lab + 4 < len) {
        int n = d[lab + 3];
        if (n > 23) n = 23;
        for (int i = 0; i < n && lab + 4 + i < len; i++) m->label[i] = (char)d[lab + 4 + i];
    }
    m->town_id = d[6];
    m->start_col = u16(d, 0x13);
    m->patches = u16(d, 0x15);
    for (int c = 0; c < m->width; c++)
        memcpy(m->grid[c], d + 0x17 + c * TOWN_ROWS, TOWN_ROWS);

    /* C009 doors (3 bytes, FFFF ends) */
    size_t o = u16(d, 9) - BASE;
    while (o + 3 <= len && u16(d, o) != 0xFFFF && m->ndoors < 16) {
        m->doors[m->ndoors].col = u16(d, o); m->doors[m->ndoors].dest = d[o + 2];
        m->ndoors++; o += 3;
    }
    /* C007 exits: no terminator — the records run up to the door list */
    size_t eo = u16(d, 7) - BASE, edn = u16(d, 9) - BASE;
    for (int i = 0; eo + 4 * i + 4 <= edn && i < 8; i++) {
        m->exits[i].flags = d[eo + 4 * i]; m->exits[i].dest = d[eo + 4 * i + 1];
        m->exits[i].gfx = d[eo + 4 * i + 2]; m->exits[i].tileset = d[eo + 4 * i + 3];
        m->nexits = i + 1;
    }
    /* C00B caves: indexed, no terminator; size it from the doors and exits */
    uint16_t cp = u16(d, 0xB);
    if (cp) {
        int n = 0;
        for (int i = 0; i < m->ndoors; i++)
            if (m->doors[i].dest >= 8 && m->doors[i].dest != 0xFF && m->doors[i].dest - 8 + 1 > n) n = m->doors[i].dest - 8 + 1;
        for (int i = 0; i < m->nexits; i++)
            if ((m->exits[i].flags & 0xFE) && m->exits[i].dest + 1 > n) n = m->exits[i].dest + 1;
        if (n > 8) n = 8;
        size_t co = cp - BASE;
        for (int i = 0; i < n && co + 5 * i + 5 <= len; i++) {
            m->caves[i].col = u16(d, co + 5 * i); m->caves[i].row = d[co + 5 * i + 2];
            m->caves[i].side = d[co + 5 * i + 3]; m->caves[i].map = d[co + 5 * i + 4];
            m->ncaves = i + 1;
        }
    }
    /* C00F NPCs (8 bytes, FFFF ends) */
    o = u16(d, 0xF) - BASE;
    while (o + 8 <= len && u16(d, o) != 0xFFFF && m->nnpcs < 32) {
        TownNpc *n = &m->npcs[m->nnpcs++];
        n->col = u16(d, o); n->sprite = d[o + 2]; n->saved = d[o + 3]; n->anim = d[o + 4];
        n->type = d[o + 5]; n->flags = d[o + 6]; n->script = d[o + 7];
        o += 8;
    }
    /* C011 walker range */
    o = u16(d, 0x11) - BASE;
    if (o + 4 <= len) { m->range_min = u16(d, o); m->range_max = u16(d, o + 2); }
    /* C00D dialogue: a pointer table with no count — stop at the first pointer
     * that does not land inside the image (mdt2png.py uses the same rule) */
    o = u16(d, 0xD) - BASE;
    while (m->ndlg < 48 && o + 2 <= len) {
        uint16_t p = u16(d, o + 2 * m->ndlg);
        if (p < BASE + 0x17 || (size_t)(p - BASE) >= len) break;
        m->dlg_ptr[m->ndlg++] = p;
    }
    return 0;
}

/* docs/TOWN.md §3: the ten town maps are ZELRES2[36..45] (kernel table records
 * 32..41, AH = 0x80 | index). */
int town_load_map(TownMap *m, const char *dir, int index)
{
    if (index < 0 || index > 9) return -1;
    size_t len;
    uint8_t *d = sar_load(dir, 1, 36 + index, 1, &len);
    if (!d) return -1;
    free(m->raw);
    if (town_parse_raw(m, d, len, index)) { free(d); memset(m, 0, sizeof *m); return -1; }
    return 0;
}

void town_free_map(TownMap *m) { free(m->raw); m->raw = NULL; m->rawlen = 0; }

/* ======================================================================== */
/* The dialogue box — town.bin dialogue_start 635A / dialogue_run 63C5       */
/* docs/TOWN.md §4 (geometry) and §6 (the script opcodes).                   */
/* ======================================================================== */

/* 65E6: the pixel width of the next word (stops at a space, '/' or an opcode) */
static int dlg_word_width(const uint8_t *s, const uint8_t *end)
{
    int w = 0;
    for (; s < end && *s < 0x80 && *s != ' ' && *s != '/'; s++)
        if (*s >= 0x20) w += FONT_ADVANCE[*s - 0x20];
    return w;
}
/* 6609: how many lines the text takes, word-wrapped at 0xA8 px */
static int dlg_count_lines(const uint8_t *s, const uint8_t *end)
{
    int n = 0, x = 0;
    for (; s < end; ) {
        uint8_t c = *s++;
        if (c & 0x80) return x ? n + 1 : n;
        if (c == '/') { n++; x = 0; continue; }
        if (c >= 0x20) x += FONT_ADVANCE[c - 0x20];
        if (c == ' ' && x + dlg_word_width(s, end) >= 0xA8) { x = 0; n++; }
    }
    return x ? n + 1 : n;
}

static void dlg_frame(Town *t)
{
    t->frame_no++;
    if (t->present) t->present(t);
}
/* 65A1 dialogue_end: wait for the button to be released, then for Space/Alt */
static void dlg_wait_key(Town *t)
{
    t->btn1_edge = t->btn2_edge = 0;
    for (int i = 0; i < 3000 && !t->quit; i++) {
        dlg_frame(t);
        if (t->btn1_edge || t->btn2_edge) return;
        if (!(t->buttons & 3)) break;
    }
    for (int i = 0; i < 6000 && !t->quit; i++) {
        dlg_frame(t);
        if (t->btn1_edge || t->btn2_edge) return;
    }
}
/* 64E6 newline: advance a line, scroll when the 8th is reached, and pause with
 * the red '|' marker every 7 lines while more text is left. */
static void dlg_newline(Town *t, int *shown, int *left, int total)
{
    if (t->dlg.nvis < 8) t->dlg.nvis++;
    else {                                                          /* 64FA: scroll */
        for (int i = 0; i < 7; i++) memcpy(t->dlg.line[i], t->dlg.line[i + 1], sizeof t->dlg.line[0]);
    }
    t->dlg.line[t->dlg.nvis - 1][0] = 0;
    (*shown)++;                                                     /* 6516 */
    if (*shown < 7 || total == 8) return;                           /* 651A */
    *left -= 7;                                                     /* 652E */
    t->dlg.marker = 1;                                              /* 6533: the red '|' */
    t->btn1_edge = t->btn2_edge = 0;
    for (int i = 0; i < 6000 && !t->quit; i++) {
        dlg_frame(t);
        if (!t->dlg_forced && t->btn2_edge) break;                  /* 6567: Alt cancels */
        if (t->btn1_edge) break;
    }
    t->dlg.marker = 0;                                              /* 657D */
    t->btn1_edge = 0; *shown = 0; t->sfx_request = 0x1D;            /* 658F */
}

void town_dialogue_run(Town *t, int script, int face_left)
{
    const TownMap *m = t->map;
    if (script < 0 || script >= m->ndlg || !m->raw) return;
    size_t o = (size_t)m->dlg_ptr[script] - BASE;
    if (o >= m->rawlen) return;
    const uint8_t *p = m->raw + o, *end = m->raw + m->rawlen;

    t->hero_anim |= 1;                                              /* 63C5 */
    t->sfx_request = 0x1E;                                          /* 636B */
    int total = dlg_count_lines(p, end);                            /* 63F2 */
    int left = total, shown = 0;
    int cl = total > 8 ? 8 : (total ? total : 1);                   /* 63FA */
    memset(&t->dlg, 0, sizeof t->dlg);
    t->dlg.x8 = face_left ? 7 : 11;                                 /* 6370: x8 7 or 11 */
    t->dlg.w4 = 0x2C;                                               /* 176 px */
    t->dlg.h  = cl * 10 + 6;                                        /* 6401 */
    t->dlg.y  = 24 + (0x56 - t->dlg.h) - ((0x40 - ((cl & 0xFE) << 3)) >> 1);   /* 640F..642A */
    t->dlg.nvis = 1;
    t->dlg.active = 1;
    t->message[0] = 0;
    int mlen = 0, x = 0, col = 0;

    for (int guard = 0; guard < 4096; guard++) {
        if (p >= end) break;
        uint8_t c = *p++;
        if (c == 0x2F) {
            dlg_newline(t, &shown, &left, total); x = col = 0;
            if (mlen < (int)sizeof t->message - 1) t->message[mlen++] = '\n';
            continue;
        }
        if (c == 0xFF) break;                                       /* 6471 */
        if (c == 0x83) { t->g->page[0x34] |= 0x80; t->g->page[0x9A] = 0xFF;   /* 6685: the Elf Crest */
                         town_apply_patches((TownMap *)m, t->g->page); break; }
        if (c == 0x8B) { t->g->page[0x04] |= 0x80;                  /* 664D */
                         town_apply_patches((TownMap *)m, t->g->page); break; }
        if (c == 0x85) { t->dlg_forced = 0xFF; t->dlg.active = 0;    /* 6695: restart with script 4 */
                         town_dialogue_run(t, 4, face_left); return; }
        if (c == 0x87) { dlg_wait_key(t); t->dlg.active = 0;         /* 66A2: then script 5 */
                         town_dialogue_run(t, 5, face_left); return; }
        /* 0x81 (yes/no) and 0x89 (Take / No Take) need the menu widget; the
         * port takes the "no" branch (scripts 13 and 6) for now. */
        if (c == 0x81) { t->dlg.active = 0; town_dialogue_run(t, 13, face_left); return; }
        if (c == 0x89) { t->dlg.active = 0; town_dialogue_run(t, 6, face_left); return; }
        if (c >= 0x80 || c < 0x20) continue;
        if (col < (int)sizeof t->dlg.line[0] - 1) {                  /* 6478: printable */
            t->dlg.line[t->dlg.nvis - 1][col++] = (char)c;
            t->dlg.line[t->dlg.nvis - 1][col] = 0;
        }
        x += FONT_ADVANCE[c - 0x20];                                 /* 64B8 */
        if (mlen < (int)sizeof t->message - 1) t->message[mlen++] = (char)(c == '\\' ? '\'' : c);
        if (c == ' ' && x + dlg_word_width(p, end) >= 0xA8) {         /* 64C7 */
            dlg_newline(t, &shown, &left, total); x = col = 0;        /* t->message keeps the
                         unwrapped text: only an explicit 0x2F breaks a line there */
        }
    }
    t->message[mlen] = 0;
    dlg_wait_key(t);                                                 /* 65A1 */
    t->dlg.active = 0;                                               /* 6396: the box comes down */
    t->btn1_edge = t->btn2_edge = 0; t->dlg_forced = 0;
}

/* draw the box and its lines over the finished town frame (63C5 / 6478) */
static void dlg_render(uint8_t *fb, const Town *t)
{
    if (!t->dlg.active || !t->font) return;
    vid_window(fb, 0xFF, t->dlg.x8 * 2, t->dlg.y, t->dlg.w4, t->dlg.h);
    for (int l = 0; l < t->dlg.nvis; l++) {
        int x = t->dlg.x8 * 8 + 4, y = t->dlg.y + l * 10 + 4;
        for (const char *c = t->dlg.line[l]; *c; c++) {
            uint8_t ch = (uint8_t)*c;
            if (ch < 0x20 || ch >= 0x80) continue;
            vid_putchar(fb, t->font, ch, 1, x - FONT_XOFF[ch - 0x20], y);
            x += FONT_ADVANCE[ch - 0x20];
        }
    }
    if (t->dlg.marker)                                               /* 6533 */
        vid_putchar(fb, t->font, 0x7C, 2, t->dlg.x8 * 8 + 0x54, t->dlg.y + 0x4A);
}

const char *town_dialogue(const TownMap *m, int script)
{
    static char buf[512];
    if (script < 0 || script >= m->ndlg || !m->raw) return "";
    size_t o = m->dlg_ptr[script] - BASE;
    size_t n = 0;
    while (o < m->rawlen && m->raw[o] != 0xFF && n < sizeof buf - 1) {
        uint8_t c = m->raw[o++];
        if (c == 0x2F) buf[n++] = '\n';                                 /* 64E6 newline */
        else if (c == '\\') buf[n++] = '\'';                            /* the font draws \ as an apostrophe */
        else if (c >= 0x80) break;                                      /* an opcode ends the plain text */
        else buf[n++] = (char)c;
    }
    buf[n] = 0;
    return buf;
}

/* 0x6AED  the same shape as the cavern list but with byte pokes:
 * {u16 flag_ptr, u8 mask, {u16 ptr, u8 val}.. FFFF}.. FFFF */
int town_apply_patches(TownMap *m, const uint8_t page[256])
{
    if (!m->raw || !m->patches) return 0;
    uint8_t *d = m->raw;
    size_t len = m->rawlen, o = (size_t)(m->patches - BASE);
    int applied = 0, guard = 0;
    while (o + 3 <= len && ++guard < 256) {
        uint16_t fp = u16(d, o);
        if (fp == 0xFFFF) break;
        uint8_t mask = d[o + 2];
        o += 3;
        int on = page && (page[fp & 0xFF] & mask) != 0;
        while (o + 3 <= len) {
            uint16_t addr = u16(d, o);
            if (addr == 0xFFFF) { o += 2; break; }
            if (on && addr >= BASE && (size_t)(addr - BASE) < len) { d[addr - BASE] = d[o + 2]; applied++; }
            o += 3;
        }
    }
    if (applied) town_parse_raw(m, d, len, m->index);   /* the tables moved with the pokes */
    return applied;
}

/* ------------------------------------------------------------- tile bank */
/* gtmcga 3AF9 / tools/mdt2png.py decode48_town: three big-endian planes per
 * row; the cell's `type` byte says which plane is the sky mask instead. */
static void decode48_town(const uint8_t *c, int typ, Cell8 *out, uint8_t sky[8][8])
{
    for (int row = 0; row < 8; row++) {
        int w[3];
        for (int i = 0; i < 3; i++) w[i] = (c[row * 6 + 2 * i] << 8) | c[row * 6 + 2 * i + 1];
        int a = w[0], b = w[1], cc = w[2], mk = 0;
        if (typ == 1)      { cc = 0; mk = w[2]; }
        else if (typ == 2) { b = 0;  mk = w[1]; }
        else if (typ == 3) { a = 0;  mk = w[0]; }
        else if (typ == 4) { mk = 0xFFFF; }
        int p[16];
        for (int x = 0; x < 16; x++)
            p[x] = (((cc >> (15 - x)) & 1) << 2) | (((b >> (15 - x)) & 1) << 1) | ((a >> (15 - x)) & 1);
        for (int k = 0; k < 8; k++) {
            out->px[row][k] = (uint8_t)((p[2 * k] << 3) | p[2 * k + 1]);
            sky[row][k] = (uint8_t)(((mk >> (14 - 2 * k)) & 3) == 3);
        }
    }
}

int town_load_tiles(TownTiles *t, const char *dir, int index)
{
    if (index < 0 || index > 2) return -1;
    size_t len;
    uint8_t *d = sar_load(dir, 1, 33 + index, 1, &len);                 /* 6DCE: cpat/mpat/dpat */
    if (!d || len < 0x100) { free(d); return -1; }
    memset(t, 0, sizeof *t);
    t->index = index;
    size_t off_block = u16(d, 2), off_anim = u16(d, 4);
    int n = (int)((len - 0x100) / 48);
    if (n > 256) n = 256;
    for (int i = 0; i < n; i++) {
        int typ = (6 + (size_t)i < off_block && d[6 + i] < 5) ? d[6 + i] : 0;
        decode48_town(d + 0x100 + i * 48, typ, &t->cell[i], t->sky[i]);
        t->present[i] = 1;
    }
    if (off_block < 0x100 && off_block < len) {
        int nb = d[off_block];
        if (nb > 16) nb = 16;
        for (int i = 0; i < nb && off_block + 1 + i < len; i++) t->block[i] = d[off_block + 1 + i];
        t->nblock = nb;
    }
    if (off_anim < 0x100 && off_anim < len) {
        int na = d[off_anim];
        if (na > 32) na = 32;
        for (int i = 0; i < na && off_anim + 2 + 2 * i < len; i++) {
            t->anim_from[i] = d[off_anim + 1 + 2 * i]; t->anim_to[i] = d[off_anim + 2 + 2 * i];
        }
        t->nanim = na;
    }
    free(d);
    return 0;
}

/* gtmcga 3A71: colour 0 transparent, colour 7 (white) drawn as black */
static void decode48_sprite(const uint8_t *c, Cell2 *out)
{
    memset(out, 0, sizeof *out);
    for (int row = 0; row < 8; row++) {
        int a = (c[row * 6] << 8) | c[row * 6 + 1];
        int b = (c[row * 6 + 2] << 8) | c[row * 6 + 3];
        int cc = (c[row * 6 + 4] << 8) | c[row * 6 + 5];
        int p[16];
        for (int x = 0; x < 16; x++)
            p[x] = (((cc >> (15 - x)) & 1) << 2) | (((b >> (15 - x)) & 1) << 1) | ((a >> (15 - x)) & 1);
        for (int k = 0; k < 8; k++) {
            int l = p[2 * k], r = p[2 * k + 1];
            if (!l && !r) { out->mask[row][k] = 0; continue; }
            out->mask[row][k] = 1;
            out->px[row][k] = (uint8_t)(((l == 7 ? 0 : l) << 3) | (r == 7 ? 0 : r));
        }
    }
}

int town_load_sprites(TownSprites *s, const char *dir, int index)
{
    if (index < 0 || index > 1) return -1;
    size_t len;
    uint8_t *d = sar_load(dir, 1, 29 + index, 1, &len);                 /* 6D88: mman/cman */
    if (!d || len < 0x100) { free(d); return -1; }
    memset(s, 0, sizeof *s);
    s->index = index;
    for (int sp = 0; sp < 5; sp++)
        for (int f = 0; f < 8; f++)
            memcpy(s->frame[sp][f], d + sp * 48 + f * 6, 6);
    int n = (int)((len - 0x100) / 48);
    if (n > 256) n = 256;
    for (int i = 0; i < n; i++) decode48_sprite(d + 0x100 + i * 48, &s->cell[i]);
    s->ncells = n;
    free(d);
    return 0;
}

int town_load_hero(TownHero *h, const char *dir)
{
    size_t len;
    uint8_t *d = sar_load(dir, 1, 31, 1, &len);                         /* 6E1E: tman.grp, 46 cells */
    if (!d) return -1;
    memset(h, 0, sizeof *h);
    int n = (int)(len / 48);
    if (n > 64) n = 64;
    for (int i = 0; i < n; i++) decode48_sprite(d + i * 48, &h->cell[i]);
    h->ncells = n;
    free(d);
    return 0;
}

/* ------------------------------------------------------------- the engine */
/* 6A3B / 6A59: 5 hero frames x 6 tman cell indices (col0 rows 0-2, col1 rows 0-2) */
static const uint8_t HERO_L[5][6] = {{0,2,4,1,3,5},{6,8,10,7,9,11},{0,12,14,1,13,15},{6,16,18,7,17,19},{20,22,24,21,23,25}};
static const uint8_t HERO_R[5][6] = {{26,28,30,27,29,31},{32,34,36,33,35,37},{26,38,40,27,39,41},{32,42,44,33,43,45},{20,22,24,21,23,25}};

int town_hero_col(const Town *t) { return t->scroll_col + t->hero_scr_col + 4; }

/* 0x686E  a ground cell in the bank's block list stops the hero. */
int town_cell_walkable(const Town *t, uint8_t v)
{
    for (int i = 0; i < t->tiles->nblock; i++) if (t->tiles->block[i] == v) return 0;
    return 1;
}

/* 0x6890  a solid NPC (flags 0x40) on this column blocks walking. */
static int npc_solid_at(const Town *t, int col)
{
    for (int i = 0; i < t->map->nnpcs; i++)
        if (t->map->npcs[i].col == (uint16_t)col && (t->map->npcs[i].flags & 0x40)) return 1;
    return 0;
}
static TownNpc *npc_find(Town *t, int col)                              /* 6A94 */
{
    for (int i = 0; i < t->map->nnpcs; i++) if (t->map->npcs[i].col == (uint16_t)col) return &t->map->npcs[i];
    return NULL;
}

static uint8_t grid_cell(const Town *t, int col, int row)
{
    if (col < 0 || col >= t->map->width) return 0;
    return t->map->grid[col][row & 7];
}
static void grid_set(Town *t, int col, int row, uint8_t v)
{
    if (col < 0 || col >= t->map->width) return;
    t->map->grid[col][row & 7] = v;
}

/* 0x6C2B / 0x6C4E  NPC markers live in the map grid itself. */
static void npc_place_markers(Town *t)
{
    for (int i = 0; i < t->map->nnpcs; i++) {
        TownNpc *n = &t->map->npcs[i];
        n->saved = grid_cell(t, n->col, TOWN_NPC_ROW);
        grid_set(t, n->col, TOWN_NPC_ROW, TOWN_MARK);
    }
}
static void npc_restore_tiles(Town *t)
{
    for (int i = 0; i < t->map->nnpcs; i++) {
        TownNpc *n = &t->map->npcs[i];
        if (n->saved != TOWN_MARK) grid_set(t, n->col, TOWN_NPC_ROW, n->saved);
    }
}

/* 0x6B41  the eight behaviours */
static void npc_idle_anim(TownNpc *n)                                   /* 6BD2 */
{
    n->anim = (uint8_t)(n->anim + 0x10);
    if (n->anim & 0x30) return;
    n->anim ^= 1;
}
static void npc_step(Town *t, TownNpc *n, int every4)                   /* 6B6C / 6BA6 */
{
    n->anim = (uint8_t)(n->anim + 0x10);
    if (n->anim & (every4 ? 0x30 : 0x10)) return;
    n->anim = (uint8_t)((n->anim & 0x30) | ((n->anim + 1) & 0x0F));
    if (n->sprite & 0x80) { n->col--; if (t->map->range_min >= n->col) n->sprite &= 0x7F; }
    else                  { n->col++; if (t->map->range_max <= n->col) n->sprite |= 0x80; }
}
static void npc_wander(Town *t, TownNpc *n, int every4)                 /* 6BEC / 6C19 */
{
    (void)t;
    n->anim = (uint8_t)(n->anim + 0x10);
    if (n->anim & (every4 ? 0x30 : 0x10)) return;
    n->anim = (uint8_t)((n->anim & 0x30) | ((n->anim + 1) & 0x0F));
    if (!(n->anim & 7)) { n->sprite ^= 0x80; return; }                  /* 6C05: turn every 8 steps */
    if (n->sprite & 0x80) n->col--; else n->col++;
}
void town_npc_update(Town *t)                                           /* 6B1C */
{
    npc_restore_tiles(t);
    int hc = town_hero_col(t);
    for (int i = 0; i < t->map->nnpcs; i++) {
        TownNpc *n = &t->map->npcs[i];
        switch (n->type) {
        case 0: n->sprite |= 0x80; if (hc >= (int)n->col) n->sprite &= 0x7F; npc_idle_anim(n); break;   /* 6B51 */
        case 1: npc_step(t, n, 0); break;
        case 2: npc_step(t, n, 1); break;
        case 3: n->sprite |= 0x80; if (hc >= (int)n->col) n->sprite &= 0x7F; break;                     /* 6BB7 */
        case 4: npc_idle_anim(n); break;
        case 5: npc_wander(t, n, 0); break;
        case 6: npc_wander(t, n, 1); break;
        default: break;                                                                                 /* 6C2A static */
        }
    }
    npc_place_markers(t);
}

/* 0x6781 / 0x67F4  one column of walking (the hero is two columns wide). */
static void walk_left(Town *t)
{
    if (!town_cell_walkable(t, grid_cell(t, t->scroll_col + t->hero_scr_col + 3, TOWN_GROUND))) return;
    if (npc_solid_at(t, t->scroll_col + t->hero_scr_col + 3)) return;
    t->hero_anim = (uint8_t)((t->hero_anim + 1) & 3);
    t->hero_flags |= 1;
    if (t->hero_scr_col >= 0x0B) { t->hero_scr_col--; return; }         /* 67BF */
    if (t->scroll_col == 0)      { t->hero_scr_col--; return; }         /* 67CB: can reach -1 */
    t->scroll_col--;
}
static void walk_right(Town *t)
{
    if (!town_cell_walkable(t, grid_cell(t, t->scroll_col + t->hero_scr_col + 6, TOWN_GROUND))) return;
    if (npc_solid_at(t, t->scroll_col + t->hero_scr_col + 5)) return;
    t->hero_anim = (uint8_t)((t->hero_anim + 1) & 3);
    t->hero_flags &= (uint8_t)~1;
    if (t->hero_scr_col < 0x10) { t->hero_scr_col++; return; }          /* 6832 */
    if (t->scroll_col + 1 == t->map->width - 0x23) { t->hero_scr_col++; return; }   /* 683E: can reach 0x1C */
    t->scroll_col++;
}

/* 0x623F  Space: an NPC 1..3 columns ahead answers (unless flags & 0xC0). */
static void check_talk(Town *t)
{
    if (!t->btn1_edge) return;
    t->btn1_edge = 0;
    int dx = town_hero_col(t);
    int step = (t->hero_flags & 1) ? -1 : 1;
    for (int i = 1; i <= 3; i++) {
        int c = dx + step * i;
        if (grid_cell(t, c, TOWN_NPC_ROW) != TOWN_MARK) continue;
        TownNpc *n = npc_find(t, c);
        if (!n || (n->flags & 0xC0)) return;                            /* 6288 */
        if (step > 0) n->sprite |= 0x80; else n->sprite &= 0x7F;        /* face the hero */
        n->anim |= 1;
        town_dialogue_run(t, n->script, (t->hero_flags & 1) != 0);      /* 638F */
        return;
    }
}

/* 0x62ED  an 0x80-flagged NPC exactly two columns ahead, facing us, talks once. */
static void check_auto_talk(Town *t)
{
    int dx = town_hero_col(t);
    int step = (t->hero_flags & 1) ? -2 : 2;
    int c = dx + step;
    if (grid_cell(t, c, TOWN_NPC_ROW) != TOWN_MARK) return;
    TownNpc *n = npc_find(t, c);
    if (!n || !(n->flags & 0x80)) return;
    if (step > 0) { if (!(n->sprite & 0x80)) return; }                  /* 631C: he must face us */
    else          { if (n->sprite & 0x80) return; }
    n->anim |= 1;
    n->flags &= 0x7F;                                                   /* 635A: only once */
    t->sfx_request = 0x1E;
    t->dlg_forced = 0xFF;                                               /* 62ED: uncancellable */
    town_dialogue_run(t, n->script, (t->hero_flags & 1) != 0);
}

/* 0x6E29  "up" in front of a door (the hero's column ±1). */
static void enter_door(Town *t)
{
    t->hero_anim |= 1;
    int col = town_hero_col(t);
    for (int i = 0; i < t->map->ndoors; i++) {
        int dc = t->map->doors[i].col;
        if (dc != col && dc != col + 1 && dc != col - 1) continue;
        t->hero_anim = 4;                                               /* 6E5B: back view */
        uint8_t dest = t->map->doors[i].dest;
        if (dest == 0xFF)      { t->action = TOWN_PAST_DOOR; t->action_arg = 0; }
        else if (dest >= 8)    { t->action = TOWN_TO_CAVERN; t->action_arg = dest - 8; }
        else                   { t->action = TOWN_SHOP;      t->action_arg = dest; }
        return;
    }
}

/* 0x6CB5  walking off an edge: the exit record picks another town or a cavern. */
static void check_edge_exit(Town *t)
{
    int left;
    if (t->hero_scr_col < 0) left = 1;
    else if (t->hero_scr_col == 0x1C) left = 0;
    else return;
    npc_restore_tiles(t);
    const TownExit *e = NULL;
    for (int i = 0; i < t->map->nexits; i++) {
        int is_left = t->map->exits[i].flags & 1;
        if (is_left == left) { e = &t->map->exits[i]; break; }
    }
    if (!e) { t->hero_scr_col = left ? 0 : 0x1B; npc_place_markers(t); return; }
    if (e->flags & 0xFE) { t->action = TOWN_TO_CAVERN; t->action_arg = e->dest; return; }   /* 6CD9 */
    t->action = TOWN_TO_TOWN; t->action_arg = e->dest | (left ? 0x100 : 0);                 /* 6CE1 */
}

/* re-mark the NPC row after the shell moved the hero (6168) */
void town_npc_markers_reset(Town *t) { npc_place_markers(t); }

/* 0x7DE1  place the hero on map column `col`, clamping the scroll. */
void town_place(Town *t, int col, int face_left)
{
    int max = t->map->width - 0x24;
    int sc = col - 17;
    if (sc < 0) sc = 0;
    if (sc > max) sc = max;
    t->scroll_col = sc;
    t->hero_scr_col = col - 4 - sc;
    if (t->hero_scr_col < 0) t->hero_scr_col = 0;
    if (t->hero_scr_col > 0x1B) t->hero_scr_col = 0x1B;
    t->hero_flags = (uint8_t)(face_left ? 1 : 0);
    t->hero_anim = 1;
    npc_place_markers(t);
}

void town_init(Town *t, TownMap *m, TownTiles *ti, TownSprites *s, TownHero *h, Game *g)
{
    memset(t, 0, sizeof *t);
    t->map = m; t->tiles = ti; t->spr = s; t->hero = h; t->g = g;
    t->hero_scr_col = 0x0D;
    t->hero_anim = 1;
}

/* 0x61FC  one iteration of the town main loop. */
/* 0x68F3  Enter: swap select.bin in from arena:C000 and run the status screen
 * (status.c).  town.bin then clears the playfield, repaints the backdrop and
 * forces a full redraw; nothing reads menu_result [FF4B] on this side. */
static void check_status_menu(Town *t)
{
    if (!t->menu_key) { t->menu_debounce = 0; return; }                 /* 68F3 */
    if (t->menu_debounce) return;
    t->g->sfx_request = 0x0B;                                           /* 68FC */
    status_run_town(t);                                                 /* 6909 */
    t->menu_debounce = 0xFF;
    t->btn1_edge = t->btn2_edge = 0;                                    /* 692D/6932 */
}

void town_step(Town *t)
{
    town_npc_update(t);                                                 /* 68AC */
    t->frame_no++;
    if (t->present) t->present(t);
    if (t->action) return;
    check_status_menu(t);                                               /* 68F3 */
    if (t->action) return;
    check_edge_exit(t);                                                 /* 6202 */
    if (t->action) return;
    check_talk(t);                                                      /* 6205 */
    if (!t->was_idle) check_auto_talk(t);                               /* 6208 */
    t->was_idle = 0;
    uint8_t d = t->dirs;
    if (d == DIR_UP)              { enter_door(t); return; }            /* 621D */
    if ((d & 0xC) == DIR_LEFT)    { walk_left(t);  return; }
    if ((d & 0xC) == DIR_RIGHT)   { walk_right(t); return; }
    t->hero_anim |= 1; t->was_idle = 0xFF;                              /* 6234 */
}

/* ------------------------------------------------------------- rendering */
/* docs/TOWN.md §4.3: the 8 map rows are drawn at y 78..141, x = 48 + col*8;
 * rows 0-2 show the backdrop through the cells' sky mask (the port paints a
 * flat sky instead of running ympd/ckpd).  The HUD is fight.bin's. */
#define TOWN_Y   78
#define SKY_IDX  0x2C

static void blit_cell8(uint8_t *fb, const Cell8 *c, const uint8_t sky[8][8], int x, int y)
{
    for (int r = 0; r < 8; r++) {
        int yy = y + r;
        if (yy < 14 || yy >= 158) continue;
        for (int k = 0; k < 8; k++) {
            int xx = x + k;
            if (xx < 48 || xx >= 48 + 224) continue;
            fb[yy * FB_W + xx] = (sky && sky[r][k]) ? SKY_IDX : c->px[r][k];
        }
    }
}
static void blit_sprite(uint8_t *fb, const Cell2 *c, int x, int y)
{
    for (int r = 0; r < 8; r++) {
        int yy = y + r;
        if (yy < 14 || yy >= 158) continue;
        for (int k = 0; k < 8; k++) {
            if (!c->mask[r][k]) continue;
            int xx = x + k;
            if (xx < 48 || xx >= 48 + 224) continue;
            fb[yy * FB_W + xx] = c->px[r][k];
        }
    }
}

void town_render(uint8_t *fb, const Town *t)
{
    memset(fb, 0, FB_W * FB_H);
    for (int y = 14; y < TOWN_Y; y++) memset(fb + y * FB_W + 48, SKY_IDX, 224);
    for (int c = 0; c < TOWN_SCR_COLS; c++) {
        int mc = t->scroll_col + 4 + c;                                 /* gtmcga 3074 skips 4 columns */
        if (mc < 0 || mc >= t->map->width) continue;
        for (int r = 0; r < TOWN_ROWS; r++) {
            uint8_t v = t->map->grid[mc][r];
            if (v == TOWN_MARK) {                                       /* an NPC marker: draw the tile below it */
                for (int i = 0; i < t->map->nnpcs; i++)
                    if (t->map->npcs[i].col == (uint16_t)mc) { v = t->map->npcs[i].saved; break; }
                if (v == TOWN_MARK) v = 0;
            }
            if (!v || !t->tiles->present[v]) continue;
            blit_cell8(fb, &t->tiles->cell[v], r < 3 ? t->tiles->sky[v] : NULL, 48 + c * 8, TOWN_Y + r * 8);
        }
    }
    /* NPCs: 2 columns x 3 rows on rows 5-7 (34EC: frame = (anim & 3) + 4*right) */
    for (int i = 0; i < t->map->nnpcs; i++) {
        const TownNpc *n = &t->map->npcs[i];
        int sc = (int)n->col - (t->scroll_col + 4);
        if (sc < -1 || sc >= TOWN_SCR_COLS) continue;
        int sp = n->sprite & 0x7F;
        if (sp > 4) sp = 4;
        int f = (n->anim & 3) + ((n->sprite & 0x80) ? 0 : 4);
        const uint8_t *cells = t->spr->frame[sp][f & 7];
        for (int k = 0; k < 6; k++) {
            int cell = cells[k];
            if (!cell || cell > t->spr->ncells) continue;
            int cx = sc + k / 3, cy = TOWN_NPC_ROW + k % 3;
            if (cx < 0 || cx >= TOWN_SCR_COLS) continue;
            blit_sprite(fb, &t->spr->cell[cell - 1], 48 + cx * 8, TOWN_Y + cy * 8);
        }
    }
    /* the hero: the same 2x3 layout, tman cells (0-based) */
    if (t->hero_scr_col >= 0 && t->hero_scr_col < 0x1B) {
        const uint8_t *cells = (t->hero_flags & 1) ? HERO_L[t->hero_anim > 4 ? 4 : t->hero_anim]
                                                   : HERO_R[t->hero_anim > 4 ? 4 : t->hero_anim];
        for (int k = 0; k < 6; k++) {
            int cell = cells[k];
            if (cell >= t->hero->ncells) continue;
            int cx = t->hero_scr_col + k / 3, cy = TOWN_NPC_ROW + k % 3;
            if (cx < 0 || cx >= TOWN_SCR_COLS) continue;
            blit_sprite(fb, &t->hero->cell[cell], 48 + cx * 8, TOWN_Y + cy * 8);
        }
    }
    dlg_render(fb, t);                                              /* 63C5 */
}
