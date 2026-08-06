# VST3 multitimbral and Cubase contract

This note documents the host-facing structure that keeps all 16 MIDI channels working in VST3 hosts, especially Cubase. It is compatibility-critical: do not simplify it without preserving the automated smoke test and repeating the host matrix.

## Why VST3 needs more than raw MIDI handling

AU and some host paths deliver channelized MIDI Program Change directly. VST3 also allows a host to represent Program Change as parameter automation associated with units and a program list. Hosts do not all choose the same path. FL Studio and Cubase therefore succeeding or failing independently is expected unless both contracts are implemented.

JuicySF Rack supports both:

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

The vendored JUCE VST3 wrapper patch is wired in by CMake and pinned to JUCE 8.0.14. It supplies the missing `kIsProgramChange` flag for the per-channel program parameters. CMake fails if the expected wrapper source layout cannot be found. The patch and `Source/VST3Multitimbral.*` together are part of the compatibility contract.

## Engine synchronization

A raw MIDI Program Change and a `progChN` parameter change must converge on the same state:

- FluidSynth channel N changes program;
- engine bank/preset atomics update;
- message-thread state for channel N updates;
- `progChN` and the selected-channel controls mirror the engine without recursively reapplying it;
- serialized state contains the final channel assignment.

Incoming Bank Select remains MIDI CC0/32 state in FluidSynth. `progChN` carries only program 0–127, so host program-parameter automation alone cannot select an arbitrary bank.

## Automated and manual validation

`vst3_multitimbral_smoke` currently verifies early discovery, component/controller interfaces, all unit/channel mappings, all 16 program parameter flags/ranges, all 16 MIDI mappings, and invalid-input rejection. The offline engine test separately proves independent Program Change on all 16 channels.

Before Beta 1, Cubase must still prove the complete workflow with the exact packaged VST3:

1. Fresh instance and unmodified multichannel game-rip MIDI.
2. Load its associated bank; do not assign patches manually.
3. Confirm initial and mid-song instruments on all channels.
4. Repeat after stop/start, rewind, loop, project save/reopen, and plugin recreation.
5. Confirm channel 10 percussion and reset-at-tick-zero behavior.
6. Repeat in FL Studio and at least one additional VST3 host to exercise the other delivery path.

A result where only channel 1 works, later Program Changes are missed, or FL Studio works while Cubase fails is a Beta 1 B1 regression.
