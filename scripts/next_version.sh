#!/usr/bin/env bash
# What the next version would be. It prints the facts and decides nothing: a release, and whether a milestone is met, is the human's call.
# Versions follow ROADMAP.md: a milestone (v0.1.0, v0.2.0, ...) bumps the minor version; every other release bumps the patch number.
#   usage: scripts/next_version.sh [REF=HEAD]
set -euo pipefail
ref=${1:-HEAD}
strict='^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$'
# The highest release by version order among the tags reachable from REF.
last=$(git tag --merged "$ref" -l 'v[0-9]*' | grep -E "$strict" | sort -V | tail -1 || true)
first=$(grep -oE '^### `v[0-9]+\.[0-9]+\.0\+?`' ROADMAP.md | grep -oE 'v[0-9]+\.[0-9]+\.0' | head -1 || true)
if [ -z "$last" ]; then
  echo "No release yet. The first release is ${first:-v0.1.0} (the first roadmap milestone), when it is met."
  echo "Until then dev builds and packages are unversioned."
else
  [[ "$last" =~ $strict ]]
  major=${BASH_REMATCH[1]}; minor=${BASH_REMATCH[2]}; patch=${BASH_REMATCH[3]}
  echo "Last release: $last  ($(git rev-list --no-merges --count "$last..$ref") commits since)"
  echo "A patch release would be: v$major.$minor.$((patch + 1))"
  if grep -qE "^### \`v$major\.$((minor + 1))\.0\+?\`" ROADMAP.md; then
    echo "The next milestone is v$major.$((minor + 1)).0: tag it only when that milestone is met."
  elif [ "$major" -eq 0 ] && [ "$minor" -ge 5 ]; then
    echo "The roadmap's last entry, v0.6.0+, is open-ended: v$major.$((minor + 1)).0 would be the next milestone once it is added to ROADMAP.md."
  else
    echo "There is no later milestone in ROADMAP.md."
  fi
fi
echo "Only the human decides that a milestone is met; everything else is a patch. Tagging needs an explicit go/no-go."
