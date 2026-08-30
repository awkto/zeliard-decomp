# decomp-zel — Zeliard (DOS, 1990) decompilation

[![CI](https://github.com/awkto/zeliard-decomp/actions/workflows/ci.yml/badge.svg)](https://github.com/awkto/zeliard-decomp/actions/workflows/ci.yml)

Reverse engineering of **Zeliard** v1.208 (Game Arts / Sierra On-Line).

CI (`.github/workflows/ci.yml`) builds `port/` twice on every push — once
against `libsdl2-dev` and once as the headless fallback with no SDL headers at
all — runs `make test` in both, byte-compiles every Python tool and smoke-tests
the ones that need no game data.  `make verify`, `make playthrough` and the
extraction pipeline need `zeliard/`, which is copyrighted and not in this
repository, so they print a SKIP line instead of failing; a runner that does
have a copy of the game runs them for real.

- `zeliard/` — original game files (not committed; supply your own copy)
- `tools/sarex.py` — .SAR resource archive extractor (format fully verified)
- `tools/disasm.sh` — regenerates `disasm/` (16-bit ndisasm at true load origins)
- `tools/sardec.py` — SAR payload decompressor (8-opcode RLE engine, 194/194 verified)
- `tools/resnames.py` — recovers original resource filenames → `docs/RESOURCES.md`
- `tools/ghidra.sh` — headless Ghidra decompile of any binary/overlay to C (uses `tools/ghidra_dump_c.py`)
- `tools/run_dosbox.sh` — ground-truth harness: runs the real game in Xvfb+DOSBox, captures screenshots
- `port/` — the SDL2 source port: `make && ./zeliard` boots the intro, plays the game and reaches the ending; `make test` (1885 assertions in 10 binaries, none of which need the game files), `make verify` (186 pixel comparisons at 100% against DOSBox captures), `make playthrough` (two autonomous routes).  `--video mcga|cga|cga2|ega|hgc|tandy` renders through any of the five original video drivers
- `docs/ROADMAP.md` — phased plan, mirrors the tracking epic
- `tools/grp2png.py` — renders .grp graphics (tiles, enemies, hero frames, portraits, font) to PNG; `tools/palette.py` = the MCGA palette
- `src/kernel.c`, `src/video_mcga.c` — hand-cleaned C of the STICK.BIN kernel and the MCGA video driver (original addresses in comments); `port/video_*.c` are the same five drivers as output stages of the port
- `src/fight.c`, `docs/FIGHT.md`, `docs/STATE_PAGE.md` — the cavern game loop: physics, collision, combat, enemy records, AI-overlay interface, FF00 state page
- `src/town.c`, `src/shops.c`, `src/select.c`, `docs/TOWN.md` — town walk/dialogue engine, all 8 shops (prices, effects), the status/inventory screen and the potion effects, save-file format, town map format
- `src/ai/`, `docs/ENEMIES.md` — all 8 cavern enemy AIs + 11 bosses: stats tables, movement/attack patterns, AI-overlay ABI
- `docs/DOSBOX_RECIPE.md` — verified key timelines to skip the intro and enter cavern 1 in the harness
- `port/` also has audio: a from-scratch OPL2 core with the MSC*/SND* drivers ported, cross-checked against the Python converter
- `tools/msd2mid.py`, `docs/MUSIC.md`, `src/music_{std,adlib}.c` — music score format + drivers; converts every score to MIDI
- `docs/SERVICES.md`, `docs/VIDEO_DRIVERS.md` — every kernel service vector (0x10C+) and video-driver vector (0x2000+, all 5 drivers) with args and callers
- `src/opdemo.c`, `src/enddemo.c`, `src/rokademo.c`, `docs/CUTSCENES.md` — the intro, ending and Tear cutscenes, plus the gd art format
- `docs/ARCHITECTURE.md` — boot chain, memory map, SAR format, kernel protocol, graphics formats

Original game files, extracted resources, and disassembly listings stay
untracked — only clean tools and documentation live in this repo.

Quick start (with your own game copy in `zeliard/`):

```sh
python3 tools/sarex.py extracted   # unpack all three ZELRES*.SAR
tools/disasm.sh                    # regenerate disassembly listings
```

Findings are tracked as issues on the project tracker.

See `docs/ARCHITECTURE.md` for the full picture.

## License

This repository's own code and documentation are **MIT** (`LICENSE`).

*Zeliard* itself is copyright Game Arts / Sierra On-Line and is **not**
distributed here — no game file is in this repository, and the port reads the
originals at runtime from a copy you supply yourself.  See `NOTICE`.
