/*
 * enddemo.c — hand-cleaned decompilation of ENDDEMO.BIN (ZELRES2[50], 8683
 * bytes, image 6000..81EB): the **ending** — the reunion scenes, then the
 * typewriter credits roll with the boss list.  Companion: docs/CUTSCENES.md §4.
 *
 * NOT COMPILABLE — pseudo-C from disasm/overlays/enddemo.asm (origin 6002).
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
 *   6002  act 1 — nine still scenes with dissolves, no text
 *   6638  act 2 — the credits, typed out to zend.msd, synchronised to the score
 *
 * The two unpackers (696D mask+delta, 69F0 RLE) and all the geometry
 * conventions are identical to opdemo's; see src/opdemo.c and
 * docs/CUTSCENES.md §6.
 */

typedef unsigned char u8;
typedef unsigned short u16;

/* gdmcga slots — same table as src/opdemo.c */
#define GD_DRAW3       (*(void(*)())0x3004)
#define GD_ERASE       (*(void(*)())0x3006)
#define GD_PALETTE     (*(void(*)())0x3008)
#define GD_DRAW3_FAST  (*(void(*)())0x3010)
#define GD_DRAW_MASKED (*(void(*)())0x3022)
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
static const u8 req_seip [] = { 0, 0x1D, "seip.grp" };  /* 815D Spirit port. */
static const u8 req_himp [] = { 0, 0x11, "himp.grp" };  /* 8168 Felicia port. */
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

extern const u8 credits_script[];   /* 787E — see run_credits()             */
extern const u8 char_map[];         /* 7F55 — ASCII -> glyph, via [0x3030]  */

/* ---- scratch ------------------------------------------------------------ */
static const u8 *script_p;  /* 6965 */
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
static void beat(void) { tick = 0; wait_ticks(0x10); }              /* 6318 */

/* =======================================================================
 * ACT 1 — 6002.  Nine tableaux; unlike the opening demo there is no text,
 * only pictures, dissolves and the built-in effects.
 * ======================================================================= */
void act1_reunion(void)                                             /* 6002 */
{
    SP = 0x2000;
    script_p = credits_script;                     /* 6007 [6630] = 0x6AA8  */
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
    beat();

    /* 3. the King holding Felicia */
    LOAD_RES(2, req_new2, CS, 0xA000);  unpack_mask(CS, 0xA000, arena, 0x4000);
    GD_FX_20(1);                                                  /* 6120   */
    GD_PALETTE(7);
    GD_DRAW3(0xFF, arena, 0x4000, 0x12, 0x1D, 0x1C, 0x64);        /* 613D   */
    beat();

    /* 4. the Guardian Spirit (2 planes: bit 0 and bit 2) */
    LOAD_RES(2, req_sei, CS, 0xA000);   unpack_mask(CS, 0xA000, arena, 0x4000);
    GD_DRAW_MASKED(5, arena, 0x4000, 0x10, 0x16, 0x24, 0x68);     /* 616D   */
    beat();

    /* 5. Garland and the Spirit, one in each picture box */
    LOAD_RES(2, req_yuup, CS, 0xA000);  unpack_mask(CS, 0xA000, arena, 0x4000);
    LOAD_RES(2, req_seip, CS, 0xA000);  unpack_mask(CS, 0xA000, arena, 0x8000);
    GD_FX_20(0);  GD_PALETTE(6);
    GD_BOX(0x0A, 0x15, 0x1A, 0x5D);                               /* 61C4   */
    GD_DRAW3_FAST(arena, 0x4000, 0x0B, 0x18, 0x18, 0x58);
    GD_BOX(0x2C, 0x15, 0x1A, 0x5D);                               /* 61E2   */
    GD_DRAW3_FAST(arena, 0x8000, 0x2D, 0x18, 0x18, 0x58);
    beat();

    /* 6. Felicia replaces the Spirit in the right box */
    LOAD_RES(2, req_himp, CS, 0xA000);  unpack_mask(CS, 0xA000, arena, 0x8000);
    GD_DRAW3(0xFF, arena, 0x8000, 0x2D, 0x18, 0x18, 0x58);        /* 622A   */
    beat();

    /* 7. the two of them, larger */
    LOAD_RES(2, req_ne80, CS, 0xA000);  unpack_mask(CS, 0xA000, arena, 0x4000);
    LOAD_RES(2, req_ne81, CS, 0xA000);  unpack_mask(CS, 0xA000, arena, 0x8000);
    GD_FX_20(2);  GD_PALETTE(7);                                  /* 626C   */
    GD_DRAW3(0xFF, arena, 0x4000, 0x0B, 0x12, 0x1A, 0x64);        /* 628C   */
    GD_DRAW3(0xFF, arena, 0x8000, 0x33, 0x25, 0x12, 0x51);        /* 62A1   */
    beat();

    /* 8. a rolling 0x55 dither over the left picture — the fade to white */
    memset(arena + 0x4000, 0, 0x1E78);                            /* 62B1   */
    for (u8 v = 0x55, n = 0x64; n; n--, v = (v >> 1) | (v << 7))  /* 62C0   */
        memset(arena + 0x4000 + (0x64 - n) * 0x1A, v, 0x1A);
    GD_DRAW3(0, arena, 0x4000, 0x0B, 0x12, 0x1A, 0x64);           /* 62D6   */
    beat();

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
 *               left after it and erased (GD_CURSOR 0) before the next byte.
 *   0x09        tab: cur_col = (cur_col + 4) & ~3
 *   0xF7        wait for the *score* to bump [FF21] (music sync opcode F1),
 *               then clear it and [FF50]
 *   0xF8 w      pause_ticks = w        (16-bit)
 *   0xF9        wait until [FF50] >= pause_ticks, then [FF50] = 0
 *   0xFA b      char_delay = b
 *   0xFB c r    cur_col = c, cur_row = r
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
    run_script();                                                 /* 66C5   */
    for (;;) poll_hotkeys();          /* 66C8 — the ending just sits here   */
}

/* --- the seven scene routines ------------------------------------------ */
static void scene_castle(void)                                      /* 682E */
{   unpack_mask(CS + 0x2000, 0x0000, arena, 0x4000);   /* end5 = Felishika castle */
    GD_DRAW3(0xFF, arena, 0x4000, 0x08, 0x0B, 0x39, 0x9A);  /* 228x154 at (32,11) */
}
static void scene_ride(void)                                        /* 685A */
{   unpack_mask(CS + 0x2000, 0x3400, arena, 0x4000);   /* end4 = riding away  */
    GD_ERASE(0x08, 0x0B, 0x39, 0x9A);
    GD_DRAW3(0xFF, arena, 0x4000, 0x14, 0x21, 0x2F, 0x72);  /* 188x114 at (80,33) */
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
