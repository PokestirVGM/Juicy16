# Engine threading model

This policy describes which Juicy16 state may cross the audio/message-thread boundary. Changes to engine state must preserve it and update the offline and ThreadSanitizer tests when appropriate.

## Audio-thread rules

`processBlock`, MIDI dispatch, rendering, and audio-thread parameter callbacks may call FluidSynth and update fixed-size atomics. They must not allocate unbounded storage, touch `ValueTree`, manipulate UI components, load/unload banks, create/delete temporary files, or issue synchronous host/UI notifications.

MIDI- and host-driven program changes and the six exposed sound controllers write `midi*`/`engine*` atomics and trigger `AsyncUpdater`. Every program entry point uses `applyProgramToEngine`; failed applications set a per-channel diagnostic bit without publishing rejected state. The message thread consumes dirty masks and mirrors only the latest accepted state into parameters and `channelPrograms`. The audio engine never waits for that mirror before rendering.

GM, GS, and XG reset SysEx is handled entirely in timestamp order on the audio thread. Immediately after FluidSynth processes the reset, the model reapplies the latest engine program and exposed-controller atomics before dispatching the next MIDI event. Reset itself does not queue a full-state message-thread resync, so it cannot later overwrite a newer same-block Program Change.

The mono render path uses scratch memory allocated in `prepareToPlay`; an unexpectedly larger host block is rendered in bounded chunks. Diagnostic MIDI trace arrays, including the program snapshot captured immediately before each note-on, are fixed-size atomics and add no allocation.

SysEx is dispatched directly from the `MidiBuffer`'s storage through `FluidSynthModel::dispatchSysEx`, because `MidiMessage` heap-copies anything longer than four bytes and game rips carry a GM/GS/XG reset at tick 0. The payload passed to that function excludes the `0xF0`/`0xF7` framing, matching `MidiMessage::getSysExData()`. Messages of four bytes or fewer keep the inline, non-allocating `MidiMessage` route. Allocation remaining on the audio thread is internal to FluidSynth.

## Message-thread rules

The message thread owns UI and `ValueTree` mutation, bank list reconstruction, file/bookmark operations, DLS temporary-file lifetime, and bank replacement. Manual program selection is expected on this thread. `mirroringParameters` is thread-local so message-thread synchronization cannot suppress unrelated audio-thread automation.

`prepareToPlay` may recreate the FluidSynth instance and reload state before playback begins. It validates the requested rate against FluidSynth's registered range first. An unsupported rate publishes an atomic fail-safe flag; `processBlock` then clears the output without calling the stale-rate synth. Returning to a supported rate recreates the synth and reloads state. Synth pointer replacement must not occur concurrently with an active `processBlock` call.

## Shared state

- Selected UI channel, loaded SoundFont ID, sample-rate support status, engine bank/programs, mirrored CCs, dirty/error masks, and test traces are atomic.
- FluidSynth's `synth.threadsafe-api` setting is explicitly enabled because occasional message-thread bank/program calls can overlap rendering. Those operations may briefly contend on FluidSynth's internal serialization and must never be introduced as a recurring per-block UI task.
- Program-name callbacks and bank `ValueTree` data remain message-thread-only.
- The active repaired temporary file remains message-thread-owned; the atomic font ID is published only after a candidate bank is validated.
- `synth` and `settings` ownership is not atomic. They are constructed/destroyed only during initialization, teardown, or pre-play sample-rate preparation.

## Validation

The engine harness exercises concurrent-domain handoff patterns without directly mutating `ValueTree` from the audio callback. On 2026-08-05 it passed AddressSanitizer plus UndefinedBehaviorSanitizer, and separately ThreadSanitizer, on arm64 macOS. These harness results do not replace DAW stress tests involving editor open/close, automation, repeated bank loads, and multiple plugin instances.
