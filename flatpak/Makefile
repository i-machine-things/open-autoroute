# Package the plugin for Flatpak OpenCPN as a plain, GUI-importable plugin tarball (see #54).
#
# OpenCPN's Flatpak build has its own native plugin target ("flatpak-<arch>", baked in at its own
# build time -- see cmake/TargetSetup.cmake in the OpenCPN source, and `opencpn-cmd print-abi` on a
# running install) that its normal Options > Plugins > "Import plugin..." button already accepts,
# the same way it accepts a debian-x86_64 tarball on a native install. No Flatpak extension, no
# `flatpak install`, no Discover -- just a tarball built the way plugin/package.sh already builds
# every other target, with TARGET=flatpak-<arch>. flatpak-builder is used here only to get a build
# sandboxed against OpenCPN's own bundled wxWidgets (its ABI, not the host's); nothing from that
# sandboxed build is installed or exported as a Flatpak ref.
PLUGIN = org.opencpn.OpenCPN.Plugin.openautoroute
VERSION ?= 0.0.0-dev
ARCH := x86_64
# Must match the running OpenCPN Flatpak's own baked-in target version (see `opencpn-cmd print-abi`
# or `flatpak info org.opencpn.OpenCPN`'s Runtime line) -- it has moved before (22.08 -> 24.08 ->
# 25.08), and OpenCPN's own compatibility check only compares the leading digits, but keep this
# accurate rather than relying on that leniency.
TARGET_VERSION := $(shell flatpak info org.opencpn.OpenCPN 2>/dev/null | awk '/^ *Runtime:/ {print $$2}' | sed 's#.*/##')

all: package

build:
	flatpak-builder --force-clean --keep-build-dirs app $(PLUGIN).yaml

package: build
	@test -n "$(TARGET_VERSION)" || { echo "Could not detect org.opencpn.OpenCPN's runtime version -- is it installed? (flatpak info org.opencpn.OpenCPN)" >&2; exit 1; }
	VERSION=$(VERSION) TARGET=flatpak-$(ARCH) TARGET_VERSION=$(TARGET_VERSION) \
		../plugin/package.sh app/files/lib/opencpn/libopenautoroute_pi.so .

clean:
	rm -rf app .flatpak-builder *.tar.gz

.PHONY: all build package clean
