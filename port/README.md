# Zeliard SDL2 port (Phase 3, milestone c)

A from-scratch C11 re-implementation of the Zeliard engines that reads the
original game files directly.  Status: **the cavern-1 + town loop is playable
end to end** — Garland walks out of Muralla Town through the cavern gate at
column 205, fights his way through MP10, walks back through the MURALLA door,
and dying in the caverns sends him back to the Sage.  All nine caverns render
and run their own enemy AI; projectiles, magic and the orbiting spheres work;
elevators, moving platforms, locked doors and message boxes work.  No sound
output (the requests are logged), no shops, no bosses.

```
cd port
make                 # port/zeliard (SDL2 if pkg-config/sdl2-config finds it, else headless) + 3 test binaries
make test            # physics (135) + combat/AI (149) + town (73) assertions
make verify          # headless renders diffed against the DOSBox captures in docs/screenshots/
./zeliard            # play cavern 1 (needs ../zeliard/ZELRES1-3.SAR or zeliard/ in the cwd)
./zeliard --town 1   # start in Muralla Town instead
```

SDL2 is optional at build time.  Without the dev package the same binary is built
headless-only: `--screenshot`, `--script` and `--frames` still work, so everything can
be verified on a server.  If SDL2 lives somewhere unusual, put `SDL_CFLAGS`/`SDL_LIBS`
in `port/local.mk` (auto-included, gitignored).  `compat/SDL_config.h` is a generic
config shim for compiling against bare SDL2 headers (`-Icompat -I<sdl2 headers>`).

## Controls

### Caverns (fight.bin)

| key | action |
|---|---|
| Left / Right (or A / D) | walk 1 cell (8 px) per frame; the world scrolls, Garland stays on screen column 12 |
| Up (or W / Z / Space) | jump (hold for up to 2 rows), climb a ladder, enter a door, **ride an elevator up**; with Left/Right: diagonal jump |
| Down (or S) | crouch, descend a ladder, let go of a ladder, **ride an elevator down** |
| X (or Ctrl) | **attack** — sword slash; with Up an upward slash; held with Down while airborne a down-thrust (×2 damage) |
| C (or Alt) | **cast the selected spell** (`magic_sel`; see `--map`/tests — the item menu that picks one is not ported) |
| F12 | dump the framebuffer to the `--screenshot` file |
| Esc | quit |

### Town (town.bin)

| key | action |
|---|---|
| Left / Right | walk one column per frame; the map scrolls while the hero is on screen columns 0x0B..0x10 |
| Up | in front of a door: enter it (a shop is logged, a cavern gate hands over to fight.bin) |
| Space (or X) | talk to an NPC 1-3 columns ahead (the script text goes to the window title / `--verbose`) |
| Esc | quit |

Command line: `--dir GAMEDIR`, `--map N` (system map index, 0 = MP10 … 0x1E = MPA0),
`--pos COL ROW` (hero top-left map cell), `--town N` (start in town N: 0 cmap, 1 mrmp
Muralla, … 9 esmp), `--speed N` (FF33 speed, default 5 = 84.5 ms/frame), `--scale N`,
`--headless`, `--screenshot N FILE` (dump after N rendered frames),
`--script "R6 U3 .6 XL2"` (headless input: hold Right 6 frames, Up 3, idle 6,
sword+Left 2; letters U D L R, X = sword, M = magic combine, `.` = nothing),
`--frames N`, `--sound` (log every FF75 request), `--verbose` (per-frame state line).
`--script` only takes effect together with `--headless` (or `--screenshot`).
`--town-col`, `--town-scr`, `--town-anim` and `--town-npc` place the town hero and an
NPC exactly; `make verify` uses them.

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
  MPPx + DCHR tile banks (DCHR at slot 0x40), fman.grp frame maps (91 × 3×3, bit 7 flip),
  the **interleaved** enemy-bank request table (9D8D: 0 ENP1, 1 CRAB, 2 ENP2 … 14 ENP8).
* `map.c` — .mdt decode (header, column-major RLE stream, fixture lists A/B/C with the
  fixture-C variant/limit words, door table C00A, patch list C00C, object table C010) and
  the STICK.BIN system-map table (map index → archive/resource).  `map_apply_patches`
  is the 6BFC conditional-poke engine: the pokes are applied to the raw .mdt and the
  map is re-parsed.
* `physics.c` — port of the hero/camera part of `src/fight.c`: 36×64 ring with the
  original single-wrap pointer arithmetic, `passable_wall` / `passable_body`
  classification, the 3-step horizontal test, turn-before-move, rise/fall 1 row per
  frame, floor rule with the 1-cell-gap walk-over, air control, landing crouch,
  ladders, conveyors, updraft/current tiles, cavern-7 one-way walls, ice slide,
  wall unstick, camera catch-up, door arch + letter written into the ring (78DD),
  hazard tiles, the door check (7A83) with real transitions between cavern maps and
  the hand-off to the town, **elevators** (7FDC/8074: the fixture-A platform and the
  hero move a row on Down/Up when the cells beyond are empty), **fixture-B gates**
  that sink under the hero's weight (818E), **fixture-C moving platforms** that
  patrol between their two limit columns at full or half speed and carry the hero
  (81AE/8244/8299), **locked doors and keys** (7E15: a key is spent, the letter's
  bit 7 is set and the door's story flag is OR-ed into the player page), the
  **26-frame walk-in** after a transition (7C6E) and the **message box** with
  fight.bin's own strings (the 9A1E table) and its 32-frame lifetime (7210).
* `enemy.c` — the C010 object table: a live copy per map load, the per-frame enemy
  pass (8D19) with ring markers, the pending-hit → stun conversion (8DB9), the death
  animation (90E6), removal (914C), the off-screen respawn attempt (94FF) **including
  the tall (2×4) spawn path** (both records placed, class+1 for the lower half), and
  the item state machine (8E14: corpse fade, flash, chest, coins, keys, potions,
  shoes, the Hero's Crest).  It reads the AI overlay's data tables straight out of the
  original image via the **correct** interleaved request table (9CBC: 0 EAI1, 1 CRAB,
  2 EAI2 … 14 EAI8) — EXP `A008`, contact damage `A010`, drop lists `[A006]` and the
  5-byte sprite frames `A030`/`A070`.
* `ai.c` — the fight.bin services the overlays call (vector table 6000): the eight
  steps with the ring-edge refusals, the eight 2×2 probes, `cell_passable_ai`,
  `ai_on_hazard`, `map_col_to_ring`, `find_spare_object`, `ride_current`, `KRN_RANDOM`,
  plus the helpers every overlay carries its own copy of: `hero_dir`, the 2×4 "tall"
  steps (A653/A549/A5CE), `tall_sync` and the two tall hit paths.
* `ai_eai1.c` … `ai_eai8.c` — **all eight cavern AI overlays**, ported line by line from
  `src/ai/eai*.c` with the address tags kept: bat/snail/frog/hedgehog (1); tall plant
  shooter, blue slime, red spitting frog, bird (2); ceiling spider, red hopper,
  burrowing snake, charging beetle (3); shell crawler, dividing slime, icicle, spinning
  blade (4); tall spitter, dividing slime, turning charger, diving floater (5); tall
  ghost, flying fish, charging beast, falling rock (6); tall ranged walker, tall
  spitter, fast hedgehog (7); tall charger, walker, sentry gunner, flying chaser (8).
* `shots.c` — **enemy projectiles** (`EB80`, 13-byte records, ≤31 live): `shot_spawn`
  (vec 29), `shots_clear` (vec 30), the per-frame move/compaction (8422), the eight
  directions (85C2), scripted paths (85F2), the wall test, the hero hit test with the
  shield rules (846F/8556) and the ring-column shift on scroll (8639/864E).
  **Magic** (`EB15`): the 6-frame cast with the charge spent on frame 4 (87B0), the
  spell table 883F (spells 1-4 one bolt, 5 a 4-sprite rain, 6 a 3-way spread, 7 a
  screen-wide hit), the per-spell effect table 8AC6 (bolt lifetimes 5/10/12, spell 3's
  gravity, 2 columns per frame), hits with source `magic_sel + 1` on the 3×3 block
  (8BF7/8C4F) and the 2×2 sprite cells from 8C81/8C8D.  **Orbs** (`EB60`): the
  16-entry orbit table 8790, hit source 9, one charge per hit.
* `combat.c` — sword input (6E3B), the blade shapes from sword.grp section 0 (6F07),
  `damage_for_source` (9851), `enemy_take_damage` (97B5) with the drop roll,
  `enemy_killed` (96D5), the EXP award (96C1); hero contact damage (751F), the shield
  formula (75E2), knockback (6412) and death (98FC/99AD: EXP `127 − 2*level`, **GOLD
  cleared and ALMAS halved** as docs/TOWN.md §10 corrects, HP restored, hand-off to
  the town's Sage).
* `town.c` / `town.h` — **the town engine**: the raw width × 8 town map (level record,
  label, exits C007, doors C009, cave entries C00B, dialogue C00D, NPCs C00F, walker
  range C011, patch list C015), the cpat/mpat/dpat tile bank with its per-cell sky
  masks, block list and animation pairs (§4.1), the mman/cman NPC sprite sets and
  tman.grp hero cells (§4.2), the walk/collision model (6781/67F4/686E/6890), the
  seven NPC behaviours (6B41), NPC markers on grid row 5 (6C2B/6C4E), Space and
  auto-talk dialogue (623F/62ED, text only), doors (6E29), edge exits (6CB5), the
  conditional patch list (6AED) and the cavern hand-off both ways (6FF8 / 7DE1).
* `sound.c` — the FF75 requests fight.bin and town.bin produce are consumed, counted
  and (with `--sound`) logged with their names.  No synthesis yet; docs/MUSIC.md and
  `tools/msd2mid.py` are the source for a real driver.
* `render.c` — 320×200 VGA-index framebuffer, playfield 28×19 cells at (48,14), hero
  sprite in the three gfmcga passes, enemy/item sprites as 2×2 cells from `enpN.grp`,
  **projectiles and magic sprites as transparent tile-bank cells** (gfmcga 412F), the
  **walk-in cutscene**, and the HUD (LIFE bar, GOLD/ALMAS digits).  `town_render`
  draws the town: 28 × 8 cells at y 78, NPCs and the hero as 2×3 tman/mman sprites.
* `main.c` — SDL2 window (×3 nearest), 4×speed-tick frame pacing, keyboard, headless
  PNG dump, and the **two-mode shell**: `game_step()` in the caverns, `town_step()` in
  the towns, with the door/edge/death hand-offs between them.
* `test_physics.c` — 135 assertions (idle, walk, walls, jumps, ceilings, gaps, edge
  fall, ladders, conveyor, hazard, MP10 door/platform, **elevators and the fixture-C
  patrol, locked doors + keys + the message-box lifetime, the C00C patch list, the
  26-frame walk-in**).
* `test_combat.c` — 149 assertions: the eai1 tables, the damage formulas, contact and
  knockback, sword kills, EXP, drops, the bat wake window, the frog hop, **the eight
  projectile directions, the 31-shot cap, life/wall death, shot damage and the
  shield block; the cast state machine, the magic damage table, bolt speed, hit
  source `sel+1`, spell 7's screen-wide hit; the orb damage formula and hit source;
  the sound stub; every eai overlay's A008/A010 tables and a 200-frame run of each
  cavern's AI; the tall (2×4) spawn**.  It also renders the DOSBox capture's scene
  for `make verify`.
* `test_town.c` — 73 assertions: the mrmp header, doors, cave record, exits, NPCs,
  walker range and dialogue; the mpat block list; the walk/scroll model and the block
  list; the door ±1 window; the edge exit; NPC markers, the type-1 walker's 2-frame
  cadence and the type-3 facing rule; Space dialogue in and out of range; the
  town → cavern hand-off landing on MP10 (61,7) with scroll (45,61); the return
  position (7DE1); and the death penalties.
* `tools/compare_shot.py` — playfield diff against a DOSBox capture.

Verification: `make verify` renders four positions headlessly and compares them with
the DOSBox captures.  All eleven checks are 100 % pixel-identical:

| check | region | match |
|---|---|---|
| start position vs `cavern.png` | 224×144 playfield | 100 % |
| HUD LIFE bar / GOLD / ALMAS vs `cavern.png` | (84,163) 100×6, (76,187) 36×8, (152,187) 30×8 | 100 % |
| frog sprite vs `cavern_enemy.png` | (128,102) 16×16 | 100 % |
| ceiling creature vs `cavern_enemy.png` | (192,14) 16×8 (lower half) | 100 % |
| whole `cavern_enemy.png` playfield | 224×144, hero masked | 100 % |
| town tiles vs `town.png`, screen cols 0-3 / 7-9 / 12-25 | map rows 3-7 (y 102..141) | 100 % |
| town hero sprite vs `town.png` | (256,118) 16×24 | 100 % |

A view shifted by 3 columns scores 83 %, so the comparison is sensitive.  The
`cavern_enemy.png` scene is reproduced by putting the hero's top-left at map
(69, 7) of MP10 (scroll 53/61), a class-2 frog facing **left** in sit frame 1 with
its top-left at map (67, 8), and the second creature at ring column 22 one row
above the window.  The `town.png` scene is Muralla Town with `scroll_col` 179
(the right end of the 215-column map), Garland on map column 209 in walk frame 3
facing right.

**Doc corrections found so far**

1. `docs/ENEMIES.md` §2 and `docs/DOSBOX_RECIPE.md` §6/§8 call the second creature in
   `cavern_enemy.png` a "salmon/red ceiling blob" / bat; its cells are enp1 79/80,
   i.e. the **class-1 snail**, frame 0, facing left (128/128 pixels match).
2. `docs/FIGHT.md` §7 says the AI overlay is `ZELRES3[request+1]` and the enemy bank
   `ENP1..ENP8` by index; both request tables (9CBC and 9D8D) actually **interleave**
   the cavern and boss entries — 0 EAI1, 1 CRAB, 2 EAI2, 3 TAKO, 4 EAI3 … 14 EAI8,
   15 AKMA, 16 MAO1, 17 MAO2, 18 ZEL2 — and the resource numbers are 2, 10, 3, 11, 4,
   12, 5, 13, 6, 14, 7, 15/16… (`AI_RES`/`ENP_RES` in `enemy.c`/`gfx.c` hold the exact
   lists).  Only the cavern-1 case works with "+1", which is why the first version of
   this port looked right.
3. `docs/FIGHT.md` §6 "Regeneration" says the death penalty is "gold halved";
   `docs/TOWN.md` §10 corrects the register names, and 99AD really clears the 24-bit
   GOLD `[85..87]` and halves ALMAS `[8B]`.  FIGHT.md should say so too.
4. `docs/FIGHT.md` §5 says the hero is pulled back toward screen column 12 in towns
   as well; town.bin's free zone is screen columns **0x0B..0x10**, and walking left
   settles him on 0x0A (67BF tests `>= 0x0B` *before* decrementing).
5. `docs/TOWN.md` §3 says the C00B cave list has "no terminator" but does not say how
   its length is found; the port sizes it from the largest cave index used by the door
   and exit records (the same rule `tools/mdt2png.py` uses).

## Stubbed / not yet implemented

* **Bosses.**  The 11 boss overlays (CRAB, TAKO, TORI, ZELA, MEDA, LEGA, DRGN, AKMA,
  MAO1, MAO2, ZEL2) are not ported: on a boss map the enemies are placed and can be
  hit but never move (`[ai] overlay N is a boss overlay and is not ported`), and the
  boss intro/protocol (a per-frame overlay that rebuilds the C010 list, the encounter
  card, the HP bar, `post_boss_transition`) does not run.
* **Shops.**  `src/shops.c` (king, omoya, sage, armour, drug, church, bank, inn) is not
  ported: entering a shop door logs the shop name.  Consequently there is no way in
  the port to buy a sword or shield, learn a spell, level up or save; `magic_sel`,
  `magic_count[]`, `sword` and `shield` have to be set from a test or the debug flags.
  The Sage's death screen is a log line plus the town entry, not the real text screen.
* **The status screen** (select.bin, Enter in town) and the potion effects it applies.
* **The town backdrop**: ympd.bin / ckpd.bin (the mountain / cave panorama) and the
  near/far parallax strips are not decoded, so rows y 14..77 and y 142..157 are a flat
  sky instead of artwork.  The dialogue box is not drawn either — the script text goes
  to the window title and `--verbose`.
* **NPC sprite frames.**  The town NPCs are drawn from the mman/cman frame tables, but
  the one NPC visible in `docs/screenshots/town.png` (Muralla column 188) matches only
  ~75 %; every sprite/frame combination was tried, so either the frame table's stride
  or the bank's cell base needs another look.  The hero (tman) is 100 %.
* **Magic sprite artwork.**  The spell shapes, motion, timing and damage are the
  originals, but the 24 cells the sprites use (bank slots 0x67..0x7E) are refilled per
  spell by gfmcga 44CE from a table in the *parked* segment, which the port does not
  load; it draws the cavern tile bank's own cells at those slots instead.
* Item states 0x11 (touch trigger) and 0x1D (boss chest) only log; the treasure-box
  gold table is implemented but its `phase` encoding is unverified.  The pickup latch
  is simplified to "collect on overlap".
* The swing length is a 6-frame approximation: the original's `attack_var` is
  driven by gfmcga 3E45, which has not been decoded.
* HUD: only the LIFE bar and the GOLD/ALMAS digits.  No frame, no PLACE/GOLD
  narrow-font labels, no item/magic icons, no sign text.
* Sound and music: `sound.c` counts and names the requests but synthesises nothing.
* Hit-flash palettes, the hero death animation, the Roka demo, the "doorway to the
  past", the save file (NAME.USR).
* EGA/CGA/Tandy render modes (only the MCGA pair-packing path).

## Design notes

* **Direct SAR reading.**  Both the archive format and the RLE engine are ~150
  lines of C, so the port loads `ZELRES*.SAR` itself instead of depending on a
  Python pre-conversion step.  The extracted/ tree stays a research artefact.
* **The ring buffer is kept.**  fight.bin's collision tests are pointer arithmetic
  over a 36×64 byte ring with one wrap; several original quirks (a `-1` at ring
  column 0 landing in the previous row, door/fixture cells living only in the ring,
  the camera re-centring by scrolling) depend on it, so the port reproduces the
  ring rather than testing the map grid directly.
* **Two engines, one player record.**  `Game` holds both the cavern state and the
  STDPLY player page (gold, HP, keys, story flags); `Town` points at the same `Game`,
  exactly as the original's mode-0 overlay swap keeps BASE:0000 in place.
* **Fixtures are a live copy.**  Elevators and the fixture-C platforms move, so the
  port copies the map's lists into `Game.fix[]` and restores the map grid under a
  fixture's previous position each frame before drawing it at the new one.
* **Frame model.**  `game_step()` is one iteration of the 629C main loop; every
  `frame()` calls the `present` callback, which draws, waits out 4×speed ticks of the
  236.7 Hz timer and refreshes the input bits.  `town_step()` is the 61FC loop with the
  same callback.
* **Cell-granular everything.**  No sub-cell positions exist anywhere (8 px steps,
  ≈11.8 fps at speed 5); smoothing would be a deliberate deviation for later.

## Next: milestone (d) — all nine caverns

1. **The boss overlays.**  Every cavern ends in a boss room, so finishing the game
   needs the 11 overlays in `src/ai/boss_*.c` plus the boss protocol: the encounter
   card (6078), the AI called once per frame instead of per enemy (8D1D), the
   `[A002]` info block (HP bar, EXP, gold, camera column, knockback side), the
   part-buffer rebuild of the C010 list, and `post_boss_transition` (72F1).
2. **The shops**, because they are the only way to gain a sword, a shield, spells and
   levels — without them caverns 3+ are unwinnable.  `src/shops.c` is complete; the
   missing pieces are the proportional font, the text box and the menu widgets from
   town.bin (706C/7344/751A/7539) and gtmcga (3805/37CC).
3. **The remaining town machinery**: the status screen, the dialogue box renderer, the
   parallax strips and the ympd/ckpd backdrops, the save file.
4. **Sound**: wire `sound.c` to a real driver (docs/MUSIC.md, `tools/msd2mid.py`).
5. **More ground truth**: DOSBox captures of caverns 2-9 and of a town with a dialogue
   box open, so `make verify` can cover the other tilesets and the text renderer.
