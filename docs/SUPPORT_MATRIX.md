# Beta 1 support matrix

This is the approved target matrix for Juicy16 Beta 1. A target is advertised only after its exact packaged artifact passes the validation plan.

| Platform | Architecture | Minimum target | Release formats | Status |
| --- | --- | --- | --- | --- |
| macOS | Apple Silicon `arm64` | macOS 11.0 | AU, VST3 | Approved scope; exact artifact still requires minimum/current-OS and host validation |
| Windows | `x86_64` | Windows 10 version 1607 | VST3 | Approved scope; MSVC, clean-machine, DLS, and host validation remain open |

Development-only: Standalone builds may be used for QA but are not distributed as a Beta release format.

Out of Beta 1 scope: Intel macOS/x86_64, Windows ARM64, Windows 32-bit, Linux, VST2, and AUv3. Intel macOS may be reconsidered for a later release when an Intel build dependency set and physical test system or tester are available.

Host validation available from the product owner: FL Studio and Cubase on Windows x64 and Apple Silicon macOS. Logic and at least one additional AU/VST3 host still require another tester.

## Sample-rate scope

The automated engine suite verifies pitch-correct rendering at 44.1, 48, 88.2, and 96 kHz. The pinned FluidSynth 2.5.5 engine accepts rates from 8 to 96 kHz, but rates other than those four common values are not yet candidate-qualified. FluidSynth cannot render above 96 kHz; Juicy16 fails safely to silence and logs the unsupported rate instead of running the engine at a stale rate and producing incorrectly pitched audio.

## Bank formats

| Format | Status | Evidence |
| --- | --- | --- |
| SF2 | Supported | Core FluidSynth loader; covered by the private corpus test when configured |
| SF3 | Supported | The pinned closure builds FluidSynth with `enable-libsndfile=ON` against libsndfile/FLAC/Ogg/Vorbis/Opus; strict release CTest must load a real `.sf3` bank (`font_load_release_sf3`) |
| DLS | Supported on macOS; unproven on Windows | FluidSynth is built with `enable-native-dls=ON` and `enable-libinstpatch=OFF`; strict macOS CTest must load Apple's system DLS (`font_load_system_dls`), and strict Windows configuration requires a real `.dls` probe that has not yet been executed |

Some DLS files written by third-party editors declare RIFF sizes that FluidSynth rejects. Juicy16 loads those through a bounded, read-only repair of a temporary copy; the original file is never modified. See [DLS_REPAIR.md](DLS_REPAIR.md) for the exact boundary and non-goals.

Banks with no playable preset are rejected rather than loaded empty, and a rejected bank never replaces the one already playing.

## Audio and MIDI scope

- Sixteen MIDI channels, one shared stereo output. Per-channel audio outputs are not provided.
- Bank Select and Program Change select instruments independently per channel, at their event timestamps, with no manual patch assignment required.
- Channel 10 defaults to the percussion bank without requiring Bank Select.
- Full MIDI controller behaviour, including which controllers are interpreted and which are forwarded untouched, is specified in [CONTROLLER_SUPPORT.md](CONTROLLER_SUPPORT.md).
