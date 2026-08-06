# Engine threading model

This policy describes which JuicySF Rack state may cross the audio/message-thread boundary. Changes to engine state must preserve it and update the offline and ThreadSanitizer tests when appropriate.

## Audio-thread rules

`processBlock`, MIDI dispatch, rendering, and audio-thread parameter callbacks may call FluidSynth and update fixed-size atomics. They must not allocate unbounded storage, touch `ValueTree`, manipulate UI components, load/unload banks, create/delete temporary files, or issue synchronous host/UI notifications.

MIDI-driven program changes and the six exposed sound controllers write `midi*`/`engine*` atomics and trigger `AsyncUpdater`. The message thread consumes dirty masks and mirrors only the latest state into parameters and `channelPrograms`. The audio engine never waits for that mirror before rendering.

The mono render path uses scratch memory allocated in `prepareToPlay`; an unexpectedly larger host block is rendered in bounded chunks. Diagnostic MIDI trace arrays are fixed-size atomics and add no allocation.

## Message-thread rules

The message thread owns UI and `ValueTree` mutation, bank list reconstruction, file/bookmark operations, DLS temporary-file lifetime, and bank replacement. Manual program selection is expected on this thread. `mirroringParameters` is thread-local so message-thread synchronization cannot suppress unrelated audio-thread automation.

`prepareToPlay` may recreate the FluidSynth instance and reload state before playback begins. Synth pointer replacement must not occur concurrently with an active `processBlock` call.

## Shared state

- Selected UI channel, loaded SoundFont ID, engine bank/programs, mirrored CCs, dirty masks, and test traces are atomic.
- FluidSynth's `synth.threadsafe-api` setting is explicitly enabled because occasional message-thread bank/program calls can overlap rendering. Those operations may briefly contend on FluidSynth's internal serialization and must never be introduced as a recurring per-block UI task.
- Program-name callbacks and bank `ValueTree` data remain message-thread-only.
- The active repaired temporary file remains message-thread-owned; the atomic font ID is published only after a candidate bank is validated.
- `synth` and `settings` ownership is not atomic. They are constructed/destroyed only during initialization, teardown, or pre-play sample-rate preparation.

## Validation

The engine harness exercises concurrent-domain handoff patterns without directly mutating `ValueTree` from the audio callback. On 2026-08-05 it passed AddressSanitizer plus UndefinedBehaviorSanitizer, and separately ThreadSanitizer, on arm64 macOS. These harness results do not replace DAW stress tests involving editor open/close, automation, repeated bank loads, and multiple plugin instances.
