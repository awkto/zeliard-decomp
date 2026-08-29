# Enemies and bosses — the AI overlays (Sprint 7, issue #21)

Source of truth: `src/ai/ai_common.h` (ABI, verified against fight.bin and gfmcga),
`src/ai/eai1.c … eai8.c` (cavern enemies) and `src/ai/boss_*.c` (11 bosses).  Every
number below was read from the overlay images in `extracted/ZELRES3/NNN_data.bin`
(`/tmp/claude-1000/ai/hdr.py` dumps the header tables) or from the ndisasm listings.

Overlay ↔ map: level-record byte +3 indexes fight.bin's 11-byte request table `9CBC`
(`docs/ARCHITECTURE.md`): 0 EAI1, 1 CRAB, 2 EAI2, 3 TAKO, 4 EAI3, 5 TORI, 6 EAI4, 7 ZELA,
8 EAI5, 9 MEDA, 10 EAI6, 11 LEGA, 12 EAI7, 13 DRGN, 14 EAI8, 15 AKMA, 16 MAO1, 17 MAO2,
18 ZEL2.  Boss rooms (from `mdt2png --info`): mp1d→1, mp2d→3, mp3d→5, mp4d→7, mp5d→9,
mp6d→11, **mp73→18 (ZEL2, mid-cavern 7)**, mp7d→13, mp8d→15, mp90→16 (MAO1), mpa0→17 (MAO2).

## 1. AI overlay ABI

Raw image loaded to `BASE:A000` (fight.bin `7EBB`), CS = DS = BASE.

| Address | Content |
|---|---|
| `[A000]` | entry.  eai*: called by `8DF7` for every live enemy inside the ring with **SI = record, DI = ring cell of its previous position (marker already removed), `[FF4A]` = index**; returns with `ret`.  Boss maps (`FF34` or `[E6]`): called **once per frame** (`8D1D`) with no registers set up, and once right after loading (`7C27`) |
| `[A002]` | boss info (0 in eai*): +0 u16 start col, +2 u8 start row, **+3 u16 HP** (also the initial HP-bar value handed to video `[200A]/[200C]` in BX), +5 u16 EXP, +7 u8 hero screen column (camera), +8 u8 knockback-always-left (→ `9F01`), +9 u16 → name record `{u8 x, u16 y, u8 len, chars}` for video `[2010]`, **+B u16 gold**, +D… private.  FIGHT.md §7 puts "video init" at +3 and gold at +9: both wrong (verified at `6150/6162/71E8/71F1`) |
| `[A004]` | unused (0) |
| `[A006]` | → u16[8] per class → u8[4] drop ids, index `KRN_RANDOM()&3` (0 for a down-thrust kill).  ids: 0 corpse/nothing, 1 vanish, 4 coin 1 G, 5 coin 10 G, 9 full potion, 0xB the 100 G/shoes item class (`fight.bin 90E6`: object becomes item `0x70|id`) |
| `A008` | u8[8] EXP per class (`96C1`) |
| `A010` | u8[16] contact damage per `type & 0xF`, summed by `7675` per frame of overlap |
| `A030` / `A070` | u16[32] frame-list pointers, facing left / right (`hit` bit 7).  Index `type & 0x1F`: 0-7 classes, 8-F dying, 0x10-0x1F items.  Frame = 5 bytes `{palette 0-4, TL, TR, BL, BR}`; drawn entry = `phase & 0xF`; palette +3 while `hit & 0x20` (hit flash, not in boss maps) |

Enemy record (16 bytes, `[C010]` table): see `struct enemy` in `ai_common.h` — `col`,
`row`, `rcol` (recomputed each frame; hero body column = ring col **0x11**, his top-left =
0x10), `type` (class | 0x08 dying | 0x10 item | 0x20 sword-immune | 0x40 harmless | 0x80
solid), `hit` (source 0-4, 0x20 stun/hit-this-frame, 0x40 pending, 0x80 facing right),
`phase`, `flags`, `hp` (0 at spawn — **every AI writes the initial HP on its first update**),
`next`, `link`, `home_col/row/type`, `timer`.

fight.bin services (`call [cs:6000+2n]`, SI = record, CF=1 = blocked):

| n | addr | contract |
|---|---|---|
| 2 / 3 | `9723` / `973F` | step / probe by `AL & 7` (0 R, 1 RU, 2 U, 3 LU, 4 L, 5 LD, 6 D, 7 RD) |
| 4-11 | `91E5…926C` | step R, RU, U, LU, L, LD, D, RD: probe then move; ring-edge refusals (R needs rcol < 0x22, L needs rcol ≥ 2, U/D need rcol ∉ {0, 0x23}); col wraps at the map width, row & 0x3F |
| 12-19 | `92B4…949A` | probes for a 2×2 sprite (cell lists in ai_common.h); cavern 5 adds current-tile refusals |
| 20 | `6D6E` | `AL = row, AH = col → DI = E000 + (row&63)*0x24 + col` |
| 21 / 22 | `6D82` / `6D8E` | ring wrap of SI down / up by 0x900 |
| 23 | `94E1` | `AL = cell → ZF=1 passable` (cell < 0x49 iff in the arena:8000 list; 0x49..0x7F passable; ≥ 0x80 sprite = blocked).  FIGHT.md §3/§7 has the sense reversed |
| 24 | `97A0` | ZF=1 if the cell under (row+2, rcol) is a hazard tile |
| 25 | `96D5` | enemy_killed: phase 0, `type |= 0x68`, sound 7 if within 19 rows; **no EXP, no drop** |
| 26 | `97B5` | take_damage: `hp -= damage_for_source(hit&0x1F)`, sound 6; at 0: drop roll, `exp += A008[class]`, then vec 25.  Called by every AI when it sees `hit & 0x20` |
| 27 | `96A1` | `AX = map col → BL = ring col`, CF=1 if off the ring (bosses use it per part) |
| 28 | `9851` | `AL = source → AH = damage` (FIGHT.md §6 table) |
| 29 | `8611` | spawn projectile from the 13-byte template at BX (max 31) |
| 30 | `83DB` | clear all projectiles |
| 31 | `98C5` | find a spare object (DI, DL = index) |
| 32 | `975B` | ride current: pops the return address and steps twice when on an updraft/current tile |
| kernel `[11A]` | | `KRN_RANDOM` → AX |

Boss overlays additionally call video `[200C]` (BX = HP → bar), `[2000]/[202A]` (MAO1 text)
and read/write `FF2E boss_cutscene`, `FF2F boss_dying`, `FF30 boss_defeated`, `ED20[n]`
(cell under marker n), `[0x80]` scroll_col, `[0x83]` hero_scr_col, `[C002]` map width.

**Boss protocol** (all 11): the overlay rebuilds the whole `C010` list every frame from a
part buffer (each part = one 16-byte record with `type` = part class, `phase` = frame, a
ring marker `0x80|i`); before rebuilding it reads back the pending-hit bits (`hit & 0x40`)
of last frame's parts, takes the first one (weak-point parts flag it `|0x80`), scales
`damage_for_source()` by its own rule, subtracts from the HP word at `[A002]+3`, updates
the bar, and at 0 sets `boss_cutscene`; a 40-frame death (`boss_dying`) ends with
`boss_defeated`, after which fight.bin (`71DA`, needs `EDA0 == FF`) awards EXP and gold.

## 2. Cavern enemies

Common prologue in every class: (cavern 1 only) killed outright on a hazard tile (vec 24 →
vec 25, no EXP); `hp` initialised on the first update; a landed hit (`hit & 0x20`) goes to
vec 26 instead of moving.  "Tall" enemies are two records (class n upper, n+1 lower, the
lower's update is `ret`; the upper copies `phase`/facing into it).  Shots are fight.bin
projectiles: `{cell, life (cells), dir, damage}`, 1 cell/frame.

### Cavern 1 — EAI1 (ZELRES3[1]), sprites ENP1.GRP (ZELRES3[56]), maps mp10 (14/15/16/5 objects of classes 0..3)

| class | name (sprite) | frames L/R | HP | contact | EXP | drops (rand&3) | behaviour |
|---|---|---|---|---|---|---|---|
| 0 | bat (*not* the salmon/red ceiling creature in `docs/screenshots/cavern_enemy.png` — that one is the class-1 snail, see below) | `A0B0/A0D3`, 7 | **2** | **5** | 3 | `A24C` {10 G, 1 G, 1 G, —} | Hangs (frame 0) with `phase` counting down 0x10/frame (spawn 0x10, after a retreat 0x70); wakes when its ring column is within 0x0B..0x1A (6 left … 9 right of the hero's body column 0x11), frames 1-3, then chases one cell per frame diagonally toward the hero (`hero_map_row` bands ±3 rows), no gravity except directly above/below him; backs off (diag-up + up, 2-frame pause, 7-frame idle) whenever the hero was hurt this frame (`FF36`) or it is blocked below (`A2E3..A3D4`) |
| 1 | snail — this is the creature clinging to the ceiling in `docs/screenshots/cavern_enemy.png`/`cavern_enemies.png` (frame 0 facing left, one row above the window, so only cells 79/80 = its bottom row are visible; matched 128/128 px by `port/`, while no bat frame exceeds 41 %) | `A0F6/A10A`, 4 | 2 | 5 | 2 | `A250` {1 G, —, 1 G, —} | Falls 1 row/frame; on the ground one step toward the hero every 4th frame (`phase` bits 6-7), never jumps; a blocked step just stops it (`A3E7`) |
| 2 | frog — green, white eyes | `A11E/A141`, 7 (0-1 sit, 2-6 jump) | **1** | **15** | 5 | `A250` {1 G, —, 1 G, —} | Hops at once whenever it faces a hero within 7 rows, otherwise every 8th frame turns toward him and hops; a hop is 4 steps RU, R, R, RD (`A71F`) / LU, L, L, LD (`A723`), one per frame, then gravity; blocked mid-hop → turn around unless facing the hero (`A43F..A4E8`) |
| 3 | hedgehog | `A164/A182`, 6 (0-3 walk, 4-5 curled) | 1 | 8 | 3 | `A248` {10 G, —, —, —} | Walks in its facing; a gap under the leading foot → 3-step gap jump (R, R, RD); a wall → 8-step wall jump (U, RU, RU, R, R, RD, RD, D, `A727/A72F`); after 16 wander steps rests 16 frames (frames 4/5) unless the hero comes within 6 rows, then chases him (`A517..A6F0`) |

Milestone (b) numbers: the frog has **HP 1** — one training-sword hit (`1 + level/2`) kills
it; it does **15** per frame of overlap, the bat/snail 5.  DOSBox check (Recipe B +
`58:+Right 61:-Right`, captures every 0.5 s): LIFE bar 100 % green at 56-58 s, 60 % at 59 s,
~0 % (red) from 60 s, hero dead/restored by 65.5 s — a ≈6.5 s kill consistent with 15-point
contacts separated by the 2-cell knockback, but the 6-frame capture spacing cannot count
individual hits.

### Cavern 2 — EAI2 (ZELRES3[2]), ENP2.GRP, maps mp20/mp21

| class | name | frames | HP | contact | EXP | drops | behaviour |
|---|---|---|---|---|---|---|---|
| 0(+1) | tall plant shooter (2×4) | `A0B0/A0D8` (8) + lower `A10F/A137` | 8 | 10 | 10 | {10 G ×4} | Keeps a random distance 7..10 (`A6D6/A6D7`) from a hero within 5 rows, walks every 2nd frame, fires an arced scripted shot (cell 0x9A, damage 8, `A4FD`) or, cornered, a straight one (7 cells, damage 20) |
| 2 | blue slime | `A16E` (12) | 4 | 8 | 4 | {1 G, —, 1 G, —} | 8 idle + 8 moving frames with ONE random-side step on the 8th, never off a ledge (`A6D8`) |
| 3 | red hopping shooter | `A1B9/A1E1` (8) | 2 | 10 | 10 | {10 G, 1 G, 1 G, —} | Cavern-1 frog hop (`A8EC/A8F0`) plus a spit (cell 0x9E, 6 cells, damage 20) from frame 7 |
| 4 | green bird | `A218/A23B` (7) | 3 | 8 | 4 | {10 G, 1 G, 10 G, —} | The cavern-1 bat state machine (`A923`) |
| 5 | red bird (unused) | `A26D/A290` | 3 | 40 | 255 | {full potion ×4} | class 4 in palette 1; placed in no map |

### Cavern 3 — EAI3 (ZELRES3[3]), ENP3.GRP, maps mp30/mp31

| class | name | frames | HP | contact | EXP | drops | behaviour |
|---|---|---|---|---|---|---|---|
| 0 | ceiling spider | `A0B0/A0EC` (12) | 2 | 40 | 20 | {1 G, 1 G, —, —} | `next&7` states: crawl on the ceiling, drop, land, hop up/across, hop down, climb back (`A2E9`) |
| 1 | red hopper | `A137/A155` (6) | 2 | 40 | 10 | {10 G, 10 G, —, —} | Hops toward the hero without pause (RU,RU,R,R,RD,RD `A4E4/A4EA`), re-facing at every landing; hops away once if the first step is blocked |
| 2 | burrowing snake | `A182/A1A5` (7) | 4 | 16 | 10 | {1 G ×4} | Buried (immune, harmless) → rises → spits venom (cell 0x2B, 15 cells, damage 40, `A654`) → sinks (`A519`) |
| 3 | charging beetle | `A1D7/A1F5` (6) | 4 | 40 | 20 | {10 G ×4} | Idle until the hero is in range, then charges (`next&1`, `link` counts frames, `A66E`) |

### Cavern 4 (ice) — EAI4 (ZELRES3[4]), ENP4.GRP, maps mp40/mp41

| class | name | frames | HP | contact | EXP | drops | behaviour |
|---|---|---|---|---|---|---|---|
| 0 | shell crawler | `A0B0/A100` (16: walk, curl, ball, spin) | 8 | 20 | 10 | {10 G, 1 G, 1 G, 10 G} | Walks; curls into a ball and dashes; waits at walls then wall-jumps (`A456/A45E` tables, `A281`) |
| 1 | dividing green slime | `A15F` (8) | 16 | 4 | 10 | {1 G ×4} | Only magic 3/4/7 (sources 4, 5, 8), the Enchantment sword (sword 6) or a hit on an odd frame hurt it; any other hit makes it SPLIT: claims a spare object (vec 31) and spawns a copy 2 cells ahead (hp 16) |
| 2, 3 | icicles | `A196`, `A1A0` (2) | — (immune) | 80 | 0 | vanish | Hang until the hero is roughly below (rcol 8..0x12), 25 %/frame start falling 1 row/frame, die on landing (no EXP) |
| 4 | spinning blade | `A1B9` (4) | 2 | 80 | 20 | {10 G, 10 G, 10 G, 1 G} | Crawls along surfaces: heading state 0..7, 8×5 candidate tables `A756/A7CE` tried in order each frame |

### Cavern 5 (water) — EAI5 (ZELRES3[5]), ENP5.GRP, maps mp50/mp51 (classes 2-4 call vec 32: swept 2 cells/frame by currents)

| class | name | frames | HP | contact | EXP | drops | behaviour |
|---|---|---|---|---|---|---|---|
| 0(+1) | tall spitter | `A0B0/A0EC` (12) + `A137/A173` | 24 | 40 | 50 | {100 G item, 10 G, 10 G, 10 G} | Walks toward the hero's column; facing him within 4 rows, spits (cell 0xB1, 18/20 cells, damage 40, `A41B/A428`).  Only the upper record takes hits (`A363`) |
| 2 | dividing slime | `A1BE` (8) | 16 | 20 | 20 | {10 G, 1 G, 10 G, 1 G} | As cavern 4 class 1; the copy starts immune/harmless in frames 4-7 (`A641`, `A7FF`) |
| 3 | turning charger | `A1F5/A213` (6) | 8 | 20 | 10 | {10 G, —, 10 G, —} | walk → look around (turn frames 0..4) → charge (frame 5) (`A812`) |
| 4 | diving floater | `A240` (8) | 8 | 10 | 10 | {10 G, —, 10 G, —} | Floats up 1 row/frame, drifts to the hero's column every 2nd frame, dives when over him (rcol 0x10..0x12) (`A91A`) |

### Cavern 6 — EAI6 (ZELRES3[6]), ENP6.GRP, maps mp60/mp61/mp62

| class | name | frames | HP | contact | EXP | drops | behaviour |
|---|---|---|---|---|---|---|---|
| 0(+1) | tall ghost | `A0B0/A100` (16; frames 1,3,12,14 empty) | 48 | 80 | 100 | {100 G item ×4} | Sword only (magic discarded, `A42E`); invisible, immune and harmless (`type|0x60`) while walking, materialises to shoot (cell 0x63, 20 cells, damage 20, `A4DD/A4EA`) |
| 2 | flying fish | `A20E/A25E` (16) | 16 | 40 | 50 | {10 G ×4} | No gravity; 8-entry path tables `A75E`.. (R,R,RU,R,R,R,RD,R), flees when hit, recovers |
| 3 | charging beast | `A2BD/A2E5` (8) | 8 | 40 | 50 | {10 G, 10 G, —, —} | walk → charge (frame 4) → bounce off walls (frame 5), may climb (`A857`) |
| 4 | falling rock | `A31C` (4) | — (immune) | 80 | 0 | {—} | Cavern-4 icicle with sound 0x21 on landing and 8 crumble frames |

### Cavern 7 (heat) — EAI7 (ZELRES3[7]), ENP7.GRP, maps mp70/mp71/mp72

| class | name | frames | HP | contact | EXP | drops | behaviour |
|---|---|---|---|---|---|---|---|
| 0(+1) | tall ranged walker | `A0B0/A0D8` (8) + `A10F/A137` | 16 | 80 | 80 | {100 G ×3, 10 G} | The cavern-2 plant AI: distance 7..10 (`A491/A492`), fires a straight shot (cell 0x30/0x2F, 20 cells, damage 40) |
| 2(+3) | tall spitter | `A16E/A196` (8) + `A1CD/A1F5` | 64 | 80 | 200 | {100 G ×3, 10 G} | The cavern-5 spitter: 25 % per odd frame to spit (cell 0x32/0x31, 20 cells, damage 40) when facing a hero within 5 rows |
| 4 | fast hedgehog | `A22C/A240` (4) | 8 | 40 | 50 | {100 G, 10 G, 10 G, —} | The cavern-1 hedgehog at 2 cells/frame, no resting state (`A8B1` tables) |

### Cavern 8 — EAI8 (ZELRES3[8]), ENP8.GRP, maps mp80..mp84 (every class 255 EXP)

| class | name | frames | HP | contact | EXP | drops | behaviour |
|---|---|---|---|---|---|---|---|
| 0(+1) | tall charger | `A0B0/A0CE` (6) + `A0FB/A119` | 100 | 160 | 255 | {100 G ×4} | Never falls; idles facing the hero; facing him within 5 rows / 14 columns charges 16 cells at 1 cell/frame, stopping at walls (`A2D2`) |
| 2 | walker | `A146/A16E` (8) | 48 | 60 | 255 | {10 G, 10 G, —, —} | 1 cell every 2nd frame, turning at walls; rushes every frame when facing a hero within 5 rows (`A483`) |
| 3 | sentry gunner | `A1A5/A1BE` (5) | 64 | 80 | 255 | {100 G, 100 G, 10 G, 10 G} | Faces a hero within 5 rows, 1/8 per frame fires (cell 0x2A, 18 cells, damage **80** right / **1** left — the left template at `A673` has damage 1, a data bug); shuffles away from ledges (`A538`) |
| 4 | flying chaser | `A1E6` (4) | 96 | 80 | 255 | {100 G, 10 G, —, —} | The cavern-6 fish roaming logic run every 2nd frame, no flee (`A68F`) |

### Cross-cavern patterns

* Four base AIs are reused with new numbers: the **bat** (c1 class 0 → c2 class 4/5), the
  **frog hop** (c1 class 2 → c2 class 3 + spit → c3 class 1 without pause), the **hedgehog**
  (c1 class 3 → c7 class 4 at double speed), the **plant/ranged walker** (c2 class 0 → c7
  class 0), the **tall spitter** (c5 class 0 → c7 class 2), the **dividing slime** (c4 class 1
  → c5 class 2), the **fish** (c6 class 2 → c8 class 4), the **icicle** (c4 → c6 rock).
* Initial HP is a per-class constant written by the AI (1-2 in cavern 1, 48-100 in cavern
  8); contact damage climbs 5 → 160, EXP 2 → 255; drops go from 1 G/10 G coins (caverns 1-4)
  to 10 G / the 100 G item (5-8).  Full potions are only dropped by the unused c2 class 5.
* Tall (2×4) enemies exist from cavern 2 on; only the upper record is ever hurt.
* Only cavern 1 kills enemies on hazard tiles (vec 24 in every class prologue).

## 3. Bosses

Common: HP word at `[A002]+3`, 40-frame death (`boss_dying`, sound effects) then
`boss_defeated`; "damage" = the multiplier applied to `damage_for_source()` (vec 28).

| # | overlay / map | name | start (col,row) | HP | EXP | gold | camera col | knockback | contact |
|---|---|---|---|---|---|---|---|---|---|
| 1 | CRAB `[9]` mp1d | Cangrejo | 0x2B, 0x0C | 150 | 120 | 150 | 12 | free | 6 |
| 2 | TAKO `[10]` mp2d | Pulpo | 0x24, 0x10 | 250 | 200 | 200 | 7 | always left | 10 |
| 3 | TORI `[11]` mp3d | Pollo | 0x2E, 0x12 | 500 | 500 | 500 | 8 | always left | 56 head / 18 |
| 4 | ZELA `[12]` mp4d | Agar | 0x30, 0x0C | 500 | 1000 | 600 | 12 | free | 30 |
| 5 | MEDA `[13]` mp5d | Vista | 0x30, 0x0B | 700 | 3000 | 800 | 12 | free | 30 |
| 6 | LEGA `[14]` mp6d | Tarso | 0x26, 0x07 | 640 | 6000 | 1500 | 8 | always left | 160 body / 80 shot / 10 |
| 7a | ZEL2 `[15]` mp73 | Paguro | 0x30, 0x0C | 600 | 3000 | 1600 | 12 | free | 30 |
| 7 | DRGN `[16]` mp7d | Dragon | 0x1E, 0x08 | 800 | 12000 | 2500 | 5 | free | 40 head+flame / 30 |
| 8 | AKMA `[17]` mp8d | Alguien | 0x2A, 0x00 | 800 | 30000 | 3800 | 12 | free | 40 / 80 beam |
| 9 | MAO1 `[18]` mp90 | Jashiin (appearance) | 0x10, 0x01 | 250 (unused) | 200 | 0 | 5 | always left | 0 |
| 10 | MAO2 `[19]` mpa0 | Jashiin | 0x30, 0x09 | 800 | 10000 | 0 | 12 | free | 80 |

### Cangrejo (crab, `boss_crab.c`)
Matrix of 2×2 parts (6 rows × 10 columns, `A70A`), poses 0-8 + jump pose 9.  Walks one cell
every 2nd frame between columns 0x10..0x31, turning at the limits.  1/8 per frame: 8-frame
crouch (`A481`) then a jump toward the hero (script `A5F9`: 5 up, 3 across, 5 down) dropping
a projectile part (class 0x15, contact 6) from (col+4, row+3) that falls along `A5B6`.
Damage ×4, ×8 on the three lower weak-point parts (classes 0x10-0x12); every hit hops it 2
cells away and plays 0x22.  Death: 40 frames, sound 0x23 every other frame for 30.

### Pulpo (octopus, `boss_tako.c`)
8×7-part bitmap image (`A9AF` bitmaps, `A57D` part lists), 32 poses = stage (0/8/0x10) +
frame.  Damage ×2, ×4 on weak parts (classes ≥ 0x0E), sounds 0x24/0x25; each clean hit
advances the stage with a 16-frame flicker.  Stage 0x10 attacks: 4-frame wind-up then an
ink cloud (4 parts, class 0x10, immune, contact 10) drifting left one cell/frame for 24
frames from (col+4, row+4), sound 0x27.  Death: 40 frames, sound 0x28.

### Pollo (bird, `boss_tori.c`)
9×8 parts from up to four layers (`A64D` bitmaps / `A6CB` lists): body (0/1 flinch),
wings (4), legs (3), head (4).  Hovers over the hero every 2nd frame (columns ≥ 0x0D..0x30);
1/32 per frame or when hit: 4 flaps then a 15-frame dive left (rising to row 0x0E, sound
0x2B) then back to row 0x12; 1/16 per frame lays an egg (fight.bin shot: cell 0xA7, 50
cells left, damage 40) from (col+4, row+4).  Damage ×2, ×8 on the head (class 0, contact
56); sound 0x29; a hit ends a dive.  Death: 40 frames, sound 0x2C.

### Agar (`boss_zela.c`) and Paguro (`boss_zel2.c`)
4×3 parts, `type` = pose (0..4) and `phase` = part index (roles swapped).  Hops (1/16 per
frame) toward the hero, columns 0x11..0x32; while a shot is pending walks toward him at half
animation speed.  50 % per frame when facing him: shot from (col+1, row+3) left or (col+7,
row+3) right — Agar: cells 0x15/0x12, 50 cells, **damage 80**, hit sound 0x25, damage ×½
except magic 3 (source 4) ×2 with sound 0x24.  **Paguro** is the same code (13 bytes shorter:
no magic-3 branch, always ×½ and sound 0x24), palette 0, bolts cell 0x05/0x04 **damage 120**,
HP 600 / EXP 3000 / gold 1600.  Death: 40 frames, sound 0x28 every 4th frame for 21.

### Vista (jellyfish, `boss_meda.c`)
14×12 word image from layered bitmaps (`A5DC` body, `A613` right side, tentacles by hero
position `A62E`, attack `A687`).  Cruises at row 7 between columns 0x0A..0x31 one cell/frame;
when the hero is right below (col+5..col+6) dives 4 rows and back.  5-frame attack cycle
with a 3-frame pause: two drips (fight.bin shots straight down, cell 0x32, 50 cells, damage
80) from (col+6, row+12) and (col+7, row+10).  Damage ×1/8, but a sword ≥ 4 hit ×4 (sound
0x2D, else 0x2E).  Death: 40 frames, sound 0x23.

### Tarso (`boss_lega.c`)
8×8 parts, poses 0..8 (`A744` bitmaps, `A6C8` lists), face patched in.  Walks LEFT (8-frame
cycle, one cell on poses 1,2,3,7, limit column 0x0E); a hit before column 0x2F makes it
retreat right for 20 frames (limit 0x32), sound 0x2F.  On pose 6, 50 %, HP ≥ 20 and no shot
out: 3-step attack (sound 0x30) launching a projectile PART (class 6, immune, contact 80)
from (col+4, row) along the 17-entry path `A5D8` (sounds 0x31), exploding at column < 0x12
(0x32).  No movement while the shot is on its way.  Damage: sword ×2, orb ×1, magic ×1/8.
Death: 40 frames, sound 0x33.

### Dragon (`boss_drgn.c`)
29×10 cell buffer from 5 layers (pose/neck+head 12 cols, body 11, front legs, hind legs,
tail) — wider than the window; the hero is pinned at camera column 5.  Parts are **solid**
(`type|0x80`) while alive.  Walks LEFT one cell per 2 frames to column 0x10; poses by
distance from the window's left edge (> 16 columns: 6/7 upright, 12..16: 0, ≤ 11: 4 head
down).  From a rest pose (0, 4, 7), 1/4 per frame: 6-frame wind-up (pose alternates) then
10 frames of FLAME (sound 0x36): a 13×8-cell image of class 8/9 parts (immune, contact 40)
at (col−10, row+4) (col−6 for pose 5).  Damage: sword/stomp ×½, magic/orb ×1/8; the head
(class 0, contact 40) doubles it (sound 0x34, 7-pose rearing reaction + 8-cell retreat);
a body hit (0x35) makes it back off right to column 0x1E.  Death: 40 frames, poses 2/3
thrash with sound 0x37 every 4th frame for 30, then pose 0xA.

### Alguien (winged demon, `boss_akma.c`)
13×16 cell buffer, 3 wing frames (sound 0x2B on frame 1), wing-tip and face patches.  Flies
2 cells/frame along a fixed swoop (row table by column: high (row 60, above the top) at the
side it comes from, down to row 1 at the far side, `A954/A969`), columns 0x0C..0x33; at each
end climbs 2 rows/frame to row 0x3D, turns and FIRES (sound 0x34): a diagonal BEAM of
class-6 parts (immune, contact 80) hanging forward-down from the body, growing one segment
per frame to 8 (2 columns : 1 row) or 7 (steep, 2:2, chosen when the hero is on the near
half: col < 40 at the left end, ≥ 20 at the right end) then retracting.  **Damage ×1 for
every source** (sound 0x22) — no weak point.  Death: 40 frames hovering, sound 0x37 every 4th
frame.

### Jashiin appearance (`boss_mao1.c`, mp90)
Not a fight: a 135-frame script (`A3BB`) draws Jashiin's 6×8-part image at (0x10/0x0D, 1)
growing from the human figure (class 0) into the demon (classes 1-6), shows three texts —
"Finally, you reached me." / "I enjoyed your show." / "Come on!  I'll kill you." — via video
`[2000]`/`[202A]`, plays sound 0x38, then clears `[E6]` so the map continues as a normal
level.  No hits are read, contact 0, HP unused; the EXP 200 in the block is never awarded
(`boss_defeated` is never set).

### Jashiin (`boss_mao2.c`, mpa0, final)
6×9 cell buffer, 14 poses (walk 0-3, crouch 4-5, jump 6, throw 7-9, cast 10-13).  Waits for
`[FF21]` (not written by fight.bin; only enddemo references it).  **Phase 1** (HP ≥ 200):
invisible; teleports to 8 columns left or right of the hero (row 9), flickers in (5 frames,
immune), holds 5 frames of {0,0,7,7,9} (throw → projectile 1) or {10,10,11,11,12} (cast →
projectile 2), flickers out, vanishes until the projectile is gone.  Hurtable only in those
5 frames.  **Phase 2** (HP < 200): visible; keeps exactly 8 columns from the hero (1-2
cells/frame, columns 0x10..0x35), jumps over obstacles (14-frame 6-up/16-forward/6-down arc
`A666`), throws (1/16 per frame at range); **regenerates 80 HP every 32 frames (sound 0x3C)
and returns to phase 1 at 800**.  Projectiles are contact parts (80, immune, unblockable):
1 = thrown object falling 3 rows and flying 9 cells, gone after 11 frames; 2 = bolt falling
3 rows then flying across the room 1 cell/frame.  Damage: sword ×½, everything else ×¼
(sound 0x39).  Death: 40 frames, poses `ABF9`, sound 0x23 every 8th; 10000 EXP, no gold.

## 4. Corrections to FIGHT.md (not applied there)

* §7 `[A002]`: +3 is the boss **HP** (initial bar value), +B is the **gold** (not +9; +9 is
  the name-record pointer).
* §3/§7 vector 23 sense: cells < 0x49 are passable **iff listed**; 0x49..0x7F always pass;
  markers ≥ 0x80 block.
* §7 "In boss maps called once per frame": the boss overlay also rebuilds the object list
  itself and reads the pending-hit bits directly — fight.bin's sword/contact code is the
  only part of the enemy pass it relies on.
* §9 (b) is right for cavern 1: bat/snail HP 2, frog/hedgehog HP 1; contact 5/5/15/8.

## 5. Not decoded / uncertain

* `[FF21]` (MAO2 start gate) — who sets it (enddemo.bin is the only other reference).
* EAI8 left-facing gun shot damage 1 (`A673`) — data bug or intended, unverified in play.
* The exact pixel/colour of drop ids 0xB (100 G vs shoes) — from `fight.bin 8E14` only.
* Boss sprite banks: ZEL2 shares ZELA.GRP (same cell numbers, palette 0); which
  ZELRES3[64..] bank MAO1/MAO2 use is inferred from the level record (+4 = 16/17).
* MAO1's post-script behaviour (fight.bin's normal enemy pass over the left-over records
  after `[E6] = 0`) and how mp90 transitions to mpa0.
* Ghidra output for DRGN/AKMA/MAO1 is unusable (header decoded as code); those three were
  read from `ndisasm` listings only — the composed sprite layouts were verified by part
  counts, not rendered.
