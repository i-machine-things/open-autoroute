#!/bin/sh
# Pack the built plugin as a tarball OpenCPN's Options > Plugins > "Import plugin..." accepts on macOS.
# Layout confirmed against https://github.com/leamas/opencpn/wiki/Tarballs: a top folder holding metadata.xml
# and OpenCPN.app/Contents/PlugIns/<lib>.dylib -- the installer overlays this onto the user's real OpenCPN.app.
# usage: plugin/package_macos.sh path/to/libopenautoroute_pi.dylib [outdir]
set -e
LIB=${1:?usage: package_macos.sh path/to/libopenautoroute_pi.dylib [outdir]}
OUT=${2:-.}
VERSION=${VERSION:-0.0.0-dev}
# GitHub's hosted macos-latest runners are Apple Silicon; darwin-arm64 matches ocpn-plugin.xsd's target enum.
TARGET=${TARGET:-darwin-arm64}
TARGET_VERSION=${TARGET_VERSION:-14}
label=${VERSION#0.0.0-}; [ "$label" = dev ] || label=v$label
TOP=openautoroute-opencpn-plugin-$label-macos-arm64
TMP=$(mktemp -d)
mkdir -p "$TMP/$TOP/OpenCPN.app/Contents/PlugIns"
cp "$LIB" "$TMP/$TOP/OpenCPN.app/Contents/PlugIns/libopenautoroute_pi.dylib"
cat > "$TMP/$TOP/metadata.xml" <<XML
<plugin version="1">
  <name>Auto-route</name>
  <version>$VERSION</version>
  <release>1</release>
  <summary>Plan a safe route between two points from NOAA S-57 charts</summary>
  <api-version>1.20</api-version>  <!-- must match GetAPIVersionMinor() in openautoroute_pi.cpp: the ABI class OpenCPN casts this plugin to -->
  <open-source>yes</open-source>
  <is-imported>yes</is-imported>
  <author>open-autoroute contributors</author>
  <source>https://github.com/i-machine-things/open-autoroute</source>
  <info-url>https://github.com/i-machine-things/open-autoroute</info-url>
  <description>Plans a route between two points using the open-autoroute engine and adds it to the Route Manager. A planning aid only; not for navigation. UNTESTED on real macOS/OpenCPN -- builds clean in CI but has not been confirmed to load; not code-signed or notarized (same as OpenCPN's own installer -- macOS will warn about an unidentified developer, the same override OpenCPN itself already requires). Please report back if you try it.</description>
  <target>$TARGET</target>
  <target-version>$TARGET_VERSION</target-version>
  <target-arch>arm64</target-arch>
</plugin>
XML
mkdir -p "$OUT"
tar -C "$TMP" -czf "$OUT/$TOP.tar.gz" "$TOP"
rm -rf "$TMP"
echo "wrote $OUT/$TOP.tar.gz"
