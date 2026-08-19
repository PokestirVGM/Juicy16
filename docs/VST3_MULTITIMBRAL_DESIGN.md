# VST3 multitimbral and Cubase contract

This note documents the host-facing structure that keeps all 16 MIDI channels working in VST3 hosts, especially Cubase. It is compatibility-critical: do not simplify it without preserving the automated smoke test and repeating the host matrix.

## Why VST3 needs more than raw MIDI handling

AU and some host paths deliver channelized MIDI Program Change directly. VST3 also allows a host to represent Program Change as parameter automation associated with units and a program list. Hosts do not all choose the same path. FL Studio and Cubase therefore succeeding or failing independently is expected unless both contracts are implemented.

Juicy16 supports both:

1. `IMidiMapping` maps `kCtrlProgramChange` on event-bus channels 0–15 to `progCh1`–`progCh16`.
2. `IUnitInfo` exposes a root unit plus one unit per MIDI channel, with each channel unit attached to a shared 128-entry program list. The corresponding `progChN` parameter is a discrete list/program-change parameter.

There is no hard-coded `if host == Cubase` or `if host == FL Studio` branch. Compatibility comes from satisfying the VST3 interfaces each host uses.

## Stable structure

- Unit count: 17 (root plus 16 channel units).
- Event-bus MIDI channel N maps to channel unit N.
- Program list: one shared list with 128 entries; loaded bank-0 names populate available entries and generic names fill gaps.
- Parameters: `progCh1` through `progCh16`, integer range 0–127, `stepCount == 127`, and the VST3 `kIsProgramChange` and `kIsList` flags.
- Parameter group IDs and VST3 unit IDs use the same JUCE string-hash derivation.
- Out-of-range event buses, channels, controller numbers, units, list indices, and program indices must be rejected.

## Initialization-order invariant

Cubase may query and cache `IUnitInfo` before JUCE has connected the VST3 component and controller. The root/channel units and program list must therefore be available during early controller queries and remain identical after connection. Returning an empty list early can make only the first MIDI channel appear functional for the plugin instance.

The vendored JUCE VST3 wrapper patch is wired in by CMake and pinned to JUCE 8.0.14. It is the sole `IUnitInfo` implementation for both the component and controller, supplies early controller-side unit/program-list discovery, per-channel `IMidiMapping`, bounds rejection, and the missing `kIsProgramChange`/`kIsList` flags. It also recognizes the frozen `progChN` ParamIDs while processing: every point in each program-parameter queue becomes a channelized MIDI Program Change at the queue point's sample offset. This is necessary because stock JUCE treats ordinary parameters as block-level values and keeps only the final point, which would quantize mid-block Program Changes to block start. `Source/VST3Multitimbral.*` no longer claims the same COM interface; it owns only the shared program-name store and host refresh notification. This avoids JUCE's duplicate-interface assertion while retaining component-side queries and pre-connection Cubase discovery. CMake verifies the exact upstream and vendored wrapper hashes and fails if either side drifts. The reproducible diff and instructions live in `vendor/juce_patched/`.

## Engine synchronization

A raw MIDI Program Change and a `progChN` parameter change must converge on the same state:

- FluidSynth channel N changes program;
- engine bank/preset atomics update;
- message-thread state for channel N updates;
- `progChN` and the selected-channel controls mirror the engine without recursively reapplying it;
- serialized state contains the final channel assignment.

Incoming Bank Select remains MIDI CC0/32 state in FluidSynth. `progChN` carries only program 0–127, so host program-parameter automation alone cannot select an arbitrary bank.

## Automated and manual validation

`vst3_multitimbral_smoke` verifies early discovery, identical pre/post-connection unit answers, repeatable component/controller lifecycles, component/controller interfaces, all unit/channel mappings, all 16 program parameter flags/ranges, all 16 MIDI mappings, invalid-input rejection, and program-name refresh plus host notification after loading the system DLS. It then configures `IAudioProcessor` and drives the checked-in `tests/fixtures/vst3_multichannel_programs.csv` through ParamIDs independently discovered via `IMidiMapping` and the unit/program-parameter path. The fixture covers all 16 channels, Bank Select, channel 10 percussion, simultaneous and mid-block Program Changes, same-block notes, framed GM/GS/XG reset SysEx, and a stop/restart-at-sample-zero reset scenario. Each scenario must produce audio, converge all channel parameters and serialized bank/program state, report exactly one final host edit per changed `progChN`, and suppress unchanged duplicates; the restart scenario deliberately expects zero edits while still requiring correct engine/state restoration. A direct Debug run is required so assertion text cannot be hidden by CTest's passing-output suppression. The offline engine test separately proves raw MIDI controller values, Program Change timing, reset behavior, and per-note engine checkpoints.

Before Beta 1, Cubase must still prove the complete workflow with the exact packaged VST3:

1. Fresh instance and unmodified multichannel game-rip MIDI.
2. Load its associated bank; do not assign patches manually.
3. Confirm initial and mid-song instruments on all channels.
4. Repeat after stop/start, rewind, loop, project save/reopen, and plugin recreation.
5. Confirm channel 10 percussion and reset-at-tick-zero behavior.
6. Repeat in FL Studio and at least one additional VST3 host to exercise the other delivery path.

A result where only channel 1 works, later Program Changes are missed, or FL Studio works while Cubase fails is a Beta 1 B1 regression.
