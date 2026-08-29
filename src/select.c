/*
 * select.c — hand-cleaned decompilation of SELECT.BIN (ZELRES2[1], 3613 bytes,
 * image A000..AE1C): the **status / inventory screen** and the **potion
 * effects**.  Companion: docs/TOWN.md §12; the callers are src/town.c
 * (check_status_menu @68F3) and src/fight.c (check_item_menu @7202).
 *
 * NOT COMPILABLE — pseudo-C from disasm/overlays/select.asm (origin A000; the
 * Ghidra dump loses all six jump tables).  Every routine carries its original
 * address; constants cite the instruction they come from.
 *
 * Loading and calling convention
 * ------------------------------
 * select.bin is a slot-B overlay but it is *not* loaded on demand: GAME.BIN
 * parks it at **arena:C000** at boot and both engines swap it into BASE:A000
 * for the duration of the screen (0x800 words, town 6938 / fight 72D9), so the
 * shop slot / the boss AI in A000 survives.  Two vectors, differing only in
 * the value of `in_town`:
 *
 *   [A000] = A004   entry from **fight.bin** (`call [A000]` @728C), in_town = 0
 *   [A002] = A00B   entry from **town.bin**  (`call [A002]` @6909), in_town = 0xFF
 *
 * Both `ret` to the caller.  The caller has already cleared the playfield
 * ([2002]) and set `sfx_request = 0x0B`; on return it repaints the world.
 * The only value passed back is `menu_result [FF4B]`, set to the *potion slot
 * value* (drug id + 1) whenever a potion is used: fight.bin tests it for **8**
 * = Kioku Feather and warps to town (72D9/729C).
 *
 * Everything is drawn through the **video driver** vectors at BASE:2000
 * (docs/VIDEO_DRIVERS.md) — never through gtmcga/gfmcga — because the same
 * image has to run under both engines.  select.bin is the only caller of
 * [202E] cursor frame, [2030]/[2032] digits, [2034]/[2036]/[203A]/[203C]
 * icon sections, and it is the reason those slots exist.
 *
 * Player-record fields (BASE:0000 page, docs/STATE_PAGE.md):
 *   [90]/[B2] hp / max_hp      [92] sword    [93] shield   [94]/[96] shield hp / max
 *   [98] keys  [99] lion keys  [9A] Elf Crest  [9B] Crest of Glory  [9C] Hero's Crest
 *   [9D] selected magic 1..7   [9E] worn item (0 = none, 1..5 = item id)
 *   [A1..A5] key items (ids, packed)   [A6..AA] potion slots (drug id + 1, packed)
 *   [AB..B1]/[B4..BA] magic charges / maxima     [BB..C1] spells learned
 *   [8D] level  [8E] exp  [E4] sword power bonus
 */

typedef unsigned char u8;
typedef unsigned short u16;

/* ---- video driver, BASE:2000 (docs/VIDEO_DRIVERS.md §1.2) --------------- */
#define VID_WINDOW        (*(void(*)())0x2000)  /* AL style, BH x4, BL y, CH w4, CL h  */
#define VID_LIFE_BAR_CUR  (*(void(*)())0x2008)  /* reads [90] */
#define VID_NUM_MAGIC_CNT (*(void(*)())0x2018)  /* 3 digits at (220,187) = [AB + [9D]-1] */
#define VID_NUM_SHIELD_HP (*(void(*)())0x201A)  /* 3 digits at (248,187) = [94] if [93] */
#define VID_ICON_SWORD    (*(void(*)())0x201C)  /* AL 1-based, BH x8, BL y  (40x18 -> 20 px) */
#define VID_ICON_MAGIC    (*(void(*)())0x201E)  /* AL 1-based, BH x4, BL y  itemp section 3 */
#define VID_ICON_SHIELD   (*(void(*)())0x2020)  /* AL 1-based, BH x4, BL y  itemp section 1 */
#define VID_PUTCHAR       (*(void(*)())0x2022)  /* AL char, AH colour, BX x px, CL y */
#define VID_SAVE_RECT     (*(void(*)())0x2026)  /* AH x8, AL y, CH w8, CL rows, DI dst */
#define VID_RESTORE_RECT  (*(void(*)())0x2028)
#define VID_CURSOR_FRAME  (*(void(*)())0x202E)  /* AL colour, BH x4, BL y — hollow 20x20 box */
#define VID_DRAW_DIGITS   (*(void(*)())0x2030)  /* DI digits, AH x4, AL y, CL count, BL colour */
#define VID_TO_DECIMAL    (*(void(*)())0x2032)  /* DL:AX -> 7 ASCII-less digits at CS:DI */
#define VID_ICON_ITEM     (*(void(*)())0x2034)  /* AL 1-based (0 = blank), section 6 — shoes/cape */
#define VID_ICON_POTION   (*(void(*)())0x2036)  /* AL 1-based (0 = blank), section 5 */
#define VID_LABEL_ASCIIZ  (*(void(*)())0x2038)  /* DS:SI ASCIIZ, BH x4, BL y, CL xoff; SI advances */
#define VID_ICON_KEY      (*(void(*)())0x203A)  /* AL 0-based, section 4 */
#define VID_ICON_CREST    (*(void(*)())0x203C)  /* AL 0-based, section 2 */
#define VID_DISSOLVE      (*(void(*)())0x2040)

/* ---- kernel, BASE:0100 ------------------------------------------------- */
#define KRN_IDLE_0        (*(void(*)())0x110)   /* Ctrl+Q exit */
#define KRN_IDLE_1        (*(void(*)())0x112)   /* Esc pause */
#define KRN_IDLE_2        (*(void(*)())0x114)   /* F9 speed */
#define KRN_IDLE_3        (*(void(*)())0x116)   /* Ctrl+J */
#define KRN_IDLE_4        (*(void(*)())0x118)   /* Ctrl+K */
#define INT61H()          /* AL = kbd_dirs [FF17], AH = kbd_buttons [FF16] */

/* ---- state page -------------------------------------------------------- */
u16  key_mask;              /* FF18  bit0 = the menu key (Enter) */
u8   tick;                  /* FF1A */
u8   music_fade;            /* FF24 */
u8   menu_result;           /* FF4B  -> fight.bin 729C (8 = warp to town) */
u8   sfx_request;           /* FF75 */

/* ======================================================================== */
/* select.bin private variables (ADF8..AE1C, all zero in the image)          */
/* ======================================================================== */
u8   in_town;               /* ADF8  0xFF = entered from town.bin, 0 = from fight.bin */
u8   pane;                  /* ADF9  0 magic row, 1 item row, 2 potion row */
u8   n_magic;               /* ADFA  learned spells */
u8   magic_cursor;          /* ADFB  0..n_magic-1 */
u8   n_items;               /* ADFC  key items + 1 (the leading "NO USE" entry), 0 if none */
u8   item_cursor;           /* ADFD */
u8   n_potions;             /* ADFE  potions + 1 (leading "NO USE"), 0 if none */
u8   potion_sel;            /* ADFF  the slot value under the cursor (drug id + 1, 0 = none) */
u8   potion_cursor;         /* AE00 */
u8   menu_key_held;         /* AE01  the Enter that opened the screen is still down */
u8   box_open;              /* AE02  the message/level box is up and its rect is saved */
u8   magic_list[7];         /* AE03  spell numbers 1..7 that have been learned */
u8   item_list[6];          /* AE0A  [0] = 0 ("NO USE"), then the packed [A1..A5] ids */
u8   potion_list[6];        /* AE10  [0] = 0 ("NO USE"), then the packed [A6..AA] values */
u8   digits[7];             /* AE16  VID_TO_DECIMAL scratch */

/* ======================================================================== */
/* Data                                                                     */
/* ======================================================================== */

/* ADE8 — the four framed windows, drawn in order at entry */
static const struct { u8 x4, y, w4, h; } WINDOW[4] = {
    { 0x0C, 0x0E, 0x38, 0x33 },   /* magic  : (48,14) 224x51  */
    { 0x0C, 0x3F, 0x22, 0x30 },   /* wear   : (48,63) 136x48  */
    { 0x0C, 0x6D, 0x22, 0x30 },   /* use    : (48,109) 136x48 */
    { 0x2D, 0x3F, 0x17, 0x5E },   /* inventory: (180,63) 92x94 */
};

/* A9FC — the four headers.  `pane` picks the one drawn in red (2) instead of
 * green (3); index 3 ("INVENTORY") is never the active pane. */
static const struct { u16 x; u8 y; char text[]; } HEADER[4] = {
    { 0x0034, 0x12, "SELECT-MAGIC:" },      /* A9FC */
    { 0x0034, 0x43, "WEAR:"         },      /* AA0D */
    { 0x0034, 0x71, "USE:"          },      /* AA16 */
    { 0x00B8, 0x43, "INVENTORY"     },      /* AA1E */
};
static const char s_nothing[] = "NOTHING";      /* AA92 — empty magic / wear row */
static const char s_no_use[]  = "NO USE\0";     /* AA9A — empty potion row (2 lines) */
static const char s_level[]   = "LEVEL";        /* AAA2 */
static const char s_exp[]     = "EXP";          /* AAA8 */
static const char s_i_used[]  = "I have used";  /* AAAC */

/* AAB8 — spell names, indexed by [9D]-1 */
static const char *const MAGIC_NAME[7] = { "Espada", "Saeta", "Fuego", "Lanzar",
                                           "Rascar", "Agua", "Guerra" };
/* AAF3 — item name + description, indexed by [9E] (0 = nothing worn) */
static const char *const ITEM_NAME[6][2] = {
    { "NO USE",   ""            },  /* AA9A */
    { "Feruza",   "      shoes" },  /* AAFF */
    { "Pirika",   "      shoes" },  /* AB12 */
    { "Silkarn",  "      shoes" },  /* AB25 */
    { "Ruzeria",  "      shoes" },  /* AB39 */
    { "Asbestos", "       cape" },  /* AB4D */
};
/* AB62 — the "I have used …" line, indexed by potion_sel-1 = drug id */
static const char *const POTION_USED[8] = {   /* AB72, AB8A, ABA2, ... */
    "       a Ken\\ko Potion.",  "        a Juu-en Fruit.",
    "     a Elixir of Kashi.",   "      a Chikara Powder.",
    "         a Magia Stone.",   " a Holy Water of Acero.",
    "           a Sabre Oil.",   "       a Kioku Feather.",
};
/* AC32 — potion name + description for the USE: row, indexed by potion_sel
 * (0 = the leading "NO USE" entry) */
static const char *const POTION_NAME[9][2] = {
    { "NO USE",       ""            },  /* AA9A */
    { "Ken\\ko",      "      Potion" }, /* AC44 */
    { "Juu-en ",      "       Fruit" }, /* AC58 */
    { "Elixir",       "    of Kashi" }, /* AC6D */
    { "Chikara",      "      Powder" }, /* AC81 */
    { "Magia Stone", ""             },  /* AC96 */
    { "Holy Water",   "    of Acero" }, /* ACA3 */
    { "Sabre Oil",   ""             },  /* ACBB */
    { "Kioku",        "     feather" }, /* ACC6 */
};
/* ACD9 — sword names, indexed by [92]-1 */
static const char *const SWORD_NAME[6][2] = {
    { "Training",     "     Sword" }, { "Wise man\\s", "      Sword" },
    { "Spirit",       "    Sword"  }, { "Knight\\s",   "    Sword"  },
    { "Illumination", "       Sword" }, { "Enchantment", "       Sword" },
};
/* AD67 — shield names, indexed by [93]-1 */
static const char *const SHIELD_NAME[6][2] = {
    { "Clay",  "     Shield" }, { "Wise Man\\s", "      Shield" },
    { "Stone", "     Shield" }, { "Honor",       "     Shield" },
    { "Light", "     Shield" }, { "Titanium",    "      Shield" },
};
/* A520 — Holy Water of Acero: shield HP restored, indexed by [93]-1.
 * (The maxima [96] are armrpro's {30, 80, 180, 300, 300, 600}.) */
static const u16 HOLY_WATER[6] = { 80, 90, 100, 110, 115, 120 };
/* A584 — the Magia Stone orb template, patched per orb before each copy */
static u8 orb_template[7] = { 0, 0, 0x50, 0, 0, 0, 0 };

/* Row geometry.  Every row is a strip of 20x20 cursor cells; the icon sits two
 * rows of pixels inside the cursor box.  x4 units are 4 px. */
#define MAGIC_ICON_X4   0x0E   /* A8BC  step 8 x4 = 32 px */
#define MAGIC_ICON_Y    0x1C
#define MAGIC_CUR_X4    0x0E   /* A17B  bx = cursor*8 + 0x0E1A */
#define MAGIC_CUR_Y     0x1A
#define ITEM_ICON_X4    0x0E   /* A6DE  step 5 x4 = 20 px */
#define ITEM_ICON_Y     0x55
#define ITEM_CUR_Y      0x53   /* A266  bx = cursor*5 + 0x0E53 */
#define POTION_ICON_X4  0x0E   /* A676 */
#define POTION_ICON_Y   0x83
#define POTION_CUR_Y    0x81   /* A37A  bx = cursor*5 + 0x0E81 */

/* ======================================================================== */
/* Entry                                                                     */
/* ======================================================================== */

/* ==== [A000] @ A004 — entry from fight.bin (72D9 `call [A000]`) ========== */
void select_entry_fight(void) { in_town = 0x00; goto select_main; }   /* A004/A009 */
/* ==== [A002] @ A00B — entry from town.bin (6909 `call [A002]`) =========== */
void select_entry_town(void)  { in_town = 0xFF; goto select_main; }   /* A00B */

/* ==== select_main @ A010 ================================================ */
void select_main(void)
{
    box_open = 0;                                              /* A010 */
    for (i = 0; i < 4; i++)                                    /* A015..A02C */
        VID_WINDOW(0xFF, WINDOW[i].x4, WINDOW[i].y, WINDOW[i].w4, WINDOW[i].h);
    draw_headers();                                            /* A02E -> A9D5 */

    /* pack the learned spells into magic_list[] (A033..A04E) */
    n_magic = 0;
    for (n = 1; n <= 7; n++) if (player[0xBB + n - 1]) magic_list[n_magic++] = n;

    /* pack [A1..A5] into item_list[1..], slot 0 = "NO USE" (A052..A071) */
    item_list[0] = 0;  cl = 0;
    for (i = 0; i < 5; i++) if (player[0xA1 + i]) item_list[1 + cl++] = player[0xA1 + i];
    n_items = cl ? cl + 1 : 0;                                 /* A06B: 0 stays 0 */

    build_potion_list();                                       /* A075 -> A643 */
    draw_magic_row();                                          /* A078 -> A8AF */
    draw_item_row();                                           /* A07B -> A6D1 */
    draw_potion_row();                                         /* A07E -> A669 */
    draw_equipment();                                          /* A081 -> A752 */
    menu_key_held = idle_poll() ? 0xFF : 0;                    /* A084..A089 (sbb al,al) */

    /* first non-empty row wins; the potion row is skipped in town (A09E) */
    cl = 0;
    if (n_magic)                    goto run;                  /* A08E */
    cl = 1;
    if (n_items)                    goto run;                  /* A097 */
    if (in_town)                    goto idle_only;            /* A09E */
    cl = 2;
    if (n_potions)                  goto run;                  /* A0A7 */
idle_only:
    while (!idle_poll()) ;                                     /* A0AE: nothing to select */
    return;
run:
    pane = cl;                                                 /* A0B4 */
dispatch:                                                      /* A0B8 */
    PANE[pane]();                                              /* A0C0 jmp [A0C4 + pane*2] */
    return;
}
/* A0C4 */ static void (*const PANE[3])(void) = { pane_magic /*A0CA*/,
                                                  pane_item  /*A1BB*/,
                                                  pane_potion/*A2B9*/ };

/* ======================================================================== */
/* Row 0 — SELECT-MAGIC                                                      */
/* ======================================================================== */

/* ==== pane_magic @ A0CA ================================================= */
void pane_magic(void)
{
    draw_headers();                                            /* A0CA */
    magic_cursor_draw(2);                                      /* A0CD/A0CF: red = active */
    do INT61H(); while (al & 3);                               /* A0D2: wait for the buttons */
    for (;;) {
        if (idle_poll()) return;                               /* A0D8: Enter closes the screen */
        INT61H();                                              /* A0DE  AL = kbd_dirs */
        if (!(al & 0x0E)) continue;                            /* A0E0: down / left / right only */
        if (!(al & 0x0C)) goto down;                           /* A0E4: bit1 = DOWN -> next row */
        if (al & 4) {                                          /* A0EB: bit2 = LEFT */
            if (magic_cursor == 0) continue;                   /* A116 */
            magic_cursor_draw(0);                              /* A11D: erase */
            magic_cursor--;                                    /* A122 */
        } else {                                               /* bit3 = RIGHT */
            if (n_magic - 1 < magic_cursor + 1) continue;      /* A0EF..A0FC */
            magic_cursor_draw(0);
            magic_cursor++;                                    /* A103 */
        }
        magic_cursor_draw(2);
        sfx_request = 0x0C;                                    /* A10C: menu move */
        magic_select();                                        /* A111 -> A135 */
    }
down:                                                          /* A190 */
    cl = 1;  if (n_items)   goto leave;                        /* A192 */
    /* A199 `test in_town` — the result is discarded by the `mov cl,2` that
     * follows, so unlike the item row (A293) the potion row IS reachable from
     * the magic row in town.  Original bug. */
    cl = 2;  if (n_potions) goto leave;                        /* A1A0 */
    return_to_loop();
leave:
    sfx_request = 0x0D;                                        /* A1AA: row change */
    pane = cl;                                                 /* A1AF */
    magic_cursor_draw(5);                                      /* A1B3: blue = inactive */
    goto dispatch;                                             /* A1B8 -> A0B8 */
}

/* ==== magic_select @ A135 — commit the cursor to [9D] and repaint ======== */
void magic_select(void)
{
    magic_sel = magic_list[magic_cursor];                      /* A135..A13C  [9D] */
    VID_WINDOW(0, x4 = 0x27, y = 0x11, w4 = 0x10, h = 0x09);   /* A13F..A147: clear the name */
    puts_shadow(MAGIC_NAME[magic_sel - 1], x = 0x9E, y = 0x12, colour = 1);   /* A14C..A161 */
    VID_ICON_MAGIC(magic_sel, x4 = 0x37, y = 0xA4);            /* A164..A16A: the HUD slot (222,164) */
    VID_NUM_MAGIC_CNT();                                       /* A16F: charges at (220,187) */
    do INT61H(); while (al & 0x0C);                            /* A174: wait for left/right release */
}
/* ==== magic_cursor_draw @ A17B ========================================== */
void magic_cursor_draw(u8 colour)
{ VID_CURSOR_FRAME(colour, MAGIC_CUR_X4 + magic_cursor * 8, MAGIC_CUR_Y); }   /* bx = cursor*8 + 0x0E1A */

/* ==== draw_magic_row @ A8AF ============================================= */
void draw_magic_row(void)
{
    if (!n_magic) { puts_shadow(s_nothing, 0x9E, 0x12, 1); return; }    /* A8AF/A91C */
    for (i = 0; i < n_magic; i++)                                       /* A8B6..A8D2 */
        VID_ICON_MAGIC(magic_list[i], MAGIC_ICON_X4 + i * 8, MAGIC_ICON_Y);
    draw_magic_counts();                                                /* A8D4 -> A929 */
    /* the cursor starts on the selected spell (repne scasb over magic_list) */
    magic_cursor = index_of(magic_list, 7, magic_sel);                  /* A8D7..A8E9 */
    VID_CURSOR_FRAME(5, MAGIC_CUR_X4 + magic_cursor * 8, MAGIC_CUR_Y);  /* A8ED..A8FF: blue */
    puts_shadow(MAGIC_NAME[magic_sel - 1], 0x9E, 0x12, 1);              /* A904..A919 */
}
/* ==== draw_magic_counts @ A929 — "nnn" over "(mmm)" under each icon ===== */
void draw_magic_counts(void)
{
    dx = 0x0E2E;                                              /* A929: x4 0x0E, y 0x2E */
    for (i = 0; i < n_magic; i++, dx += 0x0800) {             /* A9AA: +8 x4 per column */
        u8 n = magic_list[i] - 1;
        VID_WINDOW(0, dx.x4, dx.y, w4 = 5, h = 8);            /* A94B..A952 */
        draw_number(player[0xAB + n], 3, dx, colour = 1);     /* A957..A960: charges */
        dx2 = dx + 9;                                         /* A964: the second line, y+9 */
        VID_PUTCHAR('(', 4, (dx2.x4 - 2) * 4 + 2, dx2.y);     /* A968..A97C */
        draw_number(player[0xB4 + n], 3, dx2, colour = 4);    /* A982..A98D: maximum */
        VID_PUTCHAR(')', 4, (dx2.x4 + 4) * 4 - 1, dx2.y);     /* A991..A9A4 */
    }
}

/* ======================================================================== */
/* Row 1 — WEAR (the key items; selecting one *wears* it: [9E])              */
/* ======================================================================== */

/* ==== pane_item @ A1BB ================================================== */
void pane_item(void)
{
    draw_headers();                                            /* A1BB */
    item_cursor_draw(2);                                       /* A1BE/A1C0 */
    do INT61H(); while (al & 3);                               /* A1C3 */
    for (;;) {
        if (idle_poll()) return;                               /* A1C9 */
        INT61H();  ah = al;                                    /* A1CF..A1D5  (AH keeps bit0 = UP) */
        if (!(al & 0x0F)) continue;                            /* A1D1 */
        if (!(al & 0x0C)) goto vertical;                       /* A1D7: up or down */
        if (al & 4) {                                          /* A1DE: LEFT */
            if (item_cursor == 0) continue;                    /* A209 */
            item_cursor_draw(0); item_cursor--;                /* A210..A215 */
        } else {                                               /* RIGHT */
            if (n_items - 1 < item_cursor + 1) continue;       /* A1E2..A1EF */
            item_cursor_draw(0); item_cursor++;                /* A1F1..A1F6 */
        }
        item_cursor_draw(2);
        sfx_request = 0x0C;                                    /* A1FF */
        item_select();                                         /* A204 -> A228 */
    }
vertical:                                                      /* A27D */
    if (ah & 1) {                                              /* UP -> the magic row */
        if (!n_magic) continue;                                /* A282 */
        pane = 0;                                              /* A28C */
    } else {                                                   /* DOWN -> the potion row */
        if (in_town)   continue;                               /* A293: potions are cavern-only */
        if (!n_potions) continue;                              /* A29D */
        pane = 2;                                              /* A2A7 */
    }
    sfx_request = 0x0D;                                        /* A2AC */
    item_cursor_draw(5);                                       /* A2B1 */
    goto dispatch;                                             /* A2B6 -> A0B8 */
}

/* ==== item_select @ A228 — wear the item under the cursor ([9E]) ========= */
void item_select(void)
{
    shoes = item_list[item_cursor];                            /* A228..A22F  [9E]; 0 = nothing worn */
    VID_WINDOW(0, x4 = 0x17, y = 0x42, w4 = 0x16, h = 0x11);   /* A232..A23A: clear the name */
    puts_shadow(ITEM_NAME[shoes][0], 0x5C, 0x43, 1);           /* A23F..A252 */
    puts_shadow(ITEM_NAME[shoes][1], 0x5C, 0x4B, 1);           /* A255..A25C */
    do INT61H(); while (al & 0x0C);                            /* A25F */
}
/* ==== item_cursor_draw @ A266 =========================================== */
void item_cursor_draw(u8 colour)
{ VID_CURSOR_FRAME(colour, ITEM_ICON_X4 + item_cursor * 5, ITEM_CUR_Y); }     /* bx = cursor*5 + 0x0E53 */

/* ==== draw_item_row @ A6D1 ============================================== */
void draw_item_row(void)
{
    if (!n_items) { puts_shadow(s_nothing, 0x5C, 0x43, 1); return; }    /* A6D1/A745 */
    for (i = 0; i < n_items; i++)                                       /* A6D8..A6F4 */
        VID_ICON_ITEM(item_list[i], ITEM_ICON_X4 + i * 5, ITEM_ICON_Y); /* id 0 -> the blank icon */
    item_cursor = index_of(item_list, 6, shoes);                        /* A6F6..A708 */
    VID_CURSOR_FRAME(5, ITEM_ICON_X4 + item_cursor * 5, ITEM_CUR_Y);    /* A70C..A720 */
    puts_shadow(ITEM_NAME[shoes][0], 0x5C, 0x43, 1);                    /* A725..A738 */
    puts_shadow(ITEM_NAME[shoes][1], 0x5C, 0x4B, 1);                    /* A73B..A742 */
}

/* ======================================================================== */
/* Row 2 — USE (the potions).  Only reachable outside town.                  */
/* ======================================================================== */

/* ==== build_potion_list @ A643 ========================================== */
void build_potion_list(void)
{
    potion_list[0] = 0;  cl = 0;                               /* A645..A65A */
    for (i = 0; i < 5; i++) if (player[0xA6 + i]) potion_list[1 + cl++] = player[0xA6 + i];
    n_potions = cl ? cl + 1 : 0;                               /* A65E..A664 */
}
/* ==== draw_potion_row @ A669 ============================================ */
void draw_potion_row(void)
{
    if (!n_potions) { puts_shadow(s_nothing, 0x54, 0x71, 1); return; }  /* A669/A6C4 */
    for (i = 0; i < n_potions; i++)                                     /* A670..A68C */
        VID_ICON_POTION(potion_list[i], POTION_ICON_X4 + i * 5, POTION_ICON_Y);
    potion_sel = 0;  potion_cursor = 0;                                 /* A68E/A693 */
    if (in_town) return;                                                /* A698: no cursor in town */
    VID_CURSOR_FRAME(5, POTION_ICON_X4, POTION_CUR_Y);                  /* A6A0..A6A5 */
    VID_WINDOW(0, x4 = 0x15, y = 0x70, w4 = 0x18, h = 0x11);            /* A6AA..A6B2 */
    puts_shadow(s_no_use, 0x54, 0x71, 1);                               /* A6B7..A6C1 */
}
/* ==== potion_cursor_draw @ A37A ========================================= */
void potion_cursor_draw(u8 colour)
{ VID_CURSOR_FRAME(colour, POTION_ICON_X4 + potion_cursor * 5, POTION_CUR_Y); }

/* ==== pane_potion @ A2B9 =============================================== */
void pane_potion(void)
{
    draw_headers();                                            /* A2B9 */
    potion_cursor_draw(2);                                     /* A2BC/A2BE */
    do INT61H(); while (al & 3);                               /* A2C1 */
    for (;;) {
        if (idle_poll()) return;                               /* A2C7 */
        if (key_mask == 0x0286) { show_level_box(); continue; }/* A2CD -> A3B7 (hidden panel) */
        INT61H();
        if (ah & 1) { use_potion(); continue; }                /* A2DA: the sword button uses it */
        if (!(al & 0x0D)) continue;                            /* A2E2: up / left / right */
        close_box();                                           /* A2E7 -> A629: any key closes it */
        if (!(al & 0x0C)) goto up;                             /* A2EB: bit0 = UP -> previous row */
        if (al & 4) {                                          /* A2F2: LEFT */
            if (potion_cursor == 0) continue;                  /* A31D */
            potion_cursor_draw(0); potion_cursor--;            /* A324..A329 */
        } else {                                               /* RIGHT */
            if (n_potions - 1 < potion_cursor + 1) continue;   /* A2F6..A303 */
            potion_cursor_draw(0); potion_cursor++;            /* A305..A30A */
        }
        potion_cursor_draw(2);
        sfx_request = 0x0C;                                    /* A313 */
        potion_show();                                         /* A318 -> A33C */
    }
up:                                                            /* A391 */
    cl = 1;  if (n_items) goto leave;                          /* A393 */
    cl = 0;  if (n_magic) goto leave;                          /* A39C */
    return_to_loop();
leave:
    pane = cl;  sfx_request = 0x0D;                            /* A3A6/A3AA */
    potion_cursor_draw(5);                                     /* A3AF */
    goto dispatch;                                             /* A3B4 -> A0B8 */
}

/* ==== potion_show @ A33C — name the potion under the cursor ============= */
void potion_show(void)
{
    potion_sel = potion_list[potion_cursor];                   /* A33C..A343  ADFF */
    VID_WINDOW(0, x4 = 0x15, y = 0x70, w4 = 0x18, h = 0x11);   /* A346..A34E */
    puts_shadow(POTION_NAME[potion_sel][0], 0x54, 0x70, 1);    /* A353..A366 */
    puts_shadow(POTION_NAME[potion_sel][1], 0x54, 0x78, 1);    /* A369..A370 */
    do INT61H(); while (al & 0x0C);                            /* A373 */
}

/* ==== show_level_box @ A3B7 — hidden LEVEL / EXP panel ================== *
 * Reached only while the potion row is active and `key_mask [FF18] == 0x0286`
 * exactly (a specific multi-key chord).  Nothing else in the game reads the
 * level or the experience outside the sage. */
void show_level_box(void)
{
    if (box_open) return;                                      /* A3B7 */
    save_box_rect();                                           /* A3C1 -> A60F */
    VID_WINDOW(0xFF, x4 = 0x1B, y = 0x43, w4 = 0x1A, h = 0x24);/* A3C4..A3CC */
    puts_shadow(s_level, 0x80, 0x4C, 1);                       /* A3D1..A3DB */
    draw_number(level + 1, 2, (x4 = 0x2C, y = 0x4C), 6);       /* A3DE..A3EC  [8D]+1 */
    puts_shadow(s_exp, 0x80, 0x56, 1);                         /* A3EF..A3F9 */
    draw_number(exp, 5, (x4 = 0x28, y = 0x56), 6);             /* A3FC..A407  [8E] */
}

/* ======================================================================== */
/* Using a potion (A40D) and the eight effects                               */
/* ======================================================================== */

/* ==== use_potion @ A40D ================================================= */
void use_potion(void)
{
    if (!potion_sel) return;                                   /* A40D: the "NO USE" slot */
    close_box();                                               /* A417 -> A629 */
    push(A2C7 /* back to the potion loop */);                  /* A41A */
    push(A5B4 /* potion_epilogue */);                          /* A41E */
    /* clear the [A6..AA] slot the cursor points at: walk the record counting
     * non-zero entries until the count reaches potion_cursor (A422..A437) */
    for (bx = 0xA6, ch = 0; ch != potion_cursor; bx++) if (player[bx]) ch++;
    player[bx - 1] = 0;                                        /* A437 */
    build_potion_list();                                       /* A43B -> A643 */
    menu_result = potion_sel;                                  /* A43E  FF4B: 8 = Kioku Feather */
    POTION[potion_sel - 1]();                                  /* A44E jmp [A452 + (id)*2] */
}
/* A452 */ static void (*const POTION[8])(void) = {
    potion_kenko   /* A462 */, potion_juuen  /* A483 */, potion_elixir /* A496 */,
    potion_chikara /* A4BE */, potion_magia  /* A52C */, potion_holy   /* A4EA */,
    potion_sabre   /* A4DB */, potion_kioku  /* A58B */,
};

/* id 0 — Ken'ko Potion @ A462: +80 HP, capped at max_hp */
void potion_kenko(void)
{
    sfx_request = 0x0E;                                        /* A462 */
    hp += 0x50;                                                /* A467  [90] */
    if (hp > max_hp) hp = max_hp;                              /* A46C..A478  [B2] */
    VID_LIFE_BAR_CUR();                                        /* A47B */
}
/* id 1 — Juu-en Fruit @ A483: HP to full */
void potion_juuen(void)
{ sfx_request = 0x0E; hp = max_hp; VID_LIFE_BAR_CUR(); }       /* A483..A48E */

/* id 2 — Elixir of Kashi @ A496: refill the *selected* spell */
void potion_elixir(void)
{
    sfx_request = 0x0E;                                        /* A496 */
    if (!magic_sel) return;                                    /* A49B: nothing selected, wasted */
    player[0xAB + magic_sel - 1] = player[0xB4 + magic_sel - 1];   /* A4A3..A4AF */
    VID_NUM_MAGIC_CNT();                                       /* A4B3 */
    draw_magic_counts();                                       /* A4B8 -> A929 */
}
/* id 3 — Chikara Powder @ A4BE: refill *all seven* spells */
void potion_chikara(void)
{
    sfx_request = 0x0E;                                        /* A4BE */
    memcpy(&player[0xAB], &player[0xB4], 7);                   /* A4C5..A4CE */
    VID_NUM_MAGIC_CNT();  draw_magic_counts();                 /* A4D0/A4D5 */
}
/* id 6 — Sabre Oil @ A4DB: +1 sword power bonus [E4] (fight.bin's damage
 * multiplier; town.bin clears [E4] on every town entry, 60CC) */
void potion_sabre(void)
{ sfx_request = 0x0E; attack_bonus++; draw_power(); }          /* A4DB..A4E4 -> A86E */

/* id 5 — Holy Water of Acero @ A4EA: restore shield HP by shield type */
void potion_holy(void)
{
    sfx_request = 0x0E;                                        /* A4EA */
    if (!shield) return;                                       /* A4EF: no shield, wasted */
    shield_hp += HOLY_WATER[shield - 1];                       /* A4F7..A505  [94] += A520[] */
    if (shield_hp > shield_hp_max) shield_hp = shield_hp_max;   /* A509..A515  [96] */
    VID_NUM_SHIELD_HP();                                       /* A518 */
}
/* id 4 — Magia Stone @ A52C: arm the four orbiting spheres (EB60, 7 bytes
 * each; docs/FIGHT.md §6): phases 0/4/8/0x0C, directions +1/-1/-1/+1, 0x50
 * hits each.  In town the records are written but nothing runs them. */
void potion_magia(void)
{
    sfx_request = 0x0E;                                        /* A52E */
    orb_template[0] = 0x00; orb_template[1] = 0x01; memcpy(0xEB60, orb_template, 7);  /* A533..A546 */
    orb_template[0] = 0x04; orb_template[1] = 0xFF; memcpy(0xEB67, orb_template, 7);  /* A548..A55B */
    orb_template[0] = 0x08;                         memcpy(0xEB6E, orb_template, 7);  /* A55D..A56B */
    orb_template[0] = 0x0C; orb_template[1] = 0x01; memcpy(0xEB75, orb_template, 7);  /* A56D..A580 */
}
/* id 7 — Kioku Feather @ A58B: back to the last sage.  Unwinds the two return
 * addresses use_potion pushed and returns straight out of the overlay with
 * menu_result [FF4B] = 8; fight.bin 729C then runs return_to_town (72D9). */
void potion_kioku(void)
{
    sfx_request = 0x0F;                                        /* A58B */
    show_used_box();                                           /* A590 -> A5DA */
    potion_epilogue();                                         /* A593 -> A5B4 */
    pop(); pop();                                              /* A596/A597: discard A5B4 + A2C7 */
    music_fade = 8;                                            /* A598  FF24 */
    for (tick = 0; tick < 0x78; ) ;                            /* A59D..A5A7: 120 ticks */
    VID_DISSOLVE();                                            /* A5A9 */
    int60h(ax = 1);                                            /* A5AE: stop the music */
    /* ret -> select_main's caller */
}

/* ==== potion_epilogue @ A5B4 — repaint the row after a potion is spent === */
void potion_epilogue(void)
{
    potion_cursor_draw(0);                                     /* A5B4 */
    VID_WINDOW(0, x4 = 0x0E, y = 0x83, w4 = 0x1E, h = 0x10);   /* A5B9..A5C1: clear the icons */
    if (!n_potions) n_potions = 1;                             /* A5C6: keep the "NO USE" slot */
    draw_potion_row();                                         /* A5D2 -> A669 */
    potion_cursor_draw(2);                                     /* A5D5 */
}
/* ==== show_used_box @ A5DA — "I have used / <potion>" ==================== */
void show_used_box(void)
{
    save_box_rect();                                           /* A5DA -> A60F */
    VID_WINDOW(0xFF, x4 = 0x0F, y = 0x43, w4 = 0x32, h = 0x24);/* A5DD..A5E5 */
    puts_shadow(s_i_used, 0x44, 0x4C, 1);                      /* A5EA..A5F4 */
    puts_shadow(POTION_USED[potion_sel - 1], 0x48, 0x56, 1);   /* A5F7..A60C */
}
/* ==== save_box_rect @ A60F / close_box @ A629 =========================== *
 * The message box overlaps the WEAR / INVENTORY windows, so the 28x36-cell
 * region behind it is parked in the driver's staging buffer and put back when
 * any key is pressed. */
void save_box_rect(void)
{ if (box_open) return; box_open = 0xFF; VID_SAVE_RECT(x8 = 0x06, y = 0x43, w8 = 0x1C, rows = 0x24, di = 0); }
void close_box(void)
{ if (!box_open) return; box_open = 0; VID_RESTORE_RECT(x8 = 0x06, y = 0x43, w8 = 0x1C, rows = 0x24, di = 0); }

/* ======================================================================== */
/* The INVENTORY window (right): sword, shield, keys, crests                 */
/* ======================================================================== */

/* ==== draw_equipment @ A752 ============================================= */
void draw_equipment(void)
{
    if (sword) {                                               /* A752  [92] */
        VID_ICON_SWORD(sword, x8 = 0x17, y = 0x4D);            /* A759..A75F: 20x18 at (184,77) */
        VID_LABEL_ASCIIZ(SWORD_NAME[sword - 1][0], x4 = 0x34, y = 0x4E, 0);   /* A764..A777 */
        VID_LABEL_ASCIIZ(SWORD_NAME[sword - 1][1], x4 = 0x34, y = 0x56, 0);   /* A77C..A781 */
        draw_power();                                          /* A786 -> A86E */
    }
    if (shield) {                                              /* A789  [93] */
        VID_ICON_SHIELD(shield, x4 = 0x2E, y = 0x61);          /* A790..A796: (186,97) */
        VID_LABEL_ASCIIZ(SHIELD_NAME[shield - 1][0], x4 = 0x34, y = 0x61, 0); /* A79B..A7AE */
        VID_LABEL_ASCIIZ(SHIELD_NAME[shield - 1][1], x4 = 0x34, y = 0x69, 0); /* A7B3..A7B8 */
        draw_shield_hp();                                      /* A7BD -> A844 */
    }
    if (keys) {                                                /* A7C0  [98] */
        VID_ICON_KEY(0, x4 = 0x2E, y = 0x75);                  /* A7C7..A7CC: (186,117) */
        VID_PUTCHAR(0x5E, 1, x = 0xC8, y = 0x7E);              /* A7D1..A7DA: the "x" glyph */
        draw_number(keys, 1, (x4 = 0x34, y = 0x7E), 1);        /* A7DF..A7EC */
    }
    if (lion_keys) {                                           /* A7EF  [99] */
        VID_ICON_KEY(1, x4 = 0x3A, y = 0x75);                  /* A7F6..A7FB: (234,117) */
        VID_PUTCHAR(0x5E, 1, x = 0xF8, y = 0x7E);              /* A800..A809 */
        draw_number(lion_keys, 1, (x4 = 0x40, y = 0x7E), 1);   /* A80E..A81B */
    }
    /* [9A] Elf Crest, [9B] Crest of Glory, [9C] Hero's Crest — packed left */
    for (i = 0, bx = 0x3089; i < 3; i++)                       /* A81E..A841 */
        if (player[0x9A + i]) { VID_ICON_CREST(i, bx.x4, bx.y); bx += 0x0600; }  /* (192,137) +24 px */
}
/* ==== draw_shield_hp @ A844 — "(mmm)" under the shield name ============= */
void draw_shield_hp(void)
{
    draw_number(shield_hp_max, 3, (x4 = 0x34, y = 0x69), 4);   /* A844..A84F  [96] */
    VID_PUTCHAR('(', 4, x = 0xCA, y = 0x69);                   /* A852..A85B */
    VID_PUTCHAR(')', 4, x = 0xE0, y = 0x69);                   /* A860..A869 */
}
/* ==== draw_power @ A86E — the Sabre Oil bonus, "(n)" under the sword ==== */
void draw_power(void)
{
    if (!attack_bonus) return;                                 /* A86E  [E4] */
    VID_WINDOW(0, x4 = 0x32, y = 0x57, w4 = 0x08, h = 0x04);   /* A876..A87E */
    VID_PUTCHAR('(', 1, x = 0xCA, y = 0x57);                   /* A883..A88C */
    draw_number(attack_bonus, 1, (x4 = 0x34, y = 0x57), 1);    /* A891..A89E */
    VID_PUTCHAR(')', 1, x = 0xD4, y = 0x57);                   /* A8A1..A8AA */
}

/* ======================================================================== */
/* Small helpers                                                             */
/* ======================================================================== */

/* ==== draw_number @ A9B3 — value -> the last `n` of 7 decimal digits ===== */
void draw_number(u16 value, u8 n, struct { u8 x4, y; } pos, u8 colour)
{
    VID_TO_DECIMAL(dl = 0, ax = value, di = digits);           /* A9B6..A9BB */
    VID_DRAW_DIGITS(di = digits + (7 - n), ah = pos.x4, al = pos.y, cl = n, bh = 0, bl = colour);
}
/* ==== draw_headers @ A9D5 — the four window captions ==================== */
void draw_headers(void)
{
    for (i = 0; i < 4; i++)                                    /* A9D5..A9F9 */
        puts_shadow(HEADER[i].text, HEADER[i].x, HEADER[i].y, pane == i ? 2 : 3);
}
/* ==== puts_shadow @ AA2B — ASCIIZ, 8 px pitch, blue drop shadow ========== *
 * colour 1 draws flat; every other colour first draws the same glyph one
 * pixel down and right in colour 5 (blue). */
void puts_shadow(const char *s, u16 x, u8 y, u8 colour)
{
    for (; *s; s++, x += 8) {                                  /* AA2B/AA53 */
        if (colour != 1) VID_PUTCHAR(*s, 5, x + 1, y + 1);     /* AA32..AA3F */
        VID_PUTCHAR(*s, colour, x, y);                         /* AA47..AA4A */
    }
}
/* ==== idle_poll @ AA58 — one wait iteration; returns "close the screen" == *
 * Runs the five kernel hotkey services, then reports the menu key.  While
 * `menu_key_held` is set (the Enter that opened the screen has not been
 * released) it always reports 0 and clears the latch on release — the
 * debounce that stops the screen from closing immediately. */
int idle_poll(void)
{
    KRN_IDLE_0(); KRN_IDLE_1(); KRN_IDLE_2(); KRN_IDLE_3(); KRN_IDLE_4();   /* AA58..AA6C */
    if (!menu_key_held) return menu_down();                    /* AA71 -> AA86 */
    if (menu_down()) return 0;                                 /* AA78..AA7E */
    menu_key_held = 0;  return 0;                              /* AA7F..AA85 */
}
/* ==== menu_down @ AA86 ================================================= */
int menu_down(void) { return (key_mask & 1) != 0; }            /* AA86: CF = FF18 bit0 */

/* index_of: the `repne scasb` at A6F6 / A8D7 — the position of `v` in the
 * first `n` bytes of `list`, or the last index when it is not there. */
u8 index_of(const u8 *list, u8 n, u8 v)
{
    for (i = 0; i < n; i++) if (list[i] == v) return i;
    return n - 1;                       /* `repne scasb` exhausted: CX = 0 */
}

/* ------------------------------------------------------------------------ *
 * Notes
 * -----
 * * `\` in every name table is the game's apostrophe glyph (font.grp 0x5C),
 *   the same convention as the dialogue text (docs/TOWN.md §6).
 * * The magic row is drawn with [201E] and its charges with [2018]; the shield
 *   with [2020] and its durability with [201A].  docs/VIDEO_DRIVERS.md names
 *   those slots `vid_icon_item` / `vid_num_item_count` / `vid_icon_magic` /
 *   `vid_num_magic` — from select.bin's use they are really the **magic** icon
 *   + charge count and the **shield** icon + durability, and the HUD's three
 *   boxes are sword [92] / magic [9D] / shield [93] left to right.
 * * The screen never clears the playfield itself: both callers do that before
 *   the swap, and the town/cavern border outside x 48..271 stays on screen.
 * * There is no "quit game", "save" or "equip sword" here — swords and shields
 *   are only ever changed in armrpro (src/shops.c).  This screen equips the
 *   *magic* ([9D]) and the *worn item* ([9E]) and consumes potions.
 * ------------------------------------------------------------------------ */
