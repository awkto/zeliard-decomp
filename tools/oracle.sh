#!/bin/sh
# Run the original Zeliard under the oracle DOSBox-X build and trace it frame by frame.
# usage: tools/oracle.sh SCRIPT [--load NAME] --out DIR [--png-every K]
#   SCRIPT   the port's --script language ("R20 .10 X1 ..."), or a file holding it
#   --load   NAME.USR to F7-restore (a path, or a name looked up in . and zeliard/);
#            without it the trace starts at the first town frame after the intro
#   --out    output directory: frames.jsonl, base.bin, frame_NNNNN.png, oracle.log
# env: ORACLE_DOSBOX (binary; default $ORACLE_BUILD/src/dosbox-x, see
#      tools/harness/build-oracle.sh), ORACLE_MAX_MS, ORACLE_STALL_MS, ORACLE_DEBUG_MS.
# Exit status: 0 = script consumed; 3 watchdog; 4/5 restore failed; 6 stalled
# (left the town/fight loops: menu, shop, cutscene).  See docs/HARNESS.md §6.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SCRIPT=; LOAD=; OUT=; PNG=0
while [ $# -gt 0 ]; do
  case "$1" in
    --load) LOAD=$2; shift 2 ;;
    --out) OUT=$2; shift 2 ;;
    --png-every) PNG=$2; shift 2 ;;
    -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
    *) [ -z "$SCRIPT" ] || { echo "oracle.sh: extra argument $1" >&2; exit 2; }; SCRIPT=$1; shift ;;
  esac
done
[ -n "$OUT" ] || { echo "usage: oracle.sh SCRIPT [--load NAME] --out DIR [--png-every K]" >&2; exit 2; }
BUILD=${ORACLE_BUILD:-$HOME/.cache/zel-oracle/dosbox-x}
DOSBOX=${ORACLE_DOSBOX:-$BUILD/src/dosbox-x}
[ -x "$DOSBOX" ] || { echo "oracle.sh: no oracle binary at $DOSBOX (run tools/harness/build-oracle.sh)" >&2; exit 2; }
[ -f "$SCRIPT" ] && SCRIPT=$(cat "$SCRIPT")

mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
rm -rf "$OUT/stage" "$OUT"/frame_*.png "$OUT"/boot_*.png "$OUT/frames.jsonl" "$OUT/base.bin" "$OUT/final.png"
G="$OUT/stage/dos/zeliard"
mkdir -p "$G"
for f in "$ROOT"/zeliard/*; do
  case "$f" in *.usr|*.USR) ;; *) cp "$f" "$G/" ;; esac
done
LOADFLAG=0
if [ -n "$LOAD" ]; then
  U=
  for c in "$LOAD" "$LOAD.USR" "$LOAD.usr" "$ROOT/zeliard/$LOAD.usr" "$ROOT/zeliard/$LOAD.USR"; do
    [ -f "$c" ] && { U=$c; break; }
  done
  [ -n "$U" ] || { echo "oracle.sh: save $LOAD not found" >&2; exit 2; }
  N=$(basename "$U"); N=${N%.*}
  # the only .USR in the directory, so the restore list's first entry is it
  cp "$U" "$G/$(echo "$N" | tr a-z A-Z).USR"
  LOADFLAG=1
fi
# file dates reach the guest (the restore list's DTA lives inside BASE): pin them
find "$OUT/stage" -exec touch -h -d '2000-01-01 00:00:00 UTC' {} +
sed "s|@DOSDIR@|$OUT/stage/dos|" "$ROOT/tools/harness/oracle.conf" > "$OUT/stage/oracle.conf"

NICE=
command -v nice >/dev/null && NICE="nice -n 19"
command -v ionice >/dev/null && NICE="$NICE ionice -c3"
cd "$OUT/stage"
set +e
TZ=UTC SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
ORACLE_OUT="$OUT" ORACLE_SCRIPT="$SCRIPT" ORACLE_LOAD=$LOADFLAG ORACLE_PNG_EVERY=$PNG \
  $NICE "$DOSBOX" -conf "$OUT/stage/oracle.conf" -nopromptfolder -fastlaunch \
  > "$OUT/dosbox.log" 2>&1
RC=$?
set -e
N=0; [ -f "$OUT/frames.jsonl" ] && N=$(wc -l < "$OUT/frames.jsonl")
echo "oracle: exit $RC, $N frames in $OUT ($(tail -1 "$OUT/oracle.log"))"
exit $RC
