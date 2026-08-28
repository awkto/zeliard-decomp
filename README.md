# decomp-zel — Zeliard (DOS, 1990) decompilation

Reverse engineering of **Zeliard** v1.208 (Game Arts / Sierra On-Line).

- `zeliard/` — original game files (not committed; supply your own copy)
- `tools/sarex.py` — .SAR resource archive extractor (format fully verified)
- `tools/disasm.sh` — regenerates `disasm/` (16-bit ndisasm at true load origins)
- `tools/sardec.py` — SAR payload decompressor (8-opcode RLE engine, 194/194 verified)
- `tools/resnames.py` — recovers original resource filenames → `docs/RESOURCES.md`
- `tools/ghidra.sh` — headless Ghidra decompile of any binary/overlay to C (uses `tools/ghidra_dump_c.py`)
- `tools/run_dosbox.sh` — ground-truth harness: runs the real game in Xvfb+DOSBox, captures screenshots
- `docs/ROADMAP.md` — phased plan, mirrors the tracking epic
- `tools/grp2png.py` — renders .grp graphics (tiles, enemies, hero frames, portraits, font) to PNG; `tools/palette.py` = the MCGA palette
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
