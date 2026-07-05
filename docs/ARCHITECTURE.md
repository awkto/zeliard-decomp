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
| `BASE:0100` | **STICK.BIN — resident kernel.** INT 8/9 handlers at 0100/0103/0106/0109, near-call service vector table (`[0x10C]`, `[0x120]`, `[0x142]`, …), SAR resource loader (owns the `zelres1.sar` name template, digit patched for archive 2/3), joystick I/O | STICK.BIN |
| `BASE:2000` | Video driver for configured mode (CGA/EGA/HGC/MCGA/Tandy) | GM{CGA,EGA,HGC,MCGA,TGA}.BIN |
| `BASE:A000` | GAME.BIN — boot dispatcher; sets up state, pulls first overlay from ZELRES1 | GAME.BIN |
| `BASE+1000:0000` | Overlay/resource arena (game code + data paged in from .SAR) | ZELRES*.SAR entries |
| `BASE:FF00`–`FF7F` | Global state page: far ptrs to loader services + old INT vectors (`FF00` svc, `FF04` old INT8, `FF79` old INT9), video-mode index `FF14`, joystick flag `FF15`, overlay-arena segment `FF2C`, music-driver 8-char name `FF6C`, dozens of game flags | ZELIARD.EXE init |
| `BASE+FF0:0100` | Music driver (score playback) | MSC{STD,ADLIB,JR,MT}.DRV |
| `BASE+FF0:1100` | Sound-effect driver | SND{STD,ADLIB,JR}.DRV |

Loader filename-table format (in ZELIARD.EXE data): `uint16 load_offset,
char name[] ASCIIZ`. The load helper at `.EXE:04EF` opens the file and reads up
to 64 KB to `ES:load_offset`.

All loaded binaries are raw headerless 16-bit code (no MZ header); overlays and
drivers call kernel services via the vector table inside STICK.BIN, e.g.
`call [cs:0x10C]` / `call [cs:0x120]` / `call [cs:0x142]`.

## .SAR archive format (verified byte-exact, zero gap anomalies)

```
header : uint32le offset[N]      ; N = offset[0] / 4
entry  : at offset[i]: uint32le payload_len, uint8 payload[payload_len]
```

| Archive | Entries | Contents |
|---|---|---|
| ZELRES1.SAR | 40 | [0] main engine overlay (13.8 KB, contains intro/story text + credits), [6] second code overlay, [1–5,7–11] more engine/script data, [12+] graphics & maps |
| ZELRES2.SAR | 58 | ~12 code overlays ([1],[7],[10–17],[50],[54] — town/shop/menu logic; [10–17] share a common prologue), rest graphics/tile data |
| ZELRES3.SAR | 96 | Almost all data: sprite/enemy records ([1–19]), music scores ([20–50], note-event streams), tile/map data, palettes; [0] small code stub |

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

### Compression (discovered, not yet fully decoded)

`0x0D9D`: payloads are staged raw at `(BASE+0x3000):0000` (the top half of the
256 KB arena), then decompressed by an 8-opcode engine — dispatch
`and al,7; jmp [cs:bx+0xDBC]`, handlers at
`0DCC` (literal run: `mov cx,dx; rep movsb`), `0DD1` (RLE fill via
nibble-keyed lookup table that sits at the head of the stream — hence the
`00 06`/`00 07` prefixes on graphics entries), plus `0E13, 0E34, 0E73, 0E9C,
0EBA, 0EF5`. **TODO:** decode all 8 handlers and write a Python decompressor.

Other traced kernel services: vectors `0x6AC/0x723/0x881/0x8EF` query the
input bitmask at `cs:0xFF18`; `0x918` reads `cs:0xFF1B` (timer/frame counter);
`0x0C01` swaps a 0x3800-word buffer between `(BASE+0x2000):9000` and
`BASE:3000` (page-flip/undraw buffer). STICK itself calls into the video
driver via its vector table at `0x2000+` (e.g. `[cs:0x202A]` = draw text/tile,
used for the "insert disk" prompt).

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
