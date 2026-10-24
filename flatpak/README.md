openautoroute flatpak build
----------------------------

OpenCPN running as a Flatpak sandboxes itself from the host system and rejects every package
`plugin/package.sh` builds ("Incompatible import plugin detected" -- see #54), because its
Flatpak build identifies as target `flatpak`, not `debian-x86_64`/etc. This builds and installs
the plugin as a Flatpak extension of `org.opencpn.OpenCPN` instead.

Status: builds and installs locally against a real Flatpak OpenCPN; not yet wired into CI or
the release pipeline (see docs, `make flatpak` has no coverage there yet), and the
`org.freedesktop.Sdk` version pinned in the manifest needs rechecking against whatever
`flatpak info org.opencpn.OpenCPN` reports before each use -- it has moved before.

To build and install:

  - Install flatpak and flatpak-builder.
  - Install the `org.opencpn.OpenCPN` Flatpak and a matching `org.freedesktop.Sdk` (the
    manifest's `sdk:` line names the version to install, e.g. `flatpak install flathub
    org.freedesktop.Sdk//25.08`).
  - From the repository root: `make flatpak` (or `make -C flatpak`), then:

      cd flatpak
      make install     # exports and installs into your user Flatpak OpenCPN
      make uninstall    # removes it again

Rebuilding after a source change: commit it first (the manifest's `git` source clones `HEAD`,
so uncommitted edits are silently not picked up), then `make -C flatpak clean build install`.
