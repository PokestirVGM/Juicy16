# Building Juicy16 on Windows

Windows VST3 is an intended Beta 1 format, but the repository does not yet contain a supported, validated Windows release pipeline. This document records the current state so an old experimental build is not mistaken for a release recipe.

## Beta 1 required path

The preferred release toolchain is Windows 10/11 with Visual Studio 2022, CMake, JUCE 8.0.14, and a current FluidSynth build that proves SF2, SF3, and DLS support. The CMake target defines `WINVER` and `_WIN32_WINNT` as `0x0A00` and `NTDDI_VERSION` as `0x0A000002` (`NTDDI_WIN10_RS1`) for the approved Windows 10 version 1607 API floor. Runtime testing on an actual 1607 system remains mandatory; header macros alone are not compatibility proof.

The resulting x64 VST3 must:

- pass the repository test suite and VST3 smoke/validator checks;
- contain the expected `Contents/x86_64-win/` module layout;
- have no missing developer/runtime DLL dependency;
- load on a clean minimum-version Windows VM;
- pass all-16-channel Program Change tests in Cubase and another VST3 host;
- pass the licensed SF2/SF3/DLS corpus, including DLS capability as an executed test.
- set `JUICYSF_SF3_FIXTURE` to the licensed FluidSynth upstream SF3 regression bank (or another reviewed local SF3); strict configuration rejects a missing or non-SF3 fixture.

Until that MSVC dependency and CI setup is committed and verified, there is no endorsed one-command Windows Beta build.

## Legacy Docker cross-build

`win32.Dockerfile` and `win32_cross_compile/` remain an unsupported LLVM-MinGW experiment. The context has been partially remediated: it pins JUCE 8.0.14 and FluidSynth 2.5.5, builds only the x64 VST3 target, no longer requires a VST2 SDK, and configures FluidSynth's native C++17 DLS loader (`osal=cpp11`, `enable-libinstpatch=off`). That work reduces obvious drift but does not turn LLVM-MinGW into an upstream-supported JUCE 8 release toolchain, and the image has not been rebuilt or validated in Windows hosts.

Do not publish the result of:

```bash
docker build . -f win32.Dockerfile --tag=llvm-mingw
```

without first completing Phase 4.3 of [MILESTONE_PLAN.md](MILESTONE_PLAN.md). `distribute/bundle_win32.sh` now creates clean, version-checked, x64 VST3-only staging and a SHA-256 file, but its real Docker artifact path and final notice set remain unverified. Its output is deliberately labelled unsupported.

## Required evidence for completing this guide

When the Windows pipeline is implemented, replace this status document with exact clean-machine commands and record:

- Visual Studio, Windows SDK, CMake, JUCE, and FluidSynth versions;
- source and license of every statically linked or shipped binary dependency;
- configure/build/test commands from a clean clone;
- VST3 validator output and DLL dependency report;
- Windows versions and hosts used;
- SF2/SF3/DLS corpus result;
- package filename, SHA-256, commit, and candidate number.
