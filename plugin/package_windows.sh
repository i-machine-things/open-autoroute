#!/bin/sh
# Pack the built plugin as a tarball OpenCPN's Options > Plugins > "Import plugin..." accepts on Windows.
# Layout confirmed against https://github.com/leamas/opencpn/wiki/Tarballs: a top folder holding metadata.xml
# and plugins/<name>.dll. That wiki's own example also bundles the MSVC runtime DLLs (msvcp140.dll,
# vcruntime140.dll) alongside the plugin -- CI links both the CRT and wxWidgets statically instead (/MT and a
# static-triplet wx build), so this plugin has no external runtime DLL dependencies to bundle at all.
# usage: plugin/package_windows.sh path/to/openautoroute_pi.dll [outdir]
set -e
LIB=${1:?usage: package_windows.sh path/to/openautoroute_pi.dll [outdir]}
OUT=${2:-.}
VERSION=${VERSION:-0.0.0-dev}
# x86 (32-bit), not x86_64: OpenCPN's own official Windows builds are 32-bit, and a plugin's ABI must match
# the host app it loads into. See ocpn-plugin.xsd's target-arch enum note (opencpn/OpenCPN#2027) and the
# reference build at https://github.com/nohal/dashboardsk_pi/blob/main/.github/workflows/windows.yml.
TARGET=${TARGET:-msvc}
TARGET_VERSION=${TARGET_VERSION:-143}
label=${VERSION#0.0.0-}; [ "$label" = dev ] || label=v$label
TOP=openautoroute-opencpn-plugin-$label-windows-x86
TMP=$(mktemp -d)
mkdir -p "$TMP/$TOP/plugins"
cp "$LIB" "$TMP/$TOP/plugins/openautoroute_pi.dll"
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
  <description>Plans a route between two points using the open-autoroute engine and adds it to the Route Manager. A planning aid only; not for navigation. UNTESTED on real Windows/OpenCPN -- builds clean in CI but has not been confirmed to load; please report back if you try it.</description>
  <target>$TARGET</target>
  <target-version>$TARGET_VERSION</target-version>
  <target-arch>x86</target-arch>
</plugin>
XML
mkdir -p "$OUT"
tar -C "$TMP" -czf "$OUT/$TOP.tar.gz" "$TOP"
rm -rf "$TMP"
echo "wrote $OUT/$TOP.tar.gz"
