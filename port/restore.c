/* restore.c -- F7 "Restore Game" (restore.h; kernel 092D, town.bin 7592/7695/7838). */
#include "restore.h"
#include "text.h"
#include "player.h"
#include "sar.h"
#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>

#define BLANK 0x60              /* '`' is the blank glyph of the name box */

/* town.bin 77BA / 77AE / 7667 and kernel 0983, strings as the original has them */
static const uint8_t S_CONFIRM[]   = "Restore Game\rSure?(Y/N)\xFF";
static const uint8_t S_PROMPT[]    = "Input name:\xFF";
static const uint8_t S_NOTFOUND[]  = "User File\rNot Found\xFF";
static const char    S_RESTART[]   = "Re-Start";

static int cmp_name(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

/* STICK svc_find_files [11C] on "*.usr": the base names (up to the '.', at
 * most 8), upper case.  DOS returns directory order; the port sorts.  76AF..76C1
 * then inserts "Re-Start" as entry 0. */
static void scan_files(Restore *r, const char *dir)
{
    int n = 0;
    DIR *d = opendir(dir && *dir ? dir : ".");
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) && n < RESTORE_MAX) {
            const char *dot = strrchr(e->d_name, '.');
            if (!dot || strlen(dot) != 4) continue;
            if ((dot[1] | 0x20) != 'u' || (dot[2] | 0x20) != 's' || (dot[3] | 0x20) != 'r') continue;
            size_t len = (size_t)(dot - e->d_name);
            if (len == 0 || len > 8) continue;
            char *o = r->files[1 + n++];
            for (size_t i = 0; i < len; i++) o[i] = (char)toupper((unsigned char)e->d_name[i]);
            o[len] = 0;
        }
        closedir(d);
    }
    qsort(r->files[1], (size_t)n, sizeof r->files[0], cmp_name);
    snprintf(r->files[0], sizeof r->files[0], "%s", S_RESTART);
    r->nfiles = n + 1;
}

/* name_entry_loop 7838 bookkeeping */
static void check_restart(Restore *r)       /* 77C3 */
{
    if (memchr(r->name, '-', 8)) { r->restart = 1; r->cur = 0; }
}
static void set_name(Restore *r, const char *src)   /* 76DC..7708 / 78DA..7903 */
{
    memset(r->name, BLANK, 8); r->name[8] = 0xFF;
    int n = 0;
    for (; n < 8 && src[n]; n++) r->name[n] = (uint8_t)src[n];
    r->used = n; r->cur = n > 7 ? 7 : n;
    r->restart = 0;
    check_restart(r);
}
static int name_blank(const Restore *r)
{
    for (int i = 0; i < 8; i++) if (r->name[i] != BLANK) return 0;
    return 1;
}
static void move_caret(Restore *r, int d)   /* 7AAD */
{
    int c = r->cur + d;
    if (c < 0) c = 0;
    if (c >= 8) c = 7;
    if (c > r->used) c = r->used;
    r->cur = c;
}
static void backspace(Restore *r)           /* 7B44 */
{
    int b = r->cur ? r->cur : 1;
    memmove(r->name + b - 1, r->name + b, (size_t)(8 - b));
    if (r->used) r->used--;
    r->name[7] = BLANK;
    move_caret(r, -1);
}

void restore_open(Restore *r)
{
    memset(r, 0, sizeof *r);
    r->state = RS_CONFIRM;
    audio_sfx_request(2);                   /* dialog_open: [FF75] = 2 */
    audio_music_pause(1);                   /* 06 int60(3, FF) */
}

static void start_pick(Restore *r, Shell *sh)   /* 7695 */
{
    scan_files(r, sh->dir);
    r->sel = r->top = 0;
    set_name(r, sh->g.player_name);
    if (name_blank(r)) set_name(r, S_RESTART);  /* no name yet -> "Re-Start" */
    r->state = RS_PICK;
}

/* 75F8: does the file the name stands for load?  Done before touching the
 * player page, so a refusal leaves the running game alone.  A save has to name
 * a town in [C4] (the Sage only writes them in towns): GAME.BIN's restart would
 * otherwise have no map to enter (doubt: the original loads it regardless). */
static int loadable(const Shell *sh, const char *name)
{
    char path[300];
    snprintf(path, sizeof path, "%s.usr", name);
    FILE *f = game_fopen(sh->dir, path);     /* DOS is case-blind */
    if (!f) return 0;
    uint8_t b[256];
    size_t n = fread(b, 1, sizeof b, f);
    fclose(f);
    return n == 256 && (b[0xC4] & 0x80);
}

static void accept_name(Restore *r, Shell *sh)  /* 778A..779E, then 75AF.. */
{
    memset(sh->g.player_name, 0, 8);
    char nm[9]; int n = 0;
    for (int i = 0; i < 8; i++) {
        if (r->name[i] == 0xFF || r->name[i] == BLANK) break;
        nm[n++] = (char)r->name[i];
    }
    nm[n] = 0;
    if (r->restart) { r->pick[0] = 0; r->apply = 1; r->state = RS_CLOSED; return; }
    memcpy(sh->g.player_name, nm, (size_t)n);
    snprintf(r->pick, sizeof r->pick, "%s", nm);
    if (loadable(sh, nm)) { r->apply = 1; r->state = RS_CLOSED; }
    else r->state = RS_NOTFOUND;            /* 762F: "User File / Not Found" */
}

void restore_key(Restore *r, Shell *sh, int key, char ch)
{
    switch (r->state) {
    case RS_CONFIRM:
        if (key == RK_CHAR) key = ch == 'Y' ? RK_YES : ch == 'N' ? RK_NO : RK_NONE;   /* FF18 0x20 / 0x40 */
        if (key == RK_YES) start_pick(r, sh);
        else if (key == RK_NO || key == RK_CANCEL) {        /* dialog_close; music resumes */
            r->state = RS_CLOSED; audio_music_pause(0);
        }
        return;
    case RS_NOTFOUND:                       /* 764B: a Space edge, then the picker again */
        if (key == RK_TAKE || key == RK_ACCEPT) r->state = RS_PICK;
        return;
    case RS_PICK: break;
    default: return;
    }
    switch (key) {
    case RK_CANCEL: r->state = RS_CLOSED; audio_music_pause(0); return;
    case RK_UP:   if (r->sel > 0) r->sel--; if (r->sel < r->top) r->top = r->sel; return;
    case RK_DOWN:
        if (r->sel + 1 < r->nfiles) r->sel++;
        if (r->sel >= r->top + 5) r->top = r->sel - 4;
        return;
    case RK_LEFT:  move_caret(r, -1); return;
    case RK_RIGHT: move_caret(r, 1);  return;
    case RK_TAKE:  set_name(r, r->files[r->sel]); return;               /* 78BF */
    case RK_BACKSPACE: backspace(r); return;                            /* 794E */
    case RK_ACCEPT:                                                     /* 7874 */
        if (name_blank(r)) { memcpy(r->name, S_RESTART, 8); r->restart = 1; r->cur = 0; return; }
        accept_name(r, sh);
        return;
    case RK_CHAR:                                                       /* 7935 */
        if (!ch) return;
        if (r->restart) {                                               /* 77E5 clear_restart_name */
            r->restart = 0; memset(r->name, BLANK, 8); r->used = 0;
        }
        if (r->name[r->cur] == BLANK) r->used++;
        r->name[r->cur] = (uint8_t)ch;
        move_caret(r, 1);
        return;
    default: return;
    }
}

void restore_draw(const Restore *r, const Shell *sh, uint8_t *fb)
{
    const TextFont *f = &sh->tfont;
    if (r->state == RS_CONFIRM) {                       /* kernel dialog_open + 0983 */
        vid_window(fb, 0xFF, 0x1A, 0x46, 0x1E, 0x28);
        vid_puts(fb, f, S_CONFIRM, 0x74, 0x52);
        return;
    }
    if (r->state != RS_PICK && r->state != RS_NOTFOUND) return;
    /* 76C2..76D7: two windows, the narrower one leaves the divider */
    vid_window(fb, 0xFF, 0x0D, 0x38, 0x36, 0x37);
    vid_window(fb, 0xFF, 0x0D, 0x38, 0x26, 0x37);
    vid_puts(fb, f, S_PROMPT, 0x3C, 0x44);
    vid_window(fb, 0, 0x60 / 4, 0x56, 0x10, 8);         /* 7B1A draw_name_box */
    vid_puts(fb, f, r->name, 0x60, 0x56);
    vid_putchar(fb, f, 0x7F, 6, 0x60 + r->cur * 8, 0x56 + 8);   /* 7AAD caret */
    for (int i = 0; i < 5 && r->top + i < r->nfiles; i++) {     /* 7750..7761 */
        vid_window(fb, 0, 0x37, 0x3B + i * 10, 0x0A, 9);
        vid_label_asciiz(fb, f, r->files[r->top + i], 0x37, 0x3B + i * 10 + 1, 0);
    }
    gt_cursor(fb, 0x35, 0x3B + (r->sel - r->top) * 10, 1);
    if (r->state == RS_NOTFOUND) {                      /* 762F */
        vid_window(fb, 0xFF, 0x1A, 0x46, 0x1E, 0x1A);
        vid_puts(fb, f, S_NOTFOUND, 0x80, 0x4C);
    }
}

int restore_commit(Restore *r, Shell *sh)
{
    Game *g = &sh->g;
    r->apply = 0;
    if (!r->pick[0] || r->restart) {                    /* 75B7: a new game is a fresh STDPLY.BIN */
        if (player_load_stdply(g, sh->dir)) { fprintf(stderr, "[restore] cannot load STDPLY.BIN\n"); audio_music_pause(0); return -1; }
        memset(g->player_name, 0, sizeof g->player_name);
        fprintf(stderr, "[restore] Re-Start: a new game\n");
    } else {
        if (player_load_usr(g, sh->dir, r->pick)) { fprintf(stderr, "[restore] cannot read %s.usr\n", r->pick); audio_music_pause(0); return -1; }
        memset(g->player_name, 0, sizeof g->player_name);
        for (int i = 0; i < 8 && r->pick[i]; i++) g->player_name[i] = r->pick[i];
        fprintf(stderr, "[restore] %s.usr restored: LIFE %u/%u, level %u, EXP %u, GOLD %u\n",
                r->pick, g->hp, g->max_hp, g->level, g->exp, (unsigned)g->gold);
    }
    g->hero_dead = g->page[0xE8];
    audio_music_stop();                                 /* 761C int60(1) */
    audio_music_pause(0);                               /* 7621 int60(3, 0) */
    /* 7628: GAME.BIN restarts, A1CB loads the map [C4] names and enters town.bin
     * at 601E with the page's own [80]/[83] */
    int town = (g->page[0xC4] & 0x80) ? (g->page[0xC4] & 0x7F) : 0;
    if (!shell_enter_town(g, town, -1, 0)) return -1;
    town_page_pull(&sh->town);
    fprintf(stderr, "[restore] town %d, hero at column %d (scroll %d)\n",
            town, town_hero_col(&sh->town), sh->town.scroll_col);
    return 0;
}
