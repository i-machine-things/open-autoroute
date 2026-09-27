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
The release has two files with self-explaining names: `openautoroute-cli-vX.Y.Z-linux-x86_64.tar.gz` (the command-line tool) and two plugin packages, `openautoroute-opencpn-plugin-vX.Y.Z-debian13-x86_64.tar.gz` and `...-debian12-x86_64.tar.gz` (Options, Plugins, Import plugin). OpenCPN refuses a plugin package whose target system does not match the computer ("Incompatible import plugin detected"), so the same binary is packaged for each system it is meant for; a Debian 12 package is also accepted on Ubuntu 24.04. All of it is built in a Debian 12 container, with checksums, and notes made from the roadmap milestone (or, for
a patch, the milestone it patches) plus the changes since the previous release. They are built on Debian 12, so they need glibc 2.36 or
newer (the tool links libstdc++ statically; the plugin also needs wxWidgets 3.2). On another system, build from source. The tool reports
its version with `openautoroute --version`.

`scripts/next_version.sh` prints the last release, what the next patch would be and the next milestone. It only prints.

## Rehearsal

In GitHub, Actions, Release, Run workflow, enter a tag name such as `v0.1.0`. It does everything except publish (no tag is needed), and
prints the release notes in the job summary. Rehearse before the first real tag.

## Plugin package

The plugin package is built on Debian 12 with wxWidgets 3.2 against OpenCPN's plugin header for release 5.14 (pinned by commit in the
workflows). OpenCPN matches a package's target system exactly (a Debian package must match the Debian major version; a Debian 12 package is
also accepted on Ubuntu 24.04), so to support another system add another package line in `release.yml`. Otherwise build from source (see
the README).

## Branch protection

`master` is protected. Every change goes in through a pull request, and these checks must pass before GitHub allows the merge (admins
included): lint, build and test on g++ and clang++, tests under the sanitizers, the OpenCPN plugin build, and CodeQL (`Analyze (C++)`).
Force-pushes to `master` and deleting it are blocked. The informational version job is not required (it only runs on master). CodeRabbit is
not required either, because its free review limit can leave a PR without a review. To change any of this: Settings, Branches.

The release workflow re-runs CI on the tagged commit, but not CodeQL, so before tagging check that CodeQL is green on master.
