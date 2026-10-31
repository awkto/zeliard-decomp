# Differential test harness (epic #46, Phase A)

Goal: run the **original game** (DOSBox-X, "the oracle") and **`port/`** from the
same inputs, frame by frame, and report the first frame where they disagree, with
named fields and a picture. No human playtesting.

Status: being built (#47 oracle, #48 port trace + diff, #49 scenarios, #50 monkey,
#51 triage). This file is the contract between the pieces; change it first.

## 1. The game frame

Both main loops end a frame by busy-waiting until `FF1A >= 4*speed`, then zeroing it
with the same instruction, `C6 06 1A FF 00` = `mov byte [0xff1a],0`:

| Loop | Overlay @6000 | Site |
|---|---|---|
| caverns | fight.bin | `717F` |
| towns | town.bin | `68ED` |

**Frame boundary** = the CPU is about to execute that instruction at `BASE:site`.
At that point the frame's logic and drawing are finished. Inputs applied at a
boundary are what the *next* frame reads. The same instruction elsewhere (the text
printer's glyph wait `7084`, the shops' timed waits, `6292`/`7FAB` on transitions)
is not a boundary: those loops read no input. Cutscenes are out of scope.

Town NPC dialogue needs nothing extra: its page and end waits (`655D`, `65AB`,
`65C8`) call the town frame `68AC`, so they are ordinary `68ED` frames. Cavern
message boxes are timed inside the `717F` frame (32 frames, no key).

**Wait-loop boundaries.** The status screen, the shops, the sage and the yes/no
widget have no frame: they spin on `idle_poll` / `int 61h` until a key arrives,
and no `FF1A` wait paces them (menu_select zeroes `FF1A` at `7358` but never waits
on it). Their boundary is the loop's **poll site**, taken

1. on the **first poll after the loop's entry** (the loop is "armed" by its entry
   instruction), and then
2. at the first poll once **`4*speed` kernel ticks** (`FF1B`, 236.7 Hz; 20 at
   speed 5, one frame's worth) have passed since the previous boundary of any kind.

Work between two polls that reads no input (printing text, the 40-tick cursor
animation, a shop's own timed waits, the death fade, loading an overlay) produces
no boundary, however long it takes, so its exact duration does not move the
token count. One token-frame = one wait-loop boundary.

| Loop | Overlay | Armed at | Site (`site` value) | Reads |
|---|---|---|---|---|
| `wait_key` 71DF (text pages, 0x0F/0x11, "come again", sage/death text) | town.bin | `71DF` | `71EC` | FF1D / FF1E edges |
| `menu_select` 7344, also `yes_no` 74D3 (shops; dialogue 0x81/0x89) | town.bin | `7344` | `7358` | FF1E, FF1D, int 61h U/D |
| status screen (all row loops and release waits) | select.bin @A000 | `A08C`, `A0B8` | `select:AA58` | FF18 bit0, int 61h, `FF18==0286` |
| omoypro wait for Space | omoypro | `A044` | `omoy:A04E` | FF1D |
| sage's name entry (Record) | kenjpro | `A568` | `kenj:A597` | FF1E, FF18 bit0, FF1D, FF29 |
| bank amount entry | bankpro | `A295` | `bank:A2D8` | int 61h (held, not edges) |

Every site is recognised by its own code bytes, so another overlay at the same IP
cannot fire it. `select:AA58` is taken at whichever select poll comes first:
`idle_poll` AA58 (except the A084 probe right after the screen is drawn) or one of
the `int 61h` release waits (A0D2/A1C3/A2C1 on row entry, A174/A25F/A373 after a
left/right step); the oracle writes the real IP as an extra `"ip"` key.

`BASE` (the one 64 KB segment that STICK, the drivers and the overlays share) =
the segment of the `INT 61h` vector (STICK's input service at `BASE:05FD`).

## 2. Inputs

The port's `--script` language is the input language for both sides: `U D L R`
directions, `X` sword, `M` magic, `E` Enter, `K` the LEVEL/EXP chord, `.N` idle,
a number = hold for N frames (`R10`, `UR2`, `X1`). One token-frame = one game frame,
or one wait-loop boundary inside a menu (§1).

The oracle applies frame `n`'s inputs at boundary `n-1`. It writes the kernel's
input state directly (`FF17` dirs, `FF16` buttons, `FF1D`/`FF1E` press edges,
`FF18` key mask), exactly as STICK's INT 9 would leave it. This is deterministic
and independent of keyboard-controller timing, and it is enough for every screen
with a site, because each reads only these bytes (`int 61h` = `FF17`/`FF16`):

| Screen | Keys | Read as |
|---|---|---|
| status screen | E closes; U/D row; L/R step; X uses the potion (held); K chord | FF18 bit0, int 61h, `FF18==0286` |
| shop menus, yes/no | U/D move (auto-repeat every 40 ticks while held); X choose; M cancel / No | int 61h, FF1D, FF1E |
| text pages, death/sage text | X or M | FF1D / FF1E |
| dialogue boxes (town frame) | X next page / close; M cancels a page | FF1D / FF1E |
| bank amount | L/R ±10, U/D ±1 (accelerating while held); X ok; M back | int 61h AH/AL |

No scancode injection is needed. Keys the script language has no token for (letters
for the sage's name entry `FF29`, Y/N of the kernel's F7 / Ctrl+Q boxes, F9's
digits) would also be direct writes (`FF29`, `FF18` bits 0x20/0x40), but those
kernel boxes spin on `FF18` with no poll site, so they stay out of scope.

## 3. Determinism

- Fixed `cycles`, `core=normal`, no frame limiter. Emulated time is then a pure
  function of the inputs.
- **Random numbers.** Kernel `[11A]` (`STICK:0918`) mixes `FF1B`, the free-running
  tick count, *at call time*, which depends on CPU speed. That cannot be reproduced
  by the port. In harness mode both sides return the **harness RNG** instead:
  `state = state*25173 + 13849` (u16, seed 0 at trace frame 0), one step per call,
  and nothing else. The oracle intercepts `BASE:0918` and returns `AX = state`.
  The port's `krn_random` does the same when tracing, without the `+20` per-frame
  stir. **The number of RNG calls per frame is itself a traced field.**
- Start state: both sides start from the same `NAME.USR` save. The oracle boots,
  skips the intro and F7-restores it; the port uses `--load NAME`. Trace frame 0 =
  the first boundary after the restore. RNG and counters are reset there.

## 4. Trace format

### Oracle (`tools/oracle.sh` → `DIR/`)
- `DIR/frames.jsonl`: one line per boundary:
  `{"f":n,"site":"717F","base":"0x1234","rng_calls":k,"input":"R"}`
- `DIR/base.bin`: for every boundary, the full 65536 bytes of segment `BASE`,
  concatenated (frame `n` at offset `n*65536`).
- `DIR/frame_NNNNN.png` (optional, every `--png-every K` frames): the 320x200 screen
  from VGA memory + DAC.

The oracle stays dumb on purpose: raw memory, no field names. Names live in the
schema.

### Port (`zeliard --trace DIR`)
- `DIR/frames.jsonl`: one line per frame with the same keys, plus `"fields":{...}`:
  named values for every schema field the port models.
- `DIR/frame_NNNNN.png` at the same cadence.

### Schema (`tools/harness/fields.tsv`)
`name  addr  size  mode  note`: `mode` is `fight`, `town` or `any`. Names come from
`docs/STATE_PAGE.md` / `docs/FIGHT.md`. `tracediff` decodes the oracle's `base.bin`
through it. Fields the port does not model are simply absent from its `fields`.

### Diff (`tools/tracediff.py ORACLE_DIR PORT_DIR`)
Aligns by `f`, reports the first frame where a field differs (name, both values),
writes `oracle | port | diff` PNGs around it, and exits non-zero. Fields that
legitimately differ go in an explicit, commented allow-list.

## 5. Where it runs

The oracle is a separate debug build of DOSBox-X: upstream `dosbox-x-v2026.08.02`
plus `tools/harness/oracle-dosbox-x.patch` **only** (none of the zeliard-wasm
shipping patches, which change input behaviour). Heavy builds and big batches run on
a `claude-test` DigitalOcean droplet. pro runs single scenarios at `nice -n 19`.

## 6. The oracle: build, run, and what it really does (#47)

### Build and run

    tools/harness/build-oracle.sh [BUILD_DIR]      # tag dosbox-x-v2026.08.02 + the patch; INSTALL_DEPS=1 to apt-get
    tools/oracle.sh SCRIPT [--load NAME] --out DIR [--png-every K]

`build-oracle.sh` clones the tag into `BUILD_DIR` (default `~/.cache/zel-oracle/dosbox-x`),
applies `tools/harness/oracle-dosbox-x.patch` and builds `src/dosbox-x` (≈7 min on 8 vCPU from a fresh clone,
incremental with ccache). `oracle.sh` finds it there, or takes `ORACLE_DOSBOX=/path/dosbox-x`.
`--load` takes a path or a name looked up in `.` and `zeliard/` (`NAME.usr` from
`port/zeliard --town 1 --town-col 205 --save NAME`). Without `--load` the trace starts at the
first town frame after the intro (Felishika's Castle).

Exit status: `0` script consumed; `3` emulated-time watchdog (`ORACLE_MAX_MS`, default 600 s);
`4`/`5` the restore did not complete; `6` **stalled**: no boundary of any kind (frame or
wait-loop) for `ORACLE_STALL_MS` (default 120 s emulated; the longest boundary-free stretch
seen, death → sage text, is ≈4 s) — an input loop with no site (kernel F7/Ctrl+Q boxes,
cutscenes). The last line of `DIR/oracle.log` names the CS:IP it stopped at; `DIR/final.png`
has the screen on any failure. `ORACLE_MENU_TICKS` overrides the wait-loop rate (§1). `ORACLE_DEBUG_MS=N` writes `boot_<ms>.png` every N emulated ms
until frame 0 (that is how the boot sequence was debugged).

The patch is one file, `src/misc/zel_oracle.cpp`, plus four hook lines: a check at the top
of the normal core's instruction loop (`ORACLE_Exec()` when `CS == BASE`), a call per
emulated millisecond in `TIMER_AddTick`, a fixed RTC start in `CMOS_Init` (only when
`ORACLE_OUT` is set), and the Makefile.am entry. Without `ORACLE_OUT` the binary is plain
DOSBox-X.

**Speed** (Debian 13 droplet, 8 vCPU, one core used): boot + intro skip + F7 restore = 1.6 s
wall (22 s emulated); then ≈160 game frames per wall second (2040 frames in 12.7 s,
≈13x real time). `cycles=fixed 3000` is the cost driver (the frame busy-wait is emulated);
it is kept because every DOSBox capture in docs/ was taken at 3000 and the 84.5 ms frame
(20 ticks at speed 5) is never overrun there. `frameskip` makes no difference.

### Boot sequence (all in emulated time)

1. BASE = segment of the INT 61h vector, accepted once its offset is `0109` and
   `BASE:0918` holds `2E A1 1B FF` (svc_random). With `-fastlaunch` this is at 5 ms.
2. Tap Return (held 120 ms, every 1.5 s) until town.bin's boundary bytes appear at
   `BASE:68ED`; a held Return is released then (in the town Return opens the menu).
3. Wait 20 town boundaries (`ORACLE_SETTLE`), then real key events through the emulated
   keyboard controller, each held 150 ms: F7 (t+0.1 s), `y` (+1.0), Down (+2.5),
   Space (+3.5: copies the highlighted name — the staged game dir holds only `NAME.USR`),
   Enter (+4.5).
4. Restore success = the CPU reaches town.bin `7628` (`mov ax,-1; jmp [7686]`, after
   GAME.BIN is reloaded). **Frame 0 = the first `68ED` boundary after that**: the restored
   town's *entry* frame, which is counted (its `FF1A` is ≈0x33, not 0x14, because the
   load happened inside it). With the save used here it came at 21.9 s emulated.

### Contract as implemented (changes/clarifications to §1-§4)

- **Boundary bytes verified**: `C6 06 1A FF 00` at fight.bin `717F` and town.bin `68ED`.
  The oracle checks the five bytes at the current IP, so a stale overlay cannot fire.
  The same instruction elsewhere in BASE is logged to oracle.log as an unknown site
  (seen: `6292` on the town→cave transition, `7084`/`A410` after death) and not traced.
- **The oracle executes the boundary instruction itself** (`[DS:FF1A]=0`, IP+=5) right
  after the snapshot, so an IRQ taken at that IP cannot make the boundary fire twice.
- **Order at boundary n**: write `frames.jsonl` line n and `base.bin` record n (the state
  as frame n left it), *then* apply the next token. `"input"` in line n = the input frame
  n was played with (= the kernel state in record n), letters in the order `UDLRXMEK`,
  `"."` for idle. Line 0: `"input":"."`, `"rng_calls":0`. A script of T token-frames
  gives T+1 lines (frames 0..T); the run ends at boundary T.
- **Input state written** (§2): `FF17` = dirs (1 U, 2 D, 4 L, 8 R); `FF16` = bit0 X
  (Space), bit1 M (Alt); `FF18` = `0x0001` for E (Enter) | `0x0286` for K; on a press
  edge `FF1D`/`FF1E` = `FF`. The kernel's own edge latches `BASE:02BE`/`02BF` are also set
  (0 while held, FF while released), so STICK's ~47 Hz `timer_poll_fire_keys` never adds a
  second edge. The internal key bitmaps `05C1-05C4` are left alone (all zero once the boot
  keys are released). **K chord confirmed**: select.bin `A2CD cmp word [0xff18],0x286` —
  an exact compare, i.e. Shift+Ctrl+S+E held and nothing else (so `EK` together fails).
- **RNG** (§3): on reaching `BASE:0918` during the trace the oracle steps the harness LCG,
  sets AX to the *new* state (first call returns 13849, like the port's `krn_random`), and
  jumps to `092A`, the routine's own near `ret` (all callers use `call [cs:0x11A]`, near).
  The original's state word `[092B]` is left untouched. Before frame 0 the real routine runs.
- **Extra determinism requirements found** (all handled by the patch/wrapper; §3 said only
  "fixed cycles"):
  - `fastbioslogo=true` + `-fastlaunch`: DOSBox-X's BIOS logo waits ~1 s of *host* time while
    the emulated clock runs, which shifted the whole boot by 0.7-1.5 s run to run.
  - The RTC (and from it the BIOS tick count `0040:006C` and the DOS date) starts from the
    host clock; the patch pins it to 2000-01-01 00:00 UTC when `ORACLE_OUT` is set, the
    wrapper sets `TZ=UTC`.
  - File dates reach BASE: the restore list's FindFirst DTA lives at `BASE:0A59`, so the
    wrapper `touch`es every staged file to 2000-01-01.
  - `turbo=true` (DOSBox-X's fast-forward: no frame limiter, ticks back to back),
    `core=normal`, `cputype=386`, `nosound=true`, joystick off.
- **Wait-loop boundaries** (§1): `frames.jsonl` lines for them carry the loop's `site`
  name and an extra `"ip"` (where the CPU really was; diagnostic only, the port need not
  write it). Rate = 4*`[FF33]` ticks of `FF1B` since the last boundary, measured at the
  poll; arming is cleared by any boundary. Same record format, same `base.bin` record,
  same input/edge rules as the frame boundaries. The oracle executes `7358`/`A597`
  (`mov byte [ff1a],0`) itself like the frame sites.
- **The DOS console must not see the host path.** Every character the guest prints costs
  emulated instructions, and `mount c <path>` printed the path of `--out`: two runs of the
  same script into directories of different name length started the game a few
  instructions apart, and one byte (`FF29`, the last key's ASCII after the restore's Enter)
  came out `00` or `0D`, after which shop hook timers drifted. `oracle.conf` now runs
  `@echo off` + `mount -q`. Runs are byte-identical whatever `--out` is; the frame
  traces of the #47 baselines are unchanged.
- **`ip` vs site.** In the status screen the row loop and its release waits share one
  site; the port gets the same count by modelling the release waits as passes (below).
- `FF6C` player_name: the original stores the restored save's file name there (`CAVE`),
  the port `ZELIARD` — a real port difference, not an oracle artefact.

### What the port must emit at wait-loop boundaries

The port writes a frame line + applies the next token exactly as `trace_boundary` does
now, but with the site name of §1 and from inside the wait loops. In `--trace` mode
today the shops/status screen advance no token at all (`present` only counts
`trace_idle`), and in plain `--script` mode they take one token per 20-tick
`shop_frame` / per status iteration, including while text prints: neither matches.

- **Ticks, not frames.** Inside the wait loops the port must count 236.7 Hz ticks
  (`shop.c` already steps the `[A002]` hook per tick) and keep `ticks_since_boundary`
  (reset by every boundary, frame ones included) and an `armed` flag (set on loop
  entry, cleared by every boundary). At the poll: boundary if `armed ||
  ticks_since_boundary >= 4*speed`; then test the input; then one tick. Rendering for
  SDL can stay at 20 ticks, but must be decoupled from the boundary.
- `shop.c shop_wait_key` (71DF) → site **`71EC`**. Arm on entry, poll, test edges.
- `shop.c menu_select` and `yes_no` (7344/74D3) → **`7358`**. Order per pass: boundary
  rule → Alt edge = cancel → Space edge = choose → `dirs&3`: a move runs the 10×4-tick
  cursor animation (40 ticks, no boundary inside) and returns to the poll. **Drop the
  `held` debounce**: the original repeats the move every 40 ticks while U/D is held
  (seen: `D2` at the bank menu moves two rows, to "Deposit money").
- `town.c dlg_menu` (dialogue opcodes 0x81/0x89 → 6673 `call 74D3`) is that same widget:
  it must emit **`7358`** on the rule above and must **not** run town frames
  (`dlg_frame` → 68ED) while it waits; NPCs do not move during it. The rest of the
  dialogue (`dlg_wait_key`, `dlg_newline`, the box itself) stays on `68ED`: correct.
- Text printing (706C, 6 ticks/glyph), scrolls, the shops' timed waits (`shop_wait`), the
  greeting/goodbye animations, overlay loads and the death fade: **no boundary**, and no
  token consumed, whatever their length.
- `status.c status_loop` → **`select:AA58`**, one boundary per pass, the first at the
  first pass after `status_open` (the A084 probe does not count), then per the tick rule;
  with no time base inside select.bin this is "one boundary per pass" as long as each
  pass is one poll. Replace the `held` flag by the original's two **release waits**,
  each its own pass with a boundary and nothing else read in it (Enter is not seen
  there): on (re)entering a row, wait until `(dirs & 3) == 0` (A0D2/A1C3/A2C1); after an
  L/R step, until `(dirs & 0x0C) == 0` (A174/A25F/A373). A row change re-arms
  (the next pass is a boundary at once).
- Death → sage: the dying `717F` frames, then no boundary until the sage text's first
  `71EC` page wait (armed), then `68ED` when the town resumes.
- Not in the port yet, needed for these sites: the bank amount entry (**`bank:A2D8`**;
  the port deposits everything; one pass = redraw + `int 61h` + a wait of `[AD2F]` ticks
  that starts at 0x23 and drops by one per held pass — the oracle adds the original's
  redraw time to each pass, so holds longer than ~10 boundaries may drift), omoypro's
  Space wait (**`omoy:A04E`**) and the sage's name entry (**`kenj:A597`**, letters are
  not in the script language).
- `tools/harness/schema.py` `SITE_MODE` knows only `717F`/`68ED`, so `tracediff` compares
  nothing but `site` on the new lines. It needs a mode per new site: `71EC`, `7358`,
  `bank:`, `omoy:`, `kenj:` are town-side (`town`); `select:AA58` takes the mode of the
  last frame boundary before it (the status screen opens from either loop).

### Verified (wait-loop boundaries)

All runs below twice: once one at a time, once all seven in parallel into output
directories with names of other lengths. `frames.jsonl`, `base.bin` and every
`frame_NNNNN.png` byte-identical; the #47 town and cavern traces also identical to
their pre-change baselines.

| Scenario (save) | Script | Frames | Result |
|---|---|---|---|
| status screen in the cavern (`--town-col 205 --spells 1,2 --potions 1,2` → STAT) | `U3 .40 E1 .3 R3 .3 L1 .3 D1 .3 R1 .3 D1 .3 R1 .3 E1 .10` | 85, 30 at `select:AA58` | opens at f44 (first poll A0D2), magic L/R, row change to USE:, closes, f74 back at `717F` |
| NPC talk in Muralla (`--town-col 186` → NPC) | `.5 X1 .30 X1 .30 X1 .30 X1 .20` | 120, all `68ED` | box, second page, close, talk again |
| drug store buy + leave (`--town-col 111 --gold 5000` → DRUG) | `U2 .3 D1 .3 X1 .3 X1 .20 X1 .30 X1 .30 M1 .5 M1 .5 X1 .40` | 150, 106 at `7358`/`71EC` | two Ken'ko Potions (GOLD 5000 → 4900), cancel, "come again" page, town at f109 |
| bank deposit (`--town-col 138 --gold 5000` → BANK) | `U2 .3 D2 .3 X1 .10 L5 .3 U3 .3 X1 .10 X1 .10 M1 .10 M1 .10` | 80, 77 at `7358`/`bank:A2D8`/`71EC` | amount 0 → 50 → 53, deposited, balance 53 |
| death → sage (`--town-col 205 --life 4` → DIE) | `U3 .40 R40 .150 X1 .20 …` | 339 | dies in the cavern (last `717F` f118), sage text from f119 at `71EC`, two pages, town `68ED` from f255 |

### Verified (#47)

- Determinism: same script twice → `base.bin` and `frames.jsonl` byte-identical, in town
  (`R20 .10 L20 X1 .5 R30`, 87 frames) and in the cavern (`--load CAVE`, 146 frames);
  also identical with a fresh `build-oracle.sh` build (/root/build-test) vs the dev tree.
- Cavern: save at Muralla column 205 (`--town 1 --town-col 205 --save CAVE`), script
  `U3 .40 X1 .5 X1 .5 X1 .5 R10 X1 .5 X1 .5 R20 X1 .5 X1 .5 R30`: frames 1-2 town
  (`68ED`), from frame 3 `717F`; scroll_col 45 → 96 while R is held, FF35 (hero map row)
  7, EXP 0 → 5 → 10 as enemies die, LIFE 80 → 35; 3 RNG calls (frames 74, 106, 139). The
  port with the same script: RNG at 75, 107, 135, LIFE 50 — tracediff has something to
  report. With `U1` instead of `U3` the original does **not** enter the cave (the port
  does): Up must still be held at the extra `68ED` tail frame.

## 7. Scenario library (#49)

`tests/scenarios/*.zs`: one scenario per file, `# start: NAME.usr` (the save both
sides restore), `# make: ARGS` (the `port/zeliard` arguments that create that save,
ending in `--save NAME`), `# what:` (free text), then the script on one line.
`tools/harness/run_scenarios.sh [-j N] [--out DIR] [--png] [NAME ...]` runs oracle +
port + `tracediff --first-only` for each, makes any missing start save from its
`# make:` line, prints a table and exits non-zero on any divergence or failure.
It builds nothing: run it on the droplet after building the port.

Every scenario must be clean. The ones that die stop on their last dying `717F`
frame, because the port does not yet emit the Sage text's `71EC` wait-loop
boundaries (§6 "What the port must emit", #64).
