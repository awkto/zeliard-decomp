/*
 * Zeliard (DOS, 1990) — STICK.BIN resident kernel, hand-cleaned C reconstruction.
 *
 * Source: zeliard/STICK.BIN (4150 bytes, raw, loaded at BASE:0100), disasm/STICK.asm,
 * Ghidra decompile cross-checked against the listing.  Every function carries its
 * original address.  This is *readable pseudo-C*, not a buildable translation unit:
 * 16-bit real-mode segment arithmetic, DOS calls and far pointers are written out
 * explicitly so that each line can be matched back to the asm.
 *
 * Conventions
 *   BASE            the 256 KB arena segment (CS of all game code)
 *   ARENA           BASE + 0x1000  (64 KB data arena, == [cs:FF2C])
 *   CACHE           BASE + 0x2000  (64 KB code/data cache filled by GAME.BIN)
 *   STAGE           BASE + 0x3000  (64 KB staging area for compressed streams)
 *   FF_xx           byte/word in the state page BASE:FF00..FFFF.  NOTE: this page is
 *                   also offsets 0x00..0xFF of the music-driver segment (BASE+0xFF0),
 *                   so FF08/FF09/FF0B/FF26/FF27/FF28/FF75 are really driver variables
 *                   that the kernel pokes through the alias.
 *   video->xxx      video-driver vector table at BASE:2000 (see docs/VIDEO_DRIVERS.md)
 *   int60(ax,cl)    music driver INT 60h (installed by ZELIARD.EXE to (BASE+FF0):0103)
 *
 * Interrupt hooks installed by ZELIARD.EXE (disasm/ZELIARD.asm 0274..0292):
 *   INT 09h -> BASE:0100 (jmp 02C5)  keyboard
 *   INT 08h -> BASE:0103 (jmp 0250)  timer (PIT reprogrammed to 0x13B1 = 236.7 Hz)
 *   INT 24h -> BASE:0106 (jmp 0F18)  critical-error handler
 *   INT 61h -> BASE:0109 (jmp 05FD)  "read input state" service for overlays
 *   INT 60h -> (BASE+FF0):0103       music driver dispatcher (not in this file)
 */

/* ------------------------------------------------------------------------- */
/* Service vector table  (BASE:010C .. 0121, 11 near-call slots)              */
/* ------------------------------------------------------------------------- */

typedef void (*near_fn)(void);

const near_fn vectors[11] /* 0x010C */ = {
    /* [0x10C] */ svc_resource,        /* 0x0A84  load resource, AL = mode 0..6      */
    /* [0x10E] */ svc_nop,             /* 0x0F17  `ret` — no caller anywhere         */
    /* [0x110] */ svc_hotkey_exit,     /* 0x06AC  Ctrl+Q  -> "Exit to DOS" dialog    */
    /* [0x112] */ svc_hotkey_pause,    /* 0x0723  Esc     -> PAUSE                    */
    /* [0x114] */ svc_hotkey_speed,    /* 0x07B6  F9      -> "Speed change" dialog   */
    /* [0x116] */ svc_hotkey_joy_on,   /* 0x0881  Ctrl+J  -> calibrate/enable joystick */
    /* [0x118] */ svc_hotkey_joy_off,  /* 0x08EF  Ctrl+K  -> disable joystick        */
    /* [0x11A] */ svc_random,          /* 0x0918  AX = pseudo-random                  */
    /* [0x11C] */ svc_find_files,      /* 0x09D6  DOS FindFirst/Next -> name list    */
    /* [0x11E] */ svc_hotkey_restore,  /* 0x092D  F7      -> "Restore Game" dialog   */
    /* [0x120] */ joy_calibrate,       /* 0x089E  calibrate joystick (no key check)  */
};
/* 0x0122 is the first code byte (see timer_poll_fire_keys) — the table ends there. */

/* ------------------------------------------------------------------------- */
/* Kernel-private data (in the code segment)                                  */
/* ------------------------------------------------------------------------- */

uint8_t  tick_div5        /* 0x02BC */ = 0x0A; /* counts down to 0, reloaded with 5 */
uint8_t  tick_div13       /* 0x02BD */ = 0x0D; /* chain to BIOS INT 8 every 13 ticks */
uint8_t  fire1_armed      /* 0x02BE */;        /* Space   : 0xFF once released       */
uint8_t  fire2_armed      /* 0x02BF */;        /* Alt                                */
uint8_t  joyA_armed       /* 0x02C0 */;        /* joystick button A                  */
uint8_t  joyB_armed       /* 0x02C1 */;        /* joystick button B                  */
uint8_t  f1_armed         /* 0x02C2 */;        /* F1  (music toggle)                 */
uint8_t  f2_armed         /* 0x02C3 */;        /* F2  (sfx toggle)                   */
uint8_t  tick8            /* 0x02C4 */;        /* free-running tick byte (INT 24 wait) */

uint8_t  dir_arrows       /* 0x05C1 */;  /* bit 1=up 2=down 4=left 8=right (keypad)  */
uint8_t  dir_keypad_diag  /* 0x05C2 */;  /* lo nibble | hi nibble = diagonal combos  */
uint8_t  dir_letters      /* 0x05C3 */;  /* U/M/H/K as up/down/left/right            */
uint8_t  dir_letter_diag  /* 0x05C4 */;  /* Y/I/N/O diagonals                        */
uint8_t  kbd_e0_prefix    /* 0x05C5 */;  /* last byte was E0/E1 prefix -> skip next  */
uint16_t joy_center_x     /* 0x05C6 */;
uint16_t joy_center_y     /* 0x05C8 */;

/* scan code (1-based, <0x54) -> ASCII, unshifted / shifted (Shift = FF18 bit 1) */
const char ascii_plain[0x58] /* 0x0511 */ =
    "\0" "1234567890" "\0\0\b\0" "QWERTYUIOP" "\0\0\r\0" "ASDFGHJKL" "\0\0\0\0\0" "ZXCVBNM";
const char ascii_shift[0x58] /* 0x0569 */ =
    "\0" "!@\0$%\0\0\0()" "\0\0\b\0" "QWERTYUIOP{}" "\r\0" "ASDFGHJKL:" "\0\0\0\0" "ZXCVBNM";

const char str_exit    [] /* 0x070A */ = "Exit to DOS\rSure?(Y/N)\xFF";
const char str_pause   [] /* 0x07B0 */ = "PAUSE\xFF";
const char str_speed   [] /* 0x0845 */ = "Speed change\rSelect 0-9:\xFF";
const char str_restore [] /* 0x0983 */ = "Restore Game\rSure?(Y/N)\xFF";

uint16_t rng_state        /* 0x092B */;

uint16_t find_out_off, find_out_seg   /* 0x0A51, 0x0A53 */;   /* saved ES:DI */
uint16_t find_spec_off, find_spec_seg /* 0x0A55, 0x0A57 */;   /* saved DS:DX */
uint8_t  dta[43]          /* 0x0A59 */;  /* DOS DTA; +0x1E (0x0A77) = ASCIIZ file name */

const near_fn mode_table[6] /* 0x0ACA */ = {
    mode1_load_system_record,  /* 0x0AD6 */
    mode2_load_decompress,     /* 0x0AFF */
    mode3_load_raw,            /* 0x0C2F */
    mode4_install_sword_block, /* 0x0B6F */
    mode5_load_music,          /* 0x0BAE */
    mode6_probe,               /* 0x0C24 */
};

/* mode 4: sword block # -> which pointer word of the sword.grp header (at CACHE:1800) */
const uint16_t sword_ptr_off[7] /* 0x0BA0 */ = {
    0x1800, 0x1800, 0x1800, 0x1800, 0x1802, 0x1802, 0x1804 };

char archive_name[]     /* 0x0D3B */ = "zelres1.sar";          /* 0x0D41 = digit */
char str_insert_disk[]  /* 0x0D47 */ = "    Please insert DISK1\r      and press any key\xFF";
                                                               /* 0x0D5E = digit */
uint8_t  cur_resource   /* 0x0D79 */;  /* request byte +1 (0 = external file)  */
uint32_t entry_offset   /* 0x0D7A */;  /* SAR directory entry read here        */
char     dummy_fcb[]    /* 0x0D7E */ = "dummy";                /* used for INT 21/10h */

far_ptr  request_ptr    /* 0x0F5C */;  /* DS:SI of the caller's request block  */
far_ptr  dest_ptr       /* 0x0F60 */;  /* ES:DI destination                    */
uint16_t payload_len    /* 0x0F64 */;  /* low word of the entry length         */
uint16_t payload_len_hi /* 0x0F66 */;

/* "System records": 11-byte request blocks {archive, res#, name[9]} used by mode 1.
 * 0..30  = the 31 cavern maps  MP10.MDT .. MPA0.MDT  (ZELRES3[20..50])
 * 31     = {1, 0, "        "}  external file with a blank 8-char name (the save-game
 *          "USER" slot — the caller patches the name in before requesting it)
 * 32..41 = town maps CMAP MRMP STMP BSMP HLMP TMMP DRMP LLMP PRMP ESMP .MDT (ZELRES2[36..45])
 *          reached with AH = 0x80 | (n-32)                                             */
struct sysrec { uint8_t archive, resource; char name[9]; }
       sysrec[42]        /* 0x0F68 .. 0x1135 (end of file) */;

/* ------------------------------------------------------------------------- */
/* State-page variables the kernel touches (BASE:FFxx)                        */
/* ------------------------------------------------------------------------- */

far_fn   FF00_loader_exit;    /* far entry into ZELIARD.EXE: AX=0 normal exit,
                                 AX=DOS error -> abort with message, DS:DX = request */
far_fn   FF04_old_int8;
uint8_t  FF08_snd_flag;       /* sound driver [0x08] (set by GAME.BIN; not used here) */
uint8_t  FF09_snd_idle;       /* sound driver [0x09]; exit waits until != 0 */
uint8_t  FF0A_joystick_cfg;   /* RESOURCE.CFG JOYSTICK:YES -> 0xFF */
uint8_t  FF0B_music_paused;   /* music driver [0x0B] (set by int60 AX=3) */
far_fn   FF0C_music_tick;     /* (BASE+FF0):0100, called every timer tick */
far_fn   FF10_sound_tick;     /* (BASE+FF0):1100, called every timer tick */
uint8_t  FF14_video_mode;     /* 0 = EGA (selects stream A in mode-2 containers) */
uint8_t  FF15_mt32;           /* musicDrv == MSCMT.DRV (selects blob B in mode 5) */
uint8_t  FF16_fire_keys;      /* bit0 = Space held, bit1 = Alt held */
uint8_t  FF17_dir_keys;       /* 1=up 2=down 4=left 8=right, keyboard only */
uint16_t FF18_hotkeys;        /* held-key bitmask, see kbd_isr() */
uint8_t  FF1A_tick8;          /* free-running byte tick (overlays pace frames with it) */
uint16_t FF1B_tick16;         /* free-running word tick ("frame counter") */
uint8_t  FF1D_fire1_event;    /* 0xFF when Space / joy A was newly pressed; consumer clears */
uint8_t  FF1E_fire2_event;    /* 0xFF when Alt   / joy B was newly pressed */
near_fn  FF1F_tick_hook;      /* if high byte (FF20) != 0: near-called every tick */
uint8_t  FF27_sfx_off;        /* sound driver [0x27]; F2 toggles */
uint8_t  FF28_music_off;      /* music driver [0x28]; F1 toggles via int60 AX=2 */
uint8_t  FF29_ascii;          /* last typed ASCII char (kbd_scan_to_ascii) */
uint8_t  FF33_speed;          /* frame period unit, 1..10 (F9 dialog); loader inits 5 */
uint8_t  FF3B_joy_enabled;    /* joystick calibrated & active */
uint8_t  FF48_joy_dirs;       /* 1=up 2=down 4=left 8=right from the joystick */
uint8_t  FF49_joy_buttons;    /* bit0 = A, bit1 = B */
uint16_t FF50_tick16b;        /* second free-running word tick (shop overlays reset it) */
uint8_t  FF74_text_entry;     /* != 0: letter keys are NOT direction keys */
uint8_t  FF75_sfx_request;    /* sound driver [0x75]: sfx # to start (1 = blip, 2 = dialog) */
uint8_t  FF78_no_disk_prompt; /* != 0: file-not-found returns CF instead of prompting */
far_fn   FF79_old_int9;

/* ========================================================================= */
/* INT 09h — keyboard                                                        */
/* ========================================================================= */

/* 0x0100: jmp 0x02C5 */
void __interrupt kbd_isr(void) /* 0x02C5 */
{
    push bx,cx,dx,si,di,ds,es;  ds = cs;
    uint8_t scan = inb(0x60);
    if (scan == 0xFF || scan == 0xFE) {           /* keyboard overrun / ACK */
        uint8_t p = inb(0x61); outb(0x61, p | 0x80); outb(0x61, p & 0x7F);
        dir_arrows = dir_keypad_diag = dir_letters = dir_letter_diag = 0;
        outb(0x20, 0x20);
        pop; iret;
    }
    kbd_process_scan(scan);                        /* 0x0326 */
    while (bios_kbd_check())                        /* int 16h AH=1 */
        bios_kbd_read();                            /* int 16h AH=0: drain buffer */
    pop;
    jmp far FF79_old_int9;                          /* BIOS then handles the key */
}

/*
 * 0x0326: update the direction/fire/hotkey state from one scan code.
 * AL = scan code (bit 7 = break).  Codes >= 0xE0 are prefixes and are ignored
 * (after kbd_scan_to_ascii has noted them).
 *
 * Direction keys (all end up in FF17 as 1=up 2=down 4=left 8=right):
 *   0x5C1  Right  4D (Rt arrow) 4E (KP +) | Left 4B (Lt arrow) 2B (\) |
 *          Down   50 (Dn arrow) 4A (KP -) | Up   48 (Up arrow) 29 (`)
 *   0x5C2  47 Home = up+left(5)   49 PgUp = up+right(9<<4)
 *          4F End  = down+left(6<<4)  51 PgDn = down+right(0xA)
 *   0x5C3  (only if FF74 == 0)  25 K = right, 23 H = left, 32 M = down, 16 U = up
 *   0x5C4  (only if FF74 == 0)  15 Y = up+left, 17 I = up+right, 31 N = down+left, 33 O = down+right
 * Fire keys (FF16):  39 Space = bit0, 38 Alt = bit1
 * Hotkey word (FF18), held-state (or on make, xor on break):
 *   0x0001 1C Enter      0x0002 2A/36 Shift   0x0004 1D Ctrl    0x0008 01 Esc
 *   0x0010 10 Q          0x0020 15 Y          0x0040 31 N       0x0080 1F S
 *   0x0100 24 J          0x0200 12 E          0x0400 13 R       0x0800 25 K
 *   0x1000 3B F1         0x2000 3C F2         0x4000 41 F7      0x8000 43 F9
 */
void kbd_process_scan(uint8_t scan) /* 0x0326 */
{
    kbd_scan_to_ascii(scan);                        /* 0x04D0 */
    if (scan >= 0xE0) return;
    uint8_t brk = scan & 0x80, k = scan & 0x7F, bit;

    if      (k == 0x4D || k == 0x4E) bit = 8;
    else if (k == 0x4B || k == 0x2B) bit = 4;
    else if (k == 0x50 || k == 0x4A) bit = 2;
    else if (k == 0x48 || k == 0x29) bit = 1;
    else goto diagonals;
    dir_arrows |= bit; if (brk) dir_arrows ^= bit;
    goto combine;

diagonals:
    if      (k == 0x47) bit = 0x05;
    else if (k == 0x49) bit = 0x90;
    else if (k == 0x4F) bit = 0x60;
    else if (k == 0x51) bit = 0x0A;
    else goto letters;
    dir_keypad_diag |= bit; if (brk) dir_keypad_diag ^= bit;
    goto combine;

letters:
    if (FF74_text_entry) {
        dir_letters = dir_letter_diag = 0;
        goto fire;
    }
    if      (k == 0x25) bit = 8;
    else if (k == 0x23) bit = 4;
    else if (k == 0x32) bit = 2;
    else if (k == 0x16) bit = 1;
    else goto letter_diag;
    dir_letters |= bit; if (brk) dir_letters ^= bit;
    goto hotkeys;

letter_diag:
    if      (k == 0x15) bit = 0x05;
    else if (k == 0x17) bit = 0x90;
    else if (k == 0x31) bit = 0x60;
    else if (k == 0x33) bit = 0x0A;
    else goto fire;
    dir_letter_diag |= bit; if (brk) dir_letter_diag ^= bit;
    goto hotkeys;

fire:
    if      (k == 0x39) bit = 1;
    else if (k == 0x38) bit = 2;
    else goto hotkeys;
    FF16_fire_keys |= bit; if (brk) FF16_fire_keys ^= bit;

hotkeys: {
    uint16_t m;
    switch (k) {
    case 0x25: m = 0x0800; break;  case 0x13: m = 0x0400; break;
    case 0x12: m = 0x0200; break;  case 0x24: m = 0x0100; break;
    case 0x1F: m = 0x0080; break;  case 0x31: m = 0x0040; break;
    case 0x15: m = 0x0020; break;  case 0x10: m = 0x0010; break;
    case 0x01: m = 0x0008; break;  case 0x1D: m = 0x0004; break;
    case 0x36: case 0x2A: m = 0x0002; break;
    case 0x1C: m = 0x0001; break;  case 0x3B: m = 0x1000; break;
    case 0x3C: m = 0x2000; break;  case 0x41: m = 0x4000; break;
    case 0x43: m = 0x8000; break;
    default: goto combine;
    }
    FF18_hotkeys |= m; if (brk) FF18_hotkeys ^= m;
  }

combine:                                            /* 0x0497 */
    FF17_dir_keys = dir_arrows | dir_letters
                  | (dir_keypad_diag & 0x0F) | (dir_keypad_diag >> 4)
                  | (dir_letter_diag & 0x0F) | (dir_letter_diag >> 4);
}

/* 0x04D0: translate a make code into FF29 (used by name entry / speed dialog). */
void kbd_scan_to_ascii(uint8_t scan) /* 0x04D0 */
{
    if (scan >= 0xE0) { kbd_e0_prefix = 0xFF; return; }
    bool skip = kbd_e0_prefix != 0;
    kbd_e0_prefix = 0;
    if (skip) return;
    if (scan & 0x80) return;                        /* break code */
    if (scan >= 0x54) return;
    const char *tbl = (FF18_hotkeys & 0x0002) ? ascii_shift : ascii_plain;
    FF29_ascii = tbl[scan - 1];
}

/* ========================================================================= */
/* INT 08h — timer (236.7 Hz; PIT divisor 0x13B1 set by ZELIARD.EXE @02BF)   */
/* ========================================================================= */

/* 0x0103: jmp 0x0250 */
void __interrupt timer_isr(void) /* 0x0250 */
{
    push all; cld;
    FF10_sound_tick();                              /* far call */
    FF0C_music_tick();                              /* far call */
    if (--tick_div5 == 0) {                         /* every 5th tick (~47 Hz) */
        tick_div5 = 5;
        timer_poll_f1_f2();                         /* 0x01E3 */
        timer_poll_fire_keys();                     /* 0x0122 */
        timer_poll_joy_buttons();                   /* 0x017C */
    }
    FF1A_tick8++;
    FF50_tick16b++;
    FF1B_tick16++;
    tick8++;
    if (FF1F_tick_hook >> 8)                        /* test byte [FF20] */
        FF1F_tick_hook();                           /* near call */
    pop all but ax;
    if (--tick_div13 != 0) { outb(0x20, 0x20); pop ax; iret; }
    tick_div13 = 13;                                /* 236.7 / 13 = 18.2 Hz */
    pop ax;
    jmp far FF04_old_int8;                          /* BIOS clock keeps time */
}

/* 0x0122: edge-detect Space / Alt into FF1D / FF1E.  A latch arms when the key is
 * seen released and fires (event = 0xFF, latch cleared) on the next poll that sees
 * it held — i.e. one event per press, sampled at ~47 Hz. */
void timer_poll_fire_keys(void) /* 0x0122 */
{
    if (fire1_armed) { if (FF16_fire_keys & 1) { fire1_armed = 0; FF1D_fire1_event = 0xFF; } }
    else             { if (!(FF16_fire_keys & 1)) fire1_armed = 0xFF; }

    if (fire2_armed) { if (FF16_fire_keys & 2) { fire2_armed = 0; FF1E_fire2_event = 0xFF; } }
    else             { if (!(FF16_fire_keys & 2)) fire2_armed = 0xFF; }
}

/* 0x017C: same edge detection for joystick buttons (port 201h bit4 = A, bit5 = B,
 * active low).  Only while the joystick is calibrated and configured. */
void timer_poll_joy_buttons(void) /* 0x017C */
{
    if (!FF3B_joy_enabled || !FF0A_joystick_cfg) return;
    uint8_t p = inb(0x201);
    /* 0x0197 */
    if (joyA_armed) { if (!(p & 0x10)) { joyA_armed = 0; FF1D_fire1_event = 0xFF; } }
    else            { if (p & 0x10) joyA_armed = 0xFF; }
    /* 0x01BD */
    if (joyB_armed) { if (!(p & 0x20)) { joyB_armed = 0; FF1E_fire2_event = 0xFF; } }
    else            { if (p & 0x20) joyB_armed = 0xFF; }
}

/* 0x01E3: F1 = toggle music, F2 = toggle sound effects (exact-match on FF18 so no
 * other hotkey may be held).  Both play sfx #1 as confirmation. */
void timer_poll_f1_f2(void) /* 0x01E3 */
{
    if (f1_armed) {
        if (FF18_hotkeys == 0x1000) {
            FF75_sfx_request = 1;
            f1_armed = 0;
            int60(/*AX*/2, /*CL*/FF28_music_off);   /* CL!=0 -> music on, 0 -> off */
        }
    } else if (FF18_hotkeys != 0x1000) f1_armed = 0xFF;

    if (f2_armed) {
        if (FF18_hotkeys != 0x2000) return;
        f2_armed = 0;
        FF27_sfx_off = ~FF27_sfx_off;
        FF75_sfx_request = 1;
    } else if (FF18_hotkeys != 0x2000) f2_armed = 0xFF;
}

/* ========================================================================= */
/* INT 24h — critical error handler                                          */
/* ========================================================================= */

/* 0x0106: jmp 0x0F18.  DI low byte = DOS error code.  Code 2 ("drive not ready",
 * i.e. the disk was swapped out) waits ~1 s (0xF0 ticks of tick8) and returns
 * AL=1 (retry); everything else returns AL=0 (ignore). */
uint8_t __interrupt crit_error_isr(void) /* 0x0F18 */
{
    sti; push all;
    uint8_t code = (uint8_t)di;
    if ((int8_t)code < 0 || code != 2) { pop all; return /*AL*/ 0; }
    tick8 = 0;
    while (tick8 < 0xF0) ;
    pop all; return /*AL*/ 1;
}

/* ========================================================================= */
/* INT 61h — read input state                                                */
/* ========================================================================= */

/* 0x0109: jmp 0x05FD.  Returns AL = direction bits (1 up 2 down 4 left 8 right),
 * AH = fire bits (bit0 = Space/joy A held, bit1 = Alt/joy B held).  Keyboard and
 * joystick are OR-ed.  Caller example: select.bin A0D2 `int 61h; and al,3; jnz`. */
uint16_t __interrupt input_isr(void) /* 0x05FD */
{
    push bx,cx,dx;
    FF48_joy_dirs = 0; FF49_joy_buttons = 0;
    if (FF3B_joy_enabled & FF0A_joystick_cfg) joy_read_state();  /* 0x0630 */
    al = FF17_dir_keys  | FF48_joy_dirs;
    ah = FF16_fire_keys | FF49_joy_buttons;
    pop; iret;
}

/* ========================================================================= */
/* Joystick                                                                  */
/* ========================================================================= */

/* 0x05CA: time both axes of joystick A.  Returns SI = X count, DI = Y count
 * (number of port reads until each axis bit dropped; 0xFFFF-ish when absent). */
void joy_read_axes(uint16_t *si, uint16_t *di) /* 0x05CA */
{
    uint16_t x = 0, y = 0;
    cli;
    outb(0x201, al);                                /* start the one-shots */
    for (int n = 6; n; n--)                         /* let the lines settle */
        if ((inb(0x201) ^ 3) == 0) break;
    uint8_t p;
    do {
        p = inb(0x201);
        x += p & 1;                                 /* bit0 = A-X still high */
        y += (p & 2) >> 1;                          /* bit1 = A-Y still high */
    } while (p & 3);
    sti;
    *si = x; *di = y;
}

/* 0x0630: convert axis counts into FF48 direction bits and FF49 buttons.
 * Thresholds relative to the calibrated centre (cx, cy):
 *   right: x >= cx + 8      left: x <= cx/2 - 8
 *   down : y >= cy + 8      up  : y <= cy/2 - 8        (saturating arithmetic)  */
void joy_read_state(void) /* 0x0630 */
{
    uint16_t x, y, t;
    joy_read_axes(&x, &y);
    t = joy_center_x + 8;       if (t < 8) t = 0xFFFF;   if (x >= t) FF48_joy_dirs |= 8;
    t = (joy_center_x >> 1);    t = t < 8 ? 0 : t - 8;   if (x <= t) FF48_joy_dirs |= 4;
    t = joy_center_y + 8;       if (t < 8) t = 0xFFFF;   if (y >= t) FF48_joy_dirs |= 2;
    t = (joy_center_y >> 1);    t = t < 8 ? 0 : t - 8;   if (y <= t) FF48_joy_dirs |= 1;
    FF49_joy_buttons = (~inb(0x201) >> 4) & 3;
}

/* 0x089E = vector [0x120]: calibrate the joystick at its current (centre) position.
 * Does nothing if already enabled or not configured.  Waits (bounded) for port
 * 201h bit 3 to drop, reads both axes, rejects 0 / 0xFFFF, then enables it.
 * Caller: GAME.BIN A022 (once at boot, right after loading font.grp). */
void joy_calibrate(void) /* 0x089E */
{
    if (FF3B_joy_enabled) return;
    if (!FF0A_joystick_cfg) return;
    uint16_t cx = 0xFFFF;                           /* mov cx,0x103; shl ch,cl -> AH = 8 */
    while (--cx && (inb(0x201) & 0x08)) ;           /* sic: bit 3 = joystick B Y-axis */
    if (cx == 0) return;
    uint16_t x, y;
    joy_read_axes(&x, &y);
    if (x == 0xFFFF || y == 0xFFFF || x == 0 || y == 0) return;
    joy_center_x = x; joy_center_y = y;
    FF3B_joy_enabled = 0xFF;
    FF75_sfx_request = 1;
}

/* ========================================================================= */
/* Hotkey services (polled once per frame by the engine overlays)            */
/* ========================================================================= */

/* helpers shared by the dialogs ------------------------------------------ */

/* 0x099C: save the 0x0C46/0x1028 screen region to 3C80h and draw a filled box
 * (1A46h/1E28h, AL=FF) with the video driver.  Also requests sfx #2. */
void dialog_open(void) /* 0x099C */
{
    FF75_sfx_request = 2;
    video->save_region(/*AX*/0x0C46, /*CX*/0x1028, /*DI*/0x3C80);   /* [0x2026] */
    video->fill_box   (/*BX*/0x1A46, /*CX*/0x1E28, /*AL*/0xFF);     /* [0x2000] */
}

/* 0x09A2: entry into dialog_open that skips the sfx request (insert-disk prompt). */
void dialog_open_silent(void) /* 0x09A2 */ { /* save_region + fill_box as above */ }

/* 0x09BD: restore the region saved by dialog_open. */
void dialog_close(void) /* 0x09BD */
{
    video->restore_region(/*AX*/0x0C46, /*CX*/0x1028, /*DI*/0x3C80); /* [0x2028] */
}

/* 0x09CB: drain DOS stdin (INT 21h AH=6, DL=FF until ZF). */
void flush_stdin(void) /* 0x09CB */
{
    while (dos_direct_console_input() != none) ;
}

/* [0x110] 0x06AC: Ctrl+Q (FF18 == 0x0014) -> "Exit to DOS  Sure?(Y/N)".
 * Y: wait for the sound driver to go idle (FF09) and far-jump to the loader with
 * AX = 0 (clean exit).  N: restore screen, resume music, clear input events.
 * Caller: fight.bin 7155 (first of the per-frame hotkey chain). */
void svc_hotkey_exit(void) /* 0x06AC */
{
    if (FF18_hotkeys != 0x0014) return;
    push ds;
    dialog_open();
    int60(3, 0xFF);                                 /* pause music */
    video->draw_string(/*DS:SI*/ str_exit, /*BX*/0x74, /*CL*/0x52);   /* [0x202A] */
    pop ds;
    uint16_t k;
    do k = FF18_hotkeys; while (!(k & 0x60));       /* 0x20 = Y held, 0x40 = N held */
    if (!(k & 0x20)) {                              /* N */
        dialog_close();
        int60(3, 0);                                /* resume music */
        FF17_dir_keys = 0; FF1D_fire1_event = 0; FF1E_fire2_event = 0;
        return;
    }
    while (FF09_snd_idle == 0) ;                    /* Y */
    ax = 0;
    jmp far FF00_loader_exit;
}

/* [0x112] 0x0723: Esc (FF18 bit 3) -> PAUSE.  Saves the 0x101E/0x0810 region to
 * 3C80h; unless Esc+Ctrl+Shift (FF18 == 0x000E) are all held it also draws a box
 * and "PAUSE".  Music is paused.  While waiting, Esc+Ctrl+Shift blanks the pause
 * box (screen restored) but stays paused.  Any fire event (FF1D/FF1E) resumes.
 * Caller: fight.bin 715A. */
void svc_hotkey_pause(void) /* 0x0723 */
{
    if (!(FF18_hotkeys & 0x0008)) return;
    push ds;
    FF75_sfx_request = 2;
    video->save_region(0x101E, 0x0810, 0x3C80);                     /* [0x2026] */
    if (FF18_hotkeys != 0x000E) {
        video->fill_box(/*BX*/0x201E, /*CX*/0x1010, /*AL*/0xFF);   /* [0x2000] */
        video->draw_string(str_pause, /*BX*/0x8C, /*CL*/0x22);      /* [0x202A] */
    }
    int60(3, 0xFF);
    pop ds;
    for (;;) {
        if (FF18_hotkeys == 0x000E) pause_restore();              /* 0x07A2 */
        if (FF1D_fire1_event || FF1E_fire2_event) break;
    }
    pause_restore();
    FF1D_fire1_event = 0; FF1E_fire2_event = 0;
    int60(3, 0);
}

void pause_restore(void) /* 0x07A2 */
{
    video->restore_region(0x101E, 0x0810, 0x3C80);                  /* [0x2028] */
}

/* [0x114] 0x07B6: F9 (FF18 bit 15) -> "Speed change  Select 0-9:".  Shows the
 * current digit (10 - FF33), reads a digit via FF29, stores FF33 = 10 - digit
 * (so '9' = fastest, '0' = slowest; engines wait 2*FF33 / 4*FF33 ticks per frame).
 * Then waits for any key / direction / fire before restoring the screen.
 * Caller: fight.bin 715F. */
void svc_hotkey_speed(void) /* 0x07B6 */
{
    if (!(FF18_hotkeys & 0x8000)) return;
    dialog_open();
    video->draw_string(str_speed, /*BX*/0x74, /*CL*/0x52);          /* [0x202A] */
    while (FF18_hotkeys & 0x8000) ;                 /* wait for F9 release */
    uint8_t d = 10 - FF33_speed;
    d = read_digit(d);                              /* 0x085E; AL unchanged on Esc */
    video->draw_char(/*AL*/ d + '0', /*AH*/1, /*BX*/0xCC, /*CL*/0x5A);  /* [0x2022] */
    FF33_speed = 10 - d;
    FF75_sfx_request = 1;
    flush_stdin();
    FF17_dir_keys = 0; FF1D_fire1_event = 0; FF1E_fire2_event = 0;
    while (dos_direct_console_input() == none
           && !(FF17_dir_keys | FF1D_fire1_event | FF1E_fire2_event)) ;
    dialog_close();
    FF17_dir_keys = 0; FF1D_fire1_event = 0; FF1E_fire2_event = 0;
}

/* 0x085E: wait for a typed digit in FF29.  Returns AL = 0..9 with CF clear.
 * The Esc (0x1B) branch returns CF set with AL untouched, but neither ASCII
 * table ever produces 0x1B, so in practice this only returns on a digit. */
uint8_t read_digit(uint8_t al) /* 0x085E */
{
    for (;;) {
        FF29_ascii = 0;                             /* 0x085E; loop re-enters at 0x0863 */
        while (FF29_ascii == 0) ;
        uint8_t c = FF29_ascii;
        if (c == 0x1B) { cf = 1; return al; }
        c -= '0';
        if (c < 10) { cf = 0; return c; }
    }
}

/* [0x116] 0x0881: Ctrl+J (FF18 == 0x0104) -> calibrate and enable the joystick,
 * then wait for the keys to be released.  Caller: fight.bin 7164. */
void svc_hotkey_joy_on(void) /* 0x0881 */
{
    if (FF18_hotkeys != 0x0104) return;
    joy_calibrate();                                /* 0x089E */
    FF17_dir_keys = 0;
    while (FF18_hotkeys == 0x0104) ;
}

/* [0x118] 0x08EF: Ctrl+K (FF18 == 0x0804) -> disable the joystick (if enabled),
 * then wait for release.  Caller: fight.bin 7169. */
void svc_hotkey_joy_off(void) /* 0x08EF */
{
    if (FF18_hotkeys != 0x0804) return;
    if (!FF3B_joy_enabled) return;
    FF75_sfx_request = 1;
    FF3B_joy_enabled = 0;
    while (FF18_hotkeys == 0x0804) ;
}

/* [0x11E] 0x092D: F7 (FF18 == 0x4000) -> "Restore Game  Sure?(Y/N)".
 * Returns CF=1 only when the player answered Y; CF=0 when F7 is not held or on N.
 * Callers test `jnc skip` and otherwise run their restore routine:
 *   fight.bin 716E `call [cs:0x11E] / jnc 7178 / call 78D7`, town.bin 68DC.
 * (asm: `clc / jz prompt / ret` ... `test ax,0x20 / pushf ... popf / stc / jz clc_ret`) */
void svc_hotkey_restore(void) /* 0x092D */
{
    cf = 0;
    if (FF18_hotkeys != 0x4000) return;             /* CF = 0 */
    push ds;
    dialog_open();
    int60(3, 0xFF);
    video->draw_string(str_restore, /*BX*/0x74, /*CL*/0x52);        /* [0x202A] */
    pop ds;
    uint16_t k;
    do k = FF18_hotkeys; while (!(k & 0x60));       /* 0x20 = Y held, 0x40 = N held */
    bool yes = (k & 0x20) != 0;                     /* pushf */
    dialog_close();
    FF17_dir_keys = 0; FF1D_fire1_event = 0; FF1E_fire2_event = 0;
    int60(3, 0);                                    /* popf */
    cf = yes;                                       /* stc; jz -> clc */
}

/* [0x11A] 0x0918: pseudo-random number.  AX = FF1B; AL += AH (with carry into AH);
 * rng_state += AX; return AX = rng_state.  Callers: gfmcga 31B5, fight 88C3,
 * kingpro A328 (`call [cs:0x11A]; or al,al; jz`). */
uint16_t svc_random(void) /* 0x0918 */
{
    uint16_t ax = FF1B_tick16;
    uint8_t lo = (uint8_t)ax + (ax >> 8);
    uint8_t hi = (ax >> 8) + (lo < (uint8_t)ax);    /* adc ah,0 */
    ax = (hi << 8) | lo;
    rng_state += ax;
    return rng_state;
}

/* [0x11C] 0x09D6: directory listing.  In: ES:DI = output buffer (0xAF6 bytes),
 * DS:DX = ASCIIZ file spec (wildcards OK).  Out buffer layout:
 *   +0      count
 *   +1      uint16 ptr[255]  -> DI + 0x201 + 9*i  (pre-filled for all 255 slots)
 *   +0x201  char name[255][9] — file name up to the '.' (no extension), unterminated
 * Uses cs:0A59 as DTA; FindFirst attribute = DX (sloppy: CX is loaded from DX).
 * Stops after 254 files or when FindNext fails.  Callers: kenjpro A43F
 * (DI=E000, DX=A516), town.bin 76A1 — save-game file lists. */
void svc_find_files(void) /* 0x09D6 */
{
    push ds;
    find_out_off = di; find_out_seg = es; find_spec_off = dx; find_spec_seg = ds;
    memset(es:di, 0, 0xAF6);
    uint16_t *p = es:(di + 1), a = di + 0x201;
    for (int i = 0; i < 0xFF; i++) { *p++ = a; a += 9; }
    dos_set_dta(cs:dta);                            /* INT 21h AH=1A */
    if (!dos_find_first(find_spec, /*attr*/ dx)) {  /* INT 21h AH=4E, CX = DX */
        uint16_t slot = di + 0x201;
        for (int n = 0xFE; n; n--) {
            es:[di]++;                              /* count */
            const char *s = dta + 0x1E;
            for (int c = 8; c && *s != '.'; c--) es:[slot++] = *s++;
            if (dos_find_next()) break;             /* INT 21h AH=4F */
            slot = (slot_start += 9);
        }
    }
    pop ds;
}

/* [0x10E] 0x0F17: empty service — never called. */
void svc_nop(void) /* 0x0F17 */ { }

/* ========================================================================= */
/* [0x10C] Resource service                                                  */
/* ========================================================================= */

/*
 * In : AL = mode, DS:SI = request block {archive, res#, name...}, ES:DI = destination,
 *      AH = record index (mode 1) / sword block (mode 4), BX = entry vector (mode 0).
 * Out: CF from the mode handler (set = file not found with FF78 != 0); other flags
 *      restored; DI/SI/DS/ES preserved.  DOS errors never return: see load_fail().
 *
 * mode 0 (0x0C01)  swap BASE:3000..9FFF with CACHE:9000..FFFF, then `jmp [cs:BX]`
 *                   (BX -> word holding the entry point of the swapped-in code)
 * mode 1 (0x0AD6)  load system record #AH raw to BASE:C000 (maps, save slot)
 * mode 2 (0x0AFF)  load + decompress to ES:DI
 * mode 3 (0x0C2F)  load raw to ES:DI
 * mode 4 (0x0B6F)  copy sword block #AH from CACHE to ARENA:B000, relocate header
 * mode 5 (0x0BAE)  load music blob (MT-32 or other variant) to ES:DI
 * mode 6 (0x0C24)  probe: open/seek only, length -> payload_len
 * mode >= 7        ignored
 */
void svc_resource(uint8_t al) /* 0x0A84 */
{
    if (al == 0) mode0_swap_and_jump();             /* jmp 0x0C01, no register save */
    /* 0x0A8B */
    push di, si, ds, es;
    request_ptr = ds:si;
    dest_ptr    = es:di;
    pushf; cld;
    if (al < 7) mode_table[al - 1]();               /* call [cs:bp+0x0ACA] */
    /* 0x0AB8: keep the handler's CF, restore everything else from the pushed flags */
    flags = (saved_flags & ~1) | (flags & 1);
    pop es, ds, si, di;
}

/* mode 0, 0x0C01: exchange 0x3800 words between CACHE:9000 and BASE:3000 (the
 * renderer at 3000 + engine overlay at 6000, 28 KB), then jump through [cs:BX].
 * GAME.BIN fills CACHE:9000 with gf*.bin + fight.bin at boot; town.bin 703D and
 * fight.bin 7DBC use this to flip between the town and fight code sets:
 *   town.bin 7038: mov bx,0x6002 / xor al,al / jmp [cs:0x10C]   */
void mode0_swap_and_jump(void) /* 0x0C01 */
{
    push ds, bx;
    uint16_t far *src = CACHE:0x9000;               /* ds = cs + 0x2000 */
    uint16_t far *dst = BASE:0x3000;                /* es = cs           */
    for (int n = 0x3800; n; n--) { uint16_t t = *src; *src++ = *dst; *dst++ = t; }
    pop bx, ds;
    jmp [cs:bx];
}

/* mode 1, 0x0AD6: AH = system record.  Bit 7 set -> index (AH & 0x7F) + 0x20 (the
 * town maps).  Destination is forced to BASE:C000; the request pointer is redirected
 * to sysrec[i] and the raw loader is entered.  Caller: town.bin 701A
 *   `mov ah,al / mov al,1 / call [cs:0x10C]`. */
void mode1_load_system_record(void) /* 0x0AD6 */
{
    dest_ptr = cs:0xC000;
    uint8_t i = ah;
    if (i & 0x80) i = (i & 0x7F) + 0x20;
    request_ptr = cs:(0x0F68 + i * 11);
    goto mode3_load_raw;                            /* jmp 0x0C2F */
}

/* mode 2, 0x0AFF: load into STAGE:0000 and decompress into the original ES:DI.
 * Container: byte0 == 0 -> single stream follows (length-1 bytes);
 *            byte0 != 0 -> {u8 flag, u16 lenA, u16 lenB} then stream A, stream B;
 *            FF14 == 0 (EGA) uses A, other video modes seek past A and use B. */
void mode2_load_decompress(void) /* 0x0AFF */
{
    far_ptr target = dest_ptr;                      /* les di,[F60]; push di; push es */
    dest_ptr = STAGE:0000;                          /* es = cs + 0x3000 */
    uint16_t h = sar_open_entry();                  /* 0x0C42 -> AX = handle */
    read_payload(h, 1);                             /* first byte */
    uint16_t len = payload_len - 1;
    if (STAGE[0] != 0) {
        dest_ptr = STAGE:0000;
        read_payload(h, 4);                         /* lenA, lenB */
        len = STAGE[0..1];                          /* lenA */
        if (FF14_video_mode != 0) {
            dos_lseek(h, /*cur*/1, len);            /* skip stream A */
            len = STAGE[2..3];                      /* lenB */
        }
    }
    dest_ptr = STAGE:0000;
    read_payload(h, len);
    sar_close(h);                                   /* 0x0D93 */
    dx = len; es:di = target;
    decompress();                                   /* jmp 0x0D9D */
}

/* mode 3, 0x0C2F: open, read payload_len bytes to dest, close.  CF propagated from
 * sar_open_entry (file not found while FF78 != 0).  Caller: GAME.BIN A0B4
 *   `mov si,0xA270 (town.bin) / mov di,0x6000 / mov al,3 / call [cs:0x10C]`. */
void mode3_load_raw(void) /* 0x0C2F */
{
    uint16_t h = sar_open_entry();
    if (cf) return;
    read_payload(h, payload_len);
    sar_close(h);
}

/* mode 4, 0x0B6F: sword.grp lives decompressed at CACHE:1800 (GAME.BIN A14C, with its
 * three sub-resource pointers relocated by +0x1800).  Block #AH (0..6) selects one
 * of those sub-resources through sword_ptr_off[]; 0x800 words are copied to
 * ARENA:B000 and the first 15 words (the {data_off, ptr[14]} header) get +0xB000.
 * Caller: fight.bin 8F9E `mov ah,[0x92] / mov al,4 / call [cs:0x10C]`. */
void mode4_install_sword_block(void) /* 0x0B6F */
{
    uint16_t tab = sword_ptr_off[ah];
    uint16_t far *src = CACHE:( *(uint16_t far *)CACHE:tab );
    uint16_t far *dst = ARENA:0xB000;
    for (int n = 0x800; n; n--) *dst++ = *src++;
    dst = ARENA:0xB000;
    for (int n = 15; n; n--) *dst++ += 0xB000;
}

/* mode 5, 0x0BAE: music score with two variants: {u16 lenA, u16 lenB} + blobA + blobB.
 * FF15 == 0 (not MT-32) seeks past blobA and reads blobB; MT-32 reads blobA.
 * The header is read into STAGE:0000; the blob goes to the caller's ES:DI.
 * Caller: fight.bin 7DAF `mov es,[cs:FF2C] / mov di,0x3000 / mov al,5 / call [cs:0x10C]`. */
void mode5_load_music(void) /* 0x0BAE */
{
    far_ptr target = dest_ptr;
    dest_ptr = STAGE:0000;
    uint16_t h = sar_open_entry();
    read_payload(h, 4);
    uint16_t len = STAGE[0..1];                     /* lenA */
    if (FF15_mt32 == 0) {
        dos_lseek(h, /*cur*/1, len);
        len = STAGE[2..3];                          /* lenB */
    }
    dest_ptr = target;
    read_payload(h, len);
    sar_close(h);
}

/* mode 6, 0x0C24: open + seek to the entry, leave payload_len, close.  CF as mode 3.
 * Caller: kenjpro A42E `mov si,0xA907 / mov al,6 / call [cs:0x10C]` (does the save
 * file exist?). */
void mode6_probe(void) /* 0x0C24 */
{
    uint16_t h = sar_open_entry();
    if (cf) return;
    sar_close(h);
}

/* ------------------------------------------------------------------------- */
/* SAR archive access                                                        */
/* ------------------------------------------------------------------------- */

/*
 * 0x0C42: open the file named by the request block and position it on the entry.
 *   request[0]  archive 0..2  -> ASCII digit patched into "zelres1.sar" (0x0D41)
 *                               and into the insert-disk prompt (0x0D5E)
 *   request[1]  resource# (1-based).  0 -> open the external file whose ASCIIZ
 *               name starts at request+2 instead (save games).
 * Returns AX = handle, payload_len = entry length (0xFFFF:FFFF for external files,
 * i.e. "read until EOF"), CF clear.
 * File-not-found (DOS error 2): if FF78 != 0 -> disk reset + return CF set;
 * otherwise show "Please insert DISKn and press any key", wait, reset and retry.
 * Any other DOS error -> load_fail().
 */
uint16_t sar_open_entry(void) /* 0x0C42 */
{
    payload_len = 0xFFFF; payload_len_hi = 0xFFFF;
    const uint8_t far *req = request_ptr;           /* lds bx,[F5C] */
    archive_name[6] = str_insert_disk[0x17] = req[0] + '1';
    cur_resource = req[1];
    const char far *name = req + 2;                 /* DS:DX */
    if (cur_resource != 0) name = cs:archive_name;

    uint16_t h;
    for (;;) {
        if (dos_open(name, /*AL*/0, &h)) {          /* INT 21h AH=3D */
            if (ax != 2) load_fail();               /* 0x0F52 */
            if (FF78_no_disk_prompt) {
                dos_disk_reset();                   /* INT 21h AH=0D */
                dos_fcb_close(cs:dummy_fcb);        /* INT 21h AH=10, DX=0D7E */
                cf = 1; return;
            }
            push es;
            dialog_open_silent();                   /* 0x09A2 = dialog_open w/o FF75 */
            video->draw_string(str_insert_disk, /*BX*/0x6C, /*CL*/0x4A);
            flush_stdin();
            FF1D_fire1_event = 0;
            while (dos_direct_console_input() == none && !FF1D_fire1_event) ;
            dialog_close();
            pop es;
            dos_disk_reset();
            dos_fcb_close(cs:dummy_fcb);
            continue;                               /* jmp 0x0C42 */
        }
        break;
    }
    FF1D_fire1_event = 0;
    if (cur_resource == 0) return h;                /* external file: whole file */

    /* 0x0CEB: header offset[res#-1] -> entry_offset; seek; read length dword */
    if (dos_lseek(h, /*set*/0, (uint32_t)(cur_resource - 1) * 4)) load_fail();
    if (dos_read(h, cs:&entry_offset, 4))           load_fail();
    dos_lseek(h, 0, entry_offset);
    if (dos_read(h, cs:&payload_len, 4))            load_fail();
    return h;
}

/* 0x0D84: read CX bytes from handle BX to dest_ptr (DOS INT 21h AH=3F). */
void read_payload(uint16_t h, uint16_t cx) /* 0x0D84 */
{
    if (dos_read(h, dest_ptr, cx)) load_fail();
}

/* 0x0D93: close handle BX. */
void sar_close(uint16_t h) /* 0x0D93 */
{
    if (dos_close(h)) load_fail();
}

/* 0x0F52: unrecoverable DOS error — hand AX (error code) and DS:DX = request block
 * to the loader, which prints a message and exits (ZELIARD.asm 02D9). */
void load_fail(void) /* 0x0F52 */
{
    ds:dx = request_ptr;
    jmp far FF00_loader_exit;
}

/* ------------------------------------------------------------------------- */
/* Decompressor (mode 2).  Stream at STAGE:0000, DX = stream length, ES:DI = out */
/* ------------------------------------------------------------------------- */

/* 0x0D9D: DS = STAGE, SI = 0, then dispatch on (first byte & 7). */
void decompress(void) /* 0x0D9D */
{
    push ds; ds = STAGE; si = 0;
    decompress_dispatch();                          /* 0x0DAD */
    pop ds;
}

void decompress_dispatch(void) /* 0x0DAD */
{
    uint8_t op = *si++; dx--;
    static const near_fn ops[8] /* 0x0DBC */ = {
        op0_stored, op1_table_hi, op2_marker_hi, op3_table_lo,
        op4_marker_lo, op5_double, op6_table_byte, op7_escape };
    ops[op & 7]();                                  /* jmp [cs:bx+0x0DBC] */
}

/* op 0, 0x0DCC: copy the remaining DX bytes literally. */
void op0_stored(void)
{
    rep_movsb(dx);
}

/* op 1, 0x0DD1: table RLE keyed on the HIGH nibble.  Head = {key,val} pairs
 * (key low nibble is 0) terminated by 0xFF; a stream byte whose high nibble
 * equals a key expands to val * (low nibble + 2); otherwise literal. */
void op1_table_hi(void)
{
    uint8_t *table = si;                            /* bp = si */
    skip_table_ff();                                /* 0x0E08 */
    do {
        uint8_t b = *si++;
        uint16_t n = op1_lookup(table, &b);         /* 0x0DE0 */
        rep_stosb(b, n);
    } while (--dx);
}

uint16_t op1_lookup(uint8_t *t, uint8_t *b) /* 0x0DE0 */
{
    uint8_t hi = *b & 0xF0;
    for (;; t += 2) {
        if (t[0] & 0x0F) return 1;                  /* end marker (0xFF): literal */
        if (t[0] == hi) { uint16_t n = (*b & 0x0F) + 2; *b = t[1]; return n; }
    }
}

/* 0x0E08: advance SI past {key,val} pairs up to and including the 0xFF byte. */
void skip_table_ff(void)
{
    for (;;) {
        uint8_t k = *si++; dx--;
        if (k == 0xFF) return;
        si++; dx--;
    }
}

/* op 2, 0x0E13: marker RLE.  First byte M; byte with high nibble == M:
 * next byte repeated (low nibble + 3) times. */
void op2_marker_hi(void)
{
    uint8_t m = *si++; dx--;
    do {
        uint8_t b = *si++; uint16_t n = 1;
        if ((b & 0xF0) == m) { n = (b & 0x0F) + 3; b = *si++; dx--; }
        rep_stosb(b, n);
    } while (--dx);
}

/* op 3, 0x0E34: as op 1 with the nibbles swapped: key in the LOW nibble
 * (keys have high nibble 0), count = high nibble + 2. */
void op3_table_lo(void)
{
    uint8_t *table = si;
    skip_table_ff();
    do {
        uint8_t b = *si++;
        uint16_t n = op3_lookup(table, &b);         /* 0x0E43 */
        rep_stosb(b, n);
    } while (--dx);
}

uint16_t op3_lookup(uint8_t *t, uint8_t *b) /* 0x0E43 */
{
    uint8_t lo = *b & 0x0F;
    for (;; t += 2) {
        if (t[0] & 0xF0) return 1;
        if (t[0] == lo) { uint16_t n = (*b >> 4) + 2; *b = t[1]; return n; }
    }
}

/* op 4, 0x0E73: as op 2 with nibbles swapped: match low nibble, count = high + 3. */
void op4_marker_lo(void)
{
    uint8_t m = *si++; dx--;
    do {
        uint8_t b = *si++; uint16_t n = 1;
        if ((b & 0x0F) == m) { n = (b >> 4) + 3; b = *si++; dx--; }
        rep_stosb(b, n);
    } while (--dx);
}

/* op 5, 0x0E9C: doubled byte.  `B B n` -> B repeated n+2 times; else literal. */
void op5_double(void)
{
    do {
        uint8_t b = *si++; uint16_t n = 1;
        if (si[0] == b) { n = si[1] + 2; si += 2; dx -= 2; }
        rep_stosb(b, n);
    } while (--dx);
}

/* op 6, 0x0EBA: byte-keyed {key,val} word table terminated by 0xFFFF; a stream
 * byte equal to a key is followed by count n and expands to val * (n+2). */
void op6_table_byte(void)
{
    uint8_t *table = si;
    do { uint16_t w = *(uint16_t *)si; si += 2; dx -= 2; if (w == 0xFFFF) break; } while (1);
    do {
        uint8_t b = *si++;
        uint16_t n = op6_lookup(table, &b);         /* 0x0ECF */
        rep_stosb(b, n);
    } while (--dx);
}

uint16_t op6_lookup(uint8_t *t, uint8_t *b) /* 0x0ECF */
{
    for (;; t += 2) {
        if (*(uint16_t *)t == 0xFFFF) return 1;
        if (t[0] == *b) { uint16_t n = *si++ + 2; dx--; *b = t[1]; return n; }
    }
}

/* op 7, 0x0EF5: escape byte E.  `E v n` -> v repeated n+3 times; else literal. */
void op7_escape(void)
{
    uint8_t e = *si++; dx--;
    do {
        uint8_t b = *si++; uint16_t n = 1;
        if (b == e) { uint8_t v = *si++, c = *si++; b = v; n = c + 3; dx -= 2; }
        rep_stosb(b, n);
    } while (--dx);
}
