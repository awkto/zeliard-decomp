#!/bin/sh
# check_counts.sh — fail when the assertion counts quoted in the documentation
# do not match what the suite just ran (#45: they drifted once, twice counting
# the per-binary bullets, and nothing caught it).
#
# Usage: tools/check_counts.sh COUNTS_LOG   (run from port/; `make test` does)
# COUNTS_LOG holds the ten binaries' "N checks, M failures" lines in run order.
# Checked against:
#   * port/README.md's `make test` block: the per-binary "(N)" list + total
#   * every "* `test_X.c` — N assertions" bullet in port/README.md
#   * docs/ROADMAP.md's "N assertions" total
#   * the `make verify` comparison count (static: one compare_shot.py call per
#     comparison) vs README's "N headless renders" and ROADMAP's
#     "N pixel comparisons"
set -eu
log="$1"

# Without the game files the binaries run their reduced half (or print no
# summary at all), so the numbers cannot match the documented full-suite
# counts.  Skip loudly, the same contract as make verify / make playthrough:
# the check bites wherever zeliard/ exists, which is where the docs are edited.
if [ ! -f ../zeliard/ZELRES1.SAR ] && [ ! -f ../zeliard/zelres1.sar ]; then
    echo "SKIP: the assertion-count check needs the original game files in"
    echo "      zeliard/ -- the reduced no-data suite cannot match the docs."
    exit 0
fi

fail=0
complain() { echo "COUNT DRIFT: $*" >&2; fail=1; }

# the run order of `make test` (matches the recipe)
names="physics combat town boss shop status playthrough audio cutscene video"
counts=$(grep -oE '^[0-9]+ checks' "$log" | awk '{print $1}')
n=$(echo "$counts" | wc -w)
[ "$n" -eq 10 ] || { complain "expected 10 'N checks' lines in $log, found $n"; exit 1; }
total=0
for c in $counts; do total=$((total + c)); done

# --- README's `make test` block: ten "(N)" in run order, then "= N assertions"
block=$(awk '/^make test /,/assertions/' README.md)
block_counts=$(echo "$block" | grep -oE '\([0-9]+\)' | tr -d '()')
if [ "$(echo "$block_counts" | tr '\n' ' ')" != "$(echo "$counts" | tr '\n' ' ')" ]; then
    complain "README.md 'make test' block says [$(echo "$block_counts" | tr '\n' ' ')] but the run printed [$(echo "$counts" | tr '\n' ' ')]"
fi
block_total=$(echo "$block" | grep -oE '= [0-9]+ assertions' | grep -oE '[0-9]+')
[ "$block_total" = "$total" ] || complain "README.md 'make test' block total says $block_total assertions, the run printed $total"

# --- the per-binary bullets, where one quotes a count
i=1
for name in $names; do
    c=$(echo "$counts" | sed -n "${i}p"); i=$((i + 1))
    b=$(grep -oE "^\* \`test_$name\.c\` — [0-9]+ assertions" README.md | grep -oE '[0-9]+' | head -1) || true
    [ -z "$b" ] || [ "$b" = "$c" ] || complain "README.md bullet says test_$name.c has $b assertions, the run printed $c"
done

# --- docs/ROADMAP.md's total
rm_total=$(grep -oE '[0-9]+ assertions' ../docs/ROADMAP.md | grep -oE '[0-9]+' | head -1)
[ "$rm_total" = "$total" ] || complain "docs/ROADMAP.md says $rm_total assertions, the run printed $total"

# --- the verify comparison count (static: the Makefile is the source of truth)
vcount=$(grep -c 'python3 tools/compare_shot.py' Makefile)
readme_v=$(grep -oE '[0-9]+ headless renders' README.md | grep -oE '[0-9]+' | head -1)
[ "$readme_v" = "$vcount" ] || complain "README.md says $readme_v headless renders, the Makefile runs $vcount comparisons"
roadmap_v=$(grep -oE '[0-9]+ pixel comparisons' ../docs/ROADMAP.md | grep -oE '[0-9]+' | head -1)
[ "$roadmap_v" = "$vcount" ] || complain "docs/ROADMAP.md says $roadmap_v pixel comparisons, the Makefile runs $vcount"

if [ "$fail" -ne 0 ]; then
    echo "The documentation's assertion counts no longer match the suite." >&2
    echo "Update port/README.md / docs/ROADMAP.md to the numbers above." >&2
    exit 1
fi
echo "counts: $total assertions in 10 binaries, $vcount verify comparisons — the docs agree"
