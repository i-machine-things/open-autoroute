#!/bin/sh
# Pack the built plugin as a tarball OpenCPN's Options > Plugins > "Import plugin..." accepts (a top folder holding metadata.xml and
# usr/lib/opencpn/<lib>.so; OpenCPN installs it under ~/.local). usage: plugin/package.sh path/to/libopenautoroute_pi.so [outdir]
set -e
LIB=${1:?usage: package.sh path/to/libopenautoroute_pi.so [outdir]}
OUT=${2:-.}
VERSION=${VERSION:-0.0.0-dev}
TARGET=${TARGET:-debian-x86_64}
TARGET_VERSION=${TARGET_VERSION:-13}
TOP=openautoroute_pi-${VERSION#0.0.0-}-$TARGET
TMP=$(mktemp -d)
mkdir -p "$TMP/$TOP/usr/lib/opencpn"
cp "$LIB" "$TMP/$TOP/usr/lib/opencpn/libopenautoroute_pi.so"
cat > "$TMP/$TOP/metadata.xml" <<XML
<plugin version="1">
  <name>Auto-route</name>
  <version>$VERSION</version>
  <release>1</release>
  <summary>Plan a safe route between two points from NOAA S-57 charts</summary>
  <api-version>1.18</api-version>
  <open-source>yes</open-source>
  <is-imported>yes</is-imported>
  <author>open-autoroute contributors</author>
  <source>https://github.com/i-machine-things/open-autoroute</source>
  <info-url>https://github.com/i-machine-things/open-autoroute</info-url>
  <description>Plans a route between two points using the open-autoroute engine and adds it to the Route Manager. A planning aid only; not for navigation.</description>
  <target>$TARGET</target>
  <target-version>$TARGET_VERSION</target-version>
  <target-arch>x86_64</target-arch>
</plugin>
XML
mkdir -p "$OUT"
tar -C "$TMP" -czf "$OUT/$TOP.tar.gz" "$TOP"
rm -rf "$TMP"
echo "wrote $OUT/$TOP.tar.gz"
