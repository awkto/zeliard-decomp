# Zeliard SDL2 port (Phase 3, milestone d)

A from-scratch C11 re-implementation of the Zeliard engines that reads the
original game files directly.  Status: **the cavern-1 + town loop is playable
end to end** — Garland walks out of Muralla Town through the cavern gate at
column 205, fights his way through MP10, walks back through the MURALLA door,
and dying in the caverns sends him back to the Sage.  All nine caverns render
and run their own enemy AI; projectiles, magic and the orbiting spheres work;
elevators, moving platforms, locked doors and message boxes work.  **The
cavern-1 boss can be reached and killed**: the Cangrejo fight, its EXP/gold
award and the post-boss transition that opens the exit door all run.  **The
shops work**: the proportional text box, the menu widgets and the eight
overlays' own text and price tables, so swords, shields, potions, healing,
inn nights, banking, spells, levels and the NAME.USR save are all in.  **The
status / inventory screen works** (select.bin: Enter in town or in the caverns —
spell selection, the worn key item, the eight potion effects and the Kioku
Feather warp), and so does the **town dialogue box**.  No sound output (the
requests are logged).

```
cd port
make                 # port/zeliard (SDL2 if pkg-config/sdl2-config finds it, else headless) + 6 test binaries
make test            # physics (135) + combat/AI (149) + town (73) + boss (313) + shop (118) + status (87)
make verify          # headless renders diffed against the DOSBox captures in docs/screenshots/
./zeliard            # play cavern 1 (needs ../zeliard/ZELRES1-3.SAR or zeliard/ in the cwd)
./zeliard --town 1   # start in Muralla Town instead
./zeliard --map 1 --sword 4 --level 40 --life 800   # the cavern-1 boss room (mp1d)
./zeliard --town 1 --gold 5000                     # Muralla: the shops are at 39/59/111/138/172
./zeliard --map 0 --potions 0,5,6,7 --spells 1,3,6 # then Enter: the status screen with something in it
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
| C (or Alt) | **cast the selected spell** (`magic_sel`, picked on the status screen) |
| Enter | **the status / inventory screen** (select.bin): pick a spell, wear a key item, drink a potion |
| F12 | dump the framebuffer to the `--screenshot` file |
| Esc | quit |

### Town (town.bin)

| key | action |
|---|---|
| Left / Right | walk one column per frame; the map scrolls while the hero is on screen columns 0x0B..0x10 |
| Up | in front of a door: enter it (a shop is logged, a cavern gate hands over to fight.bin) |
| Space (or X) | talk to an NPC 1-3 columns ahead: the **dialogue box** opens; Space turns the page, Alt cancels |
| Enter | **the status / inventory screen** (select.bin); the potion row is disabled in town, as in the original |
| Esc | quit |

### The status screen (select.bin)

| key | action |
|---|---|
| Left / Right | move the cursor along the row (`SELECT-MAGIC:` / `WEAR:` / `USE:`) |
| Up / Down | change row |
| X (or Ctrl) | on the `USE:` row only: drink the potion under the cursor |
| Enter | close |

### Shops (kingpro / armrpro / drugpro / churpro / innapro / bankpro / kenjpro)

| key | action |
|---|---|
| Up / Down | move the red menu cursor (or scroll the list) |
| Space (or X, Enter) | select / advance the text a page |
| C (or Alt, Backspace) | cancel — the same as picking "Go outside" |

Command line: `--dir GAMEDIR`, `--map N` (system map index, 0 = MP10 … 0x1E = MPA0),
`--pos COL ROW` (hero top-left map cell), `--town N` (start in town N: 0 cmap, 1 mrmp
Muralla, … 9 esmp), `--speed N` (FF33 speed, default 5 = 84.5 ms/frame), `--scale N`,
`--headless`, `--sword N`/`--shield N`/`--level N`/`--life N`/`--gold N` (write the
player record directly; the shops do it properly), `--name NAME` / `--load NAME`
(the NAME.USR the sage writes / restore it, town.bin 7592),
`--screenshot N FILE` (dump after N rendered frames),
`--script "R6 U3 .6 XL2"` (headless input: hold Right 6 frames, Up 3, idle 6,
sword+Left 2; letters U D L R, X = sword, M = magic combine, `.` = nothing),
`--frames N`, `--sound` (log every FF75 request), `--verbose` (per-frame state line).
`--script` only takes effect together with `--headless` (or `--screenshot`).
`--town-col`, `--town-scr`, `--town-anim` and `--town-npc` place the town hero and an
NPC exactly; `make verify` uses them.  `ZEL_SHOP_DEBUG=1` in the environment logs
every shop text opcode with the text printed so far.

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
* `boss.c` / `boss.h` — **the boss protocol** (docs/ENEMIES.md §1/§3): the `[A002]`
  info block (+0/+2 start cell, +3 HP, +5 EXP, +7 camera column, +8 knock-left,
  +9 name record, +B gold), the encounter card's 12 half-flashes (6078/60E6),
  the AI called **once per frame** instead of per enemy (8D1D), the per-frame
  read-back of the pending-hit bits and rebuild of the whole C010 list from the
  overlay's part buffer, the ENEMY HP bar through the video-slot semantics of
  docs/VIDEO_DRIVERS.md ([2012] trough at (50,174), [200A]/[200C] bars at
  (84,175), `value/8` px capped at 100), the 40-frame death (`boss_dying` →
  `boss_defeated`), the reward (71CC: EXP `[A002]+5`, gold `[A002]+B`, gated on
  `EDA0`) and **`post_boss_transition` (72F1)**: the level record's +6/+7 banks
  are swapped in, `FF34` is cleared, the `{u16 ptr, u16 val}` pokes at +8 are
  applied to the map image *and* the player page, and the exit door is moved to
  the hero's column (`scroll_col + hero_scr_col`, +9 when the ring cell 5 to his
  left is not empty).  Knockback obeys `9F01`.
* `boss_crab.c` — **Cangrejo**, cavern 1's boss, ported line by line: the pose
  matrices read out of the image (`[A70A]` → the 6×10 walk matrix A71E and the
  jump matrix A75A), the every-second-frame walk between columns 0x10..0x31,
  the 1-in-8 crouch (`A481`) → 13-step jump script (`A5F9`) → 4-pose landing,
  the dropped projectile part (class 0x35, script `A5B6`), damage ×4 / ×8 on the
  three weak points, the 2-cell hop away from every hit, and the death poses.
* `boss_tako.c` / `boss_tori.c` / `boss_zela.c` — **Pulpo**, **Pollo** and
  **Agar/Paguro** (one file: ZEL2 is ZELA recompiled).  Pulpo composes its pose
  from the 7-byte column bitmap `[A9AF+2p]` plus the `{type, frame}` word list
  `[A57D+2p]`, runs the three-stage hit reaction, the wind-up and the 24-frame
  ink cloud; Pollo composes a 9×8 pose buffer from four layers (body, head,
  wings, legs) and flies the hover / flap / dive / egg cycle; Agar builds its
  12 parts with `type` = pose and `phase` = part index and fires the two bolts.
* `boss_meda.c` / `boss_lega.c` — **Vista** and **Tarso**.  Vista composes a
  14×12 buffer from four `{u8 type, u8 frame}` layers (body, right side, the
  tentacle set picked by the hero's column, the 5-frame attack overlay),
  cruises row 7 between columns 0x0A..0x31, dives when the hero is under it and
  drops two shots on the 4th frame of its 5-frame cycle; damage is ⅛ except a
  sword ≥ 4, which is ×4.  Tarso composes an 8×8 buffer from the pose bitmap
  + byte list (bit 7 = immune and harmless), walks left on poses 1,2,3,7 to
  column 0x0E, retreats right for 20 frames after a hit before column 0x2F, and
  on pose 6 launches the projectile **part** along its 17-step path (A5D8) that
  explodes below column 0x12; damage is sword ×2, orb ×1, magic ⅛.
* `boss_generic.c` — the protocol-only placeholder for DRGN, AKMA, MAO1 and
  MAO2: correct info block, hits, HP, death and rewards, but not the
  original's part composition or movement.
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
* `text.c` / `text.h` — **the presentation layer**: font.grp (ZELRES1[12]) section 0
  (the 8x8 text glyphs, `[F500]`), section 1 (the 6x7 digits, `[F502]`) and section 2
  (the 4-px narrow label glyphs, `[F504]`), the proportional metrics `font_xoff`
  (town.bin 7B82) and `font_advance` (7BE2), and the MCGA video slots the shops
  call: `[2000] vid_window` (the 2-px frame with its 2-px diagonal corner cut),
  `[2022] vid_putchar`, `[202A] vid_puts`, `[2038]/[2010]` the narrow labels with
  their two-pixel-per-bit drop shadow, `[2030] vid_draw_digits`, `[2032]/72C7
  format_number`, gtmcga `[3016] GT_DRAW_CELL` and `[3018] GT_CURSOR`.
* `player.c` / `player.h` — the STDPLY player record.  `zeliard/STDPLY.BIN` is
  loaded at start-up (that is where the per-town shop stock masks `[C9..D1]`,
  `[D2..DA]`, `[DB..E3]` and the initial magic charges come from), `Game.page[]`
  and the engine's named members are kept in step by `player_page_push/pull`,
  and **the save file** is kenjpro A862's: `<name>.usr` = a raw 256-byte image of
  BASE:0000, no header (docs/TOWN.md §8).  `--load` is town.bin 7592.
* `shop.c` / `shop.h` — **the eight shop overlays**.  Each one is loaded raw to
  BASE:A000 exactly as town.bin 6E7E does it, and every string, name, item
  description and price table is read out of that image at the addresses
  `src/shops.c` documents, so the text and the numbers are the original's byte
  for byte; only the code around them is ported.  Implemented:
  * the **text box** (706C): the proportional printer at (56, 99), 4 lines of
    10 px, word-wrapped at 208 px, the `2F`/`0D` newline, `0C` clear, `0F`/`11`
    wait, `13`/`15` mute, the red `|` page marker at (156,139), the 10-px scroll
    of a full box, and the `FF nn` action opcodes;
  * the **menus** (751A `menu_draw_items`, 7539 + gtmcga 3805/37CC
    `menu_draw_icons` with the right-aligned 24-bit price column, 7344
    `menu_select` with scrolling, 7469 `cursor_draw`, 74D3 `yes_no_prompt`);
  * **armrpro**: the five-entry main menu, buying swords and shields from this
    town's stock bitmask with the half-price trade-in, the stock bit going back
    when an item is traded in, the Enchantment sword's uniqueness, the Tumba
    Knight's-sword lock and the Crest of Glory trade, shield repair at
    `ceil((max-hp)/2)` gold, the durability table {30,80,180,300,300,600} and
    "Explain goods";
  * **drugpro**: buy (into the five `[A6..AA]` potion slots), sell at half price
    with the item returned to the town's stock, and the descriptions;
  * **churpro**: the free heal (+8 HP every 20 ticks) and, at full LIFE, the
    magic refill; **innapro**: the per-town price (A2D1), the night, LIFE and
    the magic charges; **bankpro**: the almas exchange at the town's rate
    (A8FA) and the 24-bit balance `[88..8A]`;
  * **kenjpro**: the per-sage greeting, the first-visit spell (`[9D]`, `[BB+n]`,
    the `[E5]` bit), "See Power" with `kenj_assess`'s four verdicts, the level-up
    (`EXP_NEXT` A28C, the per-sage cap A2AC, `LEVEL_TABLE` A380, one level per
    visit) and **"Record Experience" = the NAME.USR save**;
  * **kingpro**: the script chosen by the story flags and the 1000-gold gift.
  Like the original the shop takes over the frame loop (town.bin does
  `call [A000]`), so SDL, `--screenshot` and `--script` all work unchanged; it
  draws into its own 320x200 buffer, which is what the driver's A000 is.
* `status.c` / `status.h` — **the status / inventory screen** (select.bin,
  ZELRES2[1], `src/select.c` and docs/TOWN.md §12).  Both entry vectors are here:
  `status_run_town` = `[A002]` (the potion row is disabled) and
  `status_run_fight` = `[A000]`.  It owns a 320×200 framebuffer and drives the
  caller's `present` callback itself, the way `Shop` does, so the original's
  "the overlay takes over the frame loop" shape is preserved.  The four windows,
  the three rows, the `(count)/(max)` magic line, the INVENTORY window and the
  "I have used …" box are drawn with the BASE:2000 primitives only, exactly as
  the original has to be to run under both engines.  It also carries the
  **itemp.grp** reader (`ItemPics`): the seven picture sections, the 32×16 icon
  blitter (video_mcga 2748) and the 40×18 sword blitter (254C), plus the
  driver's built-in blank slot read out of `GMMCGA.BIN` @2658 — which is what
  finally puts the sword / magic / shield pictures on the HUD too
  (`itemp_hud`).  The eight potion effects are the A452 jump table.
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
* `test_boss.c` — 313 assertions: the `[A002]` block of **all eleven** boss
  overlays against docs/ENEMIES.md §3 (HP, EXP, gold, camera column, knock-left,
  start cell, contact damage, the name record); mp1d's level record (boss bit 7,
  AI 1 = CRAB, bank +5, post-boss banks +6/+7, an *empty* C010 list and no door);
  the encounter card's 12 frames; the crab's pose matrices and its 12 placed
  parts with their ring markers; the ×4 / ×8 damage rule against
  `damage_for_source`; the 40-frame death; EXP 120 + gold 150; the 72F1 pokes
  (the exit door lands on the hero's column, the reward object appears, the
  player-page flag is set); and a 200-frame run of TAKO, TORI, ZELA, ZEL2 and
  the four generic overlays with their marker and reward checks; and the layer
  tables of MEDA (21 + 8 + 6 parts) and LEGA (the nine pose popcounts against
  their byte-list run lengths) plus all four boss projectile templates.
* `test_shop.c` — 118 assertions: the font metrics and `format_number`; the
  STDPLY record and the page <-> Game mapping; the NAME.USR round trip (exactly
  256 bytes); **all nine rows of both the armour and the drug price tables**
  read out of the overlays and compared with docs/TOWN.md §7; the shield
  durabilities; buying the Wise man's sword in Muralla through the real menu
  widget (gold 5000 - 1500 + a 200 trade-in = 3700, `[92] = 2`, the old sword's
  stock bit set, `damage_for_source(1)` 1 -> 2); buying the Clay shield (50 G,
  30 durability, a shielded 40-point hit costing 40/4); a Ken'ko Potion into
  slot `[A6]`; the church heal (and that the "tired" script does *not* refill
  magic); a 30-gold night in Satono and the refusal at 10 gold; the bank's
  1:6 exchange and the deposit landing in `[88..8A]`; the sage's `EXP_NEXT`,
  cap and `LEVEL_TABLE` tables, `kenj_assess`'s four thresholds, the level-up
  (level 0 + 120 EXP -> level 1, 120/120 LIFE, 70 EXP left) and the one-level
  clamp; and the first-visit spell + `[E5]` bit (Yasmin teaches spell 1, Marid
  teaches none).
* `test_town.c` — 73 assertions: the mrmp header, doors, cave record, exits, NPCs,
  walker range and dialogue; the mpat block list; the walk/scroll model and the block
  list; the door ±1 window; the edge exit; NPC markers, the type-1 walker's 2-frame
  cadence and the type-3 facing rule; Space dialogue in and out of range; the
  town → cavern hand-off landing on MP10 (61,7) with scroll (45,61); the return
  position (7DE1); and the death penalties.
* `test_status.c` — 87 assertions: the itemp.grp section layout (6 sword
  pictures of 270 bytes, whole numbers of 192-byte icons, the GMMCGA blank slot)
  and that the Training Sword picture actually paints; the three row lists
  packed out of the record (spells from `[BB..C1]`, key items from `[A1..A5]`
  with the leading "NO USE" slot, potions from `[A6..AA]`) and each cursor
  starting on the value in the record; that the potion row is offered outside
  town and not in it (A09E/A293); **all eight potion effects** — Ken'ko +80
  capped, Juu-en to full, Elixir only the selected spell (and nothing with none
  selected, but still consumed), Chikara all seven, the Magia Stone's four orb
  records (phases 0/4/8/12, ±1, 80 hits), the Holy Water table 80/90/100/110/
  115/120 capped at `[96]` (and nothing without a shield), Sabre Oil stacking
  `[E4]` and doubling / tripling `damage_for_source`, and the Kioku Feather's
  `menu_result = 8`; selecting a spell into `[9D]` and wearing an item into
  `[9E]` through the real cursor; the row change; the Enter debounce; and the
  window frames and header colours in the rendered framebuffer.
* `tools/compare_shot.py` — playfield diff against a DOSBox capture.

Verification: `make verify` renders six positions headlessly and compares them with
the DOSBox captures.  All sixteen checks are 100 % pixel-identical:

| check | region | match |
|---|---|---|
| start position vs `cavern.png` | 224×144 playfield | 100 % |
| HUD LIFE bar / GOLD / ALMAS vs `cavern.png` | (84,163) 100×6, (76,187) 36×8, (152,187) 30×8 | 100 % |
| frog sprite vs `cavern_enemy.png` | (128,102) 16×16 | 100 % |
| ceiling creature vs `cavern_enemy.png` | (192,14) 16×8 (lower half) | 100 % |
| whole `cavern_enemy.png` playfield | 224×144, hero masked | 100 % |
| town tiles vs `town.png`, screen cols 0-3 / 7-9 / 12-25 | map rows 3-7 (y 102..141) | 100 % |
| town hero sprite vs `town.png` | (256,118) 16×24 | 100 % |
| armour shop portrait / text box / menu vs `shop_armour.png` | (56,23) 96×52, (52,96) 216×55, (164,29) 96×55 | 100 % |
| **status screen vs `menu.png`** | the whole 224×144 playfield | 100 % |
| **status INVENTORY window vs `menu.png`** | (180,63) 92×94 | 100 % |

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
6. `docs/ENEMIES.md` §1 and `src/ai/ai_common.h` describe `[A002]+9` as pointing at
   `{u8 x, u16 y, u8 len, chars}`.  It is an ordinary video `[2010]` **positioned
   label**, `{u8 x4, u8 y, u8 xoff_px, u8 len, chars}` (docs/VIDEO_DRIVERS.md §1.1):
   MEDA, LEGA, AKMA, MAO1 and MAO2 all have 2 in the third byte, which under the
   documented reading would put the name at y = 0x02BB.
7. `src/ai/boss_tori.c` (and docs/ENEMIES.md §3 "Pollo") has the two Pollo tables the
   wrong way round: **`[A6CB + 2n]` is the 9-byte column bitmap and `[A64D + 2n]` the
   `{type<<4 | frame}` byte list**, not the reverse.  Proof: the popcount of every
   A6CB entry equals the length of the matching A64D run exactly (2,2,3,2,2,2,2,2,2,2,
   3,3,3,10,9,9,10,10,10, summing to the 88 bytes between A673 and A6CB), while the
   other way round the "bitmaps" overlap each other 2-3 bytes apart.
8. `src/fight.c`'s comment on `72F1` says the exit door is put at
   `scroll_col + hero_col` "(+9 when the poke list's last byte != 0)".  `7373` tests
   the **ring cell five columns to the left of the hero** (`test byte [si-5]` with
   SI from `6DB1`), not the poke list.  mp1d's poke list also ends with a
   `{0x0000, 0xFFFF}` pair *before* the 0xFFFF terminator, i.e. it writes 0xFFFF into
   the player record at `[00]/[01]`.
9. `docs/TOWN.md` §7's armour and drug price tables are indexed by `[C006] - 1`, and
   **cmap and mrmp share `[C006] = 1`** (verified in all ten town maps).  The printed
   row labels are therefore one town out from "Muralla" on: row 0 serves the castle
   *and* Muralla, row 1 Satono, ... row 8 Esco.  In play that means the Training sword
   costs **400** in Muralla (not 800) and Esco uses the row the table calls "Pureza".
   The values themselves are all correct.
10. `docs/VIDEO_DRIVERS.md` §1.1's player-record list mislabels four fields:
   `[0x93]` is the equipped **shield** and `[0x94]`/`[0x96]` its HP and max HP
   (armrpro A686/A6A6/A6B7), while `[0x9D]` is the selected **magic** and
   `[0xAB..0xB1]` / `[0xB4..0xBA]` the magic charges and their maxima
   (kenjpro A965/A325, churpro A0CB).  `port/physics.h` already used the right names.
11. `src/shops.c` glosses gtmcga `GT_DRAW_CELL`'s BH as a 4-px unit ("x4 0x0E = 56 px").
   It is a **cell** column (8 px): the armour shop's portrait table says "x4 7" and
   `docs/screenshots/shop_armour.png` puts that portrait's yellow frame at exactly
   x = 56 = 7 x 8, spanning 12 x 8 cells.  With the 4-px reading the portrait lands
   28 px too far left (the port scored 31 % there before the fix, 100 % after).
12. `src/ai/boss_zela.c` and `docs/ENEMIES.md` §3 (Agar) give the right-hand bolt as
   cell **0x12**; the 13-byte template at A55F says **0x14** (the left one at A552 is
   0x15 as documented).  ZEL2's pair (A543/A550, cells 05/04, damage 120) is right.
13. `src/ai/boss_meda.c` and `docs/ENEMIES.md` §3 (Vista) give the drip as "cell 0x32,
   50 cells"; in the template at A6E0 the **cell is 0x30** and 0x32 *is* the 50-cell
   life byte.  The port now reads all four boss projectile templates straight out of
   the images (`boss_shot_template`) so this class of slip cannot recur.
14. `src/town.c`'s gloss on `font_advance` ("space 5, `I` 3, `W`/`M` 8") is one entry
   out: the 3 belongs to `\` (which prints as an apostrophe); `I` advances 5.  The
   table itself is verbatim and drives a text box that is pixel-identical to the
   DOSBox capture.

## Stubbed / not yet implemented

* **Four of the eleven boss overlays.**  CRAB, TAKO, TORI, ZELA, ZEL2, MEDA and
  LEGA are ported; DRGN, AKMA, MAO1 and MAO2 run on `boss_generic.c`, which is the
  protocol only — the right `[A002]` numbers, hits, HP, the 40-frame death and the
  rewards, but a placeholder block of class-0 parts instead of their real image and
  movement.  `src/ai/boss_meda.c`, `boss_lega.c`, `boss_drgn.c`, `boss_akma.c`,
  `boss_mao1.c` and `boss_mao2.c` hold the behaviour that still has to be written
  (the Ghidra output for DRGN/AKMA/MAO1 is unusable, so three of the four would
  have to come out of the ndisasm listings).  Vista's tentacle-pose *index* and
  Tarso's face-patch slot are the only inferred pieces in the seven that are
  ported; everything else is the image's own data.
* **The encounter card** is 12 blank flashes of the right length in the right
  rectangle; ENCNT.GRP itself (the "!" card art) is not decoded.  Boss maps also
  skip the `[E6]` boss-room walk-in (only mp90 uses it) and the per-boss idle
  animations the shop `[A002]` hooks would drive.
* **Shop details.**  The bank's amount entry is simplified to "deposit/withdraw
  everything" (the original's 24-bit Up/Down/Left/Right entry is not ported), the
  sage's name-entry dialog and `*.usr` file browser are replaced by `--name`, and the
  shopkeepers' idle animations (the `[A002]` hooks: the smith's eyes and mouth, the
  priest, the innkeeper's blink, the drug shop's pot, the sage's ritual aura) do not
  run — the portraits are static.  omoypro (the Princess' chamber, the ending
  hand-off) is loaded but only draws its picture.
* **Two dialogue opcodes.**  The town dialogue box runs `2F`, `FF`, `83` (the Elf
  Crest), `85`, `87` and `8B`, but the two that need the yes/no menu widget —
  `81` (the bsmp sentry's question) and `89` (the Asbestos cape for 2500 almas) —
  take the "no" branch (scripts 13 and 6) instead of prompting.  The widget
  exists in `shop.c`; it has to be lifted out of `Shop` first.
* **The status screen's hidden LEVEL/EXP panel** is bound to the exact key chord
  `[FF18] == 0x0286` in the original (docs/TOWN.md §12.3).  The port has no
  key-mask model, so it is reachable only through the `K` script token / a third
  button bit.
* **The town backdrop**: ympd.bin / ckpd.bin (the mountain / cave panorama) and the
  near/far parallax strips are not decoded, so rows y 14..77 and y 142..157 are a flat
  sky instead of artwork.  The dialogue box scrolls by whole lines where the original
  slides the box up ten single pixels.
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

## What is left of milestone (d), and (e)

1. **The four remaining boss overlays** (DRGN, AKMA, MAO1, MAO2).  The
   protocol, the part machinery and the rewards are done and shared, so each one is
   "read the layer/pose tables out of the image and port the state machine" — the
   same shape as `boss_tako.c` / `boss_tori.c` / `boss_meda.c`.  DRGN, AKMA and MAO1
   need the ndisasm listings because the Ghidra output for them is unusable.
2. **The remaining town machinery**: the two menu dialogue opcodes (`81` / `89`),
   the parallax strips and the ympd/ckpd backdrops, the shopkeeper idle hooks,
   ENCNT.GRP.
3. **Milestone (e), sound**: `sound.c` counts and names every FF75 request the two
   engines produce, and `docs/MUSIC.md` + `tools/msd2mid.py` describe the score
   format; what is missing is a driver — the AdLib/PC-speaker synthesis
   (`src/music_adlib.c`, `src/music_std.c`) behind an SDL2 audio callback, plus the
   sound-effect table.  Nothing in the engine has to change: the requests are
   already produced at the right frames.
4. **More ground truth**: DOSBox captures of caverns 2-9, of a boss encounter (the
   cavern-1 boss needs a full run through mp10, so it is a long timeline), of the
   other shops and of a town dialogue box, so `make verify` can cover the remaining
   tilesets, the boss HP bar and the dialogue renderer.  `docs/screenshots/menu.png`
   turned out to be a capture of the **select.bin status screen** (Enter in
   Felishika's Castle on a fresh game), not a shorter town menu, and `make verify`
   now diffs the port's own status screen against it.
   `docs/screenshots/shop_armour.png` was captured for this milestone with
   `KEYS="6:Return 9:Return 16:Return 20:+Right 27.5:-Right"` followed by 26
   `+Right`/`-Right` + `+Up`/`-Up` tap pairs at 0.45 s intervals from 28 s
   (docs/DOSBOX_RECIPE.md §5's edge-stop-and-scan pattern, scanning *right* from the
   Muralla entry to the first door at column 39), captured at 43 s.
