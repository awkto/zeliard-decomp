# DOSBox recipes (issue #19)

`KEYS=` timelines for `tools/run_dosbox.sh` that take the real game from a cold start to the
town and into cavern 1 (mp10 — the "MURALLA" door; the HUD calls the cavern *Cavern of
Malicia*).  Times are seconds after DOSBox launch with the harness defaults (`cycles=3000`,
`output=surface`, 640×480 Xvfb; the DOSBox window is 640×400 at y=40, i.e. 2× the 320×200
game screen).  Every timeline below was re-derived and verified on 2026-08-29; the full run log
(every KEYS line tried and its outcome) is `runs.log` in the scratch dir of that session.

## 1. Recipe A — skip the intro, playable in front of Felishika's Castle (≈19 s)

    KEYS="6:Return 9:Return 16:Return" tools/run_dosbox.sh /tmp/z 19 20 22

What each key does (see §4 for the intro state machine):

| t | key | effect |
|---|---|---|
| 6 | Return | skips the scrolling prologue ("Two thousand years…") → "Fantasy Action Game / STAFF" credits |
| 9 | Return | skips the credits → black screen (opdemo loading) until ≈15 s |
| 16 | Return | skips the opening demo (storm balcony, "Once, long ago, a terrible storm…") → town loads |
| 17–18 | — | HUD frame appears, then the castle scene; Garland stands ≈8 columns left of the castle door, PLACE "Felishika's Castle" |

Three Returns are enough.  Do **not** add more "just in case": once the town is up, Return
toggles the status/inventory menu (§3), so an odd number of extra Returns leaves the menu open
and every later key is swallowed (that is what happened with the old `Return×8` timelines).

## 2. Recipe B — enter cavern 1 (Satono Muralla, mp10) — VERIFIED 3/3

    KEYS="6:Return 9:Return 16:Return 20:+Right 48:-Right \
    49:+Left 49.06:-Left 49.15:+Up 49.3:-Up 49.45:+Left 49.51:-Left 49.60:+Up 49.75:-Up \
    49.90:+Left 49.96:-Left 50.05:+Up 50.20:-Up 50.35:+Left 50.41:-Left 50.50:+Up 50.65:-Up \
    50.80:+Left 50.86:-Left 50.95:+Up 51.10:-Up 51.25:+Left 51.31:-Left 51.40:+Up 51.55:-Up \
    51.70:+Left 51.76:-Left 51.85:+Up 52.00:-Up 52.15:+Left 52.21:-Left 52.30:+Up 52.45:-Up" \
    tools/run_dosbox.sh /tmp/z 44 49 51 54 56 58

i.e. Recipe A, then **hold Right for 28 s**, then 8 × (tap Left 0.06 s, tap Up 0.15 s).

Timeline: 20–44 s walk right through Felishika's Castle (cmap, 114 columns) and Muralla Town
(mrmp, 215 columns); ≈44.5 s Garland passes the cavern gate (door at column 205) and stops at
the right edge of the map (≈46 s; holding Right longer is harmless).  49–52 s the Left/Up taps
walk him back one or two columns at a time, pressing Up at every stop; when he is within ±1
column of 205 the Up is accepted: dissolve (≈51 s), cave-mouth backdrop with Garland walking in
(≈52–54 s), cavern rendered from ≈54–56 s with Garland standing in the purple "MURALLA" door,
PLACE = "Cavern of Malicia".  Leftover taps after the entry fall into the dissolve and are ignored.

Verified alternative (also 1/1): hold Up and tap Left underneath it —
`… 48:-Right 49:+Up 49.2:+Left 49.3:-Left 49.6:+Left 49.7:-Left 50:+Left 50.1:-Left 50.4:+Left 50.5:-Left 50.8:+Left 50.9:-Left 51.2:+Left 51.3:-Left 51.6:+Left 51.7:-Left 52.5:-Up`
(enters ≈50 s, cavern drawn by 52–56 s).

To see an enemy, add `58:+Right 61:-Right` and capture 60–64: a green frog hops in from the
right and a pink blob hangs from the ceiling (`docs/screenshots/cavern_enemy.png`).  Garland has
no armour: the frog kills him in ≈6 s (LIFE red at 60 s, black at 66 s, then the Sage Marid's
"While you were unconscious" screen at 68 s), so keep cavern walks short.

Why the detour to the map edge: the door test is `hero column within col±1` (town.bin 6E29), a
3-column = 24 px window, and a held Right moves ≈11 columns/s, so releasing Right "at the gate"
needs ±0.1 s accuracy.  The map edge (`hero_scr_col` 0x1C, column 211) is a deterministic stop,
and the Left/Up scan from there needs no timing at all.

## 3. What the keys do in the town (state after Recipe A)

| key | town (town.bin) | notes |
|---|---|---|
| Left / Right (held) | walk, ≈11 columns/s (≈90 px/s); 1 column ≈ 90 ms | screen scrolls while the hero is in columns 0x0B..0x10, otherwise the hero moves |
| Up | in front of a door: enter (castle audience, shops, cavern gate at col 205) — otherwise nothing | door table for Muralla: 39 armour, 59 church, 111 drug, 138 bank, 172 sage, 205 cave0 (docs/TOWN.md) |
| Return | toggles the status/inventory menu (select.bin) | contents below |
| Escape | "PAUSE" banner, toggles | |
| Space | talk to an NPC 1–3 columns ahead | in the cavern: sword |

**Return menu** (`docs/screenshots/menu.png`), captured at game start:

    SELECT-MAGIC : NOTHING
    WEAR         : NOTHING        INVENTORY
    USE          : NOTHING           Training Sword

HUD at start: LIFE (green bar), PLACE "Felishika's Castle", GOLD 0, ALMAS 0, one item slot with the
training sword.  Nothing else — no gold, no armour, no magic.

## 4. Intro state machine (no keys pressed)

| t | screen |
|---|---|
| 4–55 s | prologue: pendant graphic + scrolling text ("Two thousand years, from the dark reaches of another galaxy…" … "the Sixth Book of Esmesanti: The Age of Darkness") |
| ≈58 s | yellow flash, demon face, "Beware, for I shall wake from my sleep of 2,000 years…" |
| ≈80 s | title screen (ZELIARD logo, pendant) |

A Return at any time during a scene ends it: prologue → credits scroll ("Fantasy Action Game
ZELIARD — STAFF", 50+ s long) → (black, loading) → opening demo (storm balcony, typed text
"Once, long ago…", runs for minutes) → town.  Return during the black loading gap (10–14 s) is
ignored, so `6:Return 9:Return 12:Return 15:Return` behaves the same as Recipe A (castle at 18 s
instead of 19 s).

## 5. Timing caveats (read before writing a new timeline)

* **Harness drift.**  `run_dosbox.sh` sleeps `next − previous` nominal seconds but does not
  subtract the time each event takes (`xdotool windowfocus --sync` + `key --delay 80` ≈ 0.1 s,
  `import` ≈ 0.1 s), so the timeline runs progressively *late*: measured (file mtimes vs.
  nominal) +1.4 s at 44 s and +3.0 s at 56 s for Recipe B, +2.8 s when the same keys are run
  with no captures before 56 s.  Consequences: (a) a given KEYS line is reproducible run to run
  (T1/T2, Y2/Z2, BB/BC were identical), (b) adding or removing captures/keys *before* a critical
  moment shifts it by ≈0.1 s per event — keep the event list unchanged once it works, and prefer
  timings that do not need to be exact (edge stop + scan).
* **Taps must be holds.**  `xdotool key Left` (press+release in one go) is often missed —
  town.bin polls the key state once per frame, and a press/release pair inside one frame is
  invisible.  Alternating plain `Left`/`Up` taps worked only 2 runs out of 3; `+Left 0.06 s
  -Left` / `+Up 0.15 s -Up` holds worked 3/3.  A 0.06–0.1 s hold moves 1–2 columns.
* **Fractional seconds** (`49.06:-Left`) are required for that; the harness sorts events with
  `sort -n` and sleeps with `awk`, so any decimal works.  Intervals below ≈0.1 s are stretched to
  the event overhead.
* **Held Left is fast.**  0.55 s of held Left moved Garland ≈10 columns past the gate
  (runs Y1/Y3) — never "hold Left briefly" to hit a door.
* **Parallel runs are fine** (3 DOSBox+Xvfb at once, staggered by 2 s; the harness picks free
  displays :200+) and did not change timings measurably.
* **Cycles.**  Everything here is `cycles=3000`; the intro and loading gaps scale with it.

## 6. Cavern capture vs. decoded assets

`docs/screenshots/cavern_vs_mp10.png` — left: DOSBox capture (BB/shot_56) cropped to the
224×120 px play area; right: the same window cut from `python3 tools/mdt2png.py
extracted/ZELRES3/020_data.bin OUT.png` (mp10, MPP1 tileset).

* Best alignment (exhaustive search over the whole 240×64-cell map with the 64-row ring
  wrapped): view origin **column 49, row 63 (first *visible* column/row; the engine's scroll_col/scroll_row is 45/61 — see port/)** — rows 63,0,1,…,13 — i.e. the Muralla entry
  record `(col 61, row 7, side 0, MP10)` from mrmp.mdt (docs/TOWN.md §9: `scroll_row =
  (row−10)&63`, `scroll_col = col−16`, hero 16 columns in).  It is **not** the mdt's own
  `--info` start (col 26, row 16); that default is only used when no town record applies.
* **97.9 % of pixels identical**; rows 0–6 and 11–14 of the view are 100 % identical.  All
  differences are in the 7-column band around the door: Garland's sprite and the purple door
  arch, which fight.bin draws at the entry position (it is not in the tile stream — no fixture
  or object record exists at (61,7)).  The "MURALLA" sign *is* drawn by mdt2png and matches.  The earlier search that
  ignored the ring wrap locked onto the SATONO door (cols 125–153, rows 25–40) instead — the
  two doors are the same tile pattern.
* Colours match pixel-exactly (the `tools/palette.py` DAC), including the parallax-free black
  background.
* **Enemies** (`cavern_enemies.png`, from AB/shot_60): a green frog with white eye pixels and a
  salmon/red ceiling creature — identified by the port as the **class-1 snail** (enp1 cells 79/80,
  frame 0 facing left), not a bat.  `python3 tools/grp2png.py extracted/ZELRES3/056_data.bin OUT.png`
  (enp1.grp, "cells32" sheet) decodes 8×8 cells with exactly those colours — the bright-green
  scaly frog cells and the salmon/red blob cells are recognisable in the sheet — but grp2png does
  not yet assemble enemy metasprites (ROADMAP Phase 1 item 2), so only cell content/palette are
  confirmed, not the cell layout.

## 7. Harness change (uncommitted, `tools/run_dosbox.sh`)

Fractional seconds in `KEYS=`/capture times: the timeline sleeps are computed with `awk`
instead of shell integer arithmetic and the merge uses `sort -n -s`, so `30.7:-Right` and
capture times such as `49.5` are valid.  Needed because Garland walks ≈80–90 px/s and door
windows are ≈24 px.  `+key`/`-key` hold and release a key (`xdotool keydown/keyup`).

## 8. Screenshots (`docs/screenshots/`, 320×240 PNG8)

| file | run | what |
|---|---|---|
| `town.png` | X1 44 s | Muralla Town, red-rock end with the cavern gate at the right; the Garland look-alike at the left is NPC #3 (column 188), Garland himself is at the right edge |
| `menu.png` | R1 22 s | the Return menu at game start |
| `cavern.png` | BB 56 s | cavern 1 just after entry, Garland in the MURALLA door |
| `cavern_enemy.png` | AB 60 s | frog + ceiling snail, LIFE already red (reproduced 100% by `port/`, `make verify`) |
| `cavern_enemies.png` | AB 60 s | 3× crops of both enemies |
| `cavern_vs_mp10.png` | BB 56 s | play area vs. mdt2png window (§6) |
| `shop_armour.png` | scan §5 | Muralla weapon shop: greeting + main menu |
| `cavern2.png` | F7 restore (§9) | MP20 "Cavern of Peligro", MPP2 tileset — port matches 100% |
| `cavern3.png` | F7 restore (§9) | MP30 "Cavern of Madera", MPP3 + two enp5 enemies — 100% |
| `boss_cangrejo.png` | F7 restore (§9) | MP1D, the Cangrejo fight — 98.15% of the playfield (only the claw and the hero's animation phase differ) |
| `town_satono.png` | F7 restore (§9) | Satono Town, the ckpd underground backdrop |
| `restore_menu.png` | F7 | the Restore Game name box and file list |


## 9. Reaching deep locations: the save-file route (verified)

A `NAME.USR` written by `port/` **loads in the real game**: F7 "Restore Game" →
`Sure?(Y/N)` → `y` → Down/Space to pick the file → Enter.  The restored game came up with
the exact gold, level, sword, shield durability and hero column the port had saved, which
round-trip-validates our save format (kenjpro `A862`: a raw 256-byte image of BASE:0000,
no header).  town.bin keeps the hero's town position inside those same bytes (`[80]`
scroll_col, `[83]` hero_scr_col, `[C2]` facing, `[E7]` walk frame, `[C4]` town map), so the
save also fixes where you reappear.

A restore always lands in a **town**, so pick one whose gate or edge exit comes out where
you want.  Satono's two edge exits are both cavern entries — that is how the Cangrejo room
is reachable: save at Satono column 5, walk off the left edge into MP10 (128,33), then
13 columns right to the unlocked door at (141,32).

Two gotchas:

* **`Up` is jump inside a cavern** and a jump lasts ~10 frames, so the town scan spacing of
  0.45 s leaves every input landing mid-air — use ~1.3 s between taps underground.
* **Reduce captures with `convert -sample` (nearest neighbour), never `-resize`** — any
  interpolation turns an exact-pixel comparison into roughly 25%.
