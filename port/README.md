# Zeliard SDL2 port (Phase 3, milestone b)

A from-scratch C11 re-implementation of the cavern engine (`fight.bin`) that reads
the original game files directly and reproduces the DOS game's cell-granular
movement, collision and combat.  Status: **cavern 1 (MP10) renders pixel-exactly,
Garland walks, jumps, falls, crouches, climbs ladders and uses doors, and the
cavern-1 enemies fight back**: the eai1 AI (bat / snail / frog / hedgehog) runs
with the original tables, contact damage and knockback hurt the hero, the sword
kills, and drops, EXP, gold and the LIFE bar work.  No sound, magic or town yet.

```
cd port
make                 # port/zeliard (SDL2 if pkg-config/sdl2-config finds it, else headless) + the two test binaries
make test            # physics assertions (103) + combat assertions (46)
make verify          # headless renders diffed against the DOSBox captures in docs/screenshots/
./zeliard            # play (needs ../zeliard/ZELRES1-3.SAR or zeliard/ in the cwd)
```

SDL2 is optional at build time.  Without the dev package the same binary is built
headless-only: `--screenshot`, `--script` and `--frames` still work, so everything can
be verified on a server.  If SDL2 lives somewhere unusual, put `SDL_CFLAGS`/`SDL_LIBS`
in `port/local.mk` (auto-included, gitignored).  `compat/SDL_config.h` is a generic
config shim for compiling against bare SDL2 headers (`-Icompat -I<sdl2 headers>`).

## Controls

| key | action |
|---|---|
| Left / Right (or A / D) | walk 1 cell (8 px) per frame; the world scrolls, Garland stays on screen column 12 |
| Up (or W / Z / Space) | jump (hold for up to 2 rows), climb a ladder, enter a door; with Left/Right: diagonal jump |
| Down (or S) | crouch, descend a ladder, let go of a ladder |
| X (or Ctrl) | **attack** — sword slash; with Up an upward slash; held with Down while airborne a down-thrust (×2 damage) |
| C (or Alt) | magic button (read, but casting is not implemented) |
| F12 | dump the framebuffer to the `--screenshot` file |
| Esc | quit |

Command line: `--dir GAMEDIR`, `--map N` (system map index, 0 = MP10 … 0x1E = MPA0),
`--pos COL ROW` (hero top-left map cell), `--speed N` (FF33 speed, default 5 =
84.5 ms/frame), `--scale N`, `--headless`, `--screenshot N FILE` (dump after N rendered
frames), `--script "R6 U3 .6 XL2"` (headless input: hold Right 6 frames, Up 3, idle 6,
sword+Left 2; letters U D L R and X = sword combine, `.` = nothing), `--frames N`,
`--verbose` (per-frame state line, now including LIFE and the swing state).
`--script` only takes effect together with `--headless` (or `--screenshot`).

Default start: the position the DOSBox recipe (docs/DOSBOX_RECIPE.md) ends in — hero
top-left at map (61,7) of MP10, standing in the MURALLA door (scroll_col 45,
scroll_row 61).  The map header's own start (26,16) is a different entry; use
`--pos 26 16` for it.

## What is implemented

* `sar.c` — .SAR reader and the 8-opcode RLE decompressor (kernel 0D9D), AL=2
  containers (plain / two-variant).  Reads `zeliard/ZELRES*.SAR` directly: no asset
  pipeline, no converted files.
* `gfx.c` — the 64-entry MCGA DAC (GAME.BIN A41B), 48-byte PC-88 cells packed to 8×8
  6-bit pixels ([0x2044]), 32-byte 2-bpp sprite cells with the gfmcga outline mask,
  MPPx + DCHR tile banks (DCHR at slot 0x40), fman.grp frame maps (91 × 3×3, bit 7 flip).
* `map.c` — .mdt decode (header, column-major RLE stream, fixture lists A/B/C, door
  table C00A, object table C010) and the STICK.BIN system-map table (map index →
  archive/resource) so door destinations resolve.
* `physics.c` — port of the hero/camera part of `src/fight.c` (addresses in comments):
  36×64 ring with the original single-wrap pointer arithmetic, `passable_wall` /
  `passable_body` classification from tileset cell 0, 3-step horizontal test (sprite
  column, head row, two body rows) with the body = middle column, turn-before-move,
  rise 1 row/frame up to `max_rise` (2, 4 with Feruza shoes) while Up is held, fall 1
  row/frame with the on-screen-rise-first rule, floor at row+3 with the 1-cell-gap
  walk-over, walking off an edge (one extra step), air control, landing crouch after
  ≥2 rows, crouch release after 2 frames, ladders (mount/climb/descend/grab while
  falling/let go, 1 row per rendered frame), conveyors (1 cell per 4 frames + post-jump
  kick), updraft/current tiles, cavern-7 one-way walls, ice slide (cavern 4), wall
  unstick, vertical/horizontal camera catch-up, door arch + letter written into the
  ring (78DD), fixture cells written into the ring (static), hazard tile detection
  (counted, no damage yet), door check (7A83) with real transitions between cavern
  maps (`scroll_to_entry` 7DC1) and a logged hand-off for town doors.
* `enemy.c` — the C010 object table: a live copy per map load (`0xFFxx` = disabled),
  the per-frame enemy pass (8D19) with ring markers `0x80|index` and the covered
  cell saved in `ED20`, the pending-hit → stun conversion (8DB9), the death
  animation (90E6, 6 frames → drop / vanish), removal (914C), the off-screen
  respawn attempt (94FF, timer wrap + the 3×3-free / not-in-view rule) and the
  item state machine (8E14: corpse fade, flash, chest, 1/10/100 G coins, keys,
  potions).  It also **reads the AI overlay's data tables straight out of
  `ZELRES3[ai+1]`** — EXP `A008`, contact damage `A010`, drop lists `[A006]` and
  the 5-byte sprite frames `A030`/`A070` — so those numbers are the originals.
* `ai.c` — the fight.bin services the overlays call (vector table 6000): the eight
  steps (91E5..926C) with the ring-edge refusals and the map-width/row wrap, the
  eight 2×2 probes (92B4..949A) with the exact cell lists, `cell_passable_ai`
  (94E1), `ai_on_hazard` (97A0), `map_col_to_ring` (96A1), `find_spare_object`
  (98C5), `ride_current` (975B) and `KRN_RANDOM`.
* `ai_eai1.c` — the cavern-1 AI (`EAI1.BIN`, ported from `src/ai/eai1.c`, address
  tags kept): bat (idle/wake/chase/retreat, the 0x0B..0x1A wake window, HP 2),
  snail (fall + one step every 4th frame, HP 2), frog (the 4-step hop, HP 1) and
  hedgehog (walk, gap jump, wall jump, rest, HP 1).
* `combat.c` — sword input (6E3B, including the "enemy overhead → upward slash"
  scan and the airborne down-thrust), the blade shapes from sword.grp section 0
  applied every rendered frame (6F07) with the stun rule, `damage_for_source`
  (9851: `{1,2,4,8,32,127}[sword-1] + level/2`, ×(bonus+1), ×2 for a thrust),
  `enemy_take_damage` (97B5) with the drop roll (down-thrust → entry 0),
  `enemy_killed` (96D5) and the EXP award (96C1); hero contact damage (751F, four
  columns × three rows, the AI's per-class table, the shield only on the facing
  side), the shield formula (75E2), knockback 2 cells (6412) and death.
* `render.c` — 320×200 VGA-index framebuffer, playfield 28×19 cells at (48,14) (row
  19 hidden under the HUD as in the game), hero sprite in the three gfmcga passes
  (@3A95: overlay behind, body, overlay in front) with the walk/idle/jump/fall/crouch/
  ladder/conveyor frame selection, shield overlays included; enemy/item sprites as
  2×2 cells from `enp1.grp` through the frame's 16-entry colour table (+3 on the hit
  flash), and a minimal HUD: the LIFE bar (red max / green current, 100 px at
  (84,163)) and the GOLD/ALMAS 6×7 digit boxes from font.grp — all three match the
  original pixel for pixel.
* `main.c` — SDL2 window (×3 nearest), 4×speed-tick frame pacing, keyboard, headless
  PNG dump (`png.c`, stored-deflate writer).
* `test_physics.c` — 103 assertions (idle, walk, walls, jump heights, ceilings,
  diagonal jump, gaps, edge fall, ladders, conveyor, hazard, MP10 door/platform).
* `test_combat.c` — 46 assertions: the eai1 tables (EXP 3/2/5/3, contact 5/5/15/8,
  the four drop lists, the frame pointers), the damage formulas, frog contact
  (15 LIFE per overlapping frame) and the 2-cell knockback, one sword-1 hit kills
  the frog / two kill the bat, EXP 5 and 3 awarded, sword immunity, the bat's
  0x0B..0x1A wake window, the frog hop, the drop → coin → +1 gold chain and the
  down-thrust's "always entry 0" rule.  It also renders the DOSBox capture's
  scene for `make verify`.
* `tools/compare_shot.py` — playfield diff against a DOSBox capture.

Verification: `make verify` renders two positions headlessly and compares them with
the DOSBox captures.  All of it is 100 % pixel-identical:

| check | region | match |
|---|---|---|
| start position vs `cavern.png` | 224×144 playfield | 100 % |
| HUD LIFE bar / GOLD / ALMAS vs `cavern.png` | (84,163) 100×6, (76,187) 36×8, (152,187) 30×8 | 100 % |
| frog sprite vs `cavern_enemy.png` | (128,102) 16×16 | 100 % |
| ceiling creature vs `cavern_enemy.png` | (192,14) 16×8 (lower half) | 100 % |
| whole `cavern_enemy.png` playfield | 224×144, hero masked | 100 % |

A view shifted by 3 columns scores 83 %, so the comparison is sensitive.  The
`cavern_enemy.png` scene is reproduced by putting the hero's top-left at map
(69, 7) of MP10 (scroll 53/61), a class-2 frog facing **left** in sit frame 1 with
its top-left at map (67, 8), and the second creature at ring column 22 one row
above the window.  **Doc correction:** that second creature is not the class-0
bat — its cells are enp1 79/80, i.e. the **class-1 snail**, frame 0, facing left,
standing on the ceiling rock with only its lower half visible.  `docs/ENEMIES.md`
§2 and `docs/DOSBOX_RECIPE.md` §6/§8 both call it a "salmon/red ceiling blob" /
bat; the pixel match (128/128) says otherwise.

## Stubbed / not yet implemented

* The AI overlays of caverns 2-8 and the 11 bosses: only `EAI1` is ported, so on
  any other map the enemies are placed, collide and can be killed but never move
  (`[ai] overlay N is not ported`).  Boss maps do not run their overlay at all.
* Projectiles (`EB80`, vectors 29/30), magic and orbs: the hero's magic button is
  read but nothing is cast, and no enemy shoots.
* Tall (2×4) enemies exist in the record handling but are never spawned by 94FF.
* The hero death sequence is a log line plus a restart at the map entry (the
  original blinks for 30 frames and hands over to the town/Sage screen); EXP
  `127 − 2*level` and the gold halving *are* applied.
* Item states 0x11 (touch trigger), 0x1A/0x1B (shoes) and 0x1D (boss chest) only
  log; the treasure-box gold table is implemented but its `phase` encoding is
  unverified.  The pickup latch (`flags` bit 7 + `timer`) is simplified to
  "collect on overlap".
* The swing length is a 6-frame approximation: the original's `attack_var` is
  driven by gfmcga 3E45, which has not been decoded.
* HUD: only the LIFE bar and the GOLD/ALMAS digits.  No frame, no PLACE/GOLD
  narrow-font labels, no item/magic icons, no message boxes, no signs text.
* Sound and music (docs/MUSIC.md + `tools/msd2mid.py` are the source).
* Elevators (fixture A motion, 7FDC/8074/818E) — cells are drawn static; fixture-C
  variants/animation; the C00C patch list; locked doors (keys) — logged as
  "Can't open this door."; the 26-frame walk-in after a transition; boss rooms use
  the plain entry instead of the AI-driven intro; town maps.
* Hit-flash palettes, hero death animation, Roka demo.
* EGA/CGA/Tandy render modes (only the MCGA pair-packing path).

## Design notes

* **Direct SAR reading.**  Both the archive format and the RLE engine are ~150
  lines of C, so the port loads `ZELRES*.SAR` itself instead of depending on a
  Python pre-conversion step.  The extracted/ tree stays a research artefact.
* **The ring buffer is kept.**  fight.bin's collision tests are pointer arithmetic
  over a 36×64 byte ring with one wrap; several original quirks (a `-1` at ring
  column 0 landing in the previous row, door/fixture cells living only in the ring,
  the camera re-centring by scrolling) depend on it, so the port reproduces the
  ring rather than testing the map grid directly.  Columns are refilled from the
  decoded grid instead of re-decoding the RLE stream.
* **Frame model.**  `game_step()` is one iteration of the 629C main loop; every
  `frame()` (including the extra ones inside ladder climbs) calls the `present`
  callback, which draws, waits out 4×speed ticks of the 236.7 Hz timer and
  refreshes the input bits.  Headless mode drives the same callback from a script.
* **Cell-granular everything.**  No sub-cell positions exist anywhere (8 px steps,
  ≈11.8 fps at speed 5); smoothing would be a deliberate deviation for later.

## Next: milestone (c) — full cavern 1 + the town loop

1. **Projectiles and magic** — the `EB80` shot list (8422/846F/85A5), vectors 29/30,
   the hero-hit test with the shield rules, then the hero's own spells (87B0/8AAD/
   896E, the `EB15` records and the `EB60` orbs).  Nothing in cavern 1 shoots, but
   every later AI needs it, and magic is a hit source the damage table already has.
2. **Elevators and fixture-C animation** (7FDC/8074/818E/8244) and the C00C patch
   list: MP10 has moving platforms the hero currently walks over as static cells.
3. **The rest of cavern 1** — locked doors and keys (7E15), the 26-frame walk-in
   after a transition (7C6E), message boxes (7210/740E) and the sign text, so the
   whole map is playable end to end.
4. **The town side of the loop** — `town.bin` is a different engine (docs/TOWN.md):
   the minimum for milestone (c) is the door hand-off both ways plus the state the
   cavern reads back (gold, EXP, HP, sword/shield level, shoes, keys), even if the
   town itself is only a menu at first.  That also gives the hero-death path its
   real destination (the Sage's "While you were unconscious" screen, 98FC/99E0).
5. **The remaining AI overlays** — `ai_eai2.c`..`ai_eai8.c` are mechanical ports of
   `src/ai/eai*.c` on top of the vectors in `ai.c`; the tall-enemy (2×4) spawn path
   in 94FF and the boss protocol (a per-frame overlay that rebuilds the C010 list)
   are the only new machinery.
6. **Sound** — `FF75` sound ids are already produced by the combat code and thrown
   away; docs/MUSIC.md + `tools/msd2mid.py` are the source for the driver.
