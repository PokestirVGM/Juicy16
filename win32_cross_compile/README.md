# Quarantined: legacy LLVM-MinGW cross-build

These scripts and the root `win32.Dockerfile` are an **unsupported experiment**, retained only so the earlier Windows attempt is not lost. They are not part of the Beta 1 release path and nothing in CI, `tools/ci_gates.sh`, or `CMakeLists.txt` invokes them.

The approved Windows toolchain is MSVC, per Phase 4.3 of [MILESTONE_PLAN.md](../MILESTONE_PLAN.md). JUCE 8 does not support MinGW as a release toolchain, and this image has not been rebuilt or host-validated.

Do not publish anything these scripts produce. See [building.win32.md](../building.win32.md) for the required path and the evidence that must accompany it.

When the MSVC pipeline lands and is validated, delete this directory and `win32.Dockerfile` rather than maintaining two Windows stories.
