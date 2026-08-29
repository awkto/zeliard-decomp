# decomp-zel — Zeliard (DOS, 1990) decompilation

Reverse engineering of **Zeliard** v1.208 (Game Arts / Sierra On-Line).

- `zeliard/` — original game files (not committed; supply your own copy)
- `tools/sarex.py` — .SAR resource archive extractor (format fully verified)
- `tools/disasm.sh` — regenerates `disasm/` (16-bit ndisasm at true load origins)
- `tools/sardec.py` — SAR payload decompressor (8-opcode RLE engine, 194/194 verified)
- `tools/resnames.py` — recovers original resource filenames → `docs/RESOURCES.md`
- `tools/ghidra.sh` — headless Ghidra decompile of any binary/overlay to C (uses `tools/ghidra_dump_c.py`)
- `tools/run_dosbox.sh` — ground-truth harness: runs the real game in Xvfb+DOSBox, captures screenshots
- `port/` — the SDL2 source port (Phase 3): `make && ./zeliard` renders cavern 1 and walks Garland with the original collision rules; `make test` (149 checks), `make verify` (7 pixel comparisons, all 100%); enemies, eai1 AI and sword combat implemented
- `docs/ROADMAP.md` — phased plan, mirrors the tracking epic
- `tools/grp2png.py` — renders .grp graphics (tiles, enemies, hero frames, portraits, font) to PNG; `tools/palette.py` = the MCGA palette
- `src/kernel.c`, `src/video_mcga.c` — hand-cleaned C of the STICK.BIN kernel and the MCGA video driver (original addresses in comments)
- `src/fight.c`, `docs/FIGHT.md`, `docs/STATE_PAGE.md` — the cavern game loop: physics, collision, combat, enemy records, AI-overlay interface, FF00 state page
- `src/town.c`, `src/shops.c`, `docs/TOWN.md` — town walk/dialogue engine, all 8 shops (prices, effects), save-file format, town map format
- `src/ai/`, `docs/ENEMIES.md` — all 8 cavern enemy AIs + 11 bosses: stats tables, movement/attack patterns, AI-overlay ABI
- `docs/DOSBOX_RECIPE.md` — verified key timelines to skip the intro and enter cavern 1 in the harness
- `tools/msd2mid.py`, `docs/MUSIC.md`, `src/music_{std,adlib}.c` — music score format + drivers; converts every score to MIDI
- `docs/SERVICES.md`, `docs/VIDEO_DRIVERS.md` — every kernel service vector (0x10C+) and video-driver vector (0x2000+, all 5 drivers) with args and callers
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
