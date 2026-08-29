# Roadmap

Canonical copy: https://gitlab.dnsif.ca/github/zeliard-decomp/-/issues/11 (tracking epic).

Tracking epic for all remaining work. Written to be executable without prior session context. Read this top-to-bottom before starting any sub-issue.

## Ground rules

- Repo: https://github.com/awkto/zeliard-decomp (tools + docs ONLY). Original game files (\`zeliard/\`), extracted resources (\`extracted/\`), and disassembly (\`disasm/\`) are **gitignored on purpose** (copyrighted/derived) — never commit or push them.
- Working dir: \`~/git/decomp-zel\` on altanc's machine. Game files in \`zeliard/\`.
- Track findings here: close sub-issues with implementation notes; file new issues for new discoveries.
- **Read \`docs/ARCHITECTURE.md\` first.** It has the boot chain, memory map, kernel service table, compression spec, and overlay model. \`docs/RESOURCES.md\` maps 167/194 resources to original filenames.

## Established facts you need (condensed)

- All code shares one 64 KB segment BASE: kernel STICK.BIN @0100 (service vectors: \`call [cs:0x10C]\` load-resource with AL=mode 0-6; see ARCHITECTURE.md), video driver GM*.BIN @2000, renderer overlays (gd/gt/gf*.bin) @3000, engine overlays @6000 (opdemo/town/fight/enddemo), shop overlays @A000, state page @FF00. Data arena = separate segment at \`[cs:0xFF2C]\`.
- Regenerate everything: \`python3 tools/sarex.py extracted && python3 tools/sardec.py extracted && tools/disasm.sh\`.
- Ghidra 11.3.2 is installed persistently at `~/opt/ghidra_11.3.2_PUBLIC` (override with `$GHIDRA_HOME`; re-download from github NationalSecurityAgency/ghidra release `Ghidra_11.3.2_build` if missing — Java 21 works). Decompile any binary/overlay with the wrapper:
  `tools/ghidra.sh BIN SEG:OFF OUT.c [SEEDS]` → e.g. `tools/ghidra.sh extracted/ZELRES2/006_data.bin 0000:3000 /tmp/gfmcga.c` (renderers @3000, GM*.BIN @2000, slot-A overlays @6000, slot-B @A000; overlay-style images whose first word is the end of a vector table are seeded automatically). Binaries without such a table need explicit SEEDS: STICK.BIN = `0x100,0x103,0x106,0x109,table:0x10C:11`. Output has one `/* ===== name @ addr ===== */` block per function.
- Maps (SOLVED, Sprint 3): `.mdt` = header of BASE-absolute pointers + column-major RLE tile stream (64 rows/column, values = cell index into MPPx.GRP, 0x40+ = DCHR.GRP fixtures, bit 7 = sprite object). `tools/mdt2png.py --all OUT` renders every cavern. fight.bin keeps a 36×64 ring at E000 and the level record at `[C000]` selects tileset/AI/enemy bank/music.
- Sprite pixels (SOLVED, Sprint 1): a 48-byte cell as stored is PC-88 style 16×8, 3 bitplanes (8 rows × 3 big-endian words A,B,C; v=C<<2|B<<1|A). Video service [0x2044] is NOT a palette upload — it packs adjacent pixel pairs into 6-bit values (left<<3|right) so the cell becomes 8×8 with 64 colours; gfmcga blits those as VGA indices (0 = transparent). The 64-entry DAC is built by GAME.BIN @A41B as BASE[left]+BASE[right] (8 base colours @A456). `tools/palette.py` reproduces it (verified pixel-exact vs DOSBox); `tools/cellsheet.py FILE OUT.png --skip OFF` renders any bank. `.grp` files are containers — the bank offset comes from the header (Sprint 2).
- Ground truth: \`tools/run_dosbox.sh /tmp/zzz "10 20 30"\` runs the real game (Xvfb+DOSBox) and screenshots at those seconds. Use it to verify every rendering hypothesis. **docs/DOSBOX_RECIPE.md** has verified `KEYS=` timelines: intro skip (3 Returns) and entering cavern 1 (~54 s) — use them for any in-cavern check.

## Phase 1 — finish asset pipeline (issues #7, #8, #9)

1. ~~**Pin the palette (#7).**~~ DONE (#13): `tools/palette.py`, documented in ARCHITECTURE.md. The intro/ending renderer (gdmcga @425E) uses a separate 256-entry palette scheme — decode when the title/ending art is needed.
2. **Metasprite assembly (#7).** sword.grp layout: header words + offset table + tile maps (0xFF = empty) referencing cell indices. Write \`tools/grp2png.py\` that: parses container, extracts cell bank + maps, composites full sprites. Verify against screenshots (title logo ttl1-3.grp, shop portraits king.grp etc.).
3. ~~**Maps (#9).**~~ DONE (#15): `tools/mdt2png.py` renders all 31 cavern maps (column-major RLE, 64 rows, MPP1-B tile banks + DCHR fixtures, object table); format in ARCHITECTURE.md "Maps". Town maps decoded in Sprint 6 (docs/TOWN.md, `mdt2png.py --town`).
4. ~~**Music (#8).**~~ DONE (#22): `tools/msd2mid.py` (all 17 scores × AdLib/Tandy/MT-32 arrangements → MIDI, loop detection), `docs/MUSIC.md` (INT 60h API, score format, tick/tempo = 118.35 Hz·(256−T)/256), `src/music_std.c`, `src/music_adlib.c`.

## Phase 2 — decompile game logic (issue #6)

Priority order (each: Ghidra dump → hand-clean to readable C in a new \`src/\` tree, one file per overlay; keep function addresses in comments):
1. ~~STICK.BIN kernel (~4 KB).~~ DONE (#16): `src/kernel.c` + `docs/SERVICES.md` (11 vectors 0x10C-0x120, [0x10C] modes, INT 8/9, FF-page vars); video drivers: `docs/VIDEO_DRIVERS.md` (35 slots 0x2000-0x2044, 5-driver equivalence table) + `src/video_mcga.c`.
2. ~~fight.bin (16 KB).~~ DONE (#17): `src/fight.c` (108 routines), `docs/FIGHT.md` (frame model 236.7 Hz/4×FF33 ticks, cell-granular physics, sword shapes, damage formulas, 16-byte enemy record, AI overlay vectors), `docs/STATE_PAGE.md` (FF00 page + player record 0049-00E8). Static only — DOSBox cross-check pending #19.
3. ~~town.bin + 8 *pro.bin shop overlays.~~ DONE (#20): `src/town.c`, `src/shops.c`, `docs/TOWN.md` (town .mdt header, dialogue opcodes, every shop's price/effect table, `NAME.USR` save = raw 256 bytes of BASE:0000, town↔cavern handoff); town maps + *pat/mman/cman graphics decoded, `mdt2png.py --town`.
4. ~~eai1-8.bin + 11 boss AIs.~~ DONE (#21): `src/ai/` (ai_common.h ABI, eai1-8.c, 11 boss_*.c), `docs/ENEMIES.md` (per-cavern HP/contact/EXP/drop tables + patterns, boss phases). Ghidra mis-decodes DRGN/AKMA/MAO1 (header as code) — those were done from ndisasm.
5. select.bin (status screen + potion effects) — DONE (#26): `src/select.c`, `docs/TOWN.md` §12 (both entry vectors, the four windows, the three rows, the eight potion effects, the INVENTORY window); ported in `port/status.c` and verified pixel-exact against `docs/screenshots/menu.png`. mole.bin also decoded (Sprint 17): it is the **boot-time screen painter**, not a cutscene — GAME.BIN far-calls it once to draw the stone frames, the strip above the playfield and the grey HUD panel; RLE + mode-4 blitter reimplemented in `port/`.  opdemo/enddemo/rokademo also DONE (#29): `src/opdemo.c`, `src/enddemo.c`, `src/rokademo.c`, `docs/CUTSCENES.md`, and the gd art format + 16-entry palette decoded (`tools/grp2png.py` renders all 31 intro/ending resources). **Phase 2 is complete — every code module in the game is decompiled.**

Method per overlay: run ghidra_dump_c.py, then rename functions/globals against known anchors (service vectors, FF00 state page, request blocks with embedded filenames). Cross-check behavior in DOSBox.

## Phase 3 — SDL port scaffold

1. ~~\`port/\` C project.~~ DONE (#23): C11 + SDL2 (headless fallback), reads ZELRES*.SAR directly (sar.c ports sarex/sardec), `make` / `make test` (103 physics checks) / `make verify` (100% playfield match vs docs/screenshots/cavern.png). See port/README.md.
2. Implement in order: map render + scroll → player movement vs map collision (from fight.bin decomp) → sprites/animation → combat → shops/save → music (from .msd converter).
3. Milestone gates: ~~(a) walk around cavern 1 with correct collision~~ DONE (#23); ~~(b) kill one enemy with correct damage tables~~ DONE (#24); (c) full cavern 1 + town loop; (d) all 9 caverns; ~~(e) audio~~ DONE (#27): own OPL2 core + MSC/SND driver ports in `port/`; all 51 score streams match tools/msd2mid.py.

## Phase 4 — polish/stretch

- Save-game compatibility with DOS version (USER file format — kernel res#=0 path).
- EGA/CGA render modes (formats already understood at plane level).
- CI: GitHub Actions building the port; asset pipeline gated on user-supplied game copy.

## Sub-issue index

~~#5 kernel/video vector naming~~ (done, #16), #6 Ghidra decompilation (Phase 2), #7 graphics (Phase 1.1-1.2), #8 music (Phase 1.4), #9 maps (Phase 1.3). File new issues per port milestone when Phase 3 starts.
