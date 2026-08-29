/*
 * enddemo.c — hand-cleaned decompilation of ENDDEMO.BIN (ZELRES2[50], 8683
 * bytes, image 6000..81EB): the **ending** — the reunion scenes with their
 * dialogue, then the typewriter credits roll with the boss list.  Companion:
 * docs/CUTSCENES.md §4.
 *
 * NOT COMPILABLE — pseudo-C from disasm/overlays/enddemo.asm (origin 6002).
 * Every routine carries its original address.
 *
 * Loading and calling convention
 * ------------------------------
 * Slot-A overlay with a one-word vector header, `[6000] = 6002`.  It is
 * launched from **omoypro.bin** (ZELRES2[11], the Princess' chamber — see
 * src/shops.c "OMOYPRO"): when the player enters the hut with the end-game
 * flag `[0x49]` set, that overlay
 *
 *     A061  load ENDDEMO.BIN            (AL=3) -> BASE:6000
 *     A072  load gd{ega,cga,hgc,mcga,tga}.bin per [FF14] -> BASE:3000
 *     A088  wait until [FF50] >= 0x12C  (300 ticks)
 *     A09E  [3006] dissolve the whole screen
 *     A0A3  [FF77] = 0xFF               (demo palette mode)
 *     A0A9  jmp [0x6000]
 *
 * so the gd renderer is only resident while the ending runs.  enddemo never
 * returns: after the credits it spins in the hotkey poller (66C8) and the
 * player leaves with Ctrl+Q.
 *
 * Two acts, each begun by `cli / mov sp,0x2000 / sti`:
 *   6002  act 1 — seven picture beats, each followed by a run of the
 *                 **narration engine at 6318** over the script at 6AA8
 *   6638  act 2 — the credits, typed out to zend.msd, synchronised to the score
 *
 * Act 1 is *not* silent.  `6318` is a second, five-speaker copy of opdemo's
 * proportional-font narration engine (src/opdemo.c `play_narration`, 6A80),
 * and the seven calls to it at `60FD 6142 6172 61FA 622F 62A6 62DB` are what
 * play the ending's dialogue: "At long last, Jashiin was destroyed and the
 * nine Tears of Esmesanti were returned to their rightful place…" through
 * Felicia's closing "Until then, I can only believe it, and wait for him."
 * Its metric tables are enddemo's own copies at `807D` / `80DD`.
 *
 * The two unpackers (696D mask+delta, 69F0 RLE) and all the geometry
 * conventions are identical to opdemo's; see src/opdemo.c and
 * docs/CUTSCENES.md §6.  `BH` is x in 1/80ths of the screen (x4), `BL` is y,
 * `CH` is the width in plane bytes and `CL` the row count, so `mov bx,0x1D12`
 * means BH = 0x1D (x = 116) and BL = 0x12 (y = 18).
 */

typedef unsigned char u8;
typedef unsigned short u16;

/* gdmcga slots — same table as src/opdemo.c */
#define GD_DRAW3       (*(void(*)())0x3004)
#define GD_ERASE       (*(void(*)())0x3006)
#define GD_PALETTE     (*(void(*)())0x3008)
#define GD_DRAW3_FAST  (*(void(*)())0x3010)
#define GD_DRAW_MASKED (*(void(*)())0x3022)   /* AL = plane-present mask     */
#define GD_BOX         (*(void(*)())0x3024)
#define GD_WIPE        (*(void(*)())0x3028)   /* horizontal aperture wipe    */
#define GD_END_OPEN    (*(void(*)())0x302A)   /* end6: 57 steps opening      */
#define GD_END_CLOSE   (*(void(*)())0x302C)   /* end6: the same, closing     */
#define GD_CURSOR      (*(void(*)())0x302E)   /* AL colour, BH x4, BL y — 8x8 */
#define GD_PUTCHAR     (*(void(*)())0x3030)
#define GD_FX_20       (*(void(*)())0x3020)
#define LOAD_RES       (*(void(*)())0x10C)
#define VID_WINDOW     (*(void(*)())0x2000)
#define INT60H_PLAY(ax)

/* ---- request blocks, 813D..81EB ---------------------------------------- */
static const u8 req_waku [] = { 0, 0x21, "waku.grp" };  /* 813D             */
static const u8 req_sei  [] = { 0, 0x1C, "sei.grp"  };  /* 8148 the Spirit  */
static const u8 req_yuup [] = { 0, 0x26, "yuup.grp" };  /* 8152 Garland     */
static const u8 req_seip [] = { 0, 0x1D, "seip.grp" };  /* 815D Spirit port.,
                                              tail = the 0x8n lip-sync bank */
static const u8 req_himp [] = { 0, 0x11, "himp.grp" };  /* 8168 Felicia port.,
                                              tail = the 0xBn lip-sync bank */
static const u8 req_new1 [] = { 0, 0x18, "new1.grp" };  /* 8173 Felicia, full length */
static const u8 req_new2 [] = { 0, 0x19, "new2.grp" };  /* 817E King + Felicia */
static const u8 req_ne80 [] = { 0, 0x15, "ne80.grp" };  /* 8189 Garland     */
static const u8 req_ne81 [] = { 0, 0x16, "ne81.grp" };  /* 8194 Felicia     */
static const u8 req_end5 [] = { 1, 0x36, "end5.grp" };  /* 819F the castle  */
static const u8 req_end4 [] = { 1, 0x35, "end4.grp" };  /* 81AA the ride    */
static const u8 req_end6 [] = { 1, 0x37, "end6.grp" };  /* 81B5 the balcony */
static const u8 req_end7 [] = { 1, 0x38, "end7.grp" };  /* 81C0 the landscape (2 planes) */
static const u8 req_en72 [] = { 1, 0x34, "en72.grp" };  /* 81CB end7's third plane */
static const u8 req_fin  [] = { 1, 0x39, "fin.grp"  };  /* 81D6 the FIN stencil */
static const u8 req_zend [] = { 0, 0x27, "zend.msd" };  /* 81E0             */

extern const u8 narration[];        /* 6AA8 — see play_narration()          */
extern const u8 credits_script[];   /* 787E — see run_script()              */
extern const u8 glyph_bearing[];    /* 807D per-char left offset (indexed from ' ') */
extern const u8 glyph_width[];      /* 80DD per-char advance                */
extern const u8 char_map[];         /* 7F55 — ASCII -> glyph, via [0x3030]  */

/* the two lip-sync banks that live *inside* the overlay image (see below) */
extern const u8 a_mouths[3][0xA5];  /* 7437 */
extern const u8 a_eyes  [3][0xA8];  /* 7626 */
extern const u8 c_frames[][0x30];   /* 781E */

/* ---- scratch ------------------------------------------------------------ */
/* act 1 / the narration engine (6630..6637) */
static const u8 *narr_p;    /* 6630  narration script pointer (init 0x6AA8) */
static u16 narr_x;          /* 6632  proportional pen x                     */
static u8  narr_line;       /* 6634  text line 0..3                         */
static u8  narr_shadow;     /* 6635  shadow colour                          */
static u8  narr_ink;        /* 6636  text colour                            */
static u8  narr_click;      /* 6637  per-character sfx id (0 = silent)      */
/* act 2 / the credits typewriter (6965..696C) */
static const u8 *script_p;  /* 6965  credits script pointer (init 0x787E)   */
static u8 cur_col;          /* 6967 */
static u8 cur_row;          /* 6968 */
static u16 pause_ticks;     /* 6969 */
static u8 char_delay;       /* 696B */
static u8 scene_i;          /* 696C */

static void poll_hotkeys(void) { /* 62FF / 6956 — [0x110] [0x112] (+ [0x116] [0x118] in act 1) */ }

static void wait_ticks(u8 n)                                  /* 62EE / 6945 */
{
    do poll_hotkeys(); while (tick /*FF1A*/ < n);
    tick = 0;
}

/* =======================================================================
 * 6318 — the ending's own copy of the storm-demo narration engine
 * (src/opdemo.c 6A80).  Same proportional font, same word wrap, same one
 * byte every 0x10 ticks, same 0xEB..0xFF control set — but **five** lip-sync
 * speaker groups instead of two, and no abort keys (enddemo's wait loop does
 * not test [FF1D]/[FF29]).
 *
 * Unlike opdemo's, its result is discarded: 0xFF ends the *script*, not the
 * act, and act 1 falls into the credits at 62EB regardless.
 *
 *   0x20..0x7F  a character.  x = narr_x + 4 - glyph_bearing[c-0x20],
 *               y = narr_line*10 + 0x8F; drawn twice, shadow at (x+1,y+1) in
 *               [6635] then ink at (x,y) in [6636]; narr_x += width.  Unless
 *               it is one of ` . , " '` it also fires [FF75] = narr_click.
 *               On a space, 65F0 measures the next word and starts a new line
 *               if narr_x + width >= 0x138.
 *   0x8n .. 0xCn  lip-sync (below).  Costs **no** ticks: all five handlers
 *               jump back to the fetch at 6323, skipping the wait at 631E.
 *   0xEB..0xF0  set the per-character click sfx (0x41,0x40,0x3F,0x3E,0x3D,0)
 *   0xF1 F2 F3 F7  start line 3 / 2 / 1 / 0 and reset x
 *   0xF5        pause 0xF0 ticks;  0xF6  three of those
 *   0xF9 FA FB  ink/shadow = (6,2) yellow / (7,0) magenta / (7,1) white
 *   0xFD        return — end of this beat, the caller puts up the next picture
 *   0xFE        clear the text box ([0x2000] at (0,143), 320 x 57), line 0
 *   0xFF        return — end of script
 *
 * The five speaker groups.  Each draws a 3-plane frame with GD_DRAW3_FAST;
 * BH/BL are the literal `mov bx,...` operands, CH/CL the `mov cx,...` ones:
 *
 *   0x8n  6568  the **Spirit** — seip.grp @ arena:8000.  No mouth/eye split:
 *               every n indexes one bank of 7 x 24 frames, 504 bytes apart,
 *               at arena:98C0 (= the portrait + 0x18C0), BX = 0x3850
 *               (x 224, y 80), CX = 0x0718.
 *   0x9n  64E3  **Garland** — yuup.grp @ arena:4000, exactly opdemo's 0x9n:
 *               n < 6  mouth 9 x 32, arena:58C0 + n*864,   BX = 0x1350
 *               n >= 6 eyes 11 x 16, arena:6D00 + (n-6)*528, BX = 0x1238
 *   0xAn  6530  **Felicia in the new1/new2 tableau** — the only speaker whose
 *               frames live in the overlay image itself (ES = CS):
 *               n < 3  7437 + n*0xA5,     5 x 11, BX = 0x3548 (x 212, y 72)
 *               n >= 3 7626 + (n-3)*0xA8, 7 x  8, BX = 0x343E (x 208, y 62)
 *   0xBn  658C  **Felicia's portrait** — himp.grp @ arena:8000:
 *               n < 6  mouth  9 x 24, arena:98C0 + n*648,     BX = 0x3450
 *               n >= 6 eyes  10 x 24, arena:A7F0 + (n-6)*720, BX = 0x3338
 *   0xCn  65D5  **Felicia on the balcony** (the ne80/ne81 tableau), again out
 *               of the image: 781E + n*0x30, 2 x 8, BX = 0x3840 (x 224, y 64).
 *
 * arena:98C0 and arena:A7F0 are the portrait base 0x8000 plus 0x18C0 and
 * 0x27F0 — i.e. the "unidentified tails" of seip.grp and himp.grp are these
 * banks, laid out exactly like yuup.grp's and oup.grp's (docs/CUTSCENES.md
 * §6.3).  Which portrait 0x8n / 0xBn address depends on which one was loaded
 * last: beat 4 speaks over seip, beats 5-7 over himp.
 * ======================================================================= */
void play_narration(void)                                           /* 6318 */
{
    tick = 0;                                                     /* 6318   */
    int skip_wait = 0;
    for (;;) {
        if (!skip_wait) wait_ticks(0x10);                         /* 631E   */
        skip_wait = 0;
        u8 c = *narr_p++;                                         /* 6329   */
        if (c & 0x80) {                                           /* 632E   */
            if (c == 0xFF || c == 0xFD) return;              /* 63C4/63C9   */
            if ((c & 0xF0) >= 0x80 && (c & 0xF0) <= 0xC0) {  /* 63D3..63F8  */
                draw_lipsync_frame(c);        /* 6568 64E3 6530 658C 65D5   */
                skip_wait = 1;                /* … all jump back to 6323    */
                continue;
            }
            narration_control(c);                                 /* 63FB   */
            continue;
        }

        if (c != ' ' && c != '.' && c != ',' && c != '"' && c != '\'')
            sfx /*FF75*/ = narr_click;                            /* 6349   */

        u16 x = narr_x + 4 - glyph_bearing[c - 0x20];             /* 6352   */
        u8  y = narr_line * 10 + 0x8F;                            /* 6359   */
        GD_PUTCHAR(c, narr_shadow, x + 1, y + 1);                 /* 6381   */
        GD_PUTCHAR(c, narr_ink,    x,     y);                     /* 638D   */
        narr_x += glyph_width[c - 0x20];                          /* 63A0   */

        if (c == ' ' &&                                           /* 63A4   */
            narr_x + measure_word(narr_p) >= 0x138)          /* 63AF/65F0   */
            { narr_x = 0; narr_line++; }                          /* 64AB   */
    }
}

/* =======================================================================
 * ACT 1 — 6002.  Seven picture beats; after each one the narration engine
 * runs the next chunk of the script at 6AA8 until it hits 0xFD.
 * ======================================================================= */
void act1_reunion(void)                                             /* 6002 */
{
    SP = 0x2000;
    narr_p = narration;                            /* 6007 [6630] = 0x6AA8  */
    GD_PALETTE(6);

    /* 1. Garland's portrait, and Felicia rising into the frame beside him */
    LOAD_RES(2, req_yuup, CS, 0xA000);  unpack_mask(CS, 0xA000, arena, 0x4000);
    LOAD_RES(2, req_new1, CS, 0xA000);  unpack_mask(CS, 0xA000, arena, 0x8000);
    GD_DRAW3(0xFF, arena, 0x4000, 0x0B, 0x18, 0x18, 0x58);        /* 6060   */
    scroll_strip(arena + 0x8000, /*row*/0xB2, CS + 0x2000, 0);    /* 6078   */
    GD_DRAW3(0xFF, CS + 0x2000, 0, 0x2D, 0x71, 0x18, 0x58);       /* 608A   */
    tick = 0;  wait_ticks(0xFF);
    for (u8 n = 0x59; n; n--) {                    /* 609D 89 scroll steps   */
        scroll_strip(arena + 0x8000, (n - 1) * 2, CS + 0x2000, 0);
        GD_DRAW3_FAST(CS + 0x2000, 0, /*BH*/0x2D, /*BL*/n + 0x17, 0x18, 0x58);
        wait_ticks(0x0A);
    }

    /* 2. the ornate frame wipes in */
    LOAD_RES(2, req_waku, CS, 0xA000);                            /* 60E0   */
    unpack_mask(CS, 0xA000, CS + 0x2000, 0x0000);
    GD_WIPE(CS + 0x2000, 0x0000);                                 /* 60F8   */
    play_narration();   /* 60FD beat 1 — "At long last, Jashiin was destroyed
                           …", then Garland (0x9n) and Felicia (0xAn) talk   */

    /* 3. the King holding Felicia */
    LOAD_RES(2, req_new2, CS, 0xA000);  unpack_mask(CS, 0xA000, arena, 0x4000);
    GD_FX_20(1);                                                  /* 6120   */
    GD_PALETTE(7);
    GD_DRAW3(0xFF, arena, 0x4000, 0x1D, 0x12, 0x1C, 0x64);        /* 613D
                                        112x100 at (116,18); bx = 0x1D12    */
    play_narration();   /* 6142 beat 2 — "Father!" / "My darling Felicia!"   */

    /* 4. the Guardian Spirit (2 planes: bit 0 and bit 2) */
    LOAD_RES(2, req_sei, CS, 0xA000);   unpack_mask(CS, 0xA000, arena, 0x4000);
    GD_DRAW_MASKED(5, arena, 0x4000, 0x16, 0x10, 0x24, 0x68);     /* 616D
                                        144x104 at (88,16); bx = 0x1610     */
    play_narration();   /* 6172 beat 3 — the Spirit appears                  */

    /* 5. Garland and the Spirit, one in each picture box */
    LOAD_RES(2, req_yuup, CS, 0xA000);  unpack_mask(CS, 0xA000, arena, 0x4000);
    LOAD_RES(2, req_seip, CS, 0xA000);  unpack_mask(CS, 0xA000, arena, 0x8000);
    GD_FX_20(0);  GD_PALETTE(6);
    GD_BOX(0x0A, 0x15, 0x1A, 0x5D);                               /* 61C4   */
    GD_DRAW3_FAST(arena, 0x4000, 0x0B, 0x18, 0x18, 0x58);         /* 61D7   */
    GD_BOX(0x2C, 0x15, 0x1A, 0x5D);                               /* 61E2   */
    GD_DRAW3_FAST(arena, 0x8000, 0x2D, 0x18, 0x18, 0x58);         /* 61F5   */
    play_narration();   /* 61FA beat 4 — the Spirit (0x8n, out of seip's
                           tail) and Garland (0x9n) trade lines             */

    /* 6. Felicia replaces the Spirit in the right box */
    LOAD_RES(2, req_himp, CS, 0xA000);  unpack_mask(CS, 0xA000, arena, 0x8000);
    GD_DRAW3(0xFF, arena, 0x8000, 0x2D, 0x18, 0x18, 0x58);        /* 622A   */
    play_narration();   /* 622F beat 5 — "Must you leave so soon…" (0xBn,
                           out of himp's tail) and Garland's farewell        */

    /* 7. the two of them, larger */
    LOAD_RES(2, req_ne80, CS, 0xA000);  unpack_mask(CS, 0xA000, arena, 0x4000);
    LOAD_RES(2, req_ne81, CS, 0xA000);  unpack_mask(CS, 0xA000, arena, 0x8000);
    GD_FX_20(2);  GD_PALETTE(7);                                  /* 626C   */
    GD_DRAW3(0xFF, arena, 0x4000, 0x0B, 0x12, 0x1A, 0x64);        /* 628C   */
    GD_DRAW3(0xFF, arena, 0x8000, 0x33, 0x25, 0x12, 0x51);        /* 62A1   */
    play_narration();   /* 62A6 beat 6 — "Don't go, Duke Garland!" (0xCn)    */

    /* 8. a rolling 0x55 dither over the left picture — the fade to white */
    memset(arena + 0x4000, 0, 0x1E78);                            /* 62B1   */
    for (u8 v = 0x55, n = 0x64; n; n--, v = (v >> 1) | (v << 7))  /* 62C0   */
        memset(arena + 0x4000 + (0x64 - n) * 0x1A, v, 0x1A);
    GD_DRAW3(0, arena, 0x4000, 0x0B, 0x12, 0x1A, 0x64);           /* 62D6   */
    play_narration();   /* 62DB beat 7 — Felicia's closing lines             */

    /* 9. everything dissolves; on to the credits */
    GD_ERASE(0x00, 0x00, 0x50, 0xC8);                             /* 62E6   */
    goto act2_credits;                                            /* 62EB   */
}

/* =======================================================================
 * ACT 2 — 6638.  The credits typewriter.  All six ending pictures are
 * unpacked into the 64 KB scratch first (they are only *decoded* later, one
 * at a time, by the scene routines) and zend.msd starts; from then on the
 * whole ending is one byte script, `credits_script` @787E, whose 0xFE
 * opcode advances the picture.
 *
 * Script opcodes (66CD):
 *   0x20..0x7E  a character: drawn with GD_PUTCHAR(colour 7) at
 *               (cur_col*8, cur_row*14 + 0x90); cur_col++, then
 *               wait `char_delay` ticks.  A block cursor (GD_CURSOR 0xFF) is
 *               left after it and erased (GD_CURSOR 0) before the next byte;
 *               67EA passes it BH = cur_col*2, BL = cur_row*14 + 0x90.
 *   0x09        tab: cur_col = (cur_col + 4) & ~3
 *   0xF7        wait for the *score* to bump [FF21] (music sync opcode F1),
 *               then clear it and [FF50]
 *   0xF8 w      pause_ticks = w        (16-bit)
 *   0xF9        wait until [FF50] >= pause_ticks, then [FF50] = 0
 *   0xFA b      char_delay = b
 *   0xFB r c    **row then column**: 679E is `lodsw / mov [6968],al /
 *               mov [6967],ah`, and [6968] is the row (x14+0x90 at 6724)
 *               while [6967] is the column (x8 at 6730)
 *   0xFC        clear the text window (0,140)-(320,200); cur_col = cur_row = 0
 *   0xFD        newline: cur_col = 0, cur_row++
 *   0xFE        run scene_table[scene_i++]
 *   0xFF        end of script
 * ======================================================================= */
static void (*const scene_table[7])(void) = {                     /* 6820   */
    scene_castle, scene_ride, scene_balcony_open, scene_balcony_close,
    scene_black, scene_landscape, scene_landscape_redraw,
};

void act2_credits(void)                                             /* 6638 */
{
    SP = 0x2000;
    scene_i = 0;
    LOAD_RES(5, req_zend, arena, 0x3000);                         /* 664F   */
    /* all six pictures staged, still packed, in the 64 KB scratch: */
    LOAD_RES(2, req_end5, CS + 0x2000, 0x0000);                   /* 6663   */
    LOAD_RES(2, req_end4, CS + 0x2000, 0x3400);
    LOAD_RES(2, req_end6, CS + 0x2000, 0x5E00);
    LOAD_RES(2, req_end7, CS + 0x2000, 0x8A00);
    LOAD_RES(2, req_en72, CS + 0x2000, 0xB800);
    LOAD_RES(2, req_fin,  CS + 0x2000, 0xE200);
    GD_PALETTE(7);
    INT60H_PLAY(0);                                               /* 66BC   */
    script_p = credits_script;                     /* 66BF [6965] = 0x787E  */
    run_script();                                                 /* 66C5   */
    for (;;) poll_hotkeys();          /* 66C8 — the ending just sits here   */
}

/* --- the seven scene routines ------------------------------------------ */
static void scene_castle(void)                                      /* 682E */
{   unpack_mask(CS + 0x2000, 0x0000, arena, 0x4000);   /* end5 = Felishika castle */
    GD_DRAW3(0xFF, arena, 0x4000, 0x0B, 0x08, 0x39, 0x9A);
                                   /* 6855 bx = 0x0B08: 228x154 at (44,8)   */
}
static void scene_ride(void)                                        /* 685A */
{   unpack_mask(CS + 0x2000, 0x3400, arena, 0x4000);   /* end4 = riding away  */
    GD_ERASE(0x0B, 0x08, 0x39, 0x9A);                             /* 6877   */
    GD_DRAW3(0xFF, arena, 0x4000, 0x21, 0x14, 0x2F, 0x72);
                                   /* 688C bx = 0x2114: 188x114 at (132,20) */
}
static void scene_balcony_open(void)                                /* 6891 */
{   unpack_mask(CS + 0x2000, 0x5E00, arena, 0x4000);              /* end6   */
    GD_END_OPEN(arena, 0x4000);      /* 57 x 2 rows, aperture 0x2F/0x23/0x21 */
}
static void scene_balcony_close(void) { GD_END_CLOSE(arena, 0x4000); }   /* 68B5 */
static void scene_black(void) { GD_ERASE(0x00, 0x00, 0x50, 0xC8); }      /* 68C2 */

static void scene_landscape(void)                                   /* 68CF */
{
    unpack_mask(CS + 0x2000, 0x8A00, arena, 0x4000);   /* end7: 2 planes    */
    memcpy(arena + 0x93C0, CS_2000 + 0xB800, 0x29E0);  /* 68E5 en72 = plane 2 */
    memset(arena + 0x4000, 0, 0x50);                              /* 68F4   */
    GD_DRAW3(0xFF, arena, 0x4000, 0x00, 0x00, 0x50, 0x86);  /* 320x134 at (0,0) */
    unpack_sparse(CS + 0x2000, 0xE200, arena, 0xBDA0); /* 6923 fin.grp, no delta */
    stamp_fin(arena + 0xBDA0);                                    /* 692F   */
}
static void scene_landscape_redraw(void)                            /* 6932 */
{   GD_DRAW3_FAST(arena, 0x4000, 0x00, 0x00, 0x50, 0x86); }

/*
 * 6A52 — stamp the word "FIN" into the finished landscape.  fin.grp is a
 * *single-plane* 38x53 stencil (and the file holds two of them, the letters
 * and their outline): the first is ORed into all three planes of the end7
 * picture at +0x4CE6 (plane stride 0x29E0), the second is ANDed out again,
 * which is what makes the letters appear in solid white with a black edge.
 */
static void stamp_fin(const u16 *src)                               /* 6A52 */
{
    u16 *p = arena + 0x4CE6;
    for (int r = 0; r < 0x35; r++, p += 0x28)
        for (int c = 0; c < 0x13; c++, src++) {
            p[c] |= *src;  p[c + 0x14F0] |= *src;  p[c + 0x29E0] |= *src;
        }
    p = arena + 0x4CE6;                                           /* 6A80   */
    for (int r = 0; r < 0x35; r++, p += 0x28)
        for (int c = 0; c < 0x13; c++, src++) {
            p[c] &= ~*src;  p[c + 0x14F0] &= ~*src;  p[c + 0x29E0] &= ~*src;
        }
}

/*
 * 6A1E — the vertical scroller used for new1.grp (a 96x265 strip, three
 * planes 0x18D8 bytes apart): copy an 87-row window starting at row AX*24
 * into the scratch, blanking the last 24 bytes of each plane.
 */
static void scroll_strip(const u8 *src, u8 row, u8 *dst)            /* 6A1E */
{
    src += row * 0x18;
    for (int pl = 0; pl < 3; pl++) {
        memcpy(dst, src, 0x828);  dst += 0x828;
        memset(dst, 0, 0x18);     dst += 0x18;
        src += 0x18D8;
    }
}
