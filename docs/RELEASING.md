# Releasing

open-autoroute has its own versioning rule (it replaces the template's commit-count rule; see `.claude/CLAUDE.md`, Rule 4):

- **A roadmap milestone met** bumps the minor version: `v0.1.0`, `v0.2.0`, `v0.3.0`, ... one per milestone in [ROADMAP.md](../ROADMAP.md).
  Whether a milestone is met is a human decision, not a commit count.
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

1. Master is green (CI and CodeQL), and the benchmark comparison has been checked for anything that got worse.
2. Give the human the plain-language summary and the management checklist, and wait for an explicit go (Rule 6).
3. From master: `git tag vX.Y.Z && git push origin vX.Y.Z`.

The workflow refuses the tag unless it is a milestone in `ROADMAP.md` (`vX.Y.0`, and the previous milestone is already released) or the
next patch of a milestone that is already released, and its commit is on master. Versions below 1.0 are published as pre-releases.
The release has the command-line tool (Linux x86_64) and the OpenCPN plugin package, with checksums, and notes made from the roadmap
milestone (or, for a patch, the milestone it patches) plus the changes since the previous tag.

`scripts/next_version.sh` prints the last release, what the next patch would be and the next milestone. It only prints.

## Rehearsal

In GitHub, Actions, Release, Run workflow, enter a tag name such as `v0.1.0`. It does everything except publish (no tag is needed), and
prints the release notes in the job summary. Rehearse before the first real tag.

## Plugin package

The plugin package is built on Ubuntu 24.04 with wxWidgets 3.2 against OpenCPN's plugin header for release 5.14. On another system,
build it from source (see the README).
