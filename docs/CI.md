# Continuous integration and quality gates

This document describes the automated gates required by Phase 5.2 of
[MILESTONE_PLAN.md](../MILESTONE_PLAN.md), how to reproduce each one locally, and
what a passing run does and does not prove.

## Gates

| Gate | Workflow job | Local command | Proves |
|---|---|---|---|
| Documentation links | `docs` | `tools/ci_gates.sh docs` | Every internal Markdown link in active documentation resolves |
| macOS Debug | `macos-debug` | `tools/ci_gates.sh debug` | Clean-clone configure/build with first-party warnings as errors, plus the full CTest suite |
| Sanitizers | `macos-sanitizers` | `tools/ci_gates.sh asan` | The offline engine/MIDI and DLS-repair harnesses are free of ASan/UBSan findings |
| Portable Release | `macos-release-strict` | `tools/ci_gates.sh release` | Pinned static dependency closure, macOS 11 arm64 target, artifact portability, metadata, DLS and SF3 loading, VST3 unit routing, `auval` |
| Windows VST3 | `windows-vst3` | not reproducible locally on macOS | Nothing yet — see below |

`tools/ci_gates.sh all` runs every locally reproducible gate in order.
`.github/workflows/ci.yml` runs the same configure, build, and test commands, so
a failure in one is a failure in the other.

## First-party warnings

`-DJUICYSF_WARNINGS_AS_ERRORS=ON` applies `-Werror` (`/WX` on MSVC) to Juicy16's
own translation units only. JUCE modules and the bundled Steinberg VST3 SDK
compile into the same targets, so the policy is applied per source file rather
than per target, and the bundled SDK is included as a `SYSTEM` directory. Third-
party warnings therefore remain visible without failing our build.

## Whitespace in the build path

pkg-config emits unquoted `-L`/`-I` flags. A space anywhere in the repository or
dependency-prefix path is split into two arguments, the pinned prefix is
discarded, and FluidSynth silently resolves from another prefix such as
Homebrew. Strict release configuration, `tools/build_macos_dependencies.sh`, and
the release gate all refuse a whitespace path with an explicit message rather
than producing a mislinked candidate. Build release candidates from a space-free
location.

## Windows

The `windows-vst3` job is marked `continue-on-error` and must not gate a
release. Phase 4.3 is open: no MSVC FluidSynth dependency policy has been
approved, DLS capability on Windows is unproven, and no Windows artifact has
been host-validated. The job exists to surface build breakage early. Remove the
`continue-on-error` flag only when Phase 4.3 completes and
[building.win32.md](../building.win32.md) is replaced with a tested guide.

## Release tags

`.github/workflows/release.yml` runs on `v*` tags. It calls the CI workflow
first and builds a candidate only after those gates pass, so a tag cannot
produce artifacts from a failing commit.

The workflow cannot, by itself, stop someone pushing a tag on a failing commit —
it only refuses to build one. Completing the Phase 5.2 "protect release tags"
task additionally requires a repository ruleset on the `v*` tag pattern in
GitHub settings, restricting tag creation to maintainers and requiring the CI
workflow to have passed on the target commit. That setting lives outside this
repository and must be recorded in the milestone plan when applied.

## What a passing run does not prove

- No DAW host has run the artifact. FL Studio, Cubase, Logic, and the wider host
  matrix in Phase 7 remain manual.
- Runners are macOS 14; the artifact declares macOS 11 but minimum-OS runtime
  testing still requires real hardware or a VM.
- Candidate packages produced by CI are ad-hoc signed only. Developer ID signing
  and notarization are open Phase 4.2 gates.
- The licensed SF2/SF3/DLS compatibility corpus is private and is not available
  to CI; only the macOS system DLS and FluidSynth's upstream SF3 fixture run
  there.
