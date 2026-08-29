# Video drivers — GM{EGA,CGA,HGC,MCGA,TGA}.BIN (issue #16, video half)

Exactly one driver is loaded raw at `BASE:2000` by ZELIARD.EXE according to the
`videoDrv` line of RESOURCE.CFG. Mode index `[BASE:FF14]` and the file chosen
(ZELIARD.EXE name table @0x8e7 / record table @0x7FA):

| `[FF14]` | RESOURCE.CFG | file | INT 10h mode set by ZELIARD.EXE (jump table @0x588) | framebuffer |
|---|---|---|---|---|
| 0 | `ega`  | GMEGA.BIN  (3736 b) | `AX=000Eh` @0x594 — 640×200×16 planar | A000, 80 b/row, 4 planes |
| 1 | `cga`  | GMCGA.BIN  (3565 b) | `AX=0005h` @0x59A — 320×200×4 (burst off → cyan/red/white on RGB) | B800, 80 b/row, 2 bpp, odd rows at +0x2000 |
| 2 | `cga2` | GMCGA.BIN (same file) | `AX=0006h` @0x5A0 — 640×200×2 | B800; the 2-bpp driver output shows as 2-px mono dither (uncertain: inferred, not seen in DOSBox) |
| 3 | `hgc`  | GMHGC.BIN  (3723 b) | @0x5B2: `3BF←1`, CRTC 3B4 regs 0–11 ← `35 2D 2E 07 5B 02 57 57 02 03 00 00`, `3B8←2Ah`, clears B000 32 KB | B000, 90 b/row, 1 bpp, 4 banks of 0x2000 |
| 4 | `mcga` | GMMCGA.BIN (3273 b) | `AX=0013h` @0x5A6 — 320×200×256 | A000, 320 b/row, 8 bpp |
| 5 | `tga`  | GMTGA.BIN  (3704 b) | `AX=0009h` @0x5AC — Tandy 320×200×16 | B800, 160 b/row, 4 bpp, 4 banks of 0x2000 |

**No driver contains an INT 10h call or a vsync wait.** Mode set is in the
bootstrap, palette programming is in GAME.BIN (below), and only GMEGA touches
I/O ports (VGA/EGA sequencer + graphics controller). `mov ax,2 / int 10h` at
ZELIARD.EXE 0x2DA restores text mode at exit.

## 1. The vector table

Every driver starts with the same **35-entry near-pointer table at 0x2000–0x2044**;
the first word (0x2046) is both slot 0's target and the end of the table (code
starts at 0x2046 in all five). Callers use `call [cs:0x20xx]` (kernel, GAME.BIN,
engine and shop overlays) — the gd*/gf*/gt* renderers never call the driver
except `jmp [cs:0x2022]` in gdega/gdmcga. All 35 slots have at least one caller.

### 1.1 Argument conventions (identical in all five drivers)

Coordinates are **resolution independent**: `y` is always the 0..199 game row;
`x4` ("column") is x in 1/80ths of the screen width (4 px on 320-wide modes,
8 px on EGA/HGC 640-wide); `x8` ("cell column") is 1/40th (8 px / 16 px).
Widths use the same units. Tandy doubles BH internally (`add bh,bh` @2047), HGC
adds a 5-byte (40 px) left margin and remaps rows (§2.4).

Colours are PC-88 colour numbers 0..7 (`0 blk 1 wht 2 red 3 grn 4 cyn 5 blu 6 yel 7 mag`);
each driver has an 8-entry table turning them into its native pixel value
(§2). Strings for `[0x202A]` end with `0xFF`, `0x0D` = newline (+8 rows),
bytes ≥ 0x80 = set colour `b & 7`. Positioned labels for `[0x200E]/[0x2010]`
are `{u8 x4, u8 y, u8 xoff_px, u8 len, char[len]}` (SI is left after the record,
so calls chain). `DL:AX` numbers are 24-bit.

Player record fields read **directly** by the driver (BASE:0000 = STDPLY.BIN =
"standard player" defaults; CS-relative because everything shares BASE):
`0x85..0x87` GOLD (byte 0x85 = bits 16–23, word 0x86 = bits 0–15), `0x8B` ALMAS
(u16), `0x90` LIFE current (u16), `0x92` equipped sword picture #, `0x93` selected
magic (0 = none), `0x93` shield, `0x94`/`0x96` shield HP/max, `0x9D` selected magic, `0xAB..0xB1`/`0xB4..0xBA` magic charges/maxima, `0x9D` selected item
slot (1-based), `0xAB+i` item counts (bytes), `0xB2` LIFE max (u16). Which of
`0x90`/`0xB2` is "max" is inferred from call frequency (`[0x2008]` has 12 callers,
`[0x2006]` 3) — uncertain.

Global flags: `[FF77]` = intro/demo palette mode (GAME.BIN @A080 sets it to 0xFF
before running opdemo.bin when no command-line argument was given; omoypro.bin
also sets it). MCGA then draws text with VGA index `c*16|c` and frames with 0xFF
(the 256-entry gdmcga palette), EGA uses colour 7 for strings and all four planes
for frames. `[FF2C]` = data arena segment (itemp.grp pointers at `arena:E200..E20C`).
`[F500]/[F502]/[F504]` = the three sections of font.grp (GAME.BIN @A00A relocates
them): 8×8 text glyphs from char 0x20, 6×7 digit glyphs (8 bytes each, bits 5..0
of rows 0..6), 4-px-wide "narrow" HUD-label glyphs (top 4 bits of each of 8 rows).
On EGA the container's mode-0 stream is loaded, whose text/digit glyphs are 16
bytes each (16 px wide) — the narrow font stays 8 bytes.

Staging memory: `(CS+0x3000):0000` = the last 64 KB of the 256 KB block
(`BASE+0x3000`), used by `[0x2044]` as a copy of the input bank and by
`[0x2026]/[0x2028]` as the save buffer (`DI` = offset inside it).

### 1.2 Slot table

Names are new (this issue). "Args" are inputs; nothing returns a value, all
routines clobber freely (AX BX CX DX SI DI BP ES; DS preserved where noted).

| Slot | Name | Args | Purpose | MCGA | CGA | EGA | HGC | TGA | Caller example |
|---|---|---|---|---|---|---|---|---|---|
| 2000 | `vid_window` | AL style, BH x4, BL y, CH w4, CL h | AL=0: clear rect to 0. AL≠0: 2-px rounded window frame (colour 1 white / `[FF77]`→0xFF), interior cleared | 2046 | 2046 | 2046 | 2046 | 2046 | STICK 0x074A: `bx=0x201E, cx=0x1010, al=0xFF` → frame at (128,30) 64×16 (disk prompt) |
| 2002 | `vid_clear_playfield` | – | clears the 28×18-cell playfield: rows 14..157, x 48..271 (MCGA/CGA/TGA) / 96..543 (EGA/HGC) | 2106 | 20F0 | 211E | 2113 | 20F0 | fight 0x728C (`[FF75]=0x0B` then call) |
| 2004 | `vid_gauge_bar` | AL style, BH x, BL y, CH w px | draws CH one-px columns at (48+BH, 158+BL): style 0 = blue trough (row0 black, rows1-8 colour blk+blu, row 9 blu); 0x80 = erase 10 rows; other = grey line. Column before x is cleared first | 2195 | 2187 | 21C8 | 21DE | 21B7 | fight 0x6C55: `bx=0x0210, al=0, ch=0x21` (trough at 50,174 w33); 0x6C61: `al=0x80, bx=0x2310, ch=0x67` erase |
| 2006 | `vid_life_bar_max` | – (reads `[cs:B2]`) | red 6-row bar at (84,163), `value/8` px (cap 100), written with `and 2D / or 12` so it composes with the white bar → overlap = green | 2227 | 225D | 22DF | 22D6 | 22A7 | fight 0x618F (HUD redraw sequence 0x6144–0x619D) |
| 2008 | `vid_life_bar_cur` | – (reads `[cs:90]`) | white 5-row bar at (84,163) `value/8` px, remaining of 100 px cleared (`and 12`) | 2256 | 2297 | 231C | 2310 | 22E1 | fight 0x6194 |
| 200A | `vid_enemy_bar_max` | BX value | as 2006 at (84,175) — the "ENEMY" line (fight label @0x6C93) | 2231 | 2267 | 22E9 | 22E0 | 22B1 | fight 0x616C: `bx=[[A002]+3]` (boss AI header word) |
| 200C | `vid_enemy_bar_cur` | BX value | as 2008 at (84,175) | 2260 | 22A1 | 2326 | 231A | 22EB | fight 0x6172 |
| 200E | `vid_label_hud` | DS:SI record | positioned narrow-font label, colours (green, red shadow) — "GOLD"/"PLACE"/"ENEMY" | 22BF | 232E | 23A5 | 23AD | 2378 | fight 0x6C33: `si=0x6C44` → `0D BB 01 04 "GOLD"` |
| 2010 | `vid_label_text` | DS:SI record | same, colours (white, blue shadow) — place names, map `[C00E]` init text | 22CD | 233C | 23B3 | 23BB | 2386 | fight 0x6185: `si=[0xC00E]` → `16 AF 00 11 "Cavern of Malicia"` |
| 2012 | `vid_enemy_gauge_trough` | – | = `vid_gauge_bar(0, 2, 0x10, 0x88)`: blue trough (50,174) 136 px for the ENEMY bar | 2385 | 243A | 24BB | 24A9 | 24DC | fight 0x6179 |
| 2014 | `vid_num_almas` | – (`[cs:8B]`) | 5 digits (blank-led) at (152,187), white on dark-blue 6×7 boxes | 238F | 2444 | 24C5 | 24B3 | 24E6 | fight 0x6199 |
| 2016 | `vid_num_gold` | – (`[cs:85..87]`) | 6 digits at (76,187) | 23AC | 2461 | 24E2 | 24D0 | 2503 | fight 0x618A |
| 2018 | `vid_num_item_count` | – (`[cs:AB+[cs:9D]-1]`) | 3 digits at (220,187) | 23CC | 2481 | 2502 | 24F0 | 2523 | fight 0x881F (after `dec byte [bx+0xAB]`) |
| 201A | `vid_num_magic` | – (`[cs:94]` if `[cs:93]`) | 3 digits at (248,187); no-op when no magic selected | 23F5 | 24AA | 252B | 2519 | 254C | fight 0x75B4 |
| 201C | `vid_icon_sword` | AL 1-based #, BH x8, BL y | 40×18 PC-88-px picture from itemp section 0 (`arena:[E200]`, 270 b/record) | 254C | 262C | 26BE | 2698 | 26B0 | GAME 0xA19C: `al=[0x92], bx=0x18AB` → (192,171) |
| 201E | `vid_icon_item` | AL 1-based, BH x4, BL y | 32×16 picture, itemp section 3 (`[E206]`, 192 b/record), drawn at x4·4+2 | 25E2 | 26DB | 2745 | 275A | 2771 | GAME 0xA1C0: `al=[0x9D], bx=0x37A4` → (222,164) |
| 2020 | `vid_icon_magic` | AL 1-based, BH x4, BL y | 32×16, section 1 (`[E202]`) | 25FC | 26F5 | 275F | 2774 | 278B | GAME 0xA1AE: `al=[0x93], bx=0x3EA4` → (250,164) |
| 2022 | `vid_putchar` | AL char, AH colour, BX x px, CL y | one 8×8 glyph (font `[F500]`), set bits only (transparent) | 27E9 | 294B | 2958 | 29D4 | 29D9 | STICK 0x07EF: `al=digit+'0', ah=1, bx=0xCC, cl=0x5A` |
| 2024 | `vid_scroll_up_1` | BH x8, BL y, CH w8, CL rows | moves rect up one row (row+1 → row) | 2857 | 29F5 | 29E9 | 2AA0 | 2A68 | town 0x650E (`bx=[0x7C4E]+4, cx=([0x7C5A]>>1)-8`) |
| 2026 | `vid_save_rect` | AH x8, AL y (BH=0), CH w8, CL rows, DI dst | copy screen rect → `(CS+3000):DI` | 289A | 2A56 | 2A33 | 2AF9 | 2ABB | STICK 0x073D: `ax=0x101E, cx=0x0810, di=0x3C80` (16 cells, 30, 8×16) |
| 2028 | `vid_restore_rect` | same, DI src | inverse of 2026 | 28D9 | 2A9C | 2A8B | 2B32 | 2B0F | STICK 0x07AB (`jmp`) same regs |
| 202A | `vid_puts` | DS:SI string, BX x px, CL y | string via 2022, initial colour 1 (7 if `[FF77]`), advance 8 px | 291A | 2AE4 | 2AE5 | 2B7B | 2B65 | STICK 0x06CA: `si=0x070A, bx=0x74, cl=0x52` ("insert disk" prompt) |
| 202C | `vid_copy_rect` | DH sx8, DL sy, BH dx8, BL dy, CH w8, CL rows | screen→screen copy (top-down, forward) | 296F | 2B2F | 2B3A | 2BC6 | 2BAE | fight 0x6C76: `bx=0x0AA9, dx=0x0AB5, cx=0x0E03` |
| 202E | `vid_cursor_frame` | AL colour, BH x4, BL y | hollow 20×20 box, 2 px thick (menu cursor) | 29C3 | 2B97 | 2B8E | 2C18 | 2BFC | select 0xA18B (`jmp`), `bh=[0xADFB]` |
| 2030 | `vid_draw_digits` | DS:DI digit bytes (0xFF = blank), AH x4, AL y, CL count, CH&1 = +2 px, BH bg flag, BL colour | 6×7 digit glyphs (`[F502]`), 6-px pitch | 24A3 | 2558 | 25D9 | 25C7 | 25FA | select 0xA9D0 (`jmp`), `di=0xAE16+…` |
| 2032 | `vid_to_decimal` | DL:AX value, CS:DI out | 7 decimal digits (millions…units) to `[cs:di..di+6]` | 243A | 24EF | 2570 | 255E | 2591 | select 0xA9BB: `dl=0, di=0xAE16` |
| 2034 | `vid_icon_sec6` | AL 1-based (0 = built-in blank), BH x4, BL y | 32×16 from itemp section 6 (`[E20C]`) | 2616 | 270F | 2779 | 278E | 27A5 | select 0xA6E8 |
| 2036 | `vid_icon_sec5` | AL 1-based (0 = blank), BH, BL | section 5 (`[E20A]`) | 2637 | 2730 | 279A | 27AF | 27C6 | select 0xA680 |
| 2038 | `vid_label_asciiz` | DS:SI ASCIIZ, BH x4, BL y, CL x offset px | narrow-font label (white, no shadow) | 22DB | 234A | 23C1 | 23C9 | 2394 | town 0x752D: `bx=([FF54]+…)`, `cl=0` |
| 203A | `vid_icon_sec4` | AL **0-based**, BH, BL | section 4 (`[E208]`) | 2718 | 2811 | 287B | 2890 | 28A7 | select 0xA7CC: `bx=0x2E75, al=0` |
| 203C | `vid_icon_sec2` | AL 0-based, BH, BL | section 2 (`[E204]`) | 2730 | 2829 | 2893 | 28A8 | 28BF | select 0xA835 |
| 203E | `vid_tear_icon` | AL 0/1, BH x4, BL y | one of two built-in 16×13 icons (0 = blue/pink orb, 1 = green/red face); 0x80 = transparent | 2A1C | 2C19 | 2BF8 | 2CAF | 2C5B | GAME 0xA3C8: `bx=table[A3D3][i]` (x4 15,61,21,55,27,49,33,43), `al=0` → orbs along y=0 (the collected Tears row on the top border — uncertain) |
| 2040 | `vid_dissolve_playfield` | – | 8-step dissolve of the playfield to black (masks 01,03,…,FF rotating per row, 0x1F40-iteration delay per step) | 2130 | 2124 | 214E | 2143 | 2124 | fight 0x99A8 (after `int 60h ax=1`) |
| 2042 | `vid_clear_screen` | – | whole screen to 0 | 2C01 | 2D66 | 2E63 | 2E02 | 2DC3 | town 0x7617 (after loading overlay to A000) |
| 2044 | `vid_convert_cells` | DS:SI bank, CX count | copy CX×48-byte PC-88 cells to staging, rewrite bank in native format (§2) | 2C2A | 2D99 | **2E92 = `ret`** | 2E37 | 2DF6 | fight 0x6205: `ds=[FF2C], si=0x8030, cx=0x66` (DCHR bank) |

(Overlay listing addresses are given as real addresses: listing + 0x6000 for
fight/town, + 0xA000 for select. STICK/GAME listings already have the right origin.)

Slot-usage census (all callers): 2000 ×87, 2022 ×20, 2002/2040/2044 ×16,
2016 ×13, 2004/2008/2010/2028 ×12, 2012/2026 ×11, 2018 ×9, 200E ×7, 202E ×6,
2014/201A/201C/2038/2042 ×5, 201E ×4, 2006/2020/203E ×3, 2024/203A ×2,
200A/200C/202C/2030/2032/2034/2036/203C ×1. select.bin is the only user of
202E–203C (the status/inventory screen); STICK uses 2000/2022/2026/2028/202A
for the disk prompt; GAME.BIN uses 201C/201E/2020/203E for the initial HUD.

## 2. Per-driver notes

### 2.1 MCGA — GMMCGA.BIN (`src/video_mcga.c` is the cleaned C)

* Framebuffer A000, linear 320×200, one byte per pixel. Every pixel value is an
  index into the 64-entry blend DAC built by GAME.BIN @A41B (`DAC[l*8+r] =
  BASE[l]+BASE[r]`), so a plain PC-88 colour `c` is index `c*9`
  (table @24EA: `00 09 12 1B 24 2D 36 3F`). Hard-coded colours used by the HUD:
  0x05 (blk+blu, digit boxes / trough), 0x09 white, 0x12 red, 0x1B green,
  0x2D blue; the life/enemy bars rely on `red|white = 0x1B = green`.
* `[0x2044]` @2C2A (`vid_convert_cells`): copies the cells to `(CS+3000):0`,
  then rewrites each in place, 8 rows × 6 bytes: each pair of PC-88 pixels
  `(l,r)` → 6-bit `l<<3|r`. Bit stream per 8 source px is
  `p0 p1 p2 p3 p4 p5 p6 p7` (6 bits each, MSB first) but the first 16 bits are
  stored with `stosw` (little-endian), so the bytes on disk are
  `b0 = bits 8..15, b1 = bits 0..7, b2 = bits 16..23` — exactly what gfmcga @412F
  unpacks as `[b1>>2, (b1&3)<<4|b0>>4, (b0&15)<<2|b2>>6, b2&63]`. A converted
  cell is still 48 bytes (8×8 px × 6 bits) so the bank stride stays 0x30.
* Item pictures: `[E200]` records are 15 bytes/row × 18 rows (40 px: two BE
  A-words at +0/+2, one A-byte at +4; B words **little-endian** at +8/+6 and byte
  +5; C BE at +A/+C/+E) → 20 MCGA px; 32×16 records (`[E202..E20C]`) are 12
  bytes/row: A BE +0/+2, B LE +6/+4, C BE +8/+A → 16 MCGA px. The B-plane byte
  order quirk is real (no `xchg`); the CGA/HGC/TGA drivers read the same layout.
* Text: `[F500]` glyphs 1 px per bit, colour `c*9`; digits `[F502]` 6×7 on an
  optional 6×7 box of 0x05; narrow labels `[F504]` write **two** pixels per set
  bit (`[di]=fg, [di+1]=bg`) with a 5-px pitch — that is the drop-shadow look
  of "GOLD"/"PLACE" (fg green 0x1B, shadow red 0x12) and place names
  (0x09 / 0x2D).
* Built-in data: dissolve masks @218D, colour table @24EA, blank 32×16 icon
  @2658 (0xC0 bytes, used for empty item/magic slots), two 16×13 icons @2A61 /
  @2B31 (pointer table @2A5D), scratch: bar style @2226, 7 decimal digits @2433,
  fg/bg @2CBD/2CBE, string colour/x/y @2CBF/2CC0/2CC2, plane words @2CC3/5/7.
* No palette code, no ports, no vsync; all drawing is immediate.

### 2.2 CGA — GMCGA.BIN

* B800, 2 bpp, 80 bytes/row, rows interleaved: `addr = (y>>1)*0x50 + (y&1)*0x2000 + x/4`
  (helper inline everywhere; row step = `+0x2000`, wrap `-0x1FB0` past 0x3FFF).
  Hardware palette = whatever mode 5 gives (black/cyan/red/white on RGB);
  the driver never touches 3D9.
* `[0x2044]` @2D99: same staging copy; each row's three words are read
  **without byte swap** and the 6-bit pair `(l<<3|r)` is looked up in the 64-entry
  table @290B to a 2-bit CGA colour; 8 pairs → one 16-bit word per row, stored
  LE (which lands pixels 0-7 in byte 0 — the missing swap and the LE store cancel).
  **Output cells are 16 bytes (8×8×2 bpp) and written contiguously**, so after
  conversion cell N sits at `bank+N*16`, not `N*0x30`. Pair table (l down, r across,
  0=blk 1=wht 2=red 3=grn 4=cyn 5=blu 6=yel 7=mag; result 0 blk 1 cyn 2 red 3 wht):
  ```
  blk: 0 1 2 1 1 0 3 2   wht: 1 3 3 3 1 3 3 2   red: 2 3 2 1 1 2 2 2   grn: 1 3 1 3 1 1 2 2
  cyn: 1 1 1 1 1 1 3 2   blu: 0 3 2 1 1 1 3 2   yel: 3 3 2 2 3 3 3 2   mag: 1 2 2 2 2 2 2 2
  ```
* Colour → byte pattern table @259A `00 FF AA FF 55 00 FF AA` (blk, wht→white,
  red→red, grn→white, cyn→cyan, **blu→black**, yel→white, mag→red); the frame
  variant @2C11 maps blue to white. Text (0x294B): each 8-bit glyph row is
  doubled to 16 bits (@29E3: `bx = bits spread, dx = bx | bx>>1`) then ANDed
  with the colour pattern, so a glyph pixel = one 2-bpp pixel; the narrow font
  gets 4 bits → 8 bits the same way; label colours are patterns 0x55/0xAA
  (cyan text, red shadow) and 0xFF/0x00.
* Dissolve masks @217F `FE EE EA AA A8 88 80 00` are ANDed over the playfield.
  Icons @2C8E: two entries of `{AND-mask ptr, OR-data ptr}`, 4 bytes per row × 13.
  Save/restore buffer = `w8*2` bytes per row (2 words per cell column).
  No ports (`out dx,al` / `in ax,56h` seen by ndisasm at 2180/2D0F–2D13 are data).

### 2.3 EGA — GMEGA.BIN

* A000, 640×200 planar, 80 bytes/row. Register use: Sequencer 3C4 idx 2 (map
  mask: 0x0F all, 0x07 planes 0-2, 0x01/0x02/0x04/0x08 single), Graphics
  Controller 3CE idx 3 (function select: 0x00 replace, 0x08 AND, 0x10 OR),
  idx 4 (read map select 0-3, for `vid_save_rect`), idx 5 (mode: 0 / 1 =
  read-mode-1 copy for `vid_copy_rect`, `vid_scroll_up_1`, `vid_restore_rect` /
  2 = write-mode-2 for text and cursor), idx 8 (bit mask). Text and bars write
  the PC-88 colour number directly as the write-mode-2 colour, which works
  because GAME.BIN @A3FE programs the palette registers in PC-88 order via
  `INT 10h AX=1002h`, block @A409 = `00 3F 24 12 1B 09 36 2D | 38 07 04 02 03 01 06 05 | 00`
  (regs 0-7 = blk wht red grn cyn blu yel mag; 8-15 = a dim set, border 0).
* **`[0x2044]` @2E92 is `ret`**: cells stay as 8 rows × 3 BE words and the EGA
  renderers copy plane words straight into planes 0-2 (16-px cells, playfield
  448×144 at (96,14)). Item pictures (`[0x201C]` @26BE etc.) are likewise written
  plane by plane (`3C5←1,2,4`), A words as-is, B words reversed (`+5/+7/+9`
  byte-swapped), C as-is.
* Fonts: `[F500]`/`[F502]` glyphs are **16 bytes** (font.grp mode-0 stream);
  `[F504]` narrow glyphs 8 bytes, drawn 2 bytes wide via bit-mask register.
  String colour 1 (7 if `[FF77]`); label colours (3 green, 2 red) and (1 white, 5 blue).
* Frame (`vid_window`) uses AND/OR function select with map mask 0x01
  (colour 1) or 0x0F when `[FF77]`; dissolve @21C0 masks 8 bytes applied through
  the bit-mask register with map mask 0x0F. Save buffer = `w8*2*4` bytes per row
  (4 planes). Icons @2D27: pointer table, 13 rows of `{A,A2,B,B2,C,C2}` words
  combined as `mask = ~(A&B&C)` / `data = A|B|C` per plane. No vsync.

### 2.4 Hercules — GMHGC.BIN

* B000, 720×348 mono, 90 bytes/row, four 8 KB banks (`line % 4`). Row mapping
  helper @2E11: `g = (y+28)/3`, `b = (y+28)%3`, `addr = g*0x5A + b*0x2000 + x8*2 + 5`;
  when a routine steps to the next row and lands in bank 3 (`> 0x5FFF`) it writes
  the row **again** there and wraps to bank 0 of the next group (`-0x3FA6`), so
  3 game rows occupy 4 Hercules lines (200 → 267 lines, starting at line 37; the
  640-px-wide game area sits at x = 40..679). Only the bootstrap programs the card.
* `[0x2044]` @2E37: same 64-entry pair table as CGA (@2994, byte-identical), but
  here the 2-bit result is two adjacent mono pixels (00, 01, 10, 11 = 0/1/1/2 lit)
  — a dither. Cells stay 16 px wide: 16 bytes each, contiguous (stride 16).
* Colour tables @2600 `00 FF AA FF 55 00 FF AA` (blue = black, red/magenta =
  0xAA dither, cyan = 0x55, white/green/yellow solid) and frame @2CA7 (blue →
  FF). Text glyph rows are doubled 8→16 px (@2A8E), narrow labels 4→8. Label
  "colours" are dither patterns 0x55/0xAA vs 0xFF/0.
* Save buffer `w8*2` bytes per row; icons @2D2A `{mask, data}` × 2 like CGA.

### 2.5 Tandy — GMTGA.BIN

* B800, 320×200×16, 160 bytes/row (2 px per byte, high nibble left), four
  8 KB banks: helper @2E4E `addr = (y>>2)*0xA0 + (y&3)*0x2000 + xbytes`, entry
  points double BH first (`add bh,bh` @2047) so BH keeps its 4-px meaning. The
  default mode-9 palette is used unchanged (no port writes).
* `[0x2044]` @2DF6: pairs `(l<<3|r)` → 4-bit colour via the 64-entry table
  @2999; 4 pairs per word, two words per row (byte-swapped so pixel 0 is the
  high nibble of byte 0). Cells become **8×8×4 bpp = 32 bytes, contiguous**.
  Table (rows l = blk..mag, columns r = blk..mag, values = CGA/Tandy 16-colour index):
  ```
  blk: 0 7 4 2 3 1 8 5   wht: 7 F C E B 9 E D   red: 4 C C E 7 5 6 C   grn: 2 E E A A 3 A 7
  cyn: 3 B 7 A B 9 A 9   blu: 1 9 5 3 9 9 7 5   yel: 8 E 6 A A 7 E C   mag: 5 D C 7 9 5 C D
  ```
  (an approximation of the MCGA additive blend with the fixed 16 colours;
  note blk+blk = 0 but the plain "black" text colour below is 8).
* Colour → nibble-pair table @262E `88 FF CC AA BB 99 EE DD` (black → dark
  grey 8, white F, red C, green A, cyan B, blue 9, yellow E, magenta D — the
  bright variants). Text (@29D9): 2 glyph bits → two nibbles F/0 (@24C2) masked
  with the colour byte, 1 px per glyph bit; digit boxes are colour 1 (`0x11`);
  labels use patterns 0xAA/0x44 (green text, red shadow) and 0xFF/0x88.
* Life-bar composition uses `and 0x77 / or 0x11 / 0x99` style masks on nibbles.
  Save buffer `w8*4` bytes per row; icons @2CB7 `{16-bit masks, 8-byte rows}`.

## 3. Differences between the drivers

| Aspect | MCGA | CGA | EGA | HGC | Tandy |
|---|---|---|---|---|---|
| `[0x2044]` | pack to 8×8×6-bit, 48 B, stride 0x30 kept | 8×8×2 bpp, 16 B, **stride 16** | **no-op (`ret`)**, cells untouched (48 B) | 16×8×1 bpp dithered, 16 B, stride 16 | 8×8×4 bpp, 32 B, stride 32 |
| Cell width on screen | 8 px of 320 | 8 px of 320 | 16 px of 640 | 16 px of 640 (720 with margin) | 8 px of 320 |
| Palette programming | GAME.BIN `INT 10h 1010h` ×64 (@A41B) | none (hardware) | GAME.BIN `INT 10h 1002h` (@A3FE) | none | none |
| `[FF77]` honoured | yes (text `c*16\|c`, frame 0xFF) | no | yes (text 7, frame all planes) | no | no |
| Font stream | B (8 B/glyph) 1:1 | B, doubled to 2 bpp | **A (16 B/glyph)** | B, doubled 8→16 px | B, 1:1 nibbles |
| I/O ports | none | none | 3C4/3C5, 3CE/3CF | none | none |
| Vertical mapping | 1:1 | 1:1 interlaced banks | 1:1 | 3 rows → 4 lines, +37 lines | 1:1, 4 banks |
| Save-rect bytes/row | `w8*8` | `w8*2` | `w8*2*4` | `w8*2` | `w8*4` |

Every other slot is implemented in all five drivers with the same arguments and
the same visible result (same positions, same HUD geometry); no other slot is a
no-op anywhere. The only routines that differ in *behaviour* rather than pixel
format are the colour mappings above (blue → black on CGA/HGC, black → grey 8
on Tandy) and the bar-composition masks.

## 4. Verification notes

* Table extent and all 35×5 targets come from the raw files (first word 0x2046
  = end of table in every driver). Every slot has a caller (§1.2 census).
* MCGA semantics were read from the full ndisasm listing; the other four from
  `tools/ghidra.sh … table:0x2000:35` output plus the listings for the address
  calculators and pair tables (Ghidra mangles those).
* HUD geometry (LIFE bar green, PLACE at y=175, GOLD at x=76 and ALMAS at
  x=152 on y=187, item/magic boxes at 222/250) matches the DOSBox capture
  `tools/run_dosbox.sh` at 40 s (town screen).
* Marked uncertain: the cga2/mode-6 appearance, which of `[0x90]/[0xB2]` is
  max, the `[0x94]` meaning, the purpose of the `[0x203E]` icons, and the exact
  `[FF77]` semantics beyond "set for the opening demo".
