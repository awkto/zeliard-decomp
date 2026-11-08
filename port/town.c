/* town.c — the town engine (town.bin) and the town maps.
 *
 * Port of the walking / NPC / door / edge-exit part of src/town.c; the shops
 * (src/shops.c) and the status screen (select.bin, status.c) run on top of it;
 * the parallax backdrop painters are not implemented.  Hex tags are town.bin addresses; docs/TOWN.md is the spec.  */
#include "town.h"
#include "audio.h"
#include "sar.h"
#include "render.h"
#include "status.h"
#include "text.h"
#include "video.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BASE 0xC000
static uint16_t u16(const uint8_t *d, size_t o) { return (uint16_t)(d[o] | d[o + 1] << 8); }

/* A BASE-relative file pointer as an offset into the image, clamped to `len`
 * -- same guard as map.c's: a pointer below BASE wraps size_t and walks the
 * `o + n <= len` guards back into range (found by fuzz_grp). */
static size_t off_in(uint16_t ptr, size_t len)
{ return ptr >= BASE && (size_t)(ptr - BASE) <= len ? (size_t)(ptr - BASE) : len; }

/* ------------------------------------------------------------------- map */
/* docs/TOWN.md §3.  The ten town maps are ZELRES2[36..45] (kernel table
 * records 32..41, AH = 0x80 | index). */
/* parse every table out of an already-decompressed image the caller owns */
static int town_parse_raw(TownMap *m, uint8_t *d, size_t len, int index)
{
    memset(m, 0, sizeof *m);
    m->raw = d; m->rawlen = len; m->index = index;
    if (len < 0x17) return -1;          /* the fixed header, read below (fuzz_grp) */
    m->width = u16(d, 2);
    if (m->width < 0x24 || m->width > TOWN_MAX_W) return -1;
    if ((size_t)(0x17 + m->width * TOWN_ROWS) > len) return -1;
    size_t lv = off_in(u16(d, 0), len);
    if (lv + 5 <= len) {
        m->music = (uint8_t)((d[lv] >> 1) & 0xF); m->gfx = d[lv + 1];
        m->town_flags = d[lv + 3]; m->tileset = d[lv + 4];
    }
    size_t lab = off_in(u16(d, 4), len);
    m->label_ptr = u16(d, 4);
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
    size_t o = off_in(u16(d, 9), len);
    while (o + 3 <= len && u16(d, o) != 0xFFFF && m->ndoors < 16) {
        m->doors[m->ndoors].col = u16(d, o); m->doors[m->ndoors].dest = d[o + 2];
        m->ndoors++; o += 3;
    }
    /* C007 exits: no terminator — the records run up to the door list */
    size_t eo = off_in(u16(d, 7), len), edn = off_in(u16(d, 9), len);
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
        size_t co = off_in(cp, len);
        for (int i = 0; i < n && co + 5 * i + 5 <= len; i++) {
            m->caves[i].col = u16(d, co + 5 * i); m->caves[i].row = d[co + 5 * i + 2];
            m->caves[i].side = d[co + 5 * i + 3]; m->caves[i].map = d[co + 5 * i + 4];
            m->ncaves = i + 1;
        }
    }
    /* C00F NPCs (8 bytes, FFFF ends) */
    o = off_in(u16(d, 0xF), len);
    while (o + 8 <= len && u16(d, o) != 0xFFFF && m->nnpcs < 32) {
        TownNpc *n = &m->npcs[m->nnpcs++];
        n->col = u16(d, o); n->sprite = d[o + 2]; n->saved = d[o + 3]; n->anim = d[o + 4];
        n->type = d[o + 5]; n->flags = d[o + 6]; n->script = d[o + 7];
        o += 8;
    }
    /* C011 walker range */
    o = off_in(u16(d, 0x11), len);
    if (o + 4 <= len) { m->range_min = u16(d, o); m->range_max = u16(d, o + 2); }
    /* C00D dialogue: a pointer table with no count — stop at the first pointer
     * that does not land inside the image (mdt2png.py uses the same rule) */
    o = off_in(u16(d, 0xD), len);
    while (m->ndlg < 48 && o + 2 * (size_t)m->ndlg + 2 <= len) {
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

/* ------------------------------------------- wait-loop boundaries (town.h) */
WaitClock wait_clock;

void wait_boundary_now(Game *g, Town *t, const char *site)
{
    if (!wait_clock.fn) return;
    wait_reset();
    wait_clock.fn(g, t, site);
}
int wait_poll(Game *g, Town *t, const char *site)
{
    if (!wait_clock.fn) return 0;
    if (!wait_clock.armed && wait_clock.ticks < WAIT_TICKS) return 0;
    wait_boundary_now(g, t, site);
    return 1;
}

/* STICK's timer plays and clears FF75 a few ticks after it is written: the
 * frame's busy-wait (before 68ED) and the drawing of a dialogue box always
 * outlast that, so a town request is gone by the next boundary */
static void town_sound(Town *t)
{
    if (!t->sfx_request) return;
    audio_sfx_request(t->sfx_request);
    t->sfx_request = 0;
}

static void town_draw_frame(Town *t);
static void town_flush_rows(Town *t);
/* the dialogue's waits call the full town frame 68AC: the NPCs keep moving
 * (6B1C), only the talker is frozen (type 7, check_talk) */
static void dlg_frame(Town *t)
{
    town_npc_update(t);                                                 /* 68AC -> 6B1C */
    town_flush_rows(t);                                                 /* 68B5: GT_FLUSH steps the animated sky cells (the castle's
                                                                         * flag, jashiin_kill f743: it kept still under a text box) */
    t->dlg.hero_shown = 0;                                              /* the hero is redrawn */
    t->frame_no++;
    if (t->present) t->present(t);
    town_sound(t);
    if (t->frame_end) t->frame_end(t);                                  /* 68ED (65AB..65E3 -> 68AC) */
}
/* one 236.7 Hz tick inside the yes/no widget: no town frame runs (NPCs stand
 * still, nothing reaches 68ED); the screen is presented every 4*speed ticks
 * so a front end still sees it and can feed it keys */
static void dlg_tick(Town *t, int *acc)
{
    wait_tick();
    if (++*acc < WAIT_TICKS) return;
    *acc = 0;
    t->frame_no++;
    if (t->present) t->present(t);
}

#define DLG_POS_Y 0x18           /* [7C4C] low byte: 6370 0718 / 0B18 */

/* 0x74D3  yes_no_prompt, the widget the shops reach through vector [6008].
 * The dialogue box (6655 / 66AD) puts it beside itself; the row list is drawn
 * by dlg_render, so the box and the menu come back every frame.  Returns the
 * chosen row, or 1 (the "no" row) when Alt cancels.
 *
 * It is town.bin's menu_select 7344: poll (site 7358) -> Alt -> Space ->
 * U/D.  A move is the 10 x 4-tick cursor animation and then straight back to
 * the poll, so a held direction repeats every 40 ticks (no release needed). */
static int dlg_menu(Town *t, const char *a, const char *b, int x4, int y, int w4, int ix4)
{
    t->dlg.menu_item[0] = a; t->dlg.menu_item[1] = b;
    t->dlg.menu_n = 2; t->dlg.menu_row = 0; t->dlg.menu_left = 0;
    t->dlg.menu_x4 = x4; t->dlg.menu_y = y; t->dlg.menu_w4 = w4; t->dlg.menu_h = 0x19;
    t->dlg.menu_ix4 = ix4;
    t->btn1_edge = t->btn2_edge = 0;                                    /* 7344 */
    town_sound(t);                                                      /* the box took longer than a tick */
    int row = 0, acc = 0;
    wait_arm();
    for (long i = 0; i < 6000L * WAIT_TICKS && !t->quit; i++) {
        wait_poll(t->g, t, "7358");
        if (t->quit) break;
        if (t->btn2_edge) { t->btn2_edge = 0; row = 1; break; }      /* Alt = No */
        if (t->btn1_edge) { t->btn1_edge = 0; t->sfx_request = 0x1F; break; }
        uint8_t d = (uint8_t)(t->dirs & 3);
        int moved = 0;
        if (d == DIR_UP && row) { row--; moved = 1; }                   /* 7380 */
        else if (d == DIR_DOWN && row < 1) { row++; moved = 1; }        /* 73EE */
        if (!moved) { dlg_tick(t, &acc); continue; }
        t->dlg.menu_row = row;
        for (int k = 0; k < 10 * 4; k++) dlg_tick(t, &acc);             /* 747B / 74A7 */
    }
    t->dlg.menu_n = 0; t->dlg.menu_left = 1;
    return row;
}

/* 65A1 dialogue_end: wait for the button to be released, then for Space/Alt */
/* 0x65A1  the end-of-text wait: FF1D/FF1E cleared, then frames (6743 + 68AC)
 * while a direction key is still held (65C1: FF17, the held *dirs*), then
 * frames until a press edge or any direction key (65DE `jz 65C8`: a held
 * direction ends the wait too -- #85, paguro_return: Left closes "Thanks
 * again. Really." and the hero walks on the same frame run). */
static void dlg_wait_key(Town *t)
{
    t->btn1_edge = t->btn2_edge = 0;                                    /* 65A1/65A6 */
    for (int i = 0; i < 3000 && !t->quit; i++) {                        /* 65AB */
        dlg_frame(t);
        if (t->btn1_edge || t->btn2_edge) return;                       /* 65B1/65B9 */
        if (!t->dirs) break;                                            /* 65C1 */
    }
    for (int i = 0; i < 6000 && !t->quit; i++) {                        /* 65C8 */
        dlg_frame(t);
        if (t->btn1_edge || t->btn2_edge) return;                       /* 65CE/65D6 */
        if (t->dirs) return;                                            /* 65DE */
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

    int was = t->dlg.hero_shown ? t->dlg.hero_shown : t->hero_anim + 1;
    t->hero_anim |= 1;                                              /* 63C5 */
    t->sfx_request = 0x1E;                                          /* 636B */
    int total = dlg_count_lines(p, end);                            /* 63F2 */
    int left = total, shown = 0;
    int cl = total > 8 ? 8 : (total ? total : 1);                   /* 63FA */
    /* a yes/no widget the previous script left on screen stays (6676 / 66CE
     * jump straight to 63C5, nothing erases it) */
    int keep = t->dlg.menu_left, krow = t->dlg.menu_row, kx4 = t->dlg.menu_x4, ky = t->dlg.menu_y,
        kw4 = t->dlg.menu_w4, kh = t->dlg.menu_h, kix4 = t->dlg.menu_ix4;
    const char *ki0 = t->dlg.menu_item[0], *ki1 = t->dlg.menu_item[1];
    memset(&t->dlg, 0, sizeof t->dlg);
    if (keep) {
        t->dlg.menu_left = 1; t->dlg.menu_row = krow; t->dlg.menu_ix4 = kix4;
        t->dlg.menu_x4 = kx4; t->dlg.menu_y = ky; t->dlg.menu_w4 = kw4; t->dlg.menu_h = kh;
        t->dlg.menu_item[0] = ki0; t->dlg.menu_item[1] = ki1;
    }
    if (was != t->hero_anim + 1) t->dlg.hero_shown = was;
    t->dlg.x8 = face_left ? 7 : 11;                                 /* 6370: x8 7 or 11 */
    t->dlg.w4 = 0x2C;                                               /* 176 px */
    t->dlg.h  = cl * 10 + 6;                                        /* 6401 */
    t->dlg.y  = DLG_POS_Y + (0x56 - t->dlg.h) - ((0x40 - ((cl & 0xFE) << 3)) >> 1);   /* 640F..642A */
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
        if (c == 0x81) {                                             /* 6655: yes/no */
            int no = dlg_menu(t, "Yes", "No", t->dlg.x8 * 2 + 0x19,  /* 6655: [7C4C] + 193F */
                              DLG_POS_Y + 0x3F, 0x0C, 1);            /* 666B: +0103 */
            t->dlg.active = 0;
            town_dialogue_run(t, no ? 13 : 12, face_left);            /* 6676 */
            return;
        }
        if (c == 0x89) {                                             /* 66AD: the Asbestos cape */
            int no = dlg_menu(t, "Take", "No Take", t->dlg.x8 * 2 + 0x18,  /* 66AD: [7C4C] + 1832 */
                              DLG_POS_Y + 0x32, 0x12, 2);            /* 66C3: +0203 */
            t->dlg.active = 0;
            if (no) { town_dialogue_run(t, 6, face_left); return; }   /* 66D1 */
            if (t->g->almas < 2500) { town_dialogue_run(t, 7, face_left); return; }   /* 66D8 */
            t->g->almas = (uint16_t)(t->g->almas - 2500);             /* 66E7 */
            t->g->page[0x8B] = (uint8_t)t->g->almas;
            t->g->page[0x8C] = (uint8_t)(t->g->almas >> 8);
            t->g->page[0x34] |= 0x40;                                 /* 66F0 */
            for (int i = 0; i < 5; i++)                               /* 66F5: item 5 into [A1..A5] */
                if (!t->g->page[0xA1 + i]) { t->g->page[0xA1 + i] = 5; break; }
            town_apply_patches(t->map, t->g->page);                   /* 6703 */
            town_dialogue_run(t, 8, face_left);                       /* 6706 */
            return;
        }
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
    t->dlg.menu_left = 0; t->dlg.hero_shown = 0;                     /* 63A9: and the widget with it */
    t->btn1_edge = t->btn2_edge = 0; t->dlg_forced = 0;
}

/* draw the box and its lines over the finished town frame (63C5 / 6478) */
/* the widget: window at [7C4C] + 193F / 1832, [FF54] = window + 0103 / 0203;
 * labels at [FF54] + 0301 (751A), the cursor at [FF54] + 0100 + row*10 (7469) */
static void dlg_render_menu(uint8_t *fb, const Town *t)
{
    int mx4 = t->dlg.menu_x4 + t->dlg.menu_ix4, my = t->dlg.menu_y + 3;
    vid_window(fb, 0xFF, t->dlg.menu_x4, t->dlg.menu_y, t->dlg.menu_w4, t->dlg.menu_h);
    for (int i = 0; i < 2; i++)
        vid_label_asciiz(fb, t->font, t->dlg.menu_item[i], mx4 + 3, my + 1 + i * 10, 0);
    gt_cursor(fb, mx4 + 1, my + t->dlg.menu_row * 10, 1);
}

static void dlg_render(uint8_t *fb, const Town *t)
{
    if (!t->dlg.active || !t->font) return;
    if (t->dlg.menu_left && !t->dlg.menu_n) dlg_render_menu(fb, t);  /* under the new box */
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
    if (t->dlg.menu_n) dlg_render_menu(fb, t);                     /* 6655 / 66AD */
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
/* In the original the map at C000 *is* the live state: the NPC records at
 * [C00F] (col, sprite, saved, anim, type, ... -- 6B1C moves them every frame)
 * and the grid at C017 (6C2B's 0xFD markers) are the same bytes 6AED pokes, so
 * a patch run mid-visit (dialogue opcode 0x83 at 6685, 0x8B 664D, 0x89 6703,
 * the shop return 6EDE) changes only the poked bytes.  The port parses the map
 * into `grid`/`npcs`, so write those back into the image before poking it and
 * re-parsing: without this the re-parse brought the file's records back
 * (#85: Llama's Elf Crest dialogue right after the MP73 exit door -- every
 * npc[].saved 0, the walkers a step back). */
static void town_sync_raw(TownMap *m)
{
    uint8_t *d = m->raw;
    size_t len = m->rawlen;
    for (int c = 0; c < m->width && 0x17 + (size_t)(c + 1) * TOWN_ROWS <= len; c++)
        memcpy(d + 0x17 + (size_t)c * TOWN_ROWS, m->grid[c], TOWN_ROWS);
    size_t o = off_in(u16(d, 0xF), len);
    for (int i = 0; i < m->nnpcs && o + 8 <= len; i++, o += 8) {
        const TownNpc *n = &m->npcs[i];
        d[o] = (uint8_t)n->col; d[o + 1] = (uint8_t)(n->col >> 8);
        d[o + 2] = n->sprite; d[o + 3] = n->saved; d[o + 4] = n->anim;
        d[o + 5] = n->type; d[o + 6] = n->flags; d[o + 7] = n->script;
    }
}

int town_apply_patches(TownMap *m, const uint8_t page[256])
{
    if (!m->raw || !m->patches) return 0;
    town_sync_raw(m);
    uint8_t *d = m->raw;
    size_t len = m->rawlen, o = off_in(m->patches, len);
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
            out->px[row][k] = (uint8_t)(FB_GT | (p[2 * k] << 3) | p[2 * k + 1]);
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
    for (int i = 0; i < 256; i++) t->type[i] = 6 + (size_t)i < len ? d[6 + i] : 0;   /* 31F2: [8000]+cell, raw */
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
            out->px[row][k] = (uint8_t)(FB_GT | ((l == 7 ? 0 : l) << 3) | (r == 7 ? 0 : r));
        }
    }
}

/* ======================================================================== */
/* ympd.bin / ckpd.bin — the backdrop painters (town.h TownBackdrop)         */
/* ======================================================================== */

/* Both overlays are assembled for origin 0x3300 (town.bin parks them at
 * (BASE+0x2000):3300), so every address below is a file offset + 0x3300. */
#define BD_ORG 0x3300

/* ckpd 0x3664: 0x1n = n zero bytes, 0x4n = n copies of 0xAA, anything else one
 * literal; 0x00 ends the stream. */
static void bd_unpack_ck(const uint8_t *d, size_t len, size_t off, uint8_t *out, size_t n)
{
    size_t o = 0;
    memset(out, 0, n);
    while (off < len) {
        uint8_t b = d[off++];
        if (!b) return;
        uint8_t hi = (uint8_t)(b & 0xF0), v; int c;
        if (hi == 0x10)      { c = b & 0x0F; v = 0x00; }
        else if (hi == 0x40) { c = b & 0x0F; v = 0xAA; }
        else                 { c = 1;        v = b;    }
        if (!c) c = 256;
        while (c-- > 0 && o < n) out[o++] = v;
    }
}
/* ympd 0x335C: a literal byte, or 0x06 followed by {value, count}; the stream
 * ends after `rows` rows of `w` bytes, not on a terminator. */
static void bd_unpack_ym(const uint8_t *d, size_t len, size_t off, uint8_t *out, int w, int rows)
{
    int col = 0, row = 0;
    size_t o = 0, n = (size_t)w * (size_t)rows;
    memset(out, 0, n);
    while (off < len && o < n) {
        uint8_t v = d[off++]; int c = 1;
        if (v == 6 && off + 1 < len) { v = d[off]; c = d[off + 1]; off += 2; }
        while (c-- > 0) {
            if (o < n) out[o++] = v;
            if (++col == w) { col = 0; if (++row == rows) return; }
        }
    }
}
/* ympd 0x358D: the ground strips use a different RLE — 0x6n = n zero bytes,
 * anything else one literal — and one call fills exactly one 28-byte row. */
static void bd_unpack_ym_strip(const uint8_t *d, size_t len, size_t off, uint8_t *out, int w, int rows)
{
    size_t o = 0, n = (size_t)w * (size_t)rows;
    memset(out, 0, n);
    for (int r = 0; r < rows; r++) {
        int bl = 0;
        while (bl < w && off < len) {
            uint8_t v = d[off++]; int c = 1;
            if ((v & 0xF0) == 0x60) { c = v & 0x0F; v = 0; }
            while (c-- > 0 && bl < w) { if (o < n) out[o++] = v; bl++; }
        }
    }
}
/* ympd 0x34F9 / ckpd 0x383A: two planes, two bits a pixel, expanded into the
 * driver's pair byte as (p2a<<5 | p1a<<3 | p2b<<2 | p1b) — colours 0,1,4,5. */
static void bd_pairs(uint8_t *dst, const uint8_t *p1, const uint8_t *p2, int w, int rows, int pitch, uint8_t tag)
{
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < w; c++) {
            uint8_t dl = p1[r * w + c], dh = p2[r * w + c];
            for (int k = 0; k < 4; k++) {
                int p2a = (dh >> (7 - 2 * k)) & 1, p1a = (dl >> (7 - 2 * k)) & 1;
                int p2b = (dh >> (6 - 2 * k)) & 1, p1b = (dl >> (6 - 2 * k)) & 1;
                dst[r * pitch + c * 4 + k] = (uint8_t)(tag | p2a << 5 | p1a << 3 | p2b << 2 | p1b);
            }
        }
}

int town_load_backdrop(TownBackdrop *b, const char *dir, int underground)
{
    memset(b, 0, sizeof *b);
    b->index = -1;
    int res = underground ? 9 : 8;                       /* ZELRES2[8] ympd / [9] ckpd */
    size_t len;
    uint8_t *d = sar_load(dir, 1, res, 1, &len);
    if (!d) return -1;
    static uint8_t p1[8192], p2[8192];
    const uint8_t tag = underground ? FB_BD_CK : FB_BD_YM;      /* video.h: the pixel keeps its painter for CGA/HGC/Tandy */
    if (underground) {
        /* ckpd 0x3321..0x3348: 224x72 at (48,30) */
        bd_unpack_ck(d, len, 0x38E0 - BD_ORG, p1, 0x0FC0);
        bd_unpack_ck(d, len, 0x426A - BD_ORG, p2, 0x0FC0);
        bd_pairs(b->back + 30 * FB_W + 48, p1, p2, 0x38, 0x48, FB_W, tag);
        b->back_y0 = 30; b->back_y1 = 102;
        /* 0x3351..0x3380: the 112x16 far strip drawn at (48,14) */
        bd_unpack_ck(d, len, 0x4C6D - BD_ORG, p1, 0x01C0);
        bd_unpack_ck(d, len, 0x4DB8 - BD_ORG, p2, 0x01C0);
        bd_pairs(&b->far14[0][0], p1, p2, 0x1C, 0x10, 112, tag);
        b->has_far = 1;
        /* 0x3548: the near strips, two raw planes at the end of the image
         * (0x4F25 / 0x50E5), 28 bytes x 16 rows, expander 0x35AB (0,2,4,6) */
        size_t o1 = 0x4F25 - BD_ORG, o2 = 0x50E5 - BD_ORG;
        if (o2 + 0x1C0 <= len)
            for (int r = 0; r < 16; r++)
                for (int c = 0; c < 0x1C; c++) {
                    uint8_t dl = d[o1 + r * 0x1C + c], dh = d[o2 + r * 0x1C + c];
                    for (int k = 0; k < 4; k++) {
                        int p2a = (dh >> (7 - 2*k)) & 1, p1a = (dl >> (7 - 2*k)) & 1;
                        int p2b = (dh >> (6 - 2*k)) & 1, p1b = (dl >> (6 - 2*k)) & 1;
                        uint8_t v = (uint8_t)(tag | p2a << 5 | p1a << 4 | p2b << 2 | p1b << 1);
                        if (r < 8) b->near142[r][c * 4 + k] = v;
                        else       b->near150[r - 8][c * 4 + k] = v;
                    }
                }
    } else {
        /* ympd 0x3321..0x3333: 224x88 at (48,14) — no separate far strip */
        bd_unpack_ym(d, len, 0x38E7 - BD_ORG, p1, 0x38, 0x58);
        bd_unpack_ym(d, len, 0x4759 - BD_ORG, p2, 0x38, 0x58);
        bd_pairs(b->back + 14 * FB_W + 48, p1, p2, 0x38, 0x58, FB_W, tag);
        b->back_y0 = 14; b->back_y1 = 102;
        /* 0x333E..0x3357: two 448-byte streams, each 8 rows of {28, 28} bytes;
         * 0x374E draws them at (48,142) and (48,150).  The halves swap their
         * planes and the second drops the last shift, so its colours are
         * 0,1,2,3 where the first half's are 0,2,4,6. */
        static uint8_t st[896];
        bd_unpack_ym_strip(d, len, 0x559E - BD_ORG, st,       0x1C, 0x10);
        bd_unpack_ym_strip(d, len, 0x56F1 - BD_ORG, st + 448, 0x1C, 0x10);
        for (int half = 0; half < 2; half++)
            for (int r = 0; r < 8; r++) {
                const uint8_t *row = st + half * 448 + r * 0x38;
                for (int i = 0; i < 14; i++) {
                    unsigned w1 = (unsigned)row[2*i] << 8 | row[2*i + 1];
                    unsigned w2 = (unsigned)row[0x1C + 2*i] << 8 | row[0x1C + 2*i + 1];
                    unsigned dx = half ? w2 : w1, bx = half ? w1 : w2;
                    for (int k = 0; k < 8; k++) {
                        int da = (dx >> (15 - 2*k)) & 1, ba = (bx >> (15 - 2*k)) & 1;
                        int db = (dx >> (14 - 2*k)) & 1, bb = (bx >> (14 - 2*k)) & 1;
                        unsigned al = 16u*da + 8u*ba + 2u*db + bb;
                        if (!half) al = (al * 2) & 0xFF;
                        if (half) b->near150[r][i * 8 + k] = (uint8_t)(tag | al);
                        else      b->near142[r][i * 8 + k] = (uint8_t)(tag | al);
                    }
                }
            }
    }
    free(d);
    b->index = res;
    b->loaded = 1;
    return 0;
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
/* 6C2B.  Every caller in town.bin has the previous markers lifted first (6C4E);
 * the port re-places from several setup paths (town_place, the page pull after
 * a restore, the shop return), so lift any markers still down here -- marking
 * over a marker saved 0xFD as the "tile underneath" (seen by the harness as
 * npc[i].saved = FD against the original's real cells). */
static void npc_place_markers(Town *t)
{
    for (int i = 0; i < t->map->nnpcs; i++) {           /* lift only markers that are down */
        TownNpc *n = &t->map->npcs[i];
        if (grid_cell(t, n->col, TOWN_NPC_ROW) == TOWN_MARK && n->saved != TOWN_MARK)
            grid_set(t, n->col, TOWN_NPC_ROW, n->saved);
    }
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
    n->anim = (uint8_t)((n->anim + 1) & 1);                             /* 6BE1..6BE8: everything else drops */
}
static void npc_step(Town *t, TownNpc *n, int every4)                   /* 6B6C / 6BA6 */
{
    n->anim = (uint8_t)(n->anim + 0x10);
    if (n->anim & (every4 ? 0x30 : 0x10)) return;
    /* 6B7B: ((anim+1) & 0x0F) | (anim & 0x10) -- and that last term is 0 on
     * this path, so the step timer bits are dropped, not kept */
    n->anim = (uint8_t)((n->anim + 1) & 0x0F);
    if (n->sprite & 0x80) { n->col--; if (t->map->range_min >= n->col) n->sprite &= 0x7F; }
    else                  { n->col++; if (t->map->range_max < n->col) n->sprite |= 0x80; }   /* 6B9B: cmp [C013],dx; jc */
}
static void npc_wander(Town *t, TownNpc *n, int every4)                 /* 6BEC / 6C19 */
{
    (void)t;
    n->anim = (uint8_t)(n->anim + 0x10);
    if (n->anim & (every4 ? 0x30 : 0x10)) return;
    n->anim = (uint8_t)((n->anim + 1) & 0x0F);                         /* 6BFB..6C02, as 6B7B */
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
    t->scroll_col--; t->back_steps--;                                   /* GT_SCROLL_*_LEFT */
}
static void walk_right(Town *t)
{
    if (!town_cell_walkable(t, grid_cell(t, t->scroll_col + t->hero_scr_col + 6, TOWN_GROUND))) return;
    if (npc_solid_at(t, t->scroll_col + t->hero_scr_col + 5)) return;
    t->hero_anim = (uint8_t)((t->hero_anim + 1) & 3);
    t->hero_flags &= (uint8_t)~1;
    if (t->hero_scr_col < 0x10) { t->hero_scr_col++; return; }          /* 6832 */
    if (t->scroll_col + 1 == t->map->width - 0x23) { t->hero_scr_col++; return; }   /* 683E: up to 0x1B, where 6CB5 takes the right exit */
    t->scroll_col++; t->back_steps++;                                   /* GT_SCROLL_*_RIGHT */
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
        uint8_t sv_sprite = n->sprite, sv_type = n->type;              /* 6290..6296 */
        n->type = 7;                                                    /* 6297 / 62D6: frozen */
        if (step > 0) n->sprite |= 0x80; else n->sprite &= 0x7F;        /* face the hero */
        n->anim |= 1;
        town_draw_frame(t);                                             /* 6363..6368: FF1A=28, 68AF */
        if (!t->quit) town_dialogue_run(t, n->script, (t->hero_flags & 1) != 0);   /* 638F */
        town_force_redraw(t);                                           /* 63AB */
        n->type = sv_type; n->sprite = sv_sprite;                       /* 62A6..62AA */
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
    t->dlg_forced = 0xFF;                                               /* 62ED: uncancellable */
    town_draw_frame(t);                                                 /* 6363..6368: FF1A=28, 68AF */
    if (t->quit) return;
    t->sfx_request = 0x1E;                                              /* 636B */
    town_dialogue_run(t, n->script, (t->hero_flags & 1) != 0);
    town_force_redraw(t);                                               /* 63AB */
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
        npc_restore_tiles(t);                                           /* 6E61: 6C4E */
        town_draw_frame(t);                                             /* 6E64/6E69: FF1A=28, 68AF */
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
    else if (t->hero_scr_col + 1 == 0x1C) left = 0;                    /* 6CB5 inc al / 6CF5 cmp al,0x1c: [83] == 0x1B */
    else return;
    npc_restore_tiles(t);                                               /* 6CBC/6CFA: 6C4E */
    town_draw_frame(t);                                                 /* 6CBF/6CFD: FF1A=28, 68AF */
    const TownExit *e = NULL;
    for (int i = 0; i < t->map->nexits; i++) {
        int is_left = t->map->exits[i].flags & 1;
        if (is_left == left) { e = &t->map->exits[i]; break; }
    }
    if (!e) { t->hero_scr_col = left ? 0 : 0x1A; npc_place_markers(t); return; }   /* port guard: no record (the original scans on past the list) */
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

/* town.bin keeps the hero's town position inside the same BASE:0000 page the
 * save file is a raw image of: [80] scroll_col (6157), [83] hero_scr_col
 * (6CB5), [C2] hero_flags (67BA/682D) and [C4] cur_map (6D30).  The port keeps
 * them in `Town`, so they have to be written back before kenjpro A862 dumps the
 * page and read again after town.bin 7592 loads one. */
/* town.bin 614E: VID_LABEL_TEXT(si = [C004]). */
const uint8_t *town_place_record(const TownMap *m)
{
    if (!m || !m->raw || m->label_ptr < 0xC000) return NULL;
    size_t o = (size_t)(m->label_ptr - 0xC000);
    if (o + 4 >= m->rawlen || o + 4 + m->raw[o + 3] > m->rawlen) return NULL;
    return m->raw + o;
}

void town_page_push(Town *t)
{
    uint8_t *p = t->g->page;
    p[0x80] = (uint8_t)t->scroll_col;
    p[0x81] = (uint8_t)(t->scroll_col >> 8);
    p[0x83] = (uint8_t)t->hero_scr_col;
    p[0xC2] = t->hero_flags;
    p[0xE7] = t->hero_anim;
    t->g->cur_map = (uint8_t)(0x80 | t->map->index);
    p[0xC4] = t->g->cur_map;
    p[0xC5] = t->g->town_map;
}

void town_page_pull(Town *t)
{
    const uint8_t *p = t->g->page;
    int sc = p[0x80] | p[0x81] << 8;
    int max = t->map->width - 0x24;
    if (sc < 0) sc = 0;
    if (sc > max) sc = max;
    t->scroll_col = sc;
    t->hero_scr_col = p[0x83] > 0x1B ? 0x1B : p[0x83];
    t->hero_flags = p[0xC2];            /* the whole byte comes back with the page (7592) */
    t->hero_anim = 0;                   /* 6050: the 601E boot entry zeroes [E7] after the restore */
    npc_place_markers(t);
}

void town_init(Town *t, TownMap *m, TownTiles *ti, TownSprites *s, TownHero *h, Game *g)
{
    /* the held keys live in the kernel's FF page (FF16/FF17/FF18), which no
     * town.bin entry touches; only the press edges are cleared (60C3/60C6) */
    uint8_t dirs = t->dirs, buttons = t->buttons, menu_key = t->menu_key;
    memset(t, 0, sizeof *t);
    t->dirs = dirs; t->buttons = buttons; t->menu_key = menu_key;
    t->map = m; t->tiles = ti; t->spr = s; t->hero = h; t->g = g;
    t->hero_scr_col = 0x0D;
    t->hero_anim = 1;
    town_force_redraw(t);                                               /* 61A5 (every entry, 60B7 included) */
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
    if (t->quit) return;
    town_force_redraw(t);                                               /* 6920 */
    town_draw_frame(t);                                                 /* 692A: call 68AF, a tail frame */
    t->menu_debounce = 0xFF;
    t->btn1_edge = t->btn2_edge = 0;                                    /* 692D/6932 */
}

/* 0x68AF  draw and pace one frame, ending at the 68ED boundary.  68AC is the
 * same with the NPC update (6B1C) first.  The callers of 68AF preset
 * FF1A = 0x28 so it does not wait: a "tail" frame. */
void town_force_redraw(Town *t) { memset(t->scr_copy, 0xFE, sizeof t->scr_copy); }

/* gtmcga GT_FLUSH 3051, map rows 0-2 (3084..3099 -> 31D8): a cell is drawn
 * only where the map byte differs from the screen copy at E000 (cmpsb).  31D8
 * stores FE into the copy and returns at once if it held FF (protected); else
 * the copy takes the map byte.  A cell whose type byte is 0 (opaque) goes to
 * the plain 3123 draw and stops there.  Any other is drawn over the backdrop
 * strip (3203) and then, if it is 1..0x18 (32BD..32CA), looked up in the tile
 * bank's off_anim table [8004] ({n, {from, to}[n]}, a from of FF ends it,
 * 32D7..32F7): the *map* byte becomes `to` (32EE).  The screen keeps `from`
 * until the next flush finds map != copy and draws `to` -- so a visible
 * animated cell steps once per frame, and one that scrolls onto a copy byte
 * equal to it is not drawn and does not step.  Only the pixels shown come from
 * the copy (town_render); the rows-3..7 copy is not modelled. */
static void town_flush_rows(Town *t)
{
    const TownTiles *ti = t->tiles;
    for (int c = 0; c < TOWN_SCR_COLS; c++) {
        int mc = t->scroll_col + 4 + c;                                 /* 3070..3074: FF2A + 0x20 */
        if (mc < 0 || mc >= t->map->width) continue;
        for (int r = 0; r < 3; r++) {
            uint8_t v = t->map->grid[mc][r], cp = t->scr_copy[c][r];
            /* a cell the dialogue box covers is not flushed (the box lives in video memory; the
             * original keeps GT_FLUSH off it): the animated cell neither draws nor steps until
             * the box is gone, and the copy is forced so its first flush after redraws it
             * (jashiin_kill f778: the castle's pennants resume one step on from where they froze) */
            if (t->dlg.active && t->dlg.w4) {
                int x0 = 48 + c * 8, y0 = 78 + r * 8;
                int bx0 = t->dlg.x8 * 8, bx1 = bx0 + t->dlg.w4 * 4, by0 = t->dlg.y, by1 = by0 + t->dlg.h;
                if (x0 < bx1 && x0 + 8 > bx0 && y0 < by1 && y0 + 8 > by0) { t->scr_copy[c][r] = 0xFE; continue; }
            }
            if (cp == v) continue;                                      /* 3086 cmpsb */
            t->scr_copy[c][r] = 0xFE;                                   /* 31DB */
            if (cp == 0xFF) continue;                                   /* 31DF..31E3 */
            t->scr_copy[c][r] = v;                                      /* 3205 movsb */
            if (!ti || !ti->type[v]) continue;                          /* 31FC -> 3123 */
            if (v == 0 || v >= 0x19) continue;                          /* 32C0..32CA */
            for (int i = 0; i < ti->nanim; i++) {
                if (ti->anim_from[i] == 0xFF) break;                    /* 32E2 */
                if (ti->anim_from[i] == v) { t->map->grid[mc][r] = ti->anim_to[i]; break; }
            }
        }
    }
}

static void town_draw_frame(Town *t)
{
    t->frame_no++;
    town_flush_rows(t);                                                 /* 68B5: GT_FLUSH */
    if (t->present) t->present(t);
    town_sound(t);
    if (t->frame_end) t->frame_end(t);                                  /* 68ED: the frame boundary */
}
void town_tail_frame(Town *t) { town_draw_frame(t); }

/* 0x61A5..0x61E2  the town entry: E000 forced to FE, then one frame of 68AC
 * (61B1) *before* the 61FC main loop -- whose own first 68AC follows at once,
 * so the keys are first read after the second boundary.  [7C4B] = 0 (61E2).
 * The underground walk-in (61B4..61DD, [7C44]) is not modelled. */
void town_entry_frame(Town *t)
{
    town_npc_update(t);                                                 /* 68AC -> 6B1C */
    town_draw_frame(t);
    t->was_idle = 0;                                                    /* 61E2 */
}

/* 0x61B4..0x61DD  the walk-in into an underground town entered from a
 * cavern ([7C44], set at 6092 when the level record's town_flags bit 0 is
 * set, the entry was 6026 -- fight.bin's -- and [E8] is clear): four times
 * the walk step in the hero's facing (6781 left / 67F4 right, picked by [C2]
 * bit 0 at 61C1) and a 68AC frame, then one more step with no frame.  The
 * port had none of it (key_door f66: Satono's hero_scr_col 1, [E7] 1). */
void town_walk_in(Town *t)
{
    void (*step)(Town *) = (t->hero_flags & 1) ? walk_left : walk_right;   /* 61BB..61C8 */
    for (int i = 0; i < 4; i++) { step(t); town_entry_frame(t); }       /* 61CE..61DB */
    step(t);                                                            /* 61DD */
}

void town_step(Town *t)
{
    town_npc_update(t);                                                 /* 68AC */
    town_draw_frame(t);
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
 * rows 0-2 show the ympd/ckpd backdrop through the cells' own sky mask — the
 * strip GT_CAPTURE_BACKDROP (gtmcga 3028) grabs from y 78..101 before the map
 * is drawn over it.  The HUD is fight.bin's. */
#define TOWN_Y   78
#define SKY_IDX  0x2C

/* smooth scrolling (#127): NULL on the engine's own path, so verify, traces and scenarios render as before */
static const SmoothOfs *s_sm;
void town_set_smooth(const SmoothOfs *ofs) { s_sm = ofs && ofs->active ? ofs : NULL; }

static void blit_cell8(uint8_t *fb, const Cell8 *c, const uint8_t sky[8][8],
                       const TownBackdrop *bd, int x, int y)
{
    for (int r = 0; r < 8; r++) {
        int yy = y + r;
        if (yy < 14 || yy >= 158) continue;
        for (int k = 0; k < 8; k++) {
            int xx = x + k;
            if (xx < 48 || xx >= 48 + 224) continue;
            uint8_t v;
            if (sky && sky[r][k])
                v = (bd && bd->loaded) ? bd->back[yy * FB_W + xx] : SKY_IDX;
            else v = c->px[r][k];
            fb[yy * FB_W + xx] = v;
        }
    }
}
/* GT_SCROLL_FAR_* / GT_SCROLL_NEAR_* (gtmcga 3677/3628): a walking step that
 * scrolls the map slides the y14 strip 4 px, the y142 strip 8 and the y150
 * strip 16, all three 112 px wide and repeated once.  The vectors rotate the
 * pixels where they lie, so the phase is the number of scrolled columns since
 * the backdrop painter last drew the strips (`back_steps`), not `scroll_col`.
 *
 * The strips slide the *same way the map does* -- left when the hero walks
 * right -- so the phase is **-back_steps** x {4,8,16} modulo 112.  Measured
 * against eleven DOSBox frames of one continuous walk from Felishika's Castle
 * into Muralla (docs/screenshots/town_castle.png,
 * town_edge_walkin.png, town_edge_walked.png; port/README.md "The town
 * parallax phase"): in each town the strip phase index fits
 * `(entry_scroll_col - scroll_col) mod 14` exactly -- cmap 30 -> 0, 36 -> 8,
 * 49 -> 9, 61 -> 11, 74 -> 12, 78 -> 8; mrmp 0 -> 8, 15 -> 7, 22 -> 0,
 * 40 -> 10, 179 -> 11.  `docs/screenshots/town.png` alone cannot see the sign:
 * 179 and -(48+179) are both 11 mod 14.  ckpd's far strip has the same
 * sign by construction; every underground capture is an entry frame, so
 * its phase is 0 and nothing measures it. */
static void blit_strip(uint8_t *fb, const uint8_t strip[][112], int rows, int y, int shift)
{
    shift = ((shift % 112) + 112) % 112;
    for (int r = 0; r < rows; r++)
        for (int x = 0; x < 224; x++) {
            int s = (x - shift) % 112;
            if (s < 0) s += 112;
            fb[(y + r) * FB_W + 48 + x] = strip[r][s];
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

void town_dissolve(Town *t) { town_dissolve_from(t, 1); }

void town_dissolve_from(Town *t, int first)
{
    t->dsv_pic = 1;
    for (int n = 1; n <= 8 && !t->quit; n++) {
        t->dissolve = n < first ? first : n;
        if (t->present) t->present(t);
    }
    t->dissolve = 8;
    t->dsv_pic = 0;
}

int town_npc_screen_col(const Town *t, int i, int *sc)
{
    const TownNpc *n = &t->map->npcs[i];
    int c = (int)n->col - (t->scroll_col + 4);
    if (c < -1 || c >= TOWN_SCR_COLS || grid_cell(t, n->col, TOWN_NPC_ROW) != TOWN_MARK) return 0;
    *sc = c;
    return 1;
}

void town_render(uint8_t *fb, const Town *t)
{
    /* mole.bin's furniture is painted once and never redrawn (gfx.h); keep it
     * in the persistent fb so a draw on it (the status screen's stray magic
     * name, #103) survives.  Clear only what the furniture does not cover and
     * paint it only where the fb is still blank. */
    const ScreenFrame *sf = t->g ? t->g->screen : NULL;
    for (int i = 0; i < FB_W * FB_H; i++) {
        if (!sf || !sf->loaded || !sf->on[i]) fb[i] = 0;
        else if (!fb[i]) fb[i] = sf->px[i];
    }
    const TownBackdrop *bd = (t->back && t->back->loaded) ? t->back : NULL;
    if (bd) {
        for (int y = bd->back_y0; y < bd->back_y1 && y < TOWN_Y; y++)
            memcpy(fb + y * FB_W + 48, bd->back + y * FB_W + 48, 224);
        int f0 = s_sm ? s_sm->fx[0] : 0, f1 = s_sm ? s_sm->fx[1] : 0, f2 = s_sm ? s_sm->fx[2] : 0;
        if (bd->has_far) blit_strip(fb, bd->far14, 16, 14, -t->back_steps * 4 + f0);
        else for (int y = 14; y < bd->back_y0; y++) memset(fb + y * FB_W + 48, 0, 224);
        blit_strip(fb, bd->near142, 8, 142, -t->back_steps * 8 + f1);
        blit_strip(fb, bd->near150, 8, 150, -t->back_steps * 16 + f2);
    } else {
        for (int y = 14; y < TOWN_Y; y++) memset(fb + y * FB_W + 48, SKY_IDX, 224);
    }
    /* a slide draws the tiles at a pixel offset and so also needs the map column just beyond
     * the window on the side being revealed (clipped to the window by blit_cell8) */
    int wx = s_sm ? s_sm->wx : 0;
    int c0 = wx > 0 ? -1 : 0, c1 = TOWN_SCR_COLS + (wx < 0 ? 1 : 0);
    for (int c = c0; c < c1; c++) {
        int mc = t->scroll_col + 4 + c;                                 /* gtmcga 3074 skips 4 columns */
        if (mc < 0 || mc >= t->map->width) continue;
        for (int r = 0; r < TOWN_ROWS; r++) {
            uint8_t v = t->map->grid[mc][r];
            /* rows 0-2: what the last flush drew, which an animated cell's
             * map byte is already one step past (town_flush_rows) */
            if (r < 3 && c >= 0 && c < TOWN_SCR_COLS && t->scr_copy[c][r] < 0xFE) v = t->scr_copy[c][r];
            if (v == TOWN_MARK) {                                       /* an NPC marker: draw the tile below it */
                for (int i = 0; i < t->map->nnpcs; i++)
                    if (t->map->npcs[i].col == (uint16_t)mc) { v = t->map->npcs[i].saved; break; }
                if (v == TOWN_MARK) v = 0;
            }
            /* GT_FLUSH draws *every* cell, cell 0 included: in rows 0-2 its
             * mask is all sky, which is how the backdrop shows through. */
            if (!t->tiles->present[v]) continue;
            blit_cell8(fb, &t->tiles->cell[v], r < 3 ? t->tiles->sky[v] : NULL, bd, 48 + c * 8 + wx, TOWN_Y + r * 8);
        }
    }
    /* NPCs: 2 columns x 3 rows on rows 5-7 (34EC: frame = (anim & 3) + 4*right).
     * The original draws an NPC only through its 0xFD marker on map row 5:
     * GT_FLUSH's row-5 cell (311A -> 3350) and the hero-column compose (6A77
     * tests the 7C7A copy of row 5 for 0xFD).  A door / edge exit lifts the
     * markers (6E61 / 6CBC / 6CFA: 6C4E) and flushes a 68AF tail frame with
     * no NPC update to put them back, so on that frame every NPC cell is
     * redrawn as the plain tile and the NPCs vanish (#93). */
    for (int i = 0; i < t->map->nnpcs; i++) {
        const TownNpc *n = &t->map->npcs[i];
        int sc = (int)n->col - (t->scroll_col + 4);
        if (sc < -1 || sc >= TOWN_SCR_COLS + (s_sm ? 1 : 0)) continue;
        if (grid_cell(t, n->col, TOWN_NPC_ROW) != TOWN_MARK) continue;
        int nofs = s_sm && i < SMOOTH_NPCS ? s_sm->nx[i] : 0;
        int sp = n->sprite & 0x7F;
        if (sp > 4) sp = 4;
        int f = (n->anim & 3) + ((n->sprite & 0x80) ? 0 : 4);
        const uint8_t *cells = t->spr->frame[sp][f & 7];
        for (int k = 0; k < 6; k++) {
            int cell = cells[k];
            if (!cell || cell > t->spr->ncells) continue;
            int cx = sc + k / 3, cy = TOWN_NPC_ROW + k % 3;
            if (s_sm ? (cx < -1 || cx > TOWN_SCR_COLS) : (cx < 0 || cx >= TOWN_SCR_COLS)) continue;
            blit_sprite(fb, &t->spr->cell[cell - 1], 48 + cx * 8 + nofs, TOWN_Y + cy * 8);
        }
    }
    /* the hero: the same 2x3 layout, tman cells (0-based) */
    if (t->hero_scr_col >= 0 && t->hero_scr_col < 0x1B) {
        int anim = t->dlg.active && t->dlg.hero_shown ? t->dlg.hero_shown - 1 : t->hero_anim;
        if (anim > 4) anim = 4;
        const uint8_t *cells = (t->hero_flags & 1) ? HERO_L[anim] : HERO_R[anim];
        for (int k = 0; k < 6; k++) {
            int cell = cells[k];
            if (cell >= t->hero->ncells) continue;
            int cx = t->hero_scr_col + k / 3, cy = TOWN_NPC_ROW + k % 3;
            if (s_sm ? (cx < -1 || cx > TOWN_SCR_COLS) : (cx < 0 || cx >= TOWN_SCR_COLS)) continue;
            blit_sprite(fb, &t->hero->cell[cell], 48 + cx * 8 + (s_sm ? s_sm->hx : 0), TOWN_Y + cy * 8);
        }
    }
    dlg_render(fb, t);                                              /* 63C5 */
    if (t->dissolve) render_dissolve(fb, t->dissolve);              /* 6E9B: the screen being dissolved (#118) */
}
