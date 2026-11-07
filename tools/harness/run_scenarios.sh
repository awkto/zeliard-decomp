#!/bin/sh
# tools/harness/run_scenarios.sh -- run the #49 scenario library through the harness:
# for every tests/scenarios/*.zs, the oracle (tools/oracle.sh) and the port
# (port/zeliard --trace) play the same script from the same save, then
# tools/tracediff.py compares them.  Prints one row per scenario and exits
# non-zero if any scenario diverges or fails to run.
#
# usage: tools/harness/run_scenarios.sh [-j N] [--out DIR] [--png-every K] [--png] [NAME_OR_FILE ...]
#   -j N     scenarios run in parallel (default 4; each oracle uses one core)
#   --out    where the traces go (default /tmp/zel-scenarios); DIR/NAME/{o,p,diff.txt}
#   --png-every K  trace PNGs every K frames on both sides and compare pixels on those
#            frames (default 1; 0 = fields only, no pixel check)
#   --png    keep the PNGs of passing scenarios too, and write tracediff's images
#            (failing scenarios always keep theirs; passing ones are cleaned up)
#   NAME     run only these (a name from tests/scenarios/ or a path to a .zs)
# env: ORACLE_DOSBOX / ORACLE_BUILD as for tools/oracle.sh.  ORACLE_MAX_MS defaults to 1200000 here
#      (the ending, jashiin_ending, is ~530 s of emulated time; the oracle's own default is 600 s).
#
# Meant for the claude-test droplet (docs/HARNESS.md §5): it builds nothing.
# port/zeliard and the oracle binary must already exist.  A start save that is
# missing from zeliard/ is made with the scenario's `# make:` line
# (`port/zeliard --dir zeliard <make args>`).
#
# .zs format:
#   # start: NAME.usr          the save both sides restore (--load NAME)
#   # make: ARGS               port/zeliard arguments that create it (ends in --save NAME)
#   # cave: I,COL,ROW,SIDE,MAP optional: rewrite the start town's cave record I (oracle ORACLE_CAVE at
#                              frame 0, port --cave); the cave mouth then leads to MAP (HARNESS.md s6)
#   # what: TEXT               what the scenario exercises (free text)
#   # allow: PATTERN [MODE]    optional, repeatable: a tracediff allow rule for this
#                              scenario only (a known, filed divergence it must step over)
#   # allow-px: x0,y0,x1,y1 [frames A-B] -- reason
#                              optional, repeatable: pixels (inclusive rect, game 320x200
#                              coords, optional frame range) ignored by the pixel compare;
#                              the reason is mandatory (tools/harness/pixdiff.py)
#   # png-every: K             this scenario's PNG interval when PNGs are on (a long one: the intro's 7439 boundaries)
#   # intro: 1                 the scenario is the opening demo (opdemo.bin) from boot, no save (#34): the oracle runs
#                              with ORACLE_INTRO=1 and the port with --intro; boundary site `intro:wait`.  The script
#                              needs at least as many tokens as boundaries; both sides stop at the town load
#   # ends: stall              optional: the scenario ends in a cutscene the oracle has no
#                              boundary site for (the ending had none until #113): the oracle's stall exit (6)
#                              after ORACLE_STALL_MS (default 60 s here, long enough for the tear scene) is the expected end of its
#                              trace, and the port's trace is compared up to that frame
#   SCRIPT                     the first non-comment line: the --script input
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
J=4; OUT=/tmp/zel-scenarios; PNG=0; PE=1; SEL=
while [ $# -gt 0 ]; do
  case "$1" in
    -j) J=$2; shift 2 ;;
    --out) OUT=$2; shift 2 ;;
    --png-every) PE=$2; shift 2 ;;
    --png) PNG=1; shift ;;
    -h|--help) sed -n '2,34p' "$0"; exit 0 ;;
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
  [ "$(hdr "$f" intro)" = 1 ] && continue
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
  pe=$PE; hp=$(hdr "$f" png-every); [ -n "$hp" ] && [ "$PE" != 0 ] && pe=$hp   # `# png-every: K` caps a long scenario's PNGs
  cave=$(hdr "$f" cave); cargs=; [ -n "$cave" ] && cargs="--cave $cave"
  ends=$(hdr "$f" ends); stall=
  [ "$ends" = stall ] && stall=${ORACLE_STALL_MS:-60000}
  intro=$(hdr "$f" intro); oload="--load $load"; ploads="--load $load"; ienv=
  [ "$intro" = 1 ] && { oload=; ploads=--intro; ienv=ORACLE_INTRO=1; save=; load=; }
  (cd "$ROOT" && env ORACLE_MAX_MS=${ORACLE_MAX_MS:-1200000} ORACLE_CAVE="$cave" $ienv ${stall:+ORACLE_STALL_MS=$stall} tools/oracle.sh "$script" $oload --out "$d/o" --png-every $pe) >"$d/oracle.out" 2>&1
  orc=$?
  if [ $orc != 0 ] && ! { [ -n "$stall" ] && [ $orc = 6 ]; }; then
    printf 'ERROR\t-\toracle exit %s (%s/oracle.out, o/oracle.log)\n' "$orc" "$d" >"$d/result"; return
  fi
  # isolated game dir, as tools/oracle.sh stages for the oracle: the original files plus
  # only this scenario's start save, so screens that list saves (the Sage's name entry)
  # match, and any save the port writes lands here, not in the shared zeliard/ (#100)
  pd="$d/pdir"; mkdir -p "$pd"
  for g in "$ROOT"/zeliard/*; do
    case "$g" in *.usr|*.USR) ;; *) ln -s "$g" "$pd/" ;; esac
  done
  for g in "$ROOT/zeliard/$save" "$ROOT/zeliard/$load.usr" "$ROOT/zeliard/$load.USR"; do
    [ -n "$load" ] && [ -f "$g" ] && { cp "$g" "$pd/"; break; }
  done
  pargs=; [ "$pe" != 0 ] && pargs="--png-every $pe"
  if ! (cd "$ROOT" && "$PORT" --dir "$pd" $ploads --trace "$d/p" --script "$script" $cargs $pargs) >"$d/port.out" 2>&1; then
    printf 'ERROR\t-\tport exit %s (%s/port.out)\n' "$?" "$d" >"$d/result"; return
  fi
  dargs=--first-only; [ "$PNG" = 1 ] && dargs=--png
  # `# allow: PATTERN [MODE]` lines add to tools/harness/allow.tsv for this scenario only
  if grep -q '^# allow:' "$f"; then
    { cat "$ROOT/tools/harness/allow.tsv" 2>/dev/null
      sed -n 's/^# allow: *//p' "$f" | awk '{ print $1 "\t" ($2 == "" ? "any" : $2) }'; } >"$d/allow.tsv"
    dargs="$dargs --allow $d/allow.tsv"
  fi
  (cd "$ROOT" && python3 tools/tracediff.py "$d/o" "$d/p" $dargs) >"$d/diff.txt" 2>&1
  rc=$?
  frames=$(sed -n 's/^tracediff: \([0-9]*\) aligned.*/\1/p' "$d/diff.txt")
  px=; prc=0
  if [ "$pe" != 0 ]; then
    (cd "$ROOT" && python3 tools/harness/pixdiff.py "$d/o" "$d/p" --scenario "$f" --out "$d/pixdiff") >"$d/pixdiff.txt" 2>&1
    prc=$?
    px=$(sed -n 's/^PIXDIFF //p' "$d/pixdiff.txt" | head -1)
  fi
  pxdesc=
  if [ $prc = 1 ]; then
    pxdesc=$(printf '%s' "$px" | sed 's/compared=[0-9]* //; s/diff_frames=/px-frames=/; s/first=/first-px-frame=/')
  fi
  if [ $rc -gt 1 ] || [ $prc -gt 1 ]; then
    printf 'ERROR\t%s\ttracediff exit %s, pixdiff exit %s (%s/diff.txt, pixdiff.txt)\n' "${frames:--}" "$rc" "$prc" "$d" >"$d/result"
  elif [ $rc = 1 ]; then
    first=$(sed -n 's/^FIRST DIVERGENCE at frame \([0-9]*\).*/\1/p' "$d/diff.txt")
    field=$(awk '/field\(s\) differ:/{getline; print $1; exit}' "$d/diff.txt")
    printf 'DIVERGES\t%s\tframe %s: %s%s\n' "$frames" "$first" "$field" "${pxdesc:+; $pxdesc}" >"$d/result"
  elif [ $prc = 1 ]; then printf 'PIXELS\t%s\t%s\n' "$frames" "$pxdesc" >"$d/result"
  else
    printf 'clean\t%s\t\n' "$frames" >"$d/result"
    # passing: drop the big PNGs (a failing scenario keeps them)
    [ "$PNG" = 1 ] || rm -f "$d"/o/*.png "$d"/p/*.png
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
