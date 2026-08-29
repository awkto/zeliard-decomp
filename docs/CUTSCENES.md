# Cutscenes — opdemo / enddemo / rokademo, the `gd*` renderer and the intro art (issue #29)

The three cutscene overlays and the last unknown graphics family.  Sources:
`src/opdemo.c`, `src/enddemo.c`, `src/rokademo.c`; decoder in `tools/grp2png.py`
(family **`gd`**) and `tools/palette.py` (`GD_BASE`, `gd_rgb8`).

| | overlay | resource | slot / entry | launched by | hands back by |
|---|---|---|---|---|---|
| §2 | **opdemo.bin** | ZELRES1[0], 13865 b | A, `[6000] = 6002` | GAME.BIN at boot (no command-line arg): sets `[FF77] = 0xFF`, `jmp [0x6002]` | reloads `GAME.BIN` from disk to BASE:A000 and `jmp 0xA000` with `AX = 0xFFFF` |
| §4 | **enddemo.bin** | ZELRES2[50], 8683 b | A, `[6000] = 6002` | **omoypro.bin** (ZELRES2[11], the Princess' chamber) when `[0x49]` is set — `A061` loads it, `A0A9 jmp [0x6000]` | never: it spins in the hotkey poller (`66C8`) and you leave with Ctrl+Q |
| §5 | **rokademo.bin** | ZELRES3[0], 1448 b | B, `[A000] = A002` | **fight.bin** `7C18`, from the door/transition handler when the door record's byte +8 has bit 7 set | plain `ret` (tail `jmp [0x2000]`) |

opdemo and enddemo are *gd demos*: they draw through **gdmcga** (ZELRES1[5],
parked at BASE:3000) and use nothing of the game engine.  rokademo is not — it
runs inside the fight screen and draws through **gfmcga**, so it is documented
here only because it is a cutscene.

Screenshots: `docs/screenshots/intro_prologue.png`, `intro_demon.png`,
`title.png`, `demo_balcony.png`, `demo_dialogue.png`, and
`intro_art.png` (fourteen resources rendered by `tools/grp2png.py`).

---

## 1. The `gd*` renderer

`gd{ega,cga,hgc,tga,mcga}.bin` = ZELRES1[1..5].  Exactly one is loaded raw to
**BASE:3000** — by GAME.BIN at boot for the opening demo, and by omoypro.bin
(table `A0BB`, indexed by `[FF14]`) for the ending.  Like the video drivers it
begins with a **near-pointer vector table**, here 25 slots at `3000..3030`;
callers use `call [cs:0x30xx]`.  Addresses below are gdmcga's.

Argument conventions are the video driver's (docs/VIDEO_DRIVERS.md §1.1):
`BH` = x in 1/80ths of the screen (4 px on MCGA), `BL` = y (0..199),
`CH` = width in **plane bytes** (= 8 PC-88 px = 4 MCGA px), `CL` = rows.
Source bitmaps are passed in **ES:DI**.

| slot | @ | name | args | what it does |
|---|---|---|---|---|
| 3000 | 44F7 | `gd_nop` | – | `ret` |
| 3002 | 3032 | `gd_draw_2plane` | AL, ES:DI, BH BL CH CL | 2 planes → colours 0,1,8,9 (bits 0 and 3 only, leaving bits 1-2 for the text overlay); dissolve (AL, below) |
| 3004 | 3088 | `gd_draw_3plane` | same | 3 planes → colours 0..7; dissolve (AL, below) |
| 3006 | 30E4 | `gd_erase` | BH BL CH CL | 8-pass dissolve of a rect to black (`AND` writer 329D) |
| 3008 | 4221 | `gd_set_palette` | AL = record 0..9 | reprograms all 256 DAC entries (§3) |
| 300A | 44CC | `gd_clear_scratch` | – | zeroes the 64 KB at `(CS+0x2000)` |
| 300C | 32C9 | `gd_text_line` | DS:SI | renders one line (to `0x0D`/`0xFF`) into gdmcga's own 320 × 10-row text buffer at `4511`, 1 byte per pixel, 8 px per glyph |
| 300E | 332C | `gd_text_scroll` | AL row, BH BL CH CL | scrolls the whole 64 KB scratch up one 320-byte row, copies text-buffer row AL into scratch row `BL+CL`, then composites the window onto the screen with `screen &= 0x9999; screen \|= src & 0x6666` |
| 3010 | 33B7 | `gd_draw_3plane_fast` | ES:DI, BH BL CH CL | as 3004 but a straight `rep movsb`, no dissolve |
| 3012 | 3437 | `gd_storm` | DS:SI = 9 × 6-byte records | the rain/lightning effect (§2.1) |
| 3014 | 364F | `gd_face_eyes` | AL | frame AL from `arena:AB40 + AL*0xCC0`, 34 × 48 bytes, 2 planes with weights **2 and 1** |
| 3016 | 36AB | `gd_face_mouth` | AL | frame AL from `arena:97C0 + AL*0x480`, 18 × 32, weights 1 and 2 |
| 3018 | 3707 | `gd_sky_dither` | – | fills A000 with the `00 10 00 10 …` / `10 00 10 00 …` checkerboard (the title-screen night sky) |
| 301A | 30FC | `gd_draw_2plane_ao` | ES:DI, BH BL CH CL | 2 planes → 8 (both set) / 10 (plane 0) / 12 (plane 1) / 0; **colour 0 is transparent** (writer 3277); `3159` forces `AL = 0`, so it always runs both dissolve passes |
| 301C | 3732 | `gd_tile_map` | DS:SI = 25 × 34 bytes | assembles a 34-byte × 200-row 3-plane picture in the scratch out of 8-row tiles of the 40-column `ttl2.grp` array (`tile = (n/40, n%40)`) |
| 301E | 37B4 | `gd_draw_ornament_row` | AL = row | copies row AL of scratch planes 0 and 1, **bit-reverses** them into a private buffer at `5191`, and draws both the normal and the mirrored version — the title screen's symmetrical corner scrollwork |
| 3020 | 38E6 | `gd_fx_sand` | AX = 0/1/2 | a built-in scripted full-screen dither effect driven by tables at `3B1F`/`3BE3`/`3C16`; the demos use it for the "rain of sand" and as a scene transition |
| 3022 | 3C1C | `gd_draw_masked` | AL = plane mask (1\|2\|4), ES:DI, BH BL CH CL | packs like 3004 — the source only contains the planes named in AL, packed consecutively (opdemo uses AL=5 for `sei.grp`, AL=7 for the assembled demon face) — but the **blit is its own**: not the shared dissolve, a private 8-pass × 13-rows interlace over the *fixed* 288 × 104 window at screen (16,16), which also **clears every part of that window the picture does not cover** (`3D71`). BH/BL only position the picture inside that window (`3CA3` subtracts 0x0410 from BX). That is how the storm demo wipes `waku.grp`'s inset between pictures. |
| 3024 | 3D79 | `gd_picture_box` | BH BL CH CL | draws the white (`0xFF`) picture-box outline used for the talking-head boxes; `3E1C` also writes eight `0x02` bytes at `di−7..di` on every row — a colour-0/colour-2 dithered shadow down the box's left side (the last byte is immediately overwritten by the frame itself, so seven show) |
| 3026 | 3E35 | `gd_fx_recolour` | ES:DI | rewrites the three planes of a 0x1028-byte picture (a colour swap) and falls into 33B7 |
| 3028 | 3E8B | `gd_wipe` | ES:DI | 0x44-step horizontal aperture wipe of a full-screen picture |
| 302A | 4080 | `gd_end_open` | ES:DI | enddemo only: 57 × 2 rows of a 47 × 114 picture with the row width stepping 0x2F → 0x23 → 0x21 (an iris) |
| 302C | 4162 | `gd_end_close` | ES:DI | the same, closing |
| 302E | 4205 | `gd_cursor_block` | AL colour, BH BL | an 8 × 8 solid block — the ending's typewriter cursor |
| 3030 | 44DE | `gd_putchar` | AL char, AH colour, BX x, CL y | `jmp [cs:0x2022]` — straight through to the video driver |

Dissolve (`31B4`, used by 3002/3004/3006/301A): eight passes; in pass *p* row
*r* is written through the 8-bit mask `mask_table[(p + r) & 7]`
(`32B9 = 80 20 08 02 40 10 04 01` for even rows, `32C1 = 01 04 10 40 02 08 20 80`
for odd rows), so one pixel in eight per row per pass.  Between passes it zeroes
`[FF1A]` and waits for it to reach **0x14 ticks** — ≈ 0.68 s for a full dissolve
at the 236.7 Hz timer.

**The `AL` argument of 3002 / 3004 / 301A** picks *which* dissolves run
(`3189` / `3171`): with `AL == 0` the routine first runs a pass with
`[4508] = 0`, whose writer (`325B`) **ORs** the source into the screen, and
then the normal copy pass — sixteen passes and an additive fade-in.  With
`AL != 0` only the copy pass runs (eight).  `301A` zeroes AX at `3159`, so it
is always the 16-pass form.  Callers that pass `0xFF` get the plain dissolve;
opdemo passes 0 at `623B` (`ttl1` fading up over the sky dither) and `6733`
(`oui.grp`), enddemo at `62CB` (the fade-to-white dither).

**Two staging buffers, not one.**  Every draw slot first packs its source into
a *second* 64 KB segment at **`CS + 0x3000`** (`3040`, `3096`, `3109`, `3C2D`)
and blits from there; the `CS + 0x2000` segment named by `gd_clear_scratch` is
the demos' own scratch, used for text scrolling and the pictures they
assemble themselves.

---

## 2. opdemo.bin — the attract sequence

Three acts.  Each begins `cli / mov sp,0x2000 / sti` and clears the two abort
flags; Space (`[FF1D]`) or Return (`[FF29] == 0x0D`) during any wait aborts the
act.  Acts 1 and 2 fall into the next one; act 3 hands back to GAME.BIN.

### 2.1 Act 1 — 6002: prologue → demon → title

| @ | what |
|---|---|
| `601E` | `ttl3.grp` (the ZELIARD logo) → `arena:4000`, palette **4**, the copyright line via `[0x202A]` at (0,150), logo drawn with `gd_draw_2plane_ao` at (28,15) |
| `6060` | `nec.grp` (the pendant) → `arena:4000`, `hou.grp` (the storm sprites) held at `CS:B800` |
| `609B` | palette **1**, pendant drawn with `gd_draw_2plane` at (72,32) — colours 0/1/8/9 only |
| `60B8` | the **prologue scroller** (§2.4) over the pendant, text `6FF0` |
| `60BB` | palette **2**, the same pendant redrawn 3-plane — it "lights up" |
| `60E3` | `hou.grp` → `arena:9000` (from `CS:B800`, DS = CS); `nec.grp`'s second picture (64 × 64) drawn at (128,72) |
| `60F9` | `[FF75] = 4` (thunder), then `gd_storm` |
| `6109` | `dmaou.grp` → `arena:97C0`; `6E0F` assembles the demon's face; the pendant is dissolved away and the face drawn 3-plane at (92,32), palette **3** |
| `6151` | eye animation from the byte script `911E` = `01 01 01 02 02 01 01 02 02 03 03 05 00` (frame = byte−1, 20 ticks each) |
| `617E` | the **typed-text player** (§2.3) plays `9096`, "Beware, for I shall wake…", with inline mouth frames |
| `61CA` | `ttl1/ttl2/ttl3.grp` + `zopn.msd` loaded, the face dissolved away, palette **4**, music started (`int 60h AX=0`) |
| `6231` | `gd_sky_dither`, then `ttl1` (the necklace, 3-plane, 196 × 128 at (44,72)), then `ttl3` (the logo, 260 × 112 at (28,15)), then `ttl2` through `gd_tile_map` |
| `629B` | 100 steps of `gd_draw_ornament_row`, two counters walking apart (`AL` 0xC7 −2, `AH` 0x00 +2) — the corner scrollwork grows in |
| `62C4` | poll until `[FF26]` (music stopped) |

**`gd_storm` (3437)** builds nine 15-byte records at `CS:A000` from the 9 × 6
table at `9060` — `{y, x4, dy, dx, frame, frame_max}`.  Each tick it advances a
record by (dx,dy), advances its frame every other tick, saves the background,
draws the sprite and restores.  The sprite table at `3617` is 8 entries of
**`{u16 ptr, u8 rows, u8 wbytes}`** into `arena:9000` (= `hou.grp`):
`348E` reads it as `mov cx,[bx+0x3619]`, i.e. the *word at +2*, so `CL` (the
row count everywhere else) is byte +2 and `CH` (the byte width) is byte +3.
The eight records are (hex) `{9000,20,06} {9180,20,06} {9300,20,06} {9480,20,06}`
then `{9600,18,04} {96C0,18,04} {9780,18,04} {9840,18,04}` — four **6 × 32**
frames (24 × 32 px) 0x180 bytes apart and four **4 × 24** (16 × 24 px) 0xC0
apart, not 128 × 6 / 96 × 4.  Decoded that way they are an expanding ball of
light: **radiating orbs**, not lightning bolts.  A record retires when
x4 ≥ 0x4B or y ≥ 0xA0.  Once **per record** — so nine times a pass, stepping
the flash index `[4505]` each time — it patches **`[4289]`, `[428A]` and
`[428B]`** (the whole R,G,B of colour 0 of palette record 0) from the 8 × 3-byte
table at `3637`
(`1F1F00, 0F0F00, 1F1F1F, 0F0F0F, 1F001F, 0F000F, 1F0000, 0F0000`) and calls
`gd_set_palette(0)`: that is the **lightning flash**, done entirely in the DAC.
It waits 0x1E ticks a pass and stops when every record has retired, restoring
palette 2.

### 2.2 Act 2 — 640C: the STAFF credits

`zend.msd` → `arena:3000`, `int 60h AX=0`, palette 1, then the same scroller
(§2.4) over black with the text block at `742F` ("Fantasy Action Game /
ZELIARD / -- STAFF --" … "GAME ARTS Co.,Ltd./ Tomoyuki Shimada").  On abort or
end: `[FF24] = 8` (music fade), `[0x2042]` clear, wait `[FF26]`, fall into act 3.

### 2.3 Act 3 — 6540: the storm demo

Fifteen pictures inside `waku.grp`'s ornate frame, with the story typed
underneath.  The whole act is one interleaving: *set up the next picture,
then call `play_narration()`*, which runs the byte script at `79C6` until it
hits `0xFD`.  Pictures, in order (all `gd` art, §6):

`waku` (the frame, 320 × 136 at (0,0)) · `ame` (balcony in the rain) ·
`hime` (Felicia) · `dmaou` (the demon, assembled and masked over the picture) ·
`isi` (Felicia turned to stone) · `oui` (the King) · `sei` (the Guardian
Spirit, `gd_draw_masked` AL=5) · `yuu1` · **`yuup` + `oup`** (Garland and the
King as talking heads in two `gd_picture_box` frames) · `maop` (Jashiin) ·
`yuu2` (Garland) · `yuu3` + `yuu4` (the throne room, §6.4) — then the closing
text block at `7338` ("At last, the door of destiny was opened…") through the
scroller, and `6A41` hands back to GAME.BIN.

**The narration engine (`6A80`)** is a proportional-font typewriter: one byte
every **0x10 ticks**, two metric tables (`947D` left bearing, `94DD` advance,
both indexed from `' '`), a drop shadow drawn one pixel down-right, and word
wrap — at a space it measures the next word (`6CC4`) and starts a new line if
`x + width >= 0x138`.  Script opcodes:

| byte | meaning |
|---|---|
| `20`–`7E` | a character; unless it is `space . , " '` it also fires `[FF75] = ` the current click id |
| `80`–`8F` | **right** speaker (`oup.grp` @ `arena:8000`): `n < 6` mouth frame *n* (`arena:98C0 + n*1344`, 56 × 32 px at (204,80), `BX = 0x3350`); `n ≥ 6` eye frame *n−6* (`arena:B840 + (n−6)*528`, 44 × 16 at **(204,56)** — `6C69` is `mov bx,0x3338`, so `BH = 0x33`) |
| `90`–`9F` | **left** speaker (`yuup.grp` @ `arena:4000`): mouth `arena:58C0 + n*864` (36 × 32 at (76,80), `BX = 0x1350`), eyes `arena:6D00 + (n−6)*528` (44 × 16 at (72,56), `BX = 0x1238`) |
| `A0`–`EA` | **not dispatched.**  `6B2B` only tests `0x80` and `0x90`, so any other high byte falls through the whole `FB…EB` chain, which saves the click id at `6B85`, walks it through `3D`…`41` and restores it at `6BD1` — an exact no-op that still pays one `0x10`-tick wait.  The script uses two: `A0` at `7C00` and `A2` at `7DE3`, dead lip-sync codes for a talking head this act never puts up.  (enddemo's copy of the engine really does dispatch `A0`, `B0` and `C0` — §4.) |
| `EB`–`F0` | set the per-character click sfx: `EB`→0x41, `EC`→0x40, `ED`→0x3F, `EE`→0x3E, `EF`→0x3D, `F0`→0 (silent) — a different "voice" per character |
| `F1 F2 F3 F7` | start line 3 / 2 / 1 / 0 and reset x |
| `F5` | pause 0xF0 ticks; `F6` = three of those |
| `F9 FA FB` | ink/shadow = (6,2) yellow / (7,0) magenta / (7,1) white |
| `FD` | **return** — end of this beat, the demo puts up the next picture |
| `FE` | clear the text box (`[0x2000]` at (0,143), 320 × 57) and reset to line 0 |
| `FF` | end of script |

A lip-sync byte costs **no time**: the four handler exits (`6C53`, `6C74`,
`6CA0`, `6CC1`) jump back to the fetch at `6A80`, skipping the
`wait_ticks(0x10)` at `6A7B` that every other byte pays.  That is what lets the
mouth track syllables instead of characters.

The script is the full English intro text ("Once, long ago, a terrible storm
came to the land of Zeliard…" through Jashiin's challenge).  Note the lip-sync
codes are woven *into* the dialogue lines, e.g.
`"{81}Duke {80}Garla{84}n{83}d! …"`.

### 2.4 The scrollers (6358 / 6497 / 6D04)

These are **three separate routines**, not one parameterised one — the same
loop open-coded three times, each with its own window, its own act's
`wait_ticks` and its own flush count:

```
gd_clear_scratch(BH=0, BL=0x20, CH=0x50, CL=0x78)   ; the same in all three
repeat
    gd_text_line(si)             ; one line, ends at 0x0D or 0xFF
    repeat 10 times              ; ten one-row scroll steps
        gd_text_scroll(row, BH, BL, CH, CL);  wait 0x1C ticks
until the line ended with 0xFF
repeat <window height> times: gd_text_scroll(0, …)  ; scroll the last page off
```

| @ | act | scroll window (`BX` / `CX`) | flush |
|---|---|---|---|
| `6358` | 1, the prologue | `(0,32) 320 × 120` (`0x0020` / `0x5078`) | `0x78` |
| `6497` | 2, the STAFF credits | the same | `0x78` |
| `6D04` | 3, the epilogue | **`(0,20) 320 × 160`** (`0x0014` / `0x50A0`) | **`0xA0`** |

The flush count is always the window height, so the last page always leaves
the window completely; `6D04` is the one that differs, and only its *scroll*
window differs — all three clear the scratch with the same `(0,32) 320 × 120`
rect.  A line of text is 10 screen rows tall, appears from the bottom of the
window and takes 10 × 0x1C ≈ 1.2 s to rise one line.  Because
`gd_text_scroll` composites with `AND 0x9999 / OR (src & 0x6666)`, the picture
underneath may only use colour bits 0 and 3 — which is exactly why the pendant
is drawn with `gd_draw_2plane` (colours 0,1,8,9) while the text lands in
colours 6,7,14,15.

---

## 3. The 256-colour `gd` palette — DECODED

`gd_set_palette` (`4221`) is the exact analogue, one level up, of the 64-colour
cell palette in `GAME.BIN @A41B`:

```
si = 4289 + AL*0x30          ; 16 RGB triples, 5-bit components
[44F8] = si                  ; the current record
for l in 0..15:
    (rb,gb,bb) = C[l]
    for r in 0..15:
        out 3C8, index++     ; DAC write index = l*16 + r
        out 3C9, C[r].r + rb
        out 3C9, C[r].g + gb
        out 3C9, C[r].b + bb
```

i.e. **`DAC[l*16 + r] = C[l] + C[r]`** per component, over a 16-entry base
table.  There are **ten records** (`4289..4469`, 48 bytes each), reproduced as
`palette.GD_BASE`; `palette.gd_rgb8(n)` builds the 256 RGB entries.

Why pairs: the renderer packs **two adjacent PC-88 pixels into one MCGA byte**
(`left<<4 | right`), so a picture is half as wide on screen as it is in PC-88
pixels and every on-screen colour is the additive blend of two of the record's
sixteen base colours — the same trick `[0x2044]` plays with 8 colours and 3
bitplanes for the cell graphics (docs/ARCHITECTURE.md).

The pixel packer is `4469`, shared by every draw slot: four 16-bit shift
registers at `[44FB] [44FD] [44FF] [4501]` (bit weights 1, 2, 4, 8) are rotated
left and `adc`'d into AX, four pixels (16 bits) per call, then byte-swapped and
`stosw`'d — so the staging bytes come out in natural left-to-right order.
Which shift register each plane is loaded into is what makes `gd_draw_2plane`
(1 and 8), `gd_draw_3plane` (1, 2, 4), `gd_face_eyes` (2, 1) and
`gd_draw_2plane_ao` (a boolean recolour to 8/10/12) different.

**Scaling to 8-bit**: DOSBox/VGA expands a 6-bit DAC component as
`(v<<2)|(v>>4)`, not `round(v*255/63)` — measured 0x3E→251, 0x1E→121,
0x0F→60, 0x0E→56.  `tools/palette.py` now uses the exact formula
(`dac8()`); the 64-colour table is unaffected because its components are only
0x00/0x1F/0x3E, where the two agree.

---

## 4. enddemo.bin — the ending

**Act 1 (`6002`)** — the reunion, and it is **not** silent.  `6318` is a
second copy of the storm-demo narration engine (§2.3) with **five** lip-sync
speaker groups instead of two, its own metric tables at `807D` (bearing) and
`80DD` (advance), and no abort keys.  `6007` points it at the script at
**`6AA8`**, and the seven calls at `60FD 6142 6172 61FA 622F 62A6 62DB` — one
after each picture — run it until the next `0xFD`.  The script is 2446 bytes
of English dialogue and narration, "At long last, Jashiin was destroyed and
the nine Tears of Esmesanti were returned to their rightful place…" through
Felicia's "When his work in the world is done, he'll come back to me… Until
then, I can only believe it, and wait for him."

| # | picture | @ | the beat that follows |
|---|---|---|---|
| 1 | Garland's portrait (`yuup`) with Felicia rising into frame beside him — `new1.grp` is a 96 × 265 strip, `6A1E` copies an 87-row window out of it and the window walks up 89 steps — then the `waku` frame wiped in (`gd_wipe`) | `60FD` | the prologue, then Garland (`9n`) "You are as beautiful as a rose in bloom!" and Felicia (`An`) "Thank you, Duke Garland." |
| 2 | the King holding Felicia (`new2`, 112 × 100 at (116,18), `bx = 0x1D12`) | `6142` | "Father!" / "My darling Felicia!" |
| 3 | the Guardian Spirit (`sei`, `gd_draw_masked` AL=5, 144 × 104 at (88,16), `bx = 0x1610`) | `6172` | the Spirit appears |
| 4 | Garland and the Spirit in two `gd_picture_box` frames (`yuup` @arena:4000, `seip` @arena:8000) | `61FA` | the Spirit (`8n`) "You fought bravely to accomplish this quest…" and Garland (`9n`) "My next mission?" |
| 5 | Felicia (`himp`) replaces the Spirit in the right box | `622F` | Felicia (`Bn`) "Must you leave so soon, Duke Garland?" and Garland's farewell |
| 6 | the two of them larger (`ne80`, `ne81`) | `62A6` | "Don't go, Duke Garland!" (`Cn`) |
| 7 | a rotating `0x55` dither drawn over the left picture (the fade to white) | `62DB` | Felicia's closing lines; then a full-screen dissolve and act 2 |

The five speaker groups (all drawn with `gd_draw_3plane_fast`; `BX`/`CX` are
the literal `mov` operands, and every one of them jumps back to the fetch at
`6323`, so like opdemo's they cost **no ticks**):

| code | @ | bank | frames | position |
|---|---|---|---|---|
| `8n` | `6568` | the **Spirit**, `seip.grp` @`arena:8000`: `arena:98C0 + n*504` | one bank of 7 × 24, no mouth/eye split | `BX = 0x3850` → (224,80) |
| `9n` | `64E3` | **Garland**, `yuup.grp` @`arena:4000` | `n < 6` mouth 9 × 32 at `58C0 + n*864`; `n ≥ 6` eyes 11 × 16 at `6D00 + (n−6)*528` | `0x1350` → (76,80) / `0x1238` → (72,56) |
| `An` | `6530` | **Felicia in the `new1` tableau** — frames inside the overlay image (`ES = CS`) | `n < 3` 5 × 11 at `7437 + n*0xA5`; `n ≥ 3` 7 × 8 at `7626 + (n−3)*0xA8` | `0x3548` → (212,72) / `0x343E` → (208,62) |
| `Bn` | `658C` | **Felicia's portrait**, `himp.grp` @`arena:8000` | `n < 6` mouth 9 × 24 at `98C0 + n*648`; `n ≥ 6` eyes 10 × 24 at `A7F0 + (n−6)*720` | `0x3450` → (208,80) / `0x3338` → (204,56) |
| `Cn` | `65D5` | **Felicia on the balcony** (`ne81`), again inside the image | 2 × 8 at `781E + n*0x30` | `0x3840` → (224,64) |

`arena:98C0` and `arena:A7F0` are the portrait base `0x8000` plus `0x18C0` and
`0x27F0` — so the "unidentified tails" of `seip.grp` and `himp.grp` (§6.3,
§8) are exactly these banks, laid out the way `yuup.grp`'s and `oup.grp`'s
are.  Which portrait `8n` / `Bn` address depends on which was loaded last:
beat 4 speaks over `seip`, beats 5-7 over `himp`.

**Act 2 (`6638`)** — the credits.  All six ending pictures are loaded, still
packed, into the 64 KB scratch (`end5` @0, `end4` @0x3400, `end6` @0x5E00,
`end7` @0x8A00, `en72` @0xB800, `fin` @0xE200), `zend.msd` starts, and the rest
of the ending is the single byte script at `787E`:

| byte | meaning |
|---|---|
| `20`–`7E` | a character at (`col*8`, `row*14 + 0x90`) in colour 7, then `col++` and a `[696B]`-tick pause; a solid block cursor (`gd_cursor_block` 0xFF) is left after it and erased before the next byte |
| `09` | tab: `col = (col + 4) & ~3` |
| `F7` | **wait for the music**: spin until the score bumps `[FF21]` (score opcode `F1`, docs/MUSIC.md), then clear it and `[FF50]` |
| `F8 w` | set the pause length (16-bit) |
| `F9` | wait until `[FF50] ≥` that length, then zero it |
| `FA b` | set the per-character delay |
| `FB r c` | set the cursor **row then column** — `679E` is `lodsw / mov [6968],al / mov [6967],ah`, and `[6968]` is the row (×14 + 0x90 at `6724`) while `[6967]` is the column (×8 at `6730`) |
| `FC` | clear the text window (0,140)–(320,200) and home the cursor |
| `FD` | newline |
| `FE` | run the next **scene** (`scene_table[696C]++`) |
| `FF` | end |

Scene table `6820` (7 entries): `end5` the castle (228 × 154 at (44,8),
`bx = 0x0B08`) · `end4` riding away (188 × 114 at (132,20), `bx = 0x2114`;
the `gd_erase` before it uses the same `0x0B08` rect as `end5`) ·
`end6` the balcony, opened by
`gd_end_open` · `end6` closed by `gd_end_close` · full-screen dissolve ·
`end7` + `en72` the landscape (320 × 134 at (0,0)) with **FIN** stamped into it
· a plain redraw of the landscape.

The script's text is the credits again (upper-case this time), plus a
**"SERVING MONSTERS"** section that names every boss against its cavern —
`Cavern of Maricia / CANGREJO`, `Peligro / PULPO`, `Riza / POLLO`,
`Cavern of Glacial / AGER`, `Cementar / VISTA`, `Tesoro / TARSO`,
`Llama Town / PAGURO`, `Cavern of Caliente / DRAGON`, `Absor / ALGUIEN`
(cross-check for docs/ENEMIES.md).

`fin.grp` is not a picture: it is **two single-plane 38 × 53 stencils**.
`6A52` ORs the first into all three planes of the landscape at +0x4CE6 (plane
stride 0x29E0) and ANDs the second out again, which is how the letters come up
white with a black outline.

---

## 5. rokademo.bin — "a Tear of Esmesanti"

Not a gd demo: it plays on the fight screen through **gfmcga**, and is the
reason `ROKA*` ("roka" = 廊下, corridor) is named that way.  `fight.bin 7C18`
runs it from the **door/transition** handler when the door record's byte +8 has
bit 7 set, i.e. on the exit door of a boss room.

1. loads `mfan.msd` (ZELRES3[94], the fanfare) → `arena:3000` and `dman.grp`
   (ZELRES3[53]) → `arena:6000`, converting 256 `cells32` (`[0x3028]`, mask to
   `arena:D000`);
2. `[0xA0]`++ (tears collected).  `A03B` is `inc [0xA0] / mov al,0 /
   cmp [0xA0],9 / jc` — the clamp is **`>= 9`**, so the ninth Tear is both the
   last and the one drawn with `[0x203E]`'s alternate icon (`AL = 1`), exactly
   as GAME.BIN's HUD painter does at `A3C1`.  The crystal then appears in
   Garland's hands (`[0x203E]` at (148,82));
3. Garland walks 13 steps right (frames 0–3, `[FF75] = 0x1A` every other step,
   erasing the trailing column each step), stands (frame 4), then raises the
   crystal (frames 5–9) and his sword (`[0x3024]`);
4. the crystal **flies to its HUD slot**: `A4A3`/`A50A` are a byte-precision
   Bresenham interpolator from (148,80) to `(tear_x[tears-1], 2)`, with
   `[0x3026]` sparkle frames drawn over a saved/restored 24 × 16 background and
   `[FF75] = 0x1C` every third step.  `tear_x` (`A569`) and the `[0x203E]`
   positions (`A572`) are the same **nine** slots GAME.BIN uses at `A3D3`;
5. the fanfare plays, and `A371` (`test byte [0xff26],0xff / jz A371`) waits
   for the **score to finish** — `[FF26]` is the music driver's "finished"
   flag, the same one opdemo spins on at `62C7`, *not* the Return key.  There
   is no keypress wait anywhere in this cutscene.  Then Garland lowers the
   crystal and walks 13 steps further right, and the routine returns.

Hero frames are **ten 3 × 3 metasprite maps of 9 bytes at `A435`**, 0-based
indices into the converted `dman.grp` bank; unlike `fman.grp`'s row-major maps
they are **column-major** (the inner loop steps y by 8, the outer steps x by 8).
There are ten, not nine: `A435 + 10*9 = A48F`, which is `wait_frame`'s first
instruction (`mov cl,[0xff33]`).  Frame 9 —
`2E 31 23 2F 32 34 30 33 35` — is the "sword fully raised" pose the loop at
`A0AE` leaves `[E7]` on (it stops when `[E7]` reaches 9 and `A0CA` draws that
frame), and the only one that uses cell `0x35`, the last in `dman.grp`.

The two gfmcga slots it borrows:

* **`[0x3022]`** (`4933`) takes `AL` = cell index and blits the 8 × 8, 32-byte
  cell at `arena:6000 + AL*0x20`, expanding two bits to a byte through `4975`
  and `stosb`-ing it — a **fully opaque** copy, there is no mask.
* **`[0x3024]`** (`4990`) does **not** draw `itemp.grp`.  It draws one of three
  96-byte 2-bpp pictures **embedded in `gfmcga.bin`** at `4A31` / `4A91` /
  `4AF1`, chosen by the equipped sword `[0x92]` through the six-entry pointer
  table at `4A25` (`4A31 4A31 4A31 4A91 4A91 4AF1`, so swords 1-3 share one
  picture, 4-5 the next and 6 the last).  16 × 24 px at `di = 0x6C10` =
  (144,86), drawn through `4092`: colour 0 transparent, 1/2 → `[4FF5] = 8`,
  3 → `[4FF6] = 9`.

---

## 6. The intro/ending art format — DECODED

`tools/grp2png.py` family **`gd`** (`GD_ART`, `render_gd`).  A gd picture is
**not** self-describing: the demo knows which unpacker each resource needs, and
passes its geometry to the renderer.

### 6.1 Pixel layout

After unpacking, a picture is `nplanes` consecutive **planes** of
`wbytes × height` bytes, MSB = leftmost pixel — a plain PC-8801 bitmap.
`wbytes` is `CH`, so the picture is `wbytes*8` PC-88 pixels wide and
`wbytes*4` MCGA pixels wide.  The renderer's `4469` packer turns each PC-88
pixel into a 4-bit value (§3) and each *pair* of neighbours into one MCGA byte
`left<<4 | right`, which is directly a DAC index in the 16 × 16 blend palette.

### 6.2 The two unpackers

Both live in each demo (opdemo `6D5E`/`6DE1`, enddemo `696D`/`69F0`) and are
byte-identical between them.

**A — mask + 2-bit delta** (`6D5E` = `6D63` then `6D8D`) — everything except
`ttl1-3.grp`:

```
u16 nmask; u8 mask[nmask]; u8 payload[];
for each mask bit, MSB first: set -> copy one payload byte, clear -> emit 0
```
producing exactly `nmask*8` bytes, which are then **un-delta'd**: each byte
holds four 2-bit fields, and `6D8D` keeps a running 2-bit XOR accumulator
*across the whole buffer*, so `pixel[i] ^= pixel[i-2]`.  A lag-2 delta is what
turns PC-88 dither patterns into long runs of zeros for the mask stage.
(`fin.grp` uses the mask stage **only** — enddemo calls `6972` directly.)

**B — RLE** (`6DE1`) — `ttl1.grp`, `ttl2.grp`, `ttl3.grp`:

| first byte | form |
|---|---|
| bit 6 = 0 | short: count = `b & 0x3F` |
| bit 6 = 1 | long: 16-bit **big-endian** word, count = `w & 0x3FFF`, `0xFFFF` = end |
| bit 7 = 0 | literal — `count` bytes follow |
| bit 7 = 1 | run — one byte follows, repeated `count` times |

### 6.3 The resources

Geometry is what the demo passes to gdmcga; "mode" is the plane→bit-weight
mapping of the entry point it calls.

| resource | pack | palette | geometry (bytes × rows, planes) | what it is |
|---|---|---|---|---|
| `nec.grp` ZELRES1[22] | mask | 1 / 2 | 44 × 104 × 3, then 16 × 64 × 3 at +13728 | the pendant (drawn 2-plane under the prologue text, then 3-plane) |
| `hou.grp` [17] | mask | 2 | **4 × (6 × 32 × 2)** then **4 × (4 × 24 × 2)** (strides 0x180 / 0xC0) | radiating orbs for `gd_storm` (colours 0,2,5,7) — the `3617` record is `{ptr, rows, wbytes}`, see §2.1 |
| `dmaou.grp` [14] | mask | 3 | 4 × (18 × 32 × 2) mouths; 5 × (34 × 48 × 2) eyes at +0x1380 | the demon's face |
| `ttl1.grp` [29] | RLE | 4 | 49 × 128 × 3 | the necklace on the title screen |
| `ttl2.grp` [30] | RLE | 4 | 40 × 40 × 2 tile array | the corner scrollwork, addressed by the 25 × 34 map at opdemo `912B` |
| `ttl3.grp` [31] | RLE | 4 | 65 × 112 × 2 (`ao` mode) | the ZELIARD logo |
| `waku.grp` [32] | mask | 5 | 80 × 136 × 3 | the ornate picture frame |
| `ame.grp` [13] | mask | 5/9 | 72 × 104 × 3 | Felicia on the balcony in the rain |
| `hime.grp` [15] | mask | 6 | 72 × 104 × 3 | Felicia |
| `isi.grp` [18] | mask | 7 | 72 × 104 × 3 | Felicia turned to stone |
| `oui.grp` [25] | mask | 7 | 72 × 104 × 3 | King Felishika |
| `sei.grp` [27] | mask | 7 | 36 × 104 × **2** (`gd_draw_masked` AL=5: weights 1 and 4) | the Guardian Spirit |
| `yuu1.grp` [33] | mask | 7 | 72 × 104 × 3 | |
| `yuu2.grp` [34] | mask | 7 | 49 × 96 × 3 | Duke Garland |
| `yuu3.grp` [35] | mask | 1 | 64 × 192 × **2** | the throne room (§6.4) |
| `yuu4.grp` [36] | mask | 1 | 21 × 160 × 3 | Garland, masked into `yuu3` |
| `maop.grp` [19] | mask | 8 | 47 × 88 × 3 | Jashiin |
| `yuup.grp` [37] | mask | 6 | 24 × 88 × 3, +6 mouths 9 × 32 × 3 (stride 864) at 0x18C0, +6 eyes 11 × 16 × 3 (stride 528) at 0x2D00 | Garland, talking head |
| `oup.grp` [26] | mask | 6 | 24 × 88 × 3, +6 mouths 14 × 32 × 3 (stride 1344) at 0x18C0, +3 eyes 11 × 16 × 3 at 0x3840 | the King, talking head |
| `himp.grp` [16] | mask | 6 | 24 × 88 × 3, +6 mouths 9 × 24 × 3 (stride 648) at 0x18C0, +3 eyes 10 × 24 × 3 (stride 720) at 0x27F0 | Felicia, talking head — the tail is enddemo's `0xBn` lip-sync bank (§4) |
| `seip.grp` [28] | mask | 6 | 24 × 88 × 3, + one bank of 7 × 24 × 3 frames (stride 504) at 0x18C0 | the Spirit, talking head — the tail is enddemo's `0x8n` lip-sync bank (§4) |
| `new1.grp` [23] | mask | 6 | 24 × **265** × 3 | Felicia full length; enddemo scrolls an 88-row window through it |
| `new2.grp` [24] | mask | 7 | 28 × 100 × 3 | the King holding Felicia |
| `ne80.grp` [20] | mask | 7 | 26 × 100 × 3 | Garland |
| `ne81.grp` [21] | mask | 7 | 18 × 81 × 3 | Felicia |
| `end5.grp` ZELRES2[53] | mask | 7 | 57 × 154 × 3 | Felishika castle |
| `end4.grp` [52] | mask | 7 | 47 × 114 × 3 | Garland and Felicia riding away |
| `end6.grp` [54] | mask | 7 | 47 × 114 × 3 | Felicia back on her balcony |
| `end7.grp` [55] | mask | 7 | 80 × 134 × **2** | the closing landscape |
| `en72.grp` [51] | **raw** | 7 | 80 × 134 × 1 | `end7`'s missing third plane, `rep movsw`'d to `arena:93C0` |
| `fin.grp` [56] | mask, **no delta** | – | 2 × (38 × 53 × 1) | the "FIN" stencil + its outline |

`zopn.msd` (ZELRES1[39]) plays over the title screen, `zend.msd` (ZELRES1[38])
over both credit rolls, `mfan.msd` (ZELRES3[94]) over the Tear cutscene.

### 6.4 Pictures the demo builds rather than loads

* **the act-1 demon face** (opdemo `6E0F`) — a 34 × 112 3-plane image at
  `(CS+0x2000):0` assembled from three *two-plane* pieces of `dmaou.grp`.  The
  inner blitter `6E6D` does **not** rewind SI, so the two-call helper that
  wraps it copies two consecutive source planes into two destination planes:
  `6E4F` writes destination plane **1 first, then plane 0** — the source planes
  come out **swapped** — and `6E5E` writes plane 0 then plane 1, in order.
  * eyes: `arena:AB40`, 34 × 48, `BX = 0x0000` (col 0, row 0), via `6E4F`
  * nose: `arena:A9C0` (= mouth base + 0x1200), 6 × 32, `BX = 0x0F30`
    (col 15, row 48), via `6E4F`
  * mouth: **`arena:9C40`** — `gd_face_mouth` frame **1**, not frame 0 —
    18 × 32, `BX = 0x0850` (col 8, row 80), via `6E5E`, so it lands in planes
    0 **and** 1, not plane 0 alone.

  Nothing writes plane 2, which is what gives the three pieces their different
  colours.
* **eye frame → 3 planes** (`6E8F`/`6EB0`) — plane 2 is derived as
  `~p0 & p1`, `p1 ^= that`.
* **`yuu3.grp`** only *has* two planes (0x3000 bytes each); `6FAC` derives the
  third before drawing.
* **`end7.grp`** likewise has two, and `en72.grp` is the third.
* **`yuu4.grp`** is stencilled into `yuu3` at +0x819 by `6F41`.

---

## 7. Verification against DOSBox

Captures with `tools/run_dosbox.sh` (`cycles=3000`), converted with
`convert -sample` and compared index-by-index against `tools/grp2png.py`
output with the correct palette record and origin:

| resource | screen | result |
|---|---|---|
| `nec.grp` (2-plane, palette 1) | prologue, 30 s | **18304/18304 pixels = 100 %** — comparing `index & 0x99`, i.e. allowing the scroller's `\|0x66` text bits, which is itself a confirmation of §2.4 |
| `ttl3.grp` (`ao` mode, palette 4) | title, 88 s | **14937/14937 = 100 %** of the opaque pixels (14183 colour-0 pixels correctly show the sky dither through) |
| `ttl1.grp` (3-plane, palette 4) | title, 88 s | 98.7 % — every mismatch is where `ttl2`'s ornament is drawn on top |
| `waku.grp` (3-plane, palette 5) | storm demo, 34 s | **13568/13568 = 100 %** outside the inset picture |
| `ame.grp` (3-plane, palette 5) | storm demo, 34 s | **29952/29952 = 100 %** |
| `dmaou.grp` mouth frame 3 | demon, 62 s | **100 %** |
| `dmaou.grp` eye frame 4 | demon, 62 s | **100 %** (and the `[0x3014]` plane order 2,1 is what makes it so — with 1,2 the best frame scores 78 %) |
| `oup.grp` portrait (palette 6) | dialogue, 430 s | **8448/8448 = 100 %** |
| `yuup.grp` portrait | dialogue, 430 s | 97.9 % — the mismatch is exactly the lip-sync mouth frame drawn over it |

The recipe used (adds to docs/DOSBOX_RECIPE.md §1): with **no keys**, the
prologue text is on screen 8–55 s, the demon 56–80 s and the title screen from
≈82 s.  With `KEYS="6:Return 9:Return"` the storm demo starts ≈17 s and runs
for minutes — the balcony is up at 34 s, the throne-room dialogue at ≈430 s.

## 8. Decoded since (issue #30) — and what is still not

Three of the four items this section used to list came out while the cutscenes
were ported into `port/` (`port/gd.c`, `port/cutscene.c`):

* `ttl2.grp`'s **tile semantics**: `gd_tile_map` (`3732`/`3753`) divides each
  map byte by 40, so **tile *n* is one byte × 8 rows at `(n/40, n%40)`** of the
  40 × 40 bank (source stride 40 a row, 1600 a plane), assembled into a
  34 × 200 three-plane picture in the `CS + 0x2000` scratch (destination stride
  34 a row, `0x1A90` a plane) — the 25 × 34 map at `912B` walks it column by
  column, `bh` = x, `bl` = y.
* The **tails of `himp.grp` and `seip.grp`** are enddemo's own `0xBn` and `0x8n`
  lip-sync banks: 7 × 24 frames of 504 bytes at `arena:98C0` for `seip`, and
  9 × 24 / 10 × 24 frames of 648 / 720 bytes at `arena:98C0` and `arena:A7F0`
  for `himp`.  See §4.
* `gd_draw_ornament_row` (`301E`) and **`gd_fx_sand`'s three tables**: `3C16`
  the mode pair, `3A5F` twenty-four 8-byte patterns, `3B1F` the 196-byte border
  script and `3BE3` the 51-byte inward spiral.

Still not decoded:

* The gd renderer's four non-MCGA builds (`gdega/gdcga/gdhgc/gdtga`) were not
  read; only the 25-slot table is assumed common (as it is for `gm*`/`gf*`).
