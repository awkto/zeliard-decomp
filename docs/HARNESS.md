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
is not a boundary: those loops read no input. The one cutscene with sites is the ending (enddemo.bin, `end:wait`, §6).

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
| sage's name entry (Record), and its two R/L release waits `A66A` / `A67F` | kenjpro | `A568` | `kenj:A597` | FF1E, FF18 bit0, FF1D, FF29, int 61h |
| bank amount entry, deposit | bankpro | `A295` | `bank:A2D8` | int 61h (held, not edges) |
| bank amount entry, withdraw (#65) | bankpro | `A42A` | `bank:A477` | int 61h (held, not edges) |

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

`'C` (#65) types one key: `FF29` = its ASCII on the token's first frame, nothing held,
for the sage's name entry. `C` is `A-Z` (either case, stored upper case), `0-9`, or
`<` for Backspace (08) -- what STICK's INT 9 table (04D0) produces. It stands alone (no
other letters in the token); a count holds nothing more (`'A3` = the key, then two idle
frames). The frame's `"input"` is `'A` / `'<` (appended to the held letters, if any),
identically on both sides.

`F` (#42 step 2b) presses **F7**, the original's "Restore Game": the kernel's `[11E]`
(STICK 092D) puts up *Sure?(Y/N)* at the next frame, and the tokens that follow are the
dialog's keys until it closes: `'Y` / `'N` answer, then in the *Input name:* box `'C` /
`'<` type and delete, `L`/`R` move the caret, `U`/`D` move the `*.usr` list, `X` (Space)
copies the highlighted name, `E` accepts (an empty box becomes *Re-Start*, then `E` again),
`M` backs out.  One token = one dialog frame; the game stands still meanwhile.  `F` stands
alone and a count holds nothing more.  It is a **port-only token**: it is ignored under
`--trace` (the oracle's own F7 restore is `ORACLE_LOAD`, `oracle-dosbox-x.patch`, and its
frames start after the restore), so no scenario uses it; `test_playthrough.c`'s
`restore_unit` covers the dialog and the load in-process.

`S` (#116) presses **F9**, the kernel's "Speed change / Select 0-9:" (`[114]`, STICK 07B6): the
tokens that follow are the dialog's keys until it closes: `'5` (a digit) sets `FF33 = 10 - digit`
and draws it, then any other token (a direction, `X`, `E`, `'A`) restores the screen; `M` backs out
of the digit prompt.  Port-only like `F`: ignored under `--trace` (the oracle has no site for it).

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
| bank amount | R −10, L +10, D −1, U +1 (repeating 35, 34, … ticks apart while held); X held = ok; M held = back | int 61h AH/AL |
| sage's name entry | `'C` types at the cursor (`'<` Backspace); L/R move the cursor (then wait for the release); E accepts; M cancels; X copies the highlighted `*.usr` name; U/D move that list | FF29, int 61h, FF18 bit0, FF1E, FF1D |

No scancode injection is needed: typed keys are direct `FF29` writes too. Y/N of the
kernel's F7 / Ctrl+Q boxes and F9's digits would be (`FF18` bits 0x20/0x40, `FF29`),
but those kernel boxes spin on `FF18` with no poll site, so they stay out of scope.

The file list of the name entry is the game directory's `*.usr`: the oracle's staged
directory holds only the start save, the port's `zeliard/` every scenario's, so a
scenario must not move that list or copy from it (Space) -- the names differ.

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
- `DIR/walk_NNNNN_KKK.png` (with `--png-every`, #117): the walk-in (fight.bin `7C6E`, 26 passes at every
  cave mouth, door and restart) has no boundary, so its pictures are not frames.  The oracle writes the
  screen as each pass waits (`7C95` / `7CD4`: the hero was just drawn, `7F82` is next) under the
  number of the next boundary and a running index K; the port writes the same names from its
  walk-in presents.  `pixdiff.py` compares them like frames (a walk on one side only fails).
- `DIR/dsv_NNNNN_KKK.png` (with `--png-every`, #118): the screen dissolve (video `[2040]`, gmmcga `2130`: 8 passes,
  no boundary) is the same: the oracle writes the screen after each pass (`2184`, the delay loop that follows
  it) under the number of the next boundary and a running index K; the port writes the same names from
  `town_dissolve` / `shop_dissolve`.  Compared by `pixdiff.py` like the walk pictures.

- **The exit record (#81).** When the game leaves for DOS (the Sage's "No", kenjpro A934
  `jmp far [FF00]` -> INT 21h AH=4Ch, AL 0) the oracle's hook in `DOS_Terminate` writes one
  last line and a last `base.bin` record (so the files stay the same length) and stops cleanly
  (exit status 0, `oracle.log`: "program exited to DOS"):
  `{"f":N,"site":"exit","base":"0x..","rng_calls":k,"input":"M","code":0}`.
  N = the frame the program ended in (the number its next boundary would have had), `input` and
  `rng_calls` as for a boundary line of that frame, `code` the DOS return code. The port's
  `--trace` writes the same line (`code` 0, no `fields`) when it leaves through
  `SHOP_EXIT_GAME`; `tracediff` compares `input`, `rng_calls` and `code` there (no fields, no
  allow-list) and `contact_sheet` skips it (no screen). A script that ends first never reaches
  it. Scenario: `sage_exit`.

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
cutscenes). (The game exiting to DOS is not a stall: §4, the exit record.) The last line of `DIR/oracle.log` names the CS:IP it stopped at; `DIR/final.png`
has the screen on any failure. `ORACLE_MENU_TICKS` overrides the wait-loop rate (§1). `ORACLE_DEBUG_MS=N` writes `boot_<ms>.png` every N emulated ms
until frame 0 (that is how the boot sequence was debugged).

The patch is one file, `src/misc/zel_oracle.cpp`, plus five hook lines: a check at the top
of the normal core's instruction loop (`ORACLE_Exec()` when `CS == BASE`), a call per
emulated millisecond in `TIMER_AddTick`, a fixed RTC start in `CMOS_Init` (only when
`ORACLE_OUT` is set), `ORACLE_DosExit()` at the top of `DOS_Terminate` (the exit record, §4), and the Makefile.am entry. Without `ORACLE_OUT` the binary is plain
DOSBox-X.

**Speed** (Debian 13 droplet, 8 vCPU, one core used): boot + intro skip + F7 restore = 1.6 s
wall (22 s emulated); then ≈160 game frames per wall second (2040 frames in 12.7 s,
≈13x real time). `cycles=fixed 3000` is the cost driver (the frame busy-wait is emulated);
it is kept because every DOSBox capture in docs/ was taken at 3000 and the 84.5 ms frame
(20 ticks at speed 5) is never overrun there. `frameskip` makes no difference.

### Start option: `ORACLE_CAVE` / `--cave` (#111)

Some rooms have no route a script can walk (MP90 and MPA0, Jashiin's: MP8D is Alguien's room,
and its exit door leads back to MP84; MP84's own (30,5) door to MP90 is only a long walk from
anywhere).  The start save can only be a town (the restore ends in town.bin, section 1), so the
harness may rewrite **one town cave record** and let the game's own cave-mouth code do the rest:

    ORACLE_CAVE=I,COL,ROW,SIDE,MAP  tools/oracle.sh ...        # oracle
    port/zeliard --load NAME --cave I,COL,ROW,SIDE,MAP ...     # port
    # cave: I,COL,ROW,SIDE,MAP                                 # a scenario header line

`I` is the record's index in the town map's C00B table (5 bytes each: `u16 col, u8 row, u8 side,
u8 map`), `MAP` the system map index (0x1B = MP84, 0x1D = MP90).  The oracle patches the five bytes at
`BASE:[C00B] + 5*I` once, at frame 0 (the first town boundary, before its record is written;
`oracle.log`: "cave poke"); the port writes the same five bytes into its town map (`TownMap.raw`
and `caves[]`) after the town loads.  Everything after is the original's code on both sides
(town.bin `6FF8`..`703D`, fight.bin `7D2D`), so the hero arrives exactly as he would from that mouth;
the trace compares the town's C000 image too, so the patch has to be the same bytes.
`tools/harness/run_scenarios.sh` and `pilot_scenario.sh` pass the header's value to both sides.
It changes no gameplay: it is a different destination, not a state edit (no flag, no stat, no
[FF21] is poked).  `jashiin_fight` uses `0,29,6,0,0x1B`: Pureza's left-edge mouth (record 0, normally
MP80 (111,21)) into MP84 one column left of its (30,5) door; the walk-in moves him one column.

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

### The ending: `end:wait` (#113)

enddemo.bin (started by omoypro `A03D`; docs/CUTSCENES.md §4) has no game frame and reads no input,
it is paced by the 236.7 Hz tick `[FF1A]`.  Its boundary is therefore the **completion of a wait**,
not an elapsed-tick rate: how many ticks the original spends drawing between two waits is emulated
CPU speed, which the port does not model.  Tracing of it starts when the CPU reaches enddemo's entry
`6002` (recognised by its code bytes); from then on every one of these is a boundary, site
`end:wait` (and an `ip` as for the other wait sites):

- the exit of any `cmp [cs:FF1A],x ; jc back` loop: enddemo's own `62EE` (act 1) and `6945` (credits),
  and gdmcga's seven pass waits (`31B4` dissolve, the wipe `3E8B`, the sand `38E6` rings, `3544`,
  `3CEE`, `3F1E`, `40B1`, `4193`), seen as the instruction after the `jc` being reached;
- the credits' two other waits: `0xF7` (`[FF21]` set by the score, exit at `6760`) and `0xF9`
  (`[FF50]` reached, exit at `678A`).

The line has no fields in the port (`trace_write_site`: site, `rng_calls` 0, the input); `tracediff`
compares the site and the count (the site has no field mode), and the **PNG of the boundary is the
evidence**, so a scenario through the ending needs `--png-every`.  The port's `Cutscene.boundary`
callback fires at the end of `cs_wait` / `cs_wait_hard` (every gd wait goes through the latter) and at
the two credit waits; `main.c cutscene_boundary` writes the line and PNG and takes the next token
(`--script` tokens are consumed per boundary there, not per rendered frame).  The same count came out
of the oracle and the port at the first try (3691 boundaries, 872..4562 in `jashiin_ending`).  The
oracle run is long in emulated time (the ending is ~390 s; `run_scenarios.sh` raises
`ORACLE_MAX_MS` to 1200000), short in real time (1.5 min).  The oracle's last boundary is the
credits' final wait; after it enddemo spins in its hotkey poller (`66C8`) forever, so a script
must have exactly as many tokens as boundaries to end there ("script done"), else the oracle stalls
(exit 6, `# ends: stall`).

### The opening: `intro:wait` (#34)

The ending's mechanism, applied to opdemo.bin: `ORACLE_INTRO=1` (the oracle) / `--intro --trace DIR` (the
port).  The oracle does **not** tap Return to skip the demo (boot step 2 above); BASE is known as always, and
tracing starts when the CPU reaches opdemo's entry `6002` (recognised by its code bytes `FA BC 00 20 FB 2E C6
06 1D FF 00 ...`, not by address alone).  From there every wait completion of the ending's list is a boundary,
site `intro:wait` (opdemo's own `63AB`/`6456`/`6A07` loops end in `cmp [cs:FF1A],al ; jc`, gdmcga's passes are
the same seven); the demo's other loops (the title's spin on `[FF26]`, the narrator's keyboard polls) are not.
The run ends when town.bin's frame loop is reached (`68ED`: "intro done: the town loaded"), so a script needs
**at least** as many tokens as boundaries, and both sides stop at the hand-back (the port: the cutscene
returns, `--intro --trace` implies "intro only").  No input is ever pressed (a Space/Return would abort an act).
The count is 7439 boundaries, ~870 s of emulated time, ~2 min of real time on the droplet; the picture at every
boundary is the evidence.  `run_scenarios.sh` takes `# intro: 1` (no save, no `--load`; `ORACLE_INTRO=1` and
`--intro`) and `# png-every: K` (a long scenario's own PNG interval, as `jashiin_ending` would want one too).

What it found (all fixed in `port/cutscene.c`):
the storm demo's demon-eye frames (`6EB0`) store the derived plane as plane **1** and `p1 ^ p2` as plane 2
(they were swapped: magenta eyes where the game has red/cyan); the stencil that lays them over `hime.grp`
(`6ED8`) is built from the *destination* (`~p0 & ~p1 & p2`) at byte offset `0x6D3` of the picture, not from the
face at its origin; the narrator's ink and shadow bytes (`6D5B`/`6D5C`) start at 0, so the script's first stray
`P` is drawn black; `yuu3`'s third plane (`6FAC`) is `p0 | p1`, `p0 & p1`, `p1` (not the eye formula) and `yuu4`'s
stencil (`6F41`) cuts colour 3 without plane 2 out of planes 0 and 1; and the last `NARR()` mistook the
script's `0xFF` for an abort, so **the whole epilogue (273 waits: the doorway, "At last, the door of destiny was
opened...") never ran** in the port.  With those, all 7439 boundaries are pixel-identical (`opdemo_intro`).

### What the port must emit at wait-loop boundaries

**Implemented (#64).** `port/town.h` `WaitClock` (`wait_arm` / `wait_tick` /
`wait_poll` / `wait_boundary_now`) is the one tick clock; `main.c` installs
`trace_wait`, which writes the line with the site of §1 and applies the next token,
and every frame boundary resets the clock. Plain `--script` (no `--trace`) still takes
one token per presented frame (every 20 ticks in a shop, one per status pass). What the
port does, per the spec below:

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
- **#65:** the bank amount entry (`shop.c bank_enter_amount`, **`bank:A2D8`** deposit /
  **`bank:A477`** withdraw, armed at A295 / A42A): one pass = redraw + poll + `int 61h`;
  no direction = one tick; a held direction adjusts, then waits `[AD2F]` ticks (0x23,
  then one less per held pass; 0 − 1 gives 1) with no boundary inside. The redraw costs
  no tick (#80): the wait ends just after a tick and the redraw, poll and `[FF1A]` reset
  fit well inside it (at `[AD2F]` 1/0 two passes fit in one tick: +40 per 20 ticks held),
  so a held pass is exactly `[AD2F]` ticks and the port counts none for the redraw;
  `bank_hold` (U45, D40) checks it through the 1/0 regime. omoypro's Space wait
  (`shop.c omoy_wait`, **`omoy:A04E`**: only the Space edge ends it). The sage's name
  entry (`shop.c kenj_name_edit`, **`kenj:A597`**, typed keys via `'C`); its R/L release
  waits spin on `int 61h` without idle_poll and are polls of the same site on both
  sides (else a held R would stall the oracle).
- `tools/harness/schema.py` `site_mode()` / `site_modes()`: `71EC`, `7358`, `bank:`,
  `omoy:`, `kenj:` are town-side (`town`); `select:AA58` takes the mode of the last
  frame boundary before it (the status screen opens from either loop). `tracediff`,
  `port2base` and `contact_sheet` use them.
- Found while matching (fixed in the port): fight.bin leaves the status screen with
  `jmp 6F9B` (72D6), one more whole frame inside the same step; dialogue_start runs a
  68AF tail frame before the box (6363); the talker is frozen as type 7 (6297) and
  the dialogue's waits are full 68AC frames (NPCs move); STICK has consumed FF75 by
  the next boundary (status passes, town frames, the yes/no box); the shop printer
  costs 6 ticks a byte (7084) and its page-break count (7269) skips `FF xx` pairs.
  #65: menu_select (735D/7366) only *tests* FF1D/FF1E, it does not clear them on a
  choice (the bank's amount entry starts with FF1D still set); STICK's resource loader
  clears FF1D (not FF1E) on every successful open (`sar_open_entry` 0CDC), so a shop's
  overlay load and the town reload after it (6ECA) drop a pending Space edge.

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

### Verified (#65)

The oracle change (`'C` keys, the `bank:A477` site, the kenjpro release-wait polls) was
built as a separate binary first: the 24 scenarios of the time gave byte-identical
`frames.jsonl` and `base.bin` with the old and the new binary (48 files), all clean.
Then the four new scenarios, all clean: `bank_deposit` (111 frames, 36 at `bank:A2D8`,
GOLD 5000 → 4878, balance 122), `bank_withdraw` (109, 43 at `bank:A477`, GOLD 100 →
163, balance 75 → 12), `omoya_wait` (45, 11 at `omoy:A04E`), `sage_record` (132, 38 at
`kenj:A597`, FF6C `SAGE` → `HER5`). The amount and name screens are pixel-identical
to the oracle's apart from the HUD place label and the file list (§2).

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
ending in `--save NAME`), `# what:` (free text), optional `# allow: PATTERN [MODE]`
lines (tracediff allow rules for that scenario only, on top of `tools/harness/allow.tsv`:
for a known, filed divergence the scenario has to step over), optional
`# allow-px: x0,y0,x1,y1 [frames A-B] -- reason` lines (pixel allow rules, below), `# intro: 1`
(the opening demo from boot instead of a save, section 6) and `# png-every: K` (this scenario's own PNG
interval), then the script on one line.
`tools/harness/run_scenarios.sh [-j N] [--out DIR] [--png-every K] [--png] [NAME ...]` runs
oracle + port + `tracediff --first-only` for each, makes any missing start save from its
`# make:` line, prints a table and exits non-zero on any divergence or failure.
**Pixels (#91).** Both sides also write `frame_NNNNN.png` every K frames (default 1; `0` =
fields only) and `tools/harness/pixdiff.py` compares them frame by frame. A scenario is
`clean` only if fields AND pixels match; otherwise the row says `DIVERGES` (fields; the
pixel summary is appended) or `PIXELS` (fields match) with `first-px-frame`, `px-frames`
(frames with a difference), the `bbox` x0,y0,x1,y1 (inclusive, union over all frames) and
`npix`. `# allow-px:` ignores an inclusive rectangle (320x200 frame coordinates), optionally
only in frames A-B (or one frame A), before judging; the reason after ` -- ` (or an em
dash) is mandatory and the line should cite the filed issue. The first differing frame's
oracle|port|diff image goes to `DIR/NAME/pixdiff/`. Passing scenarios have their PNGs
deleted (`--png` keeps them); failing ones keep them.
**Walk-in pictures (#117).** The cave-mouth walk-in (`7C6E`: the backdrop `gfmcga [301E]` paints from
roka.grp, remapped by the door letter & 7, the hero stepping 8 px a pass with a black trail, the HUD still
the map just left) is diffed the same way from the `walk_*.png` files above; `cave_door` (town mouth and a
cavern door), `key_door` / `gate_walking` (door to a town), `cave_forced_death` and `jashiin_kill` (the
door into the [E6] room) cover it.
**Dissolve pictures (#118).** The eight passes of the building-entry dissolve (town.bin `6E9B`), the overlay's exit
dissolve and the sage's death entry (`6191`) are diffed from the `dsv_*.png` files above, the music fade aside (the
music state is not a trace field); `drug_buy`, `bank_*`, `omoya_wait`, `sage_*`, `death_sage` cover them, every
scenario that leaves a town by a cave mouth covers `7033`, the deaths cover `99A8`, and `feather_dissolve` covers the
Kioku Feather's `A5A9`.  An oracle built before #117 writes none: rebuild it
(`tools/harness/build-oracle.sh`).
**Scenarios that end in a cutscene (`# ends: stall`).**  The oracle writes a boundary only at the
sites of section 1 (and the ending's `end:wait`, §6), so a script that ends in a cutscene without
sites (none is left in the library since #113: the ending has them) leaves it
without a boundary until `ORACLE_STALL_MS` runs out and it exits 6.  A scenario with the header
`# ends: stall` takes that as the end of the oracle's trace (the stall limit is 60 s of emulated
time unless `ORACLE_STALL_MS` is set; the Tear scene before it is shorter) and the port's trace has to end on
the same frame (tracediff reports a length mismatch otherwise).  Any other exit is still an `ERROR`.
**Isolated game dirs (#100).** Each scenario's port run uses `DIR/NAME/pdir`: symlinks to
the original game files plus a copy of only that scenario's start save, the same set
`tools/oracle.sh` stages for the oracle, so screens that list saves (the Sage's name entry)
see the same list on both sides and a save the port writes stays in `pdir`, never in the
shared `zeliard/`. (Missing start saves are still made in `zeliard/`, first.)
It builds nothing: run it on the droplet after building the port.

**Story flags and long routes (#75).** `--flag N[=V]` (hex, repeatable) sets story-flag
byte `[N]` (0-48, docs/STATE_PAGE.md) to V (default FF) before the save is made, e.g.
`--flag 12=08` for the Hero's Crest that moves Bosque's sentry. A scenario may carry
`# pilot: ROUTE`: its script is then *generated*, not hand-written:
`port/zeliard --load START --trace DIR --pilot ROUTE --emit-script FILE` lets the
playthrough autopilot (`playthrough.c` / `nav.c`) play ROUTE, asked for its input once per
harness boundary (`play_pilot`), and writes exactly what it fed as a run-length `--script`
(`--pilot-tail N` idle frames after the route, default 10). Replaying that script with
`--trace` gives a byte-identical `frames.jsonl`. ROUTE is `;`-separated steps:
`town-cave C`, `town-shop C [MENU]`, `town-edge 0|1`, `door C,R` (`door -1` = the exit
door 72F1 puts down), `cell C,R`, `break C,R`, `boss`, `keys TOKENS` (fed verbatim in `--script`
syntax, for a move the autopilot cannot find; #87). `tools/harness/pilot_scenario.sh
NAME` makes the start save if missing and rewrites the scenario's script line from its
`# pilot:` line; rerun it whenever a port fix changes what the autopilot sees (it is the
port's view: the oracle then checks it).

**Live prediction (#90).**  The survey probes every move from `game_place` (hero at rest,
`[9F07]` restarted, a platform wherever the probe put it), but the pilot is asked at a
boundary half-way through a main-loop pass, with the C platforms already moved for the frame
and `[9F07]`/`hero_anim` at whatever parity the run has reached, so a move across MP61's
fix[15]/fix[13]/fix[10] that worked in the plan missed live.  `Game.step_begin` lets
`nav.c` keep a copy of the state at the top of each pass; before it commits to an edge it
replays the pass from that copy with the inputs already fed, checks the replay has reached
the live state, feeds the edge's macro and reads off where the hero settles (`predict`).  An
edge is taken when that lands on its target or closer; none ready means wait, unless waiting
itself loses ground (a platform carrying him off its end: `hold_on`).  The same replay drives
the Tarso fight (`fight_lookahead`: twelve frames of each stick/swing choice, scored by the
boss's LIFE lost against the hero's).  `tarso_fight` is the first route through moving
platforms; the Dragon/Alguien routes (MP71, MP81 lifts) are separate follow-ups.

Every scenario must be clean. `death_sage` goes on from the dying `717F` frames
through the Sage's text (`71EC`) back to Muralla; `status_cave`, `drug_buy`,
`bank_menu`, `npc_talk` and `npc_yesno` cover the other wait loops (#64);
`bank_deposit`, `bank_withdraw` (amount entry: repeat, clamp, cancel), `bank_hold` (long holds, #80), `omoya_wait` and
`sage_record` (typed name, Backspace, cursor step, the A862 write) the last three (#65).
`sage_exit` ends the run in the game's exit to DOS (the Sage's "No"; the exit record, §4, #81).
Caverns 4-6 (#49): `ice_crawler`, `ice_icicles` (EAI4), `water_slimes`, `water_current`
(EAI5), `fire_fish`, `fire_beast` (EAI6); table at the end of §8.  Not reached yet: the
cavern-5 tall spitter, the cavern-6 tall ghost in a fight (it is on screen in `fire_fish`),
a hero standing in a current.  Caverns 7-8 (#49): `heat_hedgehog`, `heat_spitter`, `heat_current` (EAI7, heat),
`absor_gunner`, `milagro_gunner` (EAI8).  Cavern 9 is MP90 only (Jashiin's room: MP84's (30,5) door, no town mouth) and MPA0 (the fight): `jashiin_fight`
reaches both with a `# cave:` start (section 6).  The Dragon is killed by `dragon_kill`.  Not reached yet: any B gate or
C platform in caverns 7/8 (MP80's (72..78,29..31) gates are not reachable from the Pureza
mouth on foot), the cavern-7 tall ranged walker, a cavern-8 tall charger in the open, the
Asbestos cape (`--worn 5` now sets it, #86: not used by a scenario yet).

## 8. Triage runbook (for agents) (#51)

Instructions to a Claude session starting cold. Goal: find divergences between the original
(oracle) and `port/`, classify each one by looking at it, and file or fix it with evidence. No
human playtesting. The user's rules apply throughout: never `git commit` unless told to.

### 8.1 Preconditions
1. **Do not run heavy work on pro** (the user's main workstation; check `free -m`). Oracle runs, scenario
   batches and monkey soaks run on a `claude-test` droplet (`doctl --context teamadf`, at most 2, tag `claude-test`).
   Only single scenarios on pro, under `nice -n 19 ionice -c3`.
2. Is a droplet up? `doctl --context teamadf compute droplet list --tag-name claude-test --format ID,Name,PublicIPv4,Status`.
   Another session may be using it: reuse it, don't touch droplets without that tag. If none, ask the user before
   creating one (`INSTALL_DEPS=1 tools/harness/build-oracle.sh` provisions a fresh Debian one).
   **Destroy it when the pass is done** (`doctl --context teamadf compute droplet delete ID`) and confirm the list is empty.
3. Oracle present? `ssh root@HOST ls /root/dosbox-x/src/dosbox-x`. If not (or `tools/harness/oracle-dosbox-x.patch`
   changed): `cd /root/decomp-zel && INSTALL_DEPS=1 tools/harness/build-oracle.sh /root/dosbox-x` (~7 min), then
   `export ORACLE_DOSBOX=/root/dosbox-x/src/dosbox-x`.
4. Sync and build (the game data `zeliard/` already lives on the droplet; other agents edit `port/` concurrently):

       rsync -az --exclude '*.o' --exclude local.mk --exclude port/zeliard ~/git/decomp-zel/port ~/git/decomp-zel/tools ~/git/decomp-zel/tests root@HOST:/root/decomp-zel/
       ssh root@HOST 'cd /root/decomp-zel/port && rm -f local.mk && make -j8 zeliard'

### 8.2 Run
    ssh root@HOST 'cd /root/decomp-zel && export ORACLE_DOSBOX=/root/dosbox-x/src/dosbox-x && tools/harness/run_scenarios.sh -j 4 --png --out /root/triage/sc'
    ssh root@HOST 'cd /root/decomp-zel/port && make monkey N=200 FRAMES=600 SEED=7 OUT=/root/triage/monkey'   # ASan/UBSan soak, port only

`run_scenarios.sh` prints one row per scenario (`clean` / `DIVERGES  frame N: field` / `ERROR`) and leaves
`/root/triage/sc/NAME/{o,p,diff.txt,oracle.out,port.out}`. `--png` makes every frame's PNG (needed in 8.3).
`monkey` writes `report.md` (grouped by failure, each with a minimal repro command). Use a new `SEED` each pass.
`make soak` (`monkey.py --soak --check`, up to 6 jobs, droplet only) is the v2 profile: random starts from towns, every
cavern, the scenario saves (with their `# cave:` / `--magic`), a random-length prefix of the scenario's own script (so the run
starts inside a boss room or at a door) followed by random input, the ending, and random `--video` / `--audio`. `--check`
also runs each case under `--trace` and tests per-frame invariants (`check_trace`: LIFE <= max+1, magic counts, EXP and gold
not falling in a cavern, bounds). The LIFE bound is `max+1` because the original's regeneration (fight.bin 71B1..71B7)
has no clamp and takes an odd max-1 to max+1.
Monkey finds crashes/hangs/sanitizer hits only; it does not compare against the oracle. To oracle-check a monkey repro,
put its script in a scenario file (`tests/scenarios/NAME.zs`, §7) and run it as above.

### 8.3 For each failure
- `ERROR`: read `oracle.out` / `o/oracle.log` (exit codes in §6) or `port.out`. A stalled/timed-out oracle or a port
  crash is a finding too, but first check it is not an environment problem (droplet out of memory, save missing).
- `DIVERGES`: render the sheet at the first diverging frame N (from `diff.txt`), **then open the PNG and look at it**
  (copy it to pro with `scp root@HOST:/root/triage/sc/NAME/sheet.png /tmp/` and Read it):

      python3 tools/harness/contact_sheet.py /root/triage/sc/NAME/o /root/triage/sc/NAME/p --frame N --span 2 --out /root/triage/sc/NAME/sheet.png

  Rows are frames N-2..N+2; columns oracle | port | diff (red = changed pixels); captions list the differing field
  names (if the PNGs are missing it prints the commands to rerun). The sheet shows effect, `diff.txt` shows cause: the
  first differing field usually is the cause, later ones follow from it (`tracediff.py ... --show 80` for all). Also
  compare the sites: `oracle 717F / port 68ED` means a desync (a transition one side took and the other did not), not a rendering bug.
- Classify:
  1. **Real port bug**: the original's behaviour is visible in the oracle column and the port differs. Confirm in the
     disassembly (`disasm/`, `docs/`): name the routine and address. This is the default for any state-field divergence.
  2. **Oracle or harness flaw**: wrong schema address/size in `fields.tsv`, field the port should not model, port trace
     emitted at the wrong boundary, input applied a frame off, oracle timing or RNG hook. Evidence: the field's
     address in the original is not what the schema says, or the oracle value is not plausible game state (the
     picture agrees with the port). Fix the harness (`fields.tsv`, `oracle-dosbox-x.patch`, `trace.c`) and say which.
  3. **Acceptable / unobservable**: only for scratch bytes, timer phase and similar that no code path can read.
     Add one commented line to `tools/harness/allow.tsv` with the **address and the reason** (who writes it, why
     nothing reads it). "Looks harmless in the picture" is not enough; no address-level justification means it is not allowed.

### 8.4 Issues (`github/zeliard-decomp`)
1. Search first: `glab issue list -R github/zeliard-decomp --all --search "KEYWORD or FIELD"` (also closed ones; a
   regression of a closed issue gets a note on it, or a new issue linking it, not a silent duplicate).
2. New issue, title `BUG: ...` (port deviates from the original) or `Harness: ...` (tooling), assignee altanc:

       glab issue create -R github/zeliard-decomp -a altanc -t "BUG: <what, where>" -d "$(cat body.md)" --no-editor -y

   Body (see #60, #63 for the tone): one paragraph of what is wrong; the **first diverging frame** with the field
   names and both values (paste `tracediff` lines); the **exact repro** (the `# start`/`# make` lines and script from
   the `.zs`, the `port/zeliard ... --script` line, and `tools/oracle.sh ... --png-every 1` for the oracle side);
   the classification and the address evidence; the contact sheet.
3. Contact sheet: glab has no `--attach`. Upload it and paste the returned markdown:

       glab api projects/:id/uploads --form file=@sheet.png     # run inside a clone of the repo; else use the URL-encoded path github%2Fzeliard-decomp
       # JSON reply has "markdown": "![sheet](/uploads/<hash>/sheet.png)"; put that line in the description or in
       glab issue note IID -R github/zeliard-decomp -m "![sheet](/uploads/<hash>/sheet.png) first diverging frame N ..."

   Fetch the PNG from the droplet first (`scp`). If the upload fails, put the droplet path in the issue and keep the
   droplet files until the issue is closed.

### 8.5 Fixing
- Faithful to the disassembly, not to what looks right: cite the original's address (`seg:offset` / `disasm/` file)
  in the code comment and in the issue note. Do not tune the port to match the oracle's numbers without finding the
  routine that produces them.
- The scenario that caught it stays in `tests/scenarios/` (add one if the catch came from monkey or by hand: `.zs`
  with `# start`, `# make`, `# what`, script) and must go `clean` on the droplet.
- `cd port && make test && make verify` must stay green, and a full `run_scenarios.sh` must show every other scenario still clean.
- Close with `glab issue close IID -R github/zeliard-decomp` after a note giving: what changed (file/function), the
  address evidence, the scenario name and the clean result. Reference the issue (`#N`) in the commit message when the
  user asks for a commit.
- Clean pass (everything `clean`, monkey 0 failures): note it on the open #51/#46 tracking issue with the seed, counts and port revision.

| Scenario | Start | Covers |
|---|---|---|
| `sword4`/`sword5`/`sword6` | Muralla 205, LV 3, LIFE 255 | the sword.grp section 1/2 shapes, right then left |
| `gate_walking` | `CAVE` | out of MP10 walking, Muralla, back in: `[C2]` bit 1 kept (#69), `79DC`'s locals reset, the `7AAA` side step |
| `key_door` | Satono 4, one Key (`--keys 1`) | `7E15` on MP10 (128,32), through it to Satono and its `61B4` walk-in, back out |
| `cave_door` | same | MP10 (141,32) → MP1D: door frame, `[C4]`, walk-in |
| `crab_fight` | Satono 4, LV 8, sword 3, one Key | MP1D: Cangrejo to the kill (#71: part records' +7..+F are the previous map's bytes past MP1D's end at C238, the scroll re-mark `6771`/`68FF`, `A5C0` turn on landing, `A510` part 0x14 phase, gfmcga `3729`/`379B` death sparks in `EDA0` with their RNG draws), the `71DA` reward (EXP +120, ALMAS +150, no gold: #68), `72F1`'s exit door and its `jmp 6042` restart (`628A` drops the press edges), out to MP10 |
| `elevator` | `TOUGH` | MP10's (48,24) A elevator up and down (`8024`/`80AF` 0-erase) and `8009`'s hit on a frog under it |
| `pulpo_fight` | Bosque 138, LV 16, sword 4, one Key (`# pilot:`) | the column-142 mouth → MP30 → (21,6) → MP20 (205,47) → the locked (171,54) → MP2D: Pulpo to the kill (#75: TAKO's 7-column pose layout `A401`, ink hit byte 0 `A4D4`, death `A530`; EAI2 plant fall test `A3A0`, frog coin `A7FD`/`A823`; door restart `7D61`→`6049` 9F20..22 and `628A` edges), the reward, out through 72F1's door |
| `pollo_fight` | Bosque 12, LV 16, one Key, `--flag 12=08` (`# pilot:`) | past the sentry (dialogue), column-7 door → MP31 → the boss door (188,20) → MP3D: Pollo to the kill (#75: TORI's 9-column layout `A4C1`, phase = whole byte, hover on the hero's column `A3FD`, step limits `A58F`/`A59D`, death `A60A`, `A231` 0FFF list end; the egg's `+B/+C` from fight.bin's shot pass `8366`; EAI3 spider `A300`/`A420`/`A441`; town→cave `701C` FF1D clear), the reward, out through 72F1's door |
| `ice_crawler` | Helada 4, LV 2, sword 1, LIFE 800 (`ICE`) | MP40 (cavern 4): shell crawler, ice slide, a rope (Space pressed on it is dropped on leaving, `6327`), spinning blade (`A756/A7CE` frame column), dividing slime: spare claim (vec 31 `98C5`), split refused → walks on (`A512`), split, copies, almas |
| `ice_icicles` | Helada 222 (`ICER`) | Helada's right exit at `[83] == 0x1B` (`6CB5`), MP41: both icicle classes fall (RNG), the crawler's curl/dash and wall-jump turn (`A444`) |
| `water_slimes` | Tumba 4, LV 4, sword 2 (`TUMBA`) | MP50 (cavern 5): the floor block that gives way under the hero (item 0x11, `8E8D`), dividing slimes split again and again (`A641..A77C`) |
| `water_current` | Tumba 265, LV 6, sword 4 (`TUMBAR`) | MP50: the water surface animation (gfmcga `315C`, FIGHT.md §2), ropes, deep water (hazard), diving floaters, turning charger; LIFE runs out: death, the Sage, Muralla |
| `fire_fish` | Dorado 4, LV 8, sword 4 (`DORADO`) | MP61 (cavern 6): the glinting cells drawing KRN_RANDOM in the draw (`319E`), a falling rock, a charging beast killed, almas |
| `fire_beast` | Dorado 210 (`DORADOR`) | MP60: down the rope onto a charging beast, then west under flying fish (one killed) and falling rocks (one crumbles) |
| `heat_hedgehog` | Llama 269, LV 16, sword 5, LIFE 800 (`CALIR`) | MP70 (1,22) (cavern 7): the slope west onto two fast hedgehogs (`A7E0`: the "turned" bit clears when *facing* the hero), almas, the heat (`704F`, 15 LIFE + message every 64 frames), regen past max_hp (`71B1`, no clamp), off the ledge |
| `heat_spitter` | Llama 8 (`CALI`) | MP70 (152,7): down the rope to the tall spitter: it walks in, spits (knock-backs); the sword meets only the lower record.  `A6A6/A6AF jnz A661`: a facing spitter that does not spit still waits the every-2nd-frame gate |
| `absor_gunner` | Pureza 4 (`PUREZA`) | Pureza's left edge → MP80 (111,21): sentry gunners (left shots damage 1, `A673`), two killed; `A680` wraps any phase >= 3 to 0 (after a shot phase 4 → 0, not 2) |
| `milagro_gunner` | Esco 205 (`MILAGRO`) | MP81 (123,6): the tall charger against its wall, the (138,7) gunner killed, walkers and the (153,38) flying chaser below; the gunner's ledge shuffle (`A5BA`/`A5E1`) tests the record's own byte +0, not the ring cell |
| `agar_fight` | Tumba 4, LV 20, sword 5 (`AGAR`, `# pilot:`) | MP50's (25,14) door into MP4D from its far side; Agar to the kill (ZELA's column-major parts `A467`, poses `A4EA`, hop `A307`, walk `A371`, bolt `A4F2` with vec 27's BL regardless of CF, #84), reward, 72F1's door out |
| `vista_fight` | Dorado 210, LV 20, sword 5 (`VISTA`, `# pilot:`) | MP60's (14,5) door into MP5D from its far side; Vista to the kill (MEDA's 14-column buffer `A539`/`A4AA`, row by column `A6ED`, drips `A3C1`), reward, out |
| `paguro_fight` | Llama 200, LV 20, sword 6 (`PAGURO`, `# pilot:`) | Llama's column-222 door → MP73; Paguro (ZEL2) to the kill and the reward; the parts' +7..+F are the *town* map's bytes (`shell_enter_cavern` seeds `c000` from the town image). |
| `paguro_return` | same | `paguro_fight` on through 72F1's exit door to Llama (#85): the 0x80 NPC at 222 auto-talks (62ED), the Elf Crest opcode 0x83 (6685) runs 6AED mid-visit and pokes only its bytes (the live NPC records and 0xFD markers stay: `town_sync_raw`); a second talk, and a held Left ends the 65A1 wait (65DE: any FF17 direction) |
| `edge_press` | Muralla 12 (`EDGEP`) | #79: Space on the frame before each town edge (Muralla → castle → Muralla) is latched (FF1D = FF) and dropped by `6D30`'s `[10C]` loads (STICK `0CDC`) and `60C3` |
| `tarso_fight` | Dorado 4, LV 25, sword 6, one Key, Feruza shoes worn (`--worn 1`), `--flag 2B=08` (`TARSO`, `# pilot:`) | Dorado's left edge → MP61, across its C platforms (live prediction, #90) to the (12,27) door → MP60, round the seam to the front door (309,41) → MP6D; Tarso to the kill (LEGA, #86), reward, 72F1's door out to MP60 (28,47).  Also: EAI6's beast turns at a wall when the step down is refused (`A947`), its `A90A` bounce is dead code; the tall-sprite step's marker test is rows -1..+3 of the column (`A595`, -2 going left, EAI2/5-8); death sparks spawn inside the 302C scan, interleaved with the cavern-6 cell animation's KRN_RANDOM draws; `94FF` writes the ring column into a refused respawn's +3 (tall pairs: upper record only, rows -1..+3, ED20 = 0) |
| `tarso_edge_left` | same as `tarso_fight` | `tarso_fight`'s route, then MP6D with the hero held at the left wall (scroll 4): Tarso's columns 40..57 are off the ring.  LEGA tests vec 27's CF at every use (body column `A4C1 jc A537`, shot `A606 jnc`), so each whole column is skipped like `boss_part` does (#84) |
| `tarso_edge_right` | same | the same with the hero held at the right wall (scroll 14): body columns 52..57 and, for a few frames, the shot (type 0x26) are off the ring (#84) |
| `dragon_fight` | Pureza 4, LV 25, sword 6 (`DRAGONB`) | MP80's (57,5) door into MP7D from its far side: the hero lands inside the Dragon's body and dies of contact after ~80 frames of DRGN (no kill) |
| `dragon_kill` | Llama 8, LV 25, **LIFE 3000**, sword 6, one Key, Feruza shoes worn (`--worn 1`) (`DRAGONK`, `# pilot:`) | Llama's right-edge door (col 269) → MP70 (1,22), the ledge climb to the front door (199,33) (the jump-4 rise onto the (195,49) elevator, `8074` sets `[FF3D]` = 0), → MP7D (21,15); Dragon to the kill (sword damage ×½, head ×2, `A33E`), reward (EXP 12000, ALMAS 2500), 72F1's exit door.  Found and fixed (#87): `8074` elevator-up must leave the hero GROUND (`80FC..8101`); DRGN's flame parts are written with +5 = 0 so they never carry the hit flash (`A71A`); the death thrash is 29 frames (`AA02` tests the counter after the `inc`); gfmcga `302C` draws sprites in ring-scan order and `3468` claims the TR/BL/BR cells (a later sprite's cell under one is skipped); `738F` ticks the message box's timer once in the post-boss transition (cavern 7's TOO HOT box is up) |
| `alguien_route` | Esco 205, LV 25, LIFE 3000, sword 6, 3 Keys, Feruza shoes worn, `--magic 7=5` (spell 7, five charges) (`ALGM`, `# pilot:`) | MP81 (123,6) → MP82 (174,9) → MP81's bottom pocket → door (236,4) → MP80 (169,19): the elevator and the fix[16] C platform, the rope (`cell 191,27; keys .12 U6`), the fix[5] elevator whose shaft is blocked by a breakable rock at (229,22) (type 0xD0, item state 0x10): spell 7 (`8918`, screen-wide, marks every sprite in the window) breaks it, a second cast for the (252,13) rock over the fix[6] elevator, the row-3 corridor to door (185,1) → MP83 (29,53).  First scenario with magic.  Found and fixed (#88): spell 7 sets `[9EED]` = 0xFF (`8918`) and ends in `jmp 6F9B` -- one more whole frame, then back to the caller (`8963..896B`), so that cast frame skips `++crouch_release`; `cast_timer` `[9F2B]` survives a door (only `87E1` and the town entry `79E7` clear it) |
| `heat_current` | Llama 8, LV 25, one Key (`CALIK`) | MP70 (176,43) → MP72 (cavern 7): into the bottom corridor's rope gap, the fall through the ring, the (116,11) rope hole, then west into the (88..90,25) current that pushes the hero back.  Current cells are one-way walls (`67A3`/`6942`) in every cavern **but** 7 (#87) |
| `jashiin_fight` | Pureza 4, LV 40, LIFE 999, sword 6, shield 4, one Key (`JASHIIN`), `# cave: 0,29,6,0,0x1B` | Jashiin, the last boss (#111): Pureza's mouth rewritten to MP84 (29,6), the (30,5) door (one Key), MP90's [E6] cutscene (MAO1: 135 frames, three speeches, no boss HUD, no input, `[9F01]`/`[FF4A]`/`[9F0A]` untouched), MPA0 loaded by `61DD` (cur_map stays 1D, `[84]` carried, `[9F26]` clear after the first frame), the score's F1 (`[FF21]`) 158 frames later, then MAO2: both throws (shot 1 pose 9, shot 2 pose 12) until the hero dies (591 frames) |
| `jashiin_damage` | Pureza 4, LV 99, LIFE 4000, sword 6, shield 4, one Key (`JASHD`), `# cave: 0,29,6,0,0x1B` | `jashiin_fight`'s route, then the autopilot's `boss` step against MAO2 (`fight_lookahead`, #111): sword blows take HP /2 (`A362`), phase 2 below 200 (`AC32`), the 80-HP regeneration every 32 frames (`AB88`) back to 800 and phase 1 (frame 2101); 2151 frames. Port fixes it found: the hit flash (`A763`) goes on the body parts only, never the shots; the crouch patch (`A6F1`) writes row 8 of columns 0/1 (right) or 4/5 (left); contact reads a stale boss marker's old record (`7643`, no `nobj` guard). No kill: the autopilot's blows never outpace the regeneration |
| `jashiin_kill` | same save as `jashiin_damage` (`JASHD`), `# cave: 0,29,6,0,0x1B` | The kill and the ending's hand-off (#111): `jashiin_fight`'s route, the autopilot's `boss` step with `kill_lookahead` (MAO2 within two blows of dead: two-leg stick plans over 40 frames, scored on the kill), Jashiin dead at f~660 (death `ABC4`, reward `71CC`, 72F1's exit door), the door (`U` at the hero's column; its target is MPA0 itself, system map 0x1E), the patch list of MPA0 firing on the second load (`6BFC`: flag [47] -> the door records and **[49] = 0xFF**, the ending flag), the Tear scene, `6266` `[49]` -> `98FC` death (63 frames, `hero_die` with `99B4`: town 0x80), Felishika's Castle entered *alive* (`6055` clears `[E8]`: no Sage, ordinary `61B1` entry), the advisor's auto-talk ("Brave knight, you have awakened ..."), X, 62 columns right to Princess Felicia's door (95), `U`: omoypro's `A03D` `[49] != 0` -> enddemo.bin.  Since #113 the ending has boundary sites (`end:wait`, section 6): the script's last 40 idle frames run into enddemo's first waits; clean on fields and pixels.  Found and fixed: the pose-parity of MAO2's phase-2 walk (`A536`/`A55D`: closing in, an odd pose steps two cells), `6BFC` writes a patch's word wherever it points (below 0x100 the player page: MPA0's `[49]`), the death return with `[49]` set is an ordinary entry (`6055`), `6266`, GT_FLUSH runs in dialogue frames (`68AC`) and skips the cells under the dialogue box |
| `jashiin_ending` | same save and route as `jashiin_kill`, `# cave: 0,29,6,0,0x1B`, ends with 3650 idle tokens (one per boundary) | The whole ending (#113): `jashiin_kill`'s route, omoypro's `A03D` -> enddemo.bin, act 1 (the nine tableaux, the narration with its five lip-sync speakers, the wipe, the sand dissolves) and the credits roll to the end: 3691 `end:wait` boundaries (frames 872..4562), 4563 frames, clean on pixels at every boundary (`tracediff` compares only the site and the count there).  Found and fixed: the wipe's prologue `3FEF` bakes the two picture windows' frames into the picture (`gd_wipe`), and the fast/normal draw of an odd-sized picture leaves its last 4 staged pixels at what the previous picture left (`33D6`, `shr cx,1`: the 0xA0 mouth, 5 x 11) |
| `opdemo_intro` | none (`# intro: 1`: the game boots, no save) | The opening demo from boot (#34): prologue, demon, title with ttl2's ornament and the sand dissolves, credits, the storm demo with its typed story, the throne-room dialogue and the doorway epilogue, to the hand-back that loads the town.  7439 `intro:wait` boundaries (section 6, "The opening"), no input, `# png-every: 5`; clean on pixels (and at every one of the 7439 when run with `--png-every 1`).  Found and fixed: see section 6 |
| `cave_forced_death` | Satono 4 `--flag 49=FF` (`FLAG49`) | Entering a cavern from a town with `[49]` set (#113): cave_door's route (R13, U2), fight.bin `7D2D..7D61` -> `6042` -> `6266 test [49],FF / jnz 98FC`: the hero dies on the restart's first pass, **before the walk-in** (`[hero_entering]` stays 0), the dying frames, `99B4`, Felishika's Castle; 361 frames clean |
| `jashiin_regen800` | same save as `jashiin_damage` (`JASHD`), `# cave: 0,29,6,0,0x1B` | Regeneration landing exactly on 800 (#114): the first 600 frames of `jashiin_damage`, then idle (1001 frames).  `AB88` leaves phase 2 only when HP+80 *overshoots* 800 (`AB95`..`AB9D`: `cmp 0x320,hp+0x50 / jnc`); landing exactly on 800 just stores it and stays in phase 2 (`AC32`, `AC22` untouched: part type 0x01), after which `AB88` returns at once.  The port used `>=` and resumed phase 1 with the 0x60 bits (type 0x61) at frame 853 |
| `jashiin_death` | Pureza 4, LV 99, LIFE 700, sword 6, **no shield**, one Key (`JASHE`), `# cave: 0,29,6,0,0x1B` | The hero dies to Jashiin at LV99 (#114): the autopilot prefix of `jashiin_damage` (450 frames), idle, death at f458, the Sage's text; 541 frames.  `99BB`..`99C6`: the death EXP bonus is `al = 0x7F - 2*level` in 8 bits (`add al,al / neg al / add al,7F`, `ah = 0`), so from LV64 it wraps: LV99 gets 0xB9 = 185; the port clamped to 0 |
| `jashiin_arena` | Pureza 4, LV 99, LIFE 4000, sword 6, **no shield**, one Key (`JASHN`), `# cave: 0,29,6,0,0x1B` | fman.grp's arena tail (#114): the no-shield hero walks right and swings in MPA0 (409 frames).  Cell 0xE5 is three bytes short, so `4EDD` reads `arena:7FF0..7FF2` after the file: the residue of the town load, `20 44 01` for all ten towns (a write watch on a patched oracle: the kernel's `0EC9` zeroes `7FEC..7FEF` with the file's end, `7FF0..2` stay untouched, `4F33`/`4F39` then convert row 7 in place to `20 20 02 04`).  The port's old guess `00 80 02` drew one wrong pixel at (157,125) from frame 330 on this route; `contact_snail` and `sword_kinds` stay clean with the real bytes.  Open: a *second* cave load in one session sees the already-converted tail instead (`jashiin_kill` f677: `20 04 24` before its reload converts it again); not modelled, no scenario draws cell E5 after one |

**Jashiin (#111).**  Three things the port got wrong and the oracle showed, all in the [E6] / MPA0
hand-over, now fixed with their addresses in the code: MP90 is an [E6]-only room (FF34 = 0), so fight.bin
takes `6078 jz 6179` (ordinary HUD with PLACE / GOLD, no `60E6` card, no `6150` so `[9F01]` stays 0), the
walk-in loop `61BE` is `call 6f9b` and nothing else (no input, no `62B5` `[9F0A]` count) and `A350`
returns to `A290`, so the last pose is drawn once more; MPA0 is entered by `61DD`..`6234`, which
writes only `[80]/[82]` (`7DC1`), `[83]`, `[9F00]` and loads the map: `[C4]` stays 1D, the hero's screen row
`[84]` (0xD) carries and `6FB8` walks it to 0xC (one map scroll), `[FF4A]`/`[4FFF]` carry, `[9F26]` is cleared
by `624D` after MPA0's first frame.  **`[FF21]`** is not written by fight.bin: it is the mmao score's `F1`,
1625 driver ticks (162.5 frames) after the score starts.  The port keeps a silent score player stepped by
game frames (`audio_clock_*`, 10 driver ticks per frame, independent of the audio device); the shell starts it when
MPA0 loads with 40 ticks already played (the load between the score start `7E93` and the first frame; the oracle's
first started MAO2 frame fixes it to 35..44) and MAO2's `A3A7` gate reads it.  Drawing: MAO2's overlapping arm
parts (f328) needed #109's per-cell E900 rule rather than hiding a whole sprite; it came in from main meanwhile.

**The Dragon's front door (#87).**  `dragon_kill` is the route.  The survey-based attempts all
started from Llama's column-8 door (MP70 (152,7)); Llama's *right* edge door (col 269, cavern entry
`(1,22,0,MP70)`) puts the hero at MP70 (1,22) instead, on the wrap side of the rows 25-31 ceiling, and from
there the autopilot reaches the rows 41-48 band east of col 192 in ~280 frames.  With the **Feruza shoes**
(`--worn 1`: rise 4, `6F9B` sets `[9F0D]`) the (195,49) A elevator and the ledge to the locked (199,33)
door are in reach; the Asbestos cape's rise is 2 and cannot make that climb, so the heat (704F, 15 LIFE
per 64 frames) stays on and the scenario uses `--life 3000` for it and for the Dragon's contact damage
(DRGN: 40 head/flame, 30 body, per frame).  The Dragon takes ×½ sword damage (`A33E`), ×2 on the head
(class 0): six or seven blows.  The same route is where the port's elevator-up bug showed (the hero
must be GROUND after `8074`).  MP80's (57,5) door (`dragon_fight`) lands inside the body; the front
door lands at (21,15), left of the Dragon's col 30.

**Alguien's front door (#88), where the route stops.**  `alguien_route` is the part that works: from
Esco's MP81 (123,6) the way to MP83 is MP82's (174,9) → MP81's bottom pocket (227,60), whose top rope
(col 205, over the wrap row 63 → 0) leads to the row-5 corridor and door (236,4) (it sets a story
flag) → MP80 (169,19) → the elevator (183,22), the fix[16] C platform (the autopilot waits out
`SETTLE` after the walk onto it and is carried 15 columns past the rope: `cell 191,27; keys .12 U6`
does it by hand), the rope up to row 11, the fix[5] elevator (227,14), the south-east rooms, the
fix[6] elevator and the col-249 rope, the row-3 corridor west, door (185,1) → MP83 (29,53).  Two
**breakable rocks** (type 0xD0 = item state 0x10, `8E32`) sit in the elevator shafts, at (229,22)
and (252,13); an elevator's `8024`/`80AF` test refuses a non-zero ring cell, and the sword cannot
reach them from the elevator (the thrust reaches row+3 only).  Magic can: the spells mark a sprite
through `8C4F` like the sword does, and spell 7 hits everything in the window, so `--magic 7=5` and
`M1` on the elevator (the cast takes 6 frames) opens both.  What is **not** done: MP83's doors (6,13)
→ MP81 (228,49) and (86,6) → MP81 (179,35), the only ways into the region of MP81 that holds the
front door (222,19) → MP8D, are not reachable by the autopilot from (29,53): the B gates
((50,46) (53,44) (56,42), (65,7)..(74,10), (7,30), (124,41); `fixture_ride` 818E, one row a frame
under the hero) are not nav edges, and a plain walk onto the first one does not move the hero.
Past that: the 4-row jump (Feruza shoes) onto MP8D's boss pocket, the Key door (flag [44] & 20).

## 9. Save states (#52, Phase B)

The wasm / DOSBox-X builds snapshot the whole machine.  The port cannot, so it lists
its engine state explicitly and versions the format.  `port/savestate.c` is the
serialiser; `--state-save-at N FILE` writes one, `--state-load FILE` starts from one.
`port/statekeys.c` is the player-facing layer on top: the quick key and the numbered
slots, their files and the on-screen confirmation (see 9.4).

### 9.1 Format

All integers little-endian, written field by field (no struct dumps, no padding):

    offset 0    8    magic "ZELSTATE"
    offset 8    4    version (1)
    offset 12   4    payload length N
    offset 16   N    payload
    offset 16+N 4    CRC-32 (IEEE) of the payload

The payload holds, in order: the trace frame and RNG-call base (the meta), the shell
mode (`in_town`, live map / town index, `town_entry`, the town walk-in) and the
256-byte player page; then the whole dynamic cavern `Game` (both rings, the `[C010]`
object list, `under_sprite`, `BASE:C000-DFFF`, the live decompressed map image with
its `72F1` pokes, every `fight.bin` local, the player record, combat, the `EB80`
shots / `EB15` magic / `EB60` orbs, the boss protocol including `EDA0`'s sparks, the
RNG, deaths, the message box and the map-transition walk-in); then the town `Town`
(scroll/backdrop phase, hero pose, input, the dialogue state, the `E000` screen copy)
and the town map's mutable bytes (the `grid` and the `NPC` records).  Resource
pointers (map, tileset, AI overlay, fonts, art) are **not** stored: the loader calls
the shell's own entry points to rebuild them and then overwrites the dynamic fields.

A state is only written at a **plain `717F` / `68ED` frame boundary**, after that
frame's post-boundary tail has run, so a fresh `game_step` / `town_step` resumes from
it exactly.  A boundary whose frame opens a shop, the status screen or a dialogue box
is refused (the blocking loop's own cursor is not in the structs).  `--state-load`
continues the trace numbering at N+1 and re-applies boundary N's token (idempotent)
before the first resumed frame.

### 9.2 Proof

    tools/harness/state_roundtrip.sh [-j N] [--out DIR] [--saves K] [--all] [--no-fuzz] [NAME ...]

Port only, no oracle.  For every `tests/scenarios/*.zs`: run the reference trace; for
several frame boundaries N, re-run with `--state-save-at N`; run a third time from the
blob with the remaining script (`tools/harness/script_cut.py N SCRIPT`) and require its
`--trace` to be identical, frame N+1 to the end, to the reference (`tools/harness/
trace_eq.py`: fields, site, input and rng_calls).  The whole library passes at three N
each.  It then drives the **in-game request path** (9.4): one more run with
`--state-inject save:N --state-inject load:N` saves and reloads the quick slot at the
same boundary through the same queue a key press uses, and the whole trace must stay
byte-identical (`trace_eq.py REF CAND -1`).  It ends with a corruption check:
truncations and bit-flips of a real blob must be refused cleanly (exit < 128), and
`ZELIARD=/path/to/san/zeliard` runs the same proof under `make SAN=1`.

**Every boundary (#110).**  `--all` replaces the K samples with *every* resumable
`717F`/`68ED` frame boundary, so the proof covers each frame of a transition, not just
a few.  It is the regression guard for the door-transition state: `7E15` writes a
door's unlocked bit into the map image at `C00A`, and the save state stores that raw
image, so `port/physics.c door_unlock` mirrors the byte into `raw` (via
`map_door_letter_set`) or a reload re-parses the door locked and the arch the ring
draws flips back to `78DD`'s LOCKED template.  `key_door` (the locked MP10 door),
`cave_door`, `gate_walking`, `town_gate`, `edge_press`, `drug_buy`, `death_sage`,
`crab_fight` and `paguro_return` all pass at every boundary.

### 9.3 Unit checks

`port/test_playthrough.c` (`state_roundtrip_unit`) saves and reloads a shell in-process
and asserts the player record and the ring survive, and that a garbage, truncated or
bit-flipped blob is refused.  `statekeys_unit` pins the player-facing layer: the
quick/slot file names, the request queue order and the overlay refusal.

### 9.4 Keys, slots and the on-screen confirmation

The player keys match the wasm builds: **Ctrl+.** quick-saves and **Ctrl+\\**
quick-loads, **Ctrl+1..9** saves to a numbered slot and **Alt+1..9** loads one.  A
request is never run where it is made — it is queued and the shell's `frame_done`
hook runs it at the true end of the frame (the only point a fresh step can resume
from), so a key press during the frame cannot capture a half-stepped engine.  While
a shop, the status screen, a dialogue box or the Tear cutscene owns the loop the
request is refused with the same message a player sees ("Cannot save here"), rather
than serialising a cursor the state does not hold.

The files live in `SDL_GetPrefPath("awkto","zeliard")/states/` (`quick.zst`,
`slot1.zst` … `slot9.zst`); a headless build, or one where SDL cannot name it, uses
`<gamedir>/states/`, and `--state-dir DIR` overrides both.  A successful save/load
shows a short message over the picture ("Saved slot 3", "Loaded quick save", "No save
in slot 5", "Save incompatible"), drawn by the presentation layer only — never into
the engine framebuffer `make verify` diffs.  Loading restarts the map's or town's
score, since the music position is not part of the state.

The headless equivalent of a key press is `--state-inject save:N[:slot]` /
`load:N[:slot]` (with `--trace`): it queues the same request at trace boundary N, and
it is what 9.2's key-path check uses.
