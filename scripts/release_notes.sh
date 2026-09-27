#!/usr/bin/env bash
# Release notes for a tag. A milestone release (vX.Y.0) shows its ROADMAP.md milestone; a patch (vX.Y.Z, Z > 0) says which milestone it
# patches. Both then list the changes since the previous v* tag, grouped by commit type.
#   usage: scripts/release_notes.sh vX.Y.Z [REF]     (REF defaults to the tag itself; pass HEAD to rehearse before the tag exists)
set -euo pipefail
tag=${1:?usage: release_notes.sh vX.Y.Z [REF]}
ref=${2:-$tag}
[[ "$tag" =~ ^v([0-9]+)\.([0-9]+)\.([0-9]+)$ ]] || { echo "not a vX.Y.Z tag: $tag" >&2; exit 2; }
major=${BASH_REMATCH[1]}; minor=${BASH_REMATCH[2]}; patch=${BASH_REMATCH[3]}
# ROADMAP.md lists one milestone per minor version, and a final "v0.6.0+" entry for everything after.
if [ "$major" -eq 0 ] && [ "$minor" -ge 6 ]; then key='v0.6.0+'; else key="v$major.$minor.0"; fi
prev=$(git describe --tags --match 'v[0-9]*' --abbrev=0 "$ref^" 2>/dev/null || git rev-list --max-parents=0 "$ref" | tail -1)

echo "# open-autoroute $tag"
echo
echo "> A planning aid only. Not for navigation. Use entirely at your own risk: check every route against up-to-date official charts and notices to mariners. Not part of OpenCPN and not supported by its developers."
echo
if [ "$patch" -gt 0 ]; then
  echo "A patch release for roadmap milestone \`$key\`: fixes and improvements since the previous release."
else
  echo "## Roadmap milestone \`$key\`"
  echo
  awk -v key="### \`$key\`" '
    index($0, key) == 1 { on = 1; next }
    on && (/^### / || /^---/) { exit }
    on { print }' ROADMAP.md | sed '/./,$!d'
fi
echo
echo "## Changes since ${prev:0:12}"
subjects=$(git log --no-merges --format='%s (%h)' "$prev..$ref")
section() { # title, regex
  local lines; lines=$(grep -E "$2" <<<"$subjects" || true)
  if [ -n "$lines" ]; then echo; echo "### $1"; echo; while IFS= read -r l; do echo "- $l"; done <<<"$lines"; fi
}
section "Features" '^feat(\([^)]*\))?!?:'
section "Fixes" '^fix(\([^)]*\))?!?:'
other=$(grep -vE '^(feat|fix)(\([^)]*\))?!?:' <<<"$subjects" || true)
if [ -n "$other" ]; then echo; echo "### Other"; echo; while IFS= read -r l; do echo "- $l"; done <<<"$other"; fi
