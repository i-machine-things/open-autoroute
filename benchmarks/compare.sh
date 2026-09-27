#!/usr/bin/env bash
# Compare two benchmark output directories route by route and flag regressions.
#   usage: benchmarks/compare.sh OLD_DIR NEW_DIR
# Prints one line per route whose SUMMARY differs. "REGRESSION" is printed when the new run is worse on a safety or traffic
# separation measure: a route lost, blocked water, wrong-way lane travel, more time in precautionary areas (small craft), more
# lane crossings or a worse crossing angle. Exits 1 if any regression was found.
set -euo pipefail
shopt -s nullglob   # an empty directory must give no files, not a literal "*.txt"
OLD=${1:?usage: compare.sh OLD_DIR NEW_DIR}
NEW=${2:?usage: compare.sh OLD_DIR NEW_DIR}
bad=0; same=0; changed=0
for f in "$OLD"/*.txt; do
  name=$(basename "$f" .txt)
  o=$(grep '^SUMMARY' "$f" || true); n=$(grep '^SUMMARY' "$NEW/$name.txt" 2>/dev/null || true)
  if [ -z "$n" ]; then echo "MISSING   $name (no result in $NEW)"; bad=1; continue; fi
  if [ "$o" = "$n" ]; then same=$((same+1)); continue; fi
  # seconds differ run to run; ignore it when deciding whether anything changed
  o2=$(sed 's/ seconds=[0-9.]*//' <<<"$o"); n2=$(sed 's/ seconds=[0-9.]*//' <<<"$n")
  if [ "$o2" = "$n2" ]; then same=$((same+1)); continue; fi
  changed=$((changed+1))
  verdict=$(awk -v o="$o" -v n="$n" 'BEGIN {
      split(o, oa, " "); split(n, na, " ");
      for (i = 2; i in oa; i++) { split(oa[i], kv, "="); ov[kv[1]] = kv[2] }
      for (i = 2; i in na; i++) { split(na[i], kv, "="); nv[kv[1]] = kv[2] }
      r = ""
      if (ov["found"] == 1 && nv["found"] != 1) r = r " route-lost"
      if (nv["blocked_m"] + 0 > ov["blocked_m"] + 0) r = r " blocked-water"
      if (nv["wrong_way_m"] + 0 > ov["wrong_way_m"] + 0) r = r " wrong-way"
      if (nv["vessel_m"] + 0 < 20 && nv["caution_m"] + 0 > ov["caution_m"] + 0) r = r " more-caution-area"   # ships are meant to use precautionary areas
      if (nv["gates_missed"] + 0 > ov["gates_missed"] + 0) r = r " more-buoy-gates-missed"
      if (nv["worst_off_deg"] + 0 > ov["worst_off_deg"] + 5) r = r " worse-crossing-angle"
      if (nv["crossings"] + 0 > ov["crossings"] + 0) r = r " more-crossings"
      print r }')
  if [ -n "$verdict" ]; then echo "REGRESSION $name:$verdict"; bad=1; else echo "changed    $name"; fi
  printf '    old: %s\n    new: %s\n' "$(sed 's/SUMMARY //; s/charts=[0-9]* //; s/snap_[a-z_]*=[0-9]* //g' <<<"$o")" "$(sed 's/SUMMARY //; s/charts=[0-9]* //; s/snap_[a-z_]*=[0-9]* //g' <<<"$n")"
done
# A route that only the new run has (a route added to the benchmark) is reported too, not silently skipped.
for f in "$NEW"/*.txt; do
  name=$(basename "$f" .txt)
  if [ ! -e "$OLD/$name.txt" ]; then echo "NEW       $name (no result in $OLD)"; fi
done
echo "identical: $same, changed: $changed"
exit $bad
