# Beta 1 known issues and unverified areas

This file describes the unreleased `0.5.1-alpha.6` development state. It must be regenerated for the exact frozen candidate.

## Stop-ship/open gates

- Product identifiers and the Beta 1 platform matrix are approved and implemented, but the renamed artifacts still require candidate-specific metadata and session-recall validation.
- JUCE 8 is used under AGPLv3 with the inherited application code remaining GPLv3. Qualified external review was **waived by owner decision on 2026-08-23**; the owner self-reviews source packaging, notices, ownership language, and the dependency inventory against [LICENSING.md](LICENSING.md) before candidate freeze, with the repository going public at the beta tag to satisfy the corresponding-source offer. Not yet performed against a frozen package.
- Homebrew's current static dependency set was compiled for macOS 26 and is rejected by strict validation. The checksum-pinned source recipe now produces a macOS 11 arm64 closure with SF3 and native DLS enabled; clean-environment reproduction and final package review remain required.
- A correctly built macOS 11-targeted arm64 artifact still needs runtime validation on both macOS 11 and the current macOS release. Intel macOS is intentionally deferred.
- The current strict-Release AU passes `auval -strict` on macOS 26.5.2, both as built and as extracted from its package. Logic, an additional AU host, and macOS 11 remain untested.
- Cubase, FL Studio, and an additional VST3 host have not yet run the exact packaged candidate through the canonical 16-channel game-rip fixture.
- The Windows MSVC/CI pipeline, clean-machine dependency check, DLS capability, and host matrix remain unproven. The legacy LLVM-MinGW Docker path is unsupported.
- A local private SF2/DLS/malformed-DLS corpus and FluidSynth's licensed upstream SF3 fixture pass the strict arm64 macOS loader. Private-bank redistribution rights, Windows results, and final-candidate results remain unresolved.
- Developer ID signing and notarization are **out of scope for Beta 1** by an approved decision; the gate is now the tester-facing Gatekeeper procedure, which is published. Packaging, clean-machine installation, and uploaded checksum verification remain incomplete.

- **REMEDIATED — CVE-2025-52194 in libsndfile 1.2.2.** A buffer overflow in `ircam_read_header`, reachable because FluidSynth passes a SoundFont's embedded sample bytes to libsndfile's automatic format detection without pre-validating the format and only *warns* when the result is not OGG — so a crafted `.sf3`, the plugin's primary untrusted input, could route into the vulnerable reader. Upstream has no release carrying the fix, so `vendor/libsndfile_patched/` backports it and both dependency recipes bracket the edit with pre- and post-edit `src/ircam.c` hashes. As of 2026-08-23 the patched closure is **built and linked**, not merely specified: the strict portable Release gate passed 15/15 against it, including the SF3 load path that reaches libsndfile. No proof-of-concept existed before or after, so this closes a reachable code path rather than disproving a demonstrated exploit. The related MPEG advisories are not reachable, because `ENABLE_MPEG=OFF` keeps that code out of the binary.

## Intentional limitations

- One stereo output for all 16 channels.
- FluidSynth 2.5.5 renders no higher than 96 kHz. Above that Juicy16 renders at an integer fraction of the host rate and interpolates each block up, so a 192 kHz project plays; MIDI event timing then quantises to one internal sample (about 10 microseconds at 96 kHz) instead of one host sample. Below FluidSynth's 8 kHz floor there is no equivalent path and playback is still muted rather than detuned.
- A drum channel's bank reaches 255, because FluidSynth adds its 128 drum offset to the Bank Select MSB. That is the number the engine, the UI, the host parameter, and the saved state all report; the font itself still defines only banks 0-128, so the kit heard on such a bank is FluidSynth's substitution.

- Beta 1 is **ad-hoc signed by decision**, not Developer ID signed and not notarized. Every macOS install therefore requires clearing the quarantine attribute; see [BETA_TESTER_GUIDE.md](BETA_TESTER_GUIDE.md). The `ADHOC` label in the package filename is expected for Beta 1 and is not a disqualifier. `LOCAL-DIRTY` still is.
- Standalone is a development/QA target, not a primary Beta format.
- Intel macOS, Windows ARM64, VST2, AUv3, Linux, and 32-bit Windows are outside the current Beta scope.
- VST3 `progChN` parameters expose program 0–127 only; arbitrary bank changes still require MIDI Bank Select CC0/32 before Program Change.
- **B2** — Selecting a bank/program the loaded bank file does not define shows the requested patch while a different one sounds. FluidSynth 2.5.5 accepts the change, records the requested bank and program on the channel, and substitutes bank 0 program 0 for synthesis. Juicy16 keeps the requested value in the editor and saved state on purpose, so reopening the project with the intended bank plays what the MIDI asked for; the disagreement lasts only while the wrong bank is loaded. Verified by the offline cross-bank fixture.
- The pinned SF3 test fixture defines only bank 0 and percussion bank 128, so SF3 Bank Select is proven for percussion-versus-melodic selection only. Cross-bank melodic selection is proven on SF2 and DLS.
- Audible pressure/CC behavior depends on modulators in the loaded bank.
- General MSB/LSB controller pairs are delivered exactly, but FluidSynth normally uses only the 7-bit MSB for synthesis; documented exceptions and channel-mode limits are listed in [CONTROLLER_SUPPORT.md](CONTROLLER_SUPPORT.md).
- FluidSynth exposes pitch-wheel sensitivity to this test harness as whole semitones, but that is a limit of the diagnostic accessor, not the engine: cents-level Data Entry LSB in RPN 0,0 is honoured and is now verified in the audio domain. RPN 0,0 MSB ranges and RPN Null are verified.
- DLS repair covers selected RIFF-size inconsistencies only, never arbitrary corrupted instrument/sample data. Banks larger than 512 MB are never repaired, and an unrepaired bank whose RIFF header claims more data than the file holds is rejected outright rather than handed to FluidSynth, whose parser can otherwise stall for minutes on such a file.
- The interface has one fixed appearance drawn from JUCE's default colour scheme. It does not follow the system light/dark setting.
- CC71, 72, 73, 74, 75 and 79 are delivered to the engine and reported by the diagnostics, but they change nothing. Juicy16's own modulators for them were removed in `0.5.1-alpha.5`; stock FluidSynth ignores those controllers, so this now matches every other SoundFont player. A rip that sends them sounds the same here as elsewhere, which is the point.
- A saved project from `0.5.1-alpha.5` or earlier restores its bank, per-channel instruments and window state, but not the six retired sound-controller values — those channels open at the GM defaults (volume 100, pan centre). Pre-Beta session compatibility is explicitly out of scope.
- Keyboard operation is not verified against a real screen reader or host focus chain. Every core workflow is reachable without a mouse — Tab reaches the bank browse button, the channel table, and all six sound sliders; up/down arrows on the table select a MIDI channel; Return opens that channel's instrument dropdown — and the headless editor suite pins each of those. What is untested is what VoiceOver or Narrator announces, and whether a given host passes Tab through to the plugin editor at all, since both need a real window.

## Current automated evidence

A randomised MIDI soak covers the input path the file and state fuzzers do not: 40 seeds of 200,000 blocks — roughly 156 million MIDI events, about 15.6 million of them SysEx — with zero failures, plus a 40,000-block run under ASan+UBSan that rendered 782,123 events with no sanitizer findings. It asserts finite bounded audio, in-range program and pitch-bend state on all 16 channels, the 512-voice ceiling, serialisable saved state, and that All Sound Off leaves nothing running. It found both B2 defects above.

Debug and statically linked Release suites pass on arm64 macOS with DLS repair/load, sample-offset MIDI, mono/stereo, 16-channel Program Change, cross-bank Bank Select and bank offsets, the 512-voice ceiling, reset chase, exhaustive CC forwarding traces, exact pitch-bend values, RPN bend ranges, pressure traces, state migration/bounds/fuzz, failed transactional replacement, in-process VST3 and AU host harnesses, and release metadata. Separate ASan+UBSan and TSan harness runs passed. This evidence does not replace DAW or clean-system validation.
