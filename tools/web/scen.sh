#!/bin/bash
# scen.sh BUILDDIR NAME [extra]  : native vs wasm trace for one scenario
# needs node on PATH (the emsdk one is fine) and a native port/zeliard
OUT=${OUT:-/tmp/zel-web-scenarios}
B=$1; n=$2; shift 2
R=${ZEL_ROOT:-$(cd "$(dirname "$0")/../.." && pwd)}; S=$R/tests/scenarios/$n.zs
save=$(sed -n "s/^# start: *//p" $S | head -1); load=${save%.*}
script=$(grep -v "^#" $S | grep -v "^[[:space:]]*\$" | head -1)
D=$OUT/$(basename $B)/$n; rm -rf $D; mkdir -p $D/pdir
for g in $R/zeliard/*; do case $g in *.usr|*.USR) ;; *) ln -sf $g $D/pdir/;; esac; done
cp $R/zeliard/$save $D/pdir/ 2>/dev/null || cp $R/zeliard/$load.USR $D/pdir/
(cd $R && timeout 300 ./port/zeliard --dir $D/pdir --load $load --trace $D/n --script "$script" "$@" > $D/n.out 2>&1); nrc=$?
res=$(timeout 700 node $R/tools/web/run_trace.js $B $D/pdir $load $D/w "$script" "$@" 2>&1 | tail -1)
nl=$(wc -l < $D/n/frames.jsonl 2>/dev/null); wl=$(wc -l < $D/w/frames.jsonl 2>/dev/null)
if cmp -s $D/n/frames.jsonl $D/w/frames.jsonl; then v=IDENTICAL; else v=DIFF; fi
echo -e "$n\t$v\tnative_rc=$nrc frames n=$nl w=$wl\t$res"
