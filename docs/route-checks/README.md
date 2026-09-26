# Route checks

A route that scores well on the numbers can still be wrong on the chart, and looking at it in OpenCPN is the check that has found the most problems. Each dated folder holds the GPX files for one round of checking, a `CHECKLIST.md` saying what to look at and what to expect, and the build they came from. Fill in the tick boxes and add notes as you go; record findings in the PR that adds the next round.

To regenerate a round: build the tool, then run `benchmarks/run.sh ENC_DIR build/openautoroute 8 OUT_DIR` and copy the GPX files from `OUT_DIR`.
