# Building and packaging Juicy16 on Windows

The native MSVC pipeline produces an x64 VST3 bundle, a standalone audition
application, a portable ZIP, an Inno Setup installer, and corresponding source.
See [Windows candidate evidence](docs/WINDOWS_RELEASE.md) for the checks actually
run and the remaining manual host/minimum-OS checks.

## Requirements

- Windows x64; runtime target Windows 10 version 1607 or later
- Visual Studio 2022 Desktop development with C++ workload and a Windows SDK
- CMake 3.31 or later (the Visual Studio bundled CMake works)
- PowerShell 7, Python 3.9 or later, and Git
- Inno Setup 6 for building the installer

No separately installed JUCE, FluidSynth, pkg-config, or Visual C++ runtime is
needed. The scripts discover Visual Studio and its CMake automatically.

## Build and verify

Run from the repository root:

```powershell
pwsh -File tools/verify_windows.ps1
# Reuse already built libraries and JUCE:
pwsh -File tools/verify_windows.ps1 -SkipDependencies -SkipJuce
```

The command stops with a failing exit code on any error. It builds the pinned
static dependencies and JUCE, enables strict release validation and first-party
warnings as errors, builds Release, and runs all CTest gates. Logs are retained
in `build-win/verification-Release.txt` and `build-win/Testing/`.
The plugin and application are under `build-win/JuicySFPlugin_artefacts/Release`.
Nothing is copied into your DAW's plugin directories during the build.

Debug needs its own matching static Debug dependency closure:

```powershell
pwsh -File tools/verify_windows.ps1 -Configuration Debug -SkipJuce `
  -DepsPrefix "$PWD/build-win-deps-debug" -BuildDir "$PWD/build-win-debug"
```

The shipped Release binary and all its codec libraries use `/MT`. Debug uses
`/MTd`; the two closures must never be mixed. Opus requires its own explicit
`OPUS_STATIC_RUNTIME=ON`. CMake config packages supply the complete static
codec dependencies and their required definitions, including `FLAC__NO_DLL`.

## Pinned sources

JUCE 8.0.14, FluidSynth 2.5.7, libsndfile 1.2.2 with the reviewed IRCAM patch,
FLAC 1.5.0, Ogg 1.3.6, Vorbis 1.3.7, Opus 1.6.1, and the pinned GCEM commit.
The FluidSynth CC1 and full-range DLS pan patches and JUCE multitimbral wrapper
are shared with macOS. Rebuild the dependency prefix for 1.0.0-beta.1.
All dependency tarballs are SHA-256 checked. `.gitattributes` preserves the
reviewed vendored bytes on Windows checkouts. Python extracts Unicode archive
names without depending on the Windows system locale.

FluidSynth uses `osal=cpp11`, native DLS enabled, libinstpatch disabled, and
unused audio/MIDI/network drivers disabled. The tests load Windows' own
`C:/Windows/System32/drivers/gm.dls` in place and upstream regression SF2/SF3/DLS
fixtures. Neither the system bank nor the local corpus goes into binary packages.

## Package

```powershell
pwsh -File distribute/bundle_windows.ps1 -Candidate BC1 `
  -Iscc 'C:/Program Files (x86)/Inno Setup 6/ISCC.exe'
```

Use a clean, committed tree. `-AllowDirty` produces a labelled local validation
package only. Pass the actual Inno compiler path on your machine. The packager requires the
strict Release build and passing tests, checks x64/system-only imports, stages
notices and documentation, verifies documentation links, builds the installer,
extracts the portable archive, checks every file hash, and runs the host smoke
test against the extracted plugin. Output is in `distribute/out/`. Existing
candidate staging directories are never silently overwritten; use a new
candidate number or explicitly remove your previous staging directories.

The installer installs the VST3 to the standard Common Files/VST3 location and
offers the standalone application. A portable user copies the complete VST3
bundle or runs `Standalone/Juicy16.exe` directly. Neither method installs banks.
Setup and binaries are unsigned unless you separately arrange Authenticode
signing. The package filename and BUILD_INFO.json identify the candidate.

An isolated installer check is available in `tests/WindowsInstallerTests.ps1`.
It silently installs twice into new test folders, compares binary hashes, loads
the installed VST3, uninstalls, and checks that an unrelated user file survived.
It refuses to run over an existing per-user Juicy16 installation. It temporarily
registers a per-user install; run it only on a build/test machine.

## Rebuild the corresponding-source ZIP without downloading sources

Extract the source ZIP and run:

```powershell
pwsh -File tools/verify_windows.ps1 -SourceArchiveDir "$PWD/upstream"
```

The source ZIP contains the exact modified project files used for the candidate,
original checksummed dependency archives, and the pinned JUCE source archive.
SOURCE_INFO.json identifies the base commit and whether it had local changes;
SHA256SUMS identifies every exact source file. The build recipes apply the
reviewed patches. An installed compiler, Windows SDK, CMake, Python and
PowerShell are still required. Replacing a library with a modified version for
relinking requires intentionally updating its source/hash checks.

The [September 27 evidence](docs/WINDOWS_RELEASE.md) covers fresh 1.0.0-beta.1
builds and package checks. Owner DAW testing and clean minimum-OS installation
remain pending before publication.

## Legacy cross-build

The old LLVM-MinGW Docker build and cross-compilation scripts have been
removed. Use the native PowerShell workflow for Windows builds.
