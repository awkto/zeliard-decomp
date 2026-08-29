# Music: the `.msd` score format and the MSC*.DRV driver API

Sprint 8 (issue #8). Sources: `disasm/MSC{STD,ADLIB,JR,MT}.asm` (ndisasm at origin
0x100), the score blobs in `extracted/ZELRES{1,2,3}/dec/`, and the callers in
`disasm/overlays/*.asm`, `disasm/STICK.asm`, `disasm/SND*.asm`. Readable C:
`src/music_std.c` (PC-speaker driver, the reference score interpreter) and
`src/music_adlib.c` (the OPL2-specific parts). Converter: `tools/msd2mid.py`.

All four drivers are one program built for four back-ends: identical INT 60h
dispatcher, identical FF-page variables, identical tick/tempo machinery, and (for
STD/JR/ADLIB) the same track byte-code with a handful of per-driver opcode
differences. MSCMT.DRV plays a different, MIDI-shaped blob (blob A) of the same
resource.

## 1. Loading and the INT 60h API

The loader copies `musicDrv` (RESOURCE.CFG) to `(BASE+FF0):0100` and points INT 60h at
`+0103`. STICK's INT 8 handler calls `far [FF0C]` = `+0100` (the tick entry) on every
236.7 Hz timer tick (docs/FIGHT.md §1). The driver's data page is the FF-page
(`(BASE+FF0):0000..00FF` = `BASE:FF00..FFFF`), which is why the game reads and writes
music state as `FF0B`, `FF24`, … directly.

Dispatcher (`0103`, all drivers): saves all registers, `ES=CS`, `if AX < 8: call
[CS:0127 + AX*2]`, restores, `iret`. Arguments: **CL** (and **DS:SI** for AX=0).

| AX | Name | Args | What it does | STD / ADLIB / JR / MT entry | Caller example |
|---|---|---|---|---|---|
| 0 | `music_start` | DS:SI = score (blob at `arena:3000`) | reset all channel state, parse the header, key everything off, start on the next tick (`FF26=0`, tempo 0x7F) | `020B` / `0247` / `022E` / `01F6` | `fight.bin 60D8: mov ds,[cs:FF2C] / mov si,3000 / xor ax,ax / int 60h` (right after the kernel mode-5 load, docs/SERVICES.md); also town 60A9, opdemo 6223, enddemo 66B1, rokademo A363 |
| 1 | `music_stop` | — | silence the chip, `FF26=FF` (stopped) | `0295` / `03C8` / `02C8` / `031E` | `fight.bin 6085: mov ax,1 / int 60h` (level entry), `ZELIARD.EXE 02E7` (exit to DOS), `select.bin A5AE` |
| 2 | `music_enable` | CL=0 off, CL≠0 on | CL=0: silence, `FF28=FF` (music off). CL≠0: `FF28=0` (ADLIB/JR re-send patches and levels). STD additionally toggles its 2-voice alternation mode on every re-enable (§4) | `0137` (all) | `STICK 0200: mov cl,[FF28] / mov ax,2 / int 60h` (F1 hotkey — CL is the current off-flag, so it toggles) |
| 3 | `music_pause` | CL≠0 pause, CL=0 resume | CL≠0: silence, `FF0B=FF` (tick returns at once); CL=0: `FF0B=0`, ADLIB/JR/MT re-send the current patch/volume/bend state | `015D` / `014E` / `014D` / `014E` | `STICK 06B9: mov cl,FF / mov ax,3 / int 60h` (Esc pause box), `town.bin 7592 / 7621` |
| 4 | `sfx_mute` | CL=FF while a PC-speaker effect plays, 0 when it ends | `094A=CL`, then calls the sound-driver hook `[CS:1102]`; while set, STD's output stage (`0845`) leaves ports 42h/61h alone | `0172` / `0165` / `0163` / `0165` | `SNDSTD.DRV 12E2: mov ax,4 / mov cl,FF / int 60h` (only the STD pair uses it) |
| 5 | `sfx_claim_channels` | CL bits 0-2 = SN76496 tone channels the sound driver owns, bits 3-5 = noise | JR: marks them muted for music (`[di+35]`), writes `9F/BF/DF` (attenuation 15) to each claimed channel; CL=0 gives them all back and re-sends the state | `0181` / `016C` / `016A` / `016C` | `SNDJR.DRV 161D: xor cl,cl / mov ax,5 / int 60h`, `16D6: mov cl,4 / mov ax,5` |
| 6 | `sfx_reset_opl` | CL=0 → release, CL≠0 → claim (bit0 → OPL ch 4, bit1 → ch 5, `[di+2B]`) | ADLIB, CL=0: key-off the claimed channels, then if playing re-send their patch/frequency; if stopped/paused/off: `BD=00, B0..B8=00` | `01B5` / `01A0` / `01D2` / `01A0` | `SNDADLIB.DRV 152C: mov cl,[20C3] / mov ax,6 / int 60h`, `1633: mov cl,3 / mov ax,6` |
| 7 | `set_volume` | CL = volume (6 bits, inverted) | `FF76 = ~CL`, calls the sound-driver hook with `AH=7, AL=CL&3F` | `01D1` / `020D` / `01EE` / `01BC` | no caller in the shipped binaries |
| ≥8 | — | — | ignored | | |

`[CS:1102]` is the sound (sfx) driver's secondary entry (`SND*.DRV` sits at `+1100` =
tick, `+1102` = pointer to its service routine), so functions 4-7 are music→sfx
cross-calls; every MSC build carries all of them regardless of back-end (STD even
contains the OPL and SN76496 mute sequences of 5/6).

### FF-page variables shared with the game

| Offset | Name | R/W by game | Meaning |
|---|---|---|---|
| `FF0B` | `pause` | via AX=3 | tick entry returns immediately while ≠0 |
| `FF21..FF23` | `sync[3]` | R (`enddemo 6759` tests `FF21` then clears it) | score-writable counters (opcodes F1/F2/F3, F4 on ADLIB): the score signals the game — the ending waits for the music to bump `FF21` before advancing a scene |
| `FF24` | `fade_rate` | **W** (`fight.bin 7120: mov byte [FF24],0A`; opdemo/select `08`; town `04`) | ≠0 starts a fade-out: every `FF24` driver ticks `FF25 += 4` (MT: master volume −2); on wrap the driver stops itself (`FF24=0, FF26=FF`) — `FF24=8` ≈ 4.3 s |
| `FF25` | `fade_level` | — | global attenuation added to every channel's level |
| `FF26` | `stopped` | R (`opdemo 62C7`, `rokademo A371`, `town 6FC1` spin on `test [FF26],FF`) | 0 = playing; FF = stopped (AX=1 / fade end); 3F = every track executed its `FF` end opcode |
| `FF28` | `music_off` | R (STICK F1 handler) | set by AX=2 CL=0 |
| `FF76` | `volume` | — | AX=7 |

Fade-out is therefore not an INT 60h function: the game pokes `FF24` and waits on `FF26`
(or a fixed time: `select.bin A598: mov [FF24],8; wait FF1A ≥ 78h; ax=1`).

### Timing

* INT 8 = 236.7 Hz (PIT divisor 0x13B1). The tick entry (`STD 02C3`, `ADLIB 03EA`,
  `JR 030F`, `MT 0341`) does work only every **2nd** call (`0947`/`0CC6`/`0B5A`/`07CF`
  reload 2) → **driver tick = 118.35 Hz**.
* Tempo: byte `T` (`0934`, default 0x7F, opcode `E0`) is added to an accumulator
  (`0935`) on every driver tick; the score advances only on the ticks where the add
  does **not** carry (`STD 036F jz`, `ADLIB 04AF jz`, `MT 0385 jnc`). Hence
  **score tick rate = 118.35 × (256 − T) / 256 Hz**: T=0x7F → 59.6/s, T=0x7D → 60.6/s,
  T=0x2D → 97.5/s. A tempo change in any track applies to every track at once.
  (Cross-check: mus5's AdLib arrangement has `E0 2D` and a 5664-tick loop, its MT-32
  arrangement `E0 69` = T 0x96 and a 2832-tick loop — both are 58.1 s.)
* Vibrato/envelope processing (`05A0`) runs on every *other* driver tick (`0938`).
* Durations are in score ticks; every score uses **24 ticks per quarter note**
  (duration tables are made of 0x60 whole, 0x30, 0x18, 0x0C, 0x06, 0x03, dotted
  0x90/0x48/0x24/0x12, triplets 0x10/0x08/0x04). So µs per quarter =
  24·10⁶·256 / (118.34775·(256−T)) = **51,914,802 / (256 − T)** (the driver tick is
  1,193,181.67/0x13B1 = 236.6955 Hz, halved); T=0x7F → 402 ms →
  149 BPM, T=0x7D → 396 ms → 151 BPM.

## 2. Resource layout (kernel mode 5)

Resource = `{u16 lenA, u16 lenB} blobA blobB`. Mode 5 hands **blob A** to MSCMT.DRV
(`FF15 ≠ 0`) and **blob B** to the other three, at `arena:3000`; AX=0 gets DS:SI =
that address and every offset inside a blob is relative to its first byte.

### Blob B header (STD / JR / ADLIB)

```
+00  u8   04               constant (format id)
+01  u16  adlib_track[6]   OPL2 melodic channels 0-5      (read by ADLIB)
+0D  u16  jr_track[3]      SN76496 channels 0-2           (read by JR; STD reads [0] and [1])
+13  u16  rhythm_track     OPL2 rhythm-mode track          (ADLIB)
+15  u16  opl_instruments  15-byte OPL patch records (§3.4) (ADLIB)
+17  u16  instruments      17-byte envelope records (§3.3)  (STD, JR)
+19  u16  dur_tables       8-byte duration tables, selected by F0 (all)
+1B  u16  length           = lenB (not read by any driver)
+1D  6    0C B0 01 05 01 01  constant, not read by any driver
+23  ...  track data (adlib_track[0] always starts here)
```

So every score is arranged three times inside blob B — 6+1 tracks for AdLib and 3
tracks for Tandy (the PC speaker plays the Tandy arrangement) — plus the MT-32
arrangement in blob A. The arrangements are different data (different note counts,
voicings, sometimes different octave numbers) but share the tempo map and loop
structure: `msd2mid.py` reports identical loop-body lengths in seconds for all three.

## 3. Track byte-code (blob B)

Each track is an independent stream interpreted by one channel struct (STD: `08D6`,
`0905`, 0x2F bytes; ADLIB: `0B99 + 0x2C·ch`, rhythm `0CA1`; JR: `0AB6 + 0x36·ch`).
On every score tick the channel decrements *remaining*; when it reaches 0 the
interpreter reads bytes until it consumes a **note byte** (< 0x80), which sets the
next duration. Bytes ≥ 0x80 are commands executed in a loop (`STD 0391` is pushed as
the return address; `FF` pops it).

### 3.1 Note byte `0x00..0x7F` = `(dur_index << 4) | pitch`

* `dur = dur_table[dur_index]`, index 0-7 into the current 8-byte table.
* `pitch` 1..12 = C, C#, D … B in the current octave (STD table `08B2`: PIT divisors
  2280 … 1207; ADLIB `0B51`: f-numbers 0x156 … 0x287; JR `0A7B`: 1709 … 905).
* `pitch = 0` → **rest**: release the sounding note now, wait `dur`.
* `pitch = 0xF` → **hold**: keep the sounding note (no retrigger, no release), only a
  new duration — ties across the 8-entry duration limit.
* Otherwise compute the frequency; unless the *legato* flag (bit5 of `[di+0F]`, set by
  opcode `E7`) is pending, key-on (restart envelope and vibrato). With the flag only the
  frequency changes (slur).
* The byte **after** the note is peeked: if it is `E7` the note is not released before
  its end (bit4).
* Release timing: each tick after the decrement, `if gate >= remaining` (and no bit4)
  → release. `gate` (`[di+10]` STD, `[di+12]` ADLIB) defaults to 1, i.e. the last tick
  of every note is its release; opcodes `D8-DF` set it from the duration table
  (staccato).

Pitch → MIDI note (from the chip formulas): **ADLIB** block b → `12·(b+1) + pitch − 1`
(block 4, pitch 1 = f-number 0x156 = 259 Hz ≈ C4); **JR** octave o →
`12·(max(o,1)+2) + pitch − 1` (SN divisor `1709 >> (o−1)`, 3.58 MHz/32/427 = 262 Hz = C4
at o=3); **STD** octave o → `12·(o+3) + pitch − 1` (PIT divisor `2280 >> o << 3`,
1.193 MHz/2280 = 523 Hz = C5 at o=3) — the speaker plays the Tandy tracks **one octave
above** the Tandy chip. (A finding, not a typo: `0548`/`0878` vs `0613`/`0A60`.)

### 3.2 Command bytes

| Byte | Args | STD (`03C6` table) | ADLIB (`0506`) | JR (`0412`) |
|---|---|---|---|---|
| `80-BF` | — | instrument `n = b & 3F` (17-byte record `instruments + 17n`; loads the vibrato bytes) | OPL patch `n` (15-byte `opl_instruments + 15n`, programs both operators) | as STD, plus noise/flag bytes 14/15 |
| `C0-CF` | — | relative volume, nibble s signed −8..7: s ≥ 0 → attenuation −= 4(s+1) (louder), s < 0 → attenuation += 4·\|s\| ; clamp 0..7F | same, clamp 0..3F | same as STD |
| `D0-D7` | — | octave = b & 7 | block = b & 7 | octave = b & 7 |
| `D8-DF` | — | gate = dur_table[b & 7] | same | same |
| `E0` | u8 T | tempo = T | same | same |
| `E1` | i8 | detune (added to the divisor / f-number before the octave shift) | same | detune = i8 >> 3 |
| `E2` | u8 d [+5] | vibrato: d=0 off; else delay d and 5 parameter bytes | same | same |
| `E3` / `E4` | — | octave −1 / +1 | block −1 / +1 | octave −1 / +1 |
| `E5` | u8 | attenuation = u8 (0 loudest, 7F silent) | same (`[di+0F]`, `>>1` is added to the carrier level); on the rhythm channel: sets all drum levels | same |
| `E6` | — | nop | nop | nop |
| `E7` | — | legato: the next note does not retrigger (and the note before it is not released) | same | same |
| `E8` | — | nop | nop | nop |
| `E9` | u8 | skip | nop (**no argument**) | SN noise control: `E0 \| (u8/11 + 4)` |
| `EA` | u8 | skip | nop | channel noise/tone flags (`[di+33]`, mute mask `0B68`) |
| `EB` | u8 | skip | nop | nop (no argument) |
| `EC` | u8 [+u8] | skip 1, or 2 if the first ≠ 0 | nop | nop |
| `ED-EF` | — | nop | nop | nop |
| `F0` | u8 n | duration table = `dur_tables + 8n` | same | same |
| `F1` / `F2` | u8 i | `FF21+i` ++ / −− (shared by all tracks and visible to the game) | same | same |
| `F3` | u8 i, u8 v | `FF21+i = v` | same | same |
| `F4` | u8 i, u8 v, u16 a | nop | if `FF21+i == v` jump to `a` | nop |
| `F5` | u8 | loop counter `[b & 3] = b >> 2` | same | same |
| `F6` | u16 a | `c = a >> 14`: `--counter[c]`; if ≠ 0 jump to `a & 3FFF` (repeat) | same | same |
| `F7` | u16 a | as F6 but jump when the counter **reaches** 0 | same | same |
| `F8` | u8 v, u16 a | if `counter[a>>14] == v` jump to `a & 3FFF` | same | same |
| `F9` / `FA` | u8 i | counter[i] ++ / −− | same | same |
| `FB` | u16 a | jump (absolute offset in the blob) — **the song loop** | same | same |
| `FC` | u16 a | call: save the return address and the current duration table | same | same |
| `FD` | u8 v, u16 a | call if `counter[a>>14] == v` | same | same |
| `FE` | — | return (restores the duration table) | same | same |
| `FF` | — | end of track; when all tracks of the build have ended (STD 2, JR 3, ADLIB 7, MT 9): `FF26 = 3F`, chip silenced | same | same |

Jump targets, `dur_tables + 8n` and patch offsets are all relative to the blob start
(`093A` in STD). The E9/EA/EB/EC mismatch is harmless because each driver only reads
its own arrangement; the STD build must skip the JR arguments because it plays the JR
tracks. (JR's `EB` takes no argument while STD skips one — no JR track uses `EB`.)

### 3.3 STD/JR instrument record (17 bytes, `instruments + 17n`)

```
+0     vibrato delay (0 = no vibrato)        +1,+2  depth multipliers up/down (× vib_scale[pitch])
+3,+4  half-period lengths up/down           +5     bit7 start downwards, bits0-4 step period
+6     env phase-1 step (bit0=1 → down, value&FE = amount)    +7   phase-1 length
+8     env phase-2 step                                       +9   phase-2 length
+10    release step                                           +11  release length
+12    hi nibble: release drop, lo nibble: envelope rate (ticks per step)
+13    hi nibble: phase-2 drop, lo nibble: initial level (level = nibble << 4)
+14    JR flags → [di+33] (bit7 = noise on key-on, bits0-2 …)   +15 JR noise value   +16 pad
```

The envelope (`0677`) only matters as a threshold on the speaker: the note is audible
while `(env_level >> 4) × ((~(FF25 + attenuation) >> 3) & 0xF) ≥ 0x88` (`06EC`, `0845`).
JR turns the same product into the SN attenuation nibble (`083D`: `(product + 0xF −
FF25) >> 4 ^ 0xF`).

### 3.4 OPL2 patch record (15 bytes, `opl_instruments + 15n`) — see `src/music_adlib.c`

```
+0..+5  vibrato as above (software vibrato on the f-number)
+6  mod 20h   +7  car 23h     (AM/VIB/EG/KSR/MULT)
+8  mod 40h   +9  car 43h     (KSL/level; level is re-computed with the channel volume)
+10 mod 60h   +11 car 63h     (AR/DR)
+12 mod 80h   +13 car 83h     (SL/RR)
+14 bits7-6 mod waveform, bits5-4 car waveform, bits3-1 feedback, bit0 connection (1 = additive)
```

Carrier level written = `(car43 & 3F) + (attenuation >> 1) + (FF25 >> 2)`, clamped 3F
(`0630`); the modulator level is scaled the same way only for additive patches.
Frequency: `A0/B0 = (fnum[pitch] + detune + vibrato) | block << 10 | key-on << 13`.
Patch load (`05AF`) first writes `80/83 = FF` and `40/43 = FF` (fast release, mute) so
a patch change mid-note does not click. Every score has its own patch bank (7–9
patches); there is no global instrument set.

### 3.5 Rhythm track (ADLIB only, `08A9`)

The 7th track drives OPL rhythm mode (`BD` register; channels 6-8 are pre-loaded from
the driver's own 27-byte percussion table at `0B7E`, f-numbers `A6/B6 = 0x120 blk 1`,
`A7/B7 = 0x150 blk 1`, `A8/B8 = 0x3C0 blk 0`).

| Byte | Meaning |
|---|---|
| `00-3F` | hit: bits 0-4 = HH, CY, TT, SD, BD (key-off then key-on of those bits in `BD`); bit5 → an explicit duration byte follows, else duration = default (`[di+14]`) |
| `80-9F` | key-off the drums in bits 0-4 |
| `A0-A7` | default duration = dur_table[b & 7] |
| `C8-CC` | drum (b−C8: BD, HH, SD, TT, CY) level += i8, clamp 0..3F |
| `CD-D1` | drum (b−CD) level = u8 × 4 |
| `D2-FF` | low nibble indexes the common F0-FF table (F0 dur table, F1-F4 counters, F5-FE flow, FF end) |

Drum levels go to `53h` (BD), `51h` (HH), `54h` (SD), `52h` (TT), `55h` (CY) plus
`FF25 >> 2` (`0988`).

### 3.x The AdLib volume bug (`C0-CF`, MSCADLIB `06B3`)

`MSCADLIB.DRV`'s relative-volume handler tests `test bl,0xC0` (`disasm/MSCADLIB.asm:562`
and again at `06BE`), so **any accumulated attenuation with bit 6 or 7 set snaps to 0 =
fully loud**.  `set_level` uses `attenuation >> 1` as a 6-bit OPL level, so the intended
range is `0..0x7F`; this is a bug in the shipped driver.

It bites exactly one score: **zopn** opens with `E5 4F` and then fades in with `C0`, so on
real AdLib hardware the first `C0` jumps straight to full volume instead of stepping up.
`tools/msd2mid.py` models the *intended* 0..0x7F range (its MIDI output is the nicer one);
`port/msd.c` reproduces the driver by default and has a `compat_msd2mid` flag so the two
event streams can be diffed.  All 51 score × arrangement streams match with that flag set.

## 4a. Sound-effect drivers (`SND{ADLIB,STD,JR}.DRV`)

Same program as `MSC*` with fewer channels: SNDADLIB owns OPL channels 4/5, SNDSTD the
speaker.  Loaded at `(BASE+FF0):1100`; the kernel's INT 8 ticks it on **every** tick
(236.7 Hz) — there is no ÷2 prescaler, unlike the music driver.

| item | SNDADLIB | SNDSTD |
|---|---|---|
| effect table | `1743`, 7-byte records `{u8 priority, u16 trackA, u16 trackB, u16 dur_tables}` | `1502`, 5-byte records |
| effects | 65 | 65 |
| empty-track stub | `201F` | — |
| OPL patches | `2020`, 9 bytes `{20,23,40,43,60,63,80,83, ws/fb/conn}` | — |
| f-numbers / vib scale / op offsets | `1576` / `158E` / `159A` | pitch table `1430` |

Opcode subset: `E0-E5, E7, F0, FF` (`E9/EA/EB` take an argument only in the STD build).
A request whose priority is ≥ the playing effect's preempts it; when both tracks end the
driver releases its channels with `INT 60h AX=6, CL=0`, which is why the top two music
voices drop out for the length of a sword swing.  `SNDADLIB 15A3` is a proximity/ambient
routine driven by `FF08` (written by fight.bin `774E`).

Pitch: the game's f-number table sits a uniform ≈14 cents flat of A=440 (so
`0x156` = 259 Hz reads as C4).

## 4. Per-driver differences

* **STD (PC speaker, `MSCSTD.DRV`)** — plays `jr_track[0]` on PIT channel 2 (mode 3
  square wave, divisor `((table[pitch] + detune) >> octave) << 3`). Only one voice can
  sound: the output stage `0845` takes channel 0 unless it is inaudible (envelope
  threshold), in which case it takes channel 1. Every *second* F1 re-enable (`0948`
  parity) switches on an alternation mode that swaps the two channels every driver
  tick — a hidden 2-voice "arpeggio" mode. Volume exists only as the audibility
  threshold. AX=4 (`sfx_mute`) hands the speaker to SNDSTD while an effect plays.
* **JR (Tandy/PCjr SN76496 at port C0h, `MSCJR.DRV`)** — 3 tone channels ←
  `jr_track[0..2]`, attenuation nibble from the envelope product; patches with byte 14
  bit7 use the noise generator (`E9` sets the noise control, `EA` the per-channel
  noise/tone mix, `0B68` = mute mask). Channels claimed by SNDJR (AX=5) are skipped.
* **ADLIB (OPL2 at 388h, `MSCADLIB.DRV`)** — 6 melodic channels ← `adlib_track[0..5]`,
  rhythm mode on channels 6-8 ← `rhythm_track`; `01=20` (waveform select) and `BD=20`
  at start. Register writes go through `0B11` (10 reads after the index, 35 after the
  data). Channels 4/5 can be claimed by SNDADLIB (AX=6, `[di+2B]`) and are then
  skipped by `0B00`; `sfx_reset_opl` re-sends their patch when they come back.
* **MT (Roland MT-32 via MPU-401 330h/331h, `MSCMT.DRV`)** — plays **blob A** (§5) on
  nine parts. Init: CC10=40h, CC11=7Fh and pitch-bend centre on every part, then four
  SysEx blocks — master volume `10 00 16` (= 100, `06A5`, decremented by 2 per fade
  step), part↔channel map `10 00 0D` = 1..9, `10 00 00` = master tune 4A / reverb mode 0
  time 1 level 7, partial reserve `10 00 04` = 3,10,6,4,3,0,0,0,6. Timbres are uploaded
  once by MTINIT.COM at boot (docs/ARCHITECTURE.md). Stop/pause = CC 7B (all notes off)
  on channels 2-9.

## 5. Blob A (MT-32) format

```
+00 u16 note_tables   16-byte tables of MIDI note numbers (0x80 = rest, 0xFF = pitch-bend follows)
+02 u16 dur_tables    16-byte tables: 8 pairs {duration, gate}
+04 9 × {u8 midi_channel (0-based; 1-6 = parts 1-6 on MIDI ch 2-7, 8, 9 = rhythm ch 10), u16 track}
+1F track data (unused parts point at 5-byte stub tracks)
```

Track opcodes (`MSCMT 042F`): note byte `< 0x80` = `(dur_index<<4)|note_index`, with
`note = note_table[index]`, `{dur, gate} = dur_table[2·dur_index]`; note-off is sent
when the gate counter expires (`0553`); a note equal to the sounding one is a tie;
`dur = 0` means "chord: fetch the next event in the same tick". `80-BF` pan
(`CC10 = (b&3F)·2`), `E0` tempo (**stored inverted**: `T = ~byte`; zopn has `E0 82` in
blob A and `E0 7D` in blob B — same tempo), `E1` pitch bend (LSB, MSB), `E2` velocity,
`E5` CC7 volume, `E6` program change (MT-32 patch 0-127), `E7` MIDI channel = byte−1,
`E9` note table, `F0` duration table, `F1` counter++, `F5` set loop counter (idx, value),
`F6` loop (idx, u16), `FB` jump, `FF` end; everything else is a nop. Tempo semantics and
the 118.35 Hz driver tick are identical to blob B; the MT arrangement runs note-lengths
scaled so that both blobs play in the same real time.

## 6. The scores

`tools/msd2mid.py --all OUTDIR` converts all 17 (names per docs/RESOURCES.md) into
`name.mid` (AdLib arrangement, 6 melodic tracks + rhythm on MIDI channel 10),
`name.mt32.mid` (blob A, MT-32 patches mapped to GM) and `name.tandy.mid` (the 3 Tandy
tracks). Length = intro + one loop body; `--loops N` repeats the body. Where the music
is used: `fight.bin` level record bits 1-4 index `{mgt1, ugm1, mgt2, ugm2, mus1..mus8,
mbos, mmao}` (docs/ARCHITECTURE.md); zopn = opening demo/title, zend = ending, mfan =
the jingle loaded by ZELRES3[0]'s stub.

| Score | Resource | Size | Tempo bytes | AdLib tracks used / notes | Length (intro+body) | Loop | Range (AdLib) | MT-32: notes / patches |
|---|---|---|---|---|---|---|---|---|
| zopn | ZELRES1[39] | 5648 | 0x7D, 0x7F | 7 / 3688 | 120.5 s | none (ends) | C2..B7 | 3712 / Elec Org4 Celesta2 SynBass1 StrSect3 Trumpet2 |
| zend | ZELRES1[38] | 7670 | 0x7F | 7 / 3680 | 148.2 s | none (ends) | C1..E6 | 3642 / Elec Org4 Violin1 Piccolo1 BrsSect2 |
| mgt1 | ZELRES2[46] | 3561 | 0x7F, 0x82 | 7 / 1713 | 83.3 s | from 36.4 s, body 47.0 s | E2..E6 | 1128 / SynBass1 StrSect1 Trumpet2 |
| mgt2 | ZELRES2[47] | 3288 | 0x7D, 0x7F | 7 / 3859 | 97.8 s | from 13.8 s, body 84.0 s | B1..A#6 | 2581 / Trumpet2 Sho |
| ugm1 | ZELRES2[48] | 4038 | 0x7F, 0x87 | 6 / 3161 | 178.6 s | from 89.3 s, body 89.2 s | F2..C6 | 1261 / Cello2 Recorder Vibe1 |
| ugm2 | ZELRES2[49] | 1780 | 0x7F, 0x9B | 6 / 924 | 70.1 s | from 4.3 s, body 65.8 s | E3..C6 | 612 / AcouPiano2 Elec Org4 StrSect2 Sho |
| mus1 | ZELRES3[85] | 3619 | 0x73, 0x7F | 7 / 3138 | 82.5 s | from 35.4 s, body 47.1 s | E2..D6 | 1374 / Guitar1 FrHorn1 Sho |
| mus2 | ZELRES3[86] | 4043 | 0x7F, 0x82 | 7 / 3235 | 80.8 s | from 8.3 s, body 72.5 s | D2..G6 | 2244 / Elec Org4 StrSect2 Trumpet2 Sho |
| mus3 | ZELRES3[87] | 3388 | 0x7F, 0x91 | 7 / 2475 | 67.4 s | from 15.0 s, body 52.4 s | G1..C6 | 1913 / ElecBass1 Trumpet2 Marimba |
| mus4 | ZELRES3[88] | 4041 | 0x7F, 0x8C | 7 / 2036 | 71.7 s | from 7.3 s, body 64.4 s | E2..E6 | 1722 / Trumpet2 Vibe1 Telephone |
| mus5 | ZELRES3[89] | 2335 | 0x2D, 0x7F | 7 / 2885 | 58.1 s | from 0.0 s, body 58.1 s | D2..A#6 | 2026 / Clavi3 SynBass1 Trumpet1 |
| mus6 | ZELRES3[90] | 5557 | 0x7D, 0x7F | 7 / 1787 | 63.6 s | from 3.4 s, body 60.2 s | D2..C#6 | 1533 / SynBass4 Trumpet1 |
| mus7 | ZELRES3[91] | 4197 | 0x7F, 0x91 | 7 / 4047 | 101.0 s | from 39.3 s, body 61.7 s | D2..D#6 | 1599 / Trumpet2 Sho |
| mus8 | ZELRES3[92] | 3638 | 0x7F, 0x82 | 7 / 3549 | 127.7 s | from 50.7 s, body 77.0 s | A1..F6 | 1681 / SynBass1 Trumpet2 |
| mbos | ZELRES3[93] | 2496 | 0x7F, 0x87 | 7 / 3022 | 52.8 s | from 6.5 s, body 46.3 s | G#2..A#5 | 1914 / Elec Org1 Sho |
| mfan | ZELRES3[94] | 527 | 0x7D, 0x7F | 6 / 178 | 12.8 s | none (ends) | C3..F#5 | 125 / BrsSect1 BrsSect2 |
| mmao | ZELRES3[95] | 3513 | 0x7F, 0x87 | 7 / 2837 | 65.2 s | from 13.7 s, body 51.5 s | D2..D#6 | 1852 / Violin1 BrsSect1 Sho |

"Tempo bytes" lists every `E0` value seen (0x7F is the driver default; most scores set
their real tempo with the first command). "Loop from" is where the machine state first
repeats — for scores whose intro uses loop counters (ugm1, mus8, mgt1) that is after
the first full pass, so the MIDI contains intro + body once. The MT-32 patch names are
the standard MT-32 preset names for the `E6` numbers in blob A ("Sho" on the rhythm
part is the drum-kit slot).

### Converter notes (`tools/msd2mid.py`)

* Type-1 MIDI, PPQ 24, one track per score channel plus a control track with the
  tempo map and `loop start` / `loop end` markers.
* AdLib volumes: channel attenuation → CC7 via the GM curve (0.75 dB per OPL level
  unit); note-on velocity from the patch's carrier level; the fade-out is not rendered.
* OPL patches have no names, so GM programs are chosen heuristically (sustained →
  square/saw lead or synth bass by register, percussive → electric piano or bass).
  Drums: BD 36, SD 38, TT 45, CY 49, HH 42.
* Vibrato, detune and the software envelopes are not rendered.
* `--dump` prints the decoded event stream; `--jr` / `--std` use the Tandy tracks with
  the JR or PC-speaker octave mapping.
* Validation: all 51 files parse (chunk lengths, balanced note on/off); zopn = 7
  tracks, 3688 notes, 120.5 s, C2..B7 in every arrangement (7297 ticks in AdLib, MT-32
  and Tandy). No renderer (fluidsynth/timidity) is installed on this machine, so the
  audio itself was not checked.

## 7. Open points

* The pitch mappings are derived from the chip formulas, not measured; the
  STD-vs-JR octave difference (§3.1) should be confirmed in DOSBox with the two
  drivers.
* JR tracks use octave 7 with noise patches for hi-hat-like sounds (mus1 track 0
  reaches "D8"); the Tandy MIDI renders them as tones.
* AX=7 / `FF76` has no caller; it is presumably the MT-32 master-volume path of the
  common driver code base.
* Blob B bytes +1B..+22 are never read; +1D..+22 (`0C B0 01 05 01 01`) is identical in
  all 17 scores and probably a compiler signature/version.
* The GM program choice for OPL patches and the MT-32→GM table are approximations.
