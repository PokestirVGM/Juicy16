# Juicy16

Juicy16 is a 16-channel multitimbral DLS/SoundFont player inspired by the automatic patch-selection workflow of Fruity LSD. Load one `.dls`, `.sf2`, or `.sf3` bank, send a multichannel MIDI file to one plugin instance, and its Bank Select and Program Change events select instruments independently on MIDI channels 1–16. All channels mix to one stereo output.

The current development version is `0.5.1-alpha.5`. It is an alpha: the engine, build, and automated gates are in good shape, but the Beta 1 readiness bar has not been met, so it is deliberately not labelled beta. The authoritative readiness checklist is [MILESTONE_PLAN.md](MILESTONE_PLAN.md).

## What is implemented

- Sixteen independent MIDI channels with per-channel bank, preset, and exposed sound-controller state.
- Timestamped notes, controllers, Program Changes, pitch bends, pressure, and supported SysEx; events are applied at their sample offsets rather than at the beginning of every audio block.
- General MIDI percussion default on channel 10 (FluidSynth bank 128), with melodic bank 0 on the other channels.
- Automatic Program Change handling for game-rip MIDI playback, including later changes during a song.
- GM, GS, and XG reset detection followed by immediate restoration of the plugin's current per-channel program and exposed controller state.
- Full CC forwarding to FluidSynth. CC7 (volume) and CC10 (pan) are also mirrored into the selected-channel controls and saved per-channel state.
- Full unnormalized 14-bit pitch bend and MIDI RPN pitch-bend range handling through FluidSynth.
- Transactional bank replacement: a failed replacement reports an error and leaves the previous working bank active.
- Safe temporary repair of a narrow class of malformed DLS RIFF-size fields. The original file is never modified; the exact limits are documented in [docs/DLS_REPAIR.md](docs/DLS_REPAIR.md).
- 7th-order FluidSynth interpolation, 512-voice polyphony, and correct host-rate rendering through FluidSynth's 96 kHz ceiling.

## Formats and current validation status

| Platform | Format | Intended Beta 1 status | Current evidence |
| --- | --- | --- | --- |
| macOS | AU | Release format | Builds and passes strict signature/dependency checks and `auval` locally; DAW and minimum-OS matrices remain required |
| macOS | VST3 | Release format | Automated 16-channel VST3 unit/mapping smoke test passes; Cubase end-to-end retest remains required |
| Windows | VST3 | Release format | Intended, but the legacy cross-build pipeline is not yet Beta-ready or host-validated |
| Desktop | Standalone | Development/QA only | Built for local testing; not a primary release format |
| Desktop | VST2 | Out of scope | Not configurable or built by the Beta 1 CMake project |

The approved scope is macOS 11 or later on Apple Silicon (`arm64`) with AU/VST3, and Windows 10 version 1607 or later on `x86_64` with VST3. Intel macOS, Windows ARM64, and Linux are outside Beta 1. Standalone remains a development/QA build only. See the [support matrix](docs/SUPPORT_MATRIX.md) and the exact [MIDI controller support contract](docs/CONTROLLER_SUPPORT.md).

## Using it with multichannel MIDI

1. Insert one Juicy16 instrument instance.
2. Load the DLS, SF2, or SF3 bank associated with the MIDI file.
3. Route the original MIDI channels 1–16 to that instance without flattening them to channel 1.
4. Start playback from the beginning so any reset, Bank Select, and Program Change setup events are delivered.

Incoming MIDI patch events are authoritative. Manual row selections provide a starting assignment, but a later Program Change on that MIDI channel replaces it at the event's timestamp.

Host routing is not hard-coded to FL Studio or Cubase. AU hosts can deliver normal channelized MIDI. VST3 hosts may use MIDI mapping or VST3 units/program parameters; Juicy16 implements both. Whether a particular DAW imports and routes a multichannel MIDI file correctly is host configuration and must be verified. See [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md).

## Interface

- The 16-row channel list selects a channel for editing and offers manual bank/preset selection.
- Volume and pan edit the selected channel's CC7 and CC10. Incoming MIDI on that channel overrides what you set, exactly as a Program Change overrides a manually picked instrument.
- Output level is a master trim in decibels for the whole plugin. It is not a MIDI controller, so nothing in a MIDI file moves it.
- The keyboard auditions the selected channel and displays incoming note activity.
- The status area reports the running version. Bank-load results are also stored in the model and exposed through the file control tooltip.
- Everything works without a mouse where the host passes Tab through: arrows on the channel list select a channel, Return opens that row's instrument list, and arrows on a focused slider change its value. Screen-reader announcements are untested — see [docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md).

## Building and testing

- macOS: [building.macos.md](building.macos.md)
- Windows status and intended path: [building.win32.md](building.win32.md)
- CI quality gates: [docs/CI.md](docs/CI.md)
- DAW host test protocol and MIDI fixtures: [docs/HOST_TEST_PROTOCOL.md](docs/HOST_TEST_PROTOCOL.md)
- VST3/Cubase architecture: [docs/VST3_MULTITIMBRAL_DESIGN.md](docs/VST3_MULTITIMBRAL_DESIGN.md)
- Beta state compatibility: [docs/STATE_COMPATIBILITY.md](docs/STATE_COMPATIBILITY.md)

The local automated gate is:

```bash
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

`tools/ci_gates.sh all` runs the full gate set — documentation links, a Debug
build with first-party warnings as errors, sanitized offline harnesses, and the
strict portable Release build. The GitHub Actions workflows call the same
script, so a local failure is a CI failure.

It currently covers DLS repair/load, sample-offset rendering, mono/stereo behavior, 16-channel Program Change, reset chase, common CCs, exact pitch-bend values, RPN bend ranges, corrupt selected-channel state, transactional failed bank replacement, and the VST3 multitimbral contract.

## Known limitations and open Beta gates

- One stereo output; no per-channel audio outputs.
- Common 44.1, 48, 88.2, and 96 kHz rates are covered by the engine suite. FluidSynth 2.5.5 cannot render above 96 kHz; Juicy16 intentionally outputs silence at those rates instead of playing at the wrong pitch.
- A complete licensed SF2/SF3/DLS compatibility corpus is not yet present.
- FL Studio, Cubase, Logic, another AU host, and another VST3 host still require candidate-specific manual validation.
- Windows DLS support and clean-machine dependency behavior have not yet been proven.
- The source-built static dependency closure and Juicy16 artifact declare macOS 11 arm64 and pass local portability checks, but runtime testing on macOS 11 and the current release is still required.
- Logic/additional-AU-host validation, Developer ID/notarization, and production packaging of the frozen candidate remain open; the local deterministic packaging workflow is implemented and self-validating.
- AGPL/GPL source packaging and notices require final qualified review before distribution.

## Privacy and licenses

The plugin has no intentional runtime networking or telemetry. See [PRIVACY.txt](PRIVACY.txt).

The inherited application code is GPLv3, while JUCE 8 is used under AGPLv3. Open-source distribution still preserves copyright and requires corresponding source and notices. See [LICENSE.txt](LICENSE.txt), [NOTICE.md](NOTICE.md), [docs/LICENSING.md](docs/LICENSING.md), and [licenses_of_dependencies](licenses_of_dependencies/).

## Project lineage

Juicy16 began from the original Birchlabs JuicySF plugin codebase, which provided the starting SoundFont-engine work. The current multichannel product, MIDI behavior, host integration, state model, testing, and release engineering have developed substantially beyond that base. “Inspired by Fruity LSD” describes the automatic multichannel patch-selection workflow; it is not a claim of exact emulation or affiliation.
