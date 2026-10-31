#!/bin/sh
# tools/harness/run_scenarios.sh -- run the #49 scenario library through the harness:
# for every tests/scenarios/*.zs, the oracle (tools/oracle.sh) and the port
# (port/zeliard --trace) play the same script from the same save, then
# tools/tracediff.py compares them.  Prints one row per scenario and exits
# non-zero if any scenario diverges or fails to run.
#
# usage: tools/harness/run_scenarios.sh [-j N] [--out DIR] [--png] [NAME_OR_FILE ...]
#   -j N     scenarios run in parallel (default 4; each oracle uses one core)
#   --out    where the traces go (default /tmp/zel-scenarios); DIR/NAME/{o,p,diff.txt}
#   --png    also write frame PNGs (every frame) and tracediff's oracle|port|diff images
#   NAME     run only these (a name from tests/scenarios/ or a path to a .zs)
# env: ORACLE_DOSBOX / ORACLE_BUILD as for tools/oracle.sh.
#
# Meant for the claude-test droplet (docs/HARNESS.md §5): it builds nothing.
# port/zeliard and the oracle binary must already exist.  A start save that is
# missing from zeliard/ is made with the scenario's `# make:` line
# (`port/zeliard --dir zeliard <make args>`).
#
# .zs format:
#   # start: NAME.usr          the save both sides restore (--load NAME)
#   # make: ARGS               port/zeliard arguments that create it (ends in --save NAME)
#   # what: TEXT               what the scenario exercises (free text)
#   SCRIPT                     the first non-comment line: the --script input
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
J=4; OUT=/tmp/zel-scenarios; PNG=0; SEL=
while [ $# -gt 0 ]; do
  case "$1" in
    -j) J=$2; shift 2 ;;
    --out) OUT=$2; shift 2 ;;
    --png) PNG=1; shift ;;
    -h|--help) sed -n '2,24p' "$0"; exit 0 ;;
    *) SEL="$SEL $1"; shift ;;
  esac
done
PORT="$ROOT/port/zeliard"
[ -x "$PORT" ] || { echo "run_scenarios: no $PORT (build the port first)" >&2; exit 2; }
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)

if [ -z "$SEL" ]; then FILES=$(ls "$ROOT"/tests/scenarios/*.zs)
else
  FILES=
  for s in $SEL; do
    if [ -f "$s" ]; then FILES="$FILES $s"; else FILES="$FILES $ROOT/tests/scenarios/$s.zs"; fi
  done
fi

hdr() { sed -n "s/^# $2: *//p" "$1" | head -1; }

# make the missing start saves first (serially: they share zeliard/)
for f in $FILES; do
  [ -f "$f" ] || { echo "run_scenarios: no scenario $f" >&2; exit 2; }
  save=$(hdr "$f" start); name=${save%.*}
  if [ ! -f "$ROOT/zeliard/$save" ] && [ ! -f "$ROOT/zeliard/$name.USR" ]; then
    make=$(hdr "$f" make)
    [ -n "$make" ] || { echo "run_scenarios: $f: no $save and no '# make:' line" >&2; exit 2; }
    echo "making $save: zeliard --dir zeliard $make" >&2
    (cd "$ROOT" && "$PORT" --dir zeliard $make >/dev/null 2>&1) || { echo "run_scenarios: making $save failed" >&2; exit 2; }
  fi
done

# one scenario -> OUT/NAME/result: "STATUS<tab>FRAMES<tab>DETAIL"
run_one() {
  f=$1; n=$(basename "$f" .zs); d="$OUT/$n"
  rm -rf "$d"; mkdir -p "$d"
  save=$(hdr "$f" start); load=${save%.*}
  script=$(grep -v '^#' "$f" | grep -v '^[[:space:]]*$' | head -1)
  pe=0; [ "$PNG" = 1 ] && pe=1
  if ! (cd "$ROOT" && tools/oracle.sh "$script" --load "$load" --out "$d/o" --png-every $pe) >"$d/oracle.out" 2>&1; then
    printf 'ERROR\t-\toracle exit %s (%s/oracle.out, o/oracle.log)\n' "$?" "$d" >"$d/result"; return
  fi
  pargs=; [ "$PNG" = 1 ] && pargs="--png-every 1"
  if ! (cd "$ROOT" && "$PORT" --dir zeliard --load "$load" --trace "$d/p" --script "$script" $pargs) >"$d/port.out" 2>&1; then
    printf 'ERROR\t-\tport exit %s (%s/port.out)\n' "$?" "$d" >"$d/result"; return
  fi
  dargs=--first-only; [ "$PNG" = 1 ] && dargs=--png
  (cd "$ROOT" && python3 tools/tracediff.py "$d/o" "$d/p" $dargs) >"$d/diff.txt" 2>&1
  rc=$?
  frames=$(sed -n 's/^tracediff: \([0-9]*\) aligned.*/\1/p' "$d/diff.txt")
  if [ $rc = 0 ]; then printf 'clean\t%s\t\n' "$frames" >"$d/result"
  elif [ $rc = 1 ]; then
    first=$(sed -n 's/^FIRST DIVERGENCE at frame \([0-9]*\).*/\1/p' "$d/diff.txt")
    field=$(awk '/field\(s\) differ:/{getline; print $1; exit}' "$d/diff.txt")
    printf 'DIVERGES\t%s\tframe %s: %s\n' "$frames" "$first" "$field" >"$d/result"
  else printf 'ERROR\t%s\ttracediff exit %s (%s/diff.txt)\n' "${frames:--}" "$rc" "$d" >"$d/result"
  fi
}

# a simple job pool
running=0
for f in $FILES; do
  run_one "$f" &
  running=$((running + 1))
  if [ $running -ge "$J" ]; then wait; running=0; fi
done
wait

fail=0
printf '%-20s %-9s %6s  %s\n' scenario result frames detail
for f in $FILES; do
  n=$(basename "$f" .zs)
  r=$(cat "$OUT/$n/result" 2>/dev/null || printf 'ERROR\t-\tno result')
  st=$(printf '%s' "$r" | cut -f1); fr=$(printf '%s' "$r" | cut -f2); de=$(printf '%s' "$r" | cut -f3)
  printf '%-20s %-9s %6s  %s\n' "$n" "$st" "$fr" "$de"
  [ "$st" = clean ] || fail=1
done
exit $fail
