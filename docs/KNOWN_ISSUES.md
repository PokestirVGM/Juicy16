# Beta 1 known issues and unverified areas

This file describes the unreleased `0.5.0-beta.1` development state. It must be regenerated for the exact frozen candidate.

## Stop-ship/open gates

- Product identity, support contacts, minimum operating systems, released architectures, and JUCE 8 licensing/distribution authority are not approved.
- The current local macOS artifact is arm64 and inherited a macOS 26.0 minimum; it is not a broadly compatible release candidate.
- The AU has not yet passed `auval` or the Logic/additional-AU-host matrix.
- Cubase, FL Studio, and an additional VST3 host have not yet run the exact packaged candidate through the canonical 16-channel game-rip fixture.
- The Windows MSVC/CI pipeline, clean-machine dependency check, DLS capability, and host matrix remain unproven. The legacy LLVM-MinGW Docker path is unsupported.
- A licensed SF2/SF3/DLS/malformed-DLS compatibility corpus is not yet available in the repository.
- Developer ID signing, notarization policy, packaging, clean-machine installation, and uploaded checksum verification are incomplete.

## Intentional limitations

- One stereo output for all 16 channels.
- Standalone is a development/QA target, not a primary Beta format.
- VST2, AUv3, Linux, and 32-bit Windows are outside the current Beta scope.
- VST3 `progChN` parameters expose program 0–127 only; arbitrary bank changes still require MIDI Bank Select CC0/32 before Program Change.
- Audible pressure/CC behavior depends on modulators in the loaded bank.
- FluidSynth exposes pitch-wheel sensitivity to this test harness as whole semitones. RPN 0,0 MSB ranges and RPN Null are verified; cents-level Data Entry LSB behavior still requires the audio-domain fixture before it is claimed.
- DLS repair covers selected RIFF-size inconsistencies only, never arbitrary corrupted instrument/sample data.

## Current automated evidence

Debug and statically linked Release suites pass on arm64 macOS with DLS repair/load, sample-offset MIDI, mono/stereo, 16-channel Program Change, reset chase, exhaustive CC forwarding traces, exact pitch-bend values, RPN bend ranges, pressure traces, state migration/bounds/fuzz, failed transactional replacement, VST3 discovery/mapping, and release metadata. Separate ASan+UBSan and TSan harness runs passed. This evidence does not replace DAW or clean-system validation.
