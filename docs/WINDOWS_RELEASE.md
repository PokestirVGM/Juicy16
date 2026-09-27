# Windows validation — 1.0.0-beta.1

Validated locally on 2026-09-27, Windows 11 x64 build 26200, MSVC 14.44,
Windows SDK 10.0.26100 and CMake 3.31.6. The owner tested the installed Windows
VST3 and approved publication on 2026-09-27; no per-host checklist was supplied.

- Strict Release and Debug: **17/17 CTest gates passed in each**. Both static
  dependency closures were rebuilt, including FluidSynth's full-range DLS pan patch.
- Automated checks passed for independent 16-channel Bank Select/Program Change,
  transport restart, state recall, interpolation choices/default/persistence,
  stereo-pair DLS pan and channel 10 fallback to a melodic-bank kit.
- `dumpbin /headers` and `/dependents`: VST3 and standalone are x64 with only
  Windows system imports; no separate codec or Visual C++ runtime DLLs.
- Portable extraction, file hashes and extracted VST3 smoke passed. Isolated
  silent install, upgrade, installed-plugin smoke, uninstall and unrelated-file
  preservation passed. Installer testing required execution outside the sandbox.

Build logs: `build-win/verification-Release.txt` and
`build-win-debug/verification-Debug.txt`; detailed gates: each build's `Testing/`.
Packages and SHA-256 sidecars are in `distribute/out/`. `BUILD_INFO.json` and
the matching source ZIP's `SOURCE_INFO.json` identify the exact clean commit.
The source ZIP includes the project, patches and pinned upstream archives.

**Validation limits:** no itemized FL Studio/Cubase results for 16-channel
routing, stop/replay, save/reopen or editor operation. Automated editor checks
do not establish DAW operation. Clean/minimum Windows 10 (1607) remains
unverified. Binaries and installer are unsigned. Debug timing is diagnostic;
Release still enforces realtime thresholds, and both enforce resource checks.
See [known issues](KNOWN_ISSUES.md) and [build/install details](../building.win32.md).
