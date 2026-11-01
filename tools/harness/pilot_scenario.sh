#!/bin/sh
# tools/harness/pilot_scenario.sh -- (re)generate a scenario's script with the
# autopilot (#75).  For tests/scenarios/NAME.zs with a `# pilot: ROUTE` line:
# make its start save from `# make:` (if missing), run
#   port/zeliard --load START --trace DIR --pilot ROUTE --emit-script FILE
# and replace the scenario's script line with what the pilot fed, one token
# per harness boundary (docs/HARNESS.md §7).  The port only: runs anywhere.
#
# usage: tools/harness/pilot_scenario.sh NAME_OR_FILE [--tail N]
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
f=$1; shift
[ -f "$f" ] || f="$ROOT/tests/scenarios/$f.zs"
TAIL=10
[ "${1:-}" = --tail ] && TAIL=$2
hdr() { sed -n "s/^# $2: *//p" "$1" | head -1; }
save=$(hdr "$f" start); name=${save%.*}
route=$(hdr "$f" pilot)
[ -n "$route" ] || { echo "pilot_scenario: $f has no '# pilot:' line" >&2; exit 2; }
PORT="$ROOT/port/zeliard"
if [ ! -f "$ROOT/zeliard/$save" ] && [ ! -f "$ROOT/zeliard/$name.USR" ]; then
  (cd "$ROOT" && "$PORT" --dir zeliard $(hdr "$f" make) >/dev/null 2>&1)
fi
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
(cd "$ROOT" && "$PORT" --dir zeliard --load "$name" --trace "$tmp/p" --pilot "$route" \
   --pilot-tail "$TAIL" --emit-script "$tmp/script") >"$tmp/out" 2>&1 || { cat "$tmp/out" >&2; exit 1; }
grep '^\[pilot\]' "$tmp/out" >&2
{ grep '^#' "$f"; cat "$tmp/script"; } >"$tmp/new"
mv "$tmp/new" "$f"
