#!/bin/sh
# Decompile one raw 16-bit real-mode binary to C with Ghidra headless.
# usage: tools/ghidra.sh BIN SEG:OFF OUT.c [SEEDS]
#   SEEDS: comma list of hex entry offsets and/or table:OFF:COUNT (see ghidra_dump_c.py);
#   default = overlay-style vector table at the image base.
#   e.g. tools/ghidra.sh zeliard/STICK.BIN 0000:0100 /tmp/stick.c 0x100,0x103,0x106,0x109,table:0x10C:11
#        tools/ghidra.sh extracted/ZELRES2/006_data.bin 0000:3000 /tmp/gfmcga.c
# Ghidra location: $GHIDRA_HOME, else ~/opt/ghidra_11.3.2_PUBLIC.
set -e
BIN=${1:?usage: ghidra.sh BIN SEG:OFF OUT.c}
BASE=${2:?usage: ghidra.sh BIN SEG:OFF OUT.c}
OUTC=${3:?usage: ghidra.sh BIN SEG:OFF OUT.c}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
GH=${GHIDRA_HOME:-$HOME/opt/ghidra_11.3.2_PUBLIC}
[ -x "$GH/support/analyzeHeadless" ] || { echo "Ghidra not found at $GH (set GHIDRA_HOME)"; exit 1; }
PROJ=$(mktemp -d "${TMPDIR:-/tmp}/ghproj.XXXXXX")
trap 'rm -rf "$PROJ"' EXIT
"$GH/support/analyzeHeadless" "$PROJ" p1 -import "$(realpath "$BIN")" \
  -processor "x86:LE:16:Real Mode" -loader BinaryLoader -loader-baseAddr "$BASE" \
  -postScript ghidra_dump_c.py "$(realpath -m "$OUTC")" "${4:-}" -scriptPath "$ROOT/tools" -deleteProject \
  >"$OUTC.log" 2>&1 || { echo "analyzeHeadless failed, see $OUTC.log"; tail -20 "$OUTC.log"; exit 1; }
echo "$OUTC: $(grep -c '^[a-zA-Z].*(' "$OUTC" 2>/dev/null || echo 0) functions"
