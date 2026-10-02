# Releasing

open-autoroute has its own versioning rule (it replaces the template's commit-count rule; see `.claude/CLAUDE.md`, Rule 4):

- **A roadmap milestone met** bumps the minor version: `v0.1.0`, `v0.2.0`, `v0.3.0`, ... one per scoped milestone heading in
  [ROADMAP.md](../ROADMAP.md). Whether a milestone is met is a human decision, not a commit count. Work not yet scoped lives in the
  roadmap's backlog with no version number; it gets a numbered milestone heading (with a "Done when") when it is scoped, and only then can
  it be released as a milestone.
- **Everything else is a patch** of the current milestone, counted up one at a time: `v0.1.0`, then `v0.1.1`, then `v0.1.2`.
- Before `v0.1.0` there are no version numbers: dev builds and packages are unversioned.

## What runs

| Workflow | When | What |
|---|---|---|
| `ci.yml` | every PR and every push to master | lint (warnings as errors on gcc and clang, shellcheck, workflow YAML), tests on gcc and clang, tests under the sanitizers, the OpenCPN plugin build, and (on master) what the next version would be |
| `codeql.yml` | every PR, push to master, and weekly | GitHub's CodeQL security scan of the C++ code |
| `release.yml` | a `vX.Y.Z` tag is pushed, or by hand as a rehearsal | validates the tag, runs `ci.yml` on that commit, builds the tool and the plugin package, publishes the release |

The 29-route benchmark needs the full NOAA chart set (over 2 GB), so it is not in CI. For any routing change run `benchmarks/run.sh` and
`benchmarks/compare.sh` by hand and read the result.

## Cutting a release

1. Master is green (CI and CodeQL: the release workflow re-runs CI on the tagged commit but does not check CodeQL), and the benchmark comparison has been checked for anything that got worse. Never tag from a branch: only from master.
2. Give the human the plain-language summary and the management checklist, and wait for an explicit go (Rule 6).
3. From master: `git tag vX.Y.Z && git push origin vX.Y.Z`.

The workflow refuses the tag unless it is a milestone in `ROADMAP.md` (`vX.Y.0`, and the previous milestone is already released) or the
next patch of a milestone that is already released and still current (once `vX.(Y+1).0` is out, `vX.Y` is closed: no backports), and its
commit is on master. Leading zeros (`v0.1.01`) are refused. Versions below 1.0 are published as pre-releases.
The release has the command-line tool (`openautoroute-cli-vX.Y.Z-linux-x86_64.tar.gz`) and plugin packages for Linux, Windows and macOS (Options, Plugins, Import plugin). OpenCPN refuses a plugin package whose target system does not match the computer ("Incompatible import plugin detected"), so the same binary is packaged for each system it is meant for; a Debian 12 package is also accepted on Ubuntu 24.04. **Windows remains unverified; macOS has a confirmed local import fix**, but the corrected release artifact still needs testing — see "Plugin package" below. All of it is built with checksums, and notes made from the roadmap milestone (or, for
a patch, the milestone it patches) plus the changes since the previous release. They are built on Debian 12, so they need glibc 2.36 or
newer (the tool links libstdc++ statically; the plugin also needs wxWidgets 3.2). On another system, build from source. The tool reports
its version with `openautoroute --version`.

`scripts/next_version.sh` prints the last release, what the next patch would be and the next milestone. It only prints.

## Rehearsal

In GitHub, Actions, Release, Run workflow, enter a tag name such as `v0.1.0`. It does everything except publish (no tag is needed), and
prints the release notes in the job summary. Rehearse before the first real tag.

## Plugin package

The plugin package is built with wxWidgets 3.2 against OpenCPN's plugin header for release 5.14 (pinned by commit in the workflows), once
per target system, in `release.yml`'s `build`/`build_windows`/`build_macos` jobs:

- **Linux** (`build`): Debian 12 container, wxWidgets from apt. OpenCPN matches a package's target system exactly (a Debian package must
  match the Debian major version; a Debian 12 package is also accepted on Ubuntu 24.04). Verified: the maintainer has imported this one
  into a real OpenCPN 5.14 install and confirmed it loads and plans a route.
- **Windows** (`build_windows`): `windows-latest`, 32-bit (`win32`/`x86`) — OpenCPN's own official Windows builds are 32-bit, and a
  plugin's ABI must match the host app. wxWidgets is linked via `vcpkg`'s `x86-windows-static` triplet and the MSVC CRT is linked
  statically (`CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`) — intended to leave the package with no external runtime DLLs to bundle, though
  that's an inference from the build flags, not something confirmed by running on a real machine. The MSVC toolset is deliberately **not**
  pinned: an earlier attempt pinned `-T v143`, copied from a working reference
  ([nohal/dashboardsk_pi](https://github.com/nohal/dashboardsk_pi)'s `windows.yml`) to guard against a plugin built with a newer toolset
  crashing against OpenCPN's own bundled runtime ([OpenCPN/OpenCPN#5399](https://github.com/OpenCPN/OpenCPN/issues/5399)) — but that
  reference links wx dynamically, where the risk applies; forcing the pin here instead caused real link errors (it put our object files on
  a different MSVC sub-toolset than vcpkg used to build wx, unpinned).
  **Unverified**: builds clean, never imported into a real Windows OpenCPN.
- **macOS** (`build_macos`): `macos-latest` (Apple Silicon), explicitly using Homebrew's `wxwidgets@3.2`
  and `wx-config-3.2`. Metadata uses `darwin-wx32` with `target-arch=arm64`. Packaging rejects other
  wxWidgets versions, rewrites wxWidgets references to OpenCPN's bundled frameworks, and renews the
  dylib's ad-hoc signature; no Homebrew runtime is required. It is not Developer ID signed or notarized.
  A corrected local ARM build was confirmed to import in OpenCPN 5.14.0 on 2026-10-02.
  The released v0.1.4 package fails import; the corrected release artifact remains unverified.

To support another system, add another package line (Linux) or another job (Windows/macOS) in `release.yml`. Otherwise build from source
(see the README).

## OpenCPN plugin catalog

Every release generates one schema-valid catalog metadata XML per packaged target (`plugin/catalog_metadata.sh`, run
once per target in `release.yml`'s Package step), validated against the live `ocpn-plugin.xsd` from
[OpenCPN/plugins](https://github.com/OpenCPN/plugins) and shipped as a release artifact alongside the plugin
tarballs. The Windows and macOS metadata say so in their own `description` field until someone confirms those
release builds load in a real OpenCPN install — only the Debian ones are confirmed as of `v0.1.4`.
The macOS local-build confirmation above does not yet verify a corrected release artifact.

**This generation step does not update the live catalog by itself.** There is no tracking, no webhook, no
subscription to this repo's releases — `OpenCPN/plugins`' own README is explicit that both new plugins *and
updates* only land via a pull request against it that touches files under its `metadata/` directory. The initial
submission (targeting `Alpha`, for experimental plugins) is
[OpenCPN/plugins#1420](https://github.com/OpenCPN/plugins/pull/1420). There's a semi-automated path some plugins
use ("frontend2": CI uploads metadata to Cloudsmith, something downstream opens the PR) but it's built around
Cloudsmith as the distribution backend; this project ships plain GitHub Releases, so it doesn't apply here.

**To update the catalog listing after a new release:**
1. Download that release's `openautoroute_pi-*.xml` files from its GitHub Releases page.
2. `i-machine-things/plugins` (forked from `OpenCPN/plugins`) already exists — fetch it, branch off `Alpha`.
3. Add the new XML files to `metadata/` (same filenames as last time, now with the new version baked in; old
   versions' files can be left in place or removed — check what other plugins in that directory actually do).
4. Push the branch, open a PR from `i-machine-things/plugins:<branch>` to `OpenCPN/plugins:Alpha`.

No icon file is needed despite what `OpenCPN/plugins`' own wiki docs suggest — a repo-wide search turned up zero
`.svg`/`.png` files anywhere in it, across what must be hundreds of plugin entries; empirical practice doesn't
match that doc. `plugin/catalog-icon.svg` still lives in this repo regardless (useful on its own merits), it's
just not part of what actually gets submitted upstream.

## Branch protection

`master` is protected. Every change goes in through a pull request, and these checks must pass before GitHub allows the merge (admins
included): lint, build and test on g++ and clang++, tests under the sanitizers, the OpenCPN plugin build, and CodeQL (`Analyze (C++)`).
Force-pushes to `master` and deleting it are blocked. The informational version job is not required (it only runs on master). CodeRabbit is
not required either, because its free review limit can leave a PR without a review. To change any of this: Settings, Branches.

The release workflow re-runs CI on the tagged commit, but not CodeQL, so before tagging check that CodeQL is green on master.
