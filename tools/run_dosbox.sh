#!/bin/sh
# Ground-truth harness: run Zeliard under DOSBox in Xvfb, capture screenshots.
# usage: tools/run_dosbox.sh OUTDIR [capture-seconds...]
# Requires: dosbox, xvfb, imagemagick (import). Game files in zeliard/.
set -e
OUT=${1:?usage: run_dosbox.sh OUTDIR [seconds...]}
shift
TIMES=${*:-"4 8 12 16 20 26 32 40 50 60"}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$OUT/dos"
[ -d "$OUT/dos/zeliard" ] || { mkdir -p "$OUT/dos/zeliard"; cp "$ROOT"/zeliard/* "$OUT/dos/zeliard/"; }
cat > "$OUT/zel.conf" <<EOF
[sdl]
output=surface
[render]
aspect=false
[cpu]
cycles=3000
[autoexec]
mount c $OUT/dos
c:
cd zeliard
zeliard
EOF
Xvfb :77 -screen 0 800x600x24 &
XPID=$!
sleep 1
DISPLAY=:77 dosbox -conf "$OUT/zel.conf" >/dev/null 2>&1 &
DPID=$!
t=0
for i in $TIMES; do
  sleep $((i - t)); t=$i
  DISPLAY=:77 import -window root "$OUT/shot_$i.png" 2>/dev/null || true
done
kill $DPID $XPID 2>/dev/null || true
echo "captures in $OUT/shot_*.png"
