#!/usr/bin/env bash
# Release notes for a tag. A milestone release (vX.Y.0) shows its ROADMAP.md milestone; a patch (vX.Y.Z, Z > 0) says which milestone it
# patches. Both then list the changes since the previous v* tag, grouped by commit type.
#   usage: scripts/release_notes.sh vX.Y.Z [REF]     (REF defaults to the tag itself; pass HEAD to rehearse before the tag exists)
set -euo pipefail
tag=${1:?usage: release_notes.sh vX.Y.Z [REF]}
ref=${2:-$tag}
[[ "$tag" =~ ^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]] || { echo "not a vX.Y.Z tag: $tag" >&2; exit 2; }
major=${BASH_REMATCH[1]}; minor=${BASH_REMATCH[2]}; patch=${BASH_REMATCH[3]}
# ROADMAP.md has one scoped milestone heading per minor version.
key="v$major.$minor.0"
# The previous release is the next-lower version by version order (not the nearest tag by commit distance, which is wrong for a patch
# tagged out of order). The tag is put in the list even when it does not exist yet (a rehearsal), so it lands at its sorted position.
strict='^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$'
prev=$( { git tag -l 'v[0-9]*' | grep -E "$strict" | grep -vx "$tag" || true; printf '%s\n' "$tag"; } | sort -V | awk -v t="$tag" '$0 == t { print p; exit } { p = $0 }' )

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
    on && (/^#+ / || /^---/) { exit }
    on { print }' ROADMAP.md | sed '/./,$!d'
fi
echo
echo "## Which file do I want?"
echo
echo "- **openautoroute-opencpn-plugin-$tag-debian13-x86_64.tar.gz**: the OpenCPN plugin for **Debian 13** and systems based on it (for example PeppermintOS). In OpenCPN: Options, Plugins, Import plugin, and pick this file."
echo "- **openautoroute-opencpn-plugin-$tag-debian12-x86_64.tar.gz**: the same plugin for **Debian 12**, and for **Ubuntu 24.04** (OpenCPN accepts a Debian 12 package there)."
echo "- OpenCPN refuses a plugin package built for another system with \"Incompatible import plugin detected\". If you get that, you picked the wrong one, or your system is not listed: build the plugin from source (see the README)."
echo "- **openautoroute-cli-$tag-linux-x86_64.tar.gz**: the command-line tool, for scripts and for checking routes without OpenCPN. You do not need it for the plugin. Needs glibc 2.36 or newer."
echo "- **SHA256SUMS**: checksums for all the files (\`sha256sum -c SHA256SUMS\`)."
echo
if [ -n "$prev" ]; then
  echo "## Changes since $prev"
  subjects=$(git log --no-merges --format='%s (%h)' "$prev..$ref")
else
  echo "## Changes (this is the first release)"
  subjects=$(git log --no-merges --format='%s (%h)' "$ref")   # the whole history, root commit included
fi
section() { # title, regex
  local lines; lines=$(grep -E "$2" <<<"$subjects" || true)
  if [ -n "$lines" ]; then echo; echo "### $1"; echo; while IFS= read -r l; do echo "- $l"; done <<<"$lines"; fi
}
section "Features" '^feat(\([^)]*\))?!?:'
section "Fixes" '^fix(\([^)]*\))?!?:'
other=$(grep -vE '^(feat|fix)(\([^)]*\))?!?:' <<<"$subjects" || true)
if [ -n "$other" ]; then echo; echo "### Other"; echo; while IFS= read -r l; do echo "- $l"; done <<<"$other"; fi
