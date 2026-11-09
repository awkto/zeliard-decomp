# Widescreen mode: design study (issue #126)

Status: design only, nothing implemented.  All citations are to `main` at 7ecb412.
`port/x.c:N` is the port; four-digit hex addresses are fight.bin (`disasm/overlays/fight.asm`)
unless a file is named (gfmcga = `disasm/overlays/gfmcga.asm`).

The ask: an opt-in mode that shows more cavern columns and widens the enemy activity window to
match, so the extra width is live.  It changes gameplay, so it is off by default and outside the
oracle checks.  With it off, everything must stay byte-identical.

## 0. Summary

* The sim already has a wider-than-screen world: the **ring** is 36 columns, the screen shows 28 of
  them (ring cols 4..31) and enemies already exist, move, shoot and collide in all 36.  The 4 spare
  columns each side are exactly the margin a wide view wants.
* A **16:10** picture at the original scale and with the original 48 px stone sides is 36 playfield
  columns, which is the current ring.  That is a presentation-only first tier with no sim change.
* Anything wider (16:9 = 41..42 columns) needs the ring itself wider.  The ring stride `RING_W` is a
  compile-time constant in ~120 places, the hero's ring column 0x11 and the edge columns
  (1, 2, 3, 0x1F, 0x20, 0x21, 0x22, 0x23) are baked into the AI overlays.  This is a mechanical
  but large refactor: make them functions of one runtime width, equal to today's constants when off.
* Horizontal AI ranges are mostly hero-relative already; only ring *presence* and the edge rules are
  absolute.  So "live extra space" is mostly free once the ring is wider.  Aggro ranges stay as they
  are (a separate, optional tuning knob).
* Boss rooms: keep the original 28-column arena, centred and pillarboxed (section 3).
* Not a risk, contrary to the brief: `FF08` is a boss-door distance cue, not a window test (2.7).

## 1. The window

| Thing | Value | Source |
|---|---|---|
| Screen | 28 x 19 cells (`SCREEN_COLS`, `SCREEN_ROWS`); row 19 is under the HUD, so 18 are visible (`PF_H` 144) | `physics.h:13-14`, `render.h:13-15` |
| Playfield pixels | x 48..271, y 14..157 in the 320x200 frame; `PF_X` 48, `PF_Y` 14, `PF_W` = 28*8 = 224 | `render.h:11-13`, VIDEO_DRIVERS.md `vid_clear_playfield` |
| Ring | 36 x 64 cells, row-major, stride 36, `RING_SIZE` 2304, at `E000`; ring row == map row; rows wrap mod 64 | `physics.h:10-12`, FIGHT.md section 2 |
| Window into the ring | `win = (scroll_row & 0x3F) * 36` (`FF31`, set at `6CE0`); screen col `sc` is ring col `sc + 4` | `physics.c:17,23`, `FIGHT.md:40` |
| Margin | ring cols 0..3 and 32..35, 4 each side, always filled from the map | `ring_fill` `physics.c:88-99` (6C98) |
| Horizontal scroll | `scroll_col` (`[80]`) is the map column in ring col 0.  `scroll_left` shifts the ring by one byte (`memmove`) and refills col 0 (66F8); `scroll_right` refills col 0x23 *after* bumping `[80]` (68A0).  Whole-cell steps only | `physics.c:114-137` |
| Hero pin | `hero_scr_col` = 12 (`0x0C`), set at map entry (`physics.c:1206,1221,1369`) and restored every frame outside boss rooms by `6FF9`: `if (hero_scr_col != 12) { try_move_left; hero_scr_col++ }`.  He is a 3x3 sprite at screen cols 12..14, body column 13 = **ring col 17 (0x11)**.  In boss rooms the target is `boss.cam_col` (5..12 per boss) instead (`6FD3`) | `physics.c:937-939`, `boss.c:68` |
| Hero ring cell | `hero_cell = win + row*36 + hero_scr_col + 4` (6DB1) | `physics.c:18` |

So the hero has 12 columns of screen to his left and 13 to his right; in the ring, 16 and 17
(including the margins).  Every hero-relative AI constant uses ring col 0x11 (section 2.1 row 4).

The screen "shadow" (`E900`, 28x19) is a render-only artefact; the port emulates only its sprite
overlap rule (`render.c:266-352`, an `ff[]` array of stride `SCREEN_COLS`) and has no shadow.

## 2. Activity: every place a position is tested against the window or the ring

Legend for risk: **0** nothing to do, **1** trivial, **2** moderate (needs care or a test),
**3** high (gameplay-visible or touching a fidelity-critical quirk).

### 2.1 Who exists and acts (enemies, `enemy.c`)

| # | What | Where | Test | Widening | Risk |
|---|---|---|---|---|---|
| 1 | The per-frame pass.  A record is *active* iff its map col maps into the ring; otherwise it is skipped entirely, keeps its `col`/`row` and just sleeps (`rcol = 0xFF`).  There is no despawn: leaving the ring is "dormant", not removal | `enemies_update` `enemy.c:446-480` (8D19); `ai_map_col_to_ring` `ai.c:36-43` (96A1: `d = col - scroll_col`, wrap, `d >= 36` is off) | `d >= RING_W` | This is *the* activity window.  Making the ring wider widens it automatically.  Records' `rcol` is a `uint8_t`, fine up to ~250 | 1 |
| 2 | Respawn of a dead record (timer sticks at FF, then 94FF is tried every frame) | `enemy_spawn` `enemy.c:361-405` (94FF) | home col must be in ring; `r == 0 || r == 0x23` refused (`:368`); not on screen: `dy < 0x18 && r >= 3 && r < 0x20` refused (`:370`), i.e. only ring cols 1, 2 and 0x20..0x22 (and any row 24+ below the window top) may spawn; and a 3x3 (tall: 3x5) free of sprite markers | `0x23` becomes `RW-1`; `3` stays 3; `0x20` becomes `VC+4` (first column right of the view plus the 4 margin).  The spawn band stays 2-3 columns outside the view, so pop-in remains off screen.  **Quirk to keep**: the rcol is stored before the free test (`:380`) | 2 |
| 3 | Enemy movement edges: step right refused at `rcol >= 0x22`, left at `< 2`, vertical at `0`/`0x23` | `ai_step` `ai.c:119-124` (vec 2); tall steps `ai.c:206,214` (`rcol < 1 || > 0x22`, `>= 0x22`, `< 2`) | `RW-2`, `RW-1`, 2, 1 | Per-overlay copies of the same edge rules in the ported overlays: EAI4 `A5AF`/`A606`/`A705` (`ai_eai4.c:115,122,240`: `>= 0x21`, `< 3`), EAI5 `A667`/`A6E3` (`ai_eai5.c:69,77`: `>= 0x20`, `< 4`).  Those are shot-spawn guards ("the muzzle cell must be 3 off the edge"), also `RW`-relative | 2 |
| 4 | Hero-relative position: ring col 0x11 is the hero body.  Chase direction, standoff distance, wake range | `ai.c:186-190` (shared `ai_hero_dirx`), `ai_eai1.c:38,66-73,124`, `ai_eai2.c:74-82`, `ai_eai3.c:24,102`, `ai_eai4.c:87,171`, `ai_eai5.c:48,120,188-197`, `ai_eai6.c` (via `ai_hero_dir`), `ai_eai7.c:53-61,114` | `rcol - 0x11`, `0x11 - rcol`, `<= 0x10` | `0x11` becomes `HB = HC + 5` (17 when off).  A purely mechanical `sed` into one inline `ai_hero_rcol(g)`.  Leave the `uint8_t` wraparound idioms (`(uint8_t)(rcol-0x11) < 10`) alone, only change the constant | 2 |
| 5 | Horizontal aggro ranges (all hero-relative, so unchanged by a wider ring): EAI1 bat wakes at rcol `0x0B..0x1A` (6 left, 9 right of the body, `ai_eai1.c:38`, A2A5); EAI3 `dx < 10` (`ai_eai3.c:173`, A6B7); EAI8 `dx < 15` (`ai_eai8.c:38`, A2FC+); shooters EAI4 A6BB and EAI6 A96F fire only with `rcol 8..0x12` (`ai_eai4.c:185`, `ai_eai6.c:186`).  Everything else is row-gated only: `ai_hero_dir(g, e, range)` compares *rows* (`ai.c:182-192`) | as listed | | **Recommend leaving them.**  An enemy 20 columns away in the extra space then patrols, falls, turns at walls and animates (it is live) but only attacks inside its original reach.  Scaling aggro by the extra width is an optional later knob (step 8), it changes difficulty | 0 |
| 6 | Hero/enemy contact and the sword probe | `hero_enemy_contact` `enemy.c:125-230` (751F), `combat.c:252-334` (6E99/74xx) | all relative to the hero cell via `game_hero_cell`, or ring deltas from a marker | Follows `hero_scr_col` automatically (`enemy.c:133,218` use `hero_scr_col + 4`/`+3`) | 0 |
| 7 | Marker placement: each active sprite writes `0x80\|i` into its ring cell and saves the cell under it (`under_sprite[]`) | `enemy.c:455-469`, `physics.c:104-111` (`remark_column`: after a scroll the new column's records get markers back) | ring col | Follows the ring | 1 |

### 2.2 Which cells are drawn and animated (render, `render.c`, `enemy.c`)

| # | What | Where | Widening | Risk |
|---|---|---|---|---|
| 8 | Background tiles: `sc` from 0..27 (or `-wx` extension for smooth scrolling, +/-1 col from the 4 margin) from `bg_ring` | `render.c:625-644` | `SCREEN_COLS` -> `VC` | 1 |
| 9 | Enemies: sorted into scan order, clipped at `sc -1..28`, the `ff[]` overlap array is `(19+3) x SCREEN_COLS` | `render.c:297-352,358` (3468, 34E3, 33E3 edge routines at sc 0..26 / 27 / -1) | The right-edge routine "34E3 = col 27" becomes `VC-1`, array stride `VC`.  Pure parameterisation | 2 |
| 10 | Shots (ring col 4..0x1F only), sparks (`c >= 27` clip at `boss.c:257`), spell sprites (cx -1..28) | `render.c:366-410` (8366), `render.c:413` (379B), `render.c:386-408` (896E) | `VC` | 1 |
| 11 | Cell animation (caverns 5..8): only ring cols 4..0x1F, rows 0..0x11, row-major; cavern 6 consumes `krn_random` in that order | `cell_anim` `enemy.c:549-568` (302C/315C) | Must cover the *visible* extra columns (otherwise water/lava stops animating at the old edge).  In wide mode the RNG stream differs from off anyway, so no fidelity cost, but **off must keep the exact loop** | 2 |
| 12 | Smooth scrolling `pos_of`: `ok` iff `sc -4..SCREEN_COLS+3` | `smooth.c:43` | `VC+3` | 1 |

### 2.3 Hero, fixtures, doors (physics, `physics.c`)

| # | What | Where | Widening | Risk |
|---|---|---|---|---|
| 13 | Hero pin 12 at `physics.c:939,1206,1221,1369` | 6FF9, 7D28 | `HC = 12 + E` (E extra columns each side).  `scroll_col` at entry = hero map col - HC - 4: find where it is computed and make it `HC`-aware | 2 |
| 14 | Scroll refill: `scroll_right` fetches `scroll_col + 0x23`, writes ring col `0x23`; `remark_column` | `physics.c:134-137` (68A0/68FF) | `RW-1` | 2 |
| 15 | Fixtures (elevators A/B, platforms C): `fixture_rcol` returns -1 when `d > 0x21` (82F8); the C erase handles ring edge cases `d > 0x25`, leftmost `>= 0x22`; `hero_on_fixture` `0x21 - rc` (82EC); `fixture_cell`-writes use `rc + k < RING_W` | `physics.c:655-660,741-756,697-698,673,835` | Every literal is `RW-3`, `RW-1`, `RW-2`.  Careful: the comment at `:683-686` documents that DL = 0x10 and the three matched columns 0x11,0x10,0x0F are hero-relative; they become `HB`-relative | 3 |
| 16 | Door arches: `door_view_col` returns -1 for `d > 0x27`, `c > 0x27` (the arch is 5 cells wide, drawn from col `vc-4`; `ncols = 40 - vc`) | `physics.c:610-634` (78DD) | `0x27` -> `RW+3`, `40` -> `RW+4` | 3 |
| 17 | Collision reads: `try_move_left/right`, `floor_under_hero`, ladders: all via `game_hero_cell` +/- ring deltas | `physics.c:166-215,911-1054` | Stride only (`RING_W`) | 1 |
| 18 | Hero-relative map columns `scroll_col + hero_scr_col + 4/5/3` used by nav (autopilot), fixtures, items, bosses | `physics.c:19,786`, `nav.c:465,482,822,874`, `boss.c:350`, `boss_*.c` (8-bit sums) | Already derived from `hero_scr_col`, nothing to do except where a literal 12 is written: `boss_crab.c:103` (`+12u`), `boss_meda.c:40` (`+16`) - boss rooms only, which stay original | 1 |

### 2.4 Projectiles and spells (`shots.c`)

| # | What | Where | Widening | Risk |
|---|---|---|---|---|
| 19 | Enemy shots are in **ring** columns (`uint8_t col`; 0 = dead, 0xFF = list end), spawned at `rcol + (0/1)`, moved +/-1 per frame, `shots_shift` adjusts them by 1 per ring scroll, wall test reads `ring[row*36 + col]` | `shots.c:60-100,134-139` (8422/85A5/864E/8639) | `RW` stride; `col == 0` still kills.  **Quirk to keep**: a right-flying shot that passes ring col 35 reads into the next row's left cells because the index is `row*36 + col`; with `RW` it does the same | 2 |
| 20 | Range/lifetime: not a window test.  Each shot has `life` (6..20 for straight shots, `0xFF` + a script for arcs) and `age`; it dies at `age >= life` (`shots.c:126`, 8455) | per-AI `s.life` | Nothing: a shot flies at most 20 cells whatever the view.  Shots from enemies in the extra space reach the hero only if they were inside 20 cells, which is the existing behaviour | 0 |
| 21 | Hero hit by a shot: ring cols `hero_scr_col + 4 (+1 facing left)` and +1 | `shots.c:95` (84B4/84C2) | Follows `hero_scr_col` | 0 |
| 22 | Spells: `magic_hit` needs the sprite in ring cols `2..0x21` (8C11 `rcol - 2 >= 0x20`); case 3 falls only when `rcol < 0x21`; spell 7 (`8918`..`8954`) hits **every** ring cell in rows -1..17 | `shots.c:172,311,248-253` | `RW-2`, `RW-3`.  Spell 7 loops `c < RING_W`: in wide mode the screen-wide spell then hits the wider ring, which is the natural reading.  Spells move 2 map cols/frame and live 10-12 frames (<= 24 cells), range stays | 2 |

### 2.5 Bosses and the off-ring rule

* `boss_ring_col` (`boss.c:132-140`, 96A3..96BB): returns CF when the column is beyond `0x23`.  Used to
  *place the boss's own shots*: ZELA bolts (`boss_zela.c:73`), MEDA drips (`boss_meda.c:103-106`),
  TORI eggs (`boss_tori.c:174`).  `boss_shot_template` (`boss.c:142-150`) itself is only a
  record decoder (cell, age, life, flags, damage from the overlay image); it has no window test.
  The off-ring rule is: a shot whose column is outside the ring is not created.
* `boss_part` (`boss.c:169-195`): a part outside the ring is not placed.  `boss_readback`
  (`boss.c:111-130`): same test.
* `boss_hero_col` clamps at the map width; several bosses read `scroll_col + 12/16/24`.

Boss rooms are tuned in absolute ring columns (CRAB limit col 0x10, DRGN 0x10..0x1E, MAO2 0x10..0x35,
TORI 0x0D/0x11, camera col 5..12).  See section 3: they stay original, so none of these change.

### 2.6 The `E900` cell machinery

The `E900` screen copy is 28x19 bytes: 0xFD "force redraw", 0xFF hero cell, 0xFC/0xFE message box and
animated cells.  The port holds no shadow, only: (a) the sprite hide rule above (a `ff[]` array,
2.2 row 9), (b) the message box 32-frame 0xFC marking (`physics.c:1320`, `render.c:454`), and
(c) `bg_ring`, the ring before the 315C animation step (`physics.c:981`, `render.c:625`).
A wide render needs (a) re-dimensioned and the message box (which is a fixed 208x16 at x 56, screen
pixel space) either left at its original x and centred on the playfield or left alone.  Risk 2,
and visual only.

### 2.7 Sound proximity (FF08)

`FF08` is **not** a window test.  `774E` (disasm `fight.asm:2215-2262`) squares |dcol|, |drow| between the
hero and the cavern's goal cell (`C013`/`C015`, the cell in front of the locked boss door) through
the tables `77C7`/`77D7` into a 0..N cue that rises as he approaches; it is zero unless both
distances are < 16.  The only ring test is `call 96A1` on the goal column, and 16 columns each side
of the hero always fit inside the 36-wide ring (16 left and 17 right of his column).  The port does
not implement the cue (`MUSIC.md:289,366`, `SNDADLIB 15A3` "not ported").  If it is ported later it
must use `HC`, not 12.  Risk 0 either way.

### 2.8 Other `RING_*` consumers (not window tests, but they must follow the stride)

`savestate.c:168-169` (the two rings are serialised raw), `trace.c:141,244` (`E000` is compared with the
oracle: **must stay 2304 bytes of stride 36 with the mode off**), `nav.c:771`, `ai_eai2.c:118-123`,
`ai_eai3.c:117-130`, `ai_eai4.c:98-123`, `ai_eai5.c:65-80`, `ai_eai1.c:244`, `ai_eai7.c:154`, `combat.c`, `boss.c`,
`test_physics.c`/`test_combat.c`/`test_boss.c` (check each for a literal 36).

### 2.9 The map seam

Maps wrap horizontally (`ring_fill` wraps at `map->width`).  A ring wider than a map duplicates columns, and
`remark_column` (equal `col`) would double-mark.  Cavern map widths must be checked against the largest
ring (`MAP_MAX_WIDTH` is only an upper bound, `map.c:46`).  If any cavern is narrower than the wide ring,
fall back to the original width for that map.

**Measured (step 0, `port/map_widths.c`, `make map_widths && ./map_widths ../zeliard`).**  All 31 system
maps are at least 42 columns wide, so none is narrower than the 36-column ring and nothing is duplicated
in the tier-0 view (view = ring).  Narrowest first: MP90 42 (boss-flag clear, the E6-only final room),
MP2D 52 (boss), MP84 64, MP62/MP7D/MP8D/MPA0 and the other boss rooms 70..73, MP21 96.  Cavern widths:
MP10 240, MP20 224, MP21 96, MP30/MP31 204, MP40 320, MP41 192, MP50/MP51 240, MP60 320, MP61 256, MP62 73,
MP70 208, MP71 196, MP72 128, MP80/MP81 256, MP82 192, MP83 128, MP84 64, MP90 42.  So the wide view needs
no clamp or pillarbox for map width.  (A wider ring, steps 3+, has the room up to 42 columns, i.e. E <= 3
on MP90; MP90 would then keep the original view.)  Boss rooms (`lvl_flags` bit 7) are pillarboxed regardless.

## 3. Bosses

**Recommendation: leave boss rooms exactly as they are.**  Render them with the original 28-column
view, centred in the wide frame with the stone frame stretched on either side (pillarbox), and run the
original 36-wide ring for the whole room.

Reasons:

1. The arenas are authored in absolute columns (2.5) and the boss AIs tie their behaviour to them: CRAB
   steps stop at col 0x10, MAO2 patrols 0x10..0x34, DRGN retreats right to 0x1E.  Beyond the walls the
   ring holds whatever the map has (wall fill or wrapped data); showing it adds nothing and could show
   the seam.
2. The boss AI has its own camera (`boss.cam_col`), and its off-ring rules (`boss_ring_col`, `boss_part`) place
   shots and parts using the 36-wide ring.  Leaving the room original keeps all of that byte-exact.
3. The boss fights are the most oracle-tested part of the port (HARNESS.md, 20+ scenarios).  Keeping them
   untouched in wide mode means the same scenarios still validate them with the flag on.
4. Boss rooms are small; the pillarbox is a one-frame transition, same as when a boss door is entered
   today (the room swap already reloads the map and resets the camera).

Mechanism: `ring_w`/`view_cols`/`hero_col` are per-map values set at map load (`game_ring_refill`,
shell cavern entry).  Boss/boss-room entry sets them to (36, 28, 12-or-cam_col).  Everything else
reads the parameters, so there is no special case in the boss code.  An option for later: an
"arena view" of 36 columns (the existing ring, no sim change) for boss rooms.

## 4. Presentation

### 4.1 How many columns

The 320x200 picture is shown at 4:3 (`video_display_size`, VGA's 1.2 pixel aspect), so it is 320 x 240
square pixels.  A target aspect `A` needs `W = 240 * A` pixels of width at the same scale:

| Target | Frame width W | Columns (8 px) | With 48 px stone sides, playfield |
|---|---|---|---|
| 4:3 (today) | 320 | 40 | 28 (224 px) |
| 16:10 | 384 | 48 | **36 (288 px)**: exactly the ring that exists today |
| 16:9 | 427 | 53.4, use 424 or 432 | 41 or 42 (328 or 336 px) |
| 21:9 | 560 | 70 | 58 |

Heights do not change: 19 rows (18 visible).  Round the playfield up to an even column count so the hero
and margins stay symmetric.

### 4.2 The frame and the HUD

The 320x200 composite today (`gfx.h:95-108`): top strip at (48,0) 224x13; stone side columns 48x200 at
x 0 and x 272; grey HUD panel at (48,158) 224x42 with three item-slot frames; the playfield (48,14) 224x144;
two cyan marks at (8,47) and (304,47).  The cavern HUD gauges and digits are at fixed absolute x
(`render.c:732-860`: LIFE bar at 84, GOLD at 76, ALMAS at 152, place name at 52+...).

Recommendation: **keep the 48 px stone sides at the very edges, widen the playfield, and leave the HUD
panel at its original size centred below the playfield**:

* Left and right stone: copy the original 48x200 art unchanged to x 0 and x `W-48`.
* Top strip: tile the 224 px strip horizontally (it is decoration; if the art has a visible seam,
  mirror alternate repeats).
* HUD: draw the existing 224x42 panel and all gauges at their original relative positions, offset
  by `(playfield width - 224) / 2`.  Left and right of it, fill with the panel's own grey (replicate
  its edge columns).  Because the HUD code takes no arguments for position today (absolute pixel
  constants, `render.c:739-860`) the cleanest way is a single `hud_x0` offset added to each blit.
* Stretching the frame art is the alternative; rejected, because it blurs the stone and the three item
  slots.

Implementation shape: a **wide framebuffer** (`WF = 320 + 16*E` pixels wide x 200) used only for the
cavern.  `render_frame(fb, g, h)` becomes `render_frame_w(fb, fbw, g, h)` with the `FB_W`/`PF_X`/`PF_W`
macros turned into parameters; `FB_W` stays 320 for every other screen.  Presentation already takes
`(sw, sh)` per call (`main.c:737`), so the town, shops, status and cutscenes simply stay 4:3
(letterboxed) inside the wide window.

Scope note: town (`town.c`, same 28-wide scroll window, `hero_scr_col 0..0x1B`) could get the same
treatment later.  The issue is about the cavern; don't start there.

### 4.3 Zoom (Z) and smooth scrolling (T)

* **Zoom** crops to the playfield + HUD, dropping the stone: `PRESENT_ZOOM_X 48, _Y 14, _W 224, _H 182`
  of a 320x200 reference (`present.h:101-106`, `present_zoom_rect` `present.c:90-103`).  Generalise to
  `x = 48, y = 14, w = sw - 96, h = 182` (derive from the source width; with 320 it is bit-identical).  The
  display shape used for the crop is `dw*cw/sw`, so it works for a wide source unchanged *provided* `dw` is
  set to the aspect-corrected size of the wide frame.  `video_display_size` returns the 4:3 size for the
  320x200 driver; for the wide frame, `dw = sw`, `dh = round(200 * 1.2)`.  Other `--video` drivers
  (CGA/EGA/HGC/Tandy, pair buffers) have their own display sizes (`video.h:39`); restrict wide mode to
  MCGA/VGA at first and refuse (log + disable) otherwise.
* **Smooth** is in framebuffer pixels, applied inside `render_frame` (`render.h:23-36`, `render.c:625`,
  `smooth.c`), and `smooth.h` states that it needs no presentation change.  So the two compose with no
  new code: smooth operates on whatever width the render has; zoom is a crop after.  What needs touching:
  the hardcoded `SCREEN_COLS` in `smooth.c:43` and `render.c:629` (both become `VC`), and the sprite
  clips `sc < -1 || sc > SCREEN_COLS`.  Smooth reveals one cell beyond the edge, which the 4-col ring
  margin supplies in the full wide mode; in the tier-0 "view = whole ring" mode (16:10, 36 columns) there
  is **no** margin, so smooth scrolling reads ring col -1 (the previous row's last column) at the edge:
  clamp, or draw the edge column only (cosmetic).  With zoom on, the crop cuts the
  frame, so any smooth slide at the playfield edge is clipped by the playfield clip (`render.c:16,34,49`)
  exactly as now.
* Pixel snapping: with Z on and a window that is not a multiple of the source, the existing integer
  prescale path (`present_prescale`) keeps working; it takes `(cw, ch)`.

### 4.4 Other things in the same layer

Touch layout (`touch.c`, `touch_set_content`), the M-key map viewer overlay, the OSD and `present_dump` all take
content rects from `present_view`, so they follow.  The web build (`__EMSCRIPTEN__`) creates one texture at
320x200; it needs a re-create on size change (only when entering/leaving a wide cavern).

## 5. Determinism: keeping "off" byte-identical

Principle: **no constant that mentions 28, 36, 12, 0x11, 0x23 ... survives except as the value of a
function of three per-map parameters**, set once at map load:

```c
typedef struct WideCfg { uint8_t extra;      /* E: columns added each side, 0 when off */
                         uint8_t vc;          /* view columns = 28 + 2E */
                         uint8_t rw;          /* ring stride = vc + 8   = 36 + 2E */
                         uint8_t hc; }        /* hero screen col = 12 + E */
```

* `g->wide` is zeroed by default.  `wide_cfg(g)` returns `{0,28,36,12}` when `g->wide_on == 0` or
  the map is a boss room, a too-narrow map, or the `--video` driver is unsupported.
* All window tests go through inline helpers: `ring_w(g)`, `view_cols(g)`, `hero_col(g)`,
  `hero_rcol(g)` (= `hc + 5`), `ring_last(g)` (= `rw-1`), `edge_lo/edge_hi`.  `RING_W` and the other
  macros are deleted by the refactor so the compiler finds every use.
* The ring storage is `RING_STRIDE_MAX * 64` bytes; the live part is `rw * 64`.  `RING_SIZE` becomes a
  function (`ring_size(g)`) except in `trace.c` and `savestate.c`, which keep the 36 layout (below).
* **The refactor is its own commit and must be a no-op**: step 1 below lands the helpers with `rw = 36`
  hard-wired.  Verified by byte-exact `--trace` equality over the whole scenario library
  (`tools/harness/trace_eq.py` against a trace taken from `main`), `make test` counts unchanged, and
  `make verify`.
* **Tests that guard "off"**:
  1. `trace_eq.py` main vs branch, every `tests/scenarios/*`, with the mode compiled in but off.
  2. "Forced-wide with E=0": a debug flag takes the wide code paths (variable framebuffer width, wide
     render function) with `extra = 0`; its trace and its PNGs must equal off's.  This catches
     off-by-one bugs in the plumbing without needing a wide oracle.
  3. Compile-time assertion that `wide_cfg(&g_default)` equals the old constants.
  4. `make monkey`/`soak` (`--san --check`, droplet only) with the mode off and on.
* **Trace and oracle**: `--trace` forces the mode off (error if `--wide` is combined with it), so
  `E000` always means 2304 bytes of stride 36.  Likewise a harness run never loads a wide save.
* **Savestate** (`savestate.c`): the two rings are written raw.  Save the mode's `(extra)` in the
  header and refill the ring on load if it differs (`ring_fill` is deterministic from the map and scroll),
  or version the file.  `bg_ring` is refreshed every frame, so it need not be saved wide.
  `.usr` saves (the original format) hold no ring and are unaffected.
* **RNG**: wide changes the stream (more enemies act, more `krn_random` calls), so a wide run never
  matches an off run past the first extra enemy action.  That is expected: wide is not compared with the
  oracle at all, only with itself.

## 6. Plan

Sizes: S <= half a day, M ~ 1 day, L = 2-3 days, XL = a week+ (agent-sized units; rough).  Every step merges on its own and
leaves the mode off; the oracle scenarios and `make test` must stay clean at each one.

| # | Step | Size | Verification (mode off) | Verification (mode on) |
|---|---|---|---|---|
| 0 | **Measure**: min cavern map width; list `RING_W`/`36`/`0x23`/`0x11` literals; add `tools/` script that greps them | S | - | records the baseline and the max safe `E` |
| 1 | **Presentation plumbing with E=0**: `FB_W` -> a runtime `fb_w` for the cavern render, `PF_X/PF_W/render_screen_frame/render_hud` take a width and an x offset, `present_zoom_rect` derives from `sw`.  No flag, no sim change.  Includes the wide frame composition (tiled top strip, HUD offset), in code that only runs when `fb_w != 320` | M | `trace_eq` and PNG equality on all scenarios; `make verify`; `test_present`, `test_smooth` | forced-wide with `fb_w=320` equals off |
| 2 | **Tier 0: 16:10 shows the whole ring** (view 36 = `vc` > 28, ring untouched): new `--wide 16:10`, cavern only, `view_cols = 36`.  The sim keeps `hero_scr_col = 12`; the view starts at ring col 0 (4 columns earlier), so the hero is drawn 16 columns in, at `PF_X + 16*8`.  Render functions take `vc`; spawn band, ring edges untouched | M | as step 1 | screenshots in 4 caverns; check enemies at ring edges move and sprites clip at the true edge; smooth with T on; zoom with Z on; boss door entry re-pillarboxes |
| 3 | **Refactor: the runtime width, no-op.**  `RING_W` -> `ring_w(g)`, `game_ring_add(g, ...)`, the hero-rcol helper `0x11` -> `hero_rcol(g)`, the edge helpers.  All values still constants (36, 17, ...) | L (large diff, mechanical, ~120 sites + tests) | byte-exact `trace_eq` over all scenarios and `make test` counts unchanged.  Do this one with the compiler as the guide: delete the macros first | none |
| 4 | **Make the values settable**: `g->wide` (extra), `ring_fill`/`scroll_*` use `rw`, `hero_scr_col = hc` at the four entry points, `scroll_col` placement at map entry, savestate header. Mode still not exposed on the command line | M | as step 3 | forced-wide with `extra = 0` equals off (trace + PNG); a new `test_physics` case with `extra = 4`: walk the first cavern end to end, assert the ring equals `ring_fill` from the map at each step |
| 5 | **Enable wide sim**: `--wide <aspect or +N>` and config key; cavern only; renders at `vc`; spawn band, `ai_step` edges, EAI4/5 muzzle guards, door arches, fixtures `0x21..` rewritten in terms of `rw` | L | as step 3 | per-AI test in `test_combat`: for each of the 8 overlays, an enemy placed in the extra columns acts (moves, turns, shoots) and a `E = 0` control is identical; fixtures and doors: a map with a lift near the left/right edge (find with the map viewer) |
| 6 | **Cell animation + render details** (anim over `vc`, `ff[]` stride, message box centring, smooth `ok` range) | S | as step 3 | caverns 5..8 water/lava animate to both edges; smooth scroll visual check |
| 7 | **Boss rooms and map-load rule**: boss entry and too-narrow maps force (36,28,12); test the transition both ways incl. a save made inside a boss room | M | all boss scenarios clean | boss scenarios with the flag on (they run original geometry, so should still pass the oracle comparisons by construction - run them as a regression, expect zero diff) |
| 8 | **Optional: aggro scaling** (bat wake, EAI3 `dx<10`, EAI8 `dx<15`, shooter windows scaled by `vc/28`) behind its own sub-flag | S | n/a | feel test only; keep disabled by default |
| 9 | **Docs, config, touch layout, web texture resize, `--wide` in the settings dialog** | S | `test_config` | - |

Total: ~8-10 days of focused work.  Steps 1-2 ship a visible, risk-free feature (16:10, zero sim change) on
their own, even if 3-5 never happen.

## 7. Risks and rabbit holes

1. **Step 3 (the stride refactor)** is the big one: ~120 `RING_W` sites in `physics.c`, `ai*.c`, `enemy.c`, `boss*.c`,
   `shots.c`, `combat.c`, `nav.c`, the tests, `savestate.c`, `trace.c`.  `game_ring_add` has no `g` parameter today.
   The fidelity-critical part is that the **original's pointer quirks survive** (column -1 lands in the previous
   row, shots running past col 35 read the next row, `0x23` is the last column, the edge refusals): keep them
   expressed on the runtime stride, never "fixed".  Do it with a global compile-fail sweep and a differential trace; do
   not hand-edit and eyeball.
2. **The hero constant 0x11** appears with several encodings: `0x11`, `0x10`, `0x0F`, `0x12`, `0x13`, `8`, `0x0B`,
   `0x1A`, and `uint8_t` wraparound comparisons.  Each is hero-relative: a missed one makes enemies
   in a wide ring chase/flee toward col 17 instead of the hero.  Grep alone will miss encodings such as
   `(rcol - 0x11)` inside macros.  Mitigation: the step-5 per-AI test with `E > 0`.
3. **Fixtures and door arches** (2.3 rows 15-16) hold 0x21..0x27 literals that are edge-of-ring tricks tied to the
   original's off-by-ones.  Highest chance of a subtle bug, which would show as a missing platform or
   ghost arch only at the extra edges.  Test with a hand-built map near a lift.
4. **Cavern map width < ring width**: duplicated columns and double-marked records.  Measure in step 0; fallback
   to the original ring for that map.
5. **Frame art**: the stone top strip and HUD panel were never designed to tile.  Visual polish only, but
   plan a mirror-tile and a review of the screenshots; not a blocker.
6. **Other video drivers**: CGA/EGA/HGC/Tandy compose on pair buffers with their own display sizes.  Restrict wide mode to MCGA at first.
7. **Aggro/difficulty**: with unchanged aggro ranges the wide ring is mostly more visible, not more dangerous;
   but enemies off screen on the left/right now shoot into the view from 20 cells (shot life), and an
   enemy offscreen in the original can now be seen walking in.  Players will feel it.  It is
   opt-in.
8. **Respawn pop-in**: the spawn band stays outside the view only if it is `VC+4` and not left at `0x20`.
9. **Performance**: the `memmove` of the ring per scroll step is `64*rw` bytes (up to ~3.4 KB), irrelevant.
10. **Anything else that persists the ring layout**: replay/trace tooling, `tools/harness/port2base.py`,
    `test_*` fixtures with hard-coded ring offsets.  Step 3 will find them.
11. **Town and other screens**: out of scope; they remain 4:3 inside the wide window.
