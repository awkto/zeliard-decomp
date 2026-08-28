# Kernel services — STICK.BIN (BASE:0100)

Companion to `src/kernel.c` (hand-cleaned C, one function per kernel routine, original
addresses in comments).  Everything here was verified against `disasm/STICK.asm`; caller
examples are real lines from `disasm/`.  Origins: STICK.BIN @0100, GAME.BIN @A000,
slot-A overlays (opdemo/town/fight/enddemo) @6000, slot-B overlays (select, *pro, mole,
rokademo) @A000, renderers (gd/gt/gf*) @3000.

## Entry points and vector table

STICK.BIN starts with four `jmp` thunks (installed as interrupt vectors by ZELIARD.EXE,
`disasm/ZELIARD.asm 0274..0292`) followed by the near-call service table.

| Offset | Target | Installed as | Purpose |
|---|---|---|---|
| `0100` | `02C5` | **INT 09h** | keyboard handler |
| `0103` | `0250` | **INT 08h** | timer handler (PIT reprogrammed to divisor 0x13B1 = **236.7 Hz**, ZELIARD @02BF) |
| `0106` | `0F18` | **INT 24h** | critical-error handler |
| `0109` | `05FD` | **INT 61h** | "read input state" service (AL = directions, AH = fire) |

(ARCHITECTURE.md lists 0100 as INT 8 and 0103 as INT 9 — it is the other way round.)

**Vector table extent: `010C..0121`, exactly 11 word slots.**  `0122` is the first code
byte (`test byte [cs:0x2BE],0xFF`, part of the timer's fire-key edge detector).  The
`[0x142]` mentioned in ARCHITECTURE.md is not a vector: no file in `disasm/` references
`[cs:0x142]`, and 0x142 is mid-instruction inside that detector.  INT 60h (music driver
dispatcher, `(BASE+FF0):0103`) is not a kernel entry either — the kernel is a *client* of it.

| Slot | Target | Name (kernel.c) | In | Out | What it does | Callers | Caller example |
|---|---|---|---|---|---|---|---|
| `[0x10C]` | `0A84` | `svc_resource` | AL = mode 0-6, DS:SI = request block, ES:DI = dest; AH = record (mode 1) / sword block (mode 4); BX = entry-vector ptr (mode 0) | CF (modes 3/6: file missing while FF78≠0); DI/SI/DS/ES preserved | resource loader, see table below | 111 sites (GAME, all engine/shop overlays, gfmcga) | `GAME.BIN A00A: mov di,0xF500 / mov si,0xA21D / mov al,0x2 / call [cs:0x10C]` (font.grp) |
| `[0x10E]` | `0F17` | `svc_nop` | — | — | single `ret`; **placeholder, no caller anywhere** | 0 | — |
| `[0x110]` | `06AC` | `svc_hotkey_exit` | reads FF18 | — | if FF18 == 0x0014 (Ctrl+Q): "Exit to DOS / Sure?(Y/N)". Y → wait FF09≠0, `AX=0; jmp far [FF00]` (loader exit). N → restore screen, resume music, clear FF17/FF1D/FF1E | 11 | `fight.bin 7155: call [cs:0x110]` (first of the per-frame chain 7155..716E) |
| `[0x112]` | `0723` | `svc_hotkey_pause` | reads FF18 | — | if FF18 bit3 (Esc): FF75=2, save region 101E/0810→3C80, draw box + "PAUSE" (skipped when FF18 == 0x000E = Esc+Ctrl+Shift), `int60 AX=3 CL=FF` (music pause); loop: Esc+Ctrl+Shift hides the box; any FF1D/FF1E event → restore, clear events, `int60 AX=3 CL=0` | 10 | `fight.bin 715A: call [cs:0x112]` |
| `[0x114]` | `07B6` | `svc_hotkey_speed` | reads FF18, FF29 | FF33 | if FF18 bit15 (F9): "Speed change / Select 0-9:", waits F9 release, reads digit d via FF29 (`read_digit` 085E), draws it, **FF33 = 10 − d**, FF75=1, then waits for any key/direction/fire and restores | 5 | `fight.bin 715F: call [cs:0x114]` |
| `[0x116]` | `0881` | `svc_hotkey_joy_on` | reads FF18 | FF3B, 05C6/05C8 | if FF18 == 0x0104 (Ctrl+J): `joy_calibrate` (089E), FF17=0, wait for release | 8 | `fight.bin 7164: call [cs:0x116]` |
| `[0x118]` | `08EF` | `svc_hotkey_joy_off` | reads FF18 | FF3B | if FF18 == 0x0804 (Ctrl+K) and FF3B≠0: FF75=1, FF3B=0, wait for release | 8 | `fight.bin 7169: call [cs:0x118]` |
| `[0x11A]` | `0918` | `svc_random` | — | AX | `ax = FF1B; al += ah; adc ah,0; [092B] += ax; return [092B]` — cheap PRNG seeded by the tick counter | 11 (gfmcga, fight, kingpro, innapro, armrpro) | `kingpro.bin A328: call [cs:0x11A] / or al,al / jz A332`; `gfmcga.bin 31B5: call [cs:0x11A] / and al,3` |
| `[0x11C]` | `09D6` | `svc_find_files` | ES:DI = 0xAF6-byte buffer, DS:DX = ASCIIZ filespec | buffer: `+0 count`, `+1 u16 ptr[255]`, `+0x201 char name[255][9]` (name up to '.', max 8) | DOS FindFirst/FindNext (DTA at cs:0A59), ≤254 entries. *Quirk:* attribute CX is loaded from DX | 2 | `kenjpro.bin A433: mov ax,cs / mov es,ax / mov ds,ax / mov di,0xE000 / mov dx,0xA516 ("*.usr") / call [cs:0x11C]` |
| `[0x11E]` | `092D` | `svc_hotkey_restore` | reads FF18 | **CF=1 only on Y**; CF=0 if F7 not held or N | if FF18 == 0x4000 (F7): "Restore Game / Sure?(Y/N)" with music paused; restores screen either way | 3 | `fight.bin 716E: call [cs:0x11E] / jnc 7178 / call 78D7` (78D7 = fight's restore-from-save) |
| `[0x120]` | `089E` | `joy_calibrate` | reads FF0A, FF3B | FF3B=FF, 05C6/05C8 = centre, FF75=1 | if joystick configured and not yet enabled: wait for port 201h bit 3 to drop (bounded), time both axes, reject 0/0xFFFF, store centre | 1 | `GAME.BIN A022: call [cs:0x120]` (boot, after font load) |

Engine overlays poll the five hotkey services back to back once per frame
(`fight.bin 7155..716E`, `town.bin 68C3..68DC`, `opdemo 63CE`, `enddemo 6301`,
`select AA58`, `gfmcga 4457`); `town.bin 7044` and `enddemo 6958` use shorter subsets.
F1/F2 are *not* services — they are handled inside the timer tick (below).

### `[0x10C]` modes (dispatch `0A84`, sub-table `0ACA`)

Request block = `{u8 archive 0-2, u8 res# 1-based, char name[] (used only when res# == 0
→ open external file)}`.  The archive digit is patched into `cs:0D3B "zelres1.sar"` and
into the "Please insert DISKn" prompt (`0D47`, digit at `0D5E`).

| AL | Handler | Action | Example |
|---|---|---|---|
| 0 | `0C01` | **swap** 0x3800 words `BASE:3000..9FFF` ↔ `(BASE+0x2000):9000..FFFF`, then **`jmp [cs:BX]`**. No register save. GAME.BIN stashes `gf*.bin` (@9000) + `fight.bin` (@C000) there at boot (`A0D4`/`A0E4`), so this flips town-set ↔ fight-set without disk I/O | `town.bin 7038: mov bx,0x6002 / xor al,al / jmp [cs:0x10C]`; `fight.bin 7DBA` |
| 1 | `0AD6` | load **system record #AH** raw to `BASE:C000`. Records = 11-byte `{archive, res#, name[9]}` at `cs:0F68`: 0-30 = `MP10..MPA0.MDT` (ZELRES3[20..50]); 31 = `{1,0,"        "}` external file with a blank name (save slot, caller patches the name); AH bit 7 → index `(AH&0x7F)+32` = town maps `CMAP MRMP STMP BSMP HLMP TMMP DRMP LLMP PRMP ESMP.MDT` (ZELRES2[36..45]). Table ends at `1135` = end of file | `town.bin 7018: mov ah,al / mov al,0x1 / call [cs:0x10C]` |
| 2 | `0AFF` | load to `(BASE+0x3000):0000` and **decompress** to ES:DI. `byte0 == 0` → one stream; else `{u8, u16 lenA, u16 lenB}` + two streams, FF14 == 0 (EGA) takes A, otherwise B | `GAME.BIN A00A` (font.grp → `BASE:F500`) |
| 3 | `0C2F` | load **raw** to ES:DI (`payload_len` bytes; external files: read to EOF). CF=1 if missing and FF78≠0 | `GAME.BIN A0B4: mov si,0xA270 (town.bin) / mov di,0x6000 / mov al,0x3 / call [cs:0x10C]` |
| 4 | `0B6F` | **install sword block #AH**: `sword.grp` sits decompressed at `(BASE+0x2000):1800` (GAME.BIN `A14C`, 3 sub-resource pointers relocated +0x1800); table `0BA0` maps AH 0-3→ptr[0], 4-5→ptr[1], 6→ptr[2]; copies 0x800 words to `ARENA:B000` and adds 0xB000 to the first 15 words (`{data_off, ptr[14]}` header) | `fight.bin 8F9E: mov ah,[0x92] / mov al,0x4 / call [cs:0x10C]` (`[0x92]` = current sword) |
| 5 | `0BAE` | load **music score** `{u16 lenA, u16 lenB} + blobA + blobB` to ES:DI: FF15≠0 (MSCMT.DRV / MT-32) → A, else seek past A and read B | `fight.bin 7DAA: mov es,[cs:0xFF2C] / mov di,0x3000 / mov al,0x5 / call [cs:0x10C]` |
| 6 | `0C24` | **probe**: open, seek to entry, length → `cs:0F64`, close; CF as mode 3 | `kenjpro.bin A427: push cs / pop es / mov si,0xA907 ({0,0,"STDPLY.BIN"}) / mov al,0x6 / call [cs:0x10C]` |
| ≥7 | — | ignored (registers restored) | — |

Loader internals (`src/kernel.c`): `sar_open_entry 0C42` (open, seek `(res#-1)*4`, read
entry offset → `0D7A`, seek, read length → `0F64`), `read_payload 0D84`, `sar_close 0D93`,
`load_fail 0F52` (`DS:DX = request; jmp far [FF00]` with AX = DOS error → ZELIARD prints a
message and exits).  File-not-found: with `FF78 == 0` shows the insert-disk prompt (box
via `[0x2000]`, text via `[0x202A]` at BX=6C/CL=4A), waits for a key or FF1D, then
`INT 21h AH=0Dh` (disk reset) + `AH=10h` with the dummy FCB at `0D7E` and retries.

Decompressor `0D9D` + opcode handlers `0DCC/0DD1/0E13/0E34/0E73/0E9C/0EBA/0EF5`: as
specified in ARCHITECTURE.md (`tools/sardec.py`); the C keeps the asm structure
(DX = bytes left, `rep stosb` per token).

## INT 09h — keyboard handler (`02C5`, `kbd_process_scan 0326`, `kbd_scan_to_ascii 04D0`)

Reads port 60h.  `FF`/`FE` (overrun/ACK) → pulse port 61h bit 7, clear the four direction
groups, EOI, iret.  Otherwise: update state, **drain the BIOS key buffer** (INT 16h
AH=1/AH=0 loop) and chain to the old INT 9 (`[FF79]`) so the BIOS sees the key too.
Codes ≥ E0h are prefixes (flag `05C5`) and the following code is ignored for FF29.

Keys are tracked as *held state*: OR on make, XOR on break.

**Direction groups** (combined into `FF17` = `1 up · 2 down · 4 left · 8 right`):

| Group | Keys (scan) |
|---|---|
| `05C1` arrows | Right 4D / KP+ 4E → 8; Left 4B / `\` 2B → 4; Down 50 / KP- 4A → 2; Up 48 / `` ` `` 29 → 1 |
| `05C2` keypad diagonals | Home 47 → 5 (up+left); PgUp 49 → 0x90 (up+right); End 4F → 0x60 (down+left); PgDn 51 → 0xA (down+right). Low and high nibbles are both OR-ed into FF17 |
| `05C3` letters (only while `FF74 == 0`) | K 25 → 8; H 23 → 4; M 32 → 2; U 16 → 1 |
| `05C4` letter diagonals (only while `FF74 == 0`) | Y 15 → 5; I 17 → 0x90; N 31 → 0x60; O 33 → 0xA |

**Fire keys** `FF16`: bit0 = Space (39), bit1 = Alt (38).  While `FF74 ≠ 0` (text entry,
set by town.bin 783B / kenjpro A559 around name input) the letter groups are cleared and
only Space/Alt are checked.

**Hotkey word `FF18`** (scan → bit):

| Bit | Key | Bit | Key |
|---|---|---|---|
| 0x0001 | Enter 1C | 0x0100 | J 24 |
| 0x0002 | L-Shift 2A / R-Shift 36 | 0x0200 | E 12 |
| 0x0004 | Ctrl 1D | 0x0400 | R 13 |
| 0x0008 | Esc 01 | 0x0800 | K 25 |
| 0x0010 | Q 10 | 0x1000 | F1 3B |
| 0x0020 | Y 15 | 0x2000 | F2 3C |
| 0x0040 | N 31 | 0x4000 | F7 41 |
| 0x0080 | S 1F | 0x8000 | F9 43 |

Combos used: Ctrl+Q = 0x0014 exit, Ctrl+J = 0x0104 joystick on, Ctrl+K = 0x0804
joystick off, Esc+Ctrl+Shift = 0x000E hide pause box, Y/N = 0x20/0x40 in dialogs;
`select.bin A2CD` compares FF18 with 0x0286 (Ctrl+Shift+E+S) — an overlay-side hidden
key combo, not a kernel feature.

**ASCII `FF29`**: make codes 1..53h are translated through `0511` (plain) or `0569`
(Shift held) — digits, letters (upper case only), Backspace 08, Enter 0D, and on the
shifted table `!@$%(){}:`.  Consumers: `read_digit 085E` (speed dialog), town.bin
7935/7941 and kenjpro (name entry).  Note `085E` checks FF29 for Esc (0x1B) but neither
table can produce it — dead branch.

## INT 08h — timer handler (`0250`)

Every tick (236.7 Hz): `call far [FF10]` (sound driver tick, `(BASE+FF0):1100`), then
`call far [FF0C]` (music driver tick, `(BASE+FF0):0100`).  Every 5th tick (~47 Hz):

- `01E3` F1/F2: exact `FF18 == 0x1000` → `FF75=1`, `int60 AX=2, CL=[FF28]` (music
  on/off toggle; the driver stores its own off-flag at [0x28] = FF28); exact
  `FF18 == 0x2000` → `FF27 = ~FF27` (sound-effects off flag), `FF75=1`.  Each uses an
  "armed on release" latch (`02C2`/`02C3`) so one press = one toggle.
- `0122` Space/Alt edge → `FF1D`/`FF1E = 0xFF` (latches `02BE`/`02BF`).
- `017C` joystick buttons (port 201h bit4/bit5, active low) → the same `FF1D`/`FF1E`
  (latches `02C0`/`02C1`), only when `FF3B && FF0A`.

Then `FF1A++` (byte), `FF50++` (word), `FF1B++` (word), `02C4++`; if `[FF20] ≠ 0` (high
byte of the near pointer at `FF1F`) → `call [FF1F]` (user tick hook; ZELIARD zeroes it, no
overlay in `disasm/` installs one).  Every 13th tick it chains to the old INT 8
(`jmp far [FF04]`, 236.7/13 = 18.2 Hz) instead of sending EOI itself.

Frame pacing convention seen in the engines: `fight.bin 7125` waits until
`FF1A >= 2*FF33`, `7150` until `FF1A >= 4*FF33`, then zeroes FF1A (FF33 = 5 by default →
20 ticks ≈ 85 ms per frame; F9 digit 9 → FF33 = 1 = fastest).

## INT 24h — critical error (`0F18`)

`AL = low byte of DI` (DOS error code).  Code 2 (drive not ready): `02C4 = 0`, spin until
`02C4 >= 0xF0` (~1 s) and return **AL=1 retry**; anything else returns **AL=0 ignore**.
Together with the insert-disk prompt this is what makes floppy swapping work.

## INT 61h — input state (`05FD`)

`FF48 = FF49 = 0`; if `FF3B & FF0A` → `joy_read_state 0630` (axes vs. calibrated centre:
right `x ≥ cx+8`, left `x ≤ cx/2−8`, down `y ≥ cy+8`, up `y ≤ cy/2−8`; buttons
`~port201 >> 4 & 3`).  Returns **AL = FF17 | FF48** (directions), **AH = FF16 | FF49**
(fire: bit0 Space/A, bit1 Alt/B).  BX/CX/DX preserved.  Example:
`select.bin A0D2: int 61h / and al,3 / jnz A0D2` (wait for up/down release);
`bankpro.bin A2D8: int 61h / call A62C / test ah,1`.

## Joystick I/O

`joy_read_axes 05CA`: `out 201h`, wait (≤6 reads) for both A-axis bits to go high, then
count reads until each bit drops → SI = X, DI = Y (cli/sti around it).
`joy_calibrate 089E` (= `[0x120]`) samples the *current* stick position as the centre
(`05C6`/`05C8`) — the game expects the stick released when Ctrl+J / boot calibration
runs.  Presence test: waits for port 201h **bit 3** (joystick B Y-axis, sic) to clear;
65535 reads without success → no joystick.

## FF00-page variables used by the kernel

The page `BASE:FF00..FFFF` doubles as offsets `00..FF` of the music-driver segment
(`BASE+0xFF0`, drivers at `:0100` and `:1100`), which is why some "state" bytes are really
driver variables (`MSCSTD.asm [cs:0x28]`, `SNDSTD.asm [0x75]`, …).

| Addr | Size | R/W by kernel | Meaning (set by) |
|---|---|---|---|
| `FF00` | far ptr | jmp | loader exit/abort entry (ZELIARD `02D9`): AX=0 clean exit, AX=DOS error → message, DS:DX = request block |
| `FF04` | far ptr | jmp | original INT 8 (chained every 13 ticks) |
| `FF09` | byte | R | sound driver `[0x09]` "idle" flag; exit waits for ≠ 0 |
| `FF0A` | byte | R | joystick configured (`RESOURCE.CFG JOYSTICK:yes`). ARCHITECTURE.md calls FF15 the joystick flag — it is FF0A |
| `FF0C` | far ptr | call | music driver tick `(BASE+FF0):0100` |
| `FF10` | far ptr | call | sound driver tick `(BASE+FF0):1100` |
| `FF14` | byte | R | video mode index; 0 = EGA selects stream A in mode-2 containers |
| `FF15` | byte | R | `musicDrv == MSCMT.DRV` (MT-32) → mode 5 takes blob A |
| `FF16` | byte | W (INT 9) | fire keys held: bit0 Space, bit1 Alt |
| `FF17` | byte | W (INT 9), cleared by dialogs | direction keys held (1 up 2 down 4 left 8 right) |
| `FF18` | word | W (INT 9) | hotkey held-mask (table above) |
| `FF1A` | byte | ++ per tick | frame-pacing counter (overlays reset it) |
| `FF1B` | word | ++ per tick | tick/frame counter; seed for `svc_random` |
| `FF1D` | byte | W | Space / joystick-A **press event** (0xFF; consumer clears) |
| `FF1E` | byte | W | Alt / joystick-B press event |
| `FF1F` | near ptr | call if `[FF20]≠0` | optional per-tick hook (unused in shipped overlays) |
| `FF27` | byte | toggle (F2) | sound driver `[0x27]` = sfx off |
| `FF28` | byte | R (F1) | music driver `[0x28]` = music off; passed as CL to `int60 AX=2` |
| `FF29` | byte | W (INT 9), R | last typed ASCII char |
| `FF2C` | word | — | data-arena segment (used by callers, not the kernel; kernel computes `cs+0x1000` itself in mode 4) |
| `FF33` | byte | R/W (F9) | game speed, `10 − digit`; loader default 5 |
| `FF3B` | byte | R/W | joystick enabled/calibrated |
| `FF48` | byte | W (INT 61) | joystick directions |
| `FF49` | byte | W (INT 61) | joystick buttons |
| `FF50` | word | ++ per tick | second tick counter (shop overlays use it as a timeout, e.g. kingpro A30A) |
| `FF74` | byte | R (INT 9) | text-entry mode: letters are not directions |
| `FF75` | byte | W | sound driver `[0x75]` = sfx request (1 = confirm blip, 2 = dialog open) |
| `FF78` | byte | R | suppress insert-disk prompt; missing file returns CF (town.bin 75F2 sets it around save-file access) |
| `FF79` | far ptr | jmp | original INT 9 |

Kernel-private data outside the page: `02BC/02BD` tick dividers (5, 13), `02BE-02C3`
edge latches, `02C4` tick byte for INT 24, `05C1-05C4` direction groups, `05C5` E0
prefix, `05C6/05C8` joystick centre, `092B` PRNG state, `0A51-0A58` saved pointers +
`0A59` DTA for `svc_find_files`, `0D3B` archive name, `0D47` insert-disk string, `0D79`
resource #, `0D7A` entry offset, `0D7E` dummy FCB, `0F5C/0F60/0F64` request/dest/length,
`0F68..1135` system records.

Video-driver vectors the kernel itself calls (for the dialogs), with the register values
it passes — the exact meaning of the coordinate words is the video-driver author's call
and is *not* verified here: `[0x2000]` box fill (BX=1A46/201E, CX=1E28/1010, AL=FF),
`[0x2022]` draw char (AL = char, AH=1, BX=CC, CL=5A), `[0x2026]` save region (AX=0C46 or
101E, CX=1028 or 0810, DI=3C80 = save buffer), `[0x2028]` restore region (same args),
`[0x202A]` draw string (DS:SI = text, `\r` = newline, 0xFF terminator; BX, CL = position,
e.g. 74/52 for the Y/N dialogs, 8C/22 for PAUSE, 6C/4A for the insert-disk prompt).

## Video driver vectors (0x2000+)

See docs/VIDEO_DRIVERS.md.
