/*
 * town.c — hand-cleaned decompilation of town.bin (ZELRES1[6], 7293 bytes),
 * the Zeliard town walk/shop/dialogue loop.  Slot-A overlay loaded raw to
 * BASE:6000 by GAME.BIN (A0B4).  Companion: docs/TOWN.md.
 *
 * NOT COMPILABLE.  Pseudo-C written from the ndisasm listing (origin 0x6000;
 * the Ghidra dump is stack-mangled because the overlay sets SP itself).
 * Every routine carries its original address; constants cite the instruction
 * they come from.  Register conventions are noted where the shop overlays
 * (kingpro..kenjpro @A000, src/shops.c) or the town renderer (gtmcga @3000)
 * depend on them.
 *
 * Conventions
 *   BASE:xxxx      shared code/data segment (CS = DS = BASE)
 *   arena:xxxx     64 KB graphics arena at [FF2C] (= BASE+0x1000)
 *   map            the town .mdt at BASE:C000 (docs/TOWN.md): RAW grid,
 *                  8 rows per column, column-major, width*8 bytes from C017
 *   town_col_ptr   [FF2A] = C017 + scroll_col*8: address of map column scroll_col.
 *                  Screen column c shows map column scroll_col + 4 + c (the
 *                  renderer skips 4 columns, gtmcga 3074), so there is no ring
 *                  buffer: the map itself is the ring.
 *   screen copy    28 columns x 8 rows at BASE:E000, COLUMN-MAJOR, stride 8.
 *                  0xFE = force redraw, 0xFF = protected (hero / NPC / dialogue
 *                  box), 0xFD = NPC marker (also written into the map itself).
 *   hero           2 columns x 3 rows (16x24 px) at screen column hero_scr_col
 *                  (0..0x1B), rows 5..7; map column = scroll_col+4+hero_scr_col.
 *                  Row 7 is the ground row (walkability test), row 5 the NPC row.
 *   screen layout  map rows at y = 78 + row*8 (gtmcga [3C98] = 0x61B0), x = 48 +
 *                  col*8; rows 0-2 blend with the 224x24 backdrop strip captured
 *                  from the ympd/ckpd panorama; y 142..157 = near parallax strips.
 */

/* ------------------------------------------------------------------------ */
/* Kernel / video / renderer entry points (docs/SERVICES.md, VIDEO_DRIVERS.md, */
/* TOWN.md §renderer)                                                          */
/* ------------------------------------------------------------------------ */
#define KRN_LOAD              (*(void(*)())0x10C)   /* AL mode, AH record, DS:SI request, ES:DI dest */
#define KRN_IDLE_0            (*(void(*)())0x110)   /* Ctrl+Q exit */
#define KRN_IDLE_1            (*(void(*)())0x112)   /* Esc pause */
#define KRN_IDLE_2            (*(void(*)())0x114)   /* F9 speed */
#define KRN_IDLE_3            (*(void(*)())0x116)   /* Ctrl+J */
#define KRN_IDLE_4            (*(void(*)())0x118)   /* Ctrl+K */
#define KRN_FIND_FILES        (*(void(*)())0x11C)   /* ES:DI buffer, DS:DX filespec */
#define KRN_RESTORE_QUERY     (*(bool(*)())0x11E)   /* CF=1: F7 "Restore Game" confirmed */
#define VID_WINDOW            (*(void(*)())0x2000)  /* AL style, BH x4, BL y, CH w4, CL h */
#define VID_CLEAR_PLAYFIELD   (*(void(*)())0x2002)
#define VID_GAUGE_BAR         (*(void(*)())0x2004)
#define VID_LIFE_BAR_MAX      (*(void(*)())0x2006)
#define VID_LIFE_BAR_CUR      (*(void(*)())0x2008)
#define VID_LABEL_HUD         (*(void(*)())0x200E)
#define VID_LABEL_TEXT        (*(void(*)())0x2010)
#define VID_ENEMY_TROUGH      (*(void(*)())0x2012)
#define VID_NUM_ALMAS         (*(void(*)())0x2014)  /* reads [8B] */
#define VID_NUM_GOLD          (*(void(*)())0x2016)  /* reads [85..87] */
#define VID_NUM_ITEM_COUNT    (*(void(*)())0x2018)
#define VID_NUM_MAGIC         (*(void(*)())0x201A)
#define VID_PUTCHAR           (*(void(*)())0x2022)  /* AL ch, AH colour, BX x px, CL y */
#define VID_SCROLL_UP_1       (*(void(*)())0x2024)  /* BH x8, BL y, CH w8, CL rows */
#define VID_SAVE_RECT         (*(void(*)())0x2026)  /* AH x8, AL y, CH w8, CL rows, DI staging offset */
#define VID_RESTORE_RECT      (*(void(*)())0x2028)
#define VID_PUTS              (*(void(*)())0x202A)  /* DS:SI 0xFF-terminated, BX x, CL y */
#define VID_LABEL_ASCIIZ      (*(void(*)())0x2038)  /* DS:SI, BH x4, BL y, CL xoff */
#define VID_DISSOLVE          (*(void(*)())0x2040)
#define VID_CLEAR_SCREEN      (*(void(*)())0x2042)
/* gtmcga.bin (ZELRES1[11] @3000) — town renderer vectors, see docs/TOWN.md */
#define GT_CAPTURE_BACKDROP   (*(void(*)())0x3002)  /* 3028: copy 224x24 px @ (48,78) -> BASE:A000 (28 x 0xC0) */
#define GT_FLUSH              (*(void(*)())0x3004)  /* 3051: redraw every cell whose E000 copy differs */
#define GT_SCROLL_NEAR_RIGHT  (*(void(*)())0x3006)  /* 3628: strips y142 (8 px) / y150 (16 px) -> right */
#define GT_SCROLL_FAR_RIGHT   (*(void(*)())0x3008)  /* 3677: strip y14..29 -> right 4 px */
#define GT_SCROLL_NEAR_LEFT   (*(void(*)())0x300A)  /* 36A4 */
#define GT_SCROLL_FAR_LEFT    (*(void(*)())0x300C)  /* 36F1 */
#define GT_HERO_BACKDROP      (*(void(*)())0x300E)  /* 32FC: SI = 6 cells under the hero -> offscreen A000:FA00 */
#define GT_NPC_COMPOSE        (*(void(*)())0x3010)  /* 3526: BX position 1..3, SI = under_hero, DI = frame ptr */
#define GT_HERO_DRAW          (*(void(*)())0x3012)  /* 359A: SI = 6 hero cell indices (tman, 0-based) */
#define GT_NPC_FRAME          (*(void(*)())0x3014)  /* 34EC: SI = NPC record -> DI = arena:4000 + sprite*48 + frame*6 */
#define GT_CURSOR             (*(void(*)())0x3018)  /* 3785: BH x4, BL y — 9-row red arrow */
#define GT_MENU_LINE          (*(void(*)())0x301A)  /* 3805: SI = ptr table, AL = index -> line buffer (+ price when [FF57]) */
#define GT_MENU_BLIT          (*(void(*)())0x301C)  /* 37CC: line buffer -> BH x4, BL y, width [FF6A]*4, 9 rows */
#define GT_MENU_SCROLL_UP     (*(void(*)())0x301E)  /* 3999: BH,BL, CL rows, CH w4, AL buffer row revealed */
#define GT_MENU_SCROLL_DOWN   (*(void(*)())0x3020)  /* 39EF */
#define GT_BUILD_TILEBANK     (*(void(*)())0x3024)  /* 3AF9: arena:8100 cells + [8000] type table -> packed + D000 masks */
#define GT_CONVERT_SPRITES    (*(void(*)())0x3026)  /* 3A71: DS:SI cells, CX, ES:DI mask out */
/* select.bin (status screen, swapped in from arena:C000) and shop overlays (@A000) */
#define SELECT_ENTRY_1        (*(void(*)())0xA002)
#define SHOP_ENTRY            (*(void(*)())0xA000)  /* shop overlay vector 0: run the shop */
#define SHOP_HOOK             (*(void(*)())0xA002)  /* shop overlay vector 1: per-frame hook (portrait animation) */
#define SHOP_DEATH_ENTRY      (*(void(*)())0xA004)  /* kenjpro only: "you died" entry */

/* ------------------------------------------------------------------------ */
/* Player record (BASE:0000 page = STDPLY.BIN "standard player"; this whole   */
/* page is what NAME.USR contains — see kenjpro in src/shops.c)               */
/* ------------------------------------------------------------------------ */
u8   flag_advisor;         /* 0x04  bit7 set by dialogue opcode 0x8B (664D); cmap patch: enables NPC script 5 */
u8   flag_entering_cavern; /* 0x06  = 0xFF just before the mode-0 flip to fight.bin (702E) */
u8   flag_hero_crest;      /* 0x12  bit3: Hero's Crest found (bsmp patch: sentry lets you pass) */
u8   flag_elf_crest;       /* 0x34  bit7 = Elf Crest given (opcode 0x83, 6685); bit6 = Asbestos cape bought (0x89, 66F0) */
u8   flag_past_door;       /* 0x45  bit7: the "doorway to the past" (door dest 0xFF) has been used once (6F98) */
u8   jashiin_defeated;     /* 0x49  != 0 after the final boss: town shows the ending dialogue set, omoypro runs
                                    enddemo.  fight.c calls this "force_death" — it is the game-won flag */
u16  scroll_col;           /* 0x80  map column of screen column -4 (see town_col_ptr) */
u8   scroll_row;           /* 0x82  written for fight.bin at a cavern entrance (7009) */
u8   hero_scr_col;         /* 0x83  0..0x1B; 0xFF / 0x1C = walked off the left / right edge (6CB5) */
u8   gold_hi; u16 gold;    /* 0x85, 0x86  24-bit GOLD (HUD "GOLD", VID_NUM_GOLD).  fight.c named it gold_total */
u16  almas;                /* 0x8B  ALMAS (HUD "ALMAS", VID_NUM_ALMAS; 2500 buys the Asbestos cape, 66DC).  fight.c named it gold */
u8   shield;               /* 0x93  != 0 -> draw the shield gauge + count (6127) */
u8   magic_sel;            /* 0x9D  != 0 -> draw the magic gauge + count (610F) */
u8   unknown_9F;           /* 0x9F  cleared on every town entry (60CC); set by a shop (see shops.c) */
u8   elf_crest;            /* 0x9A  = 0xFF by opcode 0x83 (668A) — sits next to glory_crest 9B / hero_crest 9C */
u8   inventory[10];        /* 0xA1  0-terminated item-id list; item 5 = Asbestos cape appended at 6700 */
u8   hero_flags;           /* 0xC2  bit0 = facing left (67BA / 682D) */
u8   door_side;            /* 0xC3  = -(side & 1) of the cavern-entry record (700D) */
u8   cur_map;              /* 0xC4  system record # (AH of KRN_LOAD mode 1): 0x80|n for town n (6D30/7015) */
u8   attack_bonus;         /* 0xE4  cleared on every town entry (60C9) — temporary potion effect (drugpro) */
u8   hero_anim;            /* 0xE7  walk frame 0..3 (bit0 forced while idle, 6234); 4 = back view entering a door (6E5B) */
u8   hero_dead;            /* 0xE8  set by fight.bin; town sends the hero to the sage (616B) */

/* ------------------------------------------------------------------------ */
/* FF00 page (docs/STATE_PAGE.md + additions in docs/TOWN.md)                 */
/* ------------------------------------------------------------------------ */
u8   tick;                 /* FF1A */
u8   btn1_edge, btn2_edge; /* FF1D (Space / joystick A), FF1E (Alt / joystick B) */
u16  key_mask;             /* FF18  bit0 = Enter (68F3), F7 via KRN_RESTORE_QUERY */
u8   music_fade;           /* FF24  = 4 before a door / warp (6E7E, 6F9D) */
u8   music_faded;          /* FF26  the Dorado warp spins until != 0 (6FC1). uncertain */
u8   last_ascii;           /* FF29  name entry (7935) */
u8  *town_col_ptr;         /* FF2A  C017 + scroll_col*8 (6157, 67DC, 6856) */
u16  arena_seg;            /* FF2C */
u8   speed;                /* FF33  frame = 4*speed ticks (68BA) */
u8  *shop_text_ptr;        /* FF4C  shop_print_text cursor */
u8   shop_text_x;          /* FF4E  px inside the shop text box (0..0xD0) */
u8   shop_text_line;       /* FF4F  0..4, y = line*10 + 99 */
u16  tick_total;           /* FF50 */
u8   menu_visible_rows;    /* FF52 */
u8   menu_total;           /* FF53 */
u8   menu_x4, menu_y;      /* FF54, FF55  menu box position (BL/BH order: word = y | x4<<8) */
u8   menu_scroll;          /* FF56  index of the first visible row */
u8   menu_show_prices;     /* FF57  GT_MENU_LINE appends a price column when != 0 (cleared 75A5) */
u8   menu_item_ids[16];    /* FF58  row -> item id passed to GT_MENU_LINE (xlat at 73A3) */
u8   price_col_x;          /* FF68 */
u16  menu_width_x4;        /* FF6A */
char player_name[8];       /* FF6C  save-file base name (NAME.USR).  STATE_PAGE.md calls it music_drv_name — wrong */
u8   text_entry_mode;      /* FF74  = 0xFF while typing a name (783B): letters are not directions */
u8   sfx_request;          /* FF75  5 typewriter, 0x0B menu, 0x1D page, 0x1E dialogue open, 0x1F select, 1 blip */
u8   suppress_disk_prompt; /* FF78  = 0xFF around the NAME.USR load (75F2) */

/* ------------------------------------------------------------------------ */
/* town.bin locals 7C42..7C7C (zero in the file)                              */
/* ------------------------------------------------------------------------ */
u8   shop_active;          /* 7C42  idle_poll also runs SHOP_HOOK while set */
u8   boot_entry;           /* 7C43  0xFF via vector 1 (GAME.BIN boot / restore / warp), 0 via vector 0 (from fight) */
u8   walk_in;              /* 7C44  play the 5-step walk-in on entry */
u8   town_flags;           /* 7C45  level record byte 3: bit0 = UNDERGROUND town (ckpd backdrop, far parallax strip) */
u8   tileset;              /* 7C46  level record byte 4: 0 cpat 1 mpat 2 dpat (6DCE table) */
void (*walk_fn)(void);     /* 7C47  walk_left / walk_right used by the walk-in */
u16  hero_col_m1;          /* 7C49  hero map column - 1 */
u8   was_idle;             /* 7C4B  no direction pressed last frame (gates auto-talk) */
u16  dlg_pos;              /* 7C4C  dialogue box x8/y: 0x0718 facing right, 0x0B18 facing left */
u16  dlg_box;              /* 7C4E  adjusted box position (BH x8, BL y) */
u16  dlg_pos_saved;        /* 7C50 */
u8   shop_line_count;      /* 7C52  lines printed in the shop box since the last clear */
u8   dlg_x;                /* 7C53  px inside the dialogue box (0..0xA8) */
u8   dlg_line;             /* 7C54  0..7 */
u8   dlg_lines_left;       /* 7C56  lines still to show (max 8 per box) */
u8   dlg_lines_shown;      /* 7C57  lines shown since the last "more" pause */
u8  *dlg_ptr;              /* 7C58 */
u8   dlg_w4, dlg_h;        /* 7C5A, 7C5B  box width (0x2C = 176 px) / height (lines*10+6) */
u8   dlg_forced;           /* 7C5C  dialogue cannot be cancelled with Alt (auto-talk / opcode 0x85) */
u8   shop_text_mute;       /* 7C5D  opcode 0x13/0x15 */
u8   name_len;             /* 7C5E  caret position */
u8   name_len_used;        /* 7C5F  characters typed */
u16  name_x; u8 name_y;    /* 7C60, 7C62  name box (96, 86) */
u8   menu_cursor;          /* 7C63 */
u8   restart_chosen;       /* 7C64  "Re-Start" picked (a '-' in the name) */
u8   usr_request[2];       /* 7C65  {archive 0, res# 0} -> external file, name follows */
char usr_name[9];          /* 7C67  "NAME.USR" (8 x '`' = blank while typing) */
u8   under_hero[6];        /* 7C74  map cells under the hero: col rows 5,6,7 then col+1 rows 5,6,7 */
u8   around_hero[3];       /* 7C7A  row-5 cells at hero col -1, col, col+1 */

/* ------------------------------------------------------------------------ */
/* Map header (BASE:C000) — docs/TOWN.md                                      */
/* ------------------------------------------------------------------------ */
#define MAP_LEVEL      (*(u8 **)0xC000)     /* {music_flags, gfx, 0xFF, town_flags, tileset} */
#define MAP_WIDTH      (*(u16 *)0xC002)
#define MAP_LABEL      (*(u8 **)0xC004)     /* positioned label record for VID_LABEL_TEXT ("Muralla Town") */
#define MAP_EXITS      (*(struct exit **)0xC007)
#define MAP_DOORS      (*(struct door **)0xC009)
#define MAP_CAVES      (*(struct cave **)0xC00B)
#define MAP_DIALOGUE   (*(u8 ***)0xC00D)    /* script index -> text */
#define MAP_NPCS       (*(struct npc **)0xC00F)
#define MAP_NPC_RANGE  (*(u16 **)0xC011)    /* {min_col, max_col} walkers turn around here */
#define MAP_PATCHES    (*(u8 **)0xC015)
#define MAP_GRID       ((u8 *)0xC017)       /* [col][8] */
#define MAP_CELL(col, row) (MAP_GRID[(col) * 8 + (row)])
#define NPC_MARK   0xFD                     /* map/screen cell value standing for an NPC (row 5) */
#define NPC_ROW    5
#define GROUND_ROW 7

struct exit {       /* edge exits, no terminator: the loop looks for bit0 set (left edge) or clear (right) */
    u8  flags;      /* bit0 left edge; bit7 (any bit but 0) = cavern entry, dest is a MAP_CAVES index */
    u8  dest;       /* town map number 0..9 (-> cur_map 0x80|dest) or cavern-entry index */
    u8  gfx;        /* 0 mman.grp / 1 cman.grp -> arena:4000 (NPC sprites) */
    u8  tileset;    /* 0 cpat 1 mpat 2 dpat -> arena:8000 (tile bank) */
};
struct door {       /* 3-byte records, 0xFFFF end */
    u16 col;        /* hero map column (2 wide) must be col-1..col+1 (6E46) */
    u8  dest;       /* 0..7 shop overlay (shop_requests), 8..  cavern entry dest-8, 0xFF = doorway to the past */
};
struct cave {       /* 5-byte records, indexed */
    u16 col;        /* scroll_col = col - 16 (wrapped by the cavern width) */
    u8  row;        /* scroll_row = (row - 10) & 63 */
    u8  side;       /* bit0 -> door_side = 0xFF */
    u8  map;        /* system record # of the cavern map (MP10 = 0 ...) */
};
struct npc {        /* 8-byte records, 0xFFFF end */
    u16 col;        /* map column (updated by the behaviours) */
    u8  sprite;     /* bits 0-6 sprite 0..4 of mman/cman; bit7 = facing LEFT */
    u8  saved;      /* map cell that was under the marker (restored every frame, 6C4E) */
    u8  anim;       /* bit0 / bits0-3 frame counter, bits 4-5 step timer (behaviours) */
    u8  type;       /* 0..7 behaviour, table npc_behaviours @6B41 */
    u8  flags;      /* 0x40 solid (blocks walking), 0x80 approaches the hero and talks once */
    u8  script;     /* index into MAP_DIALOGUE */
};

/* Request blocks inside town.bin ({archive, res#, name}) */
static const u8 req_ympd_ckpd[2][11] = /* 6AD3 */ {{1, 0x09, "YMPD.BIN"}, {1, 0x0A, "CKPD.BIN"}};   /* ZELRES2[8], [9] */
static const u8 req_npc_gfx[2][11]   = /* 6D88 */ {{1, 0x1E, "MMAN.GRP"}, {1, 0x1F, "CMAN.GRP"}};   /* ZELRES2[29], [30] */
static const u8 req_tileset[3][11]   = /* 6DCE */ {{1, 0x22, "CPAT.GRP"}, {1, 0x23, "MPAT.GRP"}, {1, 0x24, "DPAT.GRP"}}; /* ZELRES2[33..35] */
static const u8 req_tman[11]         = /* 6E1E */ {1, 0x20, "TMAN.GRP"};                              /* ZELRES2[31]: hero cells */
static const u8 req_ugm2[11]         = /* 6FED */ {1, 0x32, "UGM2.MSD"};                              /* ZELRES2[49] */
static const u8 req_shop[8][14] = /* 6F07, 14 bytes each, indexed by door.dest */ {
    {1, 0x0B, "KINGPRO.BIN"}, {1, 0x0C, "OMOYPRO.BIN"}, {1, 0x12, "KENJPRO.BIN"}, {1, 0x0D, "ARMRPRO.BIN"},
    {1, 0x10, "DRUGPRO.BIN"}, {1, 0x0F, "CHURPRO.BIN"}, {1, 0x0E, "BANKPRO.BIN"}, {1, 0x11, "INNAPRO.BIN"},
};  /* = ZELRES2[10] king, [11] omoya, [17] sage, [12] armour, [15] drug, [14] church, [13] bank, [16] inn */
static const u8 req_game_bin[11]     = /* 767B */ {0, 0, "GAME.BIN"};
static const u8 req_stdply[12]       = /* 7688 */ {0, 0, "STDPLY.BIN"};
static const char usr_filespec[]     = /* 77A8 */ "*.usr";
static const char str_input_name[]   = /* 77AE */ "Input name:\xFF";
static const char str_restart[]      = /* 77BA */ "Re-Start";
static const char str_not_found[]    = /* 7667 */ "User File\rNot Found\xFF";
static const char str_take[]         = /* 6736 */ "Take\0No Take\0";
static const char str_yes_no[]       = /* 7513 */ "Yes\0No\0";
/* HUD labels for VID_LABEL_HUD: {x4, y, xoff, len, text} */
static const u8 lbl_life[]  = /* 6C93 */ {0x0E, 0xA3, 0, 4, "LIFE"};
static const u8 lbl_almas[] = /* 6C9B */ {0x1E, 0xBB, 3, 5, "ALMAS"};
static const u8 lbl_gold[]  = /* 6CA4 */ {0x0D, 0xBB, 1, 4, "GOLD"};
static const u8 lbl_place[] = /* 6CAC */ {0x0D, 0xAF, 1, 5, "PLACE"};
/* Hero frames: 6 tman cell indices (0-based) = col0 rows 0-2, col1 rows 0-2; index = hero_anim 0..4 */
static const u8 hero_frames_left[5][6]  = /* 6A3B */ {{0,2,4,1,3,5},{6,8,10,7,9,11},{0,12,14,1,13,15},{6,16,18,7,17,19},{20,22,24,21,23,25}};
static const u8 hero_frames_right[5][6] = /* 6A59 */ {{26,28,30,27,29,31},{32,34,36,33,35,37},{26,38,40,27,39,41},{32,42,44,33,43,45},{20,22,24,21,23,25}};
/* proportional 8x8 font metrics for chars 0x20..0x7F (dialogue + shop text) */
extern const u8 font_xoff[96];     /* 7B82  px subtracted from the cursor before drawing the glyph */
extern const u8 font_advance[96];  /* 7BE2  px the cursor advances: space 5, 'I' 5, '\\' 3, 'W'/'M' 8 ... */

/* ======================================================================== */
/* Entry vectors 6000..601F                                                  */
/* ======================================================================== */
/*
 *  [6000] 6026 town_entry_from_fight   [6002] 601E town_entry_boot
 *  [6004] 706C shop_print_text         [6006] 72C7 format_number
 *  [6008] 74D3 yes_no_prompt           [600A] 7570 gold_can_pay
 *  [600C] 7589 gold_add                [600E] 751A menu_draw_items
 *  [6010] 7344 menu_select             [6012] 7539 menu_draw_icons
 *  [6014] 7469 cursor_draw             [6016] 7042 idle_poll
 *  [6018] 747B cursor_up_anim          [601A] 74A7 cursor_down_anim
 *  [601C] 7592 restore_game
 */

/* ==== town_entry_boot @ 601E / town_entry_from_fight @ 6026 ============== */
/* GAME.BIN jumps to [6002] after loading the map [C4], its music and NPC gfx
 * (A1CB..A219); fight.bin returns through the kernel mode-0 swap with
 * `jmp [6000]` (fight 72D9, menu_result == 8).  The restore path and the
 * Dorado warp also use 601E. */
void town_entry(void)
{
    boot_entry = (entered_at_601E) ? 0xFF : 0;          /* 601E / 6026 */
    /* NPC sprite set (mman/cman) sits at arena:4000: 0x100 bytes of frame maps then
     * 48-byte cells at 4100.  Convert 0xA4 cells in place, masks -> (BASE+2000):7000. */
    GT_CONVERT_SPRITES(ds = arena, si = 0x4100, es = BASE + 0x2000, di = 0x7000, cx = 0xA4);  /* 602C..6041 */
    SP = 0x2000;                                        /* 6047: private stack below the video driver */
    town_init_hero_gfx();                               /* 604D -> 6DEF */
    hero_anim = 0;                                      /* 6050 */
    if (jashiin_defeated) hero_dead = 0;                /* 6055: the ending sequence enters the town "dead" */
    VID_CLEAR_PLAYFIELD();                              /* 6061 */
    town_read_level_record();                           /* 6066..6075: town_flags, tileset */
    walk_in = 0;                                        /* 6078 */
    if (!hero_dead) {
        if ((town_flags & 1) && !boot_entry) walk_in = 0xFF;   /* 6084..6092: underground town entered from a cavern */
        load_backdrop_module();                         /* 6097 -> 6AAF (ympd/ckpd) */
        load_tileset_and_paint_backdrop();              /* 609A -> 6AA2 */
        GT_CAPTURE_BACKDROP();                          /* 609D */
        if (!jashiin_defeated)
            int60(AX = 0, DS:SI = arena:0x3000);        /* 60A9..60B4: start the score already at arena:3000 */
    }
town_restart_60B7:                                      /* 60B7: also the target of the edge-exit map change */
    SP = 0x2000; DS = CS;
    apply_patches();                                    /* 60BE -> 6AED */
    btn1_edge = btn2_edge = 0;                          /* 60C1.. */
    attack_bonus = 0; unknown_9F = 0;                   /* 60C9, 60CC: temporary shop effects end on entry */
    VID_GAUGE_BAR(al = 0, bx = 0x0204, ch = 0x21);      /* 60CF: LIFE trough (50,160) w33  (fight 6C55 uses 0x0210) */
    VID_GAUGE_BAR(al = 0, bx = 0x021C, ch = 0x42);      /* 60DB: gold box trough */
    VID_GAUGE_BAR(al = 0, bx = 0x481C, ch = 0x42);      /* 60E7: almas box trough */
    VID_ENEMY_TROUGH();                                 /* 60F3 */
    draw_hud_labels();                                  /* 60F8 -> 6C72 */
    VID_LIFE_BAR_MAX(); VID_LIFE_BAR_CUR();             /* 60FB, 6100 */
    VID_NUM_ALMAS(); VID_NUM_GOLD();                    /* 6105, 610A */
    if (magic_sel) { VID_GAUGE_BAR(al = 0, bx = 0xAA1C, ch = 0x17); VID_NUM_ITEM_COUNT(); }  /* 610F..6122 */
    if (shield)    { VID_GAUGE_BAR(al = 0, bx = 0xC61C, ch = 0x17); VID_NUM_MAGIC(); }       /* 6127..613A */
    tileset = MAP_LEVEL[4];                             /* 613F..614B (re-read: level rec {.., FF, flags, tileset}) */
    VID_LABEL_TEXT(si = MAP_LABEL);                     /* 614E: "PLACE" name */
    town_col_ptr = MAP_GRID + scroll_col * 8;           /* 6157..6165 */
    npc_place_markers();                                /* 6168 -> 6C2B */
    if (hero_dead) {                                    /* 616B: died in the caverns -> the sage revives you */
        hero_dead = 0;
        load_backdrop_module();                         /* 6177 */
        push(0x61FC); push(0x6EAF);                     /* 617A..6181: return chain = shop_return then main loop */
        KRN_LOAD(al = 3, si = req_shop[2] /* KENJPRO */, es:di = BASE:0xA000);   /* 6182..618C */
        VID_DISSOLVE();                                 /* 6191 */
        int60(AX = 1);                                  /* 6196: music stop */
        shop_active = 0xFF;                             /* 619B */
        jmp SHOP_DEATH_ENTRY;                           /* 61A0: kenjpro [A004] */
    }
    memset(screen_copy /* E000 */, 0xFE, 0xE0);         /* 61A5..61AF: 28 x 8, force full redraw */
    frame();                                            /* 61B1 -> 68AC */
    if (walk_in) {                                      /* 61B4 */
        walk_fn = (hero_flags & 1) ? walk_left : walk_right;   /* 61BB..61C8 */
        for (cx = 4; cx; cx--) { walk_fn(); frame(); }  /* 61CE..61DB */
        walk_fn();                                      /* 61DD: 5th step, no frame */
    }
    was_idle = 0;                                       /* 61E2 */
    if (jashiin_defeated) int60(AX = 0, DS:SI = arena:0x3000);   /* 61E7..61FB: ending: music starts here instead */
    main_loop();                                        /* 61FC */
}

/* ==== main_loop @ 61FC ==================================================== */
void main_loop(void)
{
    for (;;) {
        frame();                                        /* 61FC -> 68AC */
        check_status_menu();                            /* 61FF -> 68F3 (Enter) */
        check_edge_exit();                              /* 6202 -> 6CB5 */
        check_talk();                                   /* 6205 -> 623F (Space) */
        if (!was_idle) check_auto_talk();               /* 6208..620F -> 62ED */
        was_idle = 0;                                   /* 6212 */
        push(0x61FC);                                   /* 6217: every handler below returns to the loop */
        al = int61().dirs;                              /* 621B: 1 up 2 down 4 left 8 right */
        if (al == 1)             { enter_door(); continue; }   /* 621D..6221 -> 6E29 (up = enter) */
        if ((al & 0xC) == 4)     { walk_left(); continue; }    /* 6224..622A */
        if ((al & 0xC) == 8)     { walk_right(); continue; }   /* 622D..6231 */
        hero_anim |= 1; was_idle = 0xFF;                /* 6234..6239: standing frame */
    }
}

/* ==== check_talk @ 623F — Space in front of an NPC ======================= */
void check_talk(void)
{
    if (!btn1_edge) return;                             /* 623F */
    btn1_edge = 0;                                      /* 6247 */
    bx = town_col_ptr + (hero_scr_col + 4) * 8 + NPC_ROW;   /* 624C..6264: cell (hero col, row 5) */
    dx = hero_scr_col + 4 + scroll_col;                 /* hero map column */
    if (!(hero_flags & 1)) {                            /* 6268: facing right: look 1..3 columns right */
        for (i = 1; i <= 3; i++) { dx++; if (bx[8 * i] == NPC_MARK) goto found_r; }   /* 626F..6282 */
        return;
    found_r:
        si = npc_find(dx);                              /* 6285 -> 6A94 */
        if (si->flags & 0xC0) return;                   /* 6288: solid / auto-talk NPCs do not answer Space */
        saved = (si->sprite, si->type);                 /* 6290..6296 */
        si->type = 7; si->sprite |= 0x80; si->anim |= 1;   /* 6297..629F: freeze, face left (toward the hero), talk frame */
        dialogue_start(si);                             /* 62A3 -> 635A */
        si->type = saved.type; si->sprite = saved.sprite;   /* 62A6..62AA */
    } else {                                            /* 62AE: facing left, look 1..3 columns left */
        for (i = 1; i <= 3; i++) { dx--; if (bx[-8 * i] == NPC_MARK) goto found_l; }   /* 62AE..62C1 */
        return;
    found_l:
        si = npc_find(dx);                              /* 62C4 */
        if (si->flags & 0xC0) return;
        saved = (si->sprite, si->type);
        si->type = 7; si->sprite &= 0x7F; si->anim |= 1;   /* 62D6..62DE: face right */
        dialogue_start(si);                             /* 62E2 */
        restore;
    }
}

/* ==== check_auto_talk @ 62ED — an 0x80-flagged NPC 2 columns ahead, facing us */
void check_auto_talk(void)
{
    bx = town_col_ptr + (hero_scr_col + 4) * 8 + NPC_ROW;   /* 62ED..6305 */
    dx = hero_scr_col + 4 + scroll_col;
    if (!(hero_flags & 1)) {                            /* 6309 */
        dx += 2; if (bx[0x10] != NPC_MARK) return;      /* 6310..6318 */
        si = npc_find(dx);                              /* 6319 */
        if (!(si->sprite & 0x80)) return;               /* 631C: he must face left (toward us) */
        if (!(si->flags & 0x80)) return;                /* 6323 */
    } else {
        dx -= 2; if (bx[-0x10] != NPC_MARK) return;     /* 6335..633D */
        si = npc_find(dx);                              /* 633E */
        if (si->sprite & 0x80) return;                  /* 6341: must face right */
        if (!(si->flags & 0x80)) return;                /* 6348 */
    }
    si->anim |= 1; dlg_forced = 0xFF;                   /* 632A/634F..6353: cannot be cancelled */
    dialogue_start(si);                                 /* 6333/6358 -> 635A */
}

/* ==== dialogue_start @ 635A — open the box for NPC si and run its script == */
void dialogue_start(struct npc *si)
{
    si->flags &= 0x7F;                                  /* 635A: auto-talk happens once */
    al = si->script;                                    /* 635E */
    tick = 0x28; frame_tail();                          /* 6363..6368: one immediate frame (68AF) */
    sfx_request = 0x1E;                                 /* 636B: dialogue open */
    dlg_pos = (hero_flags & 1) ? 0x0718 : 0x0B18;       /* 6370..637D: box at x8 7 (left of hero) or 11; y 24 */
    VID_SAVE_RECT(ax = 0, cx = 0x1658, di = 0);         /* 6380..6385: save the whole 22x88 area at (0,0) into staging */
    btn1_edge = 0;                                      /* 638A */
    dialogue_run(bl = al, ax = dlg_pos);                /* 638F..6393 -> 63C5 */
    VID_RESTORE_RECT(ax = dlg_pos, cx = 0x1658, di = 0);   /* 6396..639E */
    btn1_edge = 0;
    memset(screen_copy, 0xFE, 0xE0);                    /* 63A9..63B3: full redraw */
    dlg_forced = 0; btn1_edge = btn2_edge = 0;          /* 63B5..63BF */
}

/* ==== dialogue_run @ 63C5 — the town dialogue script interpreter ========= */
/* BL = script index into MAP_DIALOGUE, AX = box position.  Text is drawn with
 * the proportional font (font_xoff / font_advance) inside a 176-px box, word
 * wrapped at 0xA8 px, at most 8 lines per box (a 7-line "page" then a red '|'
 * marker waits for Space).  Opcodes: see docs/TOWN.md §dialogue. */
void dialogue_run(u8 script, u16 pos)
{
    hero_anim |= 1;                                     /* 63C5 */
    dlg_pos_saved = dlg_box = pos;                      /* 63CA, 63CD */
    si = MAP_DIALOGUE[script];                          /* 63D0..63D8 */
    dlg_x = dlg_line = dlg_lines_shown = 0; *(u8 *)0x7C55 = 0;   /* 63DA..63E9 */
    dlg_ptr = si;                                       /* 63EE */
    cl = count_lines(si);                               /* 63F2 -> 6609: word-wrapped line count */
    dlg_lines_left = cl;                                /* 63F7 */
    if (cl > 8) cl = 8;                                 /* 63FA..63FE */
    dlg_h = cl * 10 + 6; dlg_w4 = 0x2C;                 /* 6401..640B */
    /* vertical placement: y += 0x56 - h, then y -= (0x40 - (lines & ~1)*8) / 2  (640F..642A) */
    dlg_box.y += 0x56 - dlg_h; dlg_box.y -= (0x40 - ((cl & 0xFE) << 3)) >> 1;
    VID_WINDOW(al = 0xFF, bh = dlg_box.x8 * 2, bl = dlg_box.y, cx = dlg_w4 << 8 | dlg_h);   /* 642E..6432 */
    for (;;) {
        al = *dlg_ptr++;                                /* 6437..643C */
        switch (al) {
        case 0x2F: newline(); break;                    /* 6440 -> 64E6 */
        case 0x81: /* yes/no question: continue with script 12 (yes) / 13 (no) */          /* 6447 -> 6655 */
            VID_WINDOW(al = 0xFF, bx = (dlg_pos.x8*2 | 0x19<<8 ...) + 0x193F, cx = 0x0C19);   /* 6655..6665 */
            menu_x4y = bx + 0x0103;                     /* 666B..666F: [FF54] */
            cf = yes_no_prompt();                       /* 6673 -> 74D3 */
            return dialogue_run(cf ? 0x0D : 0x0C, dlg_pos);   /* 6676..6682 (tail call) */
        case 0x83: /* Elf Crest given */                /* 644E -> 6685 */
            flag_elf_crest |= 0x80; elf_crest = 0xFF;   /* 6685, 668A */
            apply_patches();                            /* 668F */
            goto end;                                   /* 6692 -> 65A1 */
        case 0x85: /* forced restart with script 4 */   /* 6455 -> 6695 */
            dlg_forced = 0xFF; return dialogue_run(4, dlg_pos_saved);   /* 6695..669F */
        case 0x87: /* wait for a key, then script 5 */  /* 645C -> 66A2 */
            dialogue_end(); return dialogue_run(5, dlg_pos_saved);       /* 66A2..66AA */
        case 0x89: /* "Take / No Take" for 2500 almas -> script 6 (no) / 7 (poor) / 8 (bought) */   /* 6463 -> 66AD */
            VID_WINDOW(al = 0xFF, bx = ... + 0x1832, cx = 0x1219);      /* 66AD..66BD */
            menu_x4y = bx + 0x0203;                     /* 66C3..66C7 */
            if (take_prompt()) return dialogue_run(6, dlg_pos);         /* 66CB -> 670E; 66D1..66D5 */
            if (almas < 0x9C4) return dialogue_run(7, dlg_pos);         /* 66D8..66E4: 2500 almas */
            almas -= 0x9C4; VID_NUM_ALMAS();            /* 66E7, 66EB */
            flag_elf_crest |= 0x40;                     /* 66F0: Asbestos cape flag */
            inventory[first_free_slot()] = 5;           /* 66F5..6700: item 5 = Asbestos cape */
            apply_patches();                            /* 6703 */
            return dialogue_run(8, dlg_pos);            /* 6706..670B */
        case 0x8B: flag_advisor |= 0x80; apply_patches(); goto end;   /* 646A -> 664D..6652 (no shipped text uses it) */
        case 0xFF: goto end;                            /* 6471 -> 65A1 */
        default:                                        /* 6478: printable */
            x = dlg_box.x8 * 8 + dlg_x + 4;             /* 6479..648E */
            y = dlg_box.y + dlg_line * 10 + 4;          /* 6491..649A */
            VID_PUTCHAR(al, ah = 1, bx = x - font_xoff[al - 0x20], cl = y);   /* 649E..64B2 */
            dlg_x += font_advance[al - 0x20];           /* 64B8..64C3 */
            if (al == ' ' && dlg_x + word_width(dlg_ptr) >= 0xA8) newline();   /* 64C7..64E3 -> 65E6 */
        }
    }
end:
    dialogue_end();                                     /* 65A1 */
}

/* ==== newline @ 64E6 ===================================================== */
void newline(void)
{
    dlg_x = 0; dlg_line++;                              /* 64E6, 64EB */
    if (dlg_line == 8) {                                /* 64EF: box full: scroll it up 10 px */
        dlg_line--;
        for (i = 0; i < 10; i++)
            VID_SCROLL_UP_1(bx = dlg_box + 4, ch = dlg_w4 >> 1, cl = dlg_h - 8);   /* 64FA..6514 */
    }
    dlg_lines_shown++;                                  /* 6516 */
    if (dlg_lines_shown < 7 || dlg_lines_left == 8) return;   /* 651A..652B */
    dlg_lines_left -= 7;                                /* 652E */
    /* "more" marker: red '|' (0x7C, colour 2) at the box's bottom-right (6533..654C) */
    VID_PUTCHAR(ax = 0x027C, bx = dlg_box.x8 * 8 + 0x54, cl = dlg_box.y + 0x4A);
    btn1_edge = btn2_edge = 0;
    do {                                                /* 655D..657B */
        dlg_protect_box(); frame();                     /* 655F -> 6743, 6562 -> 68AC */
        if (!dlg_forced && btn2_edge) return;           /* 6567..6575: Alt cancels (pops back into dialogue_run) */
    } while (!btn1_edge);
    VID_WINDOW(al = 0, bh = dlg_box.x8 * 2 + 0x15, bl = dlg_box.y + 0x4A, cx = 0x0208);   /* 657D..658A: erase marker */
    btn1_edge = 0; dlg_lines_shown = 0; sfx_request = 0x1D;   /* 658F..6599 */
}

/* ==== dialogue_end @ 65A1 — wait for release, then for Space/Alt ========= */
void dialogue_end(void)
{
    btn1_edge = btn2_edge = 0;                          /* 65A1 */
    do { dlg_protect_box(); frame(); if (btn1_edge || btn2_edge) return; } while (kbd_dirs /* FF17 */);   /* 65AB..65C6 */
    do { dlg_protect_box(); frame(); if (btn1_edge || btn2_edge) return; } while (!kbd_dirs);            /* 65C8..65E3 */
}

/* ==== word_width @ 65E6 — px of the next word (stops at space, '/', >= 0x80) */
u16 word_width(u8 *si) { cx = 0; while ((al = *si++) < 0x80 && al != ' ' && al != '/') if (al >= 0x20) cx += font_advance[al - 0x20]; return cx; }

/* ==== count_lines @ 6609 — lines the text will take with wrapping at 0xA8 */
u8 count_lines(u8 *si)
{
    cx = 0; dx = 0;
    for (;;) {
        al = *si++;
        if (al & 0x80) return dx ? cx + 1 : cx;         /* 660E, 6646..664C: opcode/terminator ends the text */
        if (al == '/') { cx++; dx = 0; continue; }      /* 6612..6619 */
        dx += font_advance[al - 0x20];                  /* 661B..6629 */
        if (al == ' ' && dx + word_width(si) >= 0xA8) { dx = 0; cx++; }   /* 662C..6644 */
    }
}

/* ==== take_prompt @ 670E — "Take / No Take"; CF=1 = declined ============== */
bool take_prompt(void)
{
    menu_visible_rows = menu_total = 2;                 /* 670E, 6713 */
    menu_draw_items(cx = 2, si = str_take);             /* 6718..671E -> 751A */
    menu_scroll = 0;                                    /* 6721 */
    cf = menu_select(bl = 0);                           /* 6726..6728 -> 7344 */
    return cf || bl != 0;                               /* 672B..6735 */
}

/* ==== dlg_protect_box @ 6743 — mark the box's cells 0xFF in the screen copy */
/* so GT_FLUSH does not draw the map over the dialogue box (3123: old 0xFF -> skip). */
void dlg_protect_box(void)
{
    ax = dlg_box; ah -= 6; al += dlg_h;                 /* 6743..674D: x8 - 6, y + h */
    if (al < 0x56) return;                              /* 674F: box entirely above map row 1 (y 78) */
    rows = (al - 0x4E) / 8;                             /* 6754..675E */
    di = 0xE000 + ah * 8;                               /* 6763..676C */
    for (r = 0; r < rows; r++) for (c = 0; c < 0x16; c++) screen_copy[(ah + c) * 8 + r] = 0xFF;   /* 6770..677E */
}

/* ==== walk_left @ 6781 / walk_right @ 67F4 =============================== */
void walk_left(void)
{
    al = town_col_ptr[(hero_scr_col + 3) * 8 + GROUND_ROW];       /* 6781..6794: ground cell left of the hero */
    if (!cell_walkable(al)) return;                     /* 6797 -> 686E */
    if (npc_solid_at(scroll_col + hero_scr_col + 4 - 1)) return;  /* 679D..67AE -> 6890 */
    hero_anim = (hero_anim + 1) & 3;                    /* 67B1, 67B5 */
    hero_flags |= 1;                                    /* 67BA: face left */
    if (hero_scr_col >= 0x0B) { hero_scr_col--; return; }         /* 67BF..67CA */
    if (scroll_col == 0)      { hero_scr_col--; return; }         /* 67CB..67D7: may reach 0xFF = left edge */
    scroll_col--; town_col_ptr -= 8;                    /* 67D8, 67DC */
    GT_SCROLL_NEAR_RIGHT();                             /* 67E1: the map itself is redrawn by GT_FLUSH */
    if (town_flags == 1) GT_SCROLL_FAR_RIGHT();         /* 67E6..67EE: underground towns have the far strip */
}
void walk_right(void)
{
    al = town_col_ptr[(hero_scr_col + 6) * 8 + GROUND_ROW];       /* 67F4..6807: ground cell right of the hero */
    if (!cell_walkable(al)) return;                     /* 680A */
    if (npc_solid_at(scroll_col + hero_scr_col + 4 + 1)) return;  /* 6810..6821 */
    hero_anim = (hero_anim + 1) & 3;                    /* 6824, 6828 */
    hero_flags &= ~1;                                   /* 682D: face right */
    if (hero_scr_col < 0x10) { hero_scr_col++; return; }          /* 6832..683D */
    if (scroll_col + 1 == MAP_WIDTH - 0x23) { hero_scr_col++; return; }   /* 683E..6851: may reach 0x1C = right edge */
    scroll_col++; town_col_ptr += 8;                    /* 6852, 6856 */
    GT_SCROLL_NEAR_LEFT();                              /* 685B */
    if (town_flags == 1) GT_SCROLL_FAR_LEFT();          /* 6860..6868 */
}

/* ==== cell_walkable @ 686E — AL ground cell; ZF=0 (true) when it does NOT  */
/* appear in the tile bank's BLOCK list (tileset section at arena:[8002] =  */
/* {count, cells...}; cpat {3C,3D}, mpat {96,97}, dpat {BF}). */
bool cell_walkable(u8 al)
{
    es = arena_seg; si = *(u16 far *)0x8002; cl = es[si];   /* 686E..6878 */
    if (cl == 0) return true;                           /* 687B..688B */
    while (cl--) if (es[++si] == al) return false;      /* 6881..6889 */
    return true;
}

/* ==== npc_solid_at @ 6890 — ZF=1 when a solid (0x40) NPC stands at column BX */
bool npc_solid_at(u16 col)
{
    for (si = MAP_NPCS; si->col != 0xFFFF; si++)        /* 6890..68AA */
        if (si->col == col && (si->flags & 0x40)) return true;
    return false;
}

/* ==== frame @ 68AC / frame_tail @ 68AF ================================== */
void frame(void)
{
    npc_update();                                       /* 68AC -> 6B1C */
frame_tail:                                             /* 68AF: callers preset tick = 0x28 to skip the wait */
    hero_draw();                                        /* 68AF -> 6975 */
    hero_mark();                                        /* 68B2 -> 6950 */
    GT_FLUSH();                                         /* 68B5 */
    al = 4 * speed;                                     /* 68BA..68C0 */
    do {
        KRN_IDLE_0(); KRN_IDLE_1(); KRN_IDLE_2(); KRN_IDLE_3(); KRN_IDLE_4();   /* 68C3..68D7 */
        if (KRN_RESTORE_QUERY()) restore_game();        /* 68DC..68E3 -> 7592 (never returns) */
    } while (tick < al);                                /* 68E7..68EB */
    tick = 0;                                           /* 68ED */
}

/* ==== check_status_menu @ 68F3 — Enter: run select.bin (parked at arena:C000) */
void check_status_menu(void)
{
    if (!(key_mask & 1)) return;                        /* 68F3 */
    sfx_request = 0x0B;                                 /* 68FC */
    VID_CLEAR_PLAYFIELD();                              /* 6901 */
    swap_select_overlay();                              /* 6906 -> 6938: BASE:A000..AFFF <-> arena:C000 (0x800 words) */
    SELECT_ENTRY_1();                                   /* 6909: status / inventory screen */
    swap_select_overlay();                              /* 690E */
    VID_CLEAR_PLAYFIELD();                              /* 6911 */
    paint_backdrop();                                   /* 6916 -> 6AA5 */
    GT_CAPTURE_BACKDROP();                              /* 6919 */
    memset(screen_copy, 0xFE, 0xE0);                    /* 691E..6928 */
    tick_preset(0x28)? no: frame_tail();                /* 692A -> 68AF */
    btn1_edge = btn2_edge = 0;                          /* 692D, 6932 */
}
void swap_select_overlay(void)                          /* 6938 */
{   es = arena_seg; di = 0xC000; si = 0xA000;
    for (cx = 0x800; cx; cx--) { ax = es[di]; es[di] = *si; *si = ax; si += 2; di += 2; }   /* 6946..694D */
}

/* ==== hero_mark @ 6950 — protect the hero's 2x3 cells in the screen copy == */
void hero_mark(void)
{
    if (hero_scr_col >= 0x1B) return;                   /* 6950..6957 */
    di = 0xE000 + hero_scr_col * 8 + NPC_ROW;           /* 6958..6965 */
    di[0] = di[1] = di[2] = 0xFF; di[8] = di[9] = di[10] = 0xFF;   /* 6969..6973 */
}

/* ==== hero_draw @ 6975 — background under the hero, adjacent NPCs, hero == */
void hero_draw(void)
{
    si = town_col_ptr + (hero_scr_col + 4) * 8 + NPC_ROW;        /* 6975..6987 */
    under_hero[0..2] = si[0..2]; under_hero[3..5] = si[8..10];   /* 698E..699A: 2 columns, rows 5-7 */
    dx = hero_scr_col + 4 + scroll_col;                 /* 699B..69A4: hero map column */
    for (c = 0; c < 2; c++, dx++) {                     /* 69AC..69CF: replace NPC markers by the tile beneath */
        if (under_hero[c * 3] == NPC_MARK) {
            si = npc_find(dx);                          /* 69B6 -> 6A94 */
            while (si->saved == NPC_MARK) si = npc_find_from(si + 1, dx);   /* 69B9..69C6: stacked NPCs */
            under_hero[c * 3] = si->saved;
        }
    }
    GT_HERO_BACKDROP(si = under_hero);                  /* 69D1..69D4 */
    hero_col_m1 = dx - 2;                               /* 69D9..69DB (dx was advanced twice) */
    around_hero[0..2] = { si[-8], si[0], si[8] };       /* 69E0..69EF: row-5 cells at col-1, col, col+1 */
    for (si = MAP_NPCS; si->col != 0xFFFF; si++) {      /* 69F0..6A17 */
        al = npc_adjacent(si);                          /* 69F4 -> 6A77: 3 = at col-1, 2 = col, 1 = col+1, 0 = none */
        if (!al) continue;
        di = GT_NPC_FRAME(si);                          /* 69FC */
        GT_NPC_COMPOSE(bx = al, es = arena_seg, si = under_hero);   /* 6A02..6A0B */
    }
    si = ((hero_flags & 1) ? hero_frames_left : hero_frames_right)[hero_anim];   /* 6A19..6A33 (6 bytes per frame) */
    GT_HERO_DRAW(si);                                   /* 6A35 */
}

/* ==== npc_adjacent @ 6A77 / npc_find @ 6A94 ============================== */
u8 npc_adjacent(struct npc *si)                         /* AL = 3 - i for the first i in 0..2 with around_hero[i] == NPC_MARK && col match */
{   dx = hero_col_m1; for (cl = 3; cl; cl--, dx++) if (around_hero[3 - cl] == NPC_MARK && si->col == dx) return cl; return 0; }
struct npc *npc_find(u16 col)                           /* 6A94: first record with col == DX (no end check) */
{   for (si = MAP_NPCS; si->col != col; si++) ; return si; }

/* ==== load_tileset_and_paint_backdrop @ 6AA2 / paint_backdrop @ 6AA5 ===== */
void load_tileset_and_paint_backdrop(void) { load_tileset(); paint_backdrop(); }        /* 6AA2 -> 6D9E */
void paint_backdrop(void) { al = video_mode /* FF14 */; call far [0x6AE9]; }           /* 6AA5..6AAD: (BASE+2000):3300 = ympd/ckpd */

/* ==== load_backdrop_module @ 6AAF — YMPD.BIN (surface) / CKPD.BIN (underground) */
void load_backdrop_module(void)
{
    si = req_ympd_ckpd[town_flags & 1];                 /* 6AAF..6ABA */
    *(u16 *)0x6AEB = es = BASE + 0x2000;                /* 6ABE..6AC6: segment of the far pointer at 6AE9 (offset 0x3300) */
    KRN_LOAD(al = 3, di = 0x3300);                      /* 6AC8..6ACD: raw code module, far-called with AL = video mode */
}

/* ==== apply_patches @ 6AED — conditional pokes from the map (MAP_PATCHES) = */
/* {u16 flag_ptr, u8 mask, {u16 ptr, u8 val}* 0xFFFF}* 0xFFFF: when
 * [flag_ptr] & mask, write every byte.  Used to swap NPC scripts, open ways
 * and move the NPC table pointer once a story flag is set (docs/TOWN.md). */
void apply_patches(void)
{
    si = MAP_PATCHES;                                   /* 6AED */
    while ((bx = lodsw()) != 0xFFFF) {                  /* 6AF1..6AFA */
        mask = lodsb();                                 /* 6AFB */
        if (*bx & mask) { while ((bx = lodsw()) != 0xFFFF) *bx = lodsb(); }   /* 6AFC..6B18 */
        else           { while (lodsw() != 0xFFFF) si++; }                    /* 6B00..6B08 */
    }
}

/* ==== npc_update @ 6B1C — behaviours, then re-mark ======================= */
void npc_update(void)
{
    npc_restore_tiles();                                /* 6B1C -> 6C4E */
    for (si = MAP_NPCS; (dx = si->col) != 0xFFFF; si++) {   /* 6B1F..6B3F */
        dx = npc_behaviours[si->type](si, dx);          /* 6B2D..6B38: table 6B41 */
        si->col = dx;                                   /* 6B3A */
    }
    npc_place_markers();                                /* 6B2A -> 6C2B */
}
/* table @6B41: 0 face_hero 1 walk2 2 walk4 3 face_hero_static 4 idle_anim 5 wander2 6 wander4 7 static */
u16 npc_face_hero(si, dx)      /* 6B51 */ { si->sprite |= 0x80; if (hero_map_col() >= dx) si->sprite &= 0x7F; return npc_idle_anim(si, dx); }
u16 npc_walk2(si, dx)          /* 6B6C */ { si->anim += 0x10; if (si->anim & 0x10) return dx;            /* every 2nd frame */
                                            si->anim = (si->anim & 0x30) | ((si->anim + 1) & 0x0F);       /* 6B7B..6B82: frame counter */
                                            if (si->sprite & 0x80) { dx--; if (MAP_NPC_RANGE[0] >= dx) si->sprite &= 0x7F; }   /* 6B85..6B99: turn at min */
                                            else                   { dx++; if (MAP_NPC_RANGE[1] <= dx) si->sprite |= 0x80; }   /* 6B9A..6BA5: turn at max */
                                            return dx; }
u16 npc_walk4(si, dx)          /* 6BA6 */ { si->anim += 0x10; if (si->anim & 0x30) return dx; goto 6B7B; }   /* every 4th frame */
u16 npc_face_hero_static(si,dx)/* 6BB7 */ { si->sprite |= 0x80; if (hero_map_col() >= dx) si->sprite &= 0x7F; return dx; }
u16 npc_idle_anim(si, dx)      /* 6BD2 */ { si->anim += 0x10; if (si->anim & 0x30) return dx; si->anim ^= 1 /* &1 */; return dx; }   /* 2-frame idle */
u16 npc_wander2(si, dx)        /* 6BEC */ { si->anim += 0x10; if (si->anim & 0x10) return dx;
                                            si->anim = (si->anim & 0x30) | ((si->anim + 1) & 0x0F);       /* 6BFB..6C02 */
                                            if (!(si->anim & 7)) { si->sprite ^= 0x80; return dx; }       /* 6C05..6C0E: turn every 8 steps */
                                            return (si->sprite & 0x80) ? dx - 1 : dx + 1; }               /* 6C0F..6C18 */
u16 npc_wander4(si, dx)        /* 6C19 */ { si->anim += 0x10; if (si->anim & 0x30) return dx; goto 6BFB; }
u16 npc_static(si, dx)         /* 6C2A */ { return dx; }

/* ==== npc_place_markers @ 6C2B / npc_restore_tiles @ 6C4E ================ */
void npc_place_markers(void)  { for (si = MAP_NPCS; si->col != 0xFFFF; si++) { si->saved = MAP_CELL(si->col, NPC_ROW); MAP_CELL(si->col, NPC_ROW) = NPC_MARK; } }   /* 6C2B..6C4C */
void npc_restore_tiles(void)  { for (si = MAP_NPCS; si->col != 0xFFFF; si++) if (si->saved != NPC_MARK) MAP_CELL(si->col, NPC_ROW) = si->saved; }              /* 6C4E..6C70 */

/* ==== draw_hud_labels @ 6C72 ============================================= */
void draw_hud_labels(void) { VID_LABEL_HUD(lbl_life); VID_LABEL_HUD(lbl_almas); VID_LABEL_HUD(lbl_gold); VID_LABEL_HUD(lbl_place); }   /* 6C72..6C8D */

/* ==== check_edge_exit @ 6CB5 — hero walked off the left (0xFF) / right (0x1C) edge */
void check_edge_exit(void)
{
    if (hero_scr_col == 0xFF) {                         /* 6CB5..6CBA */
        npc_restore_tiles(); tick = 0x28; frame_tail(); /* 6CBC..6CC4 */
        for (si = MAP_EXITS; !(si->flags & 1); si++) ;  /* 6CC7..6CD3: the left-edge record */
        ah = si->flags; al = si->dest;                  /* 6CD5..6CD8 */
        if (ah & 0xFE) goto_cavern(al);                 /* 6CD9..6CDE -> 6FF8 (cavern-entry index) */
        change_town_map(al, si->gfx, si->tileset);      /* 6CE1 -> 6D30 */
        hero_scr_col = 0x1A; scroll_col = MAP_WIDTH - 0x24;   /* 6CE4..6CEF: appear at the right end */
        goto town_restart_60B7;                         /* 6CF2 */
    }
    if (hero_scr_col != 0x1C) return;                   /* 6CF5..6CF9 */
    npc_restore_tiles(); tick = 0x28; frame_tail();     /* 6CFA..6D02 */
    for (si = MAP_EXITS; (si->flags & 1); si++) ;       /* 6D05..6D11: the right-edge record */
    ah = si->flags; al = si->dest;
    if (ah & 0xFE) goto_cavern(al);                     /* 6D17..6D1C */
    change_town_map(al, ...);                           /* 6D1F */
    hero_scr_col = 0; scroll_col = 0;                   /* 6D22..6D2D */
    goto town_restart_60B7;
}

/* ==== change_town_map @ 6D30 — AL = town number, SI -> {gfx, tileset} ==== */
void change_town_map(u8 al)
{
    cur_map = al | 0x80;                                /* 6D30..6D32 */
    ax = lodsw();  /* al = gfx, ah = tileset */         /* 6D35 */
    KRN_LOAD(al = 1, ah = cur_map);                     /* 6D37..6D3D: town map raw -> C000 (music is NOT reloaded) */
    KRN_LOAD(al = 2, si = req_npc_gfx[gfx], es:di = arena:0x4000);   /* 6D44..6D58: mman/cman */
    GT_CONVERT_SPRITES(ds:si = arena:0x4100, es:di = (BASE+2000):0x7000, cx = 0xA0);   /* 6D5D..6D73 */
    if (ah != tileset) { tileset = ah; load_tileset(); }   /* 6D7A..6D84 */
}

/* ==== load_tileset @ 6D9E — cpat/mpat/dpat -> arena:8000, build the bank == */
void load_tileset(void)
{
    si = req_tileset[tileset];                          /* 6D9E..6DA7 (11 * tileset + 6DCE) */
    KRN_LOAD(al = 2, es:di = arena:0x8000);             /* 6DA9..6DB3 */
    *(u16 *)0x8000 += 0x8000; *(u16 *)0x8002 += 0x8000; *(u16 *)0x8004 += 0x8000;   /* 6DB8..6DC3: {types, block list, anim pairs} */
    jmp GT_BUILD_TILEBANK;                              /* 6DC9 */
}

/* ==== town_init_hero_gfx @ 6DEF — TMAN.GRP (46 cells) -> arena:6000 ====== */
void town_init_hero_gfx(void)
{
    KRN_LOAD(al = 2, si = req_tman, es:di = arena:0x6000);          /* 6DEF..6DFC */
    GT_CONVERT_SPRITES(ds:si = arena:0x6000, es:di = (BASE+2000):0x8000, cx = 0x2E);   /* 6E01..6E17 */
}

/* ==== enter_door @ 6E29 — Up in front of a door ========================== */
void enter_door(void)
{
    hero_anim |= 1;                                     /* 6E29 (or byte [E7],1) */
    ax = scroll_col + hero_scr_col + 4;                 /* 6E2E..6E39 */
    for (si = MAP_DOORS; si->col != 0xFFFF; si++)       /* 6E3C..6E59 */
        if (si->col == ax || si->col == ax + 1 || si->col == ax - 1) goto found;
    return;
found:
    hero_anim = 4;                                      /* 6E5B: back view */
    npc_restore_tiles(); tick = 0x28; frame_tail();     /* 6E61..6E69 */
    al = si->dest;                                      /* 6E6D */
    if (al == 0xFF) doorway_to_the_past();              /* 6E70..6E74 -> 6F77 */
    if (al >= 8) goto_cavern(al - 8);                   /* 6E77..6E7B -> 6FF8 */
    run_shop(al);                                       /* 6E7E */
}

/* ==== run_shop @ 6E7E — load shop overlay AL to A000 and call it ========= */
void run_shop(u8 al)
{
    music_fade = 4;                                     /* 6E7E */
    KRN_LOAD(al = 3, si = req_shop[al], es:di = BASE:0xA000);   /* 6E83..6E96 (14 bytes per record) */
    VID_DISSOLVE();                                     /* 6E9B */
    int60(AX = 1);                                      /* 6EA0: music stop */
    shop_active = 0xFF;                                 /* 6EA5 */
    SHOP_ENTRY();                                       /* 6EAA: [A000]; the shop uses [6004]..[601A] and returns with RET */
shop_return:                                            /* 6EAF: also the return address pushed for the death path */
    VID_CLEAR_PLAYFIELD();                              /* 6EAF */
    shop_active = 0;                                    /* 6EB4 */
    VID_ENEMY_TROUGH(); draw_hud_labels();              /* 6EB9, 6EBE */
    VID_LABEL_TEXT(MAP_LABEL);                          /* 6EC1..6EC5 */
    load_tileset_and_paint_backdrop();                  /* 6ECA (the shop overwrote arena:8000 with its portrait) */
    GT_CAPTURE_BACKDROP();                              /* 6ECD */
    memset(screen_copy, 0xFE, 0xE0);                    /* 6ED2..6EDC */
    apply_patches();                                    /* 6EDE: shops set story flags */
    tick = 0x28; frame_tail();                          /* 6EE1..6EE6 */
    btn1_edge = btn2_edge = 0;                          /* 6EE9, 6EEE */
    hero_anim = 1;                                      /* 6EF3 */
    int60(AX = 0, DS:SI = arena:0x3000);                /* 6EF8..6F05: music restart */
}

/* ==== doorway_to_the_past @ 6F77 — Pureza's green door: back to Dorado === */
void doorway_to_the_past(void)
{
    hero_anim = 4; frame_tail();                        /* 6F77, 6F7C */
    if (!(flag_past_door & 0x80)) {                     /* 6F7F: first time: "Fooled again! ... Taste the past" */
        dlg_forced = 0xFF;
        dialogue_run(script = 0, pos = 0x0918);         /* 6F8B..6F90 -> 63CA (skips the hero_anim|=1) */
        dlg_forced = 0; flag_past_door |= 0x80;         /* 6F93, 6F98 */
    }
    music_fade = 4;                                     /* 6F9D */
    cur_map = 0x86;                                     /* 6FA2..6FA4: DRMP.MDT = Dorado Town */
    KRN_LOAD(al = 1, ah = 0x86);                        /* 6FA8..6FAA */
    KRN_LOAD(al = 2, si = req_npc_gfx[0] /* MMAN */, es:di = arena:0x4000);   /* 6FAF..6FBC */
    while (!music_faded) ;                              /* 6FC1..6FC6 (FF26) */
    KRN_LOAD(al = 5, si = req_ugm2, es:di = arena:0x3000);   /* 6FC8..6FD5: UGM2.MSD score */
    scroll_col = 0x84; hero_scr_col = 0x0D;             /* 6FDA, 6FE0: appear at map column 0x95 */
    VID_DISSOLVE();                                     /* 6FE5 */
    goto town_entry_boot;                               /* 6FEA -> 601E */
}

/* ==== goto_cavern @ 6FF8 — AL = MAP_CAVES index: hand over to fight.bin == */
void goto_cavern(u8 al)
{
    si = MAP_CAVES + al * 5;                            /* 6FF8..7000 */
    col = lodsw();                                      /* 7002 */
    scroll_row = (lodsb() - 10) & 0x3F;                 /* 7004..7009: hero screen row 10 */
    door_side = (lodsb() & 1) ? 0xFF : 0;               /* 700C..7011 */
    cur_map = lodsb();                                  /* 7014..7015 */
    KRN_LOAD(al = 1, ah = cur_map);                     /* 7018..701C: cavern map raw -> C000 */
    ax = col - 16; if (ax < 0) ax += MAP_WIDTH /* of the cavern map now at C002 */;   /* 7021..7027 */
    scroll_col = ax;                                    /* 702B: hero_scr_col stays (screen column of the hero) */
    flag_entering_cavern = 0xFF;                        /* 702E: [0x06] */
    VID_DISSOLVE();                                     /* 7033 */
    bx = 0x6002; al = 0;                                /* 7038..703B */
    jmp KRN_LOAD;                                       /* 703D: mode 0: swap BASE:3000..9FFF with the parked gf*+fight set,
                                                                 then jmp [6002] = fight.bin entry 1.  fight.bin comes back
                                                                 with the same swap and jmp [6000] (fight 72D9, FF4B == 8). */
}

/* ==== idle_poll @ 7042 = [6016] — used by every shop/menu wait loop ====== */
void idle_poll(void)
{
    KRN_IDLE_0(); KRN_IDLE_1();                         /* 7044, 7049 (SI/DI preserved) */
    if (KRN_RESTORE_QUERY()) restore_game();            /* 704E..7055 */
    if (shop_active) SHOP_HOOK();                       /* 705A..7064: [A002] portrait animation etc. */
}

/* ==== shop_print_text @ 706C = [6004] — the shop text-box printer ======== */
/* Text at [FF4C].  Box interior x 56.., y 99 + line*10, 4 visible lines,
 * wrap at 0xD0 px.  Returns AL = 0 on a 0x00 byte (lip-sync request), or the
 * byte following 0xFF (shop action opcode; 0xFF 0xFF = end). */
u8 shop_print_text(void)
{
    si = shop_text_ptr;                                 /* 706C */
    if (shop_text_x + word_width_shop(si) >= 0xD0) shop_newline();   /* 7070..7081 -> 7224 / 7169 */
    for (;;) {
        tick = 0; do idle_poll(); while (tick < 6);     /* 7084..7091: ~25 ms per character */
        al = *shop_text_ptr++;                          /* 7093..7098 */
        switch (al) {
        case 0x2F: case 0x0D: shop_newline(); continue;             /* 709C..70A7 -> 7163 */
        case 0x0C: shop_text_x = shop_text_line = shop_line_count = 0;             /* 70AA -> 7205 */
                   VID_WINDOW(al = 0xFF, bx = 0x0D60, cx = 0x3637); continue;      /* 7214..721C: clear the box */
        case 0x0F: shop_wait_key_and_clear(); continue;             /* 70B1 -> 71B0 -> 71BC */
        case 0x11: shop_wait_key(); continue;                       /* 70B8 -> 71B6 -> 71DF */
        case 0x13: shop_text_mute = 0xFF; continue;                 /* 70BF..70C8 */
        case 0x15: shop_text_mute = 0; continue;                    /* 70CA..70D3 */
        case 0xFF: al = *shop_text_ptr++; return al;                /* 70D5..70DE */
        case 0x00: return 0;                                        /* 70DF..70E3 */
        }
        if (shop_text_x >= 0xD0) shop_newline();        /* 70E5..70EC */
        VID_PUTCHAR(al, ah = 1, bx = shop_text_x - font_xoff[al - 0x20] + 0x38, cl = shop_text_line * 10 + 0x63);   /* 70EF..711B */
        shop_text_x += font_advance[al - 0x20];         /* 7121..7131 */
        if (!shop_text_mute && al != ' ') sfx_request = 5;   /* 7135..7140 */
        if (al == ' ' && shop_text_x + word_width_shop(shop_text_ptr) >= 0xD0) shop_newline();   /* 7148..715D */
    }
}
void shop_newline(void)                                 /* 7169 */
{
    shop_text_x = 0; shop_line_count++; shop_text_line++;   /* 7169..7172 */
    if (shop_line_count < 4) return;                    /* 7176 */
    cx = shop_lines_pending();                          /* 717D -> 7269: lines left in the current text */
    shop_scroll_if_needed();                            /* 7181 -> 718E: line 5 -> scroll the 4 lines up 10 px (VID_SCROLL_UP_1 x10, bx=0x0762 cx=0x1A32) */
    if (cx >= 2) shop_wait_key_and_clear();             /* 7185..718A -> 71BC */
}
void shop_wait_key_and_clear(void)                      /* 71BC */
{
    VID_PUTCHAR(ax = 0x027C, bx = 0x9C, cl = 0x8B);     /* 71BC..71C4: red '|' page marker at (156,139) */
    shop_wait_key();                                    /* 71C9 -> 71DF */
    VID_WINDOW(al = 0, bx = 0x278B, cx = 0x020A);       /* 71CC..71D4: erase the marker */
    shop_line_count = 0;                                /* 71D9 */
}
void shop_wait_key(void)                                /* 71DF: Space or Alt; sfx 0x1D */
{   btn1_edge = btn2_edge = 0; do idle_poll(); while (!(btn1_edge | btn2_edge)); btn1_edge = btn2_edge = 0; sfx_request = 0x1D; }
u16 word_width_shop(u8 *si)                             /* 7224: like word_width, also stops at 0, 0xFF, 0x0D, 0x0C; a lone '.'/',' counts 0 */
u16 shop_lines_pending(void)                            /* 7269: lines the rest of the text (to FF FF / 0 / 0x0C) will take, wrapping at 0xD0 */

/* ==== format_number @ 72C7 = [6006] — DL:AX -> ASCII at ES:DI ============ */
/* Digits by repeated subtraction of 1e6/1e5/1e4 on the 24-bit value (7317) then
 * div by 1000/100/10 (7335); leading zeros stripped; 0xFF terminator. */
void format_number(u32 dlax, u8 *di);

/* ==== menu_select @ 7344 = [6010] ======================================== */
/* BL = cursor row.  Up/Down move the red arrow (cursor_up_anim / cursor_down_anim,
 * 10 x 1-px steps) or scroll the list (GT_MENU_SCROLL_UP/DOWN with the next item
 * composed by GT_MENU_LINE from menu_item_ids[menu_scroll + row]).
 * Returns CF=0 + BL on Space (sfx 0x1F), CF=1 on Alt. */
bool menu_select(u8 bl)
{
    btn1_edge = btn2_edge = 0;                          /* 7344, 7349 */
    cursor_draw(bl);                                    /* 734F -> 7469 */
    for (;;) {
        idle_poll(); tick = 0;                          /* 7354, 7358 */
        if (btn2_edge) return CF = 1;                   /* 735D..7365 */
        if (btn1_edge) { sfx_request = 0x1F; return CF = 0; }   /* 7366..7373 */
        al = int61().dirs & 3;                          /* 7378..737A */
        if (al == 1) {                                  /* up */
            if (bl) { cursor_up_anim(bl); bl--; continue; }          /* 7380..738B */
            if (!menu_scroll) continue;                              /* 738C */
            menu_scroll--;                                           /* 7397 */
            GT_MENU_LINE(al = menu_item_ids[menu_scroll + bl]);      /* 739B..73A4 */
            for (cx = 10; cx; cx--) {                                /* 73A9..73E3: reveal the line 1 px per frame */
                GT_MENU_SCROLL_DOWN(bx = menu_pos + 0x0301, al = cx - 1, cl = menu_visible_rows * 10 - 2, ch = menu_width_x4);
                do idle_poll(); while (tick < 4); tick = 0;
            }
        } else if (al == 2) {                           /* 73E9: down */
            if (bl < menu_visible_rows - 1) { cursor_down_anim(bl); bl++; continue; }   /* 73EE..73FE */
            if (bl + menu_scroll + 1 >= menu_total) continue;        /* 73FF..7411 */
            menu_scroll++;                                           /* 7415 */
            GT_MENU_LINE(al = menu_item_ids[menu_scroll + bl]);      /* 7419..7422 */
            for (cx = 10; cx; cx--) GT_MENU_SCROLL_UP(bx = menu_pos + 0x0301, al = 10 - cx, ...);   /* 7427..7463 */
        }
    }
}
void cursor_draw(u8 bl)      /* 7469 = [6014] */ { GT_CURSOR(bx = bl * 10 + menu_pos + 0x0100); }
void cursor_up_anim(u8 bl)   /* 747B = [6018] */ { bx = bl * 10 + menu_pos + 0x100; for (10 frames of 4 ticks) GT_CURSOR(--bx); }
void cursor_down_anim(u8 bl) /* 74A7 = [601A] */ { same with ++bx }

/* ==== yes_no_prompt @ 74D3 = [6008] — CF=0 Yes, CF=1 No/cancel =========== */
bool yes_no_prompt(void)
{
    save (menu_visible_rows, menu_total, menu_scroll);  /* 74D3..74DE */
    menu_visible_rows = menu_total = 2;                 /* 74DF, 74E4 */
    menu_draw_items(cx = 2, si = str_yes_no);           /* 74E9..74EF */
    menu_scroll = 0;                                    /* 74F2 */
    cf = menu_select(bl = 0); if (cf) bl = 1;           /* 74F7..74FE */
    restore;                                            /* 7500..7508 */
    return bl != 0;                                     /* 750C..7512 */
}
void menu_draw_items(u16 cx, char *si)                  /* 751A = [600E] */
{   for (dl = 0; cx; cx--, dl++) VID_LABEL_ASCIIZ(si, bx = dl * 10 + menu_pos + 0x0301, cl = 0);  /* si advances past each NUL */ }
void menu_draw_icons(u8 al, u8 ah, u16 cx)              /* 7539 = [6012]: rows ah.., items menu_item_ids[al..] */
{   for (; cx; cx--, al++, ah++) { GT_MENU_LINE(al = menu_item_ids[al]); GT_MENU_BLIT(bx = ah * 10 + menu_pos + 0x0300); } }

/* ==== gold_can_pay @ 7570 = [600A] / gold_add @ 7589 = [600C] ============ */
/* DL:AX = 24-bit amount.  gold_can_pay returns DL:AX = gold - amount with
 * CF=1 when the player cannot pay (nothing stored); gold_add stores gold + DL:AX. */
bool gold_can_pay(u32 dlax) { bl = gold_hi - dl; if (borrow) return CF = 1; ax = gold - ax; if (borrow) { if (bl == 0) return CF = 1; bl--; } dl = bl; return CF = 0; }
void gold_add(u32 dlax)     { gold += ax; gold_hi += dl + carry; }

/* ==== restore_game @ 7592 = [601C] — F7 "Restore Game" ==================== */
/* Also used by the sage (kenjpro) for loading.  Loads NAME.USR (a raw image of
 * the STDPLY page, BASE:0000..) or STDPLY.BIN for "Re-Start", then restarts
 * GAME.BIN with AX = 0xFFFF (GAME.BIN A000 stores AX at A474; A078 skips the
 * opening demo when it is -1 and rebuilds everything from the player record). */
void restore_game(void)
{
    int60(AX = 3, CL = 0xFF);                           /* 7592..7597: music pause */
    KRN_LOAD(al = 6, si = req_stdply);                  /* 759B..75A0: probe STDPLY.BIN (forces the disk-1 prompt) */
    menu_show_prices = 0;                               /* 75A5 */
    choose_save_file();                                 /* 75AA -> 7695: fills player_name, restart_chosen */
    if (restart_chosen) {                               /* 75AF */
        memset(player_name, 0, 8);                      /* 75B7..75BF */
        si = req_stdply;                                /* 75C1: new game = fresh STDPLY.BIN */
    } else {
        strcpy_until_nul(usr_name, player_name, 8); strcat(usr_name, ".USR");   /* 75C6..75EA */
        si = usr_request;                               /* 75EF: {0, 0, "NAME.USR"} */
        suppress_disk_prompt = 0xFF;                    /* 75F2 */
    }
    cf = KRN_LOAD(al = 3, es:di = BASE:0x0000);         /* 75F8..75FD: whole file over the STDPLY page */
    suppress_disk_prompt = 0;                           /* 7602 */
    if (cf) {                                           /* 7608 -> 762F: "User File / Not Found" */
        VID_WINDOW(al = 0xFF, bx = 0x1A46, cx = 0x1E1A); VID_PUTS(str_not_found, bx = 0x80, cl = 0x4C);   /* 762F..7646 */
        btn1_edge = 0; do KRN_IDLE_0(); while (!btn1_edge); btn1_edge = 0;   /* 764B..765E */
        goto restore_game;                              /* 7664 */
    }
    KRN_LOAD(al = 3, si = req_game_bin, es:di = BASE:0xA000);   /* 760A..7612: GAME.BIN back over the shop slot */
    VID_CLEAR_SCREEN();                                 /* 7617 */
    int60(AX = 1); int60(AX = 3, CL = 0);               /* 761C..7626: stop, unpause */
    AX = 0xFFFF; jmp [0x7686] /* = A000 */;             /* 7628..762B: restart GAME.BIN in "restored" mode */
}

/* ==== choose_save_file @ 7695 — "Input name:" box + list of *.usr ======== */
void choose_save_file(void)
{
    KRN_FIND_FILES(es:di = BASE:0xE000, ds:dx = usr_filespec);   /* 7695..76A1: {count, ptr[255], name[255][9]} */
    if (++count == 0) count--;                          /* 76A6..76AD */
    memmove(E003, E001, 255 words); *(u16 *)0xE001 = 0x77BA;      /* 76AF..76C1: insert "Re-Start" as entry 0 */
    VID_WINDOW(al = 0xFF, bx = 0x0D38, cx = 0x3637); VID_WINDOW(al = 0xFF, bx = 0x0D38, cx = 0x2637);   /* 76C2..76D7 */
    memset(usr_name, '`', 8); usr_name[8] = 0xFF; name_len = 0;    /* 76DC..76EB: '`' = blank glyph */
    copy player_name (to NUL, max 8) into usr_name counting name_len; name_len_used = name_len;   /* 76F0..7708 */
    if (usr_name is all '`') memcpy(usr_name, str_restart, 8);     /* 770B..7723: no name yet -> "Re-Start" */
    VID_PUTS(str_input_name, bx = 0x3C, cl = 0x44);     /* 7725..772D: (60,68) */
    name_x = 0x60; name_y = 0x56;                       /* 7732, 7738: name box at (96,86) */
    menu_pos = 0x343B; menu_width_x4 = 0x0A;            /* 773D, 7743: list at x4 0x34 (208), y 59, 40 px wide */
    if (count == 0) { AX = 0xFFFF; jmp far [FF00]; }    /* 7749..774E -> 77A0: abort to the loader */
    draw up to 5 entries: GT_MENU_LINE(si = E001, al = i) + GT_MENU_BLIT(bx = i*10 + menu_pos + 0x300);   /* 7750..7761 -> 7807 */
    menu_total = count; menu_visible_rows = 5;          /* 7764..776D */
    name_entry_loop(si = E001);                         /* 7772 -> 7838 */
    memset(player_name, 0, 8);                          /* 7775..777F */
    if (name_len_used == 0) return CF = 1;              /* 7781..7789 */
    copy usr_name into player_name until 0xFF or '`';   /* 778A..779E */
}

/* ==== name_entry_loop @ 7838 ============================================= */
/* Type a name (letters via last_ascii, Backspace, Left/Right move the caret,
 * Enter accepts — an empty name becomes "Re-Start"), or pick a list entry
 * with Up/Down + Space (copied into the name box).  A '-' anywhere in the
 * name (as in "Re-Start") sets restart_chosen (77C3). */
void name_entry_loop(u8 **list)
{
    check_restart_name();                               /* 7838 -> 77C3 */
    text_entry_mode = 0xFF; last_ascii = 0; btn1_edge = btn2_edge = 0; menu_scroll = 0; menu_cursor = 0;   /* 783B..7854 */
    if (menu_total) cursor_draw(bl = 0);                /* 785B..7862 */
    draw_name_box(); draw_caret(0);                     /* 7867 -> 7B1A, 786C -> 7AAD */
    for (;;) {
        tick = 0;                                       /* 786F */
        if (key_mask & 1) {                             /* 7874: Enter */
            if (usr_name is all '`') { memcpy(usr_name, str_restart, 8); check_restart_name(); draw_name_box(); }   /* 787D..789C */
            else { sfx_request = 0x1F; text_entry_mode = 0; btn2_edge = 0; return; }   /* 78AF..78B9 (+ wait for release) */
            sfx_request = 1; while (key_mask & 1) ; continue;   /* 789F..78AD */
        }
        if (btn1_edge) {                                /* 78BF: Space = take the highlighted list entry */
            sfx_request = 1;
            si = list[menu_scroll + menu_cursor];       /* 78CB..78D8 */
            memset(usr_name, '`', 8); copy si (to NUL) counting name_len; name_len_used = name_len;   /* 78DA..7903 */
            check_restart_name(); btn1_edge = 0;        /* 7907, 790A */
            VID_WINDOW(al = 0, bh = name_x / 4, bl = name_y, cx = 0x1010); draw_name_box(); draw_caret(0);   /* 790F..792B */
            continue;                                   /* 792E */
        }
        if (last_ascii) {                               /* 7935 */
            al = last_ascii; last_ascii = 0; sfx_request = 1;   /* 793C..7944 */
            if (al == 0x0D) return;                     /* 7949: Enter typed */
            if (al == 0x08) { name_backspace(); continue; }     /* 794E..7952 -> 7B44 */
            clear_restart_name();                       /* 7956 -> 77E5 */
            if (usr_name[name_len] == '`') name_len_used++;     /* 795A..7967 */
            usr_name[name_len] = al;                    /* 796B */
            draw_name_box(); sfx_request = 1; draw_caret(+1); continue;   /* 796F..7979 */
        }
        al = int61().dirs;                              /* 797C */
        if (al & 8) { sfx_request = 1; draw_caret(+1); wait release; last_ascii = 0; continue; }   /* 797E..7997 */
        if (al & 4) { sfx_request = 1; draw_caret(-1); wait release; last_ascii = 0; continue; }   /* 7998..79B1 */
        if (!menu_total) continue;                      /* 79B2 */
        if ((al & 3) == 1) { up: cursor_up_anim / scroll list down (GT_MENU_LINE + GT_MENU_SCROLL_DOWN, 79C0..7A2A) }
        if ((al & 3) == 2) { down: cursor_down_anim / scroll list up (7A30..7AAC) }
    }
}
void check_restart_name(void) { if (memchr(usr_name, '-', 8)) { restart_chosen = 0xFF; name_len = 0; } }   /* 77C3..77E4 */
void clear_restart_name(void) { if (restart_chosen) { restart_chosen = 0; memset(usr_name, '`', 8); name_len_used = 0; } }   /* 77E5..7806 */
void draw_caret(s8 delta)                               /* 7AAD: erase the old caret, clamp name_len to 0..7 and <= name_len_used, draw char 0x7F colour 6 */
void draw_name_box(void)                                /* 7B1A: VID_WINDOW(al=0, name_x/4, name_y, 0x1008) then VID_PUTS(usr_name) at (name_x, name_y) */
void name_backspace(void)                               /* 7B44: shift the name left at the caret (min 1), pad '`', name_len_used-- */

/* ==== data tables ======================================================== */
/* 7B82: font_xoff[96]  — {0,2,2,3,1,0,0,2,2,3,1,1,1,2,2,0,1,2,1,1,1,1,1,1,1,1,3,2,1,1,2,1, 0,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0, 0,0,0,0,1,0,0,0,0,0,1,2,2,2,1,1, 1,0,0,1,0,1,1,0,0,2,1,0,2,0,1,1, 0,0,0,1,1,0,0,0,1,1,1,2,0,3,1,0}
 * 7BE2: font_advance[96] — {5,4,4,4,6,8,5,3,4,4,6,6,6,5,6,8, 7,5,7,7,7,7,7,7,7,7,3,4,6,6,6,7, 8,8,8,8,8,8,8,8,8,5,8,8,8,8,8,8, 8,8,8,8,7,8,8,8,8,8,7,5,3,5,6,7, 7,8,8,7,8,7,7,8,8,5,6,8,5,8,7,7, 8,8,8,7,6,8,8,8,7,7,7,4,8,4,7,8}
 * 7C42..7C7C: locals (above), zero in the file. */
