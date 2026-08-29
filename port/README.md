# Zeliard SDL2 port (Phase 3, milestone e)

A from-scratch C11 re-implementation of the Zeliard engines that reads the
original game files directly.  Status: **the cavern-1 + town loop is playable
end to end** — Garland walks out of Muralla Town through the cavern gate at
column 205, fights his way through MP10, walks back through the MURALLA door,
and dying in the caverns sends him back to the Sage.  **The game boots the way the
real one does**: with no command-line argument it plays the whole opening demo —
the scrolling prologue over the pendant, the storm, the demon's warning, the
ZELIARD title screen, the STAFF credits and the fifteen-picture storm demo with
its typed narration and lip-sync — and then starts a new game in Felishika's
Castle; Space or Return skips an act.  The **ending** (`--ending`) and the
**"a Tear of Esmesanti"** cutscene (which now plays out of every boss room's
exit door) are in as well.  All nine caverns render
and run their own enemy AI; projectiles, magic and the orbiting spheres work;
elevators, moving platforms, locked doors and message boxes work.  **The
cavern-1 boss can be reached and killed**: the Cangrejo fight, its EXP/gold
award and the post-boss transition that opens the exit door all run.  **The
shops work**: the proportional text box, the menu widgets and the eight
overlays' own text and price tables, so swords, shields, potions, healing,
inn nights, banking, spells, levels and the NAME.USR save are all in.  **The
status / inventory screen works** (select.bin: Enter in town or in the caverns —
spell selection, the worn key item, the eight potion effects and the Kioku
Feather warp), and so does the **town dialogue box**.  **The game has sound**:
an OPL2 core written for this port plays the AdLib arrangement of the original
`.msd` scores on the original tick model, the town and every cavern get the
score their level record names, and the `FF75` sound effects run through
`SNDADLIB.DRV`'s own tracks and patches; `--speaker` plays the PC-speaker
arrangement instead.  **Every one of the eleven boss overlays is ported** —
DRGN, AKMA, MAO1 and MAO2 were the last four — and the HUD now draws its own
gauge troughs, the LIFE / PLACE / GOLD / ALMAS labels and the place name, all
pixel-identical to the DOSBox captures.  **The screen is complete**: `mole.bin`'s
stone frame, the strip above the playfield and the grey HUD panel with its
item-slot frames are decoded and painted, `ympd.bin` / `ckpd.bin` paint the
mountain and cave backdrops with their three parallax strips, and `encnt.grp`'s
"ENCOUNTER!" card flashes when a boss room opens — `docs/screenshots/cavern.png`,
`town.png` and `menu.png` now match the port over the **whole 320x200 screen**.
The `.usr` save file **round-trips
through the real game**: a file written by `port/` loads under DOS with F7
"Restore Game", and because *every* town's map records carry a cave entry that
is now enough to reach the second half of the game — cavern 4, 5, 6 and 8, four
more towns and **Paguro**, cavern 7's boss, were captured that way for this
milestone and are diffed in `make verify` (see "Ground truth" below), along with
MP70 and MP81, the last two maps a town door reaches.  Cavern 1's intended route
was worked out at the same time, and **all three of caverns 1-3's boss rooms are
now entered and left through their own doors, twice each**, with nobody at the
keyboard: once for the fight, once for the reward the map hands back afterwards
(see "What 72F1's third poke is for").

```
cd port
make                 # port/zeliard (SDL2 if pkg-config/sdl2-config finds it, else headless) + 9 test binaries
make test            # physics (141) + combat/AI (171) + town (138) + boss (627) + shop (183)
                     #   + status (87) + playthrough (60) + audio (305) + cutscene (46) = 1758 assertions
make verify          # 143 headless renders diffed against the DOSBox captures in docs/screenshots/,
                     #   plus the gd decoders vs tools/grp2png.py over all 31 intro/ending resources
make playthrough     # the same two routes as test_playthrough, with the step-by-step log
./zeliard            # the real boot: the opening demo, then Felishika's Castle
./zeliard --no-intro # straight to the castle (needs ../zeliard/ZELRES1-3.SAR or zeliard/ in the cwd)
./zeliard --intro-act 3   # just the storm demo; --ending plays enddemo
./zeliard --town 1   # start in Muralla Town instead
./zeliard --map 1 --sword 4 --level 40 --life 800   # the cavern-1 boss room (mp1d)
./zeliard --town 1 --gold 5000                     # Muralla: the shops are at 39/59/111/138/172
./zeliard --map 0 --potions 0,5,6,7 --spells 1,3,6 # then Enter: the status screen with something in it
./zeliard --speaker                                # PC-speaker music instead of AdLib
./zeliard --headless --frames 1600 --script ".1600" --music 4 --dump-audio mus1.wav
./zeliard --town 2 --town-col 210 --gold 20000 --level 12 --save ZCAV2   # write ZCAV2.usr
./zeliard --load ZCAV2                             # ... and resume exactly there
./zeliard --map 22 --level 20 --life 900           # the Dragon (mp7d); 28 = Alguien, 30 = Jashiin
```

SDL2 is optional at build time.  Without the dev package the same binary is built
headless-only: `--screenshot`, `--script` and `--frames` still work, so everything can
be verified on a server.  If SDL2 lives somewhere unusual, put `SDL_CFLAGS`/`SDL_LIBS`
in `port/local.mk` (auto-included, gitignored).  `compat/SDL_config.h` is a generic
config shim for compiling against bare SDL2 headers (`-Icompat -I<sdl2 headers>`).

## Controls

### Cutscenes (opdemo / enddemo)

| key | action |
|---|---|
| Space / Return | abort the current act (`[FF1D]` / `[FF29]`); the next one starts |
| Esc | quit |

### Caverns (fight.bin)

| key | action |
|---|---|
| Left / Right (or A / D) | walk 1 cell (8 px) per frame; the world scrolls, Garland stays on screen column 12 |
| Up (or W / Z / Space) | jump (hold for up to 2 rows), climb a ladder, enter a door, **ride an elevator up**; with Left/Right: diagonal jump |
| Down (or S) | crouch, descend a ladder, let go of a ladder, **ride an elevator down** |
| X (or Ctrl) | **attack** — sword slash; with Up an upward slash; held with Down while airborne a down-thrust (×2 damage) |
| C (or Alt) | **cast the selected spell** (`magic_sel`, picked on the status screen) |
| Enter | **the status / inventory screen** (select.bin): pick a spell, wear a key item, drink a potion |
| F1 / F2 | toggle the music / the sound effects (STICK's own hotkeys) |
| F12 | dump the framebuffer to the `--screenshot` file |
| Esc | quit |

### Town (town.bin)

| key | action |
|---|---|
| Left / Right | walk one column per frame; the map scrolls while the hero is on screen columns 0x0B..0x10 |
| Up | in front of a door: enter it (a shop is logged, a cavern gate hands over to fight.bin) |
| Space (or X) | talk to an NPC 1-3 columns ahead: the **dialogue box** opens; Space turns the page, Alt cancels |
| Enter | **the status / inventory screen** (select.bin); the potion row is disabled in town, as in the original |
| F1 / F2 | toggle the music / the sound effects |
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
player record directly; the shops do it properly), `--name NAME` (the NAME.USR the
sage's "Record Experience" writes), `--save NAME` (write NAME.usr for the state the
rest of the command line sets up and exit — kenjpro A862 without walking to a Sage)
and `--load NAME` (town.bin 7592: restore the file, and with no `--town` resume in
the town its `[C4]` names at the column `[80]`/`[83]` name),
`--screenshot N FILE` (dump after N rendered frames),
`--script "R6 U3 .6 XL2"` (headless input: hold Right 6 frames, Up 3, idle 6,
sword+Left 2; letters U D L R, X = sword, M = magic combine, `.` = nothing),
`--frames N`, `--sound` (log every FF75 request), `--verbose` (per-frame state line).
Sound: `--no-music` (no audio at all), `--speaker` (the PC-speaker back end instead
of AdLib), `--music N` (pin one score, 0-13 in the `9E53` order `mgt1 ugm1 mgt2 ugm2
mus1..mus8 mbos mmao`; `-1` = effects only) and `--dump-audio FILE.wav` (render the
sound to a 16-bit mono WAV instead of a sound card — deterministic, works headless).
**F1** toggles the music and **F2** the sound effects, as STICK's own hotkeys do.
`--script` only takes effect together with `--headless` (or `--screenshot`).
Cutscenes: `--intro` / `--no-intro` force or suppress the opening demo (the
default is to run it only on a plain interactive launch, which is what GAME.BIN
does when it was given no command-line argument — anything that names a start
state or a scripted/headless run suppresses it), `--intro-act N` plays one act
of opdemo on its own (1 = prologue + demon + title, 2 = the STAFF credits, 3 =
the storm demo), `--ending` plays enddemo, `--cutscene-frames N` stops one after
N rendered frames and `--gd-art NAME OUT.png` renders one intro/ending resource
the way `tools/grp2png.py` does.  During a cutscene **Space** or **Return**
aborts the act, exactly as `[FF1D]` / `[FF29]` do.
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
  the **interleaved** enemy-bank request table (9D8D: 0 ENP1, 1 CRAB, 2 ENP2 … 14 ENP8),
  **`mole.bin`** (ZELRES2[7]) — the screen furniture GAME.BIN far-calls once at boot
  (`A178..A18D`) and nothing ever redraws, which is why neither engine owns any of
  it: four RLE'd two-plane pictures (`0x0E` the 224x13 strip above the playfield,
  `0x34`/`0x55` the 48x200 stone frames, `0x80` the 224x42 grey HUD panel with its
  three white item-slot frames) blitted by the mode-4 entry at `0x24B` through the
  16-entry pair table at `0x2AE`, plus the two 8x5 cyan marks `0x38C` ORs on at
  (8,47) and (304,47) — and **`encnt.grp`** (ZELRES3[55]), the "ENCOUNTER!" card,
  laid out by gfmcga `[301C]` (`0x4518`) as 28x5 eight-pixel cells at (48,40) from
  the 140-byte map inside gfmcga.bin at `0x4588`, two bits a pixel through `0x4092`
  (`0` transparent, `3` -> `[4FF6]` = 0x12, `1`/`2` -> `[4FF5]` = 0x10).
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
  left is not empty).  Knockback obeys `9F01`.  Eight of the eleven overlays also
  set `hit` bit 5 on every part they emit while their own hit variable is set
  (`boss_hit_flash`; CRAB `A6BC` and its seven cousins), which `sword_apply` reads
  as "already struck" — so the blade lands on a boss every *other* frame at most.
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
* `boss_drgn.c` — **the Dragon** (cavern 7).  `boss_paste` builds a 29×10 cell
  buffer out of five layers — neck+head pose (12 columns), body (11), front and
  hind legs (7 each) and tail (4) — from the image's own pointer tables; the
  parts are solid (`type|0x80`) while it lives.  It walks left one cell every
  second frame to column 0x10, picks its pose from the distance to the window's
  left edge, and from a rest pose 1/4 per frame winds up for 6 frames and then
  breathes a 13×8 flame image (class 8/9, sword-immune) for 10.  Damage is ½ for
  the sword, ⅛ for magic and orbs, doubled on the class-0 head, which also makes
  it rear up through a 7-pose reaction table and back off eight cells.
* `boss_akma.c` — **Alguien** (cavern 8), a 13×16 buffer: the body from three
  wing frames, the wing-tip patch (5×2 at buffer (3,13) / (5,13), alternating
  every other frame) and the face patch, which A570 writes as **two columns of
  one row**, not one column of two.  It flies two cells a frame along the fixed
  swoop tables A954/A969, climbs two rows a frame at each end until row 0x3D,
  turns and fires a diagonal beam of class-6 parts that grows a segment a frame
  to 8 (7 when steep) and then retracts.  Damage is the full
  `damage_for_source()` — no scaling and no weak point.
* `boss_mao1.c` — **Jashiin's appearance** (mp90).  Not a fight: a 135-byte
  script at A3BB, one byte per frame, that steps the 6×8 two-cell-spaced image
  from the human figure (class 0) into the demon (classes 1..6), shows the
  overlay's three texts, plays sound 0x38 and finally clears `[E6]` so the map
  carries on as an ordinary level.  Nothing reads the hit bits and nothing
  touches HP, so there is no death branch at all.
* `boss_mao2.c` — **Jashiin** (mpa0, the final boss), a 6×9 buffer with 14
  poses.  Phase 1 is the teleport-and-strike loop: he appears 4 or 24 columns
  along from the window, flickers in over 5 frames (immune), holds the 5-frame
  throw `{0,0,7,7,9}` or cast `{10,10,11,11,12}` — the only frames he can be
  hurt in — flickers out and stays gone until his projectile is spent.  Under
  200 HP phase 2 keeps him visible at exactly 8 columns from the hero, jumping
  the 14-frame arc at A666 when a wall stops him and throwing 1/16 per frame at
  range, and **regenerating 80 HP every 32 frames**: back at 800 he drops into
  phase 1 again.  Both projectiles are contact parts, not fight.bin shots, so
  the shield does not apply to them.
* `boss_generic.c` — the protocol-only fallback.  Nothing uses it any more; it
  is kept for an overlay index the port does not know.
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
  It also carries **`ympd.bin` / `ckpd.bin`** (ZELRES2[8]/[9], docs/TOWN.md §4.3),
  the two backdrop painters town.bin parks at `(BASE+0x2000):3300` and far-calls
  with the video mode in AL — so every address in `town_load_backdrop` is a file
  offset + 0x3300.  Above ground `ympd` paints one 224x88 picture at (48,14)
  (mode-4 entry `0x34B8`); underground `ckpd` paints 224x72 at (48,30) plus the
  112x16 far strip at (48,14) that `GT_SCROLL_FAR_*` moves (`0x37F3`).  Both then
  paint the two 112x8 ground strips at (48,142) and (48,150), each duplicated to
  +112 px.  The pixel expanders (`0x34F9`/`0x383A`) give the backdrop four colours
  0/1/4/5 and the ground strips 0/2/4/6 (ympd's second half 0/1/2/3, which drops
  the last shift); the three RLEs differ per overlay (`0x335C`, `0x358D`, `0x3664`).
  The strips are stored unshifted and rolled by `scroll_col * {4, 8, 16}` mod 112 —
  Muralla's capture at `scroll_col` 179 is 88 px / 64 px along, which is exactly
  what `GT_SCROLL_NEAR_*`'s 8 and 16 px a step give, and both towns then match the
  capture over the whole screen.  Map rows 0-2 show the backdrop through each
  cell's own sky mask, the strip `GT_CAPTURE_BACKDROP` (gtmcga 3028) grabs from
  y 78..101 before the map is drawn over it.
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
* `gd.c` / `gd.h` — **gdmcga** (ZELRES1[5]), the second renderer: the intro/ending
  art format and the 16x16 blend palette (docs/CUTSCENES.md §1/§3/§6).  Both
  unpackers (the `u16 nmask` + mask-bit + lag-2 2-bit XOR delta one, and the
  6/14-bit RLE that only `ttl1-3.grp` use), the 4469 pixel packer (every PC-88
  pixel becomes a 4-bit value and every pair of neighbours one MCGA byte
  `left<<4 | right`, which is directly a DAC index), the ten 16-entry palette
  records read straight out of gdmcga.bin at `4289` with
  `DAC[l*16+r] = C[l] + C[r]`, and the twenty-five vector slots the demos call:
  the 2-plane / 3-plane / plane-masked / transparent-`ao` draws with the 8-pass
  dissolve (31B4 and its two mask tables at 32B9/32C1), `gd_erase`,
  `gd_text_line` + `gd_text_scroll` (the `screen &= 0x99 | src & 0x66`
  composite that is why the pendant is drawn in colours 0,1,8,9),
  `gd_picture_box` with its 7-byte dithered left shadow, `gd_sky_dither`,
  `gd_face_eyes` / `gd_face_mouth`, `gd_tile_map` + `gd_draw_ornament_row` (the
  title screen's corner scrollwork, assembled out of `ttl2.grp`'s 40-column tile
  bank through the 25 x 34 map at opdemo `912B` and drawn as a row and its
  bit-reversed mirror), `gd_storm`, `gd_fx_sand` (all three of its tables),
  `gd_fx_recolour`, `gd_wipe` and the ending's `gd_end_open` / `gd_end_close`.
  A `GD_ART[]` table carries the geometry of all 31 resources, and
  `--gd-art NAME OUT.png` renders one of them; `tools/compare_gdart.py` diffs
  that against `tools/grp2png.py`'s reference decoder.
* `cutscene.c` / `cutscene.h` — **opdemo.bin** (ZELRES1[0]) and **enddemo.bin**
  (ZELRES2[50]).  Every script, text block, metric table and tile map is read out
  of the overlay image at run time, so the text is the original's byte for byte.
  opdemo's three acts: act 1 (the logo over the copyright line, the pendant with
  the prologue scroller composited over it, the rain-and-lightning `gd_storm`,
  the demon's face assembled out of three single planes of `dmaou.grp`, his
  typed warning with inline mouth frames, and the title screen with `zopn.msd`
  and the ornament growing in over 100 steps), act 2 (the STAFF credits over
  `zend.msd`) and act 3 (the fifteen pictures inside `waku.grp`'s frame driven by
  the 5786-byte byte script at `79C6` — a proportional-font typewriter with word
  wrap, a drop shadow, per-speaker ink, per-character click effects and inline
  `0x8n`/`0x9n` lip-sync codes that cost no ticks, which is what lets the mouth
  track the syllables).  enddemo's nine tableaux — **with** the narration
  engine `src/enddemo.c` missed (see "Corrections") — and the typewriter credits
  roll synchronised to the score through `[FF21]`, with the seven-entry scene
  table, `gd_end_open`/`close` and the two-pass `fin.grp` stencil.  Like a shop,
  a `Cutscene` owns its 320x200 framebuffer and its own 256-entry DAC and drives
  the front end's `present` itself, so SDL, `--screenshot` and `--script` all
  work unchanged.  One rendered frame is four ticks of the 236.7 Hz `[FF1A]`
  counter, which divides every wait the demos use.
* `tear.c` / `tear.h` — **rokademo.bin** (ZELRES3[0]), "a Tear of Esmesanti", and
  GAME.BIN's Tear-slot row.  Not a gd demo: it plays on the fight screen through
  gfmcga, and `fight.bin 7C18` runs it from the door handler when the door
  record's byte +8 has bit 7 set — the exit door `post_boss_transition` installs
  in every boss room, so it now plays whenever a boss room is left.  Garland
  walks thirteen steps in on `dman.grp`'s ten column-major 3x3 frame maps,
  raises the crystal and his sword (a 16x24 2-bpp picture inside gfmcga.bin at
  `4A31`/`4A91`/`4AF1`, picked by `[92]`), and the crystal flies to its HUD slot
  along the byte-precision Bresenham interpolator at `A4A3`/`A50A` with the
  sparkle frames from `4BDD`/`4CDD` over a saved-and-restored background; then
  `mfan.msd` plays, and he walks off right.  `[A0]` counts the Tears, and
  **GAME.BIN `A3A5`** finally draws the row of crystals along the top border —
  the nine slot columns come out of GAME.BIN at `A3D3` and the two 16x13 icons
  out of `GMMCGA.BIN` at `2A61`/`2B31`.
* `sound.c` — the FF75 requests fight.bin and town.bin produce are consumed, counted,
  (with `--sound`) logged with their names, and handed to `audio.c`.
* `opl2.c` / `opl2.h` — **a compact OPL2 (YM3812) synthesiser written for this port**
  (no third-party dependency): 9 two-operator voices with the register set the drivers
  use (`01` waveform-select enable, `20-35` AM/VIB/EG/KSR/MULT, `40-55` KSL/TL,
  `60-75` AR/DR, `80-95` SL/RR, `A0-A8`/`B0-B8` f-number/block/key-on, `BD` rhythm
  mode, `C0-C8` feedback/connection, `E0-F5` waveform), the four waveforms, the
  exponential attack and linear decay/release of the real envelope generator
  (rate `r` moves `(4+(r&3))/4 * 2^((r>>2)-13)` attenuation units per 49716 Hz
  sample, so `r`=48 decays 96 dB in 20.6 ms as the datasheet says), key-scale level
  and rate, feedback (π/16 … 4π), the two global LFOs and the five rhythm-mode
  drums.  It runs in floating point at the caller's sample rate, scaling every
  increment by 49716/rate.
* `msd.c` / `msd.h` — **the score interpreter**: `MSCADLIB.DRV` and `MSCSTD.DRV`
  ported line by line (hex tags in the file are addresses in those two drivers).
  Blob-B header, the 6 melodic tracks + the OPL rhythm track for AdLib and the three
  Tandy tracks for the speaker, note bytes (`dur<<4 | pitch`, rest, hold, legato,
  gate/staccato), all of `80-FF`, the loop/call/counter flow control, the `FF21`
  sync counters, the software vibrato, the OPL patch loader and level computation,
  the rhythm track's `BD` register handling and drum levels, the PC-speaker envelope
  and its two-voice output stage, `FF24` fade-out and the `INT 60h` calls the engines
  make (start / stop / pause / enable / the sfx channel claim).  It can also emit an
  event log identical to `tools/msd2mid.py --dump`, which is what `test_audio.c`
  diffs against.
* `audio.c` / `audio.h` — the back end: the 236.7 Hz `INT 8` clock derived from the
  sample counter (so the tempo is right whatever the frame rate is), the music driver
  on every second tick and the sound driver on every tick, exactly as STICK calls
  `[FF10]` then `[FF0C]`; the `9E53` request table that turns a level record's music
  index into a `ZELRES` resource; **the `SND*.DRV` effect driver** — a second small
  interpreter that reads its tracks, its 9-byte OPL patches and its duration tables
  straight out of `SNDADLIB.DRV` (or `SNDSTD.DRV`), arbitrates by the record's
  priority byte and claims OPL channels 4/5 from the music through `INT 60h AX=6`;
  a one-pole DC blocker (the half/abs-sine waveforms carry a large DC term the real
  card's output stage removes); the SDL2 audio callback; and the `--dump-audio` WAV
  writer.  Nothing here runs unless `main.c` opens it, so the test binaries and plain
  `--headless` stay silent and byte-for-byte deterministic.
* `render.c` — 320×200 VGA-index framebuffer, playfield 28×19 cells at (48,14), hero
  sprite in the three gfmcga passes, enemy/item sprites as 2×2 cells from `enpN.grp`,
  **projectiles and magic sprites as transparent tile-bank cells** (gfmcga 412F), the
  **walk-in cutscene**, and the **HUD**: the four `[2004]` gauge troughs (LIFE
  51,162 w33 · PLACE/ENEMY 51,174 w136 · GOLD 51,186 w66 · ALMAS 121,186 w66,
  row 0 black, rows 1-8 dark blue, row 9 the bright lip), the LIFE and ENEMY
  bars, the GOLD/ALMAS digits, the four `[200E]` narrow-font labels (their own
  records out of town.bin 6C93..6CB4 and fight.bin 6C44/6C4C/6C8F, green on a
  red shadow) and the `[2010]` place name — the cavern map's `[C00E]`, the town
  map's `[C004]` or, in a boss room, the boss's `[A002]+9` record, whose own y
  is 0xBB: the boss name replaces the GOLD line, as PLACE is replaced by ENEMY.
  All of that is compared pixel for pixel by `make verify`.  `town_render`
  draws the town: 28 × 8 cells at y 78, NPCs and the hero as 2×3 tman/mman sprites.
* `shell.c` / `shell.h` — **the two-engine shell**, lifted out of `main.c` so every
  front end takes the same code path.  It owns the map/tileset/AI-overlay/enemy-bank
  cache, `shell_enter_town` (7B79 / 99E0), `shell_enter_cavern` (town.bin 6FF8),
  `on_door` (7A83 → 7B32), the level record's bank loads (7EBB / 6117), the post-boss
  bank swap (7305) and the town frame with its cavern / town / shop / doorway
  actions (61FC).  `main.c` and `playthrough.c` are two front ends over it.
* `main.c` — SDL2 window (×3 nearest), 4×speed-tick frame pacing, keyboard, headless
  PNG dump and the `--script` feeder; the two-mode loop itself is `shell_frame()`.
* `nav.c` / `nav.h` — **the autopilot** (not part of the original).  "Can Garland get
  from here to there" is a question about fight.bin's own rules, so rather than model
  them a second time the navigator *asks the engine*: every cell he can stand on is a
  node, and from each node a dozen short button macros are run on a throw-away clone
  of the real `Game` with the enemies switched off; wherever he settles becomes an
  edge tagged with the macro and its length.  The graph is executable by construction
  (MP10: 1542 nodes, 18190 edges, 197 k simulated frames, ~0.2 s).  Dijkstra from the
  goal over the reversed graph gives the field; playing is "run the macro on the
  cheapest outgoing edge, re-plan when he ends up somewhere else", plus the things a
  player does that the survey cannot see — back out of contact range, face what is
  hitting him, down-thrust whatever he is standing on, stand still and let the 719E
  regeneration run, and bump the cost of a patch of ground he cannot get out of.
  **Fixture rides.**  An elevator, a gate or a patrolling platform is a *moving*
  standable cell whose three cells live only in the ring, so every position one can
  reach is ground: `fixture_rows` / `fixture_cols` make a node of each of them
  (8024's "all three cells empty" rule bounds an elevator shaft), `probe_place_fixture`
  puts the fixture where the node needs it before the probe runs — under the hero's
  feet for a node it holds up, alongside for a ledge it passes, which is what a player
  does by waiting for it — and three extra macros ("ride 4", "ride 8", "sink 3") are
  probed from a node a fixture holds up, in *both* patrol directions, so "stand still
  and be carried" is a move.  Because the fixture's position is not part of the node,
  each such edge remembers the fixture and the column (or row) and direction it was
  probed at, and `edge_ready` refuses it until the live fixture is there: waiting on a
  ledge for the platform falls out of the ordinary planner, and `step_walks_off`
  refuses a bare left/right step whose destination has neither ground nor a fixture
  under it, so a stale plan cannot walk him off the end of one.  `test_playthrough`
  crosses both of MP10's floorless gaps with it (columns 1..15 at row 43 and 32..53 at
  row 53), and walks the whole Muralla-gate -> door-5 lap.
  **Three more things a player knows and the survey does not**, added while
  walking that lap.  *Lava costs*: the probe is immortal, so a node whose body
  cells touch one of the tileset's four hazard cells (7505/73C0) looks like any
  other; `build_field` now charges `NAV_HAZCOST` (48) to enter one, enough to
  walk round MP10's lava lake at columns 36-51 without forbidding a crossing that
  is sometimes the only way.  *Contact is broken with a real step*: 751F charges
  every frame an enemy is touching him, and the executor used to answer that by
  turning to face it and then stepping away, one frame each -- but 6824 spends
  the first frame of a move in the wrong facing just turning, so the two
  one-frame macros only flipped his facing to and fro while the enemy ate him (a
  full 800 LIFE gone beside MP10's (149,50) crawler).  Contact now gets a
  four-frame step away, before anything else.  *A jump onto a moving platform is
  never planned*: nine frames in the air is four to nine columns of platform, and
  the cell the survey measured is only where he lands if the platform is exactly
  where the probe had it; missing it off MP10's fix[2] drops him in the lava pit
  at (0..6, 44..47), which nothing climbs out of.  Jumping *off* a platform onto
  solid ground is still offered -- only the landing matters.  And an improving
  edge that has been dropped as unrepeatable is handed back when nothing else
  improves, because on a platform "it failed three times" is a statement about
  phase, not about the move.
  Two bugs came out of the same work: the edge chooser tested
  `dist[to] + cost < dist[here]`, which is *never* true where the anti-stuck penalty
  is still zero because that is exactly the equation `build_field` solved — so the
  navigator only moved at all once the "rooted to the spot" counter had poisoned the
  ground around it, and route 1 is 29 000 frames shorter now that it picks the field's
  own edge; and `fits` / `standable` rejected any row within three of the ring's wrap,
  so MP20's entry cell from Satono (row 62, feet on row 1) was not even a node and the
  whole map was unreachable.  Rows wrap with the ring now, and MP20's locked door to
  MP2D at (171,54) is reachable from the Satono entry where nothing was before.
* `playthrough.c` / `playthrough.h` — **the route driver**.  A route is a list of
  objectives ("walk to Muralla's weapon shop and buy a shield", "reach the door at
  map (128,32)", "kill the boss", "work this shelf until 500 EXP"); the *movement* is
  not scripted at all.  Shops are answered frame by frame: the driver pages the text
  with the select button and walks the cursor to the row the route names, using the
  `in_menu` / `menu_row` that `menu_select` now publishes.
* `test_physics.c` — 139 assertions (idle, walk, walls, jumps, ceilings, gaps, edge
  fall, ladders, conveyor, hazard, MP10 door/platform, elevators and the fixture-C
  patrol, locked doors + keys + the message-box lifetime, the C00C patch list, the
  26-frame walk-in, **and the ring/`[80]` alignment after scrolling both ways** —
  the last one walks MP10's entrance shelf 40 columns east and back and checks that
  ring column *i* is still map column `scroll_col + i` on every step, which is the
  regression guard for the `scroll_right` bug below).
* `test_combat.c` — 149 assertions: the eai1 tables, the damage formulas, contact and
  knockback, sword kills, EXP, drops, the bat wake window, the frog hop, **the eight
  projectile directions, the 31-shot cap, life/wall death, shot damage and the
  shield block; the cast state machine, the magic damage table, bolt speed, hit
  source `sel+1`, spell 7's screen-wide hit; the orb damage formula and hit source;
  the sound stub; every eai overlay's A008/A010 tables and a 200-frame run of each
  cavern's AI; the tall (2×4) spawn**.  It also renders the DOSBox capture's scene
  for `make verify`.
* `test_boss.c` — 594 assertions: the `[A002]` block of **all eleven** boss
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
  their byte-list run lengths) plus all four boss projectile templates; and the
  **part hit bit** — that an unstruck boss emits `hit = 0`, that the frame after a
  hit *every* part carries bit 5, that `sword_apply` then refuses them all so the
  boss takes no damage that frame, that the flag clears again the next frame and the
  blade lands, and that ZELA — one of the three overlays whose image has no
  `or byte [si+5],0x20` — never sets it while still taking the hit.
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
* `test_town.c` — 138 assertions: the mrmp header, doors, cave record, exits, NPCs,
  walker range and dialogue; the mpat block list; the walk/scroll model and the block
  list; the door ±1 window; the edge exit; NPC markers, the type-1 walker's 2-frame
  cadence and the type-3 facing rule; Space dialogue in and out of range; the
  town → cavern hand-off landing on MP10 (61,7) with scroll (45,61); the return
  position (7DE1); the death penalties; and **the static screen art** — that
  mole.bin's four blocks cover exactly the left 48×200, the right 48×200, the
  224×13 strip and the 224×42 panel and never the playfield, that the panel is grey
  with white slot frames and carries `0x38C`'s cyan marks, that ympd paints y14..101
  with no far strip of its own and ckpd y30..101 with one, that every pair in a
  backdrop is one of the four colours the expander can make, and that encnt.grp
  composes "ENCOUNTER!" out of nothing but `[4FF5]` and `[4FF6]`.
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
* `test_playthrough.c` — 38 assertions over two autopilot runs, two fixture
  crossings and a survey of cavern 1's topology (`make playthrough` prints the
  routes step by step):
  * **route 1, the opening**, played end to end with nobody at the keyboard: the
    castle, the King's 1000 gold, the road east into Muralla Town, the weapon shop
    (the Clay shield through the real menu widget), the cavern gate at column 205,
    ~130 000 frames of work on MP10's entrance shelf for 500 EXP, back out through
    the MURALLA door, two visits to the Sage (level 1, 120 LIFE), the church and back
    into the cavern.  Asserts the gold, the shield, the level, the LIFE and that both
    hand-offs happened.
  * **route 2, caverns 1-3, their bosses and their rewards**: MP1D/Cangrejo,
    MP2D/Pulpo and MP3D/Pollo, each fought to the 40-frame death and left through
    the exit door 72F1 puts on the hero's column, with Satono's smith and Sage and
    Bosque's smith in between.  **All three boss rooms are now entered twice**, the
    way the maps mean them to be (see "What 72F1's third poke is for"): out onto
    the shelf, straight back in through the door standing beside him there, pick
    the boss's Key up off the floor of what is now an ordinary room, out again, and
    open the locked door the Key was for — MP10's (128,32) into Satono, MP20's
    (205,47) into MP30, MP31's (188,20).  **Cavern 2 is walked from its own
    entrance now**: off Satono's *right* edge into MP20 at (6,62), a hundred and
    fifty columns and one fixture ride to the Key lying at (149,44), and into MP2D
    through the room's own **locked** door at (171,54) — which is what the room's
    header start record `(171,55)` says the way in is.  15 doors, four Keys and
    three shops, all walked.  Two `P_GOTO`s are left: MP20's descent from the Key
    ledge at row 43 to the row-55 floor in front of the boss door (the graph route
    goes back west to column 106 and down three elevators), and the whole of MP31
    (see "What is left" below).  Asserts three bosses, no deaths, the boss EXP and
    gold, three shops, fourteen doors, the Key left in the bag, the three
    boss-defeated story flags, that MP20's locked boss door was opened with a Key,
    and both doors a boss Key unlocked for good.
  * **cavern 1's topology**, surveyed three ways, because it is what the walking
    route depends on: from the Muralla gate at (61,7) the graph reaches 1073 of
    MP10's 1542 nodes and, of its six doors, only the MURALLA door back out and
    **(159,50) into MP21** — not the boss door and not the locked one; from where
    MP21's (15,50) door lands the hero, (95,50), the locked boss door at (26,15)
    *and* MP10's own Key item are both reachable; and from where Satono's left edge
    drops him the two doors on the boss shelf are.  It also pins the col-165 ladder,
    whose nodes exist only because the survey now hangs the hero on a ladder the way
    65C5 does.
  * **the fixture rides**: that the elevator shaft at column 48 is a node at every
    row 8024 lets the platform reach and not only at the row the record starts on,
    that the whole of fix[2]'s patrol (columns 1..15 at row 43) is standable while
    the map under it is not, that a mid-platform node really has ride edges and that
    they remember the fixture they need — and then two live crossings of MP10's
    floorless gaps with the planner driving, westward over fix[2] (67 frames) and
    over fix[3] at row 53 (638 frames — the platform has to come back for him
    now that the executor waits for it instead of walking off its front).
* `test_audio.c` — 305 assertions over the sound back end:
  * **the score parser against `tools/msd2mid.py`**: all 17 scores in all three
    blob-B arrangements (AdLib, Tandy, PC speaker) are run through `msd.c`, and the
    decoded event stream — note on/off with velocity and the legato flag, drum hits,
    patch changes, CC7 volumes, the tick number of every one of them, and the tick at
    which the machine state first repeats (the song loop) — is diffed line for line
    against `python3 tools/msd2mid.py NAME --dump`.  All 51 match exactly; the test is
    skipped with a note if python3 or the tool is missing.
  * **the tick/tempo model**: `INT 8` = 236.7 Hz, the score rate
    `118.35 * (256-T)/256` for the tempo bytes docs/MUSIC.md quotes, µs/quarter =
    51,913,590/(256-T), and — driven through the real accumulator — exactly `256-T`
    score ticks per 256 driver ticks for every tempo from 0x20 to 0xA0.  mus1's loop
    is asserted at ticks 2305..5377 and mus5's body at 5664 ticks = 58.1 s, the
    cross-check docs/MUSIC.md makes against the MT-32 arrangement.
  * **the `9E53` music table**: the 14 `{archive, resource, name}` records the port
    compiles in are compared byte for byte with the table read out of `fight.bin`
    (`ZELRES2[0]` at `6000`), and every one of the 14 scores is loaded and run.
  * **`FF75` → the `SND*.DRV` effect table**: `SNDADLIB.DRV` defines 65 effects
    (7-byte records at `1743`: priority, two track pointers, the duration-table
    base); the priorities of the 15 ids the engines use are checked against the file,
    every track pointer is inside the track area or the empty stub at `201F`, every
    id the engines write to `FF75` resolves, and `SNDSTD.DRV`'s 5-byte records give
    the same 65 effects.  `INT 60h AX=6` is checked both ways: `CL` bits 0-1 mute the
    music's OPL channels 4 and 5, `CL = 0` gives them back.
  * **the OPL2 core**: a keyed-on voice at f-number 580 block 4 is rendered for a
    second and measured at 440 Hz by zero crossings, and key-off with RR 15 drops it
    a hundredfold inside 200 ms.
  * **note pitch**: every note-on in mus1's first 2400 ticks is compared with the
    f-number and block the driver programmes — all within 14 cents of the logged MIDI
    note (the game's f-number table is a uniformly flat scale), with the `E1` detunes
    accounted for separately.
  * **silence and determinism**: the engine hooks are no-ops while audio is off, and
    the same `--dump-audio` script rendered twice is bit-identical.
* `tools/compare_shot.py` — playfield diff against a DOSBox capture.

Verification: `make verify` renders nine positions headlessly and compares them with
the DOSBox captures.  All 53 checks are 100 % pixel-identical, three of them over
the **whole 320×200 screen** — every pixel of the frame, the backdrop, the
playfield and the HUD at once:

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
| **MP10 vs `cavern.png`** | the **whole screen** 320×200 | 100 % |
| **Muralla vs `town.png`** | the **whole screen** (ympd + parallax at `scroll_col` 179) | 100 % |
| **the status screen vs `menu.png`** | the **whole screen** | 100 % |
| mole.bin's stone frame vs `boss_cangrejo.png` / `shop_armour.png` | (0,0) 48×200, (272,0) 48×200 | 100 % |
| the strip above the playfield vs `cavern2.png` | (48,0) 224×14 | 100 % |
| the HUD panel vs `restore_menu.png` | (48,158) 224×42 | 100 % |
| the ckpd cave backdrop vs `town_satono.png` | (48,30) 224×48 | 100 % |
| the ckpd far parallax strip vs `town_satono.png` | (48,14) 224×16 | 100 % |
| the near ground strips vs `town_satono.png` / `town.png` | (48,142) 224×16 | 100 % |

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
15. **`src/fight.c`'s item-state table is indexed by `type & 0x1F` directly**, not by
   `(type & 0x1F) - 0x10`: 8E14's own list is 0x10 corpse, 0x11 touch trigger, 0x12
   flash, 0x13 treasure box, 0x14/0x15 coins, **0x16 Key**, **0x17 lion's-head Key**,
   0x18 +80 HP, 0x19 full recovery, 0x1A shoes-by-cavern, 0x1B Feruza shoes, 0x1D boss
   chest, 0x1E Hero's Crest.  The port had the whole tail shifted by one from 0x16 on,
   which made every Key in the game a 100-gold coin — including the boss reward
   (`type 0x76`) that unlocks the door out of cavern 1.  Fixed in `enemy.c`.
16. `item_state_0x13`'s treasure box selects its prize with **`phase & 0xF`**, not
   `phase >> 4`; the shipped boxes carry 0..3 in the low nibble (MP10 (26,23) is a
   100-gold box, (99,31) a 50-gold one), so with the old reading every box in the game
   paid 50 gold.
17. **7B25 was missing from the port's `door_check`**: passing through an *unlocked*
   door ORs `d->flag_mask` into `*d->flag_ptr` (`src/fight.c` 1457).  That is how the
   post-boss exit door records that cavern 1 has been cleared (`[03] |= 0x20`), and
   the C00C patch lists key off exactly those bytes.
18. **914C's event-object flag was stubbed out**: an object with `+7 bit5` keeps a
   `{flag byte, mask}` pair in `+B`/`+D` instead of a respawn column, and removing it
   ORs the mask into the player page.  MP10's four event objects set `[02] bits
   80/40/20/10`, MP20's seven set `[0A]`/`[0B]`, and the boss-room reward chests set
   `[02] |= 08` / `[0B] |= 10` / `[13] |= 04` — which is what stops them coming back.
19. MP10's boss corridor (rows 32-35, columns 122-152, with the doors to MP1D at
   column 141 and to Satono at 128) is sealed on the left by the block at 119-122 and
   on the right by a **conveyor staircase pushing right** (columns 153-156): `walk_left`
   refuses to move while `conveyor == 1` (663E), and `gravity` skips `conveyor_check`
   while `vstate & 0x80`, so the only way *in* is fight.bin's own door at (26,15) — a
   locked door — or the exit door 72F1 creates.  Worth a line in `docs/FIGHT.md` §5:
   it is not obvious from the map that the corridor is one-way.
20. `src/town.c`'s gloss on `font_advance` ("space 5, `I` 3, `W`/`M` 8") is one entry
   out: the 3 belongs to `\` (which prints as an apostrophe); `I` advances 5.  The
   table itself is verbatim and drives a text box that is pixel-identical to the
   DOSBox capture.
21. `src/ai/boss_akma.c` calls Alguien's face patch "1 column x 2 rows"; A570 writes
   the second byte at `di + 0x10`, i.e. the **next column**, so it is 2 columns x 1
   row.  (The wing-tip patch, which uses `di + 0x0E` after two `movsb`, really is
   5 columns x 2 rows.)
22. `src/ai/boss_mao1.c` and `docs/ENEMIES.md` §3 say Jashiin's poses 0..2 "use class
   0 only".  Pose 2's byte list (`[A4BA]`) starts `18 16 17`, i.e. **class 1** — only
   poses 0 and 1 are the pure class-0 human figure.
23. Not a correction but a gap worth recording: a boss's `[A002]+9` name record has
   y = **0xBB** in all eleven overlays, which is the GOLD row, and `6150` draws it
   through `[2010]` while `6C55`/`6C87` put ENEMY where PLACE goes.  So in a boss
   room the HUD reads `LIFE / ENEMY <bar> / <boss name> ALMAS` — there is no GOLD
   line at all.  The DOSBox capture `boss_cangrejo.png` confirms it.
24. `docs/ARCHITECTURE.md`'s overlay-slot table (row **B**) lists `mole.bin` among
   the overlays loaded to `BASE:A000`.  It is not: GAME.BIN `A16C..A18D` loads it to
   **`(BASE+0x3000):0000`** (the segment goes into the far pointer at `A472`, whose
   offset word at `A470` is 0) and `call far [0xa470]`s it once, with the video mode
   in `AL`.  Nothing else ever touches it, and what it draws — the stone frame, the
   strip above the playfield and the grey HUD panel with its item-slot frames — is
   why neither engine has any code for them.  Worth a line in ARCHITECTURE.md and
   in docs/VIDEO_DRIVERS.md; the port's `gfx_load_screen_frame` is the reader.
25. `docs/TOWN.md` §4.3's screen table splits y 14..29 ("far backdrop strip") from
   y 30..77 ("backdrop painted by ympd/ckpd, static").  That is the **underground**
   layout: `ckpd.bin` really does paint the two separately (224×72 at (48,30) and a
   112×16 strip at (48,14) that is duplicated to (160,14) and is the one
   `GT_SCROLL_FAR_*` moves).  Above ground `ympd.bin` paints **one 224×88 picture
   at (48,14)**, so rows 14..29 there are part of the panorama and never scroll.
   Both also paint **to y 101**, not to y 77: rows 78..101 are the strip
   `GT_CAPTURE_BACKDROP` grabs and rows 0-2 of the map then show through their sky
   mask, which is why the table's "78..141 the 8 map rows" and "rows 0-2 show the
   strip captured from y 78..101" are two views of the same pixels.
26. `docs/TOWN.md` §4.3 does not say how far the near strips are shifted per step
   beyond "8 / 16 px".  The phase is `scroll_col × 8` and `scroll_col × 16` modulo
   the strips' own 112-px width: `town.png` was captured at `scroll_col` 179 and its
   two strips are exactly 88 px and 64 px along, which is what `make verify` now
   pins.  The far strip's step is 4 px on the same 112-px cycle.
27. `docs/ENEMIES.md` §"A030/A070" is right that the hit flash is skipped in boss
   maps — gfmcga `3713` tests `[FF34]` *before* the `add al,3` — but that leaves
   `hit |= 0x20` looking useless in a boss room, and it is not: eight of the eleven
   overlays write it into every part they emit while their own hit variable is set,
   and `sword_apply` (6F8B) refuses a marker whose object already has bit 5.  Its
   whole effect there is to halve how often the blade can land.  Worth a sentence
   in §1 next to the part-buffer rebuild.

**A port bug worth calling out** (found while porting MAO2, fixed in `shell.c`):
`shell_load_enemy_banks` took the level record's `+5` bank whenever the map's AI
index was a boss overlay.  For the ten `mpNd` rooms that is right — their `+4` is
0xFF — but **mpa0's `+4` is 17 (MAO2.GRP) and its `+5` is 0xFF**, so the final
boss map ended up with *no* sprite bank ("cannot load enemy bank 255") and
Jashiin was invisible, under the generic overlay as much as under the new one.
6117 copies `+5` over `+4` unconditionally; when `+5` is 0xFF that request
simply fails and leaves whatever 7EBB already loaded in place.  The rule is now
"`+4` unless it is 0xFF, then `+5`", which is what all eleven maps want, and
`test_boss.c`'s `boss banks` case pins every one of them.

## Stubbed / not yet implemented

* **Boss overlays: all eleven are ported.**  Vista's tentacle-pose *index* and
  Tarso's face-patch slot are the only inferred pieces; everything else — every
  layer table, pose list, column bitmap, path, script and string — is read out
  of the overlay image at run time.  The four that were on `boss_generic.c`
  (DRGN, AKMA, MAO1, MAO2) were written against `ndisasm` listings of the
  images, because the Ghidra output for three of them is unusable; the compose
  routines (DRGN A758, AKMA A7CC, MAO1 A2D3, MAO2 A939) and the record
  emitters were read instruction by instruction, which is where the AKMA face
  patch ("2 columns × 1 row", not the other way round) and MAO1's pose-2 class
  came from.  Only the *animation* is now unverified against the original —
  there is DOSBox ground truth for Cangrejo and (new this milestone) for
  **Paguro**, cavern 7's boss, whose room MP73 is straight through Llama Town's
  column-222 door; the other nine boss rooms still have no town door.
* **The per-boss idle animation hooks** are still skipped (the `[E6]` boss-room
  walk-in is in — see "Corrections found while walking cavern 1").  The part hit bit **is** written now: eight of
  the eleven overlays carry `or byte [si+5],0x20` in their part emitter (CRAB
  `A6BC`, TAKO `A44D`, TORI `A510`, MEDA `A4F5`, LEGA `A501`, DRGN `A644`, AKMA
  `A5D1`, MAO2 `A763`), gated on the overlay's own hit variable, and ZELA, ZEL2
  and MAO1 have no such write.  It is **not** a flash: gfmcga `3713` tests
  `[FF34]` before the palette bump, so nothing flashes in a boss room at all.
  What it does is throttle the blade — `sword_apply` (6F8B) skips a marker whose
  object already has bit 5 — so a boss can be struck at most every other frame,
  which is half the damage the port used to do.
* **The encounter card is in** — `encnt.grp` decoded through gfmcga's own 28x5
  cell map, "ENCOUNTER!" in the two reds on black, flashing twelve half-frames
  starting with the card up (60BC draws it before the loop).  The `[E6]` boss-room
  walk-in (61A8, only mp90 uses it) runs now, including 61DB's hand-off to MPA0.
  (The *shop* `[A002]` hooks are a different thing and are all in; the per-boss
  idle animations still are not.)
* **Shop details.**  The bank's amount entry is simplified to "deposit/withdraw
  everything" (the original's 24-bit Up/Down/Left/Right entry is not ported) and the
  sage's name-entry dialog and `*.usr` file browser are replaced by `--name`.  The
  **shopkeepers' idle animations are in** — all seven `[A002]` hooks, checked
  against DOSBox frame by frame (see "The shopkeepers' [A002] idle hooks").
  omoypro (the Princess' chamber, the ending hand-off) is loaded but only draws
  its picture, which is what the original does too: its `[A002]` word is `A004`,
  the bare `ret` byte in front of its entry point.
* **The dialogue box's yes/no widget** (`74D3`) now runs for both opcodes: `81`
  (the bsmp sentry: Yes → script 12, No → 13) and `89` (llmp's Asbestos cape:
  declined → 6, under 2500 almas → 7, otherwise `[8B] -= 2500`, `[34] |= 0x40`,
  item 5 into `[A1..]`, patches re-applied, script 8).  Only the *geometry* of the
  two boxes is inferred — `6655`/`66AD` build `BX` out of the dialogue box's own
  position with an addend the listing does not disambiguate, so the port places the
  menu beside the box; everything the widget does is the original's.
* **The status screen's hidden LEVEL/EXP panel** is bound to the exact key chord
  `[FF18] == 0x0286` in the original (docs/TOWN.md §12.3).  The port has no
  key-mask model, so it is reachable only through the `K` script token / a third
  button bit.
* **The town backdrop is in** (ympd.bin / ckpd.bin and all three parallax strips,
  see `town.c` above); what is still approximate is the *phase* of the strips, which
  the port derives from `scroll_col` rather than keeping the original's running
  `FF2A` counter — the same value in every situation the captures cover, but a
  restore into a scrolled town would leave the original at phase 0.  The dialogue box
  scrolls by whole lines where the original slides the box up ten single pixels.
* **Magic sprite artwork.**  The spell shapes, motion, timing and damage are the
  originals, but the 24 cells the sprites use (bank slots 0x67..0x7E) are refilled per
  spell by gfmcga 44CE from a table in the *parked* segment, which the port does not
  load; it draws the cavern tile bank's own cells at those slots instead.
* Item states 0x11 (touch trigger) and 0x1D (boss chest) only log; the treasure-box
  gold table is implemented but its `phase` encoding is unverified.  The pickup latch
  is simplified to "collect on overlap".
* The swing length is a 6-frame approximation: the original's `attack_var` is
  driven by gfmcga 3E45, which has not been decoded.
* HUD: the gauge troughs, the LIFE/ENEMY bars, the GOLD/ALMAS digits, the
  narrow-font labels, the place name, the grey panel, the stone frame and the
  item-slot frames are all pixel-exact now — `make verify` diffs the whole
  320x200 screen against three of the captures.  The row of **Tear slots**
  GAME.BIN's `A3A5` draws along the top border through `[203E]` when `[A0]` is
  non-zero is in now (`tear_draw_slots`); no capture shows it, because a fresh
  player has no Tears, so it is covered by `test_cutscene` instead.  The sign
  text is not drawn.
* Music: the MT-32 (`MSCMT.DRV`, blob A) and Tandy/PCjr (`MSCJR.DRV`, SN76496) back
  ends are not implemented — `msd.c` parses both arrangements (the Tandy tracks feed
  the speaker) but only OPL2 and the speaker are synthesised.  `SNDJR.DRV`'s effect
  table is not read.  The `SNDADLIB` proximity/ambient routine at `15A3` (the `FF08`
  entrance-distance sound) is not ported, and neither is the Esc pause box, so
  `INT 60h AX=3` has no caller in the port.  The one game-driven fade is the boss
  death (`FF24 = 0x0A`); a music change at a door restarts the score instead of
  fading it.
* Hit-flash palettes, the hero death animation and the "doorway to the past".
  (The Roka demo — the Tear cutscene — is in; see `tear.c`.)
* Three of gdmcga's slots are approximations rather than ports, because they are
  only used as scene transitions and nothing in `docs/screenshots/` shows one
  mid-flight: `gd_wipe` (3E8B) does not stamp the two picture-box borders into
  the source bitmap first (3FEF), and `gd_end_open` / `gd_end_close`
  (4080/4162) reproduce the width profile and the 57-step converging aperture
  but not the exact `stosb` tail of the 23..27 row band.  `gd_fx_sand` (38E6)
  *is* a full port, tables and all.  The five non-MCGA gd builds
  (`gdega/gdcga/gdhgc/gdtga`) are not read, as for the video drivers.
* EGA/CGA/Tandy render modes (only the MCGA pair-packing path).

## Sound

`--dir`'s `MSCADLIB.DRV` is **not** loaded: the driver is ported, not run.  What is
read from the game directory is the data — the `.msd` scores out of `ZELRES1-3.SAR`
and, for the effects, `SNDADLIB.DRV` / `SNDSTD.DRV` themselves, because the effect
tracks, their OPL patches and their duration tables live inside those files.

**What plays.**  A cavern's score is the one its level record names: byte +0 bits 1-4
index fight.bin's `9E53` request table (`mgt1 ugm1 mgt2 ugm2 mus1..mus8 mbos mmao`),
and the ten town maps use the same field, so Muralla plays `mgt1`, the caverns play
`mus1..mus8`, the boss rooms `mbos`.  The score is (re)started only when the index
changes, as `7E93` does.  On top of it the `FF75` effects play on OPL channels 4 and
5, which the effect driver claims from the music through `INT 60h AX=6` when it loads
a patch and gives back when both of its tracks end — so the music's two top voices
drop out for the length of a sword swing exactly as on a real AdLib card.  Killing a
boss starts the `FF24 = 0x0A` fade-out.

**Timing.**  The audio clock is the original's: the sample counter drives a 236.7 Hz
`INT 8`, the sound driver runs on every tick and the music driver on every second one
(118.35 Hz), and inside the music driver a score step happens on the ticks where
`acc += T` does not carry.  Nothing is tied to the frame rate.

**Verified.**  `--dump-audio` renders to a WAV without a sound card, which is how the
output was checked:

* `--music 4` (mus1, cavern 1) rendered for 135 s and auto-correlated on its
  amplitude envelope peaks at **47.13 s** — `msd2mid.py` reports a 47.1 s loop body.
  `--music 8` (mus5) gives **58.07 s** against 58.1 s, and `--music 12` (mbos) peaks
  at 23.17 s, exactly half of its 46.3 s body, because that body repeats internally.
  That is the whole chain — header, byte-code, tempo accumulator, OPL2, resampling —
  agreeing with the reference parser to about 0.05 %.
* Sound effects land where the requests do: with `--music -1` (effects only) and
  `--script "X4 .20 X4 .20 X4 .20 X4 .40"`, `--sound` logs FF75 = 03 at frames 2, 26,
  50 and 74 and the WAV has four bursts 2.03 s apart (the frame is 84.5 ms, so 24
  frames = 2.028 s), each about 550 ms long, with digital silence in between.
* Pitch: of the 2908 analysis frames of the mus1 render, 93.5 % of the dominant
  partial falls inside the `E2..D6` range `msd2mid.py` reports for that score and
  84.5 % lands on a MIDI note the score actually plays (the rest are FM sidebands).
  Separately `test_audio.c` measures a single OPL2 voice at 440 Hz and checks every
  note-on's f-number against its MIDI note.
* With the music off and no input the dump is **all zeroes** (peak 0), and rendering
  the same script twice is byte-identical.

**A finding about `tools/msd2mid.py`** (reported, not changed — it lives outside
`port/`).  `MSCADLIB.DRV`'s relative-volume opcode (`C0-CF`, at `06B3`) tests the
result with `test bl,0xC0`, so any attenuation above `0x3F` snaps to 0 — fully loud.
That is a bug in the shipped driver: `set_level` uses `attenuation >> 1` as a 6-bit
level, so the intended range is `0..0x7F`, and the STD/JR/MT builds all test `0x80`
instead.  It matters exactly once: zopn (the title music) opens with `E5 4F` and then
fades in with `C0`, and on AdLib the first `C0` jumps it straight to full volume.
`msd.c` reproduces the driver by default; `tools/msd2mid.py` models the intended
`0..0x7F` range, so `msd.c` has a `compat_msd2mid` flag that reproduces the tool and
`test_audio.c` uses it for the diff, with the real behaviour asserted separately.
The other 16 scores are unaffected.

## Playing it without a keyboard

`make playthrough` runs the two routes in `playthrough.c` and prints every objective
as it is reached:

```
[play] step 0: cmap: the King of Felishika (1000 gold)
[play] step 0 done in 128 frames (LIFE 80/80, EXP 0, GOLD 1000, keys 0)
...
[boss] defeated: 4 pokes applied, exit door at column 39, post-boss AI 0 / bank 0
route 2 (caverns 1-3 and their bosses): 2769 frames, 1630607 probe frames, 3 bosses, 15 doors, 3 shops
```

`./test_playthrough ../zeliard [--route 1|2] [-v] [--quiet] [--budget N]` runs them
directly; `-v` adds a position/LIFE/distance line every 50 frames, which is how the
navigator is debugged.  A route entry is
`{kind, a, b, menu, what, c}`:

| kind | meaning |
|---|---|
| `P_TOWN_SHOP` | walk to town column `a`, go in, answer the menus with `menu` |
| `P_TOWN_CAVE` | walk to town column `a` and take the cave door there |
| `P_TOWN_EDGE` | walk off the left (`a`=0) or right (`a`=1) edge |
| `P_CAV_DOOR` | reach the C00A door whose own cell is `(a,b)` and go through it (`a` < 0 = "the door this map has", for the exit door 72F1 creates) |
| `P_CAV_CELL` | reach map cell `(a,b)` — a chest, a Key, a boss reward |
| `P_BOSS` | fight until the boss is defeated and 72F1 has run |
| `P_FARM` | patrol between columns `a` and `b` until EXP reaches `c` |
| `P_GOTO` / `P_TOWN_GOTO` | the shell call a door or a town gate makes, for the legs the navigator cannot reach yet (route 2 has two left: MP20's descent to its boss door and the whole of MP31) |

`menu` is one character per widget the shop opens, in order: `0`..`9` picks that row,
`y`/`n` answers a Yes/No, `c` cancels (which every shop reads as "Go outside").  So
Muralla's `"30yc"` is *Buy shield → the first shield → Yes → leave*.

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

## Ground truth

`make verify` compares 99 boxes of headless renders against the DOSBox captures
in `docs/screenshots/`; all of them are at 100 %, and **eight** of them are the
whole 320×200 screen — including all five intro captures (`intro_prologue`,
`intro_demon`, `title`, `demo_balcony` and `demo_dialogue`), which the port
reproduces pixel for pixel, frame and all.  It then runs
`tools/compare_gdart.py`, which diffs the port's own gd decoders against
`tools/grp2png.py`'s over all 31 intro/ending resources and every one of their
sub-images.  Five of the captures were
taken for this milestone, and getting them is worth writing down because it also
**validated the save format end to end**.

### The `.usr` round trip

`port/` writes `NAME.USR` in kenjpro A862's own format — a raw 256-byte image of
`BASE:0000`, no header — and the file the port writes **loads in the real game
under DOSBox**.  The page carries the hero's town position as well as his
record: `[80]` scroll_col, `[83]` hero_scr_col, `[C2]` facing, `[E7]` walk frame
and `[C4]` the town map, so a restore resumes exactly where the save was taken.
`town_page_push` / `town_page_pull` (town.c) keep the port's `Town` and the page
in step; `--save NAME` writes the file for whatever state the command line sets
up and `--load NAME` restores it, entering the town `[C4]` names at the column
`[80]`/`[83]` name, which is what GAME.BIN A1CB + town.bin's boot entry do.

Loading one in the original:

```
tools/run_dosbox.sh OUT ...           # with the single .usr copied into OUT/dos/zeliard/
KEYS="6:Return 9:Return 16:Return     # recipe A: skip the intro, stand in the castle
      22:F7 24:+y 24.15:-y            # F7 -> "Restore Game / Sure?(Y/N)" -> y
      28:+Down 28.15:-Down            # the file list is {Re-Start, <your .usr>}
      30:+space 30.15:-space          # Space copies the highlighted name into the box
      32:+Return 32.2:-Return"        # Enter accepts; GAME.BIN restarts in restored mode
```

Keep exactly **one** `*.usr` in the directory so the list is unambiguous.  The
restore is done by ≈40 s (the game reloads GAME.BIN and the town map).

### Reaching the deep captures

The restore always lands in a **town** (GAME.BIN A097 loads the map `[C4]` names
and jumps into town.bin), so the trick is to pick a town whose gate or edge
comes out next to what you want.  Satono Town is the useful one: both its edge
exits are cavern entries (`stmp` `[C007]`: flags 0x81 → cave record 0, flags
0x80 → record 1).

| capture | save | in DOSBox |
|---|---|---|
| `cavern2.png` — MP20 "Cavern of Peligro", MPP2 | Satono, column 210 | hold Right 0.5 s: off the right edge → MP20 (6,62) |
| `cavern3.png` — MP30 "Cavern of Madera", MPP3 + two enp5 enemies | Bosque, column 142 | tap Up: the cave gate at column 142 → MP30 (185,19) |
| `boss_cangrejo.png` — MP1D, the Cangrejo fight | Satono, column 5 | tap Left: off the left edge → **MP10 (128,33)**, then walk right 13 columns to the unlocked boss door at (141,32) → MP1D |
| `town_satono.png` — Satono Town, the ckpd underground backdrop | Satono, column 5 | the restore itself |
| `restore_menu.png` — Felishika's Castle with the F7 name box | — | recipe A + F7 |

The scan into MP1D is the edge-stop-and-scan pattern again, but with **1.3 s
between the pairs**: `Up` is *jump* in a cavern and a jump takes about ten
frames, so a scan at the town's 0.45 s spacing spends every input mid-air.
`port/zeliard --map 0 --pos 128 33 --script "R13 .2 U2"` shows the port doing
the same walk, and the port agrees with the original that only columns 141 and
142 open the door.

The captures are `import`ed at 640×480 and reduced with `convert -sample
320x240` — a **nearest-neighbour** reduction.  `-resize` interpolates and turns
every exact-pixel comparison into a ~25 % match; that cost an hour once.

### Every town is a door into the second half of the game

The "only Satono is useful" reading above was wrong, and it was what made
caverns 4-9 look out of reach.  A town map's `[C007]` **exit** records and its
`[C009]` **door** records both index the same `[C00B]` cave table, and *every*
town has cave records — so a `.usr` that names any town map in `[C4]` and any
column in `[80]`/`[83]` puts the hero one edge step or one `Up` away from a
cavern the port had never been checked against.  `port/` writes exactly such a
save; the restore recipe above is unchanged.

| town | how it reaches a cavern |
|---|---|
| Muralla (1) | the gate at column 205 → MP10 (61,7) |
| Satono (2) | left edge → MP10 (128,33) *(the boss shelf)*; right edge → MP20 (6,62) |
| Bosque (3) | column 185 → MP30 (185,19); column 149 → MP31 (149,14) |
| **Helada (4)** | left edge → **MP40** (86,22); right edge → MP41 (16,22) |
| **Tumba (5)** | left edge → **MP50** (94,11); right edge → MP50 (131,10) |
| **Dorado (6)** | left edge → MP61 (31,6); right edge → **MP60** (315,49) |
| **Llama (7)** | column 269 → MP70 (1,22); column 8 → MP70 (152,7); **column 222 → MP73 (27,13), cavern 7's boss room** |
| **Pureza (8)** | left edge → **MP80** (111,21) |
| Esco (9) | column 205 → MP81 (123,6) |

Eleven captures have been taken that way and all eleven are in `make verify`
(and the same trick reaches every *shop* — see "The shopkeepers' `[A002]` idle
hooks", which added seventeen more):

| capture | save | in DOSBox |
|---|---|---|
| `town_helada.png` | Helada, column 86 | the restore itself |
| `cavern4.png` — MP40 "Cavern of Glacial", the ice tileset | Helada, column 4 | hold Left 2.5 s: off the left edge |
| `town_tumba.png` | Tumba, column 4 | the restore itself |
| `cavern5.png` — MP50 | Tumba, column 4 | hold Left 2.5 s |
| `cavern6.png` — MP60 "Cavern of Tesoro" | Dorado, column 210 | the restore lands on the last walkable column, and town.bin takes the right-edge exit itself |
| `town_pureza.png` | Pureza, column 4 | the restore itself |
| `cavern8.png` — MP80 | Pureza, column 4 | hold Left 2.5 s |
| `town_llama.png` | Llama, column 222 | the restore itself |
| `boss_paguro.png` — **MP73, cavern 7's boss "Paguro"** | Llama, column 222 | tap Up: the town door at 222 is a *cave* record and it opens straight into the boss room |
| `cavern7.png` — **MP70 "Cavern of Caliente"**, the lava tileset | Llama, column 8 | tap Up: the column-8 door → MP70 (152,7) |
| `cavern8b.png` — **MP81 "Cavern of Milagro"** | Esco, column 205 | tap Up: the column-205 door → MP81 (123,6) |

So a boss capture no longer needs a long cavern walk at all: Llama's column-222
door is a boss room's front door.  The port reproduces `boss_paguro.png` **from
Llama Town itself** now — `--town 7 --town-col 222`, tap Up, frame 40 — at 99.7 %
of the whole playfield and **100 %** in all six boxes; the residue is the boss
sprite's own animation phase.  The left-wall band that used to differ was a port
bug in how a cave record's row is read, and it is fixed (see "The MP73 entry
column").

The last two maps a town door reaches are captured too, and both reproduce: MP70
at `--map 0x12 --pos 152 7` and MP81 at `--map 0x18 --pos 123 6`, each 100 % over
everything but the sprites at a different animation phase (MP70's sky sparkles,
MP81's three hanging enemies).  Cavern 7 is the heat cavern, so half the frames
of the MP70 run carry `704F`'s "It's too hot !!" box; the capture is one of the
frames in between.  `make verify` is 143 comparisons now.

The only boss door still out of reach this way is MP6D's at (309,41), six
columns from where Dorado Town drops into MP60 but eight rows up and locked.

## What is left of milestone (e), and after

1. **Cavern 1's intended route, found — and two port bugs with it.**  The
   entrance is no longer an island: from the Muralla gate the survey now reaches
   **1073** of MP10's 1542 nodes and, of the six doors, the MURALLA door back out
   **and (159,50) into MP21**.  What was hiding it:
   * **`scroll_right` (68A0) filled the ring's new column from the *old* `[80]`.**
     The original does `inc word [0x80]` *first* and only then `mov ax,[0x80];
     add ax,0x23` for the column it writes into ring column `0x23`; the port
     wrote `scroll_col + 0x23` before the increment, i.e. one map column short.
     Because every later scroll shifts that column inwards, the error spread
     across the whole ring: after walking east the hero's own collision cell was
     the map cell *west* of where `[80]` said he was.  Nothing caught it — the
     start-of-map captures are all taken before any scrolling and the renderer
     reads the same (wrong) ring — but it made the survey graph and the live
     physics disagree everywhere, which is what the navigator kept tripping over.
     `test_physics.c`'s "scroll ring" case walks 40 columns each way and diffs the
     whole ring against the map after every step.
   * **the survey never hung the hero on a ladder.**  `standable()` counts a cell
     with no floor as a node when a ladder is in the hero's body column (which is
     what 62DB's `ladder_step` keeps him on), but the probe placed him there with
     `[FF39]` clear, so gravity dropped him on frame 1 and every such cell looked
     like a hole.  `probe_grab_ladder` now leaves him the way `ladder_mount`
     (65C5) does.  MP10's col-165 and col-112 ladders — the way down from the
     entrance shelf — were entirely invisible before.

   The route the map intends is now readable off the graph, and it is not a walk
   to the boss door at all:

   > Muralla gate → MP10 (61,7) → the long way round to **door 5 at (159,50)** →
   > MP21 (79,50) → MP21's **(15,50)** → back into MP10 at **(95,50)** → the
   > **Key** item at (99,41) → the *locked* door at **(26,15)** the Key opens →
   > MP1D → Cangrejo → the exit door 72F1 puts on the hero's column → MP10
   > **(141,32)**, the boss shelf → the **(128,32)** door → Satono Town.

   That is why MP10's header start is (26,16), the cell beside the locked door,
   and why (141,32) and (128,32) sit on a 32-node shelf nothing else reaches: the
   shelf is where the boss room lets you *out*, not how you get in.  MP20 is built
   the same way — locked door 3 at (171,54) in, unlocked door 4 at (190,47) out
   onto the shelf that carries door 5 to MP30 — and its header start is at door 3
   as well.  `PLAY_ROUTE_BOSSES` now walks cavern 1 for real (Satono's left edge →
   the shelf → (141,32) → Cangrejo → the exit door), and the topology above is
   asserted in `test_playthrough.c`.

   **The whole lap is walked now.**  `test_playthrough`'s "fixture rides" case
   runs the navigator from the Muralla gate to door 5 at (159,50) — about three
   hundred macros, a full lap of the 240-column ring — and it arrives in ~1050
   frames.  What was in the way was not the planner and not, in the end, a
   platform's *phase*: it was **82B4**, the engine's own "is the hero riding this
   platform" test, which the port had a column out ("Corrections found while
   walking cavern 1", below).  A rightward ride therefore walked him off the platform's leading end,
   and the navigator's own `step_walks_off` guard could not catch it because that
   was a column out too, in the same direction: it tested the cell the hero was
   *standing* on rather than the one he was stepping onto, so it never once fired
   on a step off the right-hand end of anything.  MP10's row-0 floor stops at
   column 117 and the fix[5] platform bridges 118-129; every lap the hero walked
   into the gap and fell through the ring wrap.

2. ~~**The remaining town machinery: the shopkeeper idle hooks.**~~  **DONE** —
   see "The shopkeepers' `[A002]` idle hooks" below.
2a. **Route 2's last two `P_GOTO`s, and what is under them.**  Both caverns are
   mapped now; what is missing is executor reach, not knowledge.
   * **MP20.**  The intended way in is Satono's *right* edge -> (6,62), one of the
     two Keys lying in the map, and the **locked** door at (171,54); (190,47) and
     (205,47) are the boss room's exit shelf and are unreachable from the entrance,
     exactly as in MP10, and MP20's header start record is `(171,55)` — its own
     locked door, the same signature MP10 and MP31 carry.  The route walks
     (6,62) -> the Key at (149,44) unaided (about 1200 frames, including a ride
     over fix[5]).  What it cannot hold is the descent from the row-43 Key ledge to
     the row-55 floor in front of the door: the survey's own route runs back west
     to column 106 and down through three of the map's elevators and two more
     patrolling platforms, twenty-five macros with a fixture in nearly every one.
   * **MP31** (system map 6).  Same shape: the locked boss door is **(188,20)**,
     the shelf door is (174,4), and the header start record is `(188,21)`.  The way
     to (188,20) is **Bosque's column-7 door**, which lands at (149,14) — 41 frames
     from the door.  The Key for it is in MP30 (system map 5) at (133,55), reachable
     only through MP31's own (153,43) door, and the way back from there to a door
     that reaches (188,20) is a five-map-transition loop whose last leg (MP30's
     column-4 shaft, rows 47 -> 20, then eight columns left across the ring wrap)
     the executor loses on a single edge.  And Bosque's column-7 door is behind the
     **sentry NPC** standing at column 8 — the `[12] & 8` patch in bsmp's own list —
     who physically blocks the way until that story flag is set; the route has no
     way to set it yet, so even the walkable half is not reachable in sequence.
3. **The rest of the sound**: the MT-32 driver (`MSCMT.DRV` over blob A, which
   `tools/msd2mid.py --mt` already decodes) and the Tandy SN76496 pair
   (`MSCJR.DRV` + `SNDJR.DRV`); `SNDADLIB`'s `15A3` ambient/proximity routine, which
   needs `FF08` (fight.bin `774E`) to be computed first; and the Esc pause box, which
   is the only caller of `INT 60h AX=3`.
4. **More ground truth.**  Caverns 4, 5, 6 and 8, four more towns and cavern 7's
   boss are now captured and in `make verify` (see "Every town is a door into the
   second half of the game" above); no long scripted cavern walk was needed after
   all, because every town's `[C00B]` cave records put a cavern one step from a
   restore.
   `docs/screenshots/menu.png` turned out to be a capture of the **select.bin status
   screen** (Enter in Felishika's Castle on a fresh game), not a shorter town menu, and
   `make verify` diffs the port's own status screen against it.
   MP70 and MP81 are captured now (see the table above), which is every map a
   town door reaches; what is left uncaptured is a town dialogue box and the nine
   bosses other than Cangrejo and Paguro, whose rooms have no town door.
   `docs/screenshots/shop_armour.png` was captured for milestone (d) with
   `KEYS="6:Return 9:Return 16:Return 20:+Right 27.5:-Right"` followed by 26
   `+Right`/`-Right` + `+Up`/`-Up` tap pairs at 0.45 s intervals from 28 s
   (docs/DOSBOX_RECIPE.md §5's edge-stop-and-scan pattern, scanning *right* from the
   Muralla entry to the first door at column 39), captured at 43 s.

## What 72F1's third poke is for: the boss reward, and the second visit

Every `mpNd` boss room hides its reward behind the same trick, and the trick is
why the reward looked unreachable.

A boss room's level record ends with 7351's `{u16 addr, u16 val}` list.  For
MP1D it is four entries:

```
[C010] = C224     the post-boss object list: one 16-byte record, a Key
[C00A] = C1B6     the post-boss door list: one door, on the hero's column
[C226] = FF05     that record's +2/+3: row = 5, rcol = 0xFF
[0000] = FFFF     the story flag "Cangrejo is dead"
```

The `.mdt` image has that same record at row **16**, two rows above MP1D's only
floor at 18 — exactly where a standing hero walks into it.  The third poke lifts
it to row 5, thirteen rows up in empty air, and it is the *only* thing that puts
it out of reach.  MP2D, MP3D and MP5D are poked to 13, MP6D to 5, MP8D to 0;
MP4D, MP73, MP7D and MPA0 carry no reward at all and have no such poke.  So the
poke is not a mistake and the field is a ring row: **72F1 deliberately parks the
reward out of reach for the rest of that visit.**

The last poke is the answer to why.  It writes a byte in the player page's
story-flag area (`[00]` for cavern 1, `[08]`, `[10]`, `[18]`, `[20]`, `[28]`,
`[30]`, `[32]`, `[47]` for the rest — one per boss room, eight apart), and the
room's *own* `[C00C]` patch list (6BFC, applied every time a map is loaded from
disk) keys off precisely that byte:

```
MP1D  if page[00] & FF:  [C010]=C224  [C00A]=C1AA  [C20D]=0000  [C20A]=0009
```

* `[C010]` is the same object list — but this is a *fresh* copy of the image, so
  the Key is back at row 16, on the floor.
* `[C00A]` is `C1AA`, which is `C1B6` minus one 12-byte record: the list now has
  **two** doors, the exit one 72F1 rewrote *and* an ordinary door at (27,14) back
  to MP10 (26,15).
* `[C20A]` rewrites the level record's byte 0 from `99` to `09`, clearing bit 7 —
  the room is not a boss room any more — and `[C20D]` swaps the AI and enemy
  banks for the post-boss pair.

So the room is meant to be entered **twice**.  The first visit is the fight and
ends through the exit door 72F1 puts on the hero's column, onto the shelf outside
(MP10 (141,32), MP20 (190,47), MP31 (174,4)); the second is one step back through
the door that is already standing beside him there, into a quiet room with the
Key on the floor.  Two further `[C00C]` records finish the job: one keyed on the
item's own flag (`page[02] & 08` in MP1D, written by 914C when the Key is picked
up) disables the record so it never comes back, and one keyed on the exit door's
story flag clears that door's `+8` bit 7 so the Tear of Esmesanti cutscene plays
only once.

And the Key is the one the *next* locked door wants: MP10's (128,32) into Satono,
MP20's (205,47) into MP30, MP31's (188,20).  `PLAY_ROUTE_BOSSES` now walks all
three of those chains, and `test_boss.c`'s "boss reward" case asserts the whole
mechanism for MP1D, MP2D and MP3D — fresh (no doors, no objects, a boss room),
after the boss (two doors, no boss bit, the Key at its image row above the floor,
the parked row empty air), and after the pickup (the record retired).

**docs/FIGHT.md's "Unresolved" caveat on this can go** — see "Doc corrections"
at the end of this file.

## The shopkeepers' `[A002]` idle hooks

town.bin's `idle_poll` (7042) ends with

```
0000705A  F606427CFF        test byte [0x7c42],0xff     ; a shop overlay is loaded
0000705F  7501 / C3
00007064  2EFF1602A0        call [cs:0xa002]
```

so every pass of every wait loop in a shop — printing a character, sitting in a
menu, waiting for a key — runs the overlay's own `[A002]` routine.  All seven
that exist are ported now (omoypro has none).  The word at `A002` is the first
thing in each overlay image, so `test_shop` reads it back out of the file:

| overlay | ZELRES2 | `[A000]` | `[A002]` | what it drives |
|---|---|---|---|---|
| kingpro | 10 | A004 | **A302** | the blink (A360's 26-step sequence into A37A's three eye maps) and the lip-sync mouth (A3D4) |
| omoypro | 11 | A005 | A004 | nothing: `A004` is a `ret` byte (`C3`) |
| armrpro | 12 | A004 | **A90F** | the smith's eyes (AAD0) / mouth (AB68) and the A9CF frame table |
| bankpro | 13 | A004 | **A728** | the teller's two 5x8 cell maps at `[AD1F]` |
| churpro | 14 | A004 | **A1D7** | the altar candles (A234) and the priest (A27C), three phases each |
| drugpro | 15 | A004 | **A644** | the bubbling pot, 6x6 cells at (104,23), three phases |
| innapro | 16 | A004 | **A22F** | the innkeeper's blink — the town's only use of `KRN_RANDOM` |
| kenjpro | 17 | A027 | **AB47** | the sage's blink (ABFB/ABFD) and the level-up aura (AA47, sequence ABFF) |

(This table used to read `A3A2` for kingpro.  The overlay's first four bytes are
`04 a0 02 a3`, so it is `A302`; `src/shops.c` had it right.)

**The tick model.**  Each hook is a state machine clocked by `[FF50]`, the second
free-running 236.7 Hz counter: it returns until `[FF50]` reaches its own threshold
(4 for kingpro, 2 for armrpro/drugpro/kenjpro, 0x1E for bankpro, 0x20 for churpro,
0x28 for innapro), then zeroes it and takes one step.  Because `idle_poll` runs far
faster than the tick, the thresholds are wall-clock periods, not frame counts.  One
rendered frame is 4*speed = 20 ticks, so `shop_frame` steps the hook **twenty times,
one tick at a time**, before presenting; the animations then run at the original's
rate rather than the port's frame rate.  `port/shop.h` keeps the overlays' own
variables under their original names.

The hooks are gated by flags the shops' own script opcodes set, so those are in
too: kingpro's op 3 (`A092`, arm the blink) / 4 (`A084`, lip-sync on) / 5 (`A08A`,
off) / 0 (`A0E4`, the twelve-frame face), armrpro's op 3 (`A6CB`, back to the
anvil), 5 (`A716`, the repair) and 9 (`A8FD`), bankpro's entry idle (`A063..A08F`:
five 0x3F-tick passes before the greeting, then off) and its A82F/A839 stand-up
and sit-down lists, drugpro's keeper walking in and out (`A708`), innapro's op 2
(`A114`, the keeper turns in) and kenjpro's raise/lower (`A1D1`/`A200`).

**Ground truth.**  Six of the eight shops had never been captured.  Each was
reached under DOSBox by restoring a `.usr` the port wrote with the hero standing
on the shop door's own column and tapping Up (docs/DOSBOX_RECIPE.md §9), then
captured as a burst of forty screenshots about 0.1 s apart.  Every distinct frame
of every cycle in those bursts is now a `make verify` box, thirty in all, and all
of them are pixel-exact:

* **armrpro.**  `shop_armour.png`, captured for milestone (d), turns out to have
  caught the smith **mid-blink**: the two cells at (128,79) are `AAD0`'s fourth
  pair (0x53,0x54), not the (0x50,0x51) the portrait map has there.  Nobody had
  noticed because the old "armour shop portrait" box stops at y 74.
  `shop_armour2.png` is the same screen 0.3 s later with his eyes open.  Measured
  blink period over five blinks: **1.00-1.01 s**; `A90F` predicts 4 phases x 0x1E
  steps x 2 ticks = 240 ticks = 1.014 s, one quarter of it shut.  The whole
  playfield matches in both phases.
* **churpro** (`shop_church*.png`, Muralla column 59): three phases, measured
  0.40-0.41 s a cycle against `A1D7`'s 3 x 0x20 = 96 ticks = 0.406 s.
* **drugpro** (`shop_drug*.png`, column 111): three phases, whole playfield 100%
  in each.  Phase 0 is the portrait's own cells, which is why `A5E4`'s map has
  `A69C`'s first row in it.
* **bankpro** (`shop_bank*.png`, column 138): both frames of the entry idle *and*
  the third and fourth frames of the `A82F` stand-up, which is what pins the
  entry loop's length (five 0x3F-tick passes) and the list's 0x28-tick step.
* **innapro** (`shop_inn*.png`, Satono column 128) and **kenjpro**
  (`shop_sage*.png`, Muralla column 172): both blink cells.
* **kingpro** (`shop_king*.png`, Felishika column 52): both mouth cells and all
  three eye maps.  Mouth toggle measured 0.10 s against 6 steps x 4 ticks =
  0.101 s.  The blink and the mouth are independent counters, so the port's frame
  27 carries the capture's mouth-shut with the *next* eye map and frame 28 the
  other way round; the two pairs of boxes are crossed on purpose.

innapro's blink and kingpro's blink-restart use `KRN_RANDOM`, whose original is
`[FF1B]`-fed (`svc_random` 0x0918) and the port's is an LCG, so the *sequence*
cannot match — both cells do, and that is all the hook decides.

## The MP73 entry column: a cave record's row is not the hero's map row

`boss_paguro.png`'s one unexplained band, screen x 48-63, was the boss room's left
wall and the corner where it meets the floor; everything above row 38 there is a
uniform wall texture, which is why the mismatch showed nowhere else.  It was never
a camera clamp and never `69CB` either: it was one map column of *hero*, and the
column was wrong because **the port read a cave record's row as the hero's map
row**.  It is not.

Two different routines put the hero on a map, and they scale the row differently:

| way in | `[82]` scroll_row | `[84]` hero screen row | hero's map row |
|---|---|---|---|
| a **door** (fight.bin `7DC1`: `mov al,[0x9f1c] / inc al / sub al,[0xc016]`) | `dest_row + 1 - [C016]` | `[C016]` (`7D2D`) | `dest_row + 1` |
| a **cave record** (town.bin `7005`: `lodsb / sub al,0xa`) | `row - 10` | `[C016]` (`7D2D`) | `row - 10 + row_bias` |

`goto_cavern` hard-codes the 10; `7D2D` (`mov al,[0xc016] / mov [0x84],al /
mov [0x9f00],al`) does not.  So a cave record's row is the hero's map row only on
a map whose `[C016]` row_bias *is* 10 — which every cavern proper is.  **Every
boss room is 12** (MPA0 is 13), and the one cave record in the game that names a
boss room is Llama Town's `(27,13)` for MP73: `13 - 10 + 12 = 15`, which is the
room's floor.  The real game never drops him at all.

The port used to place him at row 13, two rows above the floor, let gravity take
him, and hand him `69CB`'s extra step in his facing on the first frame of the
fall — so he ended at (28,15) with `scroll_col` 12 where the capture has (27,15)
and `scroll_col` 11.  `shell_enter_cavern` now takes a `from_cave_record` flag and
does `row - 10 + row_bias` for the town's own `[C00B]` table; the door path (which
was already right) is untouched.  `make verify` enters MP73 the way the capture
was made — restore into Llama Town at column 222, tap Up — and the six boxes
(the wall, the corner, the floor wall to wall, left of Paguro, right of him and
the HUD name box) are all 100% from that path, with the whole playfield at 99.7%
(the remaining 104 px is the boss's own animation phase).  `test_playthrough`
asserts the record, the row_bias and the cell it lands on.

The port's `gravity()` is faithful to 69CB as it stands (`mov al,[0xff3d]` /
`mov byte [0xff3d],0x7f` / `test al,0xff` / `jnz 69E6`, else `jmp 67C6`), and the
entry really does set `[FF3D]` to 0 at `7E5B` — the extra step was right, it was
the hero who should not have been falling.

## Corrections found while walking cavern 1 (issue #28)

Each of these was checked against `ndisasm` of the extracted overlay before it was
written down; the addresses are the ones to look at.

**`port/physics.c` — `hero_on_fixture` (fight.bin `82B4`) was one column out.**
This is the bug that lost every ride on a patrolling platform, and with it the
whole Muralla→door-5 lap.

```
000082C0  A08400            mov al,[0x84]          ; hero_scr_row
000082C3  02068200          add al,[0x82]          ; + scroll_row
000082C7  0403              add al,0x3             ; the row under his feet
000082C9  243F              and al,0x3f
000082CB  8A6402            mov ah,[si+0x2]
000082CE  80E43F            and ah,0x3f            ; the platform's row
000082D1  3AC4              cmp al,ah
000082D3  F9 / 7401 / C3                           ; different row -> not carried
000082D7  8B04 / 25FF3F     mov ax,[si] / and ax,0x3fff
000082DC  E81900            call 0x82f8            ; BX = its ring column,
                                                   ; AX = 0x21 - that, CF = off screen
000082E2  8A168300          mov dl,[0x83]
000082E6  80C204            add dl,0x4             ; *his* ring column
000082E9  B90300            mov cx,0x3
000082EC  3AD0              cmp dl,al              ; dl, dl+1, dl+2 vs 0x21 - fixcol
000082EE  F8 / 7501 / C3                           ; a match -> CARRIED
000082F2  FEC2 / E2F6       inc dl / loop 0x82ec
000082F6  F9 / C3                                  ; no match -> not carried
```

Written the long way round, but with `6FF9` holding `[83]` at `0x0C` on every
frame outside a boss room — and no boss room carries a C fixture — `DL` is 0x10
and the three matches are platform ring columns 0x11, 0x10 and 0x0F.  Those are
exactly the three positions in which one of the platform's three cells is ring
column `[83]+5`, which is the cell `6D6E + 0x6D` tests for ground under his body
column.  In map terms: **carried ⟺ `hero_map_col + 1` is one of `f->col ..
f->col+2`** — the carry window and the ground window are the same window.

The port had `rc <= hc <= rc+2`, i.e. `f->col <= hero_map_col <= f->col+2`: one
column to the left.  A platform moving right therefore stopped carrying the hero
one column *before* it stopped holding him up, and carried him one column *past*
the point where it still did — so a ride ran him off the leading end.  Nothing in
`make verify` could see it (no capture rides a platform), and it is why the
navigator's fixture edges looked unrepeatable and the "phase" theory looked right.
`test_physics.c`'s "fixtures" case now pins both ends of the window: standing on
the platform's leftmost cell he must be carried, and a platform whose cells stop
short of his support cell must not drag him.

**`port/nav.c` — `step_walks_off` and `hero_carried` had the same off-by-one.**
Not the original's code (nav.c is the autopilot, not a port of anything), but the
same mistake about which cell holds the hero up: both read his own map column
where the engine reads map column + 1.  For a rightward step that made the guard
test the cell he was *standing* on, which always has ground under it, so it never
fired — and MP10's row-0 floor, which ends at column 117 where the fix[5]
platform takes over, dropped him through the ring wrap on every lap.  The guard
now tests `map column + 1 ± 1`, and it lets a *moving* platform count from where
it will be after this frame's 81AE move rather than where it is, because
`fixtures_draw` runs at the top of `frame()` and `nav_step` is called below it
from `present`.

**`port/physics.c` — `scroll_right` (fight.bin `68A0`) was one column out.**

```
000068A0  FF068000          inc word [0x80]        ; [80] first
000068A4  A18000            mov ax,[0x80]
000068A7  052300            add ax,0x23            ; ... then the new column
...
000068C8  BF23E0            mov di,0xE023          ; into ring column 0x23
```

The port bumped `scroll_col` *after* fetching `scroll_col + 0x23`, so ring column
0x23 got the map column one to the west; every later scroll shifts that column
inwards, so the whole ring ends up one column behind `[80]`.  `scroll_left`
(`66F8`) was already right — it does `dec word [0x80]` first too.  Nothing in
`make verify` could see it (every capture is taken before the map has scrolled,
and the renderer reads the same ring), but it made the hero's collisions read the
cell west of the one `game_hero_map_col` reports, which is why the navigator's
graph and the live physics disagreed all over MP10.  Fixed, with
`test_physics.c`'s "scroll ring" case as the guard.

**`[FF2A]` is not the parallax phase.**  town.bin `6157`:

```
00006157  A08000            mov al,[0x80]
0000615C  D1E0 D1E0 D1E0    shl ax,1 x3            ; scroll_col * 8
00006162  0517C0            add ax,0xC017
00006165  A32AFF            mov [0xff2a],ax
```

and it moves by `±8` at `67DC`/`6856` exactly when `[80]` moves.  So `[FF2A]` is
the running pointer to the leftmost visible column of the town's tile stream
(8 bytes per column, `C017` is the stream's base), it is re-derived from `[80]`
every time a map is set up, and it can never diverge from `scroll_col`.  It is
used as a *pointer* — `add bx,[0xff2a]` at 6264/6305/6790/6803/6987 — never as a
pixel offset.

**The parallax phase *does* diverge, and it is not `scroll_col`.**  The three
backdrop strips are rotated in place in video memory by the gtmcga vectors
`GT_SCROLL_FAR_*` / `GT_SCROLL_NEAR_*` (`3677`/`36F1`, `3628`/`36A4`), 4 / 8 / 16 px
per scrolling step.  There is no counter behind them: the phase is however many
steps have been rotated since the *backdrop painter* last drew them, so it starts
at **0** whenever a map is set up.  That is the same as `scroll_col * step` for a
hero who walked in from the left edge — which is every capture the port had — and
different after an F7 restore into a town that is already scrolled.  Measured:
restoring into Helada Town at column 86 (`scroll_col` 69), the port's strips were
52 / 104 / 96 px ahead of the original's, i.e. exactly `scroll_col * {4,8,16}`
mod 112, and the real phase was 0.  `Town.back_steps` now carries the count and
`town_render` uses it; `--town-scr`, which exists to reproduce a *walked-to*
capture, sets it to `scroll_col`.  Three of the new `make verify` boxes are the
strips of a restored town.

**The `[E6]` boss-room walk-in (fight.bin `61A8`/`61BE`/`61DB`) is in.**  MP90
(system map `0x1D`) is the one level whose record has bit 6.  The main loop puts
the camera at `scroll_col = 0x29` with `hero_scr_col = 5`, refills the ring and
hands the frame to the boss overlay, which walks Alguien's intro and clears
`[E6]` (`boss_mao1` `A36A`); the loop then loads system map `0x1E` (MPA0) and
places the hero at `(0x18,0x0D)` for the last fight.  `game_boss_room_intro()`,
`shell_frame`'s hand-off and `test_boss.c`'s "[E6] room" case cover all three
steps; before this the port set `[E6]` and then simply ran the ordinary `7C6E`
walk-in and never loaded MPA0.

**The navigator's node model** (not the original's, but worth recording).  A node
is a cell; the state the survey probed from is not.  Four things that made an
edge unrepeatable in a live run are now handled: the hero must be facing the way
`game_place` left the probe (6824 spends a frame turning, and 65C5 only mounts a
ladder on the side he faces), he must not be inside 6B41's two-frame post-fall
crouch (a crouch swallows the step), the walks-off guard must not veto a move the
probe *measured* as a drop, and an edge the live run fails to reproduce three
times is dropped.  What is still missing is a patrolling platform's phase.

## Corrections found while walking cavern 2 (issue #28)

Two more navigator bugs, both of them the same shape as the cavern-1 ones: an
executable-by-construction graph that the *executor* could not reproduce.  They
are `port/nav.c` only — the engine is right in both cases.

**Walking across a moving platform outruns it.**  `nav_step`'s "riding a
patrolling platform" clause stepped the hero in the platform's own direction as
soon as its trailing cell reached `bc - 1` — one column early.  He then kept
stepping, a column a frame, while the half-speed platform moved a column every
*other* frame, so he walked off its **leading** end.  The survey cannot catch this
on its own: it probes every column the platform can reach with the platform under
that column, so all eleven columns of MP20's row-57 gap (62..72, fix[5]) are
perfectly good nodes with ordinary walk edges between them, and the field is happy
to walk straight across.  Two changes: the trailing guard now fires at `f->col ==
bc`, and while he is carried with nothing but gap on either side the hook stands
still and lets the platform do the work — which is what a player does.  Both are
suspended the moment there *is* ground of his own within a step, so stepping off
at the far end is still the planner's decision.

**`edge_ready` refused the step off a platform because the platform had turned
round.**  A fixture edge remembers the direction the probe had the mover going,
and rightly so for a ride: the macro was run with it moving that way.  But it also
gated the step **off** the platform onto solid ground, and a patrolling platform
reverses exactly at the far end of its patrol — which is where that step is.  The
hero rode MP20's fix[5] to column 70, the platform turned, every outgoing edge
went not-ready in the same frame, and the trailing guard dragged him back across
the gap; he did that for ever.  `edge_ready` now ignores the direction bit when
the node the edge lands on is held up by the map itself (`grid[ncol+1][nrow+3]`
not passable): the platform only has to be under him for the first frame.

With both in, MP20's gap crosses in **29 frames** and the whole Satono -> Key at
(149,44) leg walks in about 1200.

**`shell_enter_cavern` read a cave record's row as the hero's map row.**  See "The
MP73 entry column" above: town.bin `7005` hard-codes the 10 that fight.bin `7D2D`
does not.

## Corrections found while porting the cutscenes (issue #30)

Everything below was checked against `ndisasm` of the extracted overlay before
being written down; where a decompilation and the disassembly disagree, the
address is quoted.  This list is the record of what the port found; **every
item on it has since been folded back into `src/opdemo.c`, `src/enddemo.c`,
`src/rokademo.c`, `docs/CUTSCENES.md`, `docs/STATE_PAGE.md`,
`docs/VIDEO_DRIVERS.md` and `tools/grp2png.py`**, so the reference sources and
the docs now say what is below, not what they said when it was written.

**`docs/CUTSCENES.md`**

* §2.1 / §6.3 — `hou.grp`'s geometry is **`4 x (6 x 32)` then `4 x (4 x 24)`**,
  not `4 x (32 x 6)` / `4 x (24 x 4)`.  The sprite table at gdmcga `3617` is
  read as `mov cx,[bx+0x3619]`, i.e. the *word at +2*, so `CL` = byte +2 = rows
  and `CH` = byte +3 = width in plane bytes; every copier (3596, 35B1, 35CC)
  then uses `CH` as the byte width and `CL` as the row count.  Decoded that way
  the four large frames are a clean expanding ball of light, so they are
  radiating orbs, not "128 x 6-px lightning bolts".  (`tools/grp2png.py`'s
  `GD_ART` had the same error; both tables now agree, and
  `tools/compare_gdart.py` diffs all 31 entries.)
* §2.1 — `gd_storm` patches **three** bytes (`4289`, `428A`, `428B` = the whole
  R,G,B of colour 0 of palette record 0), not just `[0x4289]`, and it calls
  `gd_set_palette(0)` **once per record, nine times a pass**, stepping the flash
  index each time.
* §1 — slot `3022` `gd_draw_masked` is **not** "like 3004": the packing is, but
  the blit is a private 8-pass, 13-rows-per-pass interlace confined to the fixed
  288 x 104 picture window at screen (16,16), and it *clears* every part of that
  window the picture does not cover.  That is how the storm demo wipes
  `waku.grp`'s inset between pictures.
* §1 — the `AL` argument of `3002`/`3004` is undocumented: `AL == 0` runs an
  additive OR dissolve **and then** the copy dissolve (16 passes), `AL != 0`
  only the copy one (8).  `301A` forces `AL = 0`.
* §1 — every draw slot packs into a second 64 KB staging segment at `CS+0x3000`
  before blitting; only the `CS+0x2000` scratch is documented.
* §1 — `3024` `gd_picture_box` also writes a 7-byte, colour-`0x02` dithered
  shadow immediately left of the box on every row (`3E1C`).
* §2.3 — the right speaker's **eye** frames are drawn at `BH = 0x33` (x = 204),
  not (200,56); `6C69` is `mov bx,0x3338`.
* §2.3 — the script at `79C6` also contains `0xA0` (at `7C00`) and `0xA2` (at
  `7DE3`), which the documented opcode list does not mention.  Traced through
  `6B2B` they are exact no-ops that still cost one 0x10-tick delay — dead
  lip-sync codes for a talking head this scene never puts up.  (In *enddemo* the
  same engine really does dispatch `0xA0`, `0xB0` and `0xC0`.)
* §2.3 — a `0x8n`/`0x9n` lip-sync byte costs **no** ticks: `6C53`, `6C74`,
  `6CA0` and `6CC1` jump back to the fetch at `6A80`, skipping the
  `wait_ticks(0x10)` at `6A7B` that every other byte pays.
* §2.4 — the three scrollers are three separate routines and the epilogue's
  (`6D04`) is different: its window is `(0,20) 320 x 160` and it flushes
  **0xA0** rows at the end, not 0x78.  The flush count is the window height.
* §4 — enddemo's act 1 is **not** "nine still tableaux, no text".  `6318` is a
  second, five-speaker copy of the narration engine, called seven times
  (`60FD 6142 6172 61FA 622F 62A6 62DB`) — exactly where `src/enddemo.c` writes
  `beat()` — and it plays the script at `6AA8` ("At long last, Jashiin was
  destroyed …").  Its metric tables are at `807D` / `80DD`.
* §4 — the credits opcode `FB` takes **row then column** (`679E` stores `AL`
  into `[6968]` = row and `AH` into `[6967]` = column), not "c r".
* §5 — `[FF26]` is the music driver's "score finished" flag, not the Return key:
  `A371` waits for the *fanfare to end* and then stops the score.  There is no
  keypress wait in the Tear cutscene.
* §5 — there are **ten** hero frame maps at `A435`, not nine (`A435 + 10*9 =
  A48F`, which is `wait_frame`'s first instruction; frame 9 is the "sword fully
  raised" pose `[E7]` is left on at `A0C8`, and it uses cell 0x35, the last cell
  in `dman.grp`).
* §5 — `[3024] GF_DRAW_SWORD` does **not** draw `itemp.grp`: it draws one of
  three 96-byte 2-bpp pictures embedded in `gfmcga.bin` at `4A31` / `4A91` /
  `4AF1`, chosen through the six-entry pointer table at `4A25`.  `[3022]`'s
  blit is fully opaque — there is no mask.
* §8 — three of the four "not decoded" items are now decoded: `gd_tile_map`'s
  tile arithmetic (`tile n` = one byte x 8 rows at `(n/40, n%40)` of the 40 x 40
  bank, assembled into a 34 x 200 three-plane picture in the scratch),
  `gd_draw_ornament_row`, and `gd_fx_sand`'s three tables (`3C16` the mode pair,
  `3A5F` twenty-four 8-byte patterns, `3B1F` the 196-byte border script and
  `3BE3` the 51-byte inward spiral).  The tails of `himp.grp` and `seip.grp` are
  identified too: they are enddemo's own `0x8n` / `0xBn` lip-sync banks
  (7 x 24 frames of 504 bytes and 9 x 24 / 10 x 24 frames of 648 / 720 bytes at
  `arena:98C0` and `arena:A7F0`).  Both `GD_ART` tables now carry them, so
  `--gd-art himp.grp` / `seip.grp` render the lip-sync frames too.

**`src/opdemo.c`** — the call order, resources, palette records and tick counts
are all right; the arguments are not.  `BH`/`BL` are transposed in ten calls
(`6681 66A1 66C6 6776 680F 6822 682D 6840 688D 68C5 68D0 68E3`; e.g. `680F` is
`mov bx,0x0A15`, so `BH = 0x0A`), the two `GD_FACE_EYES` calls at `618B`/`619F`
load `AL` directly and so are frames **2 and 3**, not 1 and 2, `6255`/`6280`
unpack from `CS:B000` / `CS:A000` (not the arena), `scroll_block` is flattened
into one routine (see §2.4 above), the `0x8n`/`0x9n` wait is wrong (see §2.3),
and `6E0F`'s demon-face assembly is wrong in three ways: the writer at `6E4F`
**swaps** the two source planes into the destination, the mouth goes into planes
0 **and** 1 (not plane 0 only), and it comes from `arena:9C40` — mouth frame
**1**, not frame 0.

**`src/enddemo.c`** — `beat()` is `play_narration()` (above), and `BH`/`BL` are
transposed in four calls (`613D 616D 6855 688C`).

**`src/rokademo.c`** — nine frame maps instead of ten, `[FF26]` described as the
Return key, `if (++tears > 9)` where `A041` is `cmp ..,9 / jc`, i.e. `>= 9`, and
the `[3022]` header comment claims a mask.

**`docs/STATE_PAGE.md`** — `[A0]` (Tears of Esmesanti collected, 0..9) is not
documented at all, and the `[FF75]` table stops at `0x19`: the Tear cutscene
uses `0x1A` (footstep), `0x1B` (flash) and `0x1C` (flight sparkle).

**`docs/VIDEO_DRIVERS.md`** — the `[203E]` Tear row is marked "uncertain" with
eight x4 values; it is nine (`A3D3`: 0F 3D 15 37 1B 31 21 2B 26) and the ninth
uses `AL = 1`, a different picture.

**`docs/screenshots/intro_art.png`** — this contact sheet cannot be matched
pixel for pixel by either decoder: an exhaustive search of every one of the 31
resources and each of their sub-images, at scale 1 and 2, over every position in
the 640 x 480 image finds no exact placement.  It appears to have been assembled
by hand from renders that are not reproducible from the current `GD_ART`
geometry, so `make verify` checks the C decoder against **its generator**
(`tools/grp2png.py`) over all 31 resources instead, and against the five DOSBox
intro captures over the whole screen.

## Doc corrections found while finishing issue #28

Everything below is outside `port/` and has **not** been edited; each was checked
against `ndisasm` of the extracted overlay or against the map images themselves
before being written down.

**`docs/FIGHT.md` §"Cavern layout, and the post-boss reward"** — the caveat
("either that field is not a ring row, or the reward is collected some other way.
Unresolved.") is now resolved and should be replaced.  `72F1`'s third poke *is* a
ring row, and putting the reward out of reach is its whole purpose: the room's own
`[C00C]` list, keyed on the story-flag byte `72F1`'s last poke sets, gives the same
object list back at the `.mdt` image's own row — on the floor — the next time the
map is loaded, in a room whose level record has lost its bit 7 and whose door list
has grown a second, ordinary door.  The boss room is entered twice.  Full write-up
in "What 72F1's third poke is for" above; the flag bytes are `[00]`, `[08]`, `[10]`,
`[18]`, `[20]`, `[28]`, `[30]`, `[32]`, `[47]`, one per boss room.

**`docs/FIGHT.md`, same bullet** — "left through the door `72F1` creates on the
hero's column, which lands on a shelf nothing else reaches" is right as far as it
goes, but the shelf's *other* door is the way back in for the Key: MP10 (141,32),
MP20 (190,47), MP31 (174,4).  And the Key each room hands back is the one the next
locked door wants — MP10 (128,32) into Satono, MP20 (205,47) into MP30, MP31
(188,20) — which is the chain the whole first third of the game is built on.

**`src/fight.c` `0x72F1`** — "mp1d's poke list also writes 0xFFFF to the player
page `[00]`/`[01]`" reads as a curiosity; it is the boss-defeated story flag, every
boss room has one, and `6BFC` keys the room's own patch list off it.

**`docs/STATE_PAGE.md`** — the page table starts at `[49]` ("Player record
(BASE:0049-00E8)") and `[00]..[48]` are not described at all.  They are the
**story-flag array**: written by door records (`7B25` from `+9/+B`, and `7E39`
when a key opens a locked door), by item pickups (`914C`, from the C010 record's
`+B/+D`) and by `72F1`'s poke list; read by every map's `[C00C]` patch list
(`6BFC`) and by door `flag_ptr`s.  A worked example is MP10's list, where
`page[03] & 80` rewrites door 0's letter byte to `81` and `page[03] & 40` does the
same for door 3 — which is how a locked door stays unlocked once a key has opened
it.

**`src/fight.c` has no decompilation of `82B4`** (the "is the hero riding this C
fixture" test that `8271`/`828C` call).  It is worth adding, because it is written
in a way that is easy to get wrong — see the correction above — and the port had it
a column out for four sprints.

**`src/shops.c` `kenj_ritual_lower` @`A200`** — the comment says the frames are
`{2,1}`.  The routine is `mov si,0xa1fe / mov al,[si] / dec si`, and `A1FD..A1FF`
hold `00 01 02`, so it plays **`{1,0}`** — frame 1 then frame 0, the reverse of
`A1D1`'s `{0,1,2}` minus the top of the arc.

**`docs/TOWN.md` §3, the per-map table** — bsmp's cave entries read
`(185,19,0,MP31) (149,14,1,MP3D)`.  The second record's map byte is the same
system map as the first pair's partner, i.e. **MP31** (system map 6), not MP3D
(system map 7): map 6's door at (149,13) is the one whose destination is Bosque
column 7, and MP3D is only reachable from map 6's (174,4) and (188,20).  Either
that cell or `docs/RESOURCES.md`'s "6 MP3D" naming is wrong; the door lists in the
`.mdt`s are the authority.

**`docs/TOWN.md` §3 and `docs/ARCHITECTURE.md`** — nothing anywhere records that a
`[C00B]` cave record's row is **not** the hero's map row.  town.bin `7005` is
`lodsb / sub al,0xa`, a hard-coded 10, while fight.bin `7D2D` sets the hero's
screen row from the map's own `[C016]` row_bias; the hero's map row is therefore
`row - 10 + row_bias`.  It only ever matters for Llama Town's `(27,13)` record for
MP73, the one cave record that names a boss room (row_bias 12, so he lands at row
15), but it is worth a line in the `[C00B]` description — see "The MP73 entry
column" above.

**`docs/DOSBOX_RECIPE.md` §8 and §9.1** — the screenshot table and the "that is
how ... were captured" list should gain `cavern7.png` (MP70 "Cavern of Caliente",
from a Llama Town save at column 8, tap Up) and `cavern8b.png` (MP81 "Cavern of
Milagro", from an Esco village save at column 205, tap Up).  Both were taken with
the §9 restore timeline plus `44:+Up 44.3:-Up 47:+Up 47.3:-Up` and captured at
53 s.  Worth adding to §9.1's gotchas: **cavern 7 is the heat cavern**, so `704F`
puts "It's too hot !!" over the top of the playfield every 64 frames for 32 of
them — take the capture in one of the gaps, or the message box covers the
tileset.

**`docs/DOSBOX_RECIPE.md` needs a §9.3, "reaching a shop"** — the same `.usr`
restore that reaches a cavern reaches any shop: write the save with the hero on
the shop door's own column (`port/zeliard --town N --town-col C --save NAME`),
restore it with F7, and tap **Up**.  The doors are in docs/TOWN.md §3's table;
Felishika 52 is the king, Muralla 59 the church, 111 the drug shop, 138 the bank,
172 the sage, Satono 128 the inn.  `shop_church*.png`, `shop_drug*.png`,
`shop_bank*.png`, `shop_inn*.png`, `shop_sage*.png` and `shop_king*.png` were all
taken that way with
`KEYS="6:Return 9:Return 16:Return 20:F7 21.5:y 23:+Down 23.2:-Down 24:+space 24.2:-space 25:+Return 25.2:-Return 30:+Up 30.3:-Up"`
and forty captures 0.05 s apart from 33 s (the harness stretches those to about
0.1 s of real time each, which is fine — read the real spacing off the file
mtimes).  `shop_armour2.png` used the milestone-(d) walking recipe instead,
because Muralla's armour shop is the first door a held Right reaches.
