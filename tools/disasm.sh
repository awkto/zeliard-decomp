#!/bin/sh
# Regenerate disasm/ from original game files (zeliard/) + extracted SAR entries.
# Origins per docs/ARCHITECTURE.md memory map. Requires: nasm (ndisasm), python3.
set -e
cd "$(dirname "$0")/.."
python3 tools/sarex.py extracted >/dev/null
mkdir -p disasm/overlays
ndisasm -b16 -o 0x0000 zeliard/STDPLY.BIN  > disasm/STDPLY.asm
ndisasm -b16 -o 0x0100 zeliard/STICK.BIN   > disasm/STICK.asm
ndisasm -b16 -o 0xA000 zeliard/GAME.BIN    > disasm/GAME.asm
ndisasm -b16 -e 0x200  zeliard/ZELIARD.EXE > disasm/ZELIARD.asm
for f in GMCGA GMEGA GMHGC GMMCGA GMTGA; do
  ndisasm -b16 -o 0x2000 "zeliard/$f.BIN" > "disasm/$f.asm"
done
for f in MSCADLIB MSCJR MSCMT MSCSTD; do
  ndisasm -b16 -o 0x0100 "zeliard/$f.DRV" > "disasm/$f.asm"
done
for f in SNDADLIB SNDJR SNDSTD; do
  ndisasm -b16 -o 0x1100 "zeliard/$f.DRV" > "disasm/$f.asm"
done
# SAR code overlays (raw, AL=3 loads): leading words are entry-point vectors,
# origins recovered from GAME.BIN load sites (docs/RESOURCES.md for names).
# fmt: sar/index:name:vector_bytes:org
overlays="
ZELRES1/000:opdemo:2:0x6002
ZELRES1/006:town:4:0x6004
ZELRES2/000:fight:4:0x6004
ZELRES2/001:select:4:0xa004
ZELRES2/007:mole:2:0xa002
ZELRES2/010:kingpro:4:0xa004
ZELRES2/011:omoypro:4:0xa004
ZELRES2/012:armrpro:4:0xa004
ZELRES2/013:bankpro:4:0xa004
ZELRES2/014:churpro:4:0xa004
ZELRES2/015:drugpro:4:0xa004
ZELRES2/016:innapro:4:0xa004
ZELRES2/017:kenjpro:4:0xa004
ZELRES2/050:enddemo:2:0x6002
ZELRES3/000:rokademo:2:0xa002
"
for spec in $overlays; do
  entry=${spec%%:*}; rest=${spec#*:}
  name=${rest%%:*};  rest=${rest#*:}
  skip=${rest%%:*};  org=${rest#*:}
  src=$(ls "extracted/${entry%/*}/${entry#*/}"_*.bin)
  ndisasm -b16 -e "$skip" -o "$org" "$src" > "disasm/overlays/$name.asm"
done
echo "disasm/ regenerated"
