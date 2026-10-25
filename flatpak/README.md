openautoroute flatpak build
----------------------------

OpenCPN running as a Flatpak sandboxes itself from the host system and rejects every package
`plugin/package.sh` builds for other platforms ("Incompatible import plugin detected" -- see #54),
because its Flatpak build identifies itself with its own compatibility target (`flatpak-x86_64` as
of OpenCPN 5.14.2, baked in at OpenCPN's own build time -- see `cmake/TargetSetup.cmake` in the
OpenCPN source, or run `opencpn-cmd print-abi` against your own install). This builds a normal
plugin tarball for that target instead of using the Flatpak extension mechanism: `plugin/package.sh`
already understands an arbitrary `TARGET`, so there is no separate packaging format here -- see
`flatpak/Makefile`.

Status: builds and imports successfully via OpenCPN's own Options > Plugins > "Import plugin..."
button against a real Flatpak OpenCPN 5.14.2 (confirmed: plugin appears in the plugin list after
import). Now built by `release.yml`'s `build_flatpak` job and published as a release asset; that
CI-built package itself hasn't yet been re-confirmed by import (only the manually-built one was).

An earlier version of this build shipped the plugin as a Flatpak extension of `org.opencpn.OpenCPN`
(`flatpak install` + merge-dirs into the sandbox). That approach is abandoned: OpenCPN's own
`merge-dirs` wiring for the extension point never actually linked the installed extension into
`/app/lib/opencpn` in testing, and it required a separate `flatpak install`/Discover step the normal
plugin-import flow doesn't need. See `.claude/CODING_NOTES.md` for both findings.

To build:

  - Install flatpak and flatpak-builder.
  - Install the `org.opencpn.OpenCPN` Flatpak and a matching `org.freedesktop.Sdk` (the
    manifest's `sdk:` line names the version to install, e.g. `flatpak install flathub
    org.freedesktop.Sdk//25.08`; recheck this against `flatpak info org.opencpn.OpenCPN`'s Sdk line
    before each use -- it has moved before).
  - From the repository root: `make flatpak` (or `make -C flatpak`). This writes
    `flatpak/openautoroute-opencpn-plugin-<version>-flatpak<runtime-version>-x86_64.tar.gz`.

To install: open OpenCPN, Options > Plugins > "Import plugin...", and select that tarball, the same
way as any other platform's package. No `flatpak install`, Discover, or other package manager step
is needed -- the plugin lands in OpenCPN's own per-user Flatpak data directory
(`~/.var/app/org.opencpn.OpenCPN/lib/opencpn`), which its Flatpak build already searches.

Rebuilding after a source change: commit it first (the manifest's `git` source clones `HEAD`, so
uncommitted edits are silently not picked up), then `make -C flatpak clean package`.
