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
- Ghidra 11.3.2 lives in the session scratchpad (re-download if gone; Java 21 works). Decompile any overlay:
  \`ghidra_11.3.2_PUBLIC/support/analyzeHeadless /tmp/ghproj p1 -import extracted/ZELRES2/006_data.bin -processor "x86:LE:16:Real Mode" -loader BinaryLoader -loader-baseAddr 0000:3000 -postScript tools/ghidra_dump_c.py /tmp/out.c -scriptPath tools -deleteProject\`
  (base 3000 for renderers, 6004/6002 region files import at 0000:6000 with 2-4 byte vector header — simplest is import whole file at the slot base minus header bytes, or strip header first.)
- Sprite pixels: 8x8 cells, 48 B/cell, 6-bit packed: per 3 bytes → px=[b1>>2, (b1&3)<<4|(b0>>4), (b0&0xF)<<2|(b2>>6), b2&0x3F], 0=transparent. Banks at arena:8000, cells from +0x30; bank's first 0x100 bytes = palette/color map uploaded via video-driver service [0x2044].
- Ground truth: \`tools/run_dosbox.sh /tmp/zzz "10 20 30"\` runs the real game (Xvfb+DOSBox) and screenshots at those seconds. Use it to verify every rendering hypothesis.

## Phase 1 — finish asset pipeline (issues #7, #8, #9)

1. **Pin the palette (#7).** Decompile GMMCGA.BIN (base 0000:2000, it has a vector table too) and find service [0x2044] (vector index (0x44-0x2000)/2... the table starts at 2000; entry for offset 0x2044). Decode how the 0x100-byte bank header becomes VGA DAC values. Acceptance: render any .grp bank to PNG whose colors match a DOSBox screenshot of the same scene.
2. **Metasprite assembly (#7).** sword.grp layout: header words + offset table + tile maps (0xFF = empty) referencing cell indices. Write \`tools/grp2png.py\` that: parses container, extracts cell bank + maps, composites full sprites. Verify against screenshots (title logo ttl1-3.grp, shop portraits king.grp etc.).
3. **Maps (#9).** mp10.mdt = ZELRES3[20] etc., loaded raw at BASE:C000 via kernel AL=1. Header looks like {u16 ptr, u16 len} + pointer lists. Cross-reference the code in fight.bin/town.bin that walks them (search disasm for mov si/bx with C000-region constants). Acceptance: render a cavern map as a tile-index grid PNG that visually matches DOSBox gameplay.
4. **Music (#8).** .msd = score data consumed by MSC*.DRV at (BASE+FF0):0100. Diff MSCSTD vs MSCADLIB decompilations to separate score parsing from device code. Acceptance: .msd → MIDI converter that produces the opening theme recognizably.

## Phase 2 — decompile game logic (issue #6)

Priority order (each: Ghidra dump → hand-clean to readable C in a new \`src/\` tree, one file per overlay; keep function addresses in comments):
1. STICK.BIN kernel (~4 KB) — already 60% understood, documents itself.
2. fight.bin (16 KB) — THE game: player physics, combat, damage, scrolling. State page FF00-FF7F variables get names as they're identified.
3. town.bin (7 KB) + the 8 *pro.bin shop overlays (small, formulaic dialogue/menu logic).
4. eai1-8.bin — per-cavern enemy AI (~2 KB each).
5. select.bin, mole.bin, opdemo/enddemo/rokademo (cutscenes last).

Method per overlay: run ghidra_dump_c.py, then rename functions/globals against known anchors (service vectors, FF00 state page, request blocks with embedded filenames). Cross-check behavior in DOSBox.

## Phase 3 — SDL port scaffold

1. \`port/\` C project: SDL2 shell, 320x200 framebuffer, asset loading via the (by then complete) Python pipeline pre-converted to PNG/JSON, or direct SAR reading in C (port sarex/sardec — both are simple).
2. Implement in order: map render + scroll → player movement vs map collision (from fight.bin decomp) → sprites/animation → combat → shops/save → music (from .msd converter).
3. Milestone gates: (a) walk around cavern 1 with correct collision; (b) kill one enemy with correct damage tables; (c) full cavern 1 + town loop; (d) all 9 caverns; (e) audio.

## Phase 4 — polish/stretch

- Save-game compatibility with DOS version (USER file format — kernel res#=0 path).
- EGA/CGA render modes (formats already understood at plane level).
- CI: GitHub Actions building the port; asset pipeline gated on user-supplied game copy.

## Sub-issue index

#5 kernel/video vector naming (feeds Phase 2), #6 Ghidra decompilation (Phase 2), #7 graphics (Phase 1.1-1.2), #8 music (Phase 1.4), #9 maps (Phase 1.3). File new issues per port milestone when Phase 3 starts.
