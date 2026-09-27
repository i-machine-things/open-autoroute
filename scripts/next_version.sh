#!/usr/bin/env bash
# What the next version would be. It prints the facts and decides nothing: a release, and whether a milestone is met, is the human's call.
# Versions follow ROADMAP.md: a milestone (v0.1.0, v0.2.0, ...) bumps the minor version; every other release bumps the patch number.
#   usage: scripts/next_version.sh [REF=HEAD]
set -euo pipefail
ref=${1:-HEAD}
last=$(git describe --tags --match 'v[0-9]*' --abbrev=0 "$ref" 2>/dev/null || true)
milestones=$(grep -oE '^### `v[0-9]+\.[0-9]+\.0\+?`' ROADMAP.md | grep -oE 'v[0-9]+\.[0-9]+\.0' )
if [ -z "$last" ]; then
  echo "No release yet. The first release is $(head -1 <<<"$milestones") (the first roadmap milestone), when it is met."
  echo "Until then dev builds and packages are unversioned."
else
  [[ "$last" =~ ^v([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]
  major=${BASH_REMATCH[1]}; minor=${BASH_REMATCH[2]}; patch=${BASH_REMATCH[3]}
  echo "Last release: $last  ($(git rev-list --no-merges --count "$last..$ref") commits since)"
  echo "A patch release would be: v$major.$minor.$((patch + 1))"
  next=$(grep -A1 -x "v$major.$minor.0" <<<"$milestones" | tail -1)
  if [ -n "$next" ] && [ "$next" != "v$major.$minor.0" ]; then echo "The next milestone is $next: tag it only when that milestone is met."
  else echo "There is no later milestone in ROADMAP.md."; fi
fi
echo "Only the human decides that a milestone is met; everything else is a patch. Tagging needs an explicit go/no-go."
