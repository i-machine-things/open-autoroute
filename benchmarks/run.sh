#!/usr/bin/env bash
# Run every route in benchmarks/routes.csv through the router and print one scored table.
#   usage: [OAR_ARGS="--no-marks ..."] benchmarks/run.sh ENC_DIR [BIN=build/openautoroute] [PARALLEL=4] [OUT=benchmarks/out]
# OAR_ARGS adds extra flags to every route, e.g. to switch a feature off and compare with compare.sh.
# Flags in the last column: N no route found, S start/end snapped >1 km to reach safe water, B blocked-water stretches,
# W wrong-way lane travel, P a small craft (under 20 m) passes through a precautionary area, X a lane crossing >30 degrees from square, D route more than 1.6x the straight line.
set -euo pipefail
ENC=${1:?usage: run.sh ENC_DIR [BIN] [PARALLEL] [OUT]}
BIN=${2:-build/openautoroute}
PAR=${3:-4}
OUT=${4:-benchmarks/out}
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT"
rm -f "$OUT"/*.txt "$OUT"/*.gpx "$OUT"/*.ppm "$OUT"/*.ppm.json

run_one() {
  IFS=, read -r name flat flon tlat tlon len cell fromname toname _notes <<<"$1"
  # shellcheck disable=SC2086
  "$BIN" ${OAR_ARGS:-} --enc "$ENC" --from "$flat,$flon" --to "$tlat,$tlon" --length-m "$len" --cell-m "$cell" --summary --name "$fromname to $toname (${len} m)" --start-name "$fromname" --end-name "$toname" --picture "$OUT/$name.ppm" \
      -o "$OUT/$name.gpx" >"$OUT/$name.txt" 2>&1 || true
}
export -f run_one
export ENC BIN OUT OAR_ARGS

grep -v '^#' "$HERE/routes.csv" | grep -v '^$' | xargs -P "$PAR" -I{} bash -c 'run_one "$1"' _ {}

printf '%-34s %5s %7s %6s %4s %6s %6s %6s %6s %5s %4s %4s %6s %s\n' route len_m nm ratio wp clos_m blk_m wrng_m caut_m cross offd snap secs flags
grep -v '^#' "$HERE/routes.csv" | grep -v '^$' | while IFS=, read -r name _ _ _ _ len _ _ _ _; do
  line=$(grep '^SUMMARY' "$OUT/$name.txt" 2>/dev/null || true)
  if [ -z "$line" ]; then printf '%-34s %5s  (no summary: see %s)\n' "$name" "$len" "$OUT/$name.txt"; continue; fi
  awk -v name="$name" -v len="$len" '
    { for (i = 2; i <= NF; i++) { split($i, kv, "="); v[kv[1]] = kv[2] }
      if (v["found"] != 1) { printf "%-34s %5s  NO ROUTE (%s)  N\n", name, len, v["reason"]; exit }
      ratio = v["straight_nm"] > 0 ? v["nm"] / v["straight_nm"] : 0
      snap = v["snap_start_m"] > v["snap_end_m"] ? v["snap_start_m"] : v["snap_end_m"]
      f = ""
      if (snap > 1000) f = f "S"; if (v["blocked_m"] > 0) f = f "B"; if (v["wrong_way_m"] > 0) f = f "W"; if (v["caution_m"] > 0 && len < 20) f = f "P"
      if (v["worst_off_deg"] > 30) f = f "X"; if (ratio > 1.6) f = f "D"
      printf "%-34s %5s %7.1f %6.2f %4d %6d %6d %6d %6d %5d %4d %4d %6.1f %s\n", name, len, v["nm"], ratio, v["waypoints"], \
             v["closest_m"], v["blocked_m"], v["wrong_way_m"], v["caution_m"], v["crossings"], v["worst_off_deg"], snap, v["seconds"], f }' <<<"$line"
done
