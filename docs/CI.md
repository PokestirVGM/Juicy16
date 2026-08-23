# Continuous integration and quality gates

This document describes the automated gates required by Phase 5.2 of
[MILESTONE_PLAN.md](../MILESTONE_PLAN.md), how to reproduce each one locally, and
what a passing run does and does not prove.

## Gates

| Gate | Workflow job | Local command | Proves |
|---|---|---|---|
| Documentation links | `docs` | `tools/ci_gates.sh docs` | Every internal Markdown link in active documentation resolves |
| macOS Debug | `macos-debug` | `tools/ci_gates.sh debug` | Clean-clone configure/build with first-party warnings as errors, plus the full CTest suite — including the in-process VST3 and AU host harnesses and the host-test MIDI fixtures |
| Sanitizers | `macos-sanitizers` | `tools/ci_gates.sh asan` | The offline engine/MIDI and DLS-repair harnesses are free of ASan/UBSan findings |
| Leaks | `macos-leaks` | `tools/ci_gates.sh leaks` | Every offline harness, including the VST3 and AU host harnesses, exits with zero leaked allocations |
| Portable Release | `macos-release-strict` | `tools/ci_gates.sh release` | Pinned static dependency closure, macOS 11 arm64 target, artifact portability, metadata, DLS and SF3 loading, VST3 unit routing, `auval` |
| Windows VST3 | `windows-vst3` | `tools/build_windows_dependencies.ps1` then a normal configure/build/test on a Windows machine | Nothing yet — see below |

`tools/ci_gates.sh all` runs every locally reproducible gate in order.

`engine_host_program_matrix`, `engine_host_controllers`, and
`host_fixtures_reproducible` validate the importable host-test fixtures described
in [HOST_TEST_PROTOCOL.md](HOST_TEST_PROTOCOL.md): the first two play them through
the engine against the platform's system GM bank, and the third pins the committed
`.mid` files to `tools/make_host_fixtures.py` so the protocol's expected-value
tables cannot drift from the bytes a tester imports. They need no private corpus,
so they run on macOS and Windows alike.

The `dependency_patch_contract` CTest runs in the Debug and Release suites. It
does **not** rebuild the dependency closure — that needs network access — so it
proves only that the vendored libsndfile security patch, the two dependency
recipes, and `vendor/libsndfile_patched/README.md` still agree on the same three
hashes. Whether the patch actually reached a shipped binary is proven by the
recipes themselves, which fail on any hash mismatch.
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
release. Phase 4.3 is open and no Windows artifact has been host-validated.

What changed: the job used to run a bare `vcpkg install fluidsynth:x64-windows`.
That was never an approved dependency source — its version was whatever vcpkg
carried, and it was not configured for FluidSynth's native DLS loader, so the
plugin it built may not have loaded DLS banks at all. It now builds the pinned
closure from `tools/build_windows_dependencies.ps1`: the same components,
versions, and checksums as macOS, statically linked including the C runtime.

The job also now proves DLS capability at runtime rather than assuming it.
`font_load_system_dls` runs against `C:\Windows\System32\drivers\gm.dls`, the
Microsoft GS Wavetable bank present on every supported Windows install — the
only DLS proof available to CI, since the private corpus is not redistributable.
The recipe separately fails if FluidSynth built without the native DLS loader,
which would otherwise produce a plugin that builds fine and refuses every DLS
bank.

**The recipe has never run.** Expect the first hosted attempt to need fixes.
Remove the `continue-on-error` flag only after the job is green, the
`dumpbin /dependents` step shows only Windows system DLLs, and Phase 4.3
completes with host validation.

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

## Leaks

LeakSanitizer is unavailable on Darwin arm64, so the sanitizer gate runs with
leak detection off. The `leaks` gate covers that gap by running each offline
harness under macOS `leaks -atExit` and requiring `0 leaks for 0 total leaked
bytes`. It also covers Core Foundation objects — the security-scoped bookmark
path owns `CFURL`, `CFData`, and `CFError` values that a C-level sanitizer would
not attribute usefully.

`leaks` exits non-zero when it finds leaks, so its status cannot distinguish
"leaked" from "failed to run"; the gate reads the summary line instead and fails
on a missing summary as well as on a non-zero count. Reports are written to
`build-ci-debug/Testing/leaks-*.log` and archived by the job.

## In-process plugin-format harnesses

`vst3_multitimbral_smoke` and `au_host_smoke` load the built bundles the way a
host does, in the test process, without installing anything:

- The VST3 harness instantiates component and controller, checks pre-connection
  unit discovery, both Program Change routes, audio, host edits, and state.
- The AU harness reads the component description from the bundle's own
  `Info.plist`, registers the factory it names with `AudioComponentRegister`,
  then drives the unit through `MusicDeviceMIDIEvent` and `AudioUnitRender`. It
  covers instantiation, stream format, the 24 published parameters, per-channel
  Program Change and its parameter mirror, per-channel audio isolation across all
  16 channels, ClassInfo save/restore, disposal, and reinstantiation.

Because the AU is registered from its own bundle, a copy already installed in
`~/Library/Audio/Plug-Ins/Components` can never be tested by mistake. Neither
harness replaces a real host: no window is created, so editor resizing, keyboard
focus, native file access, and screen-reader behaviour remain manual.

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
