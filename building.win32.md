# Building Juicy16 on Windows

Windows VST3 is an intended Beta 1 format. This document now records a concrete,
pinned recipe rather than only a status — but **the recipe has never been
executed**. Nothing here is a validated release procedure until the CI job runs
green and Phase 4.3 of [ROADMAP.md](ROADMAP.md) completes. Treat
every command below as a proposal under test.

## What is pinned

The Windows dependency closure is built from the same sources, the same
versions, and the same SHA-256 checksums as the macOS closure, so both platforms
share one dependency inventory ([docs/DEPENDENCIES.md](docs/DEPENDENCIES.md)):

| Component | Version |
| --- | --- |
| FluidSynth | 2.5.5 |
| libsndfile | 1.2.2 |
| FLAC | 1.5.0 |
| libogg | 1.3.6 |
| libvorbis | 1.3.7 |
| Opus | 1.6.1 |
| GCEM | pinned commit, vendored into the FluidSynth tree |

FluidSynth is configured exactly as the decision log requires: `osal=cpp11`,
`enable-native-dls=ON`, `enable-libinstpatch=OFF`. Every Windows audio and MIDI
driver is switched off, because Juicy16 only renders blocks a host hands it.

Everything is linked statically, **including the Microsoft C runtime**, so the
VST3 requires no Visual C++ redistributable. `CMakeLists.txt` derives the
matching `/MT` setting for the plugin whenever `FLUIDSYNTH_LINK_STATIC=ON` under
MSVC; the two must agree or the link fails.

## Requirements

- Windows 10 version 1607 or later, x64
- Visual Studio 2022 with the Desktop C++ workload
- CMake 3.15 or later
- PowerShell 7 (`pwsh`)
- JUCE 8.0.14 installed to a prefix, for example `C:\juicydeps`

There is no pkg-config requirement on Windows. The pinned closure installs
FluidSynth's own CMake package config, and `CMakeLists.txt` uses it there
(`JUICYSF_FLUIDSYNTH_CMAKE_CONFIG`, which defaults to `ON` under MSVC). That
discovery path is not Windows-only guesswork: it is exercised on macOS as well,
where the full test suite runs against it.

## One-shot verification

If you are sitting at a Windows machine, run this instead of the steps below. It
does the whole path — dependency closure, configure, build, tests, DLS probe,
VST3 layout, DLL dependencies, hashes — and writes one pasteable report. It
deliberately does not stop at the first failure, because nothing here has ever
executed and a run that stops early wastes the trip.

```powershell
pwsh -File tools\verify_windows.ps1
# reuse an existing closure:
pwsh -File tools\verify_windows.ps1 -SkipDependencies
```

Run it from a **Developer PowerShell for VS 2022** so `dumpbin` is on PATH; the
script says so if it cannot find it. The report lands at
`verify-windows-report.txt`.

## Build from a clean clone

```powershell
# 1. The pinned static dependency closure. Sources and build trees live in a
#    temporary directory; only build products reach the prefix.
.\tools\build_windows_dependencies.ps1 -InstallPrefix C:\juicy16-deps

# 2. Configure and build the VST3.
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH="C:/juicy16-deps;C:/juicydeps" `
  -DFLUIDSYNTH_LINK_STATIC=ON `
  -DJUICYSF_SF3_FIXTURE="C:/juicy16-deps/share/juicy16-test-fixtures/VintageDreamsWaves-v2.sf3" `
  -DJUICYSF_COPY_PLUGIN_AFTER_BUILD=OFF `
  -DJUICYSF_WARNINGS_AS_ERRORS=ON
cmake --build build --config Release --parallel

# 3. Tests.
ctest --test-dir build -C Release --output-on-failure
```

The recipe fails deliberately if FluidSynth built without its native DLS loader.
That would not break the build — the plugin would simply refuse every DLS bank
at runtime — so it is checked where the cause is obvious.

## DLS capability on Windows

`font_load_system_dls` runs against `C:\Windows\System32\drivers\gm.dls`, the
Microsoft GS Wavetable bank present on every supported Windows install. It is
read in place, never copied or packaged. This is the only DLS capability proof
available to CI, because the private corpus is not redistributable.

Strict release validation (`JUICYSF_RELEASE_VALIDATION=ON`) additionally
requires `JUICYSF_FONT_CORPUS` to contain a private `.dls`, so it can only be run
on a machine that has the corpus — not in CI.

## Packaging

`distribute/bundle_win32.sh` validates the version, clears versioned staging,
requires the x64 VST3 artifact, packages notices and documentation only, and
emits a SHA-256. Its complete staging path has not been exercised against a real
artifact.

## Legacy Docker cross-build

`win32.Dockerfile` and `win32_cross_compile/` remain an unsupported LLVM-MinGW
experiment and are superseded by the MSVC recipe above. Nothing in CMake, CI, or
the gate script invokes them, and their output must not be published. See
[win32_cross_compile/README.md](win32_cross_compile/README.md).

## Still required before this is a release procedure

- One green run of the `windows-vst3` CI job, which has never executed.
- `dumpbin /dependents` showing only Windows system DLLs.
- The Steinberg VST3 validator, or the repository's VST3 smoke harness, run
  against the Windows bundle.
- All-16-channel Program Change in Cubase and one other Windows VST3 host.
- The licensed SF2/SF3/DLS corpus on Windows.
- A clean Windows 10 1607 VM install with no developer runtimes.
- Package filename, SHA-256, commit, and candidate number recorded for the
  frozen candidate.
