# Zeliard (DOS, 1990) — Reverse Engineering Notes

Zeliard v1.208, Game Arts Co. Ltd (1987–1990), published by Sierra On-Line.
16-bit x86 real-mode DOS game with a custom overlay/resource system.

## Boot chain

```
GAME.BAT → ZELIARD.EXE (3 KB bootstrap loader)
             │  parses RESOURCE.CFG   (videoDrv / musicDrv / soundDrv / JOYSTICK)
             │  EXECs MTINIT.COM      (only when musicDrv = MSCMT.DRV, Roland MT-32 init)
             │  INT 21/48: allocates 0x4000 paragraphs = 256 KB  → segment BASE
             │  loads flat binaries into the block (see memory map)
             │  hooks INT 8 (timer), INT 9 (kbd), INT 24h (crit err), INT 61h
             └─ far-jumps into GAME.BIN
```

## Memory map of the 256 KB arena (BASE = allocated segment)

| Linear (BASE-relative) | Contents | Source file |
|---|---|---|
| `BASE:0000` | STDPLY.BIN — tiny standard-player stub (233 b) | STDPLY.BIN |
| `BASE:0100` | **STICK.BIN — resident kernel.** INT 9/8/24h/61h thunks at 0100/0103/0106/0109, 11-slot near-call service vector table `[0x10C]..[0x120]` (see docs/SERVICES.md), SAR resource loader (owns the `zelres1.sar` name template, digit patched for archive 2/3), joystick I/O | STICK.BIN |
| `BASE:2000` | Video driver for configured mode (CGA/EGA/HGC/MCGA/Tandy) | GM{CGA,EGA,HGC,MCGA,TGA}.BIN |
| `BASE:A000` | GAME.BIN — boot dispatcher; sets up state, pulls first overlay from ZELRES1 | GAME.BIN |
| `BASE+1000:0000` | Overlay/resource arena (game code + data paged in from .SAR) | ZELRES*.SAR entries |
| `BASE:FF00`–`FF7F` | Global state page: far ptrs to loader services + old INT vectors (`FF00` svc, `FF04` old INT8, `FF79` old INT9), video-mode index `FF14`, joystick flag `FF0A`, MT-32 flag `FF15`, overlay-arena segment `FF2C`, player name `FF6C` (save-file stem), dozens of game flags | ZELIARD.EXE init |
| `BASE+FF0:0100` | Music driver (score playback) | MSC{STD,ADLIB,JR,MT}.DRV |
| `BASE+FF0:1100` | Sound-effect driver | SND{STD,ADLIB,JR}.DRV |

Loader filename-table format (in ZELIARD.EXE data): `uint16 load_offset,
char name[] ASCIIZ`. The load helper at `.EXE:04EF` opens the file and reads up
to 64 KB to `ES:load_offset`.

All loaded binaries are raw headerless 16-bit code (no MZ header); overlays and
drivers call kernel services via the vector table inside STICK.BIN, e.g.
`call [cs:0x10C]` / `call [cs:0x120]` — full reference in docs/SERVICES.md; video driver vectors at `0x2000+` in docs/VIDEO_DRIVERS.md.

## .SAR archive format (verified byte-exact, zero gap anomalies)

```
header : uint32le offset[N]      ; N = offset[0] / 4
entry  : at offset[i]: uint32le payload_len, uint8 payload[payload_len]
```

| Archive | Entries | Contents |
|---|---|---|
| ZELRES1.SAR | 40 | [0] main engine overlay (13.8 KB, contains intro/story text + credits), [6] second code overlay, [1–5,7–11] more engine/script data, [12+] graphics & maps |
| ZELRES2.SAR | 58 | ~12 code overlays ([1],[7],[10–17],[50],[54] — town/shop/menu logic; [10–17] share a common prologue), rest graphics/tile data |
| ZELRES3.SAR | 96 | Almost all data: sprite/enemy records ([1–19]), cavern maps ([20–50] mp*.mdt), music scores ([85–95], three arrangements each — docs/MUSIC.md), tile/sprite banks, palettes; [0] small code stub |

Extractor: `tools/sarex.py` → `extracted/ZELRES{1,2,3}/NNN_{code,data}.bin`.

### Resource-load service (STICK.BIN, traced)

Public entry: `call [cs:0x10C]` with **ES:DI** = destination, **DS:SI** =
request block, **AL** = mode (observed 2, 3, 5 — raw vs. decompress variants).

Request block (`lds bx,[cs:0xF5C]` in the kernel):

```
+0  byte  archive#   (0-2; patched as ASCII digit into "zelres1.sar" @ cs:0xD3B)
+1  byte  resource#  (1-based; 0 = open external file, name follows at +2)
+2  char  filename[] (only when resource# == 0 — save-game "USER file" path)
```

Kernel routine `0x0C42`: opens the archive, seeks `(resource#-1)*4`, reads the
entry offset dword (buffer `cs:0xD7A`), seeks it, reads the payload length
dword into `cs:0xF64`. `0x0D84` reads payload to `[cs:0xF60]` far ptr;
`0x0D93` closes. On open failure it prompts to insert the disk (patches the
digit into the prompt string too) and retries.

### Service [0x10C] AL modes (dispatch 0x0A84, subtable 0x0ACA)

| AL | Handler | Action |
|---|---|---|
| 0 | 0x0C01 | swap 0x3800 words `BASE:3000-9FFF` ↔ `(BASE+0x2000):9000-FFFF` (gf*.bin+fight.bin parked there by GAME.BIN at boot), then `jmp [cs:BX]` — town↔fight flip without disk I/O |
| 1 | 0x0AD6 | load *system* resource #AH (11-byte records `{archive, res#, name}` at `cs:0xF68` — the `.MDT` map table) to `BASE:C000`, then as AL=2 |
| 2 | 0x0AFF | **load + decompress** to ES:DI (container below) |
| 3 | 0x0C2F | load raw to ES:DI (code overlays) |
| 4 | 0x0B6F | install sword block #AH: copy 4 KB sub-resource of sword.grp (cached at `(BASE+0x2000):1800`) to `arena:B000`, relocate first 15 words by +0xB000 |
| 5 | 0x0BAE | load music score: `{u16 lenA, u16 lenB} + blobA + blobB`, A when `FF15` (MT-32) else B |
| 6 | 0x0C24 | probe: open/seek only, length → `cs:0xF64` |

AL=2 container: `byte0 == 0` → rest is one compressed stream; `byte0 != 0` →
`{u8 flag, u16 lenA, u16 lenB}` + two per-video-mode streams (mode 0 = EGA
uses A, others B; e.g. `font.grp` = ZELRES1[12], `5+0x7C3+0x4D1 = 3225` ✓).

### Compression — DECODED (`tools/sardec.py`, 194/194 entries verified)

`0x0D9D`: stream staged at `(BASE+0x3000):0000`, first byte `& 7` = opcode,
dispatch table `0x0DBC`. All RLE variants; DX = remaining input:

| Op | Handler | Scheme |
|---|---|---|
| 0 | 0DCC | stored (rest literal) |
| 1 | 0DD1 | table RLE: head = `{key,val}` pairs (key lo-nibble 0) to `0xFF`; byte matching key by hi-nibble → val × (lo-nibble+2) |
| 2 | 0E13 | marker RLE: marker byte M; byte with hi-nibble == M → next byte × (lo-nibble+3) |
| 3 | 0E34 | as op 1, nibbles swapped: key by lo-nibble, count = hi-nibble+2 |
| 4 | 0E73 | as op 2, nibbles swapped: match lo-nibble, count = hi-nibble+3 |
| 5 | 0E9C | doubled byte `B B n` → B × (n+2) |
| 6 | 0EBA | byte-keyed `{key,val}` word table to `0xFFFF`; match → count byte `n` from stream, val × (n+2) |
| 7 | 0EF5 | escape byte E; `E v n` → v × (n+3) |

Other kernel services (full table in docs/SERVICES.md): `[0x110..0x11E]` are the
per-frame hotkey pollers (Ctrl+Q exit, Esc pause, F9 speed → `FF33`, Ctrl+J/K joystick,
F7 restore) reading the hotkey mask at `cs:0xFF18`; `[0x11A]` is a PRNG seeded by the
236.7 Hz tick counter `FF1B`; `[0x11C]` = DOS FindFirst/Next (save files `*.usr`).
STICK calls the video driver via its own vector table at `0x2000+`
(`[cs:0x202A]` text draw, used for the "insert disk" prompt).

## Runtime code layout & overlays

All code modules share the single BASE segment; `[cs:0xFF2C]` (= BASE+0x1000)
is a separate 64 KB **data arena** for graphics/maps. Code overlays are raw
(AL=3) images whose leading words are **entry-point vectors**; GAME.BIN jumps
via `jmp [0x6000]` / `jmp [0x6002]` after loading.

| Slot | Origin | Overlays |
|---|---|---|
| A | `BASE:6000` | opdemo.bin (entry 0x6002), town.bin (0x6026/0x601E), fight.bin, enddemo.bin |
| B | `BASE:A000` | select.bin, mole.bin, kingpro/omoypro/armrpro/bankpro/churpro/drugpro/innapro/kenjpro.bin, rokademo.bin — overwrite the spent GAME.BIN boot code |

## Graphics: cell format and MCGA palette — DECODED (`tools/cellsheet.py`, `tools/palette.py`)

Pixel data is PC-88 heritage. A **cell** is 48 bytes = 8 rows × 3 big-endian
16-bit bitplane words (A, B, C): 16 px per row, 3 bits per pixel,
`v = C<<2 | B<<1 | A`, bit 15 = leftmost. `v` indexes an 8-colour table:
0 black · 1 white · 2 red · 3 green · 4 cyan · 5 blue · 6 yellow · 7 magenta
(`GAME.BIN @A456`).

**MCGA conversion — video service `[0x2044]` (GMMCGA @2C2A):** takes DS:SI =
bank, CX = cell count; copies the cells to a staging area at `(CS+0x3000):0`
and rewrites them in place, packing each pair of adjacent 3-bit pixels into one
6-bit value `left<<3 | right` (8 px → 3 bytes, MSB-first). A 16×8 PC-88 cell
therefore becomes an **8×8 cell with 64 colours** on MCGA. The fight blitter
(gfmcga @412F) writes those 6-bit values straight into A000 as VGA colour
indices (0 = transparent), reading per 3 bytes `b0 b1 b2` →
`[b1>>2, (b1&3)<<4|b0>>4, (b0&15)<<2|b2>>6, b2&63]`.

**MCGA palette — `GAME.BIN @A41B`** (mode-4 branch of the video-mode jump
table @A3ED; EGA branch @A3FE does `int 10h AX=1002h` with the 17-byte block
@A409): for every pair, `DAC[left*8+right] = BASE[left] + BASE[right]`
per component, BASE from the table above with components 0x00/0x1F — i.e.
every on-screen colour is the additive blend of two PC-88 colours (0..0x3E of
0x3F). `tools/palette.py` reproduces the 64 entries. The intro/title renderer
(gdmcga @425E) programs a different 256-entry scheme (16 base colours × 16
offsets via a table at `[0x44F8]`) for the demo/ending art — not yet decoded.

### .grp resource families (`tools/grp2png.py`, Sprint 2)

| Family | Resources | Layout | Consumer |
|---|---|---|---|
| **cells48** bank | **mpp1-9,a,b.grp** (ZELRES3[74..84], per-cavern map tiles → `arena:8000`, cell 0 = passable-cell list + special-tile lists), **dchr.grp** (ZELRES3[54], gates/chests/items → `arena:8C00` = slot 0x40), roka.grp (→ `arena:8000` for the Roka demo), shop portraits king/omoya/armor/bank/church/drug/inn/kenjya.grp (→ `arena:8000`, 256 cells converted by kingpro etc.), en72.grp | plain array of 48-byte cells, index 1-based (`cell N at bank+N*0x30`, 0 = empty) | fight blitter gfmcga @412F via `[0x3008]`; portraits arranged by maps inside the *pro.bin overlays |
| **cells32** bank (2 bpp) | enp1-8.grp — per-cavern enemy sprites, chosen from the 11-byte table at fight.bin 9D8D by map byte `[C000+5]`, loaded to `arena:4000` and converted by gfmcga vec_20 @4EDD (`[0x3028]`, CX=0x100) | 32-byte cells: 8 rows × {planeA u16 BE, planeB u16 BE}, 16 px, `v = B<<1|A`. Converter interleaves to 2 bpp and writes a per-row **outline mask** to `A000+cell*8`: `m = A|B; d = m | m>>1 | m<<1; mask bit = pair fully clear in ~d` (horizontal dilation → the black outline). Blit (gfmcga 3599/366E): each **pixel pair** `(left<<2|right)` indexes a 16-entry table → VGA index; 5 tables @4F98/4FA8/4FB8/4FC8/4FD8 selected by `[0x4ff4]` = 4 PC-88 colours each: {blk,wht,red,grn}, {blk,red,cyn,yel}, {blk,wht,cyn,blu}, {blk,blu,yel,mag}, {blk,yel,blu,mag}. Table[0] = black = outline colour; transparency comes from the mask. | fight.bin composes sprites into a 36-column cell layer at `[FF31]` (ring E000–E900, 64 rows) which gfmcga draws over the 28-column background cell buffer at E900 |
| **hero** | fman.grp (fight hero, loaded to `arena:6000`; `[0x3028]` with SI=6333, CX=0xE6) | `0x000–0x333`: **91 frame maps × 9 bytes** = 3×3 cells row-major, 1-based index into the bank, **bit 7 = horizontal flip**, 0 = empty (frames are 24×24). `0x333–end`: cells32 bank (cell 0 blank). Frame 0-9 stand/walk right, 10-19 left (flipped), then jump/attack/partial overlays. Colours = 2bpp table 0. | fight.bin animation tables (Sprint 5) |
| **font** | font.grp (AL=2 two-variant container; 3 sections `0006 0606 06A6`) | section 1: 192 glyphs × 8 bytes 1 bpp from char 0x20 | kernel/video text service `[0x202A]` |
| **3-section shape containers** | sword.grp (`0006 06D1 1043`), itemp.grp, magic.grp | `{u16 hdr_len=6, u16 off2, u16 off3}` then three sub-resources, each `{u16 data_off, u16 ptr[14]}` (ptrs relative to the sub-resource) → per-item byte scripts (`46 01 23 01 01 22 … ff`) and 16-bit bitmask rows at `data_off` (sword-swing shapes). sword shapes decoded in docs/FIGHT.md §6 | partially decoded |
| **town tiles** | cpat/mpat/dpat.grp (→ `arena:8000` for town.bin) | 0x100 header (type table = which plane is the sky mask, block list, tile-cycle list) + 48-byte cells — docs/TOWN.md | town.bin / gt* renderer (`tools/mdt2png.py --town`) |
| **town/demo art** | mman/cman/tman.grp (`GAME.BIN @A1E0` picks MMAN/CMAN by map byte, → `arena:4000` for town.bin), ttl1-3, end4-7, ame/dmaou/hime/… (intro/ending) | mman/cman = 5 NPC sprites × 8 frames × 6 cells48 (colour 0 transparent), tman = 46 hero cells48 (docs/TOWN.md); intro art is the gd* renderer's own format with the 256-entry palette (gdmcga @425E) | gtmcga / gdmcga — **not decoded yet** |

`tools/grp2png.py --all OUTDIR` renders every resource with a known family; `tools/cellsheet.py` is the low-level 48-byte viewer.

## Maps: `.mdt` cavern format — DECODED (`tools/mdt2png.py`, Sprint 3)

`mp10..mpa0.mdt` (ZELRES3[20..50]) are loaded **raw** to `BASE:C000` by kernel
mode AL=1 (record table `cs:0xF68`, index in AH); every pointer inside is an
absolute BASE offset. Consumer: fight.bin (addresses below).

```
C000 u16 level        -> level record (see below)
C002 u16 width        map width in 8x8 cells (42..320); the map WRAPS horizontally
C004 u16 fixtures A   list of {u16 col, u8 row}: three DCHR cells 40,41,42 at col..col+2 (7FB1)
C006 u16 fixtures B   same layout, DCHR cells 43,44,45 (8163)
C008 u16 fixtures C   {u16 col|variant<<14, u8 row|state<<6, u8[4]}: DCHR cells 46,47,48 (81AE)
C00A u16 doors        12-byte records {u16 col, u8 row, u8 letter|flags, ...}: door/transition table (DCHR 0x4A + Up, 7A83/7B32), sign drawn 4 rows x 5 (78DD)
C00C u16 patches      {u16 ptr, u8 mask, u16 ptr, u16 val}* conditional pokes (6BFC)
C00E u16 init data    passed to video service [0x2010] at level start
C010 u16 objects      16-byte records {u16 col, u8 row, u8 ?, u8 type, ...}; col 0xFFxx = disabled
C012 u8  cavern       1..10
C013 u16 start col    0xFFFF = none (entered from another map);  C015 u8 start row
C016 u8  row bias     10 = normal cavern, 12 = boss room, 13 = mp90
C017 u16 ?            usually 0, sometimes a pointer
C019 u16 stream end   == C004: the byte after the tile stream (used to decode backwards)
C01B     tile stream  column-major RLE, 64 rows per column
```
All lists end with a `0xFFFF` word.

**Tile stream** (decoder 6CED/6D57, reverse decoder 6D06 for left scrolling):
byte `b`, `k = b >> 6`:

| k | bytes | run |
|---|---|---|
| 0 | `b v` | `b+1` cells of value `v` |
| 1 | `b` | `((b>>4)&3)+2` cells of value `(b&15)+1` |
| 2 | `b` | `b&0x3F` empty cells (value 0) |
| 3 | `b` | one cell of value `b&0x3F` |

Runs never cross a column; each column totals 64 rows; decoding all
`width` columns lands exactly on `[C019]` for all 31 maps.

**Cell values** are 1-based indices into the cavern **tile bank** at `arena:8000`
(48-byte cells, converted by `[0x2044]`): `MPP1..MPP9,MPPA,MPPB.GRP`
(ZELRES3[74..84], request table fight.bin `9C43`, chosen by level-record byte +2),
with `DCHR.GRP` (ZELRES3[54], "dungeon characters": gates, chests, item icons)
loaded to `arena:8C00` = bank slot `0x40`. Cell 0 of an MPP bank is not graphics:
its first 24 bytes list the **passable** cell indices (collision, fight.bin 6DF3 — callers treat
"found" as free; mpp1's list contains 0 and not the rock wall 6), followed by conveyor-L/R
(8018/801C), hazard (8020), updraft (8024) and current-L/R (8028/802C) lists — see docs/FIGHT.md §3.
Ring buffer: fight.bin keeps 36 columns × 64 rows at `E000` (row stride 0x24),
`[FF31]` = top-left of the 28×19 window (`+ [0x82]` rows); cells with **bit 7**
are sprites (`0x80 | object index`, looked up in the C010 table: byte +4 = type)
and are drawn by gfmcga 3336/33AB through the 2-bpp sprite path. `E900` holds
the 28×19 copy of what is on screen (0xFD = force redraw, 0xFF = hero).

**Level record** (at `[C000]`; 7E93/7EBB):
```
+0 flags   bit0 = 1 → keep current 4000 bank; bits 1-4 = music index into 9E53 table
           (mgt1,ugm1,mgt2,ugm2,mus1..mus8,mbos,mmao); bit6 → [E6], bit7 → [FF34]
+1 gfx     index into 9C2D table (roka/dchr/rokademo/mman/cman) → arena:4000 when bit0 = 0
+2 tileset index into 9C43 table (MPP1..MPPB) → arena:8000
+3 ai      index into 9CBC table (EAI1..8 interleaved with boss AIs CRAB,TAKO,TORI,ZELA,
           MEDA,LEGA,DRGN,AKMA,MAO1,MAO2,ZEL2) → BASE:A000 (raw overlay)
+4 enemies index into 9D8D table (ENP1..8 interleaved with boss banks CRAB..MAO2.GRP) →
           arena:4000; 0xFF = keep
boss rooms (mpNd) continue: +5 boss bank (copied to +4 at 6117 when the boss appears),
           +6/+7 post-boss ai/enemies, +8.. {u16 ptr, u16 val}* pokes applied by 72F1
```
Cavern *N* normal maps use ai = enemies = 2(N-1); boss rooms use the odd index (boss AI)
with enemies = 0xFF. mp73 is the special ZEL2/MPPB map.

**Town maps** (`cmap/mrmp/stmp/bsmp/hlmp/tmmp/drmp/llmp/prmp/esmp.mdt`, ZELRES2[36..45])
are raw (not RLE) `width × 8` column-major grids with a different header (level, place label,
edge exits, doors, cavern entries, dialogue, NPC records, patches) — DECODED in docs/TOWN.md
(Sprint 6); `tools/mdt2png.py --town OUT` renders all 10.

`tools/mdt2png.py FILE OUT.png` renders a map at 8 px/cell with fixtures composited and
objects boxed (yellow/cyan/red by type class); `--all OUTDIR` does all 31; `--text` dumps
the grid; `--info` prints the header, lists and object table.

## Resource names — recovered (`tools/resnames.py` → docs/RESOURCES.md)

167/194 entries have their original filenames embedded in request blocks
(the kernel ignores the name unless res# = 0, but the developers left them
in). Highlights: `gd/gt/gf{ega,cga,hgc,tga,mcga}.bin` = per-mode sprite
renderers (dungeon/town/fight), `eai1-8.bin` = per-cavern enemy AI,
`mp10..mpa0.mdt` = 31 cavern maps, `*.msd` = music scores
(zopn/zend/mgt1-2/ugm1-2 + per-cavern mus1-5 via a name directory),
`ttl1-3.grp` = title screens, `end4-7.grp` = ending art.

## Support files

- `EXISTS.COM`, `SPACE.COM`, `GODIR.COM` — installer helpers (file check, free-space check, mkdir+cd)
- `MTINIT.COM` — Roland MT-32 initialization
- `RESOURCE.CFG` — 4 lines: `videoDrv:MCGA`, `musicDrv:`, `soundDrv:`, `JOYSTICK:NO`

## Disassembly index (`disasm/`, ndisasm, correct origins)

| File | Origin | Notes |
|---|---|---|
| ZELIARD.asm | 0000 (file 0x200+) | bootstrap loader — fully understood |
| STDPLY.asm | 0x0000 | player stub |
| STICK.asm | 0x0100 | resident kernel — **highest-value target** |
| GM*.asm | 0x2000 | video drivers |
| GAME.asm | 0xA000 | boot dispatcher |
| MSC*.asm | 0x0100 | music drivers |
| SND*.asm | 0x1100 | sfx drivers |
| overlays/*.asm | 0x0000 | SAR code overlays (origin unknown until header decoded) |

## Next steps

1. Decode the overlay header + STICK.BIN service table (names & calling
   conventions for each vector) — unlocks correct origins for all overlays.
2. Ghidra project (Java 21 available) for real C decompilation of
   STICK.BIN + ZELRES1[0] engine.
3. Decode graphics format (planar, per-video-mode) and music score format.
4. Long-term: reconstructable C source or modern port (SDL).
