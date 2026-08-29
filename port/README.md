# Zeliard SDL2 port — scaffold (Phase 3, milestone a)

A from-scratch C11 re-implementation of the cavern engine (`fight.bin`) that reads
the original game files directly and reproduces the DOS game's cell-granular
movement and collision.  Status: **cavern 1 (MP10) renders pixel-exactly and Garland
walks, jumps, falls, crouches, climbs ladders and uses doors** with the original
rules.  No enemies, combat, HUD or sound yet.

```
cd port
make                 # port/zeliard (SDL2 if pkg-config/sdl2-config finds it, else headless) + test_physics
make test            # frame-by-frame physics assertions (synthetic maps + MP10)
make verify          # render the start position headlessly and diff it against docs/screenshots/cavern.png
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
| F12 | dump the framebuffer to the `--screenshot` file |
| Esc | quit |

Command line: `--dir GAMEDIR`, `--map N` (system map index, 0 = MP10 … 0x1E = MPA0),
`--pos COL ROW` (hero top-left map cell), `--speed N` (FF33 speed, default 5 =
84.5 ms/frame), `--scale N`, `--headless`, `--screenshot N FILE` (dump after N rendered
frames), `--script "R6 U3 .6 L2"` (headless input: hold Right 6 frames, Up 3, idle 6,
Left 2; letters U D L R combine, `.` = nothing), `--frames N`, `--verbose` (per-frame
state line).

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
* `render.c` — 320×200 VGA-index framebuffer, playfield 28×19 cells at (48,14) (row
  19 hidden under the HUD as in the game), hero sprite in the three gfmcga passes
  (@3A95: overlay behind, body, overlay in front) with the walk/idle/jump/fall/crouch/
  ladder/conveyor frame selection, shield overlays included.
* `main.c` — SDL2 window (×3 nearest), 4×speed-tick frame pacing, keyboard, headless
  PNG dump (`png.c`, stored-deflate writer).
* `test_physics.c` — 103 assertions (idle, walk, walls, jump heights, ceilings,
  diagonal jump, gaps, edge fall, ladders, conveyor, hazard, MP10 door/platform).
* `tools/compare_shot.py` — playfield diff against a DOSBox capture.

Verification: `make verify` renders the start position and compares the 224×144-px
playfield with `docs/screenshots/cavern.png` — 100 % of pixels identical (with the
hero and arch masked, and also unmasked: the fman sprite and the DCHR door arch
match too).  A view shifted by 3 columns scores 83 %, so the comparison is sensitive.

## Stubbed / not yet implemented

* Enemies, projectiles, magic, sword, contact damage, knockback, HP/death, regen,
  item pickups (object markers are parsed but never placed in the ring).
* HUD (LIFE/PLACE/GOLD/ALMAS box and frame), message boxes, signs text.
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

## Next: milestone (b) — kill one enemy

FIGHT.md §9(b): place object markers (`0x80|index`) in the ring from the C010 table
during the per-frame enemy pass (8D19), load enp1.grp (2-bpp cells + outline masks,
gfmcga 3336/33AB sprite path), port eai1.bin's AI vectors (docs/ENEMIES.md,
`src/ai/`), contact damage 5/frame with the 2-cell knockback (6412), the sword
input and blade shapes (6E3B/6F07, sword.grp section 0), `enemy_take_damage`
(1 + level/2 per hit at sword 1), death phases → drop/EXP, and the LIFE bar.
