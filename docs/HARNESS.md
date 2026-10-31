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
boundary are what the *next* frame reads. Other loops (shops, status screen,
cutscenes) are out of scope for v1. A boundary in any other place is logged as an
unknown site, not traced.

`BASE` (the one 64 KB segment that STICK, the drivers and the overlays share) =
the segment of the `INT 61h` vector (STICK's input service at `BASE:05FD`).

## 2. Inputs

The port's `--script` language is the input language for both sides: `U D L R`
directions, `X` sword, `M` magic, `E` Enter, `K` the LEVEL/EXP chord, `.N` idle,
a number = hold for N frames (`R10`, `UR2`, `X1`). One token-frame = one game frame.

The oracle applies frame `n`'s inputs at boundary `n-1`. v1 writes the kernel's
input state directly (`FF17` dirs, `FF16` buttons, `FF1D`/`FF1E` press edges,
`FF18` key mask), exactly as STICK's INT 9 would leave it. This is deterministic
and independent of keyboard-controller timing. Real scancode injection is a later
option for menu keys.

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
`4`/`5` the restore did not complete; `6` **stalled**: no town/fight boundary for
`ORACLE_STALL_MS` (default 30 s emulated) — the game left the two traced loops (status menu,
shop, death/Sage screen, cutscene). `DIR/oracle.log` has the boot milestones, `DIR/final.png`
the screen on any failure. `ORACLE_DEBUG_MS=N` writes `boot_<ms>.png` every N emulated ms
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
- **Inputs only reach the game at town/fight boundaries.** The status menu (E), shops and
  dialogs have no boundary, so after `E` the oracle cannot close the menu and stalls
  (exit 6). Same for talking to an NPC with X in a town. Scripts for v1 must stay in the
  two loops; menu keys need real scancode injection (later).
- `FF6C` player_name: the original stores the restored save's file name there (`CAVE`),
  the port `ZELIARD` — a real port difference, not an oracle artefact.

### Verified

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
