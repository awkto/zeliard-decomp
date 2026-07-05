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
for f in extracted/ZELRES1/000_code.bin extracted/ZELRES1/006_code.bin \
         extracted/ZELRES2/*code*.bin extracted/ZELRES3/000_code.bin; do
  base=$(echo "$f" | sed 's|extracted/||; s|/|_|; s|\.bin||')
  ndisasm -b16 "$f" > "disasm/overlays/$base.asm"
done
echo "disasm/ regenerated"
