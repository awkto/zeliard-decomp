# Town — town.bin, the town maps and the shop overlays (Sprint 6)

Companion to `src/town.c` (town.bin, ZELRES1[6] @6000) and `src/shops.c` (kingpro..kenjpro,
ZELRES2[10..17] @A000).  Everything below was read from the ndisasm listings; addresses are BASE
offsets (`town.bin` 6000.., shop overlays A000.., gtmcga renderer 3000.., map C000..).  *uncertain*
marks inferences from a single use.

## 1. Overview

```
GAME.BIN boot ──► town_entry_boot 601E ─┐        fight.bin 72D9 (menu_result 8 / death)
restore_game 7592 (F7) ─► GAME.BIN AX=FFFF ┘        └─► kernel mode-0 swap ─► town_entry_from_fight 6026
                                          │
              ┌───────────────────────────┴───────────────────────────────────────┐
              │ main_loop 61FC: frame 68AC (NPC update, hero, GT_FLUSH, hotkeys)    │
              │   Enter  -> select.bin status screen (swapped in from arena:C000)   │
              │   Space  -> talk to the NPC 1..3 columns ahead (dialogue_run 63C5)  │
              │   Up     -> enter_door 6E29: shop overlay / cavern / "past" door     │
              │   Left/Right -> walk_left 6781 / walk_right 67F4 (scroll, parallax)  │
              │   walking off an edge -> check_edge_exit 6CB5: other town / cavern  │
              └────────────────────────────────────────────────────────────────────┘
```

Memory while in town (all BASE-relative):

| Where | Contents |
|---|---|
| `3000` | gtmcga.bin (ZELRES1[11]) town renderer, 20 vectors `3000..3026` (§5) |
| `6000` | town.bin, 15 vectors `6000..601C` (§2) |
| `A000` | the current shop overlay (raw, `[A000]` entry, `[A002]` hook); otherwise the 28×0xC0 **backdrop strip** captured by `GT_CAPTURE_BACKDROP` |
| `C000` | the town map (§3), loaded raw by kernel mode 1 with AH = `0x80|n` |
| `D000` | per-cell sky masks (8 bytes/cell) built by `GT_BUILD_TILEBANK` |
| `E000` | screen copy: 28 columns × 8 rows, **column-major**, stride 8 (0xE0 bytes); 0xFE force redraw, 0xFF protected (hero/NPC/dialogue box), 0xFD NPC |
| `arena:3000` | music score (started with `int 60h AX=0`) |
| `arena:4000` | NPC sprite set mman/cman.grp (§4.2), converted in place; masks at `(BASE+2000):7000` |
| `arena:6000` | hero cells tman.grp (46 cells), masks at `(BASE+2000):8000` |
| `arena:8000` | town **tile bank** cpat/mpat/dpat.grp (§4.1); shops overwrite it with their 256-cell portrait |
| `(BASE+2000):3300` | ympd.bin / ckpd.bin (ZELRES2[8]/[9]): far-called backdrop painter (mountains / underground), **not decoded** |
| `SP = 2000` | town.bin's private stack (6047) |

## 2. town.bin vectors

| Vector | Routine | Name (town.c) | Used by |
|---|---|---|---|
| `6000` | 6026 | `town_entry_from_fight` (boot_entry = 0) | fight.bin via mode-0 swap |
| `6002` | 601E | `town_entry_boot` (boot_entry = 0xFF) | GAME.BIN A219, restore, Dorado warp |
| `6004` | 706C | `shop_print_text` — SI=[FF4C]; returns AL=0 on `00`, AL=nn on `FF nn` | every shop (81 call sites) |
| `6006` | 72C7 | `format_number` DL:AX → ES:DI ASCII, no leading zeros, 0xFF end | shops (prices) |
| `6008` | 74D3 | `yes_no_prompt` CF=0 Yes / CF=1 No or Alt | shops, dialogue opcode 0x81 |
| `600A` | 7570 | `gold_can_pay` DL:AX amount → DL:AX = gold−amount, CF=1 if short (no store) | armr, drug, inn |
| `600C` | 7589 | `gold_add` DL:AX | bank, drug (sell), armr |
| `600E` | 751A | `menu_draw_items` CX ASCIIZ strings at DS:SI, rows [FF54]+1+10i | shops |
| `6010` | 7344 | `menu_select` BL cursor → BL, CF=1 cancelled; uses FF52/53/54/56/58/6A | shops |
| `6012` | 7539 | `menu_draw_icons` AL first item, AH first row, CX rows (GT_MENU_LINE/BLIT) | shops (price lists) |
| `6014` | 7469 | `cursor_draw` BL | shops, name entry |
| `6016` | 7042 | `idle_poll` hotkeys (exit/pause/F7) + `[A002]` while a shop is active | all waits |
| `6018` / `601A` | 747B / 74A7 | `cursor_up_anim` / `cursor_down_anim` (10 × 1 px, 4 ticks each) | menus |
| `601C` | 7592 | `restore_game` (F7 confirmed by kernel `[11E]`) | frame, idle_poll |

Entry sequence (601E/6026 → 602C): convert the NPC sprite set, `SP=2000`, load tman.grp (6DEF),
clear playfield, read the level record (`{music_flags, gfx, FF, town_flags, tileset}` after
`[C000]`), set `walk_in` (underground town entered from a cavern), load the backdrop painter
(YMPD for surface towns, CKPD when `town_flags&1`), load the tile bank + paint + capture the
backdrop, start the music at arena:3000 (not when `[49]` = game won), apply the map patches
(6AED), zero `[E4]`/`[9F]`, draw the HUD (gauges, LIFE/ALMAS/GOLD/PLACE labels 6C93..6CB4,
place name from `[C004]`), `FF2A = C017 + scroll_col*8`, place the NPC markers.  If `[E8]`
(died in the caverns): load KENJPRO to A000 and jump to its death entry `[A004]` with the return
chain 6EAF → 61FC.  Otherwise full redraw, optional 5-step walk-in, main loop.

Frame pacing (68AC): NPC update → hero/NPC sprites → `GT_FLUSH` → the five kernel hotkey services
→ wait until `FF1A >= 4*speed` (callers preset `FF1A = 0x28` for an immediate frame).

## 3. Town map format (`cmap/mrmp/stmp/bsmp/hlmp/tmmp/drmp/llmp/prmp/esmp.mdt`, ZELRES2[36..45])

Loaded **raw** to `BASE:C000` by kernel mode 1 with `AH = 0x80|n` (records 32..41 of the
`cs:0F68` table); every pointer is an absolute BASE offset.  Unlike the cavern maps the grid is
**not RLE**: `width` columns × **8 rows**, column-major, `width*8` bytes from `C017`; `[C011]`
== `C017 + width*8` for all ten maps.  Screen column c shows map column `scroll_col+4+c`
(gtmcga 3074 skips 4 columns), so `scroll_col` ranges 0..`width-0x24`.

```
C000 u16 ->level      {u8 music_flags, u8 gfx, 0xFF, u8 town_flags, u8 tileset}
                       music = (byte0>>1)&0x1F -> GAME.BIN table A363 (mgt1 ugm1 mgt2 ugm2), only used at boot
                       gfx: 0 MMAN.GRP / 1 CMAN.GRP (NPC sprites, GAME.BIN A38F / town 6D88)
                       town_flags bit0 = underground town (CKPD backdrop, far parallax strip, walk-in)
                       tileset: 0 CPAT 1 MPAT 2 DPAT (town 6DCE)
C002 u16 width        114 (cmap) .. 320 (prmp)
C004 u16 ->label      {x4, y, xoff, len, text} for VID_LABEL_TEXT ("Muralla Town")
C006 u8  town id      1..9 (castle = Muralla = 1): index of the per-town price/rate tables in the shops
C007 u16 ->exits      4-byte records {flags, dest, gfx, tileset}; NO terminator: the left-edge exit is
                       the first record with flags bit0 set, the right-edge exit the first with bit0 clear.
                       flags & 0xFE != 0 (0x80) -> cavern, dest = index into the C00B table
C009 u16 ->doors      {u16 col, u8 dest} .. FFFF; the hero (2 columns) must stand at col-1..col+1 (6E46)
                       dest 0..7 shop overlay (0 king 1 omoya 2 sage 3 armour 4 drug 5 church 6 bank 7 inn)
                       dest 8.. cavern entry (dest-8);  dest FF = "doorway to the past" (prmp only)
C00B u16 ->caves      5-byte records {u16 col, u8 row, u8 side, u8 map}, indexed (no terminator;
                      size it from the largest cave index used by the door and edge-exit records)
C00D u16 ->dialogue   u16 ptr[] to 0xFF-terminated scripts (§6)
C00F u16 ->npcs       8-byte records .. FFFF (below)
C011 u16 ->range      {u16 min_col, u16 max_col}: walkers turn around here (== end of the grid)
C013 u16              not read by town.bin (0x22, 0xAC, 0x5C, ...: looks like a start column)
C015 u16 ->patches    {u16 flag_ptr, u8 mask, {u16 ptr, u8 val}* FFFF}* FFFF — applied on entry,
                       after every shop and after dialogue opcodes 0x83/0x89/0x8B (6AED)
C017     grid[width][8]  cell values = index into the tile bank (0 = sky)
```

NPC record (`struct npc`, C00F): `+0 u16 col` (live), `+2 sprite` (bits 0-6 sprite 0..4, bit7 =
facing LEFT), `+3 saved` (map cell under the marker), `+4 anim` (bit0/low nibble frame, bits 4-5
step timer), `+5 type` (behaviour), `+6 flags` (0x40 solid — blocks walking; 0x80 approaches and
talks once when the hero faces it two columns away), `+7 script`.  Every frame the NPC row (5)
of the map gets `0xFD` at each NPC's column (6C2B) and the previous tile back (6C4E).

| type | 6B41 table | behaviour |
|---|---|---|
| 0 | 6B51 | face the hero + 2-frame idle animation (every 4 frames) |
| 1 / 2 | 6B6C / 6BA6 | walk between `C011` min/max, one column every 2 / 4 frames, turning at the limits |
| 3 | 6BB7 | face the hero, no animation |
| 4 | 6BD2 | idle animation only |
| 5 / 6 | 6BEC / 6C19 | wander: 7 steps then turn, every 2 / 4 frames |
| 7 | 6C2A | static (also forced while talking) |

Patches are how the story changes the town: e.g. bsmp `if [0012]&08: [CCFA]=80 [CCFB]=0E` makes
the sentry non-solid with script 14 ("You have the Hero's Crest, I see") once the crest flag is
set; cmap `if [0049]&FF` moves the NPC table pointer one record back (adds the awakened advisor),
switches the four courtiers to the ending scripts 6-9 and blocks the right exit with cell 0x3D;
llmp `if [0030]&FF` (hut creature killed) rewrites door/tiles and every NPC's script.

Per-map summary (`tools/mdt2png.py --town OUT` prints the full tables):

| map | AH | name | width | tileset / sprites / music | doors (col:dest) | cavern entries | NPCs | flags |
|---|---|---|---|---|---|---|---|---|
| cmap | 80 | Felishika's Castle | 114 | cpat / mman / mgt1 | 52 king, 95 omoya | — (right exit → mrmp) | 4 | patches on [49], [04] |
| mrmp | 81 | Muralla Town | 215 | mpat / mman / mgt1 | 39 armour 59 church 111 drug 138 bank 172 sage 205 cave0 | (61,7,0,MP10) | 9 | left exit → cmap |
| stmp | 82 | Satono Town | 215 | dpat / cman / ugm1 | 44 drug 92 sage 128 inn 148 bank 185 armour | (128,33,1,MP10) (6,62,0,MP20) | 7 | underground; edges → caves 0/1 |
| bsmp | 83 | Bosque village | 152 | mpat / mman / mgt2 | 7 cave1 36 bank 61 sage 81 drug 96 armour 114 inn 142 cave0 | (185,19,0,MP31) (149,14,1,MP3D) | 12 | no edge exits; [12]&8 sentry |
| hlmp | 84 | Helada Town | 227 | dpat / cman / ugm1 | 44 sage 92 inn 111 armour 128 drug 148 bank | (86,22,1,MP40) (16,22,0,MP41) | 8 | underground; [1A]&10 |
| tmmp | 85 | Tumba Town | 270 | dpat / cman / ugm2 | 44 armour 93 inn 128 sage 181 bank 231 drug | (94,11,1,MP50) (131,10,0,MP50) | 8 | underground; [22]&2, [24]&80/2 |
| drmp | 86 | Dorado Town | 215 | dpat / mman / ugm2 | 47 armour 70 inn 92 sage 128 bank 184 drug | (31,6,1,MP62) (315,49,0,MP61) | 12 | underground; [2A]&4 |
| llmp | 87 | Llama Town | 280 | mpat / cman / mgt2 | 8 cave1 39 armour 71 sage 104 drug 142 bank 176 inn 222 cave2 269 cave0 | (1,22,0,MP71) (152,7,1,MP71) (27,13,0,MP73) | 9 | no edge exits; [30], [34]&80/40 |
| prmp | 88 | Pureza Town | 320 | dpat / cman / ugm1 | 49 drug 93 inn 128 sage 181 bank 231 armour **294 past** | (111,21,1,MP82) | 10 | underground; [42]&8, [2B]&10 |
| esmp | 89 | Esco village | 215 | mpat / mman / mgt2 | 57 armour 111 drug 138 bank 171 church 205 cave0 | (123,6,0,MP90) | 7 | — |

(cavern map = record `map` of the kernel table: 0 MP10, 2 MP20, 5 MP31, 6 MP3D, 8 MP40, 9 MP41,
0xB MP50, 0xE MP61, 0xF MP62, 0x12 MP71, 0x15 MP73, 0x17 MP82, 0x18 MP90 — docs/RESOURCES.md.)

## 4. Graphics

### 4.1 Tile banks cpat / mpat / dpat.grp (ZELRES2[33..35]) — DECODED

ARCHITECTURE.md lists these as "3-section shape containers"; they are the **town tile banks**:

```
+0   u16 6           +2 u16 off_block   +4 u16 off_anim
+6   u8 type[ncells] 0 opaque (A,B,C planes)      1 A,B + word 3 = sky mask (C=0)
                     2 A, word 2 = mask, C (B=0)   3 word 1 = mask, B, C (A=0)   4 opaque, whole cell sky
off_block  {u8 n, u8 cell[n]}   ground cells that BLOCK walking (cpat 3C 3D, mpat 96 97, dpat BF)
off_anim   {u8 n, {u8 from, u8 to}[n]}  cell cycling: after a row 0-2 cell is drawn the map byte is
                     replaced by `to` (gtmcga 32C5, only cells 1..0x18) — flames/water animate
+100 cells  48-byte PC-88 cells (cpat 157, mpat 242, dpat 191)
```
`load_tileset` (6D9E) decompresses the file to `arena:8000`, adds 0x8000 to the three header
words and calls `GT_BUILD_TILEBANK` (3AF9), which repacks the 250 cells in place into the MCGA
6-bit pair format (the same as `[0x2044]`) and writes one 8-byte mask per cell to `BASE:D000`
(bit per pixel pair, set when both mask bits are 1).  `GT_FLUSH` draws rows 0-2 with the
backdrop strip showing through the mask (3203); rows 3-7 are drawn plain (masked pairs = colour 0).

### 4.2 Sprites: mman / cman.grp (NPCs) and tman.grp (hero)

ARCHITECTURE.md's "tman starts with a pointer table" is wrong; the pointer-ish header belongs to
mman/cman: bytes `0..0xEF` = **5 sprites × 8 frames × 6 cell indices** (frames 0-3 facing left,
4-7 facing right; 6 cells = column 0 rows 0-2 then column 1 rows 0-2; indices 1-based), then 48-byte
cells from `0x100` (mman ~155 cells, cman 165).  `GT_CONVERT_SPRITES` (3A71) packs them in place
and derives the transparency mask from the *original* colour: colour 0 = transparent, colour 7
(white) is drawn as black (outline) — same trick as the fight sprites.  tman.grp is 46 plain cells;
the hero frame tables are in town.bin (6A3B left / 6A59 right, 0-based, 5 frames: 4 walk + back
view).  NPC frame = `arena:4000 + sprite*48 + ((anim&3) + 4*facing_right)*6` (34EC).

### 4.3 Screen layout and parallax

| y | content |
|---|---|
| 14..29 | far backdrop strip, scrolled 4 px per step — **underground (`ckpd`) only**.  Above ground `ympd` paints a single 224×88 panorama at (48,14), so these rows are part of it and never scroll |
| 30..77 | backdrop painted by ympd/ckpd (mountains / cave ceiling), static.  Both painters actually reach **y 101**, not 77 — rows 78..101 are the strip `GT_CAPTURE_BACKDROP` grabs |
| 78..141 | the 8 map rows (28 × 8 px cells at x = 48 + col*8); rows 0-2 show the strip captured from y 78..101 behind masked pixels |
| 142..149 / 150..157 | near ground strips scrolled 8 / 16 px per step (`GT_SCROLL_NEAR_*`).  Phase for all three strips = `scroll_col × {4, 8, 16}` modulo their own 112 px width (verified: `town.png` at `scroll_col` 179 is 88 px / 64 px along) |
| 158.. | HUD (LIFE bar, PLACE, GOLD `[85..87]`, ALMAS `[8B]`, sword/item/magic icons) |

Hero: screen column `hero_scr_col` (0..0x1B), rows 5-7; NPCs on row 5-7 too.  Dialogue boxes:
176 px wide, up to 8 lines of 10 px, at x8 7 or 11 (left/right of the hero), y from 24.

## 5. gtmcga.bin vectors (ZELRES1[11] @3000)

| Vec | Addr | Name | Args / effect |
|---|---|---|---|
| 3000 | 3A41 | `GT_INIT_PLAYFIELD` | XOR-fill the playfield with 0x3636 (GAME.BIN boot; kenjpro flashes it on level-up) |
| 3002 | 3028 | `GT_CAPTURE_BACKDROP` | copy 224×24 px at (48,78) → BASE:A000 as 28 columns × 0xC0 bytes |
| 3004 | 3051 | `GT_FLUSH` | for the 28 columns: hero column first (30D5), then every row whose E000 copy differs: rows 0-2 sky blend (31D8/3203), row 5 NPC (3350) when the map byte is 0xFD, else plain cell (3123); protected 0xFF cells are skipped once |
| 3006 / 300A | 3628 / 36A4 | `GT_SCROLL_NEAR_RIGHT/LEFT` | shift the y142 strip 8 px and the y150 strip 16 px |
| 3008 / 300C | 3677 / 36F1 | `GT_SCROLL_FAR_RIGHT/LEFT` | shift y14..29 by 4 px |
| 300E | 32FC | `GT_HERO_BACKDROP` | SI = 6 cells → offscreen A000:FA00 (2 cols × 3 rows) |
| 3010 | 3526 | `GT_NPC_COMPOSE` | BX 1..3 (NPC at hero col +1 / col / col-1), DI frame ptr: mask+OR the NPC over FA00/FAC0/FB80 |
| 3012 | 359A | `GT_HERO_DRAW` | SI = 6 tman indices: mask+OR over FA00, then blit to the screen |
| 3014 | 34EC | `GT_NPC_FRAME` | SI NPC record → DI frame pointer |
| 3016 | 371C | `GT_DRAW_CELL` | AL cell of arena:8000 (0-based), BH x4, BL y — shop portraits |
| 3018 | 3785 | `GT_CURSOR` | 9-row red arrow at BH x4, BL y |
| 301A | 3805 | `GT_MENU_LINE` | SI string-pointer table, AL index → 160×10 line buffer (narrow font); with `[FF57]` a 24-bit price from DI table (3 bytes/entry) at column `[FF68]` |
| 301C | 37CC | `GT_MENU_BLIT` | line buffer → BH x4, BL y, `[FF6A]*4` px wide, 9 rows |
| 301E / 3020 | 3999 / 39EF | `GT_MENU_SCROLL_UP/DOWN` | scroll a `CL`-row, `CH` x4-wide area by 1 px and reveal buffer row AL |
| 3022 | 388E | `GT_NUMBER_LINE` | DL:AX → 7 digits in the line buffer (`[FF6A]=0xB`) |
| 3024 | 3AF9 | `GT_BUILD_TILEBANK` | §4.1 |
| 3026 | 3A71 | `GT_CONVERT_SPRITES` | §4.2; DS:SI cells, CX count, ES:DI mask output |

## 6. Walking, collision, doors, NPC talk

* **Walk step** (6781/67F4): the ground cell (row 7) of the column just beyond the hero's 2
  columns must not be in the tile bank's block list (686E), and no NPC with flags 0x40 may stand on
  the target column (6890).  Then `hero_anim = (hero_anim+1)&3`, facing bit updated, and either the
  hero moves (columns 0x0B..0x10 are the free zone; `6781` tests `hero_scr_col >= 0x0B`
  *before* decrementing, so walking left settles the hero on **0x0A**) or the map scrolls (`scroll_col`, `FF2A ±8`,
  near/far parallax strips).  Scrolling stops at `scroll_col == 0` / `width-0x24`; beyond that the
  hero walks to the edge and `hero_scr_col` becomes 0xFF or 0x1C.
* **Edge exit** (6CB5): pick the exit record; town→town: `change_town_map` (6D30: `[C4] = 0x80|dest`,
  map raw to C000, NPC set → arena:4000, tile bank if it changed) then reappear at the far end
  (`hero_scr_col` 0x1A / 0, `scroll_col` width−0x24 / 0) via the 60B7 restart — the music is **not**
  reloaded, HUD/backdrop are.  Cavern: `goto_cavern`.
* **Up** in front of a door (6E29, hero column within col±1): `hero_anim = 4` (back view), one
  frame, then dest 0..7 → `run_shop` (6E7E: `music_fade = 4`, overlay raw → A000, dissolve, music
  stop, `shop_active = 0xFF`, `call [A000]`; on return 6EAF redraws the town, re-applies patches and
  restarts the music); dest ≥ 8 → `goto_cavern(dest-8)`; dest 0xFF → `doorway_to_the_past`.
* **Space** (623F): scans the NPC row 1..3 columns ahead in the facing direction for 0xFD; the
  NPC answers only if `flags & 0xC0 == 0`; it is frozen (type 7), turned toward the hero and
  `dialogue_run(script)` is executed with the box on the hero's far side.
* **Auto-talk** (62ED, only on a frame after walking): an NPC with flags 0x80 exactly two columns
  ahead and facing the hero starts its script uncancellably (`dlg_forced`) and clears bit 7.
* **Enter**: select.bin (status/inventory) is swapped in from arena:C000 (0x800 words, 6938).

### Dialogue script (town maps, interpreter 63C5)

Text bytes are drawn with the proportional font (`font_xoff` 7B82, `font_advance` 7BE2), word-wrapped
at 168 px; a box shows at most 8 lines, after 7 a red `|` waits for Space (`sfx 0x1D`); `&` is a
non-breaking space (it is just a glyph), `\` prints as an apostrophe.

| byte | meaning (address) |
|---|---|
| `2F` | newline (64E6); the 8th line scrolls the box up 10 px |
| `81` | yes/no prompt; continue with script **12** (Yes) or **13** (No) (6655) — bsmp sentry |
| `83` | `[34] |= 0x80`, `[9A] = 0xFF` (Elf Crest received), apply patches, end (6685) — llmp script 1 |
| `85` | restart with script **4**, uncancellable (6695) — llmp 3 |
| `87` | wait for a key, then script **5** (66A2) |
| `89` | "Take / No Take" menu: declined → script 6; `[8B] almas < 2500` → 7; else `almas -= 2500`, `[34] |= 0x40`, item **5** (Asbestos cape) appended to `[A1..]`, patches, script 8 (66AD) |
| `8B` | `[04] |= 0x80`, apply patches, end (664D) — no shipped text uses it |
| `FF` | end: wait for release, then Space/Alt (65A1) |

Shop text (`shop_print_text` 706C, used with `[FF4C]`): printable → 4-line box at (56,99), 10 px
rows, wrap at 208 px, `sfx 5` per glyph, 6 ticks per glyph; `2F`/`0D` newline (a full box shows the
`|` marker and waits); `0C` clear; `0F` wait + clear; `11` wait; `13`/`15` mute/unmute; `00` →
returns 0 (shop action 0, normally lip-sync); `FF nn` → returns nn (shop action); `FF FF` end.

## 7. The shops (src/shops.c)

Common prologue (A006..A04E in king/armr/bank/chur/drug/inna): portrait `.grp` → `arena:8000`,
`VID_CONVERT_CELLS` ×256, `[FF4E]=[FF4F]=0`, clear playfield + trough + place label, portrait via
`GT_DRAW_CELL` (12×8 cells at x4 7 or 0x0E, y 0x17), text box `VID_WINDOW(FF, 0D, 60, 36, 37)`,
then the `shop_print_text` loop with a per-shop action jump table; `FF FF` → `VID_DISSOLVE`,
`ret` to town.bin 6EAF.  `[A002]` = idle hook (blink / lip-sync; `[FF50]` is the timer).  Prices
and stock are indexed by the map's town id `[C006]` − 1.  Note `cmap` (the castle) and
`mrmp` (Muralla) **share id 1**, so the row labels in the tables below are one town out
from Muralla on: Muralla's Training sword is 400 and the last row is Esco.

### kingpro (King of Felishika, cmap col 52)
Script by flags: first visit (`[05]|[06] == 0`) → gift; `[06]==0` → "did you forget something";
`[49]==0` → "have you not yet vanquished"; else the ending text.  Action 1 (A09A): **+1000 gold**
in ten 100-gold steps, then `[05] = FF`.  Actions 0/2..5 are face animations (12-frame talk A0F8,
blink restarted when `KRN_RANDOM()==0`).

### omoypro (Princess Felicia's chamber, cmap col 95)
Picture only (16×17 cells).  `[49]==0` → wait for Space, return.  `[49]!=0` → pops the return
address, loads ENDDEMO.BIN (ZELRES2[50]) to 6000 and gd*.bin to 3000, waits 300 ticks, `[FF77]=FF`,
`jmp [6000]`: the ending.

### armrpro (Weapon and Armour Shop)
Menu: Go outside / Repair shield / Buy weapon / Buy shield / Explain goods.  Stock is a bitmask
per town in the **player record** (`[D2..DA]` swords, `[DB..E3]` shields, bit7 = first item);
buying clears the bit, trading in sets the old item's bit.  Buy: price from this town's table,
trade-in = half this town's price of the carried item; sword → `[92]`, kernel mode 4 installs the
sword block; shield → `[93]`, `[94] = [96] = durability` {Clay 30, Wise 80, Stone 180, Honor 300,
Light 300, Titanium 600}.  Repair = ⌈(`[96]`−`[94]`)/2⌉ gold.  Tumba: the Knight's sword is
withheld until `[24]&2`; carrying the Crest of Glory (`[9B]`) offers the swap `[92]=4, [9B]=0,
[24]|=2`.  Enchantment sword (id 6, price 4) is unique.

| town | Train | Wise | Spirit | Knight | Illum | Clay | Wise sh | Stone | Honor | Light | Titan |
|---|---|---|---|---|---|---|---|---|---|---|---|
| castle | 400 | 1500 | 6800 | 9800 | 90000 | 50 | 150 | 2980 | 9800 | 14800 | 39800 |
| Muralla | 800 | 1500 | 6800 | 9800 | 69800 | 50 | 150 | 2980 | 9800 | 14800 | 39800 |
| Satono | 800 | 1500 | 6800 | 9800 | 69800 | 5 | 150 | 2380 | 9800 | 14800 | 39800 |
| Bosque | 400 | 3000 | 5440 | 9800 | 69800 | 5 | 50 | 1780 | 9800 | 14800 | 39800 |
| Helada | 400 | 3000 | 4760 | 4900 | 69800 | 5 | 50 | 1780 | 7840 | 14800 | 39800 |
| Tumba | 200 | 1500 | 3400 | 7840 | 69800 | 5 | 20 | 890 | 5880 | 14800 | 39800 |
| Dorado | 200 | 1500 | 1360 | 5880 | 34800 | 5 | 20 | 890 | 5880 | 10360 | 39800 |
| Llama | 100 | 1000 | 1360 | 3920 | 32800 | 5 | 20 | 890 | 3920 | 7400 | 31800 |
| Pureza | 10 | 100 | 680 | 1960 | 29800 | 2 | 10 | 298 | 1960 | 5920 | 23800 |

(tables BAB9.., pointer table BAA7; initial stock STDPLY `D2..DA` = C0 C0 E0 E0 70 38 38 F8 F8,
`DB..E3` = C0 E0 E0 70 30 38 1C 1C FC.)

### drugpro (Witchcraft Implement shop)
Menu: Go outside / Buy / Sell / Description.  Stock bitmask per town `[C9..D1]`; purchases go into
the five **potion slots `[A6..AA]`** as id+1; selling pays half price and returns the item to the
town's stock.  No effect is applied in the shop (the item menu does that).

| id | item | told effect | castle | Mur | Sat | Bos | Hel | Tum | Dor | Lla | Pur |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | Ken'ko Potion | mild health tonic | 50 | 50 | 50 | 50 | 5 | 5 | 5 | 5 | 2 |
| 1 | Juu-en Fruit | strong healing | 240 | 240 | 240 | 300 | 600 | 600 | 900 | 900 | 200 |
| 2 | Elixir of Kashi | partial magic restore | 60 | 60 | 60 | 120 | 240 | 240 | 360 | 360 | 40 |
| 3 | Chikara Powder | full magic restore | 320 | 320 | 320 | 320 | 480 | 480 | 960 | 960 | 280 |
| 4 | Magia Stone | protects against attacks | 1000 | 1000 | 1500 | 1500 | 2000 | 2000 | 2500 | 2500 | 800 |
| 5 | Holy Water of Acero | shield to full strength | 100 | 100 | 100 | 100 | 200 | 200 | 400 | 400 | 80 |
| 6 | Sabre Oil | more sword power | 1200 | 1200 | 1200 | 1200 | 2000 | 2000 | 2400 | 2400 | 1000 |
| 7 | Kioku Feather | return to the last sage | 350 | 350 | 350 | 350 | 350 | 350 | 350 | 350 | 150 |

### bankpro (The Bank)
**Balance = `[88]` u8 hi + `[89]` u16 lo** (24-bit like gold).  Exchange converts *all* almas at
the town rate (remainder kept, no fee): castle/Muralla 1→6, Satono 1→6, Bosque 1→8, Helada 1→4,
Tumba 1→2, Dorado 1→4, Llama 4→2, Pureza 1→6, Esco 1→8 (A8FA).  Deposit/withdraw: 24-bit amount
entry (Up/Down ±1, Left/Right ±10, auto-repeat), Space confirms; deposit ≥ 1000 gold plays the
"delighted" text.  **No interest.**

### churpro (The Church) — free
`hp == max_hp` → restore magic (`[AB..B1] = [B4..BA]`); else heal `[90] += 8` every 20 ticks up
to `[B2]`.  Nothing else is touched (no gold, no status cure).

### innapro (The Inn)
Price by town (A2D1): Satono 30, Bosque 50, Helada 70, Tumba 100, Dorado 150, Llama 200, Pureza
400 gold.  Yes + `gold_can_pay` → gold −= price, going-to-bed animation, dissolve, `[90] = [B2]`,
`[AB..B1] = [B4..BA]`.  `KRN_RANDOM` only picks the innkeeper's blink frame.

### kenjpro (The Sage — Marid/Yasmin/Hajjar/Chiriga/Hisham/Maryam/Saied/Indihar by town)
Own prologue (A05A).  First visit to each sage (bit of `[E5]`, 0x80 Marid … 0x01 Indihar) plays the
intro and **teaches spell 1..7** (`[9D] = n`, `[BB+n-1] = FF`; Marid teaches nothing).  Menu: Go
outside / See Power / Listen Knowledge (per-town hint) / Record Experience.  See Power: exp vs
`EXP_NEXT[level]` @A28C = 50 150 300 420 1000 1500 3000 5000 6000 8000 10000 15000 20000 40000 50000
60000 → verdict; at ≥ need and `level < cap` (per sage @A2AC: 3 6 9 11 13 15 18 ∞) → level up:
`[8D]++`, `[B2]=[90]=max_hp`, `[B4..BA]=[AB..B1]=magic_max` from `LEVEL_TABLE` @A380 (120 160 200
240 280 320 380 460 540 600 640 680 720 760 780 800 HP), `exp -= EXP_NEXT[old]` clamped to one
level per visit.  Death entry `[A004]`: text only ("While you were unconscious, the spirits
brought you here…"), returns to town; the penalties are fight.bin's (99AD: gold = 0, almas /= 2,
hp = max, `[C4] = [C5]`).

## 8. Save file (`NAME.USR`)

* **Written by kenjpro A862** ("Record Experience"): DOS `AH=3Ch` create, `AH=40h` **256 bytes from
  BASE:0000**, `AH=3Eh`.  The file is the raw STDPLY page: the entire player record 0x00..0xE8
  (event flags 0x04..0x49, position 0x80..0x84, gold/bank/almas, equipment, items, potions, magic,
  stock masks, `[C4]` current / `[C5]` return map, spell flags 0xBB.., sage bits 0xE5) plus 23
  bytes of uninitialised memory after STDPLY (0xE9..0xFF).  No header, checksum or name inside.
* Name: `[FF6C..FF73]` player name (≤ 8 upper-case chars) + `.usr`.  The name dialog (kenjpro
  A427 / town.bin 7695 for F7) lists `*.usr` via kernel `[11C]`, lets you type or pick (Space copies
  the highlighted file, Enter accepts, Alt cancels); town.bin's version prepends **"Re-Start"** =
  new game (a `-` in the name sets `restart_chosen`).
* **Loaded by town.bin restore_game 7592** (F7 confirmed by `[11E]`): probe STDPLY.BIN (disk-1
  prompt), load `NAME.USR` raw with mode 3 to BASE:0000 (`FF78 = FF`, "User File / Not Found" →
  retry) or STDPLY.BIN for Re-Start, reload GAME.BIN to A000, `AX = 0xFFFF`, `jmp A000`.  GAME.BIN
  stores AX at A474 and skips the opening demo when it is −1, then boots normally from the loaded
  record: map `[C4]`, position `[80]/[83]`, `jmp [6002]`.

## 9. Town ↔ cavern handoff

**Town → cavern** (`goto_cavern` 6FF8, from a door with dest ≥ 8 or an exit record with flags
0x80): record = `MAP_CAVES[i]`; `scroll_row = (row-10) & 63`; `door_side [C3] = -(side&1)`;
`cur_map [C4] = map`; kernel mode 1 loads the cavern map to C000; `scroll_col = col-16` (wrapped by
the cavern width); `hero_scr_col` unchanged; `[06] = FF`; dissolve; `mov bx,6002 / xor al,al /
jmp [cs:10C]` — kernel mode 0 swaps BASE:3000..9FFF (gtmcga + town.bin) with the parked
gf*.bin + fight.bin set at (BASE+2000):9000 and jumps to `[6002]` = fight.bin entry 1.

**Cavern → town** (fight.bin 72D9, `menu_result [FF4B] == 8`, or death 99AD): the same swap with
`jmp [6000]` = `town_entry_from_fight`.  fight.bin leaves `[C4]` = the town map, the score at
arena:3000 and `[E8]` on death; town.bin then runs the walk-in (underground towns) or the sage's
death text.  `[C5]` (town_map, default 0x81) is the map the hero is sent back to.

**Doorway to the past** (prmp col 294, dest 0xFF, 6F77): first use prints script 0 ("Fooled
again!…Taste the past") and sets `[45] |= 0x80`; then `[C4] = 0x86` (Dorado), MMAN.GRP, waits for
`[FF26]`, loads UGM2.MSD, `scroll_col = 0x84, hero_scr_col = 0x0D`, dissolve, `jmp 601E`.

## 10. STATE_PAGE additions / corrections (for docs/STATE_PAGE.md)

| Addr | Meaning | Evidence |
|---|---|---|
| `[04]` bit7 | advisor flag (dialogue opcode 0x8B); cmap patch enables NPC script 5 | 664D, cmap C3D2 |
| `[05]` | king's 1000-gold gift paid | kingpro A0CE/A3EB |
| `[06]` | entered a cavern at least once | town 702E, kingpro A3EF |
| `[12]` bit3 | Hero's Crest found | bsmp patch |
| `[1A]` bit4, `[22]` bit1, `[24]` bit7/1, `[2A]` bit2, `[2B]` bit4, `[30]`, `[42]` bit3 | story event flags read by the map patch tables (Ruzeria shoes, Pirika shoes, Crest of Glory found/traded, Shirukaano shoes, ?, Llama hut cleared, lion key?) | hlmp/tmmp/drmp/prmp/llmp patches, armrpro A8CF |
| `[34]` bit7 / bit6 | Elf Crest given / Asbestos cape bought | 6685, 66F0 |
| `[45]` bit7 | doorway-to-the-past used | 6F98 |
| `[49]` | **Jashiin defeated** (ending flag), not "force_death" | 6055/61E7, kingpro A138, omoypro A03D |
| `[85..87]` / `[8B]` | **gold** (24-bit, HUD GOLD) / **almas** (HUD ALMAS) — fight.c has the names swapped | VID_NUM_GOLD/ALMAS, 66DC, kingpro A09E |
| `[88..8A]` | bank balance, 24-bit | bankpro A345 |
| `[8D]` / `[8E]` | level / exp (kenjpro is the only writer of `[8D]`) | kenjpro A306 |
| `[94]` / `[96]` | shield hp / shield hp max | armrpro A6B4 |
| `[9A]` | Elf Crest | 668A |
| `[9F]` | cleared on every town entry (temporary effect, like `[E4]`) — meaning still unknown | 60CC |
| `[A1..A5]` / `[A6..AA]` | key items / potion slots (drug id+1) | 6700, drugpro A26B |
| `[B2]` | max HP (confirmed) | kenjpro A315, churpro heal target |
| `[BB..C1]` | spells learned (FF) | kenjpro A96E |
| `[C9..D1]`, `[D2..DA]`, `[DB..E3]` | drug / sword / shield stock bitmask per town | drugpro A446, armrpro |
| `[E5]` | sages met (bit per sage) | kenjpro AC28 |
| `FF2A` | town_col_ptr = C017 + scroll_col*8 | 6157 |
| `FF4C/FF4E/FF4F` | shop text pointer / x / line | 706C |
| `FF52..FF6A` | menu state: visible rows, total, pos, scroll, show_prices, item ids[16], price column, width | 7344 |
| `FF6C..FF73` | **player name** (save file base name) — STATE_PAGE.md calls it music_drv_name | 75C6, kenjpro A862 |
| `FF26` | music fade complete (Dorado warp spins on it) *uncertain* | 6FC1 |

Other corrections: ARCHITECTURE.md ".grp families": `cpat/mpat/dpat` are the town tile banks
(§4.1), `mman/cman` NPC sprite sets and `tman` plain hero cells (§4.2); "tman starts with a pointer
table" is wrong.  ARCHITECTURE.md "Town maps … pointers at odd offsets" → §3.  VIDEO_DRIVERS.md
"which of 0x90/0xB2 is max": `[B2]` is max.

## 11. Not decoded

* ympd.bin / ckpd.bin (ZELRES2[8]/[9]): the far-called panorama painters (mountains / cave) and the
  exact pixels of the far/near parallax strips — `mdt2png.py` paints a flat sky instead.
* select.bin (status screen swapped in with Enter) and the potion effects it applies.
* gd `[3006]` call in omoypro's ending hand-off; the unreferenced gdtga record.
* `[9F]`, `[2B]` bit4, `[42]` bit3 meanings; fight.bin's death penalty formula at 9715.
* Dialogue opcode 0x8B and the `\x01` byte at the start of llmp script 3 (drawn as a glyph).

## 12. select.bin — the status / inventory screen and the potion effects

Companion to `src/select.c` (ZELRES2[1], 3613 bytes, image `A000..AE1C`).  This
closes the first bullet of §11: select.bin **is** decoded now.  Addresses are
BASE offsets; everything was read from `disasm/overlays/select.asm`.

### 12.1 Loading and calling convention

select.bin is a slot-B overlay that is never loaded on demand: GAME.BIN parks it
at **`arena:C000`** at boot, and both engines *swap* it into `BASE:A000` for the
duration of the screen so that whatever already lives at A000 (a shop overlay in
town, the enemy/boss AI in a cavern) survives.  The swap is 0x800 words in both
directions — town.bin `swap_select_overlay` 6938, fight.bin `swap_C000_A000`
72D9.

| Vector | Addr | Caller | `in_town` `[ADF8]` |
|---|---|---|---|
| `[A000]` | A004 | fight.bin `check_item_menu` 728C (`call [A000]`) | `0x00` |
| `[A002]` | A00B | town.bin `check_status_menu` 6909 (`call [A002]`) | `0xFF` |

Both fall into `select_main` A010 and differ *only* in that byte.  The caller
sets `sfx_request [FF75] = 0x0B`, calls `vid_clear_playfield [2002]`, swaps,
calls the vector, swaps back and repaints the world (town 6911..6932, fight
72A1..72B9).  The screen returns with a plain `ret`.

The **only** value handed back is `menu_result [FF4B]`, which `use_potion` sets
to the *potion slot value* (drug id + 1) of whatever was drunk.  fight.bin tests
it for **8** — id 7, the Kioku Feather — and runs `return_to_town` (729C).  No
other id means anything to the caller, and nothing ever clears `[FF4B]` again
(GAME.BIN zeroes it once at A044).

Everything is drawn through the **video driver** at `BASE:2000`
(docs/VIDEO_DRIVERS.md), never through gtmcga or gfmcga, because the same image
must run under both engines.  select.bin is the sole caller of `[202E]`
(cursor frame), `[2030]`/`[2032]` (digits) and `[2034]`/`[2036]`/`[203A]`/
`[203C]` (itemp icon sections 6/5/4/2), and the reason those slots exist.
Waiting is `idle_poll` AA58, which runs the five kernel hotkey services
`[110]..[118]` and then reports the menu key; input is `int 61h` (AL =
directions, AH = buttons) read directly.

### 12.2 Layout

Four framed windows (`vid_window` style `0xFF`), from the table at `ADE8`:

| # | x4, y | w4, h | pixels | contents |
|---|---|---|---|---|
| 0 | 0C, 0E | 38, 33 | (48,14) 224×51 | `SELECT-MAGIC:` + spell name, the magic row |
| 1 | 0C, 3F | 22, 30 | (48,63) 136×48 | `WEAR:` + item name, the item row |
| 2 | 0C, 6D | 22, 30 | (48,109) 136×48 | `USE:` + potion name, the potion row |
| 3 | 2D, 3F | 17, 5E | (180,63) 92×94 | `INVENTORY`: sword, shield, keys, crests |

The playfield outside those windows is left as the caller cleared it, so the
town's / cavern's decorative border outside x 48..271 stays on screen —
`docs/screenshots/menu.png` is exactly this screen (opened with Return in
Felishika's Castle), **not** a shorter town menu.

Headers (`draw_headers` A9D5, table A9FC, records `{u16 x_px, u8 y, asciiz}`)
are drawn in the 8×8 font with a blue drop shadow; the header of the active row
is red (colour 2), the others green (3).  `INVENTORY` is index 3 and therefore
never highlighted.

| header | x px | y | drawn red when |
|---|---|---|---|
| `SELECT-MAGIC:` | 52 | 18 | `pane == 0` |
| `WEAR:` | 52 | 67 | `pane == 1` |
| `USE:` | 52 | 113 | `pane == 2` |
| `INVENTORY` | 184 | 67 | never |

Row geometry — each row is a strip of 20×20 cursor cells (`[202E]`), with the
32×16 icon two pixels inside:

| row | icon vector | icon x4, y | cursor x4, y | pitch |
|---|---|---|---|---|
| magic | `[201E]` itemp §3 | 0E, 1C | 0E, 1A | +8 x4 (32 px) |
| item | `[2034]` itemp §6 | 0E, 55 | 0E, 53 | +5 x4 (20 px) |
| potion | `[2036]` itemp §5 | 0E, 83 | 0E, 81 | +5 x4 (20 px) |

Cursor colours: **2** red = the active row, **5** blue = an inactive row, **0**
erases.  Under each magic icon `draw_magic_counts` A929 prints the charge count
`[AB+n-1]` (colour 1) at y 46 and `(max)` `[B4+n-1]` (colour 4) at y 55.

Empty rows all print `NOTHING` (`AA92`, colour 1, no shadow): magic at (158,18),
items at (92,67), potions at (84,113).  `NO USE` (`AA9A`) is the *name* of the
leading blank slot of the item and potion rows when the row is not empty.

### 12.3 The three rows and their model

`pane [ADF9]` selects the row; the jump table is at `A0C4` = {A0CA magic, A1BB
item, A2B9 potion}.  Left/Right move within a row, Up/Down change rows, and each
row is entered only when it is non-empty.  The lists are packed at entry:

| list | source | count | cursor |
|---|---|---|---|
| `magic_list[7]` `AE03` | spell numbers 1..7 where `[BB+n-1] != 0` | `n_magic [ADFA]` | `[ADFB]` |
| `item_list[6]` `AE0A` | `0` then the non-zero bytes of `[A1..A5]` | `n_items [ADFC]` = items+1 | `[ADFD]` |
| `potion_list[6]` `AE10` | `0` then the non-zero bytes of `[A6..AA]` | `n_potions [ADFE]` = potions+1 | `[AE00]` |

The leading `0` entry of the item and potion lists is the "NO USE" slot, drawn
with the driver's built-in blank icon; the counts are `n+1` when `n > 0` and
`0` when the record is empty.  Each cursor starts on the value currently in the
record (`repne scasb` A8D7 / A6F6).

* **Magic row** — `magic_select` A135 writes the spell number to `magic_sel
  [9D]`, prints the name from the table at `AAB8` (Espada, Saeta, Fuego,
  Lanzar, Rascar, Agua, Guerra) at (158,18), and refreshes the HUD's magic box
  (`[201E]` at (222,164)) and its charge count (`[2018]`).  This is the only
  place the player picks a spell.
* **Item row (`WEAR:`)** — `item_select` A228 writes the *item id* to `[9E]`,
  the field docs/STATE_PAGE.md calls `shoes`.  So `[A1..A5]` is the bag of key
  items and `[9E]` is the single one currently **worn**; wearing is exclusive
  and choosing the leading `0` entry takes everything off.  Names from `AAF3`,
  indexed by `[9E]` itself (0 = "NO USE"): 1 Feruza shoes, 2 Pirika shoes,
  3 Silkarn shoes, 4 Ruzeria shoes, 5 Asbestos cape.
* **Potion row (`USE:`)** — moving the cursor calls `potion_show` A33C, which
  stores the slot value in `potion_sel [ADFF]` and prints the two-line name
  from `AC32`.  **The sword button (`int 61h` AH bit 0) drinks it** — see §12.4.

**Potions are cavern-only.**  The initial row choice at A09E skips the potion
row when `in_town`, and the item row's Down at A293 refuses to go there in town;
`draw_potion_row` A669 does not even draw a cursor in town.  The magic row's
Down (A190) *means* to make the same check but the `test [ADF8]` at A199 is
immediately clobbered by the `mov cl,2` that follows, so the potion row is
reachable from the magic row in town — an **original bug**, harmless because
`use_potion` still works there (the effects just have nothing to act on).

**Hidden LEVEL / EXP panel** (`show_level_box` A3B7): while the potion row is
active, `key_mask [FF18] == 0x0286` exactly opens a framed box at (108,67)
104×36 showing `LEVEL` = `[8D]+1` (2 digits) and `EXP` = `[8E]` (5 digits).
Any direction key closes it.  This is the only place outside the sage where the
level and the experience are shown.

Both the level box and the "I have used" box save the 28×36-cell region behind
them with `vid_save_rect [2026]` (`x8 6, y 0x43, w8 0x1C, 0x24 rows`, staging
offset 0) and put it back with `[2028]`; `box_open [AE02]` is the flag.

### 12.4 Potion effects (`use_potion` A40D, jump table `A452`)

Using a potion first **removes it from the record**: A422..A437 walks `[A6..AA]`
counting non-zero slots until it reaches `potion_cursor` and zeroes that byte,
then rebuilds `potion_list`.  `menu_result [FF4B]` is set to `potion_sel`.  Two
return addresses are pushed — `A5B4` `potion_epilogue` (repaint the row) and
`A2C7` (back to the potion loop) — and the effect is dispatched on
`potion_sel - 1`, i.e. the drugpro item id of §7.

| id | item | addr | effect | record fields | redraw |
|---|---|---|---|---|---|
| 0 | Ken'ko Potion | A462 | `hp += 80`, capped at `max_hp` | `[90]`, cap `[B2]` | `[2008]` life bar |
| 1 | Juu-en Fruit | A483 | `hp = max_hp` | `[90] = [B2]` | `[2008]` |
| 2 | Elixir of Kashi | A496 | refill the **selected** spell; **no effect if `[9D] == 0`** (the potion is still consumed) | `[AB+[9D]-1] = [B4+[9D]-1]` | `[2018]` + the count row |
| 3 | Chikara Powder | A4BE | refill **all seven** spells | `[AB..B1] = [B4..BA]` | `[2018]` + the count row |
| 4 | Magia Stone | A52C | arm the four orbiting spheres | writes `EB60/EB67/EB6E/EB75` | – |
| 5 | Holy Water of Acero | A4EA | `shield_hp += HOLY[shield-1]`, capped; **no effect if `[93] == 0`** | `[94]`, cap `[96]` | `[201A]` |
| 6 | Sabre Oil | A4DB | `attack_bonus++` | `[E4] += 1` | the `(n)` under the sword |
| 7 | Kioku Feather | A58B | return to the last sage | `[FF4B] = 8`, `[FF24] = 8` | dissolve + music stop |

* **Holy Water table** (`A520`, indexed by `[93]-1`): Clay **80**, Wise Man's
  **90**, Stone **100**, Honor **110**, Light **115**, Titanium **120** — added
  and then clamped to `[96]` (armrpro's maxima 30/80/180/300/300/600), so it is
  a full restore only for the two weakest shields, not "shield to full
  strength" as the shop claims.
* **Magia Stone** copies the 7-byte template at `A584` (`00 00 50 00 00 00 00`)
  into the four orb records at `EB60` (`struct Orb`, docs/FIGHT.md §6), patching
  bytes 0 and 1 each time: phases **0, 4, 8, 0x0C** and directions **+1, −1,
  −1, +1**, all with **0x50 = 80** hits.  fight.bin's `orbs_update` 86FC then
  runs them.  Drinking it in town writes the records but nothing animates them.
* **Sabre Oil** stacks (`inc [E4]`, no cap) but town.bin zeroes `[E4]` on every
  town entry (60CC), so the bonus lasts only for the current trip.
* **Kioku Feather** is the odd one: it shows its "I have used" box, repaints the
  row, then **pops the two return addresses** and returns straight out of the
  overlay after `music_fade [FF24] = 8`, a 120-tick wait, `vid_dissolve [2040]`
  and `int 60h AX=1` (stop the music).  fight.bin sees `[FF4B] == 8` and warps
  to `[C5]` (§9).  In town nothing reads `[FF4B]`, so it only fades the screen.

After any other effect the pushed `potion_epilogue` A5B4 erases the cursor,
clears the icon strip (`x4 0x0E, y 0x83, 0x1E×0x10`), forces `n_potions` to at
least 1 so the "NO USE" slot survives, redraws the row and puts the red cursor
back; then A2C7 resumes the potion loop.

The "I have used / *&lt;name&gt;*" box (`show_used_box` A5DA) is a framed window at
(60,67) 200×36 with `I have used` at (68,76) and the right-aligned name from the
table at `AB62` at (72,86).

### 12.5 The INVENTORY window (`draw_equipment` A752)

Read-only; nothing here can be selected.

| item | condition | icon | name (narrow font `[2038]`) | number |
|---|---|---|---|---|
| sword | `[92]` | `[201C]` §0, 20×18 at (184,77) | `ACD9[[92]-1]`, two lines at x4 0x34, y 0x4E / 0x56 | `draw_power` A86E: `([E4])` at y 0x57 when `[E4] != 0` |
| shield | `[93]` | `[2020]` §1 at (186,97) | `AD67[[93]-1]`, y 0x61 / 0x69 | `draw_shield_hp` A844: `(` `[96]` `)` at y 0x69 |
| keys | `[98]` | `[203A]` §4 idx 0 at (186,117) | – | `^` glyph at (200,126) + 1 digit at x4 0x34 |
| lion keys | `[99]` | `[203A]` §4 idx 1 at (234,117) | – | `^` at (248,126) + 1 digit at x4 0x40 |
| crests | `[9A]`/`[9B]`/`[9C]` | `[203C]` §2 idx 0/1/2, packed left from (192,137), +24 px | – | – |

Sword names `ACD9`: Training / Wise man's / Spirit / Knight's / Illumination /
Enchantment Sword.  Shield names `AD67`: Clay / Wise Man's / Stone / Honor /
Light / Titanium Shield.  `\` is the apostrophe glyph, as in the dialogue text.

Note `draw_shield_hp` prints the **maximum** `[96]`, not the current `[94]`; the
live durability is only on the HUD (`[201A]`).

### 12.6 STATE_PAGE.md additions from select.bin

| Addr | Meaning | Evidence |
|---|---|---|
| `[9D]` | **selected magic 1..7** — confirmed; select.bin is the only writer | A13C |
| `[9E]` | **worn key item** (0 none, 1..5 = the `[A1..A5]` item id), not "shoes" as such — 5 = Asbestos cape | A22F, `AAF3` names |
| `[A1..A5]` | key items, packed, 0-terminated in practice | A052 |
| `[A6..AA]` | potion slots, drug id + 1, packed | A643, A422 |
| `[E4]` | sword power bonus, `+1` per Sabre Oil, no cap, cleared on town entry | A4E0, town 60CC |
| `[96]` | shield HP **max** (confirmed: the INVENTORY line and the Holy Water cap) | A844, A50C |
| `FF4B` | `menu_result` — the potion slot value (id+1); **8** = Kioku Feather = warp to town | A43E, fight 729C |
| `FF18` | `key_mask`: `== 0x0286` opens the hidden LEVEL/EXP box in the potion row | A2CD |
| `EB60..EB7B` | four 7-byte orb records `{phase, dir, hits, 0,0,0,0}` — written by the Magia Stone as `{0,+1,80}`, `{4,-1,80}`, `{8,-1,80}`, `{12,+1,80}` | A52C |
| `ADF8..AE1C` | select.bin's private variables (all zero in the image) | §12.3 |

Corrections to docs/VIDEO_DRIVERS.md §1.2 that select.bin settles: `[201E]`
(there `vid_icon_item`) is the **magic** icon and `[2018]` (`vid_num_item_count`)
its **charge count**; `[2020]` (`vid_icon_magic`) is the **shield** icon and
`[201A]` (`vid_num_magic`) its **durability**.  The HUD's three boxes are, left
to right, sword `[92]` / magic `[9D]` / shield `[93]`.
