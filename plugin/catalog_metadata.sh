#!/bin/sh
# Emit one OpenCPN plugin-catalog metadata XML for a built release package (the file a PR to
# https://github.com/OpenCPN/plugins adds under metadata/, not the metadata.xml bundled inside the tarball
# by package.sh for "Import plugin..." — the catalog file needs a public tarball-url and validates against
# https://github.com/OpenCPN/plugins/blob/master/ocpn-plugin.xsd, which package.sh's local file does not aim for.
# usage: plugin/catalog_metadata.sh path/to/package.tar.gz https://.../package.tar.gz [outdir]
set -e
TARBALL=${1:?usage: catalog_metadata.sh path/to/package.tar.gz tarball-url [outdir]}
URL=${2:?usage: catalog_metadata.sh path/to/package.tar.gz tarball-url [outdir]}
OUT=${3:-.}
VERSION=${VERSION:?set VERSION=X.Y.Z}
TARGET=${TARGET:-debian-x86_64}
TARGET_VERSION=${TARGET_VERSION:-13}
TARGET_ARCH=${TARGET_ARCH:-x86_64}
# Not `sha256sum ... | cut ...`: this is /bin/sh (no pipefail), so a failed sha256sum would leave
# CHECKSUM empty and cut would still exit 0, letting set -e miss it and writing XML with an empty
# tarball-checksum. Capturing sha256sum's own output and exit status directly catches that.
SHA_OUTPUT=$(sha256sum "$TARBALL") || { echo "sha256sum failed for $TARBALL" >&2; exit 1; }
CHECKSUM=${SHA_OUTPUT%% *}
OUTFILE="$OUT/openautoroute_pi-$VERSION-$TARGET-$TARGET_VERSION-$TARGET_ARCH.xml"
mkdir -p "$OUT"
# Field order matches ocpn-plugin.xsd exactly (it is a strict xs:sequence): name, version, release, summary,
# api-version, open-source, author, source, description, target, target-version, target-arch, tarball-url,
# tarball-checksum, info-url. api-version must match GetAPIVersionMinor() in openautoroute_pi.cpp.
cat > "$OUTFILE" <<XML
<plugin version="1">
  <name>Auto-route</name>
  <version>$VERSION</version>
  <release>1</release>
  <summary>Plan a safe route between two points from NOAA S-57 charts</summary>
  <api-version>1.20</api-version>
  <open-source>yes</open-source>
  <author>open-autoroute contributors</author>
  <source>https://github.com/i-machine-things/open-autoroute</source>
  <description>Plans a route between two points using the open-autoroute engine and adds it to the Route Manager. A planning aid only; not for navigation.</description>
  <target>$TARGET</target>
  <target-version>$TARGET_VERSION</target-version>
  <target-arch>$TARGET_ARCH</target-arch>
  <tarball-url>$URL</tarball-url>
  <tarball-checksum>$CHECKSUM</tarball-checksum>
  <info-url>https://github.com/i-machine-things/open-autoroute</info-url>
</plugin>
XML
echo "wrote $OUTFILE"
