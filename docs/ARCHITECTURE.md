# How it works

These are the parts of Juicy16 that aren't obvious from the code and are easy to
break by accident.

## Audio path

FluidSynth renders each MIDI channel into its own internal stereo group (dry
plus reverb/chorus), so every channel gets its own trim and meter. The groups
are summed into the single stereo output. Reverb and chorus settings are global.

## Per-channel Program Change in VST3

AU hosts send Program Change as normal MIDI. VST3 hosts can instead treat it as
parameter automation tied to "units", and hosts disagree on which route they
use. Juicy16 supports both, without any host-specific code:

1. **MIDI mapping:** Program Change on channels 1–16 maps to `progCh1`–`progCh16`
   (the route FL Studio uses).
2. **Units:** a root unit plus one unit per channel, all sharing a 128-entry
   program list named from the loaded bank (the route Cubase uses). Each
   `progChN` is a discrete program-change parameter with 128 steps.

Cubase asks for the unit structure before JUCE has connected the plugin's two
halves, then caches the answer. Stock JUCE answers "one unit, no programs" at
that point, which leaves only channel 1 working. So a patched copy of JUCE's
VST3 wrapper ([vendor/juce_patched](../vendor/juce_patched/README.md)) serves the
full structure from the very first query. It also turns every point of a
`progChN` automation queue into a Program Change at its exact sample, rather than
keeping only the block's last value. CMake checks the patch against JUCE 8.0.14
and refuses to build if either side has changed.

Bank Select still comes from MIDI CC0/32; `progChN` only carries the program
number.

`vst3_smoke` checks all of this: early discovery, all 16 mappings and units, and
a 16-channel Program Change fixture driven through both routes.

## Threads

**Audio thread** (`processBlock`, MIDI dispatch, rendering): calls FluidSynth and
writes fixed-size atomics. It never allocates, touches the `ValueTree` or UI,
loads banks or creates files.

- Program changes and CC7/CC10 captured there set dirty flags. The message thread
  later copies the latest accepted values into parameters and saved state.
- GM/GS/XG resets are handled in order on the audio thread. Juicy16 restores each
  channel's program and controllers straight away, before the next event.
- SysEx is read straight from the MIDI buffer, because `MidiMessage` would
  heap-copy the reset every rip starts with.

**Message thread:** owns the UI, `ValueTree`, bank loading, bookmarks and the
repaired-DLS temp file. A new bank's ID is published only after it has loaded
and proved playable.

`prepareToPlay` may recreate the synth for a new sample rate before playback.
FluidSynth's thread-safe API stays on because occasional UI program changes can
overlap rendering.

The engine tests pass under AddressSanitizer, UndefinedBehaviorSanitizer and
ThreadSanitizer. That doesn't replace real DAW testing with the editor open,
automation running and several instances.
