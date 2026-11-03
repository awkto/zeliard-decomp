#!/bin/sh
# tools/harness/state_roundtrip.sh -- port-only proof of the realtime save
# states (#52).  For each tests/scenarios/*.zs:
#
#   1. run the scenario uninterrupted (the reference trace);
#   2. for several frame boundaries N in that trace, re-run with
#      --state-save-at N and write a state blob;
#   3. run a third time from that blob (--state-load) with the remaining
#      script, and require its --trace to be identical, frame N+1 to the end,
#      to the uninterrupted run (fields, site, input and rng_calls all).
#
# It also fuzzes one blob: truncations and bit-flips must be refused cleanly
# (no signal), which is the corruption half of the issue.  No oracle and no
# droplet: this is the port talking to itself.
#
# usage: state_roundtrip.sh [-j N] [--out DIR] [--saves K] [--all] [--no-fuzz] [NAME ...]
#   -j N      scenarios in parallel (default 2)
#   --out DIR traces and blobs (default /tmp/zel-state)
#   --saves K how many N per scenario (default 3)
#   --all     save at EVERY resumable frame boundary, not just K samples; the
#             transition proof (#110): a state from any boundary must reload
#             identically.  A boundary whose frame opens a shop/status/dialogue
#             box is still skipped (no blob is written for it).
#   --no-fuzz skip the corruption check
#   NAME      run only these scenarios (name or path)
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
J=2; OUT=/tmp/zel-state; K=3; FUZZ=1; ALL=0; SEL=
while [ $# -gt 0 ]; do
  case "$1" in
    -j) J=$2; shift 2 ;;
    --out) OUT=$2; shift 2 ;;
    --saves) K=$2; shift 2 ;;
    --all) ALL=1; shift ;;
    --no-fuzz) FUZZ=0; shift ;;
    -h|--help) sed -n '2,29p' "$0"; exit 0 ;;
    *) SEL="$SEL $1"; shift ;;
  esac
done
PORT="${ZELIARD:-$ROOT/port/zeliard}"       # override to run the whole proof under a SAN build
CUT="$ROOT/tools/harness/script_cut.py"
EQ="$ROOT/tools/harness/trace_eq.py"
[ -x "$PORT" ] || { echo "state_roundtrip: no $PORT (build the port first)" >&2; exit 2; }
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)

if [ -z "$SEL" ]; then FILES=$(ls "$ROOT"/tests/scenarios/*.zs)
else
  FILES=
  for s in $SEL; do
    if [ -f "$s" ]; then FILES="$FILES $s"; else FILES="$FILES $ROOT/tests/scenarios/$s.zs"; fi
  done
fi
hdr() { sed -n "s/^# $2: *//p" "$1" | head -1; }

# make any missing start save (they share zeliard/, so serially)
for f in $FILES; do
  [ -f "$f" ] || { echo "state_roundtrip: no scenario $f" >&2; exit 2; }
  save=$(hdr "$f" start); name=${save%.*}
  if [ ! -f "$ROOT/zeliard/$save" ] && [ ! -f "$ROOT/zeliard/$name.USR" ]; then
    make=$(hdr "$f" make)
    [ -n "$make" ] || { echo "state_roundtrip: $f: no $save and no '# make:' line" >&2; exit 2; }
    (cd "$ROOT" && "$PORT" --dir zeliard $make >/dev/null 2>&1) \
      || { echo "state_roundtrip: making $save failed" >&2; exit 2; }
  fi
done

# one scenario -> OUT/NAME/result: "STATUS<TAB>FRAMES<TAB>DETAIL"
run_one() {
  f=$1; n=$(basename "$f" .zs); d="$OUT/$n"
  rm -rf "$d"; mkdir -p "$d"
  save=$(hdr "$f" start); load=${save%.*}
  script=$(grep -v '^#' "$f" | grep -v '^[[:space:]]*$' | head -1)

  # 1. the reference run
  (cd "$ROOT" && "$PORT" --dir zeliard --load "$load" --trace "$d/ref" --script "$script") \
    >"$d/ref.out" 2>&1 || { printf 'ERROR\t-\tport exit %s (ref)\n' "$?" >"$d/result"; return; }

  # frame boundaries to sample (717F / 68ED only; menus and shops are not
  # resumable at a poll, and the loader says so)
  ns=$(python3 - "$d/ref/frames.jsonl" "$K" "$ALL" <<'PY'
import json, sys
lines = [json.loads(l) for l in open(sys.argv[1]) if l.strip()]
k = int(sys.argv[2]); all_b = int(sys.argv[3])
# candidates: every frame boundary except the last (where the script runs out,
# leaving nothing to resume); sample up to k, but try them all when there are few
fr = [o["f"] for o in lines if o["site"] in ("717F", "68ED")][:-1]
if not fr:
    sys.exit(0)
if all_b:
    picks = fr
elif len(fr) <= k:
    picks = fr
else:
    picks = []
    for i in range(1, k + 1):
        idx = (len(fr) - 1) * i // (k + 1)
        if fr[idx] not in picks:
            picks.append(fr[idx])
print(" ".join(str(x) for x in picks))
PY
)
  [ -n "$ns" ] || { printf 'ERROR\t-\tno frame boundary to sample\n' >"$d/result"; return; }

  ok=0; tried=0; skipped=0; detail=; nfail=0; keyn=
  for N in $ns; do
    # 2. the run that writes the state
    (cd "$ROOT" && "$PORT" --dir zeliard --load "$load" --trace "$d/b$N" \
        --script "$script" --state-save-at "$N" "$d/state_$N.bin") >"$d/b$N.out" 2>&1
    if [ ! -f "$d/state_$N.bin" ]; then
      # a boundary whose frame ends inside a shop/status/dialogue is not a
      # resumable point; the loader reports it, we just move on
      skipped=$((skipped + 1))
      continue
    fi
    tried=$((tried + 1))
    # 3. the resumed run: the remaining script starts at token N (the token the
    # boundary itself applied); the loader re-applies it, idempotently
    tail=$(python3 "$CUT" "$N" "$script")
    (cd "$ROOT" && "$PORT" --dir zeliard --load "$load" --trace "$d/c$N" \
        --script "$tail" --state-load "$d/state_$N.bin") >"$d/c$N.out" 2>&1
    if (cd "$ROOT" && python3 "$EQ" "$d/ref/frames.jsonl" "$d/c$N/frames.jsonl" "$N") \
        >"$d/eq$N.txt" 2>&1; then
      ok=$((ok + 1))
      [ -n "$keyn" ] || keyn=$N
      # keep one blob per scenario for the corruption check below
      if [ -f "$d/keep.bin" ]; then rm -f "$d/state_$N.bin"; else mv "$d/state_$N.bin" "$d/keep.bin"; fi
    else
      nfail=$((nfail + 1))
      if [ "$nfail" -le 8 ]; then
        one=$(sed -n '1p' "$d/eq$N.txt" | cut -c1-100)
        detail="$detail N=$N:$one;"
      fi
    fi
  done
  [ "$nfail" -gt 8 ] && detail="$detail (+$((nfail - 8)) more)"

  # 4. #52 step 2: the same save/load through the *in-game* request path -- a
  # queued request run by the shell's frame_done hook, the code a Ctrl+. / Ctrl+\
  # key press uses.  Save the quick slot at boundary N and load it back at the
  # same boundary: the round trip must leave the whole trace byte-identical.
  keyres=
  if [ -n "$keyn" ]; then
    (cd "$ROOT" && "$PORT" --dir zeliard --load "$load" --trace "$d/key" \
        --state-dir "$d/states" --state-inject "save:$keyn" --state-inject "load:$keyn" \
        --script "$script") >"$d/key.out" 2>&1
    if (cd "$ROOT" && python3 "$EQ" "$d/ref/frames.jsonl" "$d/key/frames.jsonl" -1) \
        >"$d/key_eq.txt" 2>&1; then
      keyres=", key path clean (N=$keyn)"
    else
      keyres=", key path DIVERGES N=$keyn: $(sed -n '1p' "$d/key_eq.txt")"
    fi
  fi

  frames=$(wc -l < "$d/ref/frames.jsonl")
  if [ "$tried" -eq 0 ]; then
    printf 'SKIP\t%s\tno resumable frame boundary among the samples (%s skipped)\n' "$frames" "$skipped" >"$d/result"
  elif [ "$ok" -eq "$tried" ] && [ "${keyres#*, key path DIVERGES}" = "$keyres" ]; then
    printf 'clean\t%s\t%s N round-trips%s%s\n' "$frames" "$ok" "${skipped:+, $skipped skipped}" "$keyres" >"$d/result"
  else
    printf 'DIVERGES\t%s\t%s/%s N round-trips;%s%s\n' "$frames" "$ok" "$tried" "$detail" "$keyres" >"$d/result"
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

# ------------------------------------------------------------------ report
printf '%-24s %-9s %s\n' SCENARIO STATUS DETAIL
fail=0
for f in $FILES; do
  n=$(basename "$f" .zs); d="$OUT/$n"
  if [ ! -f "$d/result" ]; then
    printf '%-24s %-9s %s\n' "$n" "ERROR" "no result"; fail=1; continue
  fi
  status=$(cut -f1 "$d/result"); frames=$(cut -f2 "$d/result"); detail=$(cut -f3 "$d/result")
  printf '%-24s %-9s %s\n' "$n" "$status" "$detail"
  case "$status" in clean) ;; *) fail=1 ;; esac
done

# ------------------------------------------------------------ corruption
if [ "$FUZZ" = 1 ]; then
  base=$(ls "$OUT"/*/keep.bin "$OUT"/*/state_*.bin 2>/dev/null | head -1)
  if [ -n "$base" ]; then
    fd="$OUT/fuzz"; rm -rf "$fd"; mkdir -p "$fd"
    echo
    echo "corruption check (truncated / bit-flipped states must not crash):"
    size=$(wc -c < "$base")
    ncrash=0; ncase=0
    try() {  # $1 = corrupt file
      ncase=$((ncase + 1))
      # sanitizer errors abort (rc >= 128) so a latent OOB is caught
      ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:abort_on_error=1 \
        timeout 30 "$PORT" --dir zeliard --trace "$fd/t" --script "." --state-load "$1" \
        >"$fd/out" 2>&1
      rc=$?
      if [ "$rc" -ge 128 ]; then
        ncrash=$((ncrash + 1))
        echo "  CRASH (rc=$rc) on $1"
      fi
    }
    for L in 0 1 7 8 15 16 19 20 $((size / 2)) $((size - 1)); do
      head -c "$L" "$base" > "$fd/cut_$L.bin"
      try "$fd/cut_$L.bin"
    done
    for O in 0 7 8 11 12 15 16 $((size - 1)); do
      cp "$base" "$fd/flip_$O.bin"
      python3 - "$fd/flip_$O.bin" "$O" <<'PY'
import sys
p, o = sys.argv[1], int(sys.argv[2])
b = bytearray(open(p, "rb").read())
b[o] ^= 0x01
open(p, "wb").write(b)
PY
      try "$fd/flip_$O.bin"
    done
    if [ "$ncrash" -eq 0 ]; then
      echo "  $ncase corrupt states refused cleanly, 0 crashes"
    else
      fail=1
    fi
  fi
fi

exit $fail
