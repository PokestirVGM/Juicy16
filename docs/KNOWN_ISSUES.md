# Beta 1 known issues and unverified areas

This file describes the unreleased `0.5.1-alpha.1` development state. It must be regenerated for the exact frozen candidate.

## Stop-ship/open gates

- Product identifiers and the Beta 1 platform matrix are approved and implemented, but the renamed artifacts still require candidate-specific metadata and session-recall validation.
- JUCE 8 will be used under AGPLv3 with the inherited application code remaining GPLv3; exact source packaging, notices, ownership language, and dependency inventory still require qualified review before public distribution.
- Homebrew's current static dependency set was compiled for macOS 26 and is rejected by strict validation. The checksum-pinned source recipe now produces a macOS 11 arm64 closure with SF3 and native DLS enabled; clean-environment reproduction and final package review remain required.
- A correctly built macOS 11-targeted arm64 artifact still needs runtime validation on both macOS 11 and the current macOS release. Intel macOS is intentionally deferred.
- The current strict-Release AU passes `auval -strict` on macOS 26.5.2, both as built and as extracted from its package. Logic, an additional AU host, and macOS 11 remain untested.
- Cubase, FL Studio, and an additional VST3 host have not yet run the exact packaged candidate through the canonical 16-channel game-rip fixture.
- The Windows MSVC/CI pipeline, clean-machine dependency check, DLS capability, and host matrix remain unproven. The legacy LLVM-MinGW Docker path is unsupported.
- A local private SF2/DLS/malformed-DLS corpus and FluidSynth's licensed upstream SF3 fixture pass the strict arm64 macOS loader. Private-bank redistribution rights, Windows results, and final-candidate results remain unresolved.
- Developer ID signing, notarization policy, packaging, clean-machine installation, and uploaded checksum verification are incomplete.

## Intentional limitations

- One stereo output for all 16 channels.
- FluidSynth 2.5.5 has a 96 kHz sample-rate ceiling. Juicy16 is pitch-verified at 44.1, 48, 88.2, and 96 kHz and intentionally outputs silence at higher host rates rather than rendering at the wrong pitch. Change the project/device rate to 96 kHz or lower.
- Standalone is a development/QA target, not a primary Beta format.
- Intel macOS, Windows ARM64, VST2, AUv3, Linux, and 32-bit Windows are outside the current Beta scope.
- VST3 `progChN` parameters expose program 0–127 only; arbitrary bank changes still require MIDI Bank Select CC0/32 before Program Change.
- Audible pressure/CC behavior depends on modulators in the loaded bank.
- General MSB/LSB controller pairs are delivered exactly, but FluidSynth normally uses only the 7-bit MSB for synthesis; documented exceptions and channel-mode limits are listed in [CONTROLLER_SUPPORT.md](CONTROLLER_SUPPORT.md).
- FluidSynth exposes pitch-wheel sensitivity to this test harness as whole semitones, but that is a limit of the diagnostic accessor, not the engine: cents-level Data Entry LSB in RPN 0,0 is honoured and is now verified in the audio domain. RPN 0,0 MSB ranges and RPN Null are verified.
- DLS repair covers selected RIFF-size inconsistencies only, never arbitrary corrupted instrument/sample data. Banks larger than 512 MB are never repaired, and an unrepaired bank whose RIFF header claims more data than the file holds is rejected outright rather than handed to FluidSynth, whose parser can otherwise stall for minutes on such a file.
- The interface has one fixed appearance drawn from JUCE's default colour scheme. It does not follow the system light/dark setting.

## Current automated evidence

Debug and statically linked Release suites pass on arm64 macOS with DLS repair/load, sample-offset MIDI, mono/stereo, 16-channel Program Change, reset chase, exhaustive CC forwarding traces, exact pitch-bend values, RPN bend ranges, pressure traces, state migration/bounds/fuzz, failed transactional replacement, VST3 discovery/mapping, and release metadata. Separate ASan+UBSan and TSan harness runs passed. This evidence does not replace DAW or clean-system validation.
