#!/bin/bash
# every tests/scenarios/*.zs through the Emscripten build under node (--yield-test) and byte-compare
# frames.jsonl with the native port trace.  usage: tools/web/run_scenarios.sh BUILD_DIR   (one row each)
# all.sh BUILD : every scenario
R=${ZEL_ROOT:-$(cd "$(dirname "$0")/../.." && pwd)}
for f in $R/tests/scenarios/*.zs; do
  n=$(basename $f .zs); save=$(sed -n "s/^# start: *//p" $f | head -1); name=${save%.*}
  if [ ! -f $R/zeliard/$save ] && [ ! -f $R/zeliard/$name.USR ]; then
    mk=$(sed -n "s/^# make: *//p" $f | head -1); (cd $R && ./port/zeliard --dir zeliard $mk >/dev/null 2>&1) || echo "make $save failed" >&2
  fi
done
for f in $R/tests/scenarios/*.zs; do "$(dirname "$0")/scen.sh" $1 $(basename $f .zs); done
