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
# Detected, not hardcoded -- flatpak-builder always builds for the host's own arch, so a hardcoded
# x86_64 here would silently mislabel the package (wrong metadata.xml target-arch, wrong filename)
# on any other arch (e.g. aarch64) instead of failing. `flatpak info --show-ref` prints
# app/<id>/<arch>/<branch>; cut out the arch field.
ARCH := $(shell flatpak info --show-ref org.opencpn.OpenCPN 2>/dev/null | cut -d/ -f3)
# Must match the running OpenCPN Flatpak's own baked-in target version (see `opencpn-cmd print-abi`
# or `flatpak info --show-runtime org.opencpn.OpenCPN`) -- it has moved before (22.08 -> 24.08 ->
# 25.08), and OpenCPN's own compatibility check only compares the leading digits, but keep this
# accurate rather than relying on that leniency. --show-runtime/--show-sdk print the plain ref
# (org.freedesktop.Platform/x86_64/25.08), not the translatable "Runtime:"/"Sdk:" label `flatpak
# info`'s human-readable output uses, so this doesn't break under a non-English LANG.
TARGET_VERSION := $(shell flatpak info --show-runtime org.opencpn.OpenCPN 2>/dev/null | sed 's#.*/##')
# The Sdk is a separate value from the Runtime above -- they're usually the same number but are not
# guaranteed to be (OpenCPN could in principle be built with a newer Sdk against an older Platform
# runtime). check-sdk compares the manifest's own pinned sdk: line (what flatpak-builder actually
# builds against) with what's really installed, so a stale pin fails loudly instead of silently
# building against the wrong wxWidgets ABI.
SDK_PINNED := $(shell awk -F'//' '/^sdk:/ {print $$2}' $(PLUGIN).yaml)
SDK_INSTALLED := $(shell flatpak info --show-sdk org.opencpn.OpenCPN 2>/dev/null | sed 's#.*/##')

all: package

check-sdk:
	@test -n "$(SDK_INSTALLED)" || { echo "Could not detect org.opencpn.OpenCPN's Sdk -- is it installed? (flatpak info org.opencpn.OpenCPN)" >&2; exit 1; }
	@[ "$(SDK_INSTALLED)" = "$(SDK_PINNED)" ] || { \
		echo "$(PLUGIN).yaml pins Sdk $(SDK_PINNED) but org.opencpn.OpenCPN reports Sdk $(SDK_INSTALLED)." >&2; \
		echo "Update the manifest's sdk: line (see flatpak/README.md) and install org.freedesktop.Sdk//$(SDK_INSTALLED) before building." >&2; \
		exit 1; \
	}

build: check-sdk
	flatpak-builder --force-clean --keep-build-dirs app $(PLUGIN).yaml

package: build
	@test -n "$(ARCH)" || { echo "Could not detect org.opencpn.OpenCPN's arch -- is it installed? (flatpak info org.opencpn.OpenCPN)" >&2; exit 1; }
	@test -n "$(TARGET_VERSION)" || { echo "Could not detect org.opencpn.OpenCPN's runtime version -- is it installed? (flatpak info org.opencpn.OpenCPN)" >&2; exit 1; }
	VERSION=$(VERSION) TARGET=flatpak-$(ARCH) TARGET_VERSION=$(TARGET_VERSION) \
		../plugin/package.sh app/files/lib/opencpn/libopenautoroute_pi.so .

clean:
	rm -rf app .flatpak-builder *.tar.gz

.PHONY: all check-sdk build package clean
