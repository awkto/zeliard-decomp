/*
 * shops.c — hand-cleaned decompilation of the eight town shop overlays
 * (ZELRES2[10..17]): KINGPRO (king/castle), OMOYPRO (princess chamber),
 * ARMRPRO (weapon and armour shop), BANKPRO (bank), CHURPRO (church),
 * DRUGPRO (witchcraft implement shop), INNAPRO (inn), KENJPRO (sage: level-up,
 * spells, save game).  Companion: docs/TOWN.md; the caller is src/town.c
 * (run_shop @6E7E, the [6004]..[601C] vectors and shop_print_text).
 *
 * NOT COMPILABLE — pseudo-C from the ndisasm listings (origin A000; the
 * Ghidra dumps are stack-mangled).  Every routine carries its original
 * address; constants cite the instruction they come from.
 *
 * All eight are raw slot-B overlays loaded to BASE:A000 by town.bin run_shop
 * (6E7E, request table 6F07, indexed by the door's dest byte):
 *   dest 0 kingpro  1 omoypro  2 kenjpro  3 armrpro  4 drugpro  5 churpro  6 bankpro  7 innapro
 * town.bin dissolves the screen, stops the music, sets shop_active (7C42) and
 * `call [A000]`; the overlay returns with RET and town.bin redraws (6EAF).
 * [A002] is a per-frame hook run by town.bin idle_poll (7042) while a shop is
 * active (portrait blink / lip-sync animation); kenjpro also has [A004], the
 * "you died" entry town.bin jumps to at 61A0.
 *
 * Vectors used (names from docs/TOWN.md):
 *   KRN_LOAD [10C]  KRN_RANDOM [11A]  KRN_FIND_FILES [11C]  VID_* [20xx]
 *   GT_DRAW_CELL [3016] (portrait cells from arena:8000)  GT_MENU_* [301A..3022]
 *   town.bin: shop_print_text [6004], format_number [6006], yes_no_prompt [6008],
 *   gold_can_pay [600A], gold_add [600C], menu_draw_items [600E], menu_select [6010],
 *   menu_draw_icons [6012], cursor_draw [6014], idle_poll [6016], cursor_up/down_anim
 *   [6018]/[601A], restore_game [601C].
 *
 * Shop script format (printed by shop_print_text, see town.c 706C): text bytes,
 * 0x2F / 0x0D newline, 0x0C clear box, 0x0F wait+clear, 0x11 wait key, 0x13/0x15
 * mute/unmute, 0x00 -> returns AL=0 (action 0, normally the lip-sync
 * animation), 0xFF nn -> returns AL=nn (shop action nn, per-shop jump table),
 * 0xFF 0xFF -> end of script.  Actions usually redirect [FF4C] to another
 * string — that is how the shops branch.
 *
 * Player-record fields the shops own (BASE:0000 page; see docs/TOWN.md):
 *   [85..87] gold (24-bit)   [88..8A] bank balance (24-bit)   [8B] almas
 *   [92] sword  [93] shield  [94] shield_hp  [96] shield_hp_max
 *   [A1..A5] items  [A6..AA] potion slots (id+1)  [AB..B1] magic counts  [B4..BA] magic max
 *   [BB..C1] spells learned (one byte per spell, kenjpro)  [C9..D1] drug stock mask per town
 *   [D2..DA] sword stock mask per town  [DB..E3] shield stock mask per town  [E5] sages met (bits)
 *   [05] king reward given  [06] entered a cavern  [24] bit1 crest traded  [49] Jashiin defeated
 */

/* ------------------------------------------------------------------------ */
/* Shared prologue — identical bytes in king/armr/bank/chur/drug/inna @A006  */
/* (kenjpro differs, omoypro differs slightly: see below)                    */
/* ------------------------------------------------------------------------ */
void shop_prologue(const struct req *portrait_req,   /* {01, res#, "NAME.GRP"} */
                   const u8 *place_label,            /* {x4, y, xoff, len, text} for VID_LABEL_TEXT */
                   void (*draw_portrait)(void))
{
    es = arena_seg;                                  /* A006 mov es,[cs:FF2C] */
    KRN_LOAD(2, portrait_req, arena:0x8000);         /* A008..A010: decompress the portrait bank */
    VID_CONVERT_CELLS(arena:0x8000, 0x100);          /* A015..A026: 256 cells -> native format */
    shop_text_x = 0;  shop_text_line = 0;            /* A027/A02C  [FF4E],[FF4F] */
    VID_CLEAR_PLAYFIELD();                           /* A031 [2002] */
    VID_ENEMY_TROUGH();                              /* A036 [2012]  (blue trough at the ENEMY line) */
    VID_LABEL_TEXT(place_label);                     /* A03B [2010]  "In the Castle" etc. on the PLACE line */
    draw_portrait();                                 /* A043 */
    VID_WINDOW(0xFF, x4=0x0D, y=0x60, w4=0x36, h=0x37);  /* A046..A04E: the 4-line text box (52,96) 216x55 */
    /* then per shop: choose script, [FF4C] = script, print loop: */
    for (;;) {
        u8 op = shop_print_text();                   /* [6004] */
        if (op == 0xFF) break;                       /* FF FF = end of script */
        shop_action(op);                             /* jump table at A078 (per shop) */
    }
    VID_DISSOLVE();                                  /* [2040] then ret to town.bin 6EAF */
}
/* Portrait drawing (same helper in every shop, only table/size/position differ):
 *   for row in rows: for col in cols: GT_DRAW_CELL(al = map[row][col], bh = x4 + col, bl = y + 8*row)
 * i.e. `mov bx,YYXX ; loop: lodsb ; call [3016] ; inc bh ; ... ; sub bh,cols ; add bl,8` */

/* ======================================================================== */
/* KINGPRO.BIN — ZELRES2[10] (res# 0x0B), 1954 bytes: King of Felishika     */
/* door dest 0 (cmap col 52)                                                */
/* ======================================================================== */
/* vectors: [A000] = A004 king_main, [A002] = A302 king_hook */
static const struct req king_portrait_req = /* A40F */ {1, 0x13, "KING.GRP"};      /* ZELRES2[18] */
static const u8 king_label[] = /* A41A */ {0x13, 0xAF, 0x00, 0x11, "King of Felishika"};

/* locals */
u8 king_mouth_idle;     /* A79D  != 0: mouth open/close animation runs (action 4 on, 5 off) */
u8 king_mouth_tick;     /* A79E  0..5 */
u8 king_mouth_phase;    /* A79F  bit0 = open */
u8 king_blink_on;       /* A7A0  != 0: blink animation runs (action 3) */
u8 king_blink_idx;      /* A7A1  index into king_blink_seq, 0xFF = restart */

/* king_main @A004: shop_prologue, then the script chosen by king_pick_script */
void king_main(void)
{
    shop_prologue(&king_portrait_req, king_label, king_draw_portrait);   /* A006..A04E */
    shop_text_ptr = king_pick_script();                                  /* A053 call A3E8; A056 [FF4C]=si */
    for (;;) {                                                           /* A05A */
        u8 op = shop_print_text();
        if (op == 0xFF) break;
        king_action(op);                                                 /* A06D: jmp [A078 + op*2] */
    }
    VID_DISSOLVE();                                                      /* A068 */
}

/* king_action @A06D — jump table A078 */
static void (*const king_action_tab[6])(void) = /* A078 */ {
    king_act0_speak,      /* A0E4: 12-frame face animation (king_speak_seq) */
    king_act1_pay_gold,   /* A09A: +1000 gold in 10 steps, sets [0x05] */
    king_act2_wait150,    /* A0D4: wait 150 ticks (0x96), hook keeps animating */
    king_act3_blink_on,   /* A092: king_blink_on = FF, redraw mouth frame 1 */
    king_act4_mouth_on,   /* A084: king_mouth_idle = FF */
    king_act5_mouth_off,  /* A08A: king_mouth_idle = 0, draw closed mouth (frame 0) */
};

/* king_act1_pay_gold @A09A: the 1000-gold gift */
void king_act1_pay_gold(void)
{
    for (int i = 0; i < 10; i++) {                       /* A09A mov cx,0xA */
        gold += 100;                                     /* A09E..A0AE: [86] += 0x64, adc [85],0 (24-bit) */
        VID_NUM_GOLD();                                  /* A0B2 [2016] */
        sfx_request = 0x13;                              /* A0B7 [FF75] = 0x13 (coin tick) */
        tick = 0; while (tick < 15) king_hook();         /* A0BC..A0C9: 0x0F ticks per step */
    }
    king_reward_given = 0xFF;                            /* A0CE  mov byte [0x05],0xFF */
}

/* king_act2_wait150 @A0D4 */
void king_act2_wait150(void) { tick = 0; while (tick < 0x96) king_hook(); }

/* king_act0_speak @A0E4: 12 face frames, 25 ticks each */
static const u8 king_speak_seq[12] = /* A0F8 */ {0,0,1,2,2,1,0,3,4,4,5,6};
void king_act0_speak(void)
{
    for (int i = 0; i < 12; i++) {                       /* A0E7 mov cx,0xC */
        king_draw_face(king_speak_seq[i]);               /* A0ED call A142 */
        tick = 0; while (tick < 0x19) king_hook();       /* A0F0 call A104 */
    }
}

/* king_draw_portrait @A114: 8 rows x 12 cols at (x4 0x0E = 56 px, y 0x17 = 23); when the
 * game is won ([0x49] != 0) also draws face frame 6 (A138..A140). */
static const u8 king_portrait_map[8][12] = /* A16E */ {
    {0x00,0x01,0x02,0x03,0x3e,0x3f,0x40,0x41,0x18,0x19,0x1a,0x1b},
    {0x04,0x05,0x06,0x07,0x42,0x43,0x44,0x45,0x1c,0x1d,0x1e,0x1f},
    {0x08,0x09,0x0a,0x46,0x47,0x48,0x49,0x4a,0x4b,0x20,0x21,0x22},
    {0x0b,0x0c,0x0d,0x4c,0x4d,0x4e,0x4f,0x50,0x51,0x23,0x24,0x25},
    {0x0e,0x0f,0x10,0x52,0x53,0x54,0x55,0x56,0x57,0x26,0x27,0x28},
    {0x11,0x12,0x13,0x58,0x59,0x5a,0x5b,0x5c,0x29,0x2a,0x2b,0x2c},
    {0x14,0x15,0x16,0x17,0x5d,0x5e,0x5f,0x2d,0x2e,0x2f,0x30,0x31},
    {0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x3b,0x3c,0x3d},
};
/* king_draw_face @A142 (AL = frame 0..6): 6 rows x 7 cols at (x4 0x11 = 68 px, y 0x17):
 * the face area of the portrait; pointer table A1CE -> 42-byte maps */
static const u8 *const king_face_ptr[7] = /* A1CE */ {A1DC, A206, A230, A25A, A284, A2AE, A2D8};
static const u8 king_face_map[7][6][7] = {
 /* A1DC f0 closed */ {{0x03,0x3e,0x3f,0x40,0x41,0x18,0x07},{0x42,0x43,0x44,0x45,0x1c,0x46,0x47},{0x48,0x49,0x4a,0x4b,0x4c,0x4d,0x4e},
                       {0x4f,0x50,0x51,0x52,0x53,0x54,0x55},{0x56,0x57,0x58,0x59,0x5a,0x5b,0x5c},{0x29,0x17,0x5d,0x5e,0x5f,0x2d,0x2e}},
 /* A206 f1 */        {{0x03,0x3e,0x3f,0x40,0x41,0x18,0x07},{0x42,0x43,0x44,0x45,0x1c,0x46,0x47},{0x48,0x49,0x4a,0x4b,0x4c,0x4d,0x4e},
                       {0x4f,0x50,0x51,0x52,0x60,0x61,0x62},{0x56,0x57,0x58,0x59,0x5a,0x5b,0x5c},{0x29,0x17,0x5d,0x5e,0x5f,0x2d,0x2e}},
 /* A230 f2 */        {{0x03,0x3e,0x3f,0x40,0x41,0x18,0x07},{0x42,0x43,0x44,0x45,0x1c,0x46,0x47},{0x48,0x49,0x4a,0x4b,0x4c,0x4d,0x4e},
                       {0x4f,0x50,0x51,0x52,0x63,0x64,0x65},{0x56,0x57,0x58,0x59,0x5a,0x5b,0x5c},{0x29,0x17,0x5d,0x5e,0x5f,0x2d,0x2e}},
 /* A25A f3 */        {{0x03,0x66,0x67,0x68,0x69,0x18,0x07},{0x6a,0x6b,0x6c,0x6d,0x1c,0x6e,0x6f},{0x70,0x71,0x72,0x73,0x74,0x75,0x76},
                       {0x77,0x78,0x79,0x7a,0x7b,0x7c,0x7d},{0x7e,0x7f,0x80,0x81,0x82,0x83,0x84},{0x29,0x17,0x85,0x86,0x87,0x2d,0x2e}},
 /* A284 f4 */        {{0x03,0x88,0x89,0x8a,0x8b,0x18,0x07},{0x8c,0x8d,0x8e,0x8f,0x1c,0x90,0x91},{0x92,0x93,0x94,0x95,0x96,0xad,0xab},
                       {0xae,0x9a,0x9b,0x9c,0x9d,0x9e,0x9f},{0xa0,0xa1,0xa2,0xa3,0xa4,0xa5,0xa6},{0x29,0x17,0xa7,0xa8,0xa9,0x2d,0x2e}},
 /* A2AE f5 */        {{0x03,0x88,0x89,0x8a,0x8b,0x18,0x07},{0x8c,0x8d,0x8e,0x8f,0x1c,0x90,0x91},{0x92,0x93,0x94,0x95,0x96,0xaa,0xab},
                       {0xac,0x9a,0x9b,0x9c,0x9d,0x9e,0x9f},{0xa0,0xa1,0xa2,0xa3,0xa4,0xa5,0xa6},{0x29,0x17,0xa7,0xa8,0xa9,0x2d,0x2e}},
 /* A2D8 f6 (won) */  {{0x03,0x88,0x89,0x8a,0x8b,0x18,0x07},{0x8c,0x8d,0x8e,0x8f,0x1c,0x90,0x91},{0x92,0x93,0x94,0x95,0x96,0x97,0x98},
                       {0x99,0x9a,0x9b,0x9c,0x9d,0x9e,0x9f},{0xa0,0xa1,0xa2,0xa3,0xa4,0xa5,0xa6},{0x29,0x17,0xa7,0xa8,0xa9,0x2d,0x2e}},
};

/* king_hook @A302 = [A002]: every 4 ticks of tick_total [FF50]: blink + mouth idle */
void king_hook(void)
{
    idle_poll();                                    /* callers call [6016] which calls us; A302 itself: */
    if (tick_total < 4) return;                     /* A302 cmp word [FF50],4 */
    tick_total = 0;
    king_blink_step();                              /* A310 call A315 */
    king_mouth_step();                              /* A313 jmp A386 */
}
/* king_blink_step @A315: 26-entry frame sequence, then a random pause */
static const u8 king_blink_seq[26] = /* A360 */ {0,0,0,0,0,0,1,1,1,1,1,2,2,2,2,2,1,1,1,1,1,0,0,0,0,0};
static const u8 king_eyes_map[3][4]  = /* A37A */ {{0x96,0x97,0x98,0x99},{0x96,0xaa,0xab,0xac},{0x96,0xad,0xab,0xae}};
void king_blink_step(void)
{
    if (!king_blink_on) return;                     /* A315 */
    if (++king_blink_idx >= 0x1A) {                 /* A31D..A326 */
        if (KRN_RANDOM() != 0) return;              /* A328 [11A]: 1/256 chance per 4 ticks to restart */
        king_blink_idx = 0xFF; return;              /* A332 */
    }
    /* A338..A35F: 1 row x 4 cells at (x4 0x11, y 0x2F) = the eyes */
    for (c = 0; c < 4; c++) GT_DRAW_CELL(king_eyes_map[king_blink_seq[king_blink_idx]][c], 0x11 + c, 0x2F);
}
/* king_mouth_step @A386: toggles the 2x5-cell mouth every 6 hook calls (24 ticks) */
static const u8 king_mouth_map[2][2][5] = /* A3D4 */ {
    {{0xa2,0xa3,0xa4,0xa5,0xa6},{0x17,0xa7,0xa8,0xa9,0x2d}},   /* closed */
    {{0xaf,0xb0,0xb1,0xb2,0xb3},{0x17,0xb4,0xb5,0xb6,0x2d}},   /* open   */
};
void king_mouth_step(void)
{
    if (!king_mouth_idle) return;                   /* A386 */
    if (++king_mouth_tick < 6) return;              /* A38E..A397 */
    king_mouth_tick = 0; king_mouth_phase++;        /* A39A..A39F */
    king_draw_mouth(king_mouth_phase & 1);          /* A3A6: 2 rows x 5 cols at (x4 0x11, y 0x3F) */
}
/* king_act3_blink_on @A092: king_blink_on = FF; king_draw_mouth(1)   (AL=0xFF & 1) */
/* king_act4_mouth_on @A084: king_mouth_idle = FF */
/* king_act5_mouth_off @A08A: king_mouth_idle = 0; king_draw_mouth(0) */

/* king_pick_script @A3E8 */
const u8 *king_pick_script(void)
{
    const u8 *s = king_script_first;                        /* A3E8 */
    if ((king_reward_given | entered_cavern) == 0) return s; /* A3EB: [0x05] | [0x06] */
    s = king_script_again;                                   /* A3F5 */
    if (!entered_cavern) return s;                           /* A3F8 [0x06] */
    s = king_script_after_cavern;                            /* A400 */
    if (!jashiin_defeated) return s;                         /* A403 [0x49] */
    return king_script_ending;                               /* A40B */
}
static const char king_script_first[] = /* A42F */
 "\x0c\xff\x00\xff\x03\xff\x04" "Brave Duke Garland, " "\xff\x05\xff\x02\xff\x04"
 "you\\ll need money for your journey./I&hereby bestow upon you 1000&Golds." "\xff\x05\xff\x02\xff\x01" "\x0d\xff\x04"
 "Go to town and outfit yourself, then make haste to the labyrinth to defeat the forces of Jashiin. "
 "My kingdom and the life of my daughter are at stake." "\xff\x05\x11\xff\xff";
static const char king_script_again[] = /* A53C */
 "\x0c\xff\x00\xff\x03\xff\x04" "Brave Duke, did you forget something?" "\xff\x05\xff\x02\x0d\xff\x04"
 "The entrance to the labyrinth is at the edge of town." "\xff\x05\x0d\xff\x04"
 "Please hurry, before it\\s too late! " "\xff\x05\x11\xff\xff";
static const char king_script_after_cavern[] = /* A5D2 */
 "\x0c\xff\x00\xff\x03\xff\x04" "Duke Garland, " "\xff\x05\xff\x02\xff\x04" "I am in debt to you for your efforts. "
 "\xff\x05\xff\x02\xff\x04" "Have you not yet succeeded in vanquishing Jashiin? " "\xff\x05\xff\x02\x0d\xff\x04"
 "I pray that the spirits will guide you. Please don\\t give up, the people of Zeliard are depending on you!"
 "\xff\x05\x11\xff\xff";
static const char king_script_ending[] = /* A6C1 */
 "\x0c\xff\x03\xff\x04" "Duke Garland, " "\xff\x05\xff\x02\xff\x04"
 "you are a brave man. You have conquered Jashiin and returned the nine Tears of Esmesanti. " "\xff\x05\xff\x02\x0d\xff\x04"
 "Now go quickly to the chamber of Princess Felicia. The&crystals will bring her back to life. " "\xff\x05\x11\xff\xff";

/* ======================================================================== */
/* OMOYPRO.BIN — ZELRES2[11] (res# 0x0C), 595 bytes: the Princess' chamber   */
/* ("omoya" = main house), door dest 1 (cmap col 95)                         */
/* ======================================================================== */
/* vectors: [A000] = A005 omoy_main, [A002] = A004 (= `ret` byte 0xC3 @A004: no hook) */
static const struct req omoy_portrait_req = /* A239 */ {1, 0x14, "OMOYA.GRP"};   /* ZELRES2[19] */
static const u8 omoy_label[] = /* A245 */ {0x16, 0xAF, 0x02, 0x0A, "In the Hut"};

/* omoy_main @A005: prologue variant (no text box, no script) */
void omoy_main(void)
{
    KRN_LOAD(2, &omoy_portrait_req, arena:0x8000);  VID_CONVERT_CELLS(arena:0x8000, 0x100);   /* A006..A027 */
    VID_CLEAR_PLAYFIELD(); VID_ENEMY_TROUGH(); VID_LABEL_TEXT(omoy_label);                     /* A028..A035 */
    omoy_draw_picture();                                                                       /* A03A call A104 */
    if (!jashiin_defeated) {                        /* A03D test byte [0x49] */
        btn1_edge = 0;                              /* A044 */
        do idle_poll(); while (!btn1_edge);         /* A049..A053: wait for the sword button */
        VID_DISSOLVE(); return;                     /* A055 jmp [2040] -> back to town.bin */
    }
    /* ---- ending: never returns to town.bin ---- */
    pop();                                          /* A05A drop town.bin's return address */
    KRN_LOAD(3, &omoy_enddemo_req, BASE:0x6000);    /* A061..A069: ENDDEMO.BIN (ZELRES2[50]) into slot A */
    KRN_LOAD(3, omoy_gd_req[video_mode], BASE:0x3000);  /* A072..A083: gd{ega,cga,hgc,mcga,tga}.bin renderer */
    tick_total = 0; while (tick_total < 300) ;      /* A088..A096: 300 ticks (~1.3 s) silence */
    GD_3006(bx = 0, cx = 0x50C8);                   /* A098..A09E: gd renderer vector [3006] (uncertain: palette/fade) */
    demo_palette = 0xFF;                            /* A0A3 [FF77] = FF */
    jmp [0x6000];                                   /* A0A9 enddemo.bin entry 0 */
}
static const struct req omoy_enddemo_req = /* A0AD */ {1, 0x33, "enddemo.bin"};
static const struct req *const omoy_gd_req[5] = /* A0BB */ {A0C5 /*gdega ZELRES1[1]*/, A0D3 /*gdcga [2]*/, A0D3 /*cga2 -> gdcga*/, A0DF /*gdhgc [3]*/, A0EB /*gdmcga [5]*/};
/* records @A0C5: {0,0x02,"gdega.bin"} {0,0x03,"gdcga.bin"} {0,0x04,"gdhgc.bin"} {0,0x06,"gdmcga.bin"} {0,0x05,"gdtga.bin"}
 * (note: the table has no entry for video mode 5/Tandy = A0F7 gdtga — index 4 is gdmcga; uncertain whether
 *  mode 5 is reachable here) */
/* omoy_draw_picture @A104: 16 rows x 17 cols at (x4 0x1E = 120 px, y 0x0C = 12) from omoy_picture_map */
static const u8 omoy_picture_map[16][17] = /* A129 */ {
    {0,0,0,0,0,0,0,0x01,0x02,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0x03,0x04,0x05,0x06,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0x07,0x08,0x09,0x0a,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0x0b,0x0c,0x0d,0x0e,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0x0f,0x10,0x11,0x12,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0x13,0x14,0x15,0x16,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0x17,0x18,0x19,0x1a,0,0,0,0,0,0,0},
    {0,0x1b,0x1c,0x1d,0x1e,0x1f,0x20,0x21,0x22,0x23,0,0,0,0,0,0,0},
    {0x24,0x25,0x26,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0,0,0,0,0,0,0},
    {0x2e,0x2f,0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0,0,0,0,0,0},
    {0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,0x40,0x41,0x42,0x43,0,0,0,0,0,0},
    {0x44,0x45,0x46,0x47,0,0x48,0x49,0x4a,0x4b,0x4c,0x4d,0,0,0,0,0,0},
    {0x4e,0x4f,0x50,0x51,0,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0,0,0,0,0},
    {0x59,0x5a,0x5b,0x5c,0x5d,0x5e,0x5f,0x60,0x61,0x62,0x63,0x64,0x65,0x66,0,0,0},
    {0x67,0x68,0x69,0x6a,0x6b,0x6c,0x6d,0x6e,0x6f,0x70,0x71,0x72,0x73,0x74,0x75,0,0},
    {0x76,0x77,0x78,0x79,0x7a,0x7b,0x7c,0x7d,0x7e,0x7f,0x80,0x81,0x82,0x83,0x84,0x85,0x86},
};  /* cell 0 = blank (GT_DRAW_CELL draws bank cell 0, which is empty in OMOYA.GRP — uncertain) */

/* ======================================================================== */
/* BANKPRO.BIN — ZELRES2[13] (res# 0x0E), 3384 bytes: the bank               */
/* door dest 6 (present in all 9 towns)                                     */
/* ======================================================================== */
/* vectors: [A000] = A004 bank_main, [A002] = A728 bank_hook */
static const struct req bank_portrait_req = /* A8E3 */ {1, 0x16, "BANK.GRP"};    /* ZELRES2[21] */
static const u8 bank_label[] = /* A8EE */ {0x18, 0xAF, 0x02, 0x08, "The Bank"};

/* Player record used: gold [85..87] (24-bit: u8 hi @85, u16 lo @86), almas [8B],
 * BANK BALANCE [88..8A] (24-bit: u8 hi @88, u16 lo @89) — new field. */
u8  bank_hi;  u16 bank_lo;      /* 0x88, 0x89 */

/* locals */
u8  bank_menu_cursor;           /* AD1E  remembered between menu visits */
const u8 *bank_anim_frames;     /* AD1F  A773 (talking) or A7C3 (delighted, deposit >= 1000) */
u8  bank_anim_on;               /* AD21  hook animates the banker while != 0 */
u8  bank_anim_tick;             /* AD22  frame parity */
u8  bank_did_business;          /* AD23  something exchanged/deposited/withdrawn/checked */
u8  bank_big_deposit;           /* AD24  set by action 3 (after a >= 1000 gold deposit) */
u8  bank_rate_almas;            /* AD25  almas per unit  } from bank_rates[map id] */
u8  bank_rate_gold;             /* AD26  gold per unit   } */
u8  bank_digit[2];              /* AD27  "d\xff" one-digit string printed via [6004] */
u8  bank_amt_hi; u16 bank_amt;  /* AD29, AD2A  amount being entered (24-bit) */
u8  bank_max_hi; u16 bank_max;  /* AD2C, AD2D  upper limit (gold carried / bank balance) */
u8  bank_repeat_delay;          /* AD2F  key auto-repeat delay, 0x23 ticks then decreasing to 1 */
u8  bank_numbuf[8];             /* AD30  format_number output */

/* Exchange rate table, indexed by map id [C006]-1: {almas in, gold out} per unit (A16C..A17D) */
static const u8 bank_rates[9][2] = /* A8FA */ {
    {1,6},  /* 1 Felishika Castle / Muralla */
    {1,6},  /* 2 Satono  */
    {1,8},  /* 3 Bosque  */
    {1,4},  /* 4 Helada  */
    {1,2},  /* 5 Tumba   */
    {1,4},  /* 6 Dorado  */
    {4,2},  /* 7 Llama   (4 almas -> 2 gold) */
    {1,6},  /* 8 Pureza  */
    {1,8},  /* 9 Esco    */
};
static const char bank_menu[] = /* A90C */ "Go outside\0Exchange almas\0Deposit money\0Withdraw money\0Check balance\0";
static const char bank_deposit_labels[]  = /* A951 */ "GOLD CARRIED\0\0\0 DEPOSIT AMT\0";   /* 4 rows: text, blank, blank, text */
static const char bank_withdraw_labels[] = /* A96D */ "GOLD IN BANK\0\0\0WITHDRAW AMT\0";

/* bank_main @A004 */
void bank_main(void)
{
    shop_prologue(&bank_portrait_req, bank_label, bank_draw_portrait);   /* A006..A053; A031: bank_menu_cursor = 0 */
    bank_anim_on = 0xFF; bank_anim_frames = A773;                        /* A058, A05D */
    shop_text_ptr = A989; shop_print_text();                             /* A063: "\x0c" (+ FF 2E returns AL=0x2E, ignored) */
    for (i = 0; i < 5; i++) {                                            /* A06E: banker is busy: prints "....." */
        tick = 0; shop_text_ptr = A98B; shop_print_text();               /* "." (FF 4F -> AL ignored) */
        do bank_hook(); while (tick < 0x3F);                             /* A082..A08A: 63 ticks per dot */
    }
    bank_anim_on = 0;                                                    /* A08F */
    shop_text_ptr = bank_script_greeting;                                /* A094 [FF4C] = A98D */
    for (;;) { u8 op = shop_print_text(); if (op == 0xFF) break; bank_action(op); }   /* A09A..A0A6 */
    VID_DISSOLVE();                                                      /* A0A8 */
}
static void (*const bank_action_tab[4])(void) = /* A0B8 */ {
    bank_act0_bow,        /* A0C0: wait 60 ticks, then frame sequence A82F (banker turns to the counter) */
    bank_act1_menu,       /* A0D2: the main menu */
    bank_act2_unbow,      /* A5F3: reverse sequence A839, then talking animation for 100 ticks */
    bank_act3_flag_big,   /* A619: bank_big_deposit = FF */
};
/* bank_act0_bow @A0C0 */
void bank_act0_bow(void)
{
    tick = 0; do bank_hook(); while (tick < 0x3C);         /* A0C2..A0C9 */
    bank_play_seq(bank_seq_bow);                           /* A0CC si=A82F; A0CF jmp A813 */
}
static const u16 bank_seq_bow[]   = /* A82F */ {A843, A86B, A893, A8BB, 0xFFFF};
static const u16 bank_seq_unbow[] = /* A839 */ {A8BB, A893, A86B, A843, 0xFFFF};
/* bank_play_seq @A813: each 40-cell frame map drawn by bank_draw_figure, 40 ticks apart */
void bank_play_seq(const u16 *seq)
{
    for (; *seq != 0xFFFF; seq++) { tick = 0; bank_draw_figure(*seq); while (tick < 0x28) ; }   /* A813..A82D (no idle poll!) */
}
/* bank_draw_figure @A751 (SI = 40-byte map): 5 rows x 8 cols at (x4 0x09 = 36 px, y 0x1F = 31) */
/* bank_hook @A728 = [A002]: while bank_anim_on, every 0x1E ticks of tick_total toggle between
 * bank_anim_frames[0] and [1] (2 x 40 bytes) */
void bank_hook(void)
{
    idle_poll();                                           /* callers */
    if (!bank_anim_on) return;                             /* A728 test [AD21] */
    if (tick_total < 0x1E) return;  tick_total = 0;        /* A730..A738 */
    bank_draw_figure(bank_anim_frames + (++bank_anim_tick & 1) * 0x28);   /* A73E..A751 */
}
static const u8 bank_portrait_map[8][12] = /* A6C8 */ {
    {0x6c,0x6d,0x6e,0x6f,0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77},
    {0x78,0x79,0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x7a,0x7b},
    {0x7c,0x7d,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x7e,0x7f},
    {0x80,0x81,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x82,0x83},
    {0x84,0x85,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x86,0x87},
    {0x88,0x89,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x8a,0x8b},
    {0x8c,0x8d,0x8e,0x8f,0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97},
    {0x98,0x99,0x9a,0x9b,0x9c,0x9d,0x9e,0x9f,0xa0,0xa1,0xa2,0xa3},
};  /* bank_draw_portrait @A6A3: 8 rows x 12 cols at (x4 0x07 = 28 px, y 0x17) */
static const u8 bank_talk_frames[2][40] = /* A773 */ {
    {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27},
    {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x28,0x29,0x12,0x13,0x14,0x15,0x16,0x17,0x2a,0x2b,0x2c,0x1b,0x1c,0x1d,0x1e,0x1f,0x20,0x2d,0x2e,0x23,0x24,0x25,0x26,0x27},
};
static const u8 bank_happy_frames[2][40] = /* A7C3 */ {
    {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x41,0x42,0x43,0x44,0x45,0x0f,0x10,0x11,0x46,0x4d,0x4e,0x49,0x4a,0x39,0x18,0x19,0x1a,0x4f,0x50,0x51,0x4c,0x3d,0x20,0x21,0x22,0x52,0x53,0x3e,0x3f,0x40},
    {0x00,0x01,0x54,0x55,0x56,0x05,0x06,0x07,0x08,0x09,0x57,0x58,0x59,0x5a,0x5b,0x0f,0x10,0x5c,0x5d,0x5e,0x5f,0x60,0x61,0x17,0x18,0x19,0x62,0x63,0x64,0x65,0x66,0x67,0x20,0x21,0x22,0x68,0x69,0x3e,0x6a,0x6b},
};
static const u8 bank_bow_frames[4][40] = {
 /* A843 */ {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x2f,0x30,0x1d,0x1e,0x1f,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27},
 /* A86B */ {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x2f,0x30,0x1d,0x1e,0x1f,0x20,0x21,0x22,0x23,0x31,0x32,0x33,0x27},
 /* A893 */ {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x34,0x35,0x0e,0x0f,0x10,0x11,0x12,0x13,0x36,0x37,0x38,0x39,0x18,0x19,0x1a,0x2f,0x3a,0x3b,0x3c,0x3d,0x20,0x21,0x22,0x23,0x24,0x3e,0x3f,0x40},
 /* A8BB */ {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x41,0x42,0x43,0x44,0x45,0x0f,0x10,0x11,0x46,0x47,0x48,0x49,0x4a,0x39,0x18,0x19,0x1a,0x2f,0x30,0x4b,0x4c,0x3d,0x20,0x21,0x22,0x23,0x24,0x3e,0x3f,0x40},
};

/* bank_act1_menu @A0D2 */
void bank_act1_menu(void)
{
    bank_erase_box();                                        /* A0D2 call A61F: VID_WINDOW(0, x4 0x17, y 0x27, w 0x41, h 0x1C) */
    VID_WINDOW(0xFF, x4=0x1D, y=0x28, w4=0x1A, h=0x37);      /* A0D5..A0DD */
    menu_pos = 0x2820; menu_visible_rows = 5; menu_total = 5;/* A0E2..A0ED [FF54],[FF52],[FF53] */
    menu_draw_items(bank_menu, 5);                           /* A0F2..A0F8 [600E] */
    menu_scroll = 0;                                         /* A0FD [FF56] */
    bl = bank_menu_cursor;                                   /* A102 */
    if (menu_select(&bl) == CANCEL) bl = 0;                  /* A106 [6010]; A10B jnc; A10D xor bl,bl */
    bank_menu_cursor = bl;                                   /* A10F */
    bank_menu_tab[bl]();                                     /* A117 jmp [A11B + bl*2] */
}
static void (*const bank_menu_tab[5])(void) = /* A11B */ { bank_leave /*A125*/, bank_exchange /*A14B*/, bank_deposit /*A23B*/, bank_withdraw /*A3D0*/, bank_balance /*A595*/ };

/* bank_leave @A125 */
void bank_leave(void)
{
    bank_erase_box();
    shop_text_ptr = bank_big_deposit ? bank_str_thanks_big      /* A128/A12E: ACD4 */
                  : bank_did_business ? bank_str_next_time      /* A136/A13C: AC9D */
                  : bank_str_busy_man;                          /* A144: AC5A */
}
/* bank_exchange @A14B: converts ALL almas at the town's rate */
void bank_exchange(void)
{
    bank_erase_box(); bank_anim_on = 0; bank_draw_figure(A8BB);          /* A14B..A156 (counter pose) */
    shop_text_ptr = bank_str_no_almas;                                    /* A15F: A9B2 */
    if (almas == 0) return;                                               /* A159 test word [8B] */
    bank_rate_almas = bank_rates[map_id - 1][0];                          /* A168..A176  [C006] */
    bank_rate_gold  = bank_rates[map_id - 1][1];                          /* A179..A17D */
    print(A9D9 "Our exchange rate is "); print_digit(bank_rate_almas);    /* A180..A199  (AD27 = '0'+n, "\xff") */
    print(A9F1 "&almas to ");           print_digit(bank_rate_gold);      /* A19E..A1B7 */
    print(A9FD "&golds./Will that be all right?");                        /* A1BC */
    VID_WINDOW(0xFF, x4=0x2F, y=0x2B, w4=0x0C, h=0x19); menu_pos = 0x302E;/* A1C7..A1D4 */
    shop_text_ptr = bank_str_dont_understand;                             /* A1DF: AA48 */
    if (yes_no_prompt() == NO) return;                                    /* A1DA [6008] */
    shop_text_ptr = bank_str_not_enough_almas;                            /* A1F3: AA1D */
    if (almas < bank_rate_almas) return;                                  /* A1E8..A1F9 */
    bank_erase_box(); bank_did_business = 0xFF;                           /* A1FD, A201 */
    shop_text_ptr = bank_str_anything_else;                               /* A206: AA82 */
    while (almas >= bank_rate_almas) {                                    /* A20C..A239 */
        almas -= bank_rate_almas;                                         /* A217 [8B] */
        gold_add(bank_rate_gold);                                         /* A222 [600C] DL:AX = 0:rate_gold */
        VID_NUM_GOLD(); VID_NUM_ALMAS();                                  /* A227, A22C — redrawn every unit, no delay */
    }
}
/* bank_deposit @A23B */
void bank_deposit(void)
{
    bank_erase_box(); bank_anim_on = 0; bank_draw_figure(A8BB);          /* A23B..A246 */
    shop_text_ptr = bank_str_no_gold;                                     /* A249: AAA1 */
    if (gold == 0) return;                                                /* A24F..A25A (24-bit test) */
    shop_text_ptr = bank_str_how_much_deposit; shop_print_text();                             /* A25D..A263 "How much gold would you like to deposit?" */
    VID_WINDOW(0xFF, x4=0x2C, y=0x1D, w4=0x12, h=0x37); menu_pos = 0x2A20;/* A268..A275 */
    menu_visible_rows = menu_total = 4; menu_show_prices = 0;             /* A27B..A285 */
    menu_draw_items(bank_deposit_labels, 4);                              /* A28A..A290 */
    bank_amt_hi = 0; bank_amt = 0;                                        /* A295..A29A */
    bank_max_hi = gold_hi; bank_max = gold_lo;                            /* A2A0..A2AB (limit = gold carried) */
    for (;;) {                                                            /* A2AE */
        GT_NUMBER_LINE(gold_can_pay(bank_amt));  GT_MENU_BLIT(x4 0x2E, y 0x31);   /* A2B7..A2C4 "GOLD CARRIED" = gold - amount */
        GT_NUMBER_LINE(bank_amt);                GT_MENU_BLIT(x4 0x48, y 0x31);   /* A2CB..A2D3 "DEPOSIT AMT" */
        input = INT61();                                                  /* A2D8 */
        bank_adjust_amount(input.dirs);                                   /* A2DA call A62C */
        if (input.fire & 1) break;                                        /* A2DD confirm (Space/A) */
        shop_text_ptr = bank_str_dont_understand;                         /* A2E2 */
        if (input.fire & 2) return;                                       /* A2E8 cancel (Alt/B) */
        bank_key_repeat(input.dirs);                                      /* A2EE..A319: first repeat after 0x23 ticks, then delay-1 each time, min 1 */
    }
    shop_text_ptr = bank_str_dont_understand;                             /* A31B */
    if (bank_amt_hi == 0 && bank_amt == 0) return;                        /* A321..A330 */
    if (bank_amt_hi != 0 || bank_amt >= 1000) {                           /* A331..A338 */
        bank_anim_on = 0xFF; bank_anim_frames = bank_happy_frames;        /* A33A, A33F: delighted banker */
    }
    bank_lo += bank_amt; bank_hi += bank_amt_hi + carry;                  /* A345 add [89],ax ; A349 adc [88],dl */
    gold = gold_can_pay(bank_amt); VID_NUM_GOLD();                        /* A34D..A360: [85],[86] = gold - amount */
    bank_did_business = 0xFF;                                             /* A365 */
    if (bank_anim_on) { shop_text_ptr = bank_str_thank_you; return; }     /* A36A..A3C9: AB10 (-> action 3 sets bank_big_deposit) */
    bank_say_balance(bank_str_account_empty /*ABF7*/);                 /* A371..A3C8 */
}
/* bank_say_balance @A371/A535/A598 (inlined three times): */
void bank_say_balance(const char *empty_str)
{
    shop_text_ptr = empty_str;  if (bank_hi == 0 && bank_lo == 0) return;             /* A371..A384 */
    shop_text_ptr = bank_str_one_gold_balance;  if (bank_hi == 0 && bank_lo == 1) return;   /* A385..A398 (AC35) */
    print(AAF4 "\x0dYour balance is "); print_number(bank_hi:bank_lo); /* A399..A3C4: format_number -> AD30, printed; then continues "&golds.\xff\x01" */
}
/* bank_withdraw @A3D0 */
void bank_withdraw(void)
{
    bank_erase_box(); bank_anim_on = 0; bank_draw_figure(A8BB);          /* A3D0..A3DB */
    shop_text_ptr = bank_str_no_account_gold;                             /* A3DE: AB32 */
    if (bank_hi == 0 && bank_lo == 0) return;                             /* A3E4..A3F1 */
    print(AB80 "How much do you wish to withdraw?/");                     /* A3F2..A3F8 */
    VID_WINDOW(0xFF, 0x2C, 0x1D, 0x12, 0x37); menu_pos = 0x2A20; rows = total = 4; menu_show_prices = 0;   /* A3FD..A41A */
    menu_draw_items(bank_withdraw_labels, 4);                             /* A41F..A425 */
    bank_amt = 0; bank_max = bank balance;                                /* A42A..A440 */
    for (;;) {                                                            /* A443: as deposit but the left column shows bank - amount (A44C..A459) */
        ... same loop as A2AE..A319 (A443..A4B8) ...
    }
    shop_text_ptr = bank_str_dont_understand; if (amount == 0) return;    /* A4BA..A4CF */
    bank_did_business = 0xFF;                                             /* A4D0 */
    if (amount == 1) print(ABC1 "Here you are, sir. One gold.");          /* A4D5..A4E9, A51A */
    else { print(ABA4 "Here you are, sir. "); print_number(amount); print("&golds."); }   /* A4EB..A51A */
    bank -= amount;                                                       /* A51F..A532 */
    bank_say_balance(ABDE /*"\x0dYour account is empty."*/);              /* A535..A583 */
    gold_add(amount); VID_NUM_GOLD();                                     /* A584..A590 [600C],[2016] */
}
/* bank_balance @A595 */
void bank_balance(void)
{
    bank_erase_box();                                                     /* A595 */
    shop_text_ptr = bank_str_account_empty;  if (bank == 0) return;       /* A598..A5A9 (ABF7) */
    bank_did_business = 0xFF;                                             /* A5AA */
    shop_text_ptr = bank_str_one_gold_balance; if (bank == 1) return;     /* A5AF..A5C2 (AC35) */
    print(AC10 "You have "); print_number(bank); /* "&golds in your account." */   /* A5C3..A5F2 */
}
/* bank_act2_unbow @A5F3 */
void bank_act2_unbow(void)
{
    bank_anim_on = 0; bank_play_seq(bank_seq_unbow);                      /* A5F3..A5FB */
    bank_anim_on = 0xFF; bank_anim_frames = bank_talk_frames;             /* A5FE, A603 */
    tick = 0; do bank_hook(); while (tick < 0x64);                        /* A609..A616 */
}
void bank_act3_flag_big(void) { bank_big_deposit = 0xFF; }                /* A619 */
void bank_erase_box(void) { VID_WINDOW(0, x4=0x17, y=0x27, w4=0x41, h=0x1C); }   /* A61F */
/* bank_adjust_amount @A62C (AL = directions): right -10, left +10, down -1, up +1; clamp 0..max */
void bank_adjust_amount(u8 dirs)
{
    u24 v = bank_amt_hi:bank_amt;
    if      (dirs & 8) { v -= 10; if (borrow) v = 0; }                        /* A634..A644 */
    else if (dirs & 4) { v += 10; if (v > bank_max) v = bank_max; }           /* A646..A666 */
    else if (dirs & 2) { v -= 1;  if (borrow) v = 0; }                        /* A668..A678 */
    else if (dirs & 1) { v += 1;  if (v > bank_max) v = bank_max; }           /* A67A..A696 */
    bank_amt_hi:bank_amt = v;                                                 /* A69A..A6A2 */
}

/* bank strings (A989..AD1D) — '&' = non-breaking space */
static const char bank_str_dots_open[]  = /* A989 */ "\x0c\xff" ".\xff";                 /* printed as two calls: "\x0c", then "." x5 */
static const char bank_script_greeting[]= /* A98D */ "Oh, excuse me. \xff\x00" "Can I help you?/" "\xff\x01\xff\xff";
static const char bank_str_no_almas[]   = /* A9B2 */ "\x0cSir, you aren\\t carrying any almas. \xff\x01";
static const char bank_str_rate_a[]     = /* A9D9 */ "\x0cOur exchange rate is \xff\x00";   /* FF 00 here ends the call (AL=0 ignored) */
static const char bank_str_rate_b[]     = /* A9F1 */ "&almas to \xff\x00";
static const char bank_str_rate_c[]     = /* A9FD */ "&golds./Will that be all right?\xff";
static const char bank_str_not_enough_almas[] = /* AA1D */ "\x0cI\\m sorry, you do not have enough almas.\xff\x01";
static const char bank_str_dont_understand[]  = /* AA48 */ "\x0cI don\\t understand. Please state your business clearly.\xff\x01";
static const char bank_str_anything_else[]    = /* AA82 */ "\x0cWill there be anything else?\xff\x01";
static const char bank_str_no_gold[]          = /* AAA1 */ "\x0cYou aren\\t carrying any gold, are you?\xff\x01";
static const char bank_str_how_much_deposit[] = /* AACA */ "\x0cHow much gold would you like to deposit?\xff";
static const char bank_str_balance_is[]       = /* AAF4 */ "\x0dYour balance is \xff\x00" "&golds.\xff\x01";
static const char bank_str_thank_you[]        = /* AB10 */ "\x0cThank you. Please come again.\xff\x03\xff\x01";
static const char bank_str_no_account_gold[]  = /* AB32 */ "\x0cI\\m afraid we have a problem here. You don\\t have any gold in your account.\xff\x01";
static const char bank_str_how_much_withdraw[]= /* AB80 */ "\x0cHow much do you wish to withdraw?/\xff";
static const char bank_str_here_you_are[]     = /* ABA4 */ "Here you are, sir. \xff\x00" "&golds.\xff";
static const char bank_str_here_one_gold[]    = /* ABC1 */ "Here you are, sir. One gold.\xff";
static const char bank_str_account_empty_nl[] = /* ABDE */ "\x0dYour account is empty.\xff\x01";
static const char bank_str_account_empty[]    = /* ABF7 */ "\x0cYour account is empty.\xff\x01";
static const char bank_str_you_have[]         = /* AC10 */ "\x0cYou have \xff\x00" "&golds in your account.\xff\x01";
static const char bank_str_one_gold_balance[] = /* AC35 */ "\x0cYou have one gold in your account.\xff\x01";
static const char bank_str_busy_man[]         = /* AC5A */ "\x0cUnless you have business, don\\t come in here. I\\m a busy man.\xff\x02\x11\xff\xff";
static const char bank_str_next_time[]        = /* AC9D */ "\x0cNext time please deposit a large sum in savings. \xff\x02\x11\xff\xff";
static const char bank_str_thanks_big[]       = /* ACD4 */ "\x0cThank you. Come again to make a deposit for a large sum in savings. \xff\x02\x11\xff\xff";

/* ======================================================================== */
/* CHURPRO.BIN — ZELRES2[14] (res# 0x0F), 998 bytes: the church (free heal)  */
/* door dest 5                                                              */
/* ======================================================================== */
/* vectors: [A000] = A004 chur_main, [A002] = A1D7 chur_hook */
static const struct req chur_portrait_req = /* A299 */ {1, 0x17, "CHURCH.GRP"};  /* ZELRES2[22] */
static const u8 chur_label[] = /* A2A6 */ {0x17, 0xAF, 0x02, 0x0A, "The Church"};
u8 chur_cross_frame;   /* A3E4 */
u8 chur_priest_frame;  /* A3E5  0..2 */

void chur_main(void)
{
    shop_prologue(&chur_portrait_req, chur_label, chur_draw_portrait);   /* A006..A04E (portrait A152: 8x12 at x4 0x0E, y 0x17, map A177) */
    shop_text_ptr = (hp == max_hp) ? chur_script_full : chur_script_tired;   /* A053 call A288: [90] vs [B2] */
    for (;;) { u8 op = shop_print_text(); if (op == 0xFF) break; chur_action(op); }   /* A05A..A066 */
    VID_DISSOLVE();
}
static void (*const chur_action_tab[5])(void) = /* A078 */ {
    chur_act0_cross,        /* A0E5: 5-frame holy-light animation over the altar */
    chur_act1_goto_bless,   /* A082: shop_text_ptr = chur_script_bless (A36A) */
    chur_act2_wait250,      /* A089: wait 0xFA ticks (priest keeps animating) */
    chur_act3_heal,         /* A099: hp += 8 every 20 ticks until max_hp, then hp = max_hp */
    chur_act4_restore_magic,/* A0CB: magic_count[0..6] = magic_max[0..6] ([B4..BA] -> [AB..B1]); redraw if magic_sel */
};
void chur_act3_heal(void)
{
    while (hp + 8 < max_hp) {                              /* A099..A0A3 */
        hp += 8; VID_LIFE_BAR_CUR();                       /* A0A5, A0A8 [2008] */
        tick = 0; do chur_hook(); while (tick < 0x14);     /* A0AD..A0BA */
    }
    hp = max_hp; VID_LIFE_BAR_CUR();                       /* A0BE..A0C4 */
}
void chur_act4_restore_magic(void)
{
    memcpy(&magic_count, &magic_max, 7);                   /* A0CB..A0D6: si=B4, di=AB, cx=7 */
    if (magic_sel) VID_NUM_ITEM_COUNT();                   /* A0D8..A0DF [2018] */
}
/* chur_act0_cross @A0E5: frames 0..4, 3 rows x 2 cols at (x4 0x16 = 88 px, y 0x3F), 32 ticks each */
static const u8 chur_cross_frames[5][6] = /* A134 */ {
    {0x41,0x42,0x4d,0x4e,0x57,0x58}, {0x41,0x42,0x6b,0x6c,0x6d,0x6e}, {0x41,0x42,0x6f,0x70,0x71,0x72},
    {0x73,0x42,0x74,0x75,0x76,0x77}, {0x78,0x79,0x7a,0x7b,0x7c,0x77},
};
static const u8 chur_portrait_map[8][12] = /* A177 */ {
    {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b},{0x0c,0x0d,0x0e,0x0f,0x10,0x11,0x12,0x10,0x13,0x14,0x15,0x16},
    {0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x20,0x21,0x22},{0x23,0x24,0x25,0x26,0x26,0x27,0x28,0x26,0x29,0x2a,0x2b,0x2c},
    {0x2d,0x2e,0x2f,0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38},{0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,0x40,0x41,0x42,0x43,0x44},
    {0x45,0x46,0x47,0x48,0x49,0x4a,0x4b,0x4c,0x4d,0x4e,0x4f,0x50},{0x51,0x52,0x53,0x54,0x53,0x55,0x56,0x53,0x57,0x58,0x59,0x5a},
};
/* chur_hook @A1D7 = [A002]: every 0x20 ticks of tick_total: priest frame = (frame+1) % 3;
 * draws 2 rows x 3 cols at (x4 0x10, y 0x37) from chur_priest_a (0xFF = skip cell) and
 * 2 rows x 2 cols at (x4 0x15, y 0x37) from chur_priest_b */
static const u8 chur_priest_a[3][6] = /* A234 */ {{0xff,0x30,0x31,0x3b,0x3c,0x3d},{0xff,0x5b,0x5c,0x5d,0x5e,0x5f},{0xff,0x60,0x61,0x62,0x63,0x64}};
static const u8 chur_priest_b[3][4] = /* A27C */ {{0x34,0x35,0x40,0xff},{0x65,0x66,0x67,0xff},{0x68,0x69,0x6a,0xff}};
static const char chur_script_full[]  = /* A2B4 */ "\x0cBrave Knight, whenever you\\re tired come to this church./\xff\x04\xff\x01";
static const char chur_script_tired[] = /* A2F2 */ "\x0cBrave Knight, whenever you\\re weary, come here to rest. \xff\x02\xff\x02"
                                                   "The Holy Spirit will help you to regain your strength.\xff\x03\x0d\xff\x01";
static const char chur_script_bless[] = /* A36A */ "Brave Knight, you look fatigued from battle. Why not rest awhile and let the Spirit heal you. "
                                                   "\xff\x02/May God go with you.\xff\x00\x11\xff\xff";
/* NB: when hp == max the script still restores magic (action 4) and then prints the "you look
 * fatigued" text; the healing loop (action 3) only runs from chur_script_tired. */

/* ======================================================================== */
/* INNAPRO.BIN — ZELRES2[16] (res# 0x11), 1296 bytes: the inn                */
/* door dest 7                                                              */
/* ======================================================================== */
/* vectors: [A000] = A004 inn_main, [A002] = A22F inn_hook */
static const struct req inn_portrait_req = /* A2E1 */ {1, 0x19, "INN.GRP"};      /* ZELRES2[24] */
static const u8 inn_label[] = /* A2EB */ {0x19, 0xAF, 0x00, 0x07, "The Inn"};
u8  inn_idle_on;      /* A505  innkeeper blink animation enabled */
u16 inn_price;        /* A506 */
u8  inn_numbuf[];     /* A508  format_number output */

/* Price of a night, indexed by map id [C006]-1 (A08A..A098); id 1 (Castle/Muralla) has no inn */
static const u16 inn_prices[8] = /* A2D1 */ {0, 30, 50, 70, 100, 150, 200, 400};
/*  Satono 30 · Bosque 50 · Helada 70 · Tumba 100 · Dorado 150 · Llama 200 · Pureza 400 (Esco: no inn) */

void inn_main(void)
{
    shop_prologue(&inn_portrait_req, inn_label, inn_redraw);   /* A006..A03E; A043 call A05F */
    shop_text_ptr = inn_script;                                /* A046 [FF4C] = A2F6 */
    for (;;) { u8 op = shop_print_text(); if (op == 0xFF) break; inn_action(op); }   /* A04C..A058 */
    VID_DISSOLVE();
}
/* inn_redraw @A05F: portrait (A1AA: 8x12 at x4 0x07, y 0x17, map A1CF), text box, inn_idle_on = FF */
static void (*const inn_action_tab[5])(void) = /* A080 */ {
    inn_act0_price,     /* A08A: prints the price number into the running text */
    inn_act1_ask,       /* A0BE: yes/no; then pay or refuse */
    inn_act2_sleep,     /* A114: 4-frame "going to bed" animation (inn_idle_on = 0) */
    inn_act3_night,     /* A12A: wait, VID_DISSOLVE, wait x2, hp = max_hp, magic restored, redraw */
    inn_act4_wait150,   /* A15F: 0x96 ticks */
};
void inn_act0_price(void)
{
    inn_price = inn_prices[map_id - 1];                      /* A08A..A098 [C006] */
    format_number(0, inn_price, inn_numbuf);                  /* A09C..A0A3 [6006] */
    push shop_text_ptr; shop_text_ptr = inn_numbuf; shop_print_text(); pop shop_text_ptr;   /* A0A8..A0B9 */
}
void inn_act1_ask(void)
{
    VID_WINDOW(0xFF, x4=0x2F, y=0x2B, w4=0x0C, h=0x19); menu_pos = 0x302E;   /* A0BE..A0CB */
    bool no = yes_no_prompt();                                              /* A0D1 [6008] */
    VID_WINDOW(0, 0x2F, 0x2B, 0x0C, 0x19);                                  /* A0D7..A0DF erase */
    shop_text_ptr = inn_str_sorry; if (no) return;                          /* A0E5 (A3BD), A0EB */
    r = gold_can_pay(inn_price);                                            /* A0EE..A0F3 [600A] DL:AX = 0:price */
    shop_text_ptr = inn_str_no_funds; if (r.CF) return;                     /* A0F8 (A41A), A0FE */
    gold = r;  VID_NUM_GOLD();                                              /* A101..A108: [85]=dl, [86]=ax */
    shop_text_ptr = inn_str_thank_you;                                      /* A10D (A483) */
}
void inn_act2_sleep(void)
{
    inn_idle_on = 0;                                                        /* A114 */
    for (f = 0; f < 4; f++) { inn_draw_bed(f); inn_wait(0x32); }            /* A119..A127: A17F (4 rows x 5 cols at x4 0x08, y 0x27), A16F */
}
void inn_act3_night(void)
{
    inn_wait(0x96); VID_DISSOLVE(); inn_wait(0x96); inn_wait(0x96);         /* A12A..A135 */
    hp = max_hp; VID_LIFE_BAR_CUR();                                        /* A138..A13E */
    memcpy(&magic_count, &magic_max, 7); if (magic_sel) VID_NUM_ITEM_COUNT();/* A143..A157 */
    inn_redraw();                                                           /* A15C jmp A05F */
}
/* inn_hook @A22F = [A002]: if inn_idle_on and tick_total >= 0x28: tick_total = 0;
 * frame = KRN_RANDOM() & 1 -> inn_blink_frames[frame] drawn 2x2 at (x4 0x08, y 0x27) (random blink) */
static const u8 inn_blink_frames[2][4] = /* A279 */ {{0x19,0x1a,0x24,0x25},{0x5e,0x5f,0x24,0x60}};
static const u8 inn_bed_frames[4][20] = /* A281 */ {
    {0x19,0x1a,0x1b,0x10,0x1c,0x24,0x25,0x26,0x10,0x27,0x2f,0x30,0x31,0x32,0x33,0x3b,0x3c,0x3d,0x3e,0x3f},
    {0x19,0x1a,0x1b,0x10,0x1c,0x24,0x25,0x26,0x10,0x27,0x2f,0x30,0x31,0x32,0x33,0x3b,0x3c,0x3d,0x3e,0x3f},
    {0x19,0x1a,0x1b,0x10,0x1c,0x24,0x61,0x62,0x10,0x27,0x2f,0x63,0x64,0x32,0x33,0x3b,0x65,0x66,0x3e,0x3f},
    {0x19,0x1a,0x1b,0x10,0x1c,0x24,0x25,0x26,0x67,0x68,0x2f,0x69,0x6a,0x6b,0x6c,0x3b,0x6d,0x6e,0x6f,0x3f},
};
static const u8 inn_portrait_map[8][12] = /* A1CF */ {
    {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b},{0x0c,0x0d,0x0e,0x0f,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17},
    {0x18,0x19,0x1a,0x1b,0x10,0x1c,0x1d,0x1e,0x1f,0x20,0x21,0x22},{0x23,0x24,0x25,0x26,0x10,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d},
    {0x2e,0x2f,0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39},{0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,0x40,0x41,0x42,0x43,0x44,0x45},
    {0x46,0x47,0x48,0x49,0x4a,0x4b,0x4c,0x4d,0x4e,0x4f,0x50,0x51},{0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x5b,0x5c,0x5d},
};
static const char inn_script[] = /* A2F6 */
 "\x0cWelcome, sir!/You look like you\\ve come a long way./One night of rest in my inn is all you need to recover your strength. "
 "You can have the best room in the house for only \xff\x00" "&golds. Will you stay? \xff\x01";
static const char inn_str_sorry[]     = /* A3BD */ "\x0cOh, I\\m sorry to hear that./Well, if you should ever need a place to rest, do come back. \x11\xff\xff";
static const char inn_str_no_funds[]  = /* A41A */ "\x0cI\\m sorry sir, but I can\\t accommodate you without funds./\xff\x04" "Please come back when you can afford it. \x11\xff\xff";
static const char inn_str_thank_you[] = /* A483 */ "\x0cThank you, sir. Enjoy your stay. \xff\x02\xff\x04\xff\x03" "\x0c\xff\x04"
                                                   "I trust you had a good night\\s sleep. We\\ll be looking forward to seeing you again./\x11\xff\xff";

/* ======================================================================== */
/* ===== ARMRPRO.BIN (ZELRES2[12], 7239 bytes) — Weapon and Armour Shop ==== */
/* ======================================================================== */
/*
 * Slot-B overlay, raw at BASE:A000.  Vectors: [A000] = A004 (entry, called by
 * town.bin 6EAA), [A002] = A90F (per-frame hook, called by idle_poll 7042 while
 * [7C42]).  Portrait ARMOR.GRP (ZELRES2[20], request block ACA2) -> arena:8000.
 * Follows the shared shop_prologue() (see kingpro) with one deviation: the
 * prologue at A004 does NOT start with a 4-byte jump; it ALSO copies the
 * per-town price table and builds the per-town stock lists before the text loop.
 *
 * Text-script convention (shop_print_text 706C): a string ends "FF nn"; nn is
 * returned in AL and dispatched through armr_action[] below; "FF FF" leaves the
 * shop (VID_DISSOLVE and return to town.bin).
 */

/* ---- overlay locals BC21..BC46 (BSS after the price table) ---------------- */
u8   armr_main_cursor;        /* BC21  cursor of the 5-entry main menu (persists) */
u8   armr_list_cursor;        /* BC22  cursor in the buy/explain lists */
u8   armr_anim_on;            /* BC23  idle fidget animation enabled (A90F) */
u8   armr_anim_t0, armr_anim_phase, armr_anim_state, armr_anim_t1; /* BC24..BC27 */
u8   armr_bought;             /* BC28  != 0: something was bought/repaired -> polite goodbye */
u8   armr_price_hi; u16 armr_price_lo;   /* BC29/BC2A  24-bit price or trade-in value */
u8   armr_rest_hi;  u16 armr_rest_lo;    /* BC2C/BC2D  gold left after paying */
u8   armr_new_id;             /* BC2F  1-based id of the item being bought */
u8   armr_old_id;             /* BC30  1-based id of the sword/shield traded in (0 none) */
u8   armr_n_swords;           /* BC31  entries in armr_sword_list */
u8   armr_n_shields;          /* BC32  entries in armr_shield_list */
u8   armr_numbuf[8];          /* BC33  ASCII number from format_number */
u8   armr_sword_list[6];      /* BC3B  0-based sword ids in stock (built A0B6) */
u8   armr_shield_list[6];     /* BC41  0-based shield ids in stock (built A0E2) */
/* BBFD: 12 x {u8 hi, u16 lo} = this town's price table, copied from armr_prices[] */
struct price24 { u8 hi; u16 lo; } armr_price[12];   /* BBFD  [0..5] swords, [6..11] shields */

/* ---- player record fields used only by the shops ------------------------- */
/* 0x96  u16 shield_hp_max  (set with [0x94] when a shield is bought, A6B4/A6B7)  */
/* 0xD2..0xDA  sword_stock[9]  per-town bitmask, bit7 = Training .. bit2 = Enchantment */
/* 0xDB..0xE3  shield_stock[9] per-town bitmask, bit7 = Clay .. bit2 = Titanium      */
/* 0x24 bit1   glory_crest_traded (A8CF); also gates the Knight's sword in Tumba    */
/* STDPLY defaults: sword_stock = C0 C0 E0 E0 70 38 38 F8 F8, shield_stock = C0 E0 E0 70 30 38 1C 1C FC */

/* ---- data --------------------------------------------------------------- */
const u8 armr_bit_of_id[6] = {0x80,0x40,0x20,0x10,0x08,0x04};   /* AC9C  1-based id -> stock bit */
const struct req armr_portrait = {1, 0x15, "ARMOR.GRP"};        /* ACA2  ZELRES2[20] */
const u8 armr_label[] = {0x10,0xAF,0x00,0x16,"Weapon and Armour Shop"}; /* ACAE  VID_LABEL_TEXT record */
const char armr_menu[] = "Go outside\0Repair shield\0Buy weapon\0Buy shield\0Explain goods\0"; /* ACC8 */
const char *armr_name[12] = {   /* AD05  pointer table, names at AD1D.. */
    "Training sword", "Wise man\\s sword", "Spirit sword", "Knight\\s sword",
    "Illumination sword", "Enchantment sword",
    "Clay shield", "Wise man\\s shield", "Stone shield", "Honor shield",
    "Light shield", "Titanium Shield" };
const u16 armr_shield_hp[6] = {30, 80, 180, 300, 300, 600};      /* A6BF  durability of shield 1..6 */
const u8  armr_smith_anim[] = {3,4,5,5,0x86,6,7,7,0xFF};         /* A6FD  frame seq; bit7 = sfx 0x20 */
/* A9CF frame table AA10: 8 frames x {u8 rows, u16 map; u8 rows, u16 map}, 12 cells/row at (x4 7, y 0x17)
 *  0 {2,AA40}{6,AA88}  1 {2,AA40}{6,AAD8}  2 {2,AA40}{6,AB20}  3 {4,AA58}{4,AB70} (surprised)
 *  4 {3,AA58}{5,ABA0}  5 {8,ABDC}{0,-}     6 {4,AA58}{4,AC3C}  7 {4,AA58}{4,AC6C}   (3..7 = hammering) */
/* AAD0: eye-blink cells (4 x 2 cells, drawn at x4 0x10, y 0x4F); AB68: mouth cells (4 x 2, at x4 0x0B, y 0x37 + 8) */

/* BAA7: 9 pointers (BAB9, BADD, BB01, BB25, BB49, BB6D, BB91, BBB5, BBD9) -> per-town price tables,
 * 12 x {u8 hi, u16 lo}; decoded (gold):                                                         */
const u32 armr_prices[9][12] = {
 /*            Train  Wise  Spirit Knight Illum  Ench | Clay  Wise  Stone  Honor  Light  Titan */
 /* cmap */ {   400, 1500,  6800,  9800, 90000,   4,    50,  150,  2980,  9800, 14800, 39800 },
 /* mrmp */ {   800, 1500,  6800,  9800, 69800,   4,    50,  150,  2980,  9800, 14800, 39800 },
 /* stmp */ {   800, 1500,  6800,  9800, 69800,   4,     5,  150,  2380,  9800, 14800, 39800 },
 /* bsmp */ {   400, 3000,  5440,  9800, 69800,   4,     5,   50,  1780,  9800, 14800, 39800 },
 /* hlmp */ {   400, 3000,  4760,  4900, 69800,   4,     5,   50,  1780,  7840, 14800, 39800 },
 /* tmmp */ {   200, 1500,  3400,  7840, 69800,   4,     5,   20,   890,  5880, 14800, 39800 },
 /* drmp */ {   200, 1500,  1360,  5880, 34800,   4,     5,   20,   890,  5880, 10360, 39800 },
 /* llmp */ {   100, 1000,  1360,  3920, 32800,   4,     5,   20,   890,  3920,  7400, 31800 },
 /* prmp */ {    10,  100,   680,  1960, 29800,   4,     2,   10,   298,  1960,  5920, 23800 },
};
/* The Enchantment sword "price" 4 is never offered: bit2 is absent from every initial stock
 * mask and A45D clears it again when it is bought; it only appears after trading one in. */

/* ==== armr_entry @ A004  ([A000]) ======================================== */
void armr_entry(void)
{
    shop_prologue();                     /* A004..A043: ARMOR.GRP -> arena:8000, VID_CONVERT_CELLS x256,
                                            FF4E=FF4F=0, [BC21]=0 (A031), clear playfield, trough,
                                            VID_LABEL_TEXT(armr_label) */
    armr_build_sword_list();             /* A048 */
    armr_build_shield_list();            /* A04B */
    memcpy(armr_price, armr_prices_ptr[MAP_ID - 1], 36);   /* A04E..A064: [C006]-1 -> BAA7 table, 0x12 words -> BBFD */
    armr_draw_frame(0);                  /* A066 */
    armr_anim_on = 0xFF;                 /* A06B */
    VID_WINDOW(0xFF, bx=0x0D60, cx=0x3637);       /* A070  text box */
    shop_text_ptr = 0xADD3;              /* A07D  "May I be of service, sir?/" FF 00 ... */
    /* Tumba (map 5) special: Crest of Glory not yet traded and the player carries it */
    if (!(P[0x24] & 2) && MAP_ID == 5 && P[0x9B] /* glory_crest */) {   /* A083..A096 */
        shop_text_ptr = 0xB2A2;          /* A098  "Well I\ll be... " FF 04 FF 04 "Sir! " FF 09 ... FF 08 */
        armr_anim_on = 0;                /* A09E */
    }
    for (;;) {                           /* A0A3 */
        u8 op = shop_print_text();       /* [6004] */
        if (op == 0xFF) { VID_DISSOLVE(); return; }   /* A0A8/A0B1  "FF FF" = leave */
        armr_action[op]();               /* A0AC -> A10E jump table A119 */
    }
}

/* ==== armr_build_sword_list @ A0B6 ========================================= */
/* stock byte [0xD2 + map-1]: bit7 = sword 0 (Training) ... bit2 = sword 5.  */
void armr_build_sword_list(void)
{
    u8 mask = P[0xD2 + MAP_ID - 1], n = 0;            /* A0B6..A0C2 */
    for (int i = 0; i < 6; i++)                        /* A0CB cx=6 */
        if (mask & (0x80 >> i)) armr_sword_list[n++] = i;   /* A0CE..A0D9 (al = 6-cl) */
    armr_n_swords = n;                                 /* A0DD -> BC31 */
}
/* ==== armr_build_shield_list @ A0E2 ======================================== */
void armr_build_shield_list(void)      /* same with [0xDB + map-1] -> BC41, count -> BC32 */
{
    u8 mask = P[0xDB + MAP_ID - 1], n = 0;
    for (int i = 0; i < 6; i++) if (mask & (0x80 >> i)) armr_shield_list[n++] = i;
    armr_n_shields = n;
}

/* ==== armr_action @ A10E  jump table A119 (10 entries) ===================== */
void (*armr_action[10])(void) = {
    armr_main_menu,      /* 0  A12D */
    armr_buy_sword,      /* 1  A259 */
    armr_buy_shield,     /* 2  A498 */
    armr_smith_anim_op,  /* 3  A6CB  angry hammering animation */
    armr_wait_150,       /* 4  A706 */
    armr_repair_done,    /* 5  A716 */
    armr_explain,        /* 6  A759 */
    armr_wait_50,        /* 7  A870 */
    armr_crest_trade,    /* 8  A880 */
    armr_frame3,         /* 9  A8FD  surprised face */
};

/* ==== armr_main_menu @ A12D  (op 0) ======================================== */
void armr_main_menu(void)
{
    armr_clear_menu_area();                      /* A12D -> A902 */
    VID_WINDOW(0xFF, bx=0x291D, cx=0x1837);      /* A130 */
    menu_pos = 0x2920; menu_visible_rows = 5; menu_total = 5;   /* A13D..A148 */
    menu_draw_items(armr_menu, 5);               /* A14D..A153  [600E] */
    menu_scroll = 0;                             /* A158 */
    u8 bl = armr_main_cursor;                    /* A15D */
    if (menu_select(&bl) == CANCEL) bl = 0;      /* A161/A168  Alt = "Go outside" */
    armr_main_cursor = bl;                       /* A16A */
    switch (bl) {                                /* A172 table A176 */
    case 0:  /* A180 Go outside */
        armr_clear_menu_area();
        shop_text_ptr = 0xB1DE;                  /* "\x0CThank you, please come again.\x11" FF FF */
        if (!armr_bought) { armr_wait_150(); shop_text_ptr = 0xB1FF; }
                                                 /* A186..A190 "\x0CIf you\re going to waste my time, please be on your way./" FF 07 FF 03 \x11 FF FF */
        return;
    case 1:  armr_repair_shield(); return;       /* A198 */
    case 2:  shop_text_ptr = 0xB026; return;     /* A244 "\x0CSomething else for you, sir?/" FF 01 -> buy sword */
    case 3:  shop_text_ptr = 0xB081; return;     /* A24B  "..." FF 02 -> buy shield */
    case 4:  shop_text_ptr = 0xB11F; return;     /* A252 "\x0CAll of my goods are of the highest quality. Which item ...?/" FF 06 */
    }
}

/* ==== armr_repair_shield @ A198 ============================================ */
void armr_repair_shield(void)
{
    armr_clear_menu_area();                                  /* A198 */
    if (P[0x93] == 0) { shop_text_ptr = 0xAE4A; return; }   /* A19B "Sir, you aren\t carrying a shield ..." FF 00 FF 0C.. */
    u16 missing = P16[0x96] - P16[0x94];                     /* A1A9  shield_hp_max - shield_hp */
    if (missing == 0) { shop_text_ptr = 0xAEB1; return; }    /* A1B0 "Sir, your shield is not in need of repair..." */
    armr_bought = 0xFF;                                      /* A1B9 */
    armr_price_lo = (missing + 1) >> 1; armr_price_hi = 0;   /* A1BE..A1C3  cost = ceil(missing/2) gold */
    shop_text_ptr = 0xAEF8; shop_print_text();               /* "\x0CI\ll be glad to repair your shield, sir, for the low price of " */
    format_number(0, armr_price_lo, armr_numbuf); print(armr_numbuf);   /* A1D1..A1EE  [6006] then [6004] */
    shop_print_text();                                       /* A1F3 " golds. Shall I proceed?" */
    VID_WINDOW(0xFF, bx=0x2F2B, cx=0x0C19); menu_pos = 0x302E;          /* A1F8..A205 */
    bool yes = (yes_no_prompt() == YES);                     /* A20B [6008] */
    armr_clear_menu_area();                                  /* A211 */
    shop_text_ptr = 0xADEF;                                  /* A215 "\x0CIs there something I can do for you, sir?/" FF 00 */
    if (!yes) return;                                        /* A21B */
    if (gold_can_pay(armr_price) == INSUFFICIENT) { shop_text_ptr = 0xAF53; return; }  /* A21E..A230 "\rI\m sorry sir, you aren\t carrying enough gold..." */
    P[0x85] = dl; P16[0x86] = ax;                            /* A231/A235  gold -= cost */
    VID_NUM_GOLD();                                          /* A238 */
    shop_text_ptr = 0xAFAF;   /* "\rPlease wait here, I\ll only be a moment." FF 04 FF 04 FF 05 "\x0CThe repairs to your armour are complete..." */
}

/* ==== armr_buy_sword @ A259  (op 1) ======================================== */
void armr_buy_sword(void)
{
    armr_bought = 0xFF;                                      /* A259 */
    memcpy(menu_item_ids, armr_sword_list, 6);               /* A25E..A269  -> FF58 */
    menu_total = armr_n_swords; menu_visible_rows = min(menu_total, 3);   /* A26B..A277 */
    menu_scroll = 0; armr_list_cursor = 0;                   /* A27A/A27F */
    VID_WINDOW(0xFF, bx=0x156E, cx=0x2524);                  /* A284 */
    menu_show_prices = 0xFF; menu_pos = 0x1571; menu_width_x4 = 0x21; price_col_x = 0x17;  /* A291..A2A2 */
    menu_draw_icons(first = menu_scroll, rows = menu_visible_rows, names = armr_name /*AD05*/, prices = armr_price /*BBFD*/);  /* A2A8..A2B7 [6012] */
    u8 bl = armr_list_cursor;
    if (menu_select(&bl) == CANCEL) { shop_text_ptr = 0xADEF; return; }   /* A2C0/A2C7 */
    armr_list_cursor = bl;                                   /* A2CE */
    u8 id = menu_item_ids[bl + menu_scroll];                 /* A2D2..A2DB  0-based sword */
    if (armr_knight_reserved(id)) return;                    /* A2DC -> A47B (pops our frame, sets FF4C=B24C) */
    shop_text_ptr = 0xB0DC; shop_print_text();               /* "\x0COh, the " */
    print(armr_name[id]); shop_print_text();                 /* A2ED..A309  name, then "?/" */
    armr_price_hi/lo = armr_price[id];                       /* A30E..A326  BBFD + 3*id */
    if (gold_can_pay(armr_price) == INSUFFICIENT) { shop_text_ptr = 0xAF54; return; }  /* A329..A337 "\rI\m sorry sir, you aren\t carrying enough gold. Perhaps after you\ve visited the bank.../" */
    armr_rest = DL:AX;                                       /* A338/A33C  gold after paying */
    armr_new_id = id + 1;                                    /* A33F/A341 */
    shop_text_ptr = 0xB106; shop_print_text();               /* "That will be " */
    format_number(armr_price, armr_numbuf); print(armr_numbuf); shop_print_text();   /* A350..A374 " golds./" */
    armr_price = 0;                                          /* A379/A37E  (trade-in value) */
    if (P[0x92] /* sword */) {                               /* A384 */
        armr_old_id = P[0x92];                               /* A38B */
        shop_text_ptr = 0xB046; shop_print_text();           /* "I\ll give you " */
        armr_price = armr_price[armr_old_id - 1] >> 1;       /* A39C..A3B9  trade-in = half this town's price of the old sword */
        format_number(...); print; shop_print_text();        /* A3BC..A3D9 " golds on your old weapon as a trade-in.\r" */
    }
    shop_text_ptr = 0xB0ED; shop_print_text();               /* A3DE "Will that be all right?" */
    VID_WINDOW(0xFF, 0x2F2B, 0x0C19); menu_pos = 0x302E;     /* A3E9..A3F6 */
    shop_text_ptr = 0xADEF;                                  /* A401 */
    if (yes_no_prompt() == NO) return;                       /* A3FC/A407 */
    shop_text_ptr = 0xAE1C;                                  /* A40A "\x0CWill there be something else for you, sir?/" FF 00 */
    P[0x85] = armr_rest_hi; P16[0x86] = armr_rest_lo;        /* A410..A41B  pay */
    gold_add(armr_price);                                    /* A41E..A425  + trade-in  [600C] */
    VID_NUM_GOLD();                                          /* A42A */
    if (armr_old_id)                                         /* A42F */
        P[0xD2 + MAP_ID - 1] |= armr_bit_of_id[armr_old_id - 1];   /* A436..A447  old sword goes back into this town's stock */
    P[0x92] = armr_new_id;                                   /* A44B/A44E */
    if (armr_new_id == 6) P[0xD2 + MAP_ID - 1] &= ~0x04;     /* A451..A45D  Enchantment sword is unique */
    armr_build_sword_list();                                 /* A462 */
    KRN_LOAD(mode 4, AH = P[0x92]);                          /* A465..A46B  install sword block */
    VID_ICON_SWORD(AL = P[0x92], BX = 0x18AB);               /* A470..A476  HUD icon */
}

/* ==== armr_knight_reserved @ A47B ========================================== */
/* In Tumba (map 5) the Knight's sword (id 3) cannot be bought until the Crest of Glory
 * has been traded ([0x24] bit1).  Pops the caller's return and continues the script. */
bool armr_knight_reserved(u8 id)
{
    if (id != 3 || (P[0x24] & 2) || MAP_ID != 5) return false;    /* A47B..A48D */
    pop_return(); shop_text_ptr = 0xB24C;    /* A490/A491 "\x0CI do not sell that weapon. I haven\t a single one in stock. Please choose another./" FF 00 FF 0C.. */
    return true;
}

/* ==== armr_buy_shield @ A498  (op 2) ======================================= */
/* Mirror of armr_buy_sword with the shield list (BC41/BC32), names armr_name+6 (AD11),
 * prices armr_price+6 (BC0F).  Differences after "Yes" (A645..A6BA):                 */
void armr_buy_shield(void)
{
    /* A498..A644 as armr_buy_sword; trade-in of the old shield = half its price (A5E4..A5EE);
       no Tumba restriction on shields. */
    ...
    P[0x85] = armr_rest_hi; P16[0x86] = armr_rest_lo; gold_add(armr_price); VID_NUM_GOLD();   /* A64B..A665 */
    if (armr_old_id) P[0xDB + MAP_ID - 1] |= armr_bit_of_id[armr_old_id - 1];   /* A66A..A682 */
    P[0x93] = armr_new_id;                                   /* A686/A689  shield id 1..6 */
    armr_build_shield_list();                                /* A68C */
    VID_ICON_MAGIC(AL = P[0x93], BX = 0x3EA4);               /* A68F..A695  [2020] = shield icon slot (250,164) */
    VID_GAUGE_BAR(AL=0, BX=0xC61C, CH=0x17);                 /* A69A..A6A1  shield gauge trough */
    P16[0x96] = P16[0x94] = armr_shield_hp[P[0x93] - 1];     /* A6A6..A6B7  durability, table A6BF */
    VID_NUM_MAGIC();                                         /* A6BA  [201A] redraws the shield-hp number */
}

/* ==== armr_smith_anim_op @ A6CB  (op 3) ==================================== */
void armr_smith_anim_op(void)
{
    armr_anim_on = 0;                                        /* A6CB */
    if (armr_anim_state) { armr_draw_frame(1); armr_wait_50(); }   /* A6D0..A6DB */
    for (const u8 *p = armr_smith_anim; *p != 0xFF; p++) {   /* A6E0..A6FB  table A6FD */
        if (*p & 0x80) sfx_request = 0x20;                   /* A6E9..A6ED  hammer clang (uncertain) */
        armr_draw_frame(*p & 7); armr_wait_50();             /* A6F2..A6F7 */
    }
}
/* ==== armr_wait_150 @ A706  (op 4) — 150 ticks (0x96) with the hook running == */
/* ==== armr_repair_done @ A716  (op 5) ====================================== */
void armr_repair_done(void)
{
    VID_DISSOLVE();                                          /* A716 */
    for (tick_total = 0; tick_total < 0x190; ) ;             /* A71B..A727  400 ticks */
    P16[0x94] = P16[0x96]; VID_NUM_MAGIC();                  /* A729..A72F  shield_hp = max */
    armr_anim_t0 = armr_anim_phase = armr_anim_state = armr_anim_t1 = 0;   /* A734..A743 */
    armr_draw_frame(0); armr_anim_on = 0xFF;                 /* A748..A74D */
    shop_text_ptr = 0xAFE0;                                  /* A752 "\x0CThe repairs to your armour are complete. It is now as good as new./" FF 00 FF 0C.. */
}

/* ==== armr_explain @ A759  (op 6) ========================================== */
void armr_explain(void)
{
    armr_list_cursor = 0; menu_scroll = 0;                   /* A759/A75E */
    /* list = swords in stock followed by shields in stock (+6) */
    memcpy(menu_item_ids, armr_sword_list, armr_n_swords);   /* A765..A771 */
    for (i..armr_n_shields) menu_item_ids[n++] = armr_shield_list[i] + 6;   /* A773..A77E */
    menu_total = armr_n_swords + armr_n_shields; menu_visible_rows = min(menu_total, 6);   /* A780..A793 */
    VID_WINDOW(0xFF, 0x2717, 0x1B41); menu_show_prices = 0; menu_pos = 0x271A; menu_width_x4 = 0x17;   /* A796..A7AE */
    menu_draw_icons(menu_scroll, menu_visible_rows, armr_name);   /* A7B4..A7C0 */
    u8 bl = armr_list_cursor;
    if (menu_select(&bl) == CANCEL) { shop_text_ptr = 0xADEF; return; }   /* A7C9/A7D0 */
    armr_list_cursor = bl;                                   /* A7D7 */
    shop_text_ptr = 0xB0EA; shop_print_text();               /* "\x0C" */
    u8 id = menu_item_ids[bl + menu_scroll];                 /* A7E6..A7F0 */
    if (armr_knight_secret(id)) return;                      /* A7F1 -> A8E0: Knight's sword in Tumba before the trade -> "Uh....../" */
    shop_text_ptr = 0xB0DD; shop_print_text(); print(armr_name[id]); shop_print_text();   /* A7F6..A81E "Oh, the " name "?/" */
    shop_text_ptr = armr_desc[id]; shop_print_text();        /* A823..A831  table B3DE (12 ptrs B3F6..BA0F), each ends "\x11\x0C" FF FF */
    shop_text_ptr = 0xB1A9; shop_print_text();               /* "Is there another item you would like to know about?/" */
    VID_WINDOW(0xFF, 0x2F2B, 0x0C19); menu_pos = 0x302E;     /* A841..A84E */
    shop_text_ptr = 0xADEF;
    if (yes_no_prompt() == NO) return;                       /* A854/A85F */
    shop_text_ptr = 0xB17E; shop_print_text();               /* A862 "\x0CWhich item would you like to know about?/" */
    goto A763;                                               /* A86D  re-list */
}

/* ==== armr_wait_50 @ A870  (op 7) — 50 ticks (0x32) with armr_hook ========== */

/* ==== armr_crest_trade @ A880  (op 8) ====================================== */
/* Reached only from the Tumba script B2A2 ("Might I trade you a knight\s sword for it?"). */
void armr_crest_trade(void)
{
    VID_WINDOW(0xFF, 0x2F2B, 0x0C19); menu_pos = 0x302E;     /* A880..A88D */
    bool yes = yes_no_prompt() == YES;                       /* A893 */
    armr_clear_menu_area();                                  /* A899 */
    shop_text_ptr = 0xB336;                                  /* "\x0COh, I see. Well, if you change your mind, please come back.\x11" FF FF */
    if (!yes) return;                                        /* A8A3 */
    armr_draw_frame(0);                                      /* A8A6 */
    shop_text_ptr = 0xB375; shop_print_text();               /* "\x0COh, thank you, sir! As promised, here is your knight\s sword./" FF 00 "Thank you, and please come back soon.\x11" FF FF */
    P[0x92] = 4;                                             /* A8B6  Knight's sword */
    P[0x9B] = 0;                                             /* A8BB  Crest of Glory handed over */
    VID_ICON_SWORD(4, 0x18AB);                               /* A8C0..A8C5 */
    P[0xD6] &= ~0x10;                                        /* A8CA  Tumba stock ([D2+4]) loses the Knight's sword */
    P[0x24] |= 0x02;                                         /* A8CF  crest traded flag */
    KRN_LOAD(mode 4, AH = 4);                                /* A8D4..A8DA */
}
/* ==== armr_frame3 @ A8FD  (op 9) — armr_draw_frame(3) (surprised) ============ */
/* ==== armr_clear_menu_area @ A902 — VID_WINDOW(0, bx=0x2717, cx=0x1C41) ====== */
/* ==== armr_knight_secret @ A8E0 — as A47B but for the explain list; FF4C = B240 "\x0CUh....../" FF 00 */

/* ==== armr_hook @ A90F  ([A002], every frame while in the shop) ============= */
void armr_hook(void)
{
    if (!armr_anim_on) return;                               /* A90F */
    if (tick_total < 2) return; tick_total = 0;              /* A917..A91F */
    if (++armr_anim_t0 < 30) return; armr_anim_t0 = 0;       /* A925..A931 */
    armr_anim_phase++;                                       /* A936 */
    if (armr_anim_state) {                                   /* A93A */
        if (armr_anim_state == 0x7F) { armr_anim_state = 0xFF; armr_draw_frame(2); return; }   /* A941..A94F */
        if (armr_anim_state == 0x80) { armr_anim_state = 0;    armr_draw_frame(0); return; }   /* A951..A95F */
        draw 2 cells AB68[(phase&3)*2] at (x4 0x0B, y 0x37 / 0x3F);    /* A961..A981  mouth */
    } else
        draw 2 cells AAD0[(phase&3)*2] at (x4 0x10/0x11, y 0x4F);      /* A985..A9A4  eyes */
    if (KRN_RANDOM() & 1) return;                            /* A9A6..A9AD */
    if (++armr_anim_t1 < 30) return; armr_anim_t1 = 0;       /* A9B0..A9BC */
    armr_anim_state = ~armr_anim_state ^ 0x80;               /* A9C1..A9C8  0 -> 7F -> FF -> 80 -> 0 */
    armr_draw_frame(1);                                      /* A9CB */
}

/* ==== armr_draw_frame @ A9CF  (AL = frame 0..7) ============================= */
/* Two blocks per frame from table AA10 {u8 rows, u16 cellmap}: rows x 12 cells via
 * GT_DRAW_CELL starting at (x4 7, y 0x17), y += 8 per row; a rows byte of 0 ends. */

/* ======================================================================== */
/* ===== DRUGPRO.BIN (ZELRES2[15], 4646 bytes) — Witchcraft Implement shop === */
/* ======================================================================== */
/*
 * Vectors: [A000] = A004 entry, [A002] = A644 hook.  Portrait DRUG.GRP (ZELRES2[23],
 * request A811) -> arena:8000.  Same prologue shape as armrpro (no leading jmp).
 * The shop only buys/sells: item EFFECTS are applied elsewhere (select.bin item
 * menu / fight.bin) — only the shopkeeper's descriptions are known here.
 */

/* ---- overlay locals ------------------------------------------------------ */
u8   drug_main_cursor;        /* B217 */
u8   drug_list_cursor;        /* B218 */
u8   drug_anim_t;             /* B219  hook counter (20 x 2 ticks) */
u8   drug_anim_frame;         /* B21A  0..2 */
u8   drug_price_hi; u16 drug_price_lo;   /* B21B/B21C */
u8   drug_numbuf[8];          /* B21E */
u8   drug_n_stock;            /* B20E */
u8   drug_stock_list[8];      /* B20F  0-based item ids in stock (built A08C) */
struct price24 drug_price[8]; /* B1F6  this town's prices (copied A04B..A061 from drug_prices) */

/* ---- player record fields ----------------------------------------------- */
/* 0xC9..0xD1  drug_stock[9] per-town bitmask, bit7 = item 0 (Ken'ko) .. bit0 = item 7 (Feather)
 *             STDPLY: 8A A6 6B 75 42 4C 4B 01 FF                                              */
/* 0xA6..0xAA  potion_slots[5]: 0 = empty, else drug item id + 1 (A26B..A28D, A41F..A42C)      */
/*             (fight.c treats 0xA1.. as one 10-slot inventory; the drug shop only uses A6..AA — uncertain split) */

/* ---- data --------------------------------------------------------------- */
const struct req drug_portrait = {1, 0x18, "DRUG.GRP"};                        /* A811  ZELRES2[23] */
const u8  drug_label[] = {0x0E,0xAF,0x00,0x19,"Witchcraft Implement shop"};    /* A81C */
const char drug_menu[] = "Go outside\0Buy item\0Sell item\0Description of item\0";   /* A839 */
const char *drug_name[8] = {   /* B08A  ptr table -> B09A.. */
    "Ken\\ko Potion", "Juu-en Fruit", "Elixir of Kashi", "Chikara Powder",
    "Magia Stone", "Holy Water of Acero", "Sabre Oil", "Kioku Feather" };
const u8 drug_bit_of_id[8] = {0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x01};       /* A494 */
/* B10C: 9 pointers (B11E, B136, B14E, B166, B17E, B196, B1AE, B1C6, B1DE) -> 8 x {u8 hi, u16 lo} */
const u32 drug_prices[9][8] = {
 /*          Ken'ko  Juu-en  Kashi  Chikara  Magia  Acero  Sabre  Feather */
 /* cmap */ {   50,    240,    60,    320,   1000,   100,  1200,   350 },
 /* mrmp */ {   50,    240,    60,    320,   1000,   100,  1200,   350 },
 /* stmp */ {   50,    240,    60,    320,   1500,   100,  1200,   350 },
 /* bsmp */ {   50,    300,   120,    320,   1500,   100,  1200,   350 },
 /* hlmp */ {    5,    600,   240,    480,   2000,   200,  2000,   350 },
 /* tmmp */ {    5,    600,   240,    480,   2000,   200,  2000,   350 },
 /* drmp */ {    5,    900,   360,    960,   2500,   400,  2400,   350 },
 /* llmp */ {    5,    900,   360,    960,   2500,   400,  2400,   350 },
 /* prmp */ {    2,    200,    40,    280,    800,    80,  1000,   150 },
};
/* AB3A: 8 description pointers (AB4A, ABC5, AC9C, AD39, ADFD, AEA6, AF3D, AFD3), each "\x11\x0C" FF FF terminated */
/* A5E4: 8 rows x 12 cells portrait map (A5BF draws it at x4 7, y 0x17)                          */
/* A69C: 3 x 36-cell (6x6) animation frames drawn at (x4 0x0D, y 0x17) by the hook               */
/* A745/A74F/A759/A761: cell-map sequences for the enter/leave/bow animations (A708), FFFF-ended;
 *   maps A769 A785 A7A1 A7BD A7D9 A7F5 = 7 rows x 4 cells at (x4 9, y 0x1F), 40 ticks per map   */

/* ==== drug_entry @ A004  ([A000]) ========================================== */
void drug_entry(void)
{
    shop_prologue();                                 /* A004..A043 (DRUG.GRP, [B217]=0 at A031, label A81C) */
    drug_build_stock_list();                         /* A048 */
    memcpy(drug_price, drug_prices_ptr[MAP_ID - 1], 24);   /* A04B..A061  0xC words -> B1F6 */
    drug_draw_portrait();                            /* A063 -> A5BF */
    VID_WINDOW(0xFF, 0x0D60, 0x3637);                /* A066 */
    shop_text_ptr = 0xA86B;                          /* A073 "Oh... " FF 00 "hello, can I help you?/" FF 02 */
    for (;;) {                                       /* A079 */
        u8 op = shop_print_text();
        if (op == 0xFF) { VID_DISSOLVE(); return; }  /* A07E/A087 */
        drug_action[op]();                           /* A082 -> A0B8, table A0C3 */
    }
}

/* ==== drug_build_stock_list @ A08C ========================================= */
void drug_build_stock_list(void)
{
    u8 mask = P[0xC9 + MAP_ID - 1], n = 0;          /* A08C..A098 */
    for (int i = 0; i < 8; i++) if (mask & (0x80 >> i)) drug_stock_list[n++] = i;   /* A0A1..A0B1 */
    drug_n_stock = n;                                /* A0B3 -> B20E */
}

/* ==== drug_action @ A0B8  jump table A0C3 (9 entries) ====================== */
void (*drug_action[9])(void) = {
    drug_enter_anim,     /* 0  A0D5  wait 80 ticks, play sequence A745 (keeper walks in) */
    drug_leave_anim,     /* 1  A0EB  wait 80 ticks, play A74F (reverse) */
    drug_main_menu,      /* 2  A10C */
    drug_buy_list,       /* 3  A18D  rebuild list from stock, then buy menu */
    drug_buy_menu,       /* 4  A1AA  buy menu (list as is) */
    drug_sell,           /* 5  A300 */
    drug_describe,       /* 6  A4BA */
    drug_bow_anim,       /* 7  A100  play A759 */
    drug_unbow_anim,     /* 8  A106  play A761 */
};

/* ==== drug_main_menu @ A10C  (op 2) ======================================== */
void drug_main_menu(void)
{
    drug_clear_menu_area();                          /* A10C -> A5B2: VID_WINDOW(0, 0x2717, 0x1D41) */
    VID_WINDOW(0xFF, 0x2722, 0x1C2D); menu_pos = 0x2725; menu_visible_rows = menu_total = 4;   /* A10F..A127 */
    menu_draw_items(drug_menu, 4); menu_scroll = 0;  /* A12C..A137 */
    u8 bl = drug_main_cursor;
    if (menu_select(&bl) == CANCEL) bl = 0;          /* A140/A147 */
    drug_main_cursor = bl;                           /* A149 */
    switch (bl) {                                    /* A151 table A155 */
    case 0: drug_clear_menu_area(); shop_text_ptr = 0xAB0E; return;   /* A15D "\x0C" FF 07 "Thank you, sir. " FF 08 "Please come again." FF 01 "\x11" FF FF */
    case 1: shop_text_ptr = 0xA88C; return;          /* A167 "\x0CWhat are you looking for?" FF 03 */
    case 2: drug_build_sell_list();                  /* A16E -> A49C */
            shop_text_ptr = menu_total ? 0xA98D : 0xAA79; return;   /* A171..A17F "\x0CWhat would you like to sell?/" FF 05  |  "\x0CYou aren\t carrying any magic items, sir./" FF 02 */
    case 3: shop_text_ptr = 0xAAA6; return;          /* A186 "\x0CWhich item can I tell you about?/" FF 06 */
    }
}

/* ==== drug_buy_list @ A18D (op 3) / drug_buy_menu @ A1AA (op 4) ============= */
void drug_buy_list(void)
{
    memcpy(menu_item_ids, drug_stock_list, 8); menu_total = drug_n_stock; menu_scroll = 0; drug_list_cursor = 0;   /* A18D..A1A5 */
    drug_buy_menu();
}
void drug_buy_menu(void)
{
    menu_total = drug_n_stock; menu_visible_rows = min(menu_total, 3);   /* A1AA..A1B6 */
    VID_WINDOW(0xFF, 0x156E, 0x2524); menu_show_prices = 0xFF; menu_pos = 0x1571; menu_width_x4 = 0x21; price_col_x = 0x17;   /* A1B9..A1D7 */
    menu_draw_icons(menu_scroll, menu_visible_rows, drug_name /*B08A*/, drug_price /*B1F6*/);   /* A1DD..A1EC */
    u8 bl = drug_list_cursor;
    if (menu_select(&bl) == CANCEL) { shop_text_ptr = 0xA965; return; }   /* A1F5/A1FC "\x0CIs there something I can do for you?/" FF 02 */
    drug_list_cursor = bl;                                   /* A203 */
    u8 id = menu_item_ids[bl + menu_scroll];                 /* A207..A210 */
    shop_text_ptr = 0xA8C4; shop_print_text();               /* "\x0CYou\d like a " */
    print(drug_name[id]); shop_print_text();                 /* A21F..A23B  name, "./" */
    drug_price_hi/lo = drug_price[id];                       /* A240..A258  B1F6 + 3*id */
    shop_text_ptr = 0xA928;                                  /* A261 "You have no money, sir." FF 00 ... */
    if (gold_can_pay(drug_price) == INSUFFICIENT) goto done; /* A25B/A267 */
    u8 *slot = first zero in P[0xA6..0xAA];                  /* A26B..A277 */
    if (!slot) { shop_text_ptr = 0xA940; return; }           /* A27B "You can\t possibly carry any more./" FF 02 */
    P[0x85] = dl; P16[0x86] = ax;                            /* A284/A288  pay */
    *slot = id + 1;                                          /* A28B/A28D */
    shop_text_ptr = 0xA8F2; shop_print_text();               /* "That will be " */
    format_number(drug_price, drug_numbuf); print(drug_numbuf);   /* A29A..A2BA  " golds." */
done:
    shop_print_text();                                       /* A2BE */
    VID_NUM_GOLD();                                          /* A2C3 */
    shop_text_ptr = 0xA909; shop_print_text();               /* "\rWill there be something else?" */
    VID_WINDOW(0xFF, 0x2F2B, 0x0C19); menu_pos = 0x302E;     /* A2D3..A2E0 */
    bool yes = yes_no_prompt() == YES; drug_clear_menu_area();   /* A2E6..A2EC */
    shop_text_ptr = yes ? 0xA8A8 : 0xA965;                   /* A2F0..A2F9  "\x0CWhat are you looking for?" FF 04  |  main-menu prompt FF 02 */
}

/* ==== drug_build_sell_list @ A49C ========================================== */
void drug_build_sell_list(void)
{
    u8 n = 0;
    for (int i = 0; i < 5; i++) if (P[0xA6 + i]) menu_item_ids[n++] = P[0xA6 + i] - 1;   /* A49E..A4B3 */
    menu_total = n;                                          /* A4B5 */
}

/* ==== drug_sell @ A300  (op 5) ============================================= */
void drug_sell(void)
{
    drug_build_sell_list(); menu_scroll = 0; menu_visible_rows = min(menu_total, 2);   /* A300..A311 */
    VID_WINDOW(0xFF, 0x1778, 0x211A); menu_show_prices = 0; menu_pos = 0x197B; menu_width_x4 = 0x19;   /* A314..A32C */
    menu_draw_icons(menu_scroll, menu_visible_rows, drug_name);   /* A332..A33E */
    u8 bl = 0;
    if (menu_select(&bl) == CANCEL) { shop_text_ptr = 0xA965; return; }   /* A345/A34C */
    drug_list_cursor = bl;                                   /* A353 */
    shop_text_ptr = 0xA8D7; shop_print_text();               /* "\x0CYou\d like to sell a " */
    u8 id = menu_item_ids[bl + menu_scroll];                 /* A362..A36C */
    print(drug_name[id]); shop_print_text();                 /* A36E..A38A  "./" */
    drug_price = drug_price[id] >> 1;                        /* A390..A3A6  sell price = half this town's price */
    shop_text_ptr = 0xA9C4; shop_print_text();               /* "I\ll give you " */
    format_number(drug_price, drug_numbuf); print(drug_numbuf); shop_print_text();   /* A3B8..A3D5  " golds for that./Will that be all right?" */
    VID_WINDOW(0xFF, 0x3421, 0x0C19); menu_pos = 0x3524;     /* A3DA..A3E7 */
    shop_text_ptr = 0xA9FE;                                  /* "\x0COh, I see. Well, that\s the best I can do. I\m sorry it is\t satisfactory." FF 02 */
    if (yes_no_prompt() == NO) return;                       /* A3ED/A3F8 */
    gold_add(drug_price);                                    /* A3FB..A402 */
    shop_text_ptr = 0xA9AD; shop_print_text();               /* "\x0CThank you very much./" */
    P[0xA6 + k] = 0 where P[0xA6+k] == id + 1;               /* A412..A42C  remove from the potion slots */
    P[0xC9 + MAP_ID - 1] |= drug_bit_of_id[id];              /* A42F..A446  back into this town's stock */
    VID_NUM_GOLD();                                          /* A448 */
    drug_build_sell_list();                                  /* A44D */
    shop_text_ptr = 0xA966;                                  /* A450 "\x0CIs there something I can do for you?/" FF 02 */
    if (menu_total == 0) return;                             /* A456 */
    shop_text_ptr = 0xAA4B; shop_print_text();               /* "Do you have anything else you\d like to sell?" */
    VID_WINDOW(0xFF, 0x2F2B, 0x0C19); menu_pos = 0x302E;     /* A469..A476 */
    shop_text_ptr = 0xA965;
    if (yes_no_prompt() == NO) return;                       /* A47C/A487 */
    drug_clear_menu_area(); shop_text_ptr = 0xA98D;          /* A48A/A48D  "\x0CWhat would you like to sell?/" FF 05 */
}

/* ==== drug_describe @ A4BA  (op 6) ========================================= */
void drug_describe(void)
{
    drug_list_cursor = 0; menu_scroll = 0;                   /* A4BA/A4BF */
    memcpy(menu_item_ids, drug_stock_list, 8); menu_total = drug_n_stock; menu_visible_rows = min(menu_total, 2);   /* A4C4..A4E0 */
    VID_WINDOW(0xFF, 0x1778, 0x211A); menu_show_prices = 0; menu_pos = 0x197B; menu_width_x4 = 0x19;   /* A4E3..A4FB */
    menu_draw_icons(menu_scroll, menu_visible_rows, drug_name);   /* A501..A50D */
    u8 bl = drug_list_cursor;
    if (menu_select(&bl) == CANCEL) { shop_text_ptr = 0xA965; return; }   /* A516/A51D */
    drug_list_cursor = bl;                                   /* A524 */
    shop_text_ptr = 0xAACA; shop_print_text();               /* "\x0CYou\re interested in the " */
    u8 id = menu_item_ids[bl + menu_scroll];
    print(drug_name[id]); shop_print_text();                 /* A53F..A55B  "./" */
    shop_text_ptr = drug_desc[id]; shop_print_text();        /* A560..A56E  AB3A table */
    shop_text_ptr = 0xAAE9; shop_print_text();               /* "\x0CCan I tell you about anything else?" */
    VID_WINDOW(0xFF, 0x2F2B, 0x0C19); menu_pos = 0x302E;     /* A57E..A58B */
    bool yes = yes_no_prompt() == YES; drug_clear_menu_area();   /* A591..A597 */
    shop_text_ptr = 0xA965;                                  /* A59B */
    if (!yes) return;                                        /* A5A1 */
    shop_text_ptr = 0xAAA6; shop_print_text();               /* A5A4 "\x0CWhich item can I tell you about?/" */
    goto A4C4;                                               /* A5AF  re-list */
}

/* ==== drug_hook @ A644  ([A002]) =========================================== */
void drug_hook(void)
{
    if (tick_total < 2) return; tick_total = 0;              /* A644..A64C */
    if (++drug_anim_t < 20) return; drug_anim_t = 0;         /* A652..A65E */
    drug_anim_frame = (drug_anim_frame + 1) % 3;             /* A663..A66E */
    draw 6 rows x 6 cells from A69C + 36*frame at (x4 0x0D, y 0x17);   /* A671..A69B  (simmering pot? uncertain) */
}

/* ==== drug_play_anim @ A708  (SI = list of u16 cell-map ptrs, FFFF ends) ==== */
void drug_play_anim(const u16 *seq)
{
    tick = 0;                                                /* A708 */
    for (; *seq != 0xFFFF; seq++) {                          /* A70D..A711 */
        draw 7 rows x 4 cells from *seq at (x4 9, y 0x1F);   /* A717..A736 */
        while (tick < 40) drug_hook();                       /* A738..A740 */
    }
}
/* ops 0/1 (A0D5/A0EB): while (tick < 80) drug_hook(); then drug_play_anim(A745 / A74F).
 * ops 7/8 (A100/A106): drug_play_anim(A759 / A761).                                   */

/* ===== KENJPRO.BIN (ZELRES2[17], 6973 bytes) ===== */
/*
 * The sage's house ("kenja" = sage).  Door dest 2 in every town but the castle.
 * Loaded raw to BASE:A000 by town.bin 6E7E (request 6F23 {01,12,"KENJPRO.BIN"}).
 * Does NOT use the common shop prologue: it has three entry vectors:
 *   [A000] = A027  normal door entry (menu: Go outside / See Power / Listen Knowledge / Record Experience)
 *   [A002] = AB47  per-frame hook, called by town.bin idle_poll (7042) while [7C42] (portrait blink/ritual anim)
 *   [A004] = A006  DEATH entry: town.bin 61A0 jumps here after [E8] hero_dead, having pushed the
 *                  return addresses 61FC (main loop) and 6EAF (post-shop redraw).  Shows a text only.
 * Sage per town = [C006] map id (1 Marid/Muralla, 2 Yasmin/Satono, 3 Hajjar/Bosque, 4 Chiriga/Helada,
 * 5 Hisham/Tumba, 6 Maryam/Dorado, 7 Saied/Llama, 8 Indihar/Pureza).  cmap has no sage door.
 * Text engine: shop_print_text [6004]; the byte after a 0xFF terminator is dispatched by kenj_action (A0A4).
 */

/* ---- locals (BASE:BB12..BB3C) ---- */
u16  kj_portrait_pos;      /* BB12  BH x4 / BL y of the portrait: 0x0717 normal, 0x0E17 on the death entry */
u8   kj_menu_cursor;       /* BB14  last main-menu row (persists while the overlay is resident) */
u8   kj_power_done;        /* BB15  "See Power" already used in this visit */
u8   kj_spirits_gone;      /* BB16  never written -> the AE42 text ("spirits are no longer with you") is dead */
u8   kj_level_capped;      /* BB17  this sage cannot raise the level further (cap table A2AC) */
u8   kj_ritual_on;         /* BB18  hook: ritual aura animation running */
u8   kj_blink_off;         /* BB19  hook: suppress eye blink */
u8   kj_ritual_fade;       /* BB1A  hook: finishing the ritual (stops when frame counter wraps to 1) */
u8   kj_eyes_closed;       /* BB1B  blink cells 67/68 instead of 29/2A */
u8   kj_ritual_ctr;        /* BB1C */
u8   kj_ritual_frame;      /* BB1D  index into ABFF */
u8   kj_blink_state;       /* BB1E  toggled */
u8   kj_blink_timer;       /* BB1F  0..0x13 */
u8   kj_ritual_timer;      /* BB20  0..0x13 */
u16  kj_name_x;            /* BB21  = 0x60 px */
u8   kj_name_y;            /* BB23  = 0x7E */
u8   kj_list_cursor;       /* BB24  row inside the file list */
u8   kj_name_cursor;       /* BB25  edit position 0..7 */
u8   kj_name_len;          /* BB26  characters typed */
u8   kj_name[9];           /* BB27  name buffer, '`' (0x60) = empty column, 0xFF terminator */
u16  kj_new_maxhp;         /* BB34 */
u8   kj_new_magic[7];      /* BB36 */

/* ---- data ---- */
static const u8  REQ_KENJYA_GRP[] = { 1, 0x1A, "KENJYA.GRP" };          /* ACB0: ZELRES2[25] -> arena:8000, 256 cells */
static const u16 SAGE_LABEL[8]  = { 0xACCD, 0xACDF, 0xACF2, 0xAD05, 0xAD19, 0xAD2C, 0xAD3F, 0xAD51 }; /* ACBD, by [C006]-1 */
/* ACCD.. positioned labels for VID_LABEL_TEXT: {x4,y,xoff,len,text}
 *  "The Sage Marid" "The Sage Yasmin" "The Sage Hajjar" "The Sage Chiriga" "The Sage Hisham"
 *  "The Sage Maryam" "The Sage Saied" "The Sage Indihar"  (all at y=0xAF = the PLACE line) */
static const char MENU_ITEMS[] = "Go outside\0See Power\0Listen Knowledge\0Record Experience\0"; /* AD65, 4 items */

static const u8 PORTRAIT_MAP[8][12] = {  /* A9B6: 8 rows x 12 cells of KENJYA.GRP, drawn by kenj_draw_portrait (A990) */
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11}, {12,13,14,15,16,17,18,19,20,21,22,23},
    {24,25,26,27,28,29,30,31,27,32,33,34}, {35,36,37,27,38,39,40,41,27,43,44,45},
    {46,47,48,49,50,51,52,53,54,55,56,57}, {58,59,60,61,62,63,64,65,66,67,68,69},
    {70,71,72,73,74,75,76,77,78,79,80,81}, {82,83,84,85,86,87,88,89,90,91,92,93} };
static const u8 RITUAL_FRAMES[8][32];    /* AA47: 8 sub-pictures of 4 rows x 8 cells drawn at portrait+(2,16)
                                            (hands / aura); frame 0 = rest, 1..2 = raising hands, 3..7 = aura */
static const u8 BLINK_CELLS[2][2] = { {0x29,0x2A}, {0x67,0x68} };  /* ABFB open-eyes pair, ABFD closed-eyes pair, drawn at portrait+(7,24) */
static const u8 RITUAL_SEQ[8]    = { 5, 6, 7, 6, 5, 4, 3, 4 };      /* ABFF */

static const u16 EXP_NEXT[16] = { 50, 150, 300, 420, 1000, 1500, 3000, 5000,      /* A28C: EXP needed to leave level i */
                                  6000, 8000, 10000, 15000, 20000, 40000, 50000, 60000 };
static const u8  SAGE_MAX_LEVEL[8] = { 3, 6, 9, 11, 13, 15, 18, 0xFF };           /* A2AC: by [C006]-1; a sage refuses at/above this */
struct level_gain { u16 max_hp; u8 magic_max[7]; };
static const struct level_gain LEVEL_TABLE[16] = {                                /* A380: entry = level BEFORE the gain */
    {120,{12, 6, 8, 8,3,4,3}}, {160,{12, 6, 8, 8,3,4,3}}, {200,{12, 6, 8, 8,3,4,3}}, {240,{12, 6, 8, 8,3,4,3}},
    {280,{16, 6, 8, 8,3,4,3}}, {320,{20, 6, 8, 8,3,4,3}}, {380,{24, 6, 8, 8,3,4,3}}, {460,{28,12, 8, 8,3,4,3}},
    {540,{32,18,12, 8,3,4,3}}, {600,{36,24,16, 8,3,4,3}}, {640,{40,30,20,16,3,4,3}}, {680,{44,36,24,24,3,4,3}},
    {720,{48,42,28,32,3,4,3}}, {760,{52,48,36,48,9,8,6}}, {780,{56,54,44,54,15,12,9}}, {800,{60,60,60,72,21,16,12}} };
/* level >= 16: max_hp = 800 (A2DA), each magic_max += 2 saturating at 255 (A2E9) */

static const u16 POWER_VERDICT[5] = { 0xB033, 0xB069, 0xB08F, 0xB0E2, 0xB13F };   /* B029, indexed by kenj_assess() */
static const u16 KNOWLEDGE_TEXT[8] = { 0xB5FB, 0xB670, 0xB6EB, 0xB76D, 0xB81C, 0xB8B2, 0xB954, 0xB9AF }; /* B5EB, by [C006]-1 */
static const u16 GREETING_FN[8]  = { 0xAC28, 0xAC39, 0xAC4A, 0xAC5B, 0xAC6C, 0xAC7D, 0xAC8E, 0xAC9F };  /* AC18 */
static const char FILESPEC_USR[] = "*.usr";                                        /* A516 */
static const char PROMPT_NAME[]  = "Input name:\xFF";                              /* A51C */
static const u8   REQ_STDPLY[]   = { 0, 0, "STDPLY.BIN" };                         /* A907: external file, probed with mode 6 */
static const char DISK_ERROR[]   = "      Disk error.\rPlease check your disk\r  and press spacebar.\xFF"; /* B5AC */

/* ==== kenj_prologue @ A05A ==== */
void kenj_prologue(void)
{
    KRN_LOAD(mode 2, REQ_KENJYA_GRP, arena:0x8000);            /* A05A..A066: 256 portrait cells */
    VID_CONVERT_CELLS(arena:0x8000, 0x100);                    /* A06B..A077 */
    shop_text_x = 0; shop_text_line = 0;                       /* FF4E/FF4F */
    VID_CLEAR_PLAYFIELD(); VID_ENEMY_TROUGH();                 /* A087, A08C */
    VID_LABEL_TEXT(SAGE_LABEL[map_id - 1]);                    /* A091..A09F: [C006] */
}

/* ==== kenj_draw_portrait @ A990 ==== */
void kenj_draw_portrait(void)                                  /* 8 rows x 12 cells at kj_portrait_pos via GT_DRAW_CELL */
{
    for (row = 0; row < 8; row++) for (col = 0; col < 12; col++)
        GT_DRAW_CELL(PORTRAIT_MAP[row][col], BH = pos.x + col, BL = pos.y + row * 8);
}

/* ==== kenj_draw_ritual_frame @ AA16 ==== */
void kenj_draw_ritual_frame(u8 al)                             /* 4 rows x 8 cells at (pos.x+2, pos.y+0x10) */
{
    const u8 *p = RITUAL_FRAMES[al];                           /* AA16: al*0x20 + AA47 */
    for (row = 0; row < 4; row++) for (col = 0; col < 8; col++) GT_DRAW_CELL(*p++, pos.x + 2 + col, pos.y + 0x10 + row * 8);
}

/* ==== kenj_entry_death @ A006  (vector [A004]) ==== */
void kenj_entry_death(void)
{
    kenj_prologue();                                           /* A006 */
    kj_portrait_pos = 0x0E17;                                  /* A009: portrait moved right (x4=0x0E) */
    kenj_draw_portrait();                                      /* A00F */
    VID_WINDOW(0xFF, BX = 0x0D60, CX = 0x3637);                /* A012: text box */
    shop_text_ptr = 0xBA67;                                    /* A01F: "While you were unconscious, the spirits brought you here./"
                                                                  "\xFF\x04\xFF\x04Be careful not to exhaust yourself in battle./\xFF\x04"
                                                                  "Now be on your way. \xFF\x04The spirits are looking after you. \x11\xFF\xFF" */
    goto text_loop;                                            /* A025 -> A047.  NOTE: no player-record byte is touched here;
                                                                  the death penalties are applied by fight.bin 99AD..99DB before
                                                                  the swap (gold=0, almas/=2, hp=max_hp, [C4]=[C5]) */
}

/* ==== kenj_entry @ A027  (vector [A000]) ==== */
void kenj_entry(void)
{
    kenj_prologue();                                           /* A027 */
    kj_portrait_pos = 0x0717;                                  /* A02A */
    kenj_draw_portrait();                                      /* A030 */
    VID_WINDOW(0xFF, 0x0D60, 0x3637);                          /* A033 */
    shop_text_ptr = kenj_greeting();                           /* A040..A043 */
text_loop:                                                     /* A047 */
    for (;;) {
        al = shop_print_text();                                /* [6004] */
        if (al == 0xFF) break;                                 /* A04C: FF FF = leave */
        kenj_action(al);                                       /* A050 */
    }
    VID_DISSOLVE();                                            /* A055: jmp [2040] -> returns to town.bin 6EAF */
}

/* ==== kenj_greeting @ AC07 ==== */
/* First visit to a given sage (player-record byte 0xE5, one bit per sage: Marid 0x80, Yasmin 0x40, Hajjar 0x20,
 * Chiriga 0x10, Hisham 0x08, Maryam 0x04, Saied 0x02, Indihar 0x01) -> the introduction text, which ends with
 * "\xFF\x07".."\xFF\x0D" = teach magic 1..7 (Yasmin..Indihar), then "\xFF\x00" = main menu.  Otherwise AD9D. */
const u8 *kenj_greeting(void)
{
    si = 0xAD9D;                                               /* "\x0CHow can I help you, Brave One?/\xFF\x00" */
    GREETING_FN[map_id - 1]();                                 /* AC0A..AC14: each is  if (!(met_sages & bit)) { si = intro; met_sages |= bit; } */
    return si;                                                 /* intro texts: B1B8 Marid, B22D Yasmin, B29F Hajjar, B317 Chiriga,
                                                                  B38C Hisham, B400 Maryam, B488 Saied, B51E Indihar */
}

/* ==== kenj_action @ A0A4 ==== dispatch on the byte after 0xFF (table A0AF) */
void kenj_action(u8 op)
{
    switch (op) {
    case 0x00: kenj_main_menu();      break;   /* A0CB */
    case 0x01: kenj_see_power();      break;   /* A18E */
    case 0x02: kenj_ask_continue();   break;   /* A914 */
    case 0x03: kenj_save_file();      break;   /* A862 */
    case 0x04: kenj_wait_140();       break;   /* A410: 0x8C ticks while running the hook */
    case 0x05: kenj_level_up();       break;   /* A2B4 */
    case 0x06: shop_text_ptr = 0xADBF; break;  /* A420: "\x0CIs there anything else I can do for you?/\xFF\x00" */
    case 0x07 ... 0x0D: kenj_teach_magic(op - 6); break;  /* A93B..A953 */
    }
}

/* ==== kenj_clear_menu_area @ A983 ==== */  void kenj_clear_menu_area(void) { VID_WINDOW(0, BX = 0x2717, CX = 0x1D41); }

/* ==== kenj_main_menu @ A0CB ==== */
void kenj_main_menu(void)
{
    kenj_clear_menu_area();
    VID_WINDOW(0xFF, 0x2722, 0x1C2D);                          /* A0CE */
    menu_pos = 0x2725; menu_visible_rows = 4; menu_total = 4;  /* A0DB..A0E6 */
    menu_draw_items(MENU_ITEMS, 4);                            /* A0EB..A0F1 [600E] */
    menu_scroll = 0;
    bl = kj_menu_cursor;                                       /* A0FB */
    if (menu_select(&bl) == CANCEL) bl = 0;                    /* A0FF..A106 [6010]: Alt = "Go outside" */
    kj_menu_cursor = bl;
    switch (bl) {                                              /* table A114 */
    case 0: kenj_clear_menu_area(); shop_text_ptr = 0xADEB; break;          /* A11C "\x0CThe Spirits are with you.\x11\xFF\xFF" */
    case 1: kenj_clear_menu_area();                                         /* A126 */
        if (!kj_power_done)          shop_text_ptr = 0xAE08;                /* "\x0CI shall call upon the Spirits and their powers..... /\xFF\x04\xFF\x01" */
        else if (kj_spirits_gone)    shop_text_ptr = 0xAE42;                /* dead: BB16 never set */
        else                         shop_text_ptr = kj_level_capped ? 0xAF03 : 0xAEA7;
        /* AEA7 "\x0CYou are brave, but your experience is lacking. Come back when you have accomplished more.\xFF\x00"
           AF03 "\x0CI can no longer impart the power of the Spirits to you. Continue on your quest. You will soon find others to help you.\xFF\x00" */
        break;
    case 2: kenj_clear_menu_area();                                         /* A157 Listen Knowledge */
        shop_text_ptr = KNOWLEDGE_TEXT[map_id - 1]; shop_print_text();      /* each ends "\x11\xFF\x00"; the returned 0 is ignored */
        shop_text_ptr = 0xADBF; break;
    case 3: kenj_clear_menu_area();                                         /* A178 Record Experience */
        if (kenj_pick_name() == CANCEL) { shop_text_ptr = 0xADBF; break; }  /* A17B..A186 */
        shop_text_ptr = 0xAF7C;                                             /* "\x0CI shall record your experiences./\xFF\x03"
                                                                               "Place is saved on user disk. Will you continue your quest?\xFF\x02\xFF\x06" */
        break;
    }
}

/* ==== kenj_ask_continue @ A914  (action 2) ==== */
void kenj_ask_continue(void)
{
    VID_WINDOW(0xFF, 0x2B2F, 0x0C19); menu_pos = 0x302E;       /* A914..A921 */
    cf = yes_no_prompt();                                      /* A927 [6008] */
    kenj_clear_menu_area();
    if (cf) { ax = 0; jmp far [FF00]; }                        /* A934: "No" -> clean exit to DOS through the loader */
}

/* ==== kenj_save_file @ A862  (action 3) ==== THE SAVE ROUTINE */
void kenj_save_file(void)
{
    /* build "<name>.usr": copy player_name (FF6C, up to 8 chars, stops at 0) then ".usr\0" (A864..A888) */
    strcpy(kj_name, player_name); strcat(kj_name, ".usr");
    h = dos_create(kj_name, attr 0);                           /* A88D..A895: INT 21h AH=3Ch, CX=0 */
    if (!cf) {
        dos_write(h, DS:0000, 0x100);                          /* A89A..A8A4: AH=40h, DX=0, CX=0x100 -> BASE:0000..00FF */
        cf = dos_close(h);                                     /* A8AA: AH=3Eh (CF of the write is kept, A8A7/A8AE) */
        if (!cf) return;
    }
    /* disk error: save rect, frame, message, wait for Space, restore, redraw text box, retry the name dialog */
    VID_SAVE_RECT(AX = 0x0849, CX = 0x1926, DI = 0);           /* A8B2 */
    VID_WINDOW(0xFF, 0x1049, 0x3226); VID_PUTS(DISK_ERROR, BX = 0x4C, CL = 0x50);
    wait_space();                                              /* A8D9..A8E3 (FF1D) */
    VID_RESTORE_RECT(0x0849, 0x1926, 0);
    VID_WINDOW(0xFF, 0x0D60, 0x3637);
    goto A178;                                                 /* A904: back to "Record Experience" */
}

/* ==== kenj_pick_name @ A427 ==== list *.usr files, let the player pick or type a name; CF=1 cancel */
bool kenj_pick_name(void)
{
    KRN_LOAD(mode 6, REQ_STDPLY);                              /* A427: probe STDPLY.BIN -> forces the "insert disk 1" prompt if the
                                                                  game disk is not in the drive (result unused) */
    KRN_FIND_FILES(ES:DI = BASE:E000, "*.usr");                /* A433..A43F: E000 count, E001 ptr[255], E201 names */
    VID_WINDOW(0xFF, 0x0D60, 0x3637); VID_WINDOW(0xFF, 0x0D60, 0x2637);  /* A444..A459 */
    memset(kj_name, '`', 8); kj_name[8] = 0xFF; kj_name_cursor = 0;      /* A45E..A46D */
    copy player_name (until 0, max 8) into kj_name, counting into kj_name_cursor;   /* A472..A485 */
    kj_name_len = kj_name_cursor;                              /* A487 */
    VID_PUTS(PROMPT_NAME, BX = 0x3C, CL = 0x6C);               /* A48D..A495 "Input name:" at (60,108) */
    kj_name_x = 0x60; kj_name_y = 0x7E;                        /* A49A..A4A0 */
    menu_pos = 0x3463; menu_width_x4 = 0x0A;                   /* A4A5..A4AB */
    n = min(file_count, 5);                                    /* A4B1..A4B8 */
    if (n) kenj_draw_file_rows(0, E001, n);                    /* A4C5 -> A528: GT_MENU_LINE(al) + GT_MENU_BLIT at menu_pos+0x300+10*row */
    menu_total = file_count; menu_visible_rows = 5;            /* A4CB..A4D1 */
    cf = kenj_name_edit(E001);                                 /* A4D6 -> A559 */
    VID_WINDOW(0xFF, 0x0D60, 0x3637);                          /* A4DA (flags preserved) */
    if (cf) return CANCEL;                                     /* A4E8 */
    memset(player_name, 0, 8);                                 /* A4EB..A4F5 */
    if (kj_name_len == 0) return CANCEL;                       /* A4F7..A4FF: empty name = cancel */
    copy kj_name to player_name until 0xFF or '`';             /* A500..A514 */
    return OK;
}

/* ==== kenj_name_edit @ A559 ==== keyboard/joystick loop; CF=0 Enter, CF=1 Alt */
bool kenj_name_edit(const u16 *names)
{
    text_entry_mode = 0xFF; last_ascii = 0; btn1_edge = btn2_edge = 0; menu_scroll = 0; kj_list_cursor = 0;  /* A559..A577 */
    if (menu_total) cursor_draw(0);                            /* A57E..A585 [6014] */
    kenj_draw_name(); kenj_move_cursor(0);                     /* A58A, A58F */
    for (;;) {                                                 /* A592 */
        idle_poll(); tick = 0;                                 /* [6016] */
        if (btn2_edge) { cf = 1; break; }                      /* A59C: Alt/B cancels */
        if (key_mask & 1) { cf = 0; break; }                   /* A5A4: Enter accepts */
        if (btn1_edge) {                                       /* A5B9: Space = copy the highlighted file name into the buffer */
            src = names[menu_scroll + kj_list_cursor];         /* A5C3..A5CD */
            memset(kj_name, '`', 8); kj_name[8] = 0xFF; kj_name_cursor = 0;
            copy src until 0 (max 8) -> kj_name, counting kj_name_cursor; kj_name_len = kj_name_cursor;
            btn1_edge = 0;
            VID_WINDOW(0, BH = kj_name_x/4, BL = kj_name_y, CX = 0x1010);   /* A601..A613 erase name field */
            kenj_draw_name(); kenj_move_cursor(0); continue;
        }
        if (last_ascii) {                                      /* A627 */
            c = last_ascii; last_ascii = 0;
            if (c == 0x0D) continue;                           /* A636 */
            if (c == 0x08) { kenj_backspace(); continue; }     /* A63B -> A827 */
            if (kj_name[kj_name_cursor] == '`') kj_name_len++; /* A642..A64F */
            kj_name[kj_name_cursor] = c;                       /* A653 */
            kenj_draw_name(); kenj_move_cursor(+1); continue;  /* A657..A65C */
        }
        dirs = INT61.AL;                                       /* A65F */
        if (dirs & 8) { kenj_move_cursor(+1); wait release; last_ascii = 0; continue; }   /* A661..A675 right */
        if (dirs & 4) { kenj_move_cursor(-1); wait release; last_ascii = 0; continue; }   /* A676..A68A left */
        if (!menu_total) continue;                             /* A68B */
        if ((dirs & 3) == 1) {                                 /* A693 up */
            if (kj_list_cursor) { cursor_up_anim(kj_list_cursor); kj_list_cursor--; }      /* A699..A6AD [6018] */
            else if (menu_scroll) { menu_scroll--; GT_MENU_LINE(menu_scroll + kj_list_cursor); scroll list down 10 px via GT_MENU_SCROLL_UP loop A6C8..A704; }
        } else if ((dirs & 3) == 2) {                          /* A709 down */
            if (kj_list_cursor + menu_scroll + 1 <= menu_total - 1) {
                if (kj_list_cursor < menu_visible_rows - 1) { cursor_down_anim(kj_list_cursor); kj_list_cursor++; }   /* A72D [601A] */
                else { menu_scroll++; GT_MENU_LINE(menu_scroll + kj_list_cursor); scroll list up via GT_MENU_SCROLL_DOWN loop A74D..A78B; }
            }
        }
    }
    text_entry_mode = 0; btn2_edge = 0;                        /* A5AE..A5B3 */
    return cf;
}

/* ==== kenj_move_cursor @ A790 ==== AL = delta (0, 1, -1); clamps to 0..7 and to kj_name_len; draws '_'-style marker (char 0x7F, colour 6) */
void kenj_move_cursor(s8 d)
{
    VID_WINDOW(0, BH = kj_name_x/4 + kj_name_cursor*2, BL = kj_name_y + 8, CX = 0x0208);   /* A792..A7AE erase old marker */
    kj_name_cursor += d; if (kj_name_cursor & 0x80) kj_name_cursor = 0;   /* A7B4..A7BF */
    if (kj_name_cursor >= 8) kj_name_cursor--;                 /* A7C4 */
    if (kj_name_cursor > kj_name_len) kj_name_cursor = kj_name_len;   /* A7CF..A7D8 */
    VID_PUTCHAR(0x7F, colour 6, BX = kj_name_x + kj_name_cursor*8, CL = kj_name_y + 8);   /* A7DB..A7F6 */
}
/* ==== kenj_draw_name @ A7FD ==== clears 16x8 at (kj_name_x, kj_name_y) and VID_PUTS(kj_name) */
/* ==== kenj_backspace @ A827 ==== deletes the char before the cursor (shifts the tail left, pads '`' at [7]), len--, cursor-- */

/* ==== kenj_see_power @ A18E  (action 1) ==== */
void kenj_see_power(void)
{
    kj_power_done = 0xFF;                                      /* A18E */
    kenj_ritual_raise();                                       /* A1D1: frames 0,1,2 at 25 ticks each, eyes closed, blink off */
    kenj_wait_140();                                           /* A196 */
    kj_ritual_on = kj_blink_off = 0xFF;                        /* A199..A19E: hook cycles RITUAL_SEQ every 20 ticks */
    shop_text_ptr = 0xAFDE;                                    /* "\x13\xFF\x04Oh, Holy Spirits, purify my thoughts and grant me strength. \xFF\x04\xFF\x04\r\x15\xFF\x00..." */
    do { kenj_wait_140(); al = shop_print_text(); } while (al == 4);   /* A1A9..A1B3 */
    kj_ritual_fade = 0xFF;                                     /* A1B5 */
    kenj_ritual_lower();                                       /* A200: frames 2,1 back down */
    shop_print_text();                                         /* A1BD (prints nothing: pointer already past "\xFF\x00") */
    shop_text_ptr = POWER_VERDICT[kenj_assess()];              /* A1C2..A1CD */
    /* B033 "Your experience is lacking. Persevere in your quest.\xFF\x00"
       B069 "You must accumulate more experience.\xFF\x00"
       B08F "I can see the faint light of the Spirits in you. You must endure a little longer.\xFF\x00"
       B0E2 "The light of the Spirits is bursting forth within you. \xFF\x04\rIndeed, your power has grown.\xFF\x05\xFF\x04\xFF\x00"
       B13F "I can no longer impart the power of the Spirits to you. Continue on your quest. You will soon find others to help you. \xFF\x00" */
}

/* ==== kenj_assess @ A22E ==== 0..4 from EXP vs. the threshold of the current level */
u8 kenj_assess(void)
{
    lvl = min(level, 15); need = EXP_NEXT[lvl];                /* A230..A241 */
    if (exp < need / 2)          return 0;                     /* A247..A24F */
    if (exp < need - need / 4)   return 1;                     /* A250..A261 */
    if (exp < need)              return 2;                     /* A262..A26B */
    if (level < SAGE_MAX_LEVEL[map_id - 1]) return 3;          /* A26C..A282 */
    kj_level_capped = 0xFF; return 4;                          /* A283..A28B */
}

/* ==== kenj_level_up @ A2B4  (action 5) ==== */
void kenj_level_up(void)
{
    for (i = 0; i < 8; i++) { GT_INIT_PLAYFIELD(); wait 10 ticks; }   /* A2B7..A2CF: XOR-flash of the playfield */
    if (level >= 16) { kj_new_maxhp = 800; for (i = 0; i < 7; i++) kj_new_magic[i] = sat_add(magic_max[i], 2); }  /* A2D6..A2F3 */
    else memcpy(&kj_new_maxhp, &LEVEL_TABLE[level], 9);        /* A2F5..A304 */
    if (level != 0xFF) level++;                                /* A306..A30F (0x8D) */
    max_hp = hp = kj_new_maxhp;                                /* A312..A318 (0xB2, 0x90) */
    VID_LIFE_BAR_MAX(); VID_LIFE_BAR_CUR();                    /* A31B, A320 */
    memcpy(magic_max, kj_new_magic, 7); memcpy(magic_count, kj_new_magic, 7);   /* A325..A33B (0xB4, 0xAB): refill */
    if (magic_sel) VID_NUM_ITEM_COUNT();                       /* A33D..A344 */
    exp -= EXP_NEXT[min(level - 1, 15)];                       /* A349..A35E */
    if (exp >= EXP_NEXT[min(level, 15)]) exp = EXP_NEXT[min(level, 15)] - 1;   /* A362..A37C: never more than one level per visit */
}

/* ==== kenj_teach_magic @ A957  (actions 7..0D -> AL = 1..7) ==== */
void kenj_teach_magic(u8 n)
{
    VID_GAUGE_BAR(0, BX = 0xAA1C, CH = 0x17);                  /* A958..A95F: clear the magic HUD slot */
    magic_sel = n;                                             /* A965 (0x9D) */
    magic_known[n - 1] = 0xFF;                                 /* A968..A96E: player record 0xBB..0xC1 (new: "spell learned" flags) */
    VID_ICON_ITEM(magic_sel, BX = 0x37A4); VID_NUM_ITEM_COUNT();   /* A973..A97E */
}

/* ==== kenj_wait_140 @ A410 ==== 0x8C ticks, running the hook */
/* ==== kenj_ritual_raise @ A1D1 / kenj_ritual_lower @ A200 ==== frames {0,1,2} / {2,1} at 25 ticks, kj_blink_off/kj_eyes_closed */

/* ==== kenj_hook @ AB47  (vector [A002], from town.bin idle_poll while [7C42]) ==== */
void kenj_hook(void)
{
    if (tick_total < 2) return; tick_total = 0;                /* AB47..AB4F: every 2 ticks (FF50) */
    if (kj_ritual_on) {
        if (kj_ritual_fade) {                                  /* AB5C: finish on the next wrap of the 16-step counter */
            kj_ritual_ctr = (kj_ritual_ctr + 1) & 15;
            if (kj_ritual_ctr == 1) kj_ritual_on = kj_ritual_fade = kj_ritual_ctr = kj_ritual_frame = 0;
        } else if (++kj_ritual_timer >= 0x14) {               /* AB89..AB92 */
            kj_ritual_timer = 0; kj_ritual_frame++;
            kenj_draw_ritual_frame(RITUAL_SEQ[(kj_ritual_frame - 1) & 7]);   /* ABA2..ABAD */
            kj_ritual_ctr = (kj_ritual_ctr + 1) & 15;
        } else return;
    }
    if (kj_blink_off) return;                                  /* ABB9 */
    if (++kj_blink_timer < 0x14) return; kj_blink_timer = 0;   /* ABC1..ABCD */
    bl = kj_blink_state & 1; kj_blink_state = ~kj_blink_state; /* ABD2..ABDA */
    GT_DRAW_CELL(BLINK_CELLS[kj_eyes_closed ? 1 : 0][bl], BX = kj_portrait_pos + 0x0718);   /* ABDF..ABF6: cell at (x+7, y+24) */
}
