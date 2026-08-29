/*
 * rokademo.c — hand-cleaned decompilation of ROKADEMO.BIN (ZELRES3[0],
 * 1448 bytes, image A000..A5A8): the **"Tear of Esmesanti obtained"**
 * cutscene.  Companion: docs/CUTSCENES.md §5.
 *
 * NOT COMPILABLE — pseudo-C from disasm/overlays/rokademo.asm (origin A002).
 * Every routine carries its original address; constants cite the instruction
 * they come from.
 *
 * Loading and calling convention
 * ------------------------------
 * A slot-B overlay with a **one-word** vector header: `[A000] = A002`.
 * fight.bin loads it raw (AL=3) over the spent boss AI and calls it
 * (fight.asm 7C18):
 *
 *     if (door_record[+8] & 0x80) {           // 7C11, byte cached at [0x9F1D]
 *         load(ROKADEMO.BIN, AL=3, ES:DI = CS:A000);
 *         call [cs:0xA000];                   // 7C27 — returns here
 *     }
 *
 * i.e. it is triggered by a **door/transition record**, not by the boss death:
 * the exit door of a boss room carries the flag, so the scene plays as Garland
 * leaves with the crystal.  The routine `ret`s (its last instruction is
 * `jmp [cs:0x2000]`, a tail call to vid_window) and fight.bin carries on
 * loading the destination map — which also reloads BASE:A000, so nothing here
 * has to be preserved.
 *
 * It is the only user of ROKA*'s name ("roka" = 廊下, corridor): the scene is
 * Garland walking right along the corridor out of the boss room.
 *
 * State page (docs/STATE_PAGE.md):
 *   [A0]  tears collected (this routine increments it, capped at 9)
 *   [C2]  hero facing (bit 0 = left)
 *   [E7]  hero animation frame
 *   [FF1A] frame tick counter (cleared, then waited on)
 *   [FF33] speed divider (F9), [FF75] sound-effect request
 */

typedef unsigned char u8;
typedef unsigned short u16;

/* ---- kernel / video driver / fight renderer ---------------------------- */
#define LOAD_RES       (*(void(*)())0x10C)   /* AL mode, DS:SI request, ES:DI dst */
#define VID_WINDOW     (*(void(*)())0x2000)  /* AL style, BH x4, BL y, CH w4, CL h */
#define VID_SAVE_RECT  (*(void(*)())0x2026)  /* AH x8, AL y, CH w8, CL rows, DI dst */
#define VID_RESTORE    (*(void(*)())0x2028)
#define VID_TEAR_ICON  (*(void(*)())0x203E)  /* AL 0/1 icon, BH x4, BL y — 16x13 */
#define GF_BLIT_CELL   (*(void(*)())0x3022)  /* AL cell, BH x4, BL y — 8x8 from arena:6000 */
#define GF_DRAW_SWORD  (*(void(*)())0x3024)  /* equipped sword picture [0x92] at (144,86) */
#define GF_SPARKLE     (*(void(*)())0x3026)  /* AL frame (bit7 = big), BX x px, CL y */
#define GF_CONVERT_2BPP (*(void(*)())0x3028) /* DS:SI bank, BP mask dst, CX cells */
#define INT60H_PLAY(ax)  /* int 60h — AX=0 start score at DS:SI, AX=1 stop */

/* ---- resource request blocks (docs/ARCHITECTURE.md) -------------------- */
static const u8 req_mfan[] = { 2, 0x5F, "MFAN.MSD" };   /* A584 — ZELRES3[94] fanfare */
static const u8 req_dman[] = { 2, 0x36, "DMAN.GRP" };   /* A58F — ZELRES3[53] hero cells */

/*
 * A435: nine 3x3 frame maps, **column-major** (the inner loop steps y, the
 * outer steps x — unlike fman.grp's row-major maps, docs/ARCHITECTURE.md).
 * Values are 0-based cell indices into the converted dman.grp bank.
 * Frames 0-3 = walk right, 4 = stand, 5-8 = raise the crystal.
 */
static const u8 hero_frame[9][9] = {          /* A435 */
    { 0x00,0x02,0x04, 0x01,0x03,0x05, 0x00,0x00,0x06 },
    { 0x07,0x09,0x0B, 0x08,0x0A,0x0C, 0x00,0x00,0x00 },
    { 0x00,0x02,0x0E, 0x01,0x0D,0x0F, 0x00,0x00,0x10 },
    { 0x07,0x09,0x11, 0x08,0x0A,0x12, 0x00,0x00,0x00 },
    { 0x00,0x14,0x16, 0x13,0x15,0x17, 0x00,0x00,0x18 },
    { 0x19,0x00,0x1C, 0x1A,0x1B,0x1D, 0x00,0x00,0x1E },
    { 0x1F,0x00,0x23, 0x20,0x21,0x24, 0x00,0x22,0x25 },
    { 0x1F,0x00,0x23, 0x20,0x26,0x28, 0x00,0x27,0x29 },
    { 0x1F,0x00,0x23, 0x2A,0x2C,0x28, 0x2B,0x2D,0x29 },
};

/* A569: x (pixels) of the tear slot on the top border, per tear 1..9.
   The same eight positions GAME.BIN uses at @A3D3 (x4 * 4), plus a ninth. */
static const u8 tear_x[9]  = { 0x3C,0xF4,0x54,0xDC,0x6C,0xC4,0x84,0xAC,0x98 };
/* A572: the BX (BH = x4, BL = y = 0) passed to [0x203E] for the same slots. */
static const u16 tear_bx[9] = { 0x0F00,0x3D00,0x1500,0x3700,0x1B00,
                                0x3100,0x2100,0x2B00,0x2600 };

/* ---- scratch (A59A..A5A7) ---------------------------------------------- */
static u8 fly_tx, fly_ty;      /* A59A/A59B target x,y of the flying crystal   */
static u8 fly_x,  fly_y;       /* A59C/A59D current position                   */
static u8 fly_sx, fly_sy;      /* A59E/A59F step (+1 / -1) per axis            */
static u8 fly_dx, fly_dy;      /* A5A0/A5A1 |delta| per axis                   */
static u8 fly_major;           /* A5A2  0 = x is the major axis, 0xFF = y      */
static u8 fly_err;             /* A5A3  Bresenham accumulator                  */
static u8 tear_icon;           /* A5A4  [0x203E] AL: 0 normally, 1 on tear 9   */
static u8 anim_i, anim_j, sfx_phase;  /* A5A5 / A5A6 / A5A7 loop counters      */

/* =======================================================================
 * A48F — wait one animation frame.  [FF33] is the F9 speed divider, so the
 * scene slows down with the rest of the game.
 * ======================================================================= */
static void wait_frame(void)                                        /* A48F */
{
    while (tick_counter /*FF1A*/ < (u8)(speed /*FF33*/ * 4))
        ;
    tick_counter = 0;
}

/* =======================================================================
 * A407 — draw hero frame [E7] as a 3x3 block of 8x8 cells at (BH x4, BL y).
 * ======================================================================= */
static void draw_hero(u8 x4, u8 y)                                  /* A407 */
{
    const u8 *m = hero_frame[frame /*[0xE7]*/];
    for (int col = 0; col < 3; col++) {           /* A416: bh += 2 per column */
        for (int row = 0; row < 3; row++)         /* A41A: bl += 8 per row    */
            GF_BLIT_CELL(/*AL=*/*m++, /*BH=*/x4 + col * 2, /*BL=*/y + row * 8);
    }
}

/* =======================================================================
 * A4A3 — set up the Bresenham walk of the crystal from Garland's hands
 * (148, 80) to the HUD tear slot (fly_tx, 2).
 * ======================================================================= */
static void fly_setup(void)                                         /* A4A3 */
{
    fly_x = 0x94;  fly_y = 0x50;                       /* A4A5/A4AB (148,80) */
    fly_sx = 0; fly_dx = fly_tx - fly_x;
    if (fly_dx) { if (fly_tx < fly_x) { fly_dx = -fly_dx; fly_sx = 0xFF; }
                  else fly_sx = 1; }
    fly_sy = 0; fly_dy = fly_ty - fly_y;
    if (fly_dy) { if (fly_ty < fly_y) { fly_dy = -fly_dy; fly_sy = 0xFF; }
                  else fly_sy = 1; }
    if (fly_dy > fly_dx) { fly_err = fly_dy >> 1; fly_major = 0xFF; }  /* A4FD */
    else                 { fly_err = fly_dx >> 1; fly_major = 0x00; }
}

/* A50A — one Bresenham step; returns carry clear while still moving. */
static int fly_step(void)                                           /* A50A */
{
    if (fly_major) {                       /* y is the major axis            */
        if (fly_err < fly_dx) { fly_err += fly_dy; fly_x += fly_sx; }
        fly_err -= fly_dx;
        fly_y += fly_sy;
        return fly_ty == fly_y;            /* CF set (A536 stc) when arrived */
    }
    if (fly_err < fly_dy) { fly_err += fly_dx; fly_y += fly_sy; }
    fly_err -= fly_dy;
    fly_x += fly_sx;
    return fly_tx == fly_x;
}

/* =======================================================================
 * A002 — the cutscene.  Runs on the fight screen with the map already
 * erased by the caller; every drawing call goes through the *fight*
 * renderer (gfmcga), never through gdmcga — this is not a gd demo.
 * ======================================================================= */
void tear_cutscene(void)                                            /* A002 */
{
    u8 bx_hi, n;

    LOAD_RES(5, req_mfan, arena + 0x3000);           /* A002 mfan.msd        */
    LOAD_RES(2, req_dman, arena + 0x6000);           /* A014 dman.grp        */
    GF_CONVERT_2BPP(/*DS:SI*/arena + 0x6000, /*BP*/0xD000, /*CX*/0x100);
                                                     /* A026 256 cells32     */

    if (++tears /*[0xA0]*/ > 9) { tears = 9; tear_icon = 1; }  /* A03B..A04F */
    else                          tear_icon = 0;
    VID_TEAR_ICON(tear_icon, /*BH*/0x25, /*BL*/0x52);   /* A052 the crystal  */
                                                        /* appears at (148,82) */
    facing /*[0xC2]*/ &= 0xFE;                          /* A05A face right   */

    /* --- walk right, 13 steps of 8 px, x4 0x0C -> 0x24 (x 48 -> 144) ---- */
    bx_hi = 0x0C;                                                 /* A05F   */
    for (n = 13; n; n--) {
        if ((n & 1) == 0) sfx /*[FF75]*/ = 0x1A;      /* A06B footstep       */
        frame = (frame + 1) & 3;                      /* A072 walk cycle     */
        draw_hero(bx_hi, 0x6E);
        wait_frame();
        if (bx_hi != 0x24) {                          /* A082                */
            VID_WINDOW(0, bx_hi, 0x6E, /*CH*/0x02, /*CL*/0x18);  /* erase    */
            bx_hi += 2;                               /* A093 step 8 px      */
        }
    }

    /* --- stand, then raise the crystal (frames 5..8) -------------------- */
    frame = 4;  draw_hero(0x24, 0x6E);                            /* A099   */
    for (n = 5; n; n--) wait_frame();
    for (frame = 5; frame < 9; frame++) {                         /* A0AE   */
        draw_hero(0x24, 0x6E);
        wait_frame(); wait_frame();
    }
    draw_hero(0x24, 0x6E);                                        /* A0CA   */
    GF_DRAW_SWORD();                        /* A0D0 sword held up at (144,86) */

    /* --- the crystal takes off ----------------------------------------- */
    fly_tx = tear_x[tears - 1];  fly_ty = 2;                      /* A0D5   */
    fly_setup();
    VID_SAVE_RECT(fly_x >> 3, fly_y, 0x03, 0x10, 0);              /* A0EB   */
    for (anim_i = 0; anim_i < 2; anim_i++) {          /* two small sparkles  */
        GF_SPARKLE(anim_i, fly_x, fly_y);
        wait_frame();
        VID_RESTORE(fly_x >> 3, fly_y, 0x03, 0x10, 0);
    }
    VID_SAVE_RECT((fly_x >> 3) - 6, fly_y, 0x11, 0x10, 0);        /* A13E   */
    sfx = 0x1B;                                       /* A158 flash sound    */
    for (anim_i = 0; anim_i < 2; anim_i++) {          /* two BIG sparkles    */
        GF_SPARKLE(anim_i | 0x80, fly_x - 0x18, fly_y);
        wait_frame(); wait_frame();
        VID_RESTORE((fly_x >> 3) - 6, fly_y, 0x11, 0x10, 0);
    }
    VID_WINDOW(0, 0x25, 0x52, 0x04, 0x10);   /* A1A4 erase the held crystal  */
    GF_DRAW_SWORD();                                              /* A1B1   */

    VID_SAVE_RECT(fly_x >> 3, fly_y, 0x03, 0x10, 0);              /* A1B6   */
    for (anim_i = 0; anim_i < 4; anim_i++) {                      /* A1D2   */
        GF_SPARKLE(anim_i, fly_x, fly_y);
        wait_frame();
        VID_RESTORE(fly_x >> 3, fly_y, 0x03, 0x10, 0);
    }

    /* --- fly to the HUD slot, sparkling every 3rd move ------------------ */
    sfx_phase = 200;                                              /* A209   */
    do {                                                          /* A20E   */
        if ((++anim_j & 1) == 0) {
            anim_i++;
            if (++sfx_phase > 2) { sfx_phase = 0; sfx = 0x1C; }
        }
        VID_RESTORE(fly_x >> 3, fly_y, 0x03, 0x10, 0);
        n = fly_step();                                           /* A249   */
        VID_SAVE_RECT(fly_x >> 3, fly_y, 0x03, 0x10, 0);
        GF_SPARKLE((anim_i & 1) + 2, fly_x, fly_y);
        wait_frame();
    } while (!n);                                                 /* A27E   */
    VID_RESTORE(fly_x >> 3, fly_y, 0x03, 0x10, 0);

    /* --- it lands: two big flashes, then the icon is painted in --------- */
    VID_SAVE_RECT((fly_x >> 3) - 6, fly_y, 0x11, 0x10, 0);        /* A297   */
    sfx = 0x1B;
    for (anim_i = 0; anim_i < 2; anim_i++) {
        GF_SPARKLE(anim_i | 0x80, fly_x - 0x18, fly_y);
        wait_frame(); wait_frame();
        VID_RESTORE((fly_x >> 3) - 6, fly_y, 0x11, 0x10, 0);
    }
    VID_TEAR_ICON(tear_icon, tear_bx[tears - 1] >> 8, 0);         /* A2FD   */

    VID_SAVE_RECT(fly_x >> 3, fly_y, 0x03, 0x10, 0);              /* A313   */
    for (anim_i = 4; anim_i; anim_i--) {                          /* A32F   */
        GF_SPARKLE(anim_i - 1, fly_x, fly_y);
        wait_frame();
        VID_RESTORE(fly_x >> 3, fly_y, 0x03, 0x10, 0);
    }

    /* --- fanfare, wait for the player, then walk off right -------------- */
    INT60H_PLAY(0);                       /* A363 play mfan.msd @arena:3000 */
    while (!key_return /*[FF26]*/)        /* A371 wait for Return           */
        ;
    INT60H_PLAY(1);                       /* A37B stop the music            */

    VID_WINDOW(0, 0x24, 0x56, 0x06, 0x18);  /* A37D erase the raised sword  */
    for (frame = 8; frame >= 5; frame--) {                        /* A38A   */
        draw_hero(0x24, 0x6E);
        wait_frame(); wait_frame();
    }
    draw_hero(0x24, 0x6E);
    for (n = 5; n; n--) wait_frame();
    VID_WINDOW(0, 0x24, 0x6E, 0x02, 0x18);                        /* A3B6   */

    bx_hi = 0x26;                                                 /* A3C3   */
    for (n = 13; n; n--) {                                        /* A3C9   */
        if ((n & 1) == 0) sfx = 0x1A;
        frame = (frame + 1) & 3;
        draw_hero(bx_hi, 0x6E);
        wait_frame();
        if (bx_hi != 0x3E) {
            VID_WINDOW(0, bx_hi, 0x6E, 0x02, 0x18);
            bx_hi += 2;
        }
    }
    VID_WINDOW(0, 0x3E, 0x6E, 0x06, 0x18);   /* A3FD tail call; then ret    */
}
