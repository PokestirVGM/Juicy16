# Changelog

## 0.5.0-beta.1 — unreleased candidate

This candidate is still being remediated and has not passed the complete Beta 1 gate.

### Changed

- Defined AU and VST3 as release formats; Standalone is QA-only and VST2 is disabled by default.
- Pinned JUCE 8.0.14 and centralized the visible version as `0.5.0-beta.1`.
- Reworked audio rendering so MIDI events take effect at their sample positions within each block.
- Preserved deterministic input order for Bank Select, Program Change, reset SysEx, controllers, and notes sharing a timestamp.
- Recreated FluidSynth at the host sample rate and preallocated/chunked mono-render scratch storage.
- Added General MIDI channel 10 percussion-bank defaults.
- Made reset recovery use current atomic program/controller state so stale asynchronous state cannot overwrite newer events.
- Made bank replacement transactional and added structured load status/error properties.
- Restored the last working bank path/bookmark after a rejected replacement so the next project save cannot persist the failed candidate.
- Added macOS path fallback when bookmark creation fails and released Core Foundation errors.
- Made static FluidSynth linkage usable for portable macOS Release artifacts.
- Ordered bundle resources, VST3 metadata, and final signing deterministically.

### Fixed

- Restored-state selected-channel bounds and per-channel engine-call validation.
- Full 16-channel Program Change routing in the engine and VST3 unit/mapping smoke paths.
- Full 14-bit pitch-bend forwarding and per-channel RPN bend range.
- Path-only font restoration on macOS.
- Failed bank loads no longer silently destroy a working setup.
- State created by a newer schema is rejected visibly instead of being silently reinterpreted.

### Tests

- Registered CTest coverage for DLS repair/loading, offline engine/MIDI rendering, transactional load failure, and VST3 multitimbral discovery/mapping.
- Verified Debug and statically linked Release suites on arm64 macOS on 2026-08-05.
- Verified strict AU/VST3 ad-hoc signatures and absence of prohibited dynamic dependency paths in the local Release artifacts.

### Required before public Beta 1

See [MILESTONE_PLAN.md](MILESTONE_PLAN.md). Major remaining gates include product/licensing approval, the licensed cross-format font corpus, CI, audio-domain controller/pitch verification, AU/DAW validation, Windows pipeline and DLS proof, minimum-OS/architecture decisions, packaging, and clean-machine installation.
