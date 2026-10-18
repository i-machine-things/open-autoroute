#!/bin/sh
# Pack the built plugin as a tarball OpenCPN's Options > Plugins > "Import plugin..." accepts on macOS.
# Layout confirmed against https://github.com/leamas/opencpn/wiki/Tarballs: a top folder holding metadata.xml
# and OpenCPN.app/Contents/PlugIns/<lib>.dylib -- the installer overlays this onto the user's real OpenCPN.app.
# usage: plugin/package_macos.sh path/to/libopenautoroute_pi.dylib [outdir]
set -e
LIB=${1:?usage: package_macos.sh path/to/libopenautoroute_pi.dylib [outdir]}
OUT=${2:-.}
VERSION=${VERSION:-0.0.0-dev}
# OpenCPN's macOS wxWidgets 3.2 ABI is darwin-wx32; CPU architecture is declared separately below.
TARGET=${TARGET:-darwin-wx32}
TARGET_VERSION=${TARGET_VERSION:-14}
label=${VERSION#0.0.0-}; [ "$label" = dev ] || label=v$label
TOP=openautoroute-opencpn-plugin-$label-macos-arm64
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' 0
mkdir -p "$TMP/$TOP/OpenCPN.app/Contents/PlugIns"
cp "$LIB" "$TMP/$TOP/OpenCPN.app/Contents/PlugIns/libopenautoroute_pi.dylib"
PACKAGED_LIB="$TMP/$TOP/OpenCPN.app/Contents/PlugIns/libopenautoroute_pi.dylib"
LINKED_LIBS=$(otool -L "$PACKAGED_LIB")
printf '%s\n' "$LINKED_LIBS" | awk '$1 ~ /\/libwx_.*\.dylib$/ { print $1 }' |
while IFS= read -r dependency; do
  case "$dependency" in
    *-3.2.dylib) ;;
    *) echo "Expected wxWidgets 3.2, found $dependency" >&2; exit 1 ;;
  esac
  install_name_tool -change "$dependency" "@executable_path/../Frameworks/${dependency##*/}" "$PACKAGED_LIB"
done
codesign --force --sign - "$PACKAGED_LIB"
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
  <description>Plans a route between two points using the open-autoroute engine and adds it to the Route Manager. A planning aid only; not for navigation. A corrected local ARM build was confirmed to import in OpenCPN 5.14.0 on 2026-10-02; this release artifact still needs verification. Ad-hoc signed, not Developer ID signed or notarized. Please report back if you try it.</description>
  <target>$TARGET</target>
  <target-version>$TARGET_VERSION</target-version>
  <target-arch>arm64</target-arch>
</plugin>
XML
mkdir -p "$OUT"
tar -C "$TMP" -czf "$OUT/$TOP.tar.gz" "$TOP"
echo "wrote $OUT/$TOP.tar.gz"
