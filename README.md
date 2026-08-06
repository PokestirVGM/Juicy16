# JuicySF Rack

JuicySF Rack is a 16-channel multitimbral DLS/SoundFont player inspired by the automatic patch-selection workflow of Fruity LSD. Load one `.dls`, `.sf2`, or `.sf3` bank, send a multichannel MIDI file to one plugin instance, and its Bank Select and Program Change events select instruments independently on MIDI channels 1–16. All channels mix to one stereo output.

The current development version is `0.5.0-beta.1`. It is a beta candidate under active validation, not yet an approved public release. The authoritative readiness checklist is [MILESTONE_PLAN.md](MILESTONE_PLAN.md).

## What is implemented

- Sixteen independent MIDI channels with per-channel bank, preset, and exposed sound-controller state.
- Timestamped notes, controllers, Program Changes, pitch bends, pressure, and supported SysEx; events are applied at their sample offsets rather than at the beginning of every audio block.
- General MIDI percussion default on channel 10 (FluidSynth bank 128), with melodic bank 0 on the other channels.
- Automatic Program Change handling for game-rip MIDI playback, including later changes during a song.
- GM, GS, and XG reset detection followed by immediate restoration of the plugin's current per-channel program and exposed controller state.
- Full CC forwarding to FluidSynth. CC71, 72, 73, 74, 75, and 79 are also mirrored into the selected-channel controls and saved per-channel state.
- Full unnormalized 14-bit pitch bend and MIDI RPN pitch-bend range handling through FluidSynth.
- Transactional bank replacement: a failed replacement reports an error and leaves the previous working bank active.
- Safe temporary repair of a narrow class of malformed DLS RIFF-size fields. The original file is never modified.
- 7th-order FluidSynth interpolation, 512-voice polyphony, and host sample-rate tracking.

## Formats and current validation status

| Platform | Format | Intended Beta 1 status | Current evidence |
| --- | --- | --- | --- |
| macOS | AU | Release format | Builds and passes strict signature/dependency checks locally; `auval` and DAW matrix remain required |
| macOS | VST3 | Release format | Automated 16-channel VST3 unit/mapping smoke test passes; Cubase end-to-end retest remains required |
| Windows | VST3 | Release format | Intended, but the legacy cross-build pipeline is not yet Beta-ready or host-validated |
| Desktop | Standalone | Development/QA only | Built for local testing; not a primary release format |
| Desktop | VST2 | Unsupported legacy option | Disabled by default and outside Beta 1 |

Minimum operating systems, released architectures, product identity, and the JUCE licensing path are still owner decisions recorded as blockers in the milestone plan. Do not redistribute a Beta 1 artifact until those gates are resolved.

## Using it with multichannel MIDI

1. Insert one JuicySF Rack instrument instance.
2. Load the DLS, SF2, or SF3 bank associated with the MIDI file.
3. Route the original MIDI channels 1–16 to that instance without flattening them to channel 1.
4. Start playback from the beginning so any reset, Bank Select, and Program Change setup events are delivered.

Incoming MIDI patch events are authoritative. Manual row selections provide a starting assignment, but a later Program Change on that MIDI channel replaces it at the event's timestamp.

Host routing is not hard-coded to FL Studio or Cubase. AU hosts can deliver normal channelized MIDI. VST3 hosts may use MIDI mapping or VST3 units/program parameters; JuicySF Rack implements both. Whether a particular DAW imports and routes a multichannel MIDI file correctly is host configuration and must be verified. See [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md).

## Interface

- The 16-row channel list selects a channel for editing and offers manual bank/preset selection.
- Attack, decay, sustain, release, cutoff, and resonance edit MIDI sound controllers for the selected channel. Value 64 is neutral.
- The keyboard auditions the selected channel and displays incoming note activity.
- The status area reports the running version. Bank-load results are also stored in the model and exposed through the file control tooltip.

## Building and testing

- macOS: [building.macos.md](building.macos.md)
- Windows status and intended path: [building.win32.md](building.win32.md)
- VST3/Cubase architecture: [docs/VST3_MULTITIMBRAL_DESIGN.md](docs/VST3_MULTITIMBRAL_DESIGN.md)
- Beta state compatibility: [docs/STATE_COMPATIBILITY.md](docs/STATE_COMPATIBILITY.md)

The local automated gate is:

```bash
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

It currently covers DLS repair/load, sample-offset rendering, mono/stereo behavior, 16-channel Program Change, reset chase, common CCs, exact pitch-bend values, RPN bend ranges, corrupt selected-channel state, transactional failed bank replacement, and the VST3 multitimbral contract.

## Known limitations and open Beta gates

- One stereo output; no per-channel audio outputs.
- A complete licensed SF2/SF3/DLS compatibility corpus is not yet present.
- FL Studio, Cubase, Logic, another AU host, and another VST3 host still require candidate-specific manual validation.
- Windows DLS support and clean-machine dependency behavior have not yet been proven.
- The current local arm64 artifact inherits a macOS 26.0 deployment target from the toolchain and is not distributable as a broadly compatible Beta. An approved explicit minimum target and rebuild are required.
- AU validation, universal/x86_64 decisions, notarization, and release packaging remain open.
- Licensing and product-identity approval remain distribution blockers. The existing license files describe repository history but must not be treated as completed JUCE 8 release clearance.

## Privacy and licenses

The plugin has no intentional runtime networking or telemetry. See [PRIVACY.txt](PRIVACY.txt).

See [LICENSE.txt](LICENSE.txt) and [licenses_of_dependencies](licenses_of_dependencies/) for the current repository notices. The final JUCE 8 licensing/distribution position is explicitly unresolved and must be approved before public Beta distribution.
