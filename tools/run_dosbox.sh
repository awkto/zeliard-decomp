#!/bin/sh
# Ground-truth harness: run Zeliard under DOSBox in a private Xvfb, capture screenshots.
# usage: [KEYS="sec:key sec:key ..."] tools/run_dosbox.sh OUTDIR [capture-seconds...]
#   KEYS: xdotool key names to press at given seconds, e.g. KEYS="6:Return 9:space 12:Return"
#   Keys and captures are merged into one timeline. Requires: dosbox, xvfb, xdotool,
#   imagemagick (import). Game files in zeliard/.
#
# Picks a free X display (never a hardcoded one) and aborts if Xvfb fails to
# start, so DOSBox and the screenshots can never land on someone else's display.
set -e
OUT=${1:?usage: run_dosbox.sh OUTDIR [seconds...]}
shift
TIMES=${*:-"4 8 12 16 20 26 32 40 50 60"}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$OUT/dos"
[ -d "$OUT/dos/zeliard" ] || { mkdir -p "$OUT/dos/zeliard"; cp "$ROOT"/zeliard/* "$OUT/dos/zeliard/"; }
cat > "$OUT/zel.conf" <<EOC
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
EOC

# find a free display number: no lock, no unix socket, no abstract socket
D=200
while [ $D -lt 300 ]; do
  if [ ! -e "/tmp/.X$D-lock" ] && [ ! -e "/tmp/.X11-unix/X$D" ] \
     && ! ss -xl 2>/dev/null | grep -q "@/tmp/.X11-unix/X$D\b"; then
    break
  fi
  D=$((D + 1))
done
Xvfb ":$D" -screen 0 640x480x24 -nolisten tcp >"$OUT/xvfb.log" 2>&1 &
XPID=$!
sleep 1
kill -0 $XPID 2>/dev/null || { echo "Xvfb :$D failed to start:"; cat "$OUT/xvfb.log"; exit 1; }
[ -S "/tmp/.X11-unix/X$D" ] || { echo "Xvfb :$D has no socket"; kill $XPID; exit 1; }
export DISPLAY=":$D"
trap 'kill $DPID $XPID 2>/dev/null || true' EXIT INT TERM

dosbox -conf "$OUT/zel.conf" >/dev/null 2>&1 &
DPID=$!

# merge "sec:key" presses and "sec" captures into one sorted timeline
EVENTS=$( { for k in $KEYS; do echo "${k%%:*} key ${k#*:}"; done; for i in $TIMES; do echo "$i shot"; done; } | sort -n -s )
WIN=""
t=0
echo "$EVENTS" | while read -r sec kind arg; do
  [ -n "$sec" ] || continue
  d=$((sec - t)); [ $d -gt 0 ] && sleep $d; t=$sec
  if [ "$kind" = key ]; then
    [ -n "$WIN" ] || WIN=$(xdotool search --onlyvisible --class dosbox 2>/dev/null | head -1)
    [ -n "$WIN" ] && xdotool windowfocus --sync "$WIN" 2>/dev/null
    xdotool key --delay 80 "$arg" 2>/dev/null
  else
    import -window root "$OUT/shot_$sec.png" 2>/dev/null || true
  fi
done
echo "captures in $OUT/shot_*.png (display :$D)"
