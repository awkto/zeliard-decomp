#!/bin/sh
# Ground truth for `--video` (issue #32): run the real game under DOSBox with
# RESOURCE.CFG's `videoDrv` set to one driver, and capture its screen at the
# driver's own resolution.
#
#   usage: [KEYS="sec:key ..."] [USR=file.usr] \
#          port/tools/capture_video_mode.sh MODE OUTDIR [capture-seconds...]
#
#   MODE = mcga | cga | cga2 | ega | hgc | tandy   (the --video spellings)
#
# Why this exists next to tools/run_dosbox.sh rather than inside it:
#
#  * every mode needs a different DOSBox `machine=` — Tandy mode 9 only exists
#    on machine=tandy, Hercules only on machine=hercules, and the CGA
#    burst-off palette is only right on machine=cga;
#  * the captures have to be **native resolution** (320x200, 640x200, 720x348),
#    so `scaler=none` and the DOSBox window is grabbed by its own window id
#    instead of cropping a fixed offset out of the root window;
#  * `zeliard/` is copied per run anyway, so RESOURCE.CFG can be rewritten in
#    the copy and the original is never touched.
#
# KEYS uses run_dosbox.sh's syntax (`sec:key`, `+key` hold, `-key` release,
# fractional seconds).  USR names a .usr file (from `zeliard --save`) to drop
# into the game directory for an F7 restore (docs/DOSBOX_RECIPE.md §9).
set -e
MODE=${1:?usage: capture_video_mode.sh MODE OUTDIR [seconds...]}
OUT=${2:?usage: capture_video_mode.sh MODE OUTDIR [seconds...]}
shift 2
TIMES=${*:-"20 30 40 50 56"}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)

# The DOSBox machine each driver needs.  `machine=ega` and `machine=vgaonly`
# put the *same* eight colours on a 200-line mode-0Eh screen (the CGA-class
# RGBI reading of the palette registers, see port/video_ega.c), but vgaonly
# draws into a 16-bit surface, so a capture off it is RGB565-truncated and can
# never be pixel-exact against 8-bit RGB.  `ega` gives true 8-bit colour.
# MACHINE= in the environment overrides any of these.
case "$MODE" in
  mcga)  DRV=MCGA; M=svga_s3  ;;
  cga)   DRV=CGA;  M=cga      ;;
  cga2)  DRV=CGA2; M=cga      ;;
  ega)   DRV=EGA;  M=ega      ;;
  hgc)   DRV=HGC;  M=hercules ;;
  tandy|tga) DRV=TGA; M=tandy ;;
  *) echo "unknown mode $MODE (mcga cga cga2 ega hgc tandy)"; exit 2 ;;
esac
MACHINE=${MACHINE:-$M}

mkdir -p "$OUT/dos/zeliard"
cp "$ROOT"/zeliard/* "$OUT/dos/zeliard/"
sed -i "s/^videoDrv:.*/videoDrv:$DRV/" "$OUT/dos/zeliard/RESOURCE.CFG"
[ -n "$USR" ] && cp "$USR" "$OUT/dos/zeliard/"
echo "videoDrv -> $(grep videoDrv "$OUT/dos/zeliard/RESOURCE.CFG")  (machine=$MACHINE)"

cat > "$OUT/zel.conf" <<EOC
[sdl]
output=surface
[dosbox]
machine=$MACHINE
[render]
aspect=false
scaler=none
[cpu]
cycles=3000
[autoexec]
mount c $OUT/dos
c:
cd zeliard
zeliard
EOC

# a free display, exactly as tools/run_dosbox.sh picks one
D=200
while [ $D -lt 300 ]; do
  if [ ! -e "/tmp/.X$D-lock" ] && [ ! -e "/tmp/.X11-unix/X$D" ] \
     && ! ss -xl 2>/dev/null | grep -q "@/tmp/.X11-unix/X$D\b"; then
    break
  fi
  D=$((D + 1))
done
# big enough for every mode's window (Hercules text is 720x350) with no WM
Xvfb ":$D" -screen 0 1024x768x24 -nolisten tcp >"$OUT/xvfb.log" 2>&1 &
XPID=$!
sleep 1
kill -0 $XPID 2>/dev/null || { echo "Xvfb :$D failed to start:"; cat "$OUT/xvfb.log"; exit 1; }
export DISPLAY=":$D"
trap 'kill $DPID $XPID 2>/dev/null || true' EXIT INT TERM

dosbox -conf "$OUT/zel.conf" >"$OUT/dosbox.log" 2>&1 &
DPID=$!

EVENTS=$( { for k in $KEYS; do echo "${k%%:*} key ${k#*:}"; done; for i in $TIMES; do echo "$i shot"; done; } | sort -n -s )
t=0
echo "$EVENTS" | while read -r sec kind arg; do
  [ -n "$sec" ] || continue
  d=$(awk -v a="$sec" -v b="$t" 'BEGIN{d=a-b; if (d>0) print d}'); [ -n "$d" ] && sleep "$d"; t=$sec
  WIN=$(xdotool search --onlyvisible --class dosbox 2>/dev/null | head -1)
  if [ "$kind" = key ]; then
    [ -n "$WIN" ] && xdotool windowfocus --sync "$WIN" 2>/dev/null
    case "$arg" in
      +*) xdotool keydown "${arg#+}" 2>/dev/null ;;
      -*) xdotool keyup "${arg#-}" 2>/dev/null ;;
      *) xdotool key --delay 80 "$arg" 2>/dev/null ;;
    esac
  else
    # grab exactly the DOSBox client area, whatever size the mode made it
    G=$(xdotool getwindowgeometry --shell "$WIN" 2>/dev/null || true)
    if [ -n "$G" ]; then
      eval "$G"
      import -window root -crop "${WIDTH}x${HEIGHT}+${X}+${Y}" +repage "$OUT/shot_$sec.png" 2>/dev/null || true
      echo "  $sec s: ${WIDTH}x${HEIGHT} -> $OUT/shot_$sec.png"
    fi
  fi
done
echo "captures in $OUT/shot_*.png ($MODE, display :$D)"
