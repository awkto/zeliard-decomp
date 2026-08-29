/*
 * opdemo.c — hand-cleaned decompilation of OPDEMO.BIN (ZELRES1[0], 13865
 * bytes, image 6000..9629): the whole **attract sequence** — the scrolling
 * prologue, the demon's warning, the title screen, the STAFF credits and the
 * long "storm" opening demo.  Companion: docs/CUTSCENES.md §2-§4.
 *
 * NOT COMPILABLE — pseudo-C from disasm/overlays/opdemo.asm (origin 6002; the
 * Ghidra dump of the entry point is unusable because 6003 does `mov sp,0x2000`
 * and every act re-does it).  Every routine carries its original address.
 *
 * Loading and calling convention
 * ------------------------------
 * Slot-A overlay with a **one-word** vector header, `[6000] = 6002`.
 * GAME.BIN loads it raw (AL=3) at boot when no command-line argument was
 * given, sets `[FF77] = 0xFF` (the "demo palette" flag the video driver reads,
 * docs/VIDEO_DRIVERS.md §1.1) and `jmp [0x6002]`s into it — it never returns.
 * Instead each act ends by falling into the next, and the last one **reloads
 * GAME.BIN from disk** and jumps back to it:
 *
 *     6A41: [3006] dissolve the screen away
 *           load("game.bin", AL=3, ES:DI = CS:A000)     ; res# 0 = external file
 *           AX = 0xFFFF                                 ; "the demo ran" flag
 *           jmp [cs:0x6A73]                             ; = near jmp 0xA000
 *
 * Rendering goes through **gdmcga** (ZELRES1[5], parked at BASE:3000 by
 * GAME.BIN) — the intro/ending renderer with the 16x16 blend palette; the
 * regular video-driver slots at BASE:2000 are only used for `vid_window`,
 * `vid_puts`, `vid_putchar` and `vid_clear_screen`.
 *
 * Three acts, each begun by `cli / mov sp,0x2000 / sti`:
 *   6002  act 1  prologue -> demon -> title screen   (zopn.msd)
 *   640C  act 2  the STAFF credits scroll            (zend.msd)
 *   6540  act 3  the storm demo                      (no music; sfx only)
 * Space ([FF1D]) or Return ([FF29] == 0x0D) at any point aborts the current
 * act; acts 1 and 2 then start the next one, act 3 hands back to GAME.BIN.
 */

typedef unsigned char u8;
typedef unsigned short u16;

/* ---- gdmcga, BASE:3000 (docs/CUTSCENES.md §6) -------------------------- */
#define GD_DRAW2       (*(void(*)())0x3002)  /* AL, ES:DI src, BH x4, BL y, CH w, CL h
                                                2 planes -> colours 0,1,8,9; dissolve  */
#define GD_DRAW3       (*(void(*)())0x3004)  /* same, 3 planes -> colours 0..7; dissolve */
#define GD_ERASE       (*(void(*)())0x3006)  /* BH,BL,CH,CL — 8-step dissolve to black */
#define GD_PALETTE     (*(void(*)())0x3008)  /* AL = palette record 0..9              */
#define GD_CLEAR_SCRATCH (*(void(*)())0x300A) /* zero the 64 KB (CS+0x2000) buffer     */
#define GD_TEXT_LINE   (*(void(*)())0x300C)  /* DS:SI -> render one line into the text buf */
#define GD_TEXT_SCROLL (*(void(*)())0x300E)  /* AL row, BH,BL,CH,CL — scroll + composite */
#define GD_DRAW3_FAST  (*(void(*)())0x3010)  /* like GD_DRAW3 but a straight copy      */
#define GD_STORM       (*(void(*)())0x3012)  /* DS:SI = 9x6 drop table — rain/lightning */
#define GD_FACE_EYES   (*(void(*)())0x3014)  /* AL frame -> arena:AB40 + AL*0xCC0, 34x48 */
#define GD_FACE_MOUTH  (*(void(*)())0x3016)  /* AL frame -> arena:97C0 + AL*0x480, 18x32 */
#define GD_SKY_DITHER  (*(void(*)())0x3018)  /* fill A000 with the 0x00/0x10 checkerboard */
#define GD_DRAW_AO     (*(void(*)())0x301A)  /* 2 planes -> colours 8 / 10 / 12, transparent */
#define GD_TILE_MAP    (*(void(*)())0x301C)  /* DS:SI = 25x34 tile map over ttl2.grp   */
#define GD_SPARKLE     (*(void(*)())0x301E)  /* AL step — the title-screen twinkle     */
#define GD_FX_20       (*(void(*)())0x3020)  /* AX = variant — built-in "rain of sand" */
#define GD_DRAW_MASKED (*(void(*)())0x3022)  /* AL = plane-present mask (1|2|4)        */
#define GD_BOX         (*(void(*)())0x3024)  /* BH,BL,CH,CL — 0xFF-coloured picture box */
#define GD_FX_26       (*(void(*)())0x3026)  /* plane fix-up then GD_DRAW3_FAST        */
#define GD_WIPE        (*(void(*)())0x3028)  /* 0x44-step horizontal aperture wipe     */
#define GD_END_OPEN    (*(void(*)())0x302A)  /* enddemo only                           */
#define GD_END_CLOSE   (*(void(*)())0x302C)
#define GD_CURSOR      (*(void(*)())0x302E)  /* enddemo only — 8x8 block               */
#define GD_PUTCHAR     (*(void(*)())0x3030)  /* jmp [0x2022]: AL char, AH colour, BX x, CL y */

/* ---- kernel / video driver --------------------------------------------- */
#define LOAD_RES       (*(void(*)())0x10C)
#define VID_WINDOW     (*(void(*)())0x2000)
#define VID_PUTS       (*(void(*)())0x202A)
#define VID_CLEAR      (*(void(*)())0x2042)
#define KRN_HOTKEYS()  /* [0x110] exit, [0x112] pause, [0x116]/[0x118] joystick */
#define INT60H_PLAY(ax)  /* int 60h, DS:SI = score at arena:3000 */

/* ---- resource request blocks, 953D..9626 -------------------------------- */
static const u8 req_nec  [] = { 0, 0x17, "nec.grp"   };  /* 953D  the pendant   */
static const u8 req_hou  [] = { 0, 0x12, "hou.grp"   };  /* 9547  lightning     */
static const u8 req_dmaou[] = { 0, 0x0F, "dmaou.grp" };  /* 9551  demon face    */
static const u8 req_zopn [] = { 0, 0x28, "zopn.msd"  };  /* 955D  title music   */
static const u8 req_ttl1 [] = { 0, 0x1E, "ttl1.grp"  };  /* 9568                */
static const u8 req_ttl2 [] = { 0, 0x1F, "ttl2.grp"  };  /* 9573                */
static const u8 req_ttl3 [] = { 0, 0x20, "ttl3.grp"  };  /* 957E  ZELIARD logo  */
static const u8 req_zend [] = { 0, 0x27, "zend.msd"  };  /* 9589  credits music */
static const u8 req_waku [] = { 0, 0x21, "waku.grp"  };  /* 9594  picture frame */
static const u8 req_ame  [] = { 0, 0x0E, "ame.grp"   };  /* 959F  balcony, rain */
static const u8 req_hime [] = { 0, 0x10, "hime.grp"  };  /* 95A9  the princess  */
static const u8 req_isi  [] = { 0, 0x13, "isi.grp"   };  /* 95B4  turned to stone */
static const u8 req_oui  [] = { 0, 0x1A, "oui.grp"   };  /* 95BE  the king      */
static const u8 req_sei  [] = { 0, 0x1C, "sei.grp"   };  /* 95C8  the Spirit    */
static const u8 req_yuu1 [] = { 0, 0x22, "yuu1.grp"  };  /* 95D2                */
static const u8 req_yuu2 [] = { 0, 0x23, "yuu2.grp"  };  /* 95DD  Garland       */
static const u8 req_yuu3 [] = { 0, 0x24, "yuu3.grp"  };  /* 95E8  throne room   */
static const u8 req_yuu4 [] = { 0, 0x25, "yuu4.grp"  };  /* 95F3  Garland (overlay) */
static const u8 req_yuup [] = { 0, 0x26, "yuup.grp"  };  /* 95FE  Garland portrait  */
static const u8 req_oup  [] = { 0, 0x1B, "oup.grp"   };  /* 9609  King portrait     */
static const u8 req_maop [] = { 0, 0x14, "maop.grp"  };  /* 9613  Jashiin portrait  */
static const u8 req_game [] = { 0, 0x00, "game.bin"  };  /* 961E  res# 0 = real file */

/* ---- text blocks -------------------------------------------------------- */
extern const char prologue_text[];   /* 6FF0 "Two thousand years, ..."   (0x0D/0xFF) */
extern const char epilogue_text[];   /* 7338 "At last, the door of destiny ..."      */
extern const char staff_text[];      /* 742F "Fantasy Action Game / ZELIARD / STAFF" */
extern const u8   narration[];       /* 79C6 the storm-demo script (see play_narration) */
extern const u8   storm_drops[9][6]; /* 9060 {y, x4, dy, dx, frame, frame_max}        */
extern const u8   demon_speech[];    /* 9096 typed text + inline mouth frames 1..4    */
extern const u8   demon_eye_anim[];  /* 911E 01 01 01 02 02 01 01 02 02 03 03 05 00   */
extern const u8   ttl2_map[850];     /* 912B 25 rows x 34 columns of ttl2 tile indices */
extern const u8   glyph_bearing[];   /* 947D per-char left offset (proportional font) */
extern const u8   glyph_width[];     /* 94DD per-char advance                          */

static const char copyright[] =      /* 64EA — [202A] string: 0x87 = colour 7, 0xFF end */
    "\x87    Copyright (C)1987,1990 GAME ARTS    \r"
    "    Copyright (C)1990 Sierra On-Line   \xFF";

/* ---- scratch ------------------------------------------------------------ */
static u16 text_x;      /* 653D  typed-text pen x, in pixels               */
static u8  text_y;      /* 653F  typed-text pen y                          */
static const u8 *narr_p;/* 6D56  narration script pointer (init 0x79C6)    */
static u16 narr_x;      /* 6D58  proportional pen x                        */
static u8  narr_line;   /* 6D5A  text line 0..3                            */
static u8  narr_shadow; /* 6D5B  shadow colour                             */
static u8  narr_ink;    /* 6D5C  text colour                               */
static u8  narr_click;  /* 6D5D  per-character sfx id (0 = silent)         */

/* =======================================================================
 * 63AB / 6A07 — wait AL frame ticks while polling the hotkeys.  Both abort
 * the act the moment Space ([FF1D]) or Return ([FF29]) is seen; 63AB jumps
 * straight to the act-1 teardown, 6A07 to the act-3 one.
 * ======================================================================= */
static void poll_hotkeys(void)                                      /* 63CC */
{
    KRN_HOTKEYS();          /* [0x110] [0x112] [0x116] [0x118] */
}

static void wait_ticks(u8 n)                                        /* 63AB */
{
    while (btn_event /*FF1D*/ == 0 && last_key /*FF29*/ != 0x0D) {
        poll_hotkeys();
        if (tick /*FF1A*/ >= n) { tick = 0; return; }
    }
    goto end_act1;                                        /* 63B1/63B9 -> 63E5 */
}

/* 6456 and 6A07/6A18 are byte-for-byte the same routine with a different
   abort target (act 2's teardown at 6477, act 3's at 6A41). */
static void wait_ticks_act2(u8 n)                                   /* 6456 */
{
    while (btn_event == 0 && last_key != 0x0D) {
        poll_hotkeys();
        if (tick >= n) { tick = 0; return; }
    }
    goto end_act2;
}

static void wait_ticks_act3(u8 n)                              /* 6A07/6A18 */
{
    while (btn_event == 0 && last_key != 0x0D) {
        poll_hotkeys();
        if (tick >= n) { tick = 0; return; }
    }
    hand_back_to_game();                                          /* 6A41   */
}

/* =======================================================================
 * 62D1 — the *fixed-pitch* typed-text player used over the demon's face.
 * One character every 20 ticks with a keyclick; bytes 1..4 are inline
 * mouth-frame changes; 0xFF introduces a line control.
 * ======================================================================= */
static void emit_char(u8 c, const u8 **p)                           /* 62FD */
{
    if (c == 0xFF) {                                  /* line control       */
        u8 op = *(*p)++;
        if (op != 1) return;
        text_x = *(*p)++ * 8;                         /* 630C x = n * 8     */
        text_y += 10;                                 /* 6318 next line     */
        return;
    }
    GD_PUTCHAR(c, /*AH=*/2, text_x + 2, text_y + 1);  /* 6331 red shadow    */
    GD_PUTCHAR(c, /*AH=*/7, text_x,     text_y);      /* 6341 magenta text  */
    text_x += 8;
    if (c != ' ') sfx /*FF75*/ = 0x3F;                /* 6352 keyclick      */
}

static void play_typed_text(const u8 *p)                            /* 62D1 */
{
    text_y = 0x8A;                                                /* 62D1   */
    for (;;) {
        tick = 0;
        u8 c = *p++;
        if (c == 0) return;
        if (c < 5) { GD_FACE_MOUTH(c - 1, /*BH*/0x1F, /*BL*/0x70); continue; }
        emit_char(c, &p);                                         /* 62F3   */
        wait_ticks(0x14);
    }
}

/* =======================================================================
 * 6358 / 6497 / 6D04 — the three text scrollers.  All three do the same
 * thing with a different window: clear the 64 KB scratch, then for every
 * line of the block render it into gdmcga's 320x10 text buffer and scroll
 * the scratch up ten rows, compositing it over the picture (the scroll
 * blitter ANDs the screen with 0x9999 and ORs in `text & 0x6666`, so the
 * background may only use colour bits 0 and 3 — which is exactly what
 * GD_DRAW2 produces).  A line ends with 0x0D, the block with 0xFF.
 * ======================================================================= */
static void scroll_block(const char *p, u8 x4, u8 y, u8 w, u8 h,
                         void (*wait)(u8))                  /* 6358/6497/6D04 */
{
    GD_CLEAR_SCRATCH(/*BH*/0x20, /*BL*/0x00, /*CH*/0x50, /*CL*/0x78);
    do {
        GD_TEXT_LINE(p);                       /* renders up to 0x0D / 0xFF  */
        p = /* SI after the terminator */ 0;
        for (int i = 10; i; i--) {             /* ten one-row scroll steps   */
            GD_TEXT_SCROLL(/*AL row*/10 - i, x4, y, w, h);
            wait(0x1C);
        }
    } while (p[-1] != 0xFF);                                      /* 638B   */
    for (int i = 0x78; i; i--) {               /* 6391 scroll the last page off */
        GD_TEXT_SCROLL(0, x4, y, w, h);
        wait(0x1C);
    }
}

/* =======================================================================
 * ACT 1 — 6002.  Title logo -> pendant + prologue -> demon -> title screen.
 * ======================================================================= */
void act1_prologue(void)                                            /* 6002 */
{
    SP = 0x2000;  btn_event = 0;  last_key = 0;                   /* 6002   */
    VID_CLEAR();

    /* --- a first glimpse of the logo, over the copyright line ---------- */
    LOAD_RES(2, req_ttl3, CS, 0xA000);                            /* 601E   */
    unpack_rle(/*DS:SI*/CS, 0xA000, /*ES:DI*/arena, 0x4000);      /* 6036   */
    GD_PALETTE(4);
    VID_PUTS(copyright, /*BX*/0, /*CL*/0x96);                     /* 6041   */
    GD_DRAW_AO(arena, 0x4000, /*BH*/0x07, /*BL*/0x0F, /*CH*/0x41, /*CL*/0x70);

    /* --- the pendant, in two colours, with the prologue scrolling over -- */
    LOAD_RES(2, req_nec, CS, 0xA000);                             /* 6060   */
    LOAD_RES(2, req_hou, CS, 0xB800);                             /* 606F   */
    unpack_mask(CS, 0xA000, arena, 0x4000);                       /* 6087   */
    VID_CLEAR();
    GD_PALETTE(1);                        /* 0 blk / 1 blue / 8 grey / 9 grey */
    GD_DRAW2(0xFF, arena, 0x4000, 0x12, 0x20, 0x2C, 0x68);        /* 60B3   */
    scroll_block(prologue_text, /*x4*/0x00, /*y*/0x20, 0x50, 0x78, wait_ticks);

    /* --- same picture in full colour, then the storm ------------------- */
    GD_PALETTE(2);                                                /* 60BB   */
    GD_DRAW3(0xFF, arena, 0x4000, 0x12, 0x20, 0x2C, 0x68);        /* 60D3   */
    unpack_mask(CS, 0xB800, arena, 0x9000);      /* 60E3 hou.grp = 8 bolts  */
    GD_DRAW3_FAST(arena, 0x75A0, 0x20, 0x48, 0x10, 0x40);
                                          /* 60E6 nec.grp's 2nd picture     */
    sfx = 4;                                      /* 60F9 thunder           */
    GD_STORM(storm_drops);                        /* 60FF rain + palette flash */

    /* --- the demon's face and his warning ------------------------------ */
    LOAD_RES(2, req_dmaou, CS, 0xA000);                           /* 6109   */
    unpack_mask(CS, 0xA000, arena, 0x97C0);                       /* 6121   */
    build_demon_face();                                           /* 6124   */
    GD_ERASE(0x12, 0x20, 0x2C, 0x68);             /* 6127 dissolve the pendant */
    GD_PALETTE(3);
    GD_DRAW3(0xFF, CS + 0x2000, 0x0000, 0x17, 0x20, 0x22, 0x70);  /* 614C   */

    for (const u8 *p = demon_eye_anim; *p; p++) {                 /* 6151   */
        tick = 0;
        GD_FACE_EYES(*p - 1, /*BH*/0x17, /*BL*/0x20);
        wait_ticks(0x14);
    }
    tick = 0; wait_ticks(0xF0);
    play_typed_text(demon_speech);                                /* 617E   */
    tick = 0; wait_ticks(0xF0);
    GD_FACE_EYES(1, 0x17, 0x20); wait_ticks(0x0F);                /* 618B   */
    GD_FACE_EYES(2, 0x17, 0x20); wait_ticks(0xF0);                /* 619F   */
    VID_WINDOW(0, /*BH*/0x00, /*BL*/0x94, /*CH*/0x50, /*CL*/0x1E); /* 61BB  */

    /* --- the title screen ---------------------------------------------- */
    LOAD_RES(2, req_ttl1, CS, 0xA000);                            /* 61CA   */
    unpack_rle(CS, 0xA000, arena, 0x4000);                        /* 61DA   */
    LOAD_RES(2, req_ttl2, CS, 0xA000);                            /* 61E7   */
    LOAD_RES(2, req_ttl3, CS, 0xB000);                            /* 61F4   */
    LOAD_RES(5, req_zopn, arena, 0x3000);                         /* 6206   */
    GD_ERASE(0x17, 0x20, 0x22, 0x70);             /* 620B the demon fades   */
    GD_PALETTE(4);
    INT60H_PLAY(0);                               /* 622E zopn.msd starts   */
    GD_SKY_DITHER();                              /* 6231 the night sky     */
    wait_ticks(0xF0);
    GD_DRAW3(0, arena, 0x4000, 0x0B, 0x48, 0x31, 0x80);  /* 624B ttl1 necklace */
    tick = 0;
    unpack_rle(arena, 0xB000, arena, 0x4000);     /* 6260 ttl3              */
    wait_ticks(0xF0);
    GD_DRAW_AO(arena, 0x4000, 0x07, 0x0F, 0x41, 0x70);   /* 6276 the logo   */
    tick = 0;
    unpack_rle(arena, 0xA000, arena, 0x4000);     /* 628B ttl2 (RLE, like   */
                                              /*      the other ttl*)   */
    GD_TILE_MAP(ttl2_map);                        /* 6291 the corner scroll */
    wait_ticks(0xF0);

    /* twinkles: 100 steps, two sparkle streams walking in opposite
       directions through GD_SPARKLE's own table (AL 0xC7 down by 2,
       AH 0x00 up by 2). */
    for (u8 lo = 0xC7, hi = 0x00, n = 100; n; n--, lo -= 2, hi += 2) { /* 629B */
        tick = 0;
        GD_SPARKLE(lo);
        GD_SPARKLE(hi);
        wait_ticks(0x50);
    }
    do poll_hotkeys(); while (music_stopped /*FF26*/ == 0);       /* 62C4   */

end_act1:                                                         /* 63E5   */
    music_fade /*FF24*/ = 8;
    GD_ERASE(0x00, 0x00, 0x50, 0xC8);             /* dissolve the whole screen */
    while (music_stopped == 0) ;
    btn_event = 0; last_key = 0;
    /* falls through into act 2 */
}

/* =======================================================================
 * ACT 2 — 640C.  The STAFF credits, over black, to zend.msd.
 * ======================================================================= */
void act2_credits(void)                                             /* 640C */
{
    SP = 0x2000;
    VID_CLEAR();
    LOAD_RES(5, req_zend, arena, 0x3000);                         /* 6425   */
    tick = 0;
    INT60H_PLAY(0);                                               /* 643A   */
    btn_event = 0; last_key = 0;
    GD_PALETTE(1);
    scroll_block(staff_text, 0x00, 0x20, 0x50, 0x78, wait_ticks_act2); /* 6497 */
end_act2:                                                         /* 6477   */
    music_fade = 8;
    VID_CLEAR();
    while (music_stopped == 0) ;
    btn_event = 0; last_key = 0;
    /* falls through into act 3 */
}

/* =======================================================================
 * ACT 3 — 6540.  The storm demo: 15 pictures in the waku.grp frame with the
 * proportional-font narration underneath, all driven by one byte script.
 * `play_narration()` runs until the script hits 0xFD, so the code below
 * reads as "set up the next picture, then let the script talk".
 * ======================================================================= */
void act3_storm_demo(void)                                          /* 6540 */
{
    SP = 0x2000;  btn_event = 0; last_key = 0;
    narr_p = narration;                          /* 6551 [6D56] = 0x79C6    */
    GD_PALETTE(5);

    LOAD_RES(2, req_waku, CS, 0xA000);                            /* 656A   */
    unpack_mask(CS, 0xA000, CS + 0x2000, 0x0000);                 /* 657C   */
    LOAD_RES(2, req_ame, CS, 0xA000);                             /* 6589   */
    unpack_mask(CS, 0xA000, arena, 0x4000);                       /* 6599   */
    GD_DRAW3_FAST(CS + 0x2000, 0x0000, 0x00, 0x00, 0x50, 0x88);   /* 65AC frame */
    GD_DRAW3_FAST(arena, 0x4000, 0x04, 0x10, 0x48, 0x68);         /* 65B1 rain  */
    play_narration();                                             /* 65C4   */

    GD_PALETTE(9);  GD_DRAW3_FAST(arena, 0x4000, 0x04, 0x10, 0x48, 0x68);
    LOAD_RES(2, req_hime, CS, 0xA000);                            /* 65EC   */
    unpack_mask(CS, 0xA000, arena, 0x4000);
    play_narration();
    GD_FX_20(0);                                  /* 6604 the rain of sand  */
    GD_PALETTE(6);  GD_DRAW3_FAST(arena, 0x4000, 0x04, 0x10, 0x48, 0x68);

    LOAD_RES(2, req_dmaou, CS, 0xA000);                           /* 662E   */
    unpack_mask(CS, 0xA000, arena, 0x97C0);
    play_narration();
    demon_eyes_to_scratch(4);                     /* 6646 = 6E8F(AL=4)      */
    mask_demon_over_picture(CS + 0x2000, 0);      /* 6653 = 6ED8            */
    GD_DRAW3_FAST(arena, 0x4000, 0x04, 0x10, 0x48, 0x68);
    play_narration(); play_narration();
    GD_DRAW_MASKED(7, CS + 0x2000, 0x0000, 0x28, 0x17, 0x22, 0x30);  /* 6681 */
    play_narration(); play_narration();
    demon_eyes_to_scratch(2);                                     /* 668E   */
    GD_DRAW3_FAST(CS + 0x2000, 0, 0x28, 0x17, 0x22, 0x30);
    tick = 0; wait_ticks_act3(0x0F);
    demon_eyes_to_scratch(3);                                     /* 66B3   */
    GD_DRAW3_FAST(CS + 0x2000, 0, 0x28, 0x17, 0x22, 0x30);

    LOAD_RES(2, req_isi, CS, 0xA000);                             /* 66D5   */
    unpack_mask(CS, 0xA000, arena, 0x4000);
    GD_ERASE(0x04, 0x10, 0x48, 0x68);                             /* 66EE   */
    play_narration();
    GD_PALETTE(7);
    GD_DRAW3(0xFF, arena, 0x4000, 0x04, 0x10, 0x48, 0x68);        /* 670E   */
    play_narration();

    LOAD_RES(2, req_oui, CS, 0xA000);                             /* 6720   */
    unpack_mask(CS, 0xA000, arena, 0x4000);
    GD_DRAW3(0, arena, 0x4000, 0x04, 0x10, 0x48, 0x68);           /* 6743   */
    play_narration(); play_narration();

    LOAD_RES(2, req_sei, CS, 0xA000);                             /* 6758   */
    unpack_mask(CS, 0xA000, arena, 0x4000);
    GD_DRAW_MASKED(5, arena, 0x4000, 0x10, 0x16, 0x24, 0x68);     /* 6776 the Spirit */
    play_narration();
    GD_FX_20(0);
    play_narration();

    LOAD_RES(2, req_yuu1, CS, 0xA000);                            /* 6792   */
    unpack_mask(CS, 0xA000, arena, 0x4000);
    GD_DRAW3(0xFF, arena, 0x4000, 0x04, 0x10, 0x48, 0x68);        /* 67B5   */

    /* --- the throne-room dialogue: two talking heads in boxes ---------- */
    LOAD_RES(2, req_yuup, CS, 0xA000);                            /* 67C4   */
    unpack_mask(CS, 0xA000, arena, 0x4000);       /* Garland's portrait set */
    LOAD_RES(2, req_oup, CS, 0xA000);                             /* 67E1   */
    unpack_mask(CS, 0xA000, arena, 0x8000);       /* the King's portrait set */
    play_narration(); play_narration();
    GD_FX_20(0);
    GD_PALETTE(6);
    GD_BOX(0x15, 0x0A, 0x1A, 0x5D);               /* 680F left picture box  */
    GD_DRAW3_FAST(arena, 0x4000, 0x18, 0x0B, 0x18, 0x58);         /* 6822   */
    GD_BOX(0x15, 0x2C, 0x1A, 0x5D);               /* 682D right picture box */
    GD_DRAW3_FAST(arena, 0x8000, 0x18, 0x2D, 0x18, 0x58);         /* 6840   */
    play_narration(); play_narration();

    LOAD_RES(2, req_maop, CS, 0xA000);                            /* 6855   */
    unpack_mask(CS, 0xA000, arena, 0x8000);       /* Jashiin's portrait     */
    GD_FX_20(0);
    GD_PALETTE(8);
    GD_BOX(0x15, 0x15, 0x31, 0x5D);                               /* 687D   */
    GD_FX_26(arena, 0x8000, 0x18, 0x16);                          /* 688D   */
    play_narration(); play_narration();
    for (u16 bx = 0x1515, dx = 0x315D, n = 0x18; n; n--) {        /* 68A1   */
        tick = 0;  GD_BOX(bx, dx);  wait_ticks_act3(0x0F);
        bx += 0x0100;  dx -= 0x0100;   /* the box grows one column per step */
    }
    GD_BOX(0x15, 0x2C, 0x1A, 0x5D);                               /* 68C5   */
    GD_BOX(0x15, 0x0A, 0x1A, 0x5D);
    GD_DRAW3_FAST(arena, 0x4000, 0x18, 0x0B, 0x18, 0x58);
    play_narration(); play_narration();
    for (u16 bx = 0x2C15, dx = 0x1A5D, n = 0x18; n; n--) {        /* 68F7   */
        tick = 0;  GD_BOX(bx, dx);  wait_ticks_act3(0x0F);
        bx += 0x0100;  dx -= 0x0100;
    }
    GD_FX_20(0);  GD_PALETTE(7);

    LOAD_RES(2, req_yuu2, CS, 0xA000);                            /* 692E   */
    unpack_mask(CS, 0xA000, arena, 0x4000);
    GD_DRAW3_FAST(arena, 0x4000, 0x10, 0x10, 0x31, 0x60);         /* 694F   */
    play_narration();

    /* --- the last picture: yuu3 (2 planes) + yuu4 masked into it ------- */
    LOAD_RES(2, req_yuu3, CS, 0xA000);                            /* 6961   */
    LOAD_RES(2, req_yuu4, CS, 0xD000);                            /* 696E   */
    unpack_mask(CS, 0xA000, arena, 0x4000);                       /* 697E   */
    GD_ERASE(0x00, 0x00, 0x50, 0xC8);                             /* 6987   */
    synth_third_plane(arena, 0x4000);       /* 6997 = 6FAC, ends in GD_DRAW3 */
    unpack_mask(CS, 0xD000, arena, 0xD000);                       /* 69A5   */
    mask_yuu4_into_yuu3(arena, 0x4000, arena, 0xD000);            /* 69B3   */
    GD_DRAW3(0xFF, arena, 0x4000, 0x08, 0x08, 0x40, 0xC0);        /* 69C6   */
    tick = 0; wait_ticks_act3(0xF0);
    GD_DRAW2(0xFF, arena, 0x4000, 0x08, 0x08, 0x40, 0xC0);   /* 69E6 dim it */
    GD_PALETTE(1);
    scroll_block(epilogue_text, 0x00, 0x14, 0x50, 0xA0, wait_ticks_act3); /* 69F6 */
    for (int n = 10; n; n--) wait_ticks_act3(0xC8);                   /* 69FC   */
    /* falls into hand_back_to_game */
}

/* =======================================================================
 * 6A41 — teardown: dissolve, reload GAME.BIN from disk, jump into it.
 * ======================================================================= */
void hand_back_to_game(void)                                        /* 6A41 */
{
    GD_ERASE(0x00, 0x00, 0x50, 0xC8);
    btn_event = 0; last_key = 0;
    LOAD_RES(3, req_game, CS, 0xA000);      /* res# 0 -> open "game.bin"    */
    AX = 0xFFFF;                            /* 6A6B "you came from the demo" */
    goto *0xA000;                           /* 6A6E jmp [cs:0x6A73]         */
}

/* =======================================================================
 * 6A80 — the storm-demo narration engine.  A proportional font, word wrap,
 * per-speaker ink colours and lip-sync, one character every 16 ticks.
 * Returns on 0xFD (end of beat) or 0xFF (end of script); the caller then
 * puts up the next picture and calls again.
 *
 *   0x20..0x7F  a character
 *   0x80..0x8F  right speaker: n<6 mouth frame n, n>=6 eye frame n-6
 *   0x90..0x9F  left speaker: likewise
 *   0xEB..0xF0  set the per-character click sfx (0x41,0x40,0x3F,0x3E,0x3D,0)
 *   0xF1/0xF2/0xF3/0xF7  start line 3 / 2 / 1 / 0 (x = 0)
 *   0xF5        pause 0xF0 ticks;  0xF6  pause 3 x 0xF0
 *   0xF9/0xFA/0xFB  ink/shadow = (6,2) yellow / (7,0) magenta / (7,1) white
 *   0xFC        (unused)
 *   0xFD        return (end of beat)
 *   0xFE        clear the text box (0,143)-(320,200) and reset to line 0
 *   0xFF        return (end of script)
 * ======================================================================= */
void play_narration(void)                                     /* 6A75/6A80 */
{
    tick = 0;                                                     /* 6A75   */
    for (;;) {
        wait_ticks_act3(0x10);                                        /* 6A7B   */
        u8 c = *narr_p++;
        if (c & 0x80) { if (!narration_control(c)) return; continue; }

        if (c != ' ' && c != '.' && c != ',' && c != '"' && c != '\'')
            sfx = narr_click;                                     /* 6AA6   */

        u16 x = narr_x + 4 - glyph_bearing[c - 0x20];             /* 6AAF   */
        u8  y = narr_line * 10 + 0x8F;                            /* 6AB9   */
        GD_PUTCHAR(c, narr_shadow, x + 1, y + 1);                 /* 6ADE   */
        GD_PUTCHAR(c, narr_ink,    x,     y);                     /* 6AEA   */
        narr_x += glyph_width[c - 0x20];                          /* 6AFD   */

        if (c == ' ' &&                                           /* 6B01   */
            narr_x + measure_word(narr_p) >= 0x138)               /* 6B0C word wrap */
            { narr_x = 0; narr_line++; }                          /* 6BF0   */
    }
}

/* 6C28 / 6C77 — the two lip-sync sprite sets.  Both are 3-plane pictures
 * inside the *p.grp portrait file that is already in the arena:
 *   0x8n  right speaker (oup.grp @ arena:8000, box at x4 0x33 / 0x38)
 *         n < 6 : mouth  frame n, 14x32 bytes, at arena:98C0 + n*1344
 *         n >= 6: eyes   frame n-6, 11x16,     at arena:B840 + (n-6)*528
 *   0x9n  left speaker  (yuup.grp @ arena:4000, box at x4 0x13 / 0x12)
 *         n < 6 : mouth  9x32,  at arena:58C0 + n*864
 *         n >= 6: eyes   11x16, at arena:6D00 + (n-6)*528
 */

/* =======================================================================
 * The two unpackers.  Neither is self-describing: the demo knows which one
 * each resource needs (ttl1-3.grp use the RLE, everything else the mask).
 * docs/CUTSCENES.md §6.2; both are reimplemented in tools/grp2png.py.
 * ======================================================================= */
static u16 unpack_sparse(const u8 *src, u8 *dst)                    /* 6D63 */
{
    u16 n = *(u16 *)src;                     /* count of bit-mask bytes     */
    const u8 *mask = src + 2, *data = mask + n;
    for (u16 i = 0; i < n; i++)
        for (int b = 0; b < 8; b++)
            *dst++ = (mask[i] & (0x80 >> b)) ? *data++ : 0;   /* 6D72       */
    return n * 8;                                             /* 6D85       */
}

static void undelta_2bit(u8 *p, u16 n)                              /* 6D8D */
{
    u8 prev = 0;                             /* runs across the whole buffer */
    while (n--) {
        u8 out = 0;
        for (int k = 0; k < 4; k++) { prev ^= (*p >> (6 - 2*k)) & 3;
                                      out = (out << 2) | prev; }
        *p++ = out;
    }
}

static void unpack_mask(const u8 *src, u8 *dst)                     /* 6D5E */
{
    undelta_2bit(dst, unpack_sparse(src, dst));
}

static void unpack_rle(const u8 *s, u8 *d)                          /* 6DE1 */
{
    for (;;) {
        u16 v, n;
        if (*s & 0x40) {                     /* 16-bit big-endian form      */
            v = (s[0] << 8) | s[1];  s += 2;
            if (v == 0xFFFF) return;                          /* 6DEB       */
            n = v & 0x3FFF;
        } else {                             /* 6-bit form                  */
            v = *s << 8;  n = *s++ & 0x3F;
        }
        if (v & 0x8000) { u8 b = *s++; while (n--) *d++ = b; }  /* run      */
        else            { while (n--) *d++ = *s++; }            /* literal  */
    }
}

/* =======================================================================
 * Plane arithmetic — the demo builds pictures gdmcga cannot draw directly.
 *   6E0F  assemble the act-1 demon face: a 34x112 3-plane image at
 *         (CS+0x2000):0 from three single planes of dmaou.grp — the eyes
 *         (34x48) into planes 0+1, the nose (6x32) into planes 0+1 at
 *         (col 15, row 48) and the mouth (18x32) into plane 0 only at
 *         (col 8, row 80), so the three parts come out in different colours.
 *   6E8F  turn eye frame AL (2 planes at arena:AB40 + AL*0xCC0) into a
 *         3-plane 34x48 picture at (CS+0x2000):0 (6EB0 derives plane 2).
 *   6ED8  AND/OR the scratch picture into the arena picture through a mask
 *         built from its own three planes.
 *   6F41  stamp yuu4.grp (3 planes, 21x160) into yuu3.grp at +0x819.
 *   6FAC  yuu3.grp only *has* two planes (0x3000 bytes each): derive the
 *         third, then tail-call GD_DRAW3 with CX = 0x40C0 (256x192).
 * ======================================================================= */
