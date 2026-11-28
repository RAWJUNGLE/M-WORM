#!/bin/sh
# M-WORM: core regressions on the host. Use -ffp-contract=off: without it the
# compiler contracts multiply+add and the PCM-reference check drifts.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT/Work/fm1-base/mworm" || exit 1

TMP="${TMPDIR:-/tmp}/mworm-coretests"
mkdir -p "$TMP" "$TMP/t" "$TMP/fmui"

SRC="worm/worm_params.c worm/worm_midi.c worm/worm_seq.c worm/worm_store.c worm/worm_synth.c worm/worm_live.c worm/worm_lfx.c worm/worm_wt.c worm/worm_wt_data.c worm/worm_wt_main.c div64.c"
pass=0; fail=0; skip=0

for f in tests/core/*.c; do
  n=$(basename "$f" .c)
  srcs="$SRC worm/worm_ui.c"
  grep -q '#include "worm_ui.c"' "$f" && srcs=$(echo "$srcs" | sed 's| worm/worm_ui.c||')
  grep -q '#include "worm_store.c"' "$f" && srcs=$(echo "$srcs" | sed 's| worm/worm_store.c||')
  case "$n" in
    t_euclid|t_pattern_pick|t_store_tail)
      echo "SKIP $n (stale test, not applicable to FM4)"
      skip=$((skip + 1)); continue;;
  esac
  if cc -O2 -std=c99 -w -ffp-contract=off -DPLAT_HOST -DFM1_HOST_TEST \
      -Iworm -Iworm/worm -I. -I../include "$f" $srcs -lm -o "$TMP/$n" 2>"$TMP/$n.build"; then
    if [ "$n" = "t_regress_pcm" ]; then
      "$TMP/$n" "$TMP/regress.pcm" >"$TMP/$n.out" 2>&1
    else
      "$TMP/$n" >"$TMP/$n.out" 2>&1
    fi
    rc=$?
    if [ "$rc" -eq 0 ]; then
      echo "PASS $n"; pass=$((pass + 1))
    else
      echo "FAIL $n"; cat "$TMP/$n.out"; fail=$((fail + 1))
    fi
  else
    echo "BUILD FAIL $n"; head -20 "$TMP/$n.build"; fail=$((fail + 1))
  fi
done

echo "TOTAL pass=$pass fail=$fail skip=$skip"
[ "$fail" -eq 0 ]
