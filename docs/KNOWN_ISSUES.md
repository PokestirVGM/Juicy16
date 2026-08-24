# Beta 1 known issues and unverified areas

This file describes the unreleased `0.6.0-alpha.2` development state. It must be regenerated for the exact frozen candidate.

## Debug-only: unregistered LookAndFeel colour IDs

Constructing the editor produces 63 `jassertfalse` hits at
`juce_LookAndFeel.cpp:94` — `LookAndFeel::findColour` for a colour ID that was
never registered, which returns black. **Debug builds only**; release builds
compile the assertion out.

What is established:

- It is editor-specific. A `--game-rip` run, which never builds an editor,
  produces zero.
- It has no visible effect. Every surface was inspected in the running plugin at
  minimum, default and 1000px widths and rendered correctly, so whatever is
  asking for the colour is not drawing with the black it gets back.
- **The majority were Juicy16's own, and are fixed.** A cell or panel component
  built before it is parented resolves colours against the DEFAULT LookAndFeel,
  which has never heard of Juicy16's ColourIds — so it asserted and returned
  black. That is what made a lit mute button draw as a blank box. Those lookups
  moved into `lookAndFeelChanged()`, which is the hook that fires once the real
  LookAndFeel is in place, and the count fell from 138 to 63.
- `DrawableButton`'s four colour IDs were a plausible cause for the remainder —
  `LookAndFeel_V4` does not initialise them and the header's settings button is
  one — but registering them changed the count by zero. They are set anyway,
  because a themed editor should define them.

The remaining 63 are not yet identified and are most likely stock JUCE IDs that
`LookAndFeel_V4::initialiseColours` never sets. Worth attaching a debugger to
`LookAndFeel::findColour` and reading `colourID` on the first hit.

## Reverb

- **Old projects will sound different.** Juicy16 discarded FluidSynth's effects
  buses until 0.6.0-alpha.1, so its reverb was never audible. It is now mixed in,
  and a project whose MIDI sends CC91 will have reverb where it previously had
  none. That is the file being played as written, but it is an audible change to
  existing work.
- **A MIDI file cannot change your reverb settings.** GS and XG reverb macro
  SysEx is deliberately ignored, so a rip asking for a hall gets whatever profile
  you selected. Report a rip that sounds wrong in a way the manual controls
  cannot fix.
- **Channels start at the GM default reverb send (CC91 = 40).** FluidSynth
  initialises it to 0, which made every reverb control inert on any file that
  did not explicitly ask for reverb. A file's own CC91 still overrides it.
- **Some tracks' volume or attack may sound different from other players.**
  Reported on 0.6.0-alpha.2 and not fully explained. What has been ruled out by
  measurement against stock FluidSynth on the same bank: velocity response
  (within 0.3 dB), CC7 channel volume (within 0.22 dB), CC10 pan (exact), and
  the amplitude envelope — attack time is identical at 37.7 ms and unaffected by
  CC73, so the removed CC71-79 modulators are genuinely gone. Two candidates
  remain:
  1. **Reverb was on by default in 0.6.0-alpha.2**, at the GM default send. It
     was the first Juicy16 release in which the reverb was audible at all, and
     reverb both raises perceived level unevenly between instruments and smears
     attacks. It is **off by default** from the next build, which should settle
     this one way or the other.
  2. **Chorus is off** (below), while stock FluidSynth and most other players
     have it on. A file that sends CC93 will sound thinner here than elsewhere.
  **Update (2026-08-23), measured against the owner's own 24-rip VGMTrans
  corpus:** neither candidate holds, and no Juicy16-specific cause was found.
  Rendering `SEQ_BGM_VS_GYMLEADER` through stock FluidSynth 2.5.5 at matched
  settings gives RMS -19.82 dBFS against Juicy16's -19.79 — a 0.03 dB
  difference. The same bank exported as DLS and as SF2 renders within 0.15 dB.
  Across all 24 rips the corpus sends CC7 8342 times, CC10 15977 and CC11 1929,
  but CC91 only 8 times and **CC93 not once**, so neither reverb nor chorus is
  involved in how this material sounds.

  Juicy16 therefore reproduces FluidSynth faithfully, and any remaining
  difference is FluidSynth's interpretation of these banks rather than
  something this plugin does to them — most likely its CC7/CC11 attenuation
  curve and its reading of DLS articulation, against PlayStation-era material
  whose original hardware behaved differently. That is a real gap, but it is a
  deliberate-deviation question, not a defect to fix.

  **Update 2, after the owner reported this with certainty against Fruity LSD.**
  Fruity LSD plays DLS through DirectMusic, not FluidSynth, so the comparison is
  FluidSynth's DLS interpretation against Microsoft's. Ruled out so far, by
  measurement on the owner's VGMTrans corpus:

  - Per-instrument level is identical between each bank's DLS and SF2 export
    (19 programs, none differing by more than 1 dB), so it is not an export
    artefact.
  - These DLS files declare no attenuation anywhere: every region `wsmp`
    attenuation is 0 dB, there are no instrument-level `CONN_DST_GAIN` blocks,
    and the wave pool carries no `wsmp` chunks at all.
  - FluidSynth does honour DLS `EG1_SUSTAIN`: instruments VGMTrans declares at
    roughly 0% sustain render at about 1%, and those declared at 100% render at
    94-102%.
  - CC7 tracks the DLS specification's own 40·log10(cc/127) curve within about
    0.3 dB, so the volume-controller curve is not the difference.

  **The open lead**: several instruments render well below their declared
  sustain - program 17 declared 99.7% renders at 66%, program 49 declared 100%
  renders at 70%, program 100 declared 94% renders at 57%. Some of that is
  one-shot samples ending naturally, which is correct, but a sustained
  instrument landing 30 points low would put it wrong against the rest of a mix
  while its neighbours are fine. This needs a specific instrument to confirm
  rather than a survey.

  **Update 3 — isolated, 2026-08-23.** The owner named the case: in
  `SEQ_BGM_N_CASTLE` the vibraphone and the timpani are too quiet, while other
  parts are fine. Measured on that bank:

  | program | declared EG1 decay | rendered to -20 dB |
  |---|---|---|
  | 10 (vibraphone) | 2.947 s | **0.442 s** |
  | 14 (timpani) | 2.947 s | **0.603 s** |
  | 38 (bass) | 4.846 s | 1.010 s |
  | 30, 63, 75 (sustaining) | n/a, sustain 99-100% | 1.1-1.8 s, correct |

  The split is exact: **every instrument VGMTrans declares with ~0% sustain
  decays far too fast, and every instrument that sustains is fine.** That is
  why some parts sound right and the struck ones do not.

  The cause is envelope-curve interpretation, not a missing value. FluidSynth
  ramps the volume envelope linearly in DECIBELS across the declared decay time,
  so an instrument declared to decay over 2.95 s is 20 dB down in 0.61 s - and
  programs 14 and 38 match that model to within a millisecond, which is how we
  know that is what it is doing. A PlayStation SPU envelope, which is what
  VGMTrans transcribed, decays exponentially in AMPLITUDE and stays audible far
  longer. Fruity LSD plays these through DirectMusic, which is closer to the
  original, which is why the owner does not hear it there.

  DLS and SF2 exports of the same bank behave identically, so switching format
  is not a workaround.

  **This is FluidSynth's DLS envelope conversion, not a Juicy16 defect** - the
  plugin reproduces FluidSynth exactly, as measured. Making struck instruments
  ring correctly means deliberately deviating from FluidSynth's envelope, which
  is a product decision rather than a bug fix, and is not taken here without the
  owner's call. Options if it is taken: correct the decay generator per voice on
  DLS banks, or carry a patched FluidSynth.
- **Chorus does nothing.** It was discarded by the same bug and is now switched
  off explicitly rather than un-muted with defaults nobody chose. CC93 still
  reaches the engine. Chorus will get controls of its own in a later release.
- **Nobody has listened to the reverb yet.** Its defaults were chosen by
  measurement against dry material, not by ear. Tester reports on how the
  profiles actually sound on real rips are especially useful.
- **The performance envelope has not been re-agreed** for a build that mixes an
  extra stereo bus per block. The baseline test passes, but the figure it passes
  against predates the reverb.


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
