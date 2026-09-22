# Windows candidate — 0.6.1-beta.4

Prepared locally on 2026-09-22 for Windows x64. This is the first native Windows
candidate prepared with MSVC and the complete static dependency closure.
It is ready for owner DAW testing; no FL Studio, Cubase, or computer-control
session was used to validate it.

## Automated evidence

The strict Release and Debug builds each pass all 17 CTest gates with first-party warnings
as errors. These cover SF2/SF3/DLS loading, bounded malformed-bank repair,
Windows system DLS, timestamped MIDI, controllers, program changes across all
16 channels, saved-state handling, randomized MIDI soak, render equivalence,
playback reliability, performance/resource lifecycles, fixture reproducibility,
documentation links and dependency patch integrity.

The native VST3 harness loads the actual Windows module, exercises component and
controller lifecycles, MIDI/unit mappings, rendering, state, editor size/scaling
queries and module unload. It does not attach an editor to a DAW window.
PE inspection requires x64 binaries and only allowlisted Windows system DLLs;
there are no FluidSynth/codec DLL or Visual C++ redistributable dependencies.

Build toolchain: Visual Studio Build Tools 2022 17.14, MSVC 14.44, Windows SDK
10.0.26100, CMake 3.31.6, JUCE 8.0.14 and patched FluidSynth 2.5.7.
The test machine runs Windows 11 build 26200. Runtime declarations target
Windows 10 version 1607. An isolated silent installer run passed installation, upgrade, installed-plugin
smoke testing, uninstall and preservation of an unrelated user file. Packaged
artifact hashes and the final extraction checks are recorded in the candidate
verification report.

## Release files

- Portable ZIP: the complete VST3 bundle and standalone audition application.
- Setup EXE: installs the VST3, optional standalone, notices and documentation.
- Source ZIP: exact project snapshot, build recipes, patches and upstream sources.
- SHA-256 sidecars and an internal SHA256SUMS manifest.

Read INSTALL-WINDOWS.txt in the binary package. The source snapshot records
local modifications explicitly rather than pretending they belong to the base
Git commit. Distribute the matching source ZIP alongside any binary package.

## Remaining manual checks

Owner playback, stop/replay, save/reopen and editor operation in Windows DAWs;
a clean Windows 10 machine without development tools; the minimum OS itself;
and the owner's private game-rip bank/MIDI collection. Windows ARM64 and 32-bit
Windows are outside this candidate's scope. The executable and installer are
unsigned. Existing synthesis timing, single stereo output and other limitations
in [KNOWN_ISSUES.md](KNOWN_ISSUES.md) still apply.

Automated passes are evidence for this candidate, not a claim that every bank,
DAW, audio device or Windows configuration has been tested.
