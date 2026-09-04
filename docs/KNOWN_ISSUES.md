# Beta 1 known issues and unverified areas

Known limitations and unverified areas in `0.6.0-beta.1`, the Beta 1 release. Read this before reporting a bug: several entries below are deliberate, and one of them is probably what you are hearing.

## Debug-only: LookAndFeel assertions

Debug builds emit assertion messages from `juce_LookAndFeel.cpp` when the editor
is constructed — a colour ID that was never registered, which returns black.
Release builds compile these out and no surface is affected: every part of the
interface was inspected in the running plugin. Cosmetic and unresolved.

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
- **Juicy16 plays about 8.6 dB quieter than VGMTrans**, was about 10.1 dB before
  `0.6.1-beta.3`. The master trim's own +1.5 dB default was never reaching the
  audio; that is fixed, and the rest is deliberate. The offset is uniform -
  relative balance between instruments matches BASSMIDI within 0.66 dB, and
  velocity, CC7 and CC11 curves all match within 0.26 dB - but a uniform deficit
  still reads as "the quiet instruments are too quiet", because the loud ones
  stay prominent and the quiet ones fall away. It also reads as dullness: the
  same material 10 dB down loses apparent top and bottom before it loses
  midrange.

  Juicy16 uses FluidSynth's documented `synth.gain` default of 0.2, chosen in
  0.5.1-alpha.5 after gain 1.0 clipped a real rip at +7.32 dBFS. **VGMTrans gets
  its extra level by running past full scale**, which is why matching it is not
  simply a matter of turning Juicy16 up. Measured across ten rips at 48 kHz,
  decoded to float so overs are preserved rather than clipped:

  | rip | Juicy16 peak | BASSMIDI peak | RMS gap |
  | --- | ---: | ---: | ---: |
  | `SEQ_GS_ENDING` | -2.69 | **+7.61** | 10.28 |
  | `SEQ_BGM_VS_ACHROMA` | -3.24 | **+6.41** | 10.22 |
  | `SEQ_BGM_VS_GYMLEADER` | -4.78 | **+5.55** | 10.28 |
  | `SEQ_BGM_C_03` | -6.46 | **+3.50** | 10.22 |
  | `SEQ_CITY05_D` | -7.94 | **+1.87** | 10.09 |
  | `BGM_02` | -8.53 | **+1.50** | 10.24 |
  | `SEQ_BGM_N_CASTLE` | -10.24 | -0.18 | 10.26 |
  | `SEQ_ROAD_D_D` | -10.40 | -0.09 | 10.09 |
  | `Majestic Castle` | -11.50 | -5.40 | 6.12 |
  | `BGM_SHIRO` | -14.77 | -4.44 | 10.27 |

  BASSMIDI is over full scale on six of the ten. Reproducing its loudness would
  reproduce that clipping, so closing the remaining gap needs a limiter rather
  than a larger number, and that is deferred - see `MILESTONE_PLAN.md`.
  Workaround today: raise the master output trim, which spans -24 to +12 dB.
  (Juicy16 figures are at the default trim as it shipped in Beta 1; the fix
  above moves each of them 1.5 dB louder.)
- **Rips sounded slightly dull — partly fixed in 0.6.1-beta.3, not yet re-heard
  in a DAW.** Every rip was playing at FluidSynth's 4th-order interpolation
  instead of the 7th-order Juicy16 asks for, because a GM/GS/XG reset SysEx
  resets the method on all 16 channels and every VGMTrans rip opens with one.
  Restoring it puts 0.2–0.6 dB back into 4–8 kHz and takes about 0.5 dB of
  imaging noise off the top, on banks whose samples are stored well below the
  host rate. It changes nothing on a bank already sampled near 44 kHz. The
  remainder of any dullness is the level deficit above: material played 8–10 dB
  quieter loses apparent top and bottom before it loses midrange, which is a
  property of hearing rather than of the filter chain.
- **Some material sounds different from other players.** Juicy16 reproduces
  FluidSynth faithfully — measured within 0.03 dB RMS of stock FluidSynth on the
  same rip, and its whole-file spectrum matches stock FluidSynth band for band
  within 0.1 dB, with velocity, CC7, CC11, per-instrument balance and the volume
  envelope all matching VGMTrans's own BASSMIDI within 0.7 dB. What differs is
  overall level, above. If something still sounds wrong to you, a report naming
  the file, the channel and the instrument is far more useful than a general
  impression, because that can be measured.

- **Chorus does nothing.** It was discarded by the same bug and is now switched
  off explicitly rather than un-muted with defaults nobody chose. CC93 still
  reaches the engine. Chorus will get controls of its own in a later release.
- **Nobody has listened to the reverb yet.** Its defaults were chosen by
  measurement against dry material, not by ear. Tester reports on how the
  profiles actually sound on real rips are especially useful.
- **The performance envelope has not been re-agreed** for a build that mixes an
  extra stereo bus per block. The baseline test passes, but the figure it passes
  against predates the reverb.


## Pitch bend

- **A reset SysEx no longer returns the bend range to two semitones on a
  channel the file has configured.** A GM/GS/XG reset re-asserts the range the
  MIDI stream last set on that channel, cents included, exactly as it
  re-asserts programs, volume and pan. The reason is the same as for programs:
  under VST3 the RPN controllers are host parameters, the host's cache still
  holds them from the first play, and it never sends them again — so with
  spec-exact behaviour the tick-0 reset put every replay back to two semitones.
  The trade-off: play a rip that asks for 12 semitones, then in the same plugin
  instance play a file that sends no RPN and relies on the reset's default of
  two, and the second file bends 12. Reload the plugin, or set the bend range
  override for that file. A channel the first file never configured still
  resets to two.
- **What a VST3 host loses cannot all be rebuilt.** Events sharing one
  timestamp are re-ordered by role, and the RPN machinery is reconstructed from
  each controller's own queue order, which is the one thing VST3 preserves. Two
  things it does not survive: a host that keeps only the last of several values
  of one controller at one sample offset, and several RPN blocks on one tick
  where only some carry a Data Entry LSB (the LSBs pair with the earliest
  blocks). Neither pattern appears in the test corpus. A group of more than
  2048 events at one timestamp is dispatched in the host's order.
- **FL Studio squashes imported bends.** FL imports every MIDI pitch bend as
  plus or minus two semitones regardless of the file's RPN, and its wrapper only
  sends a bend range when asked. The settings popover has two compensations,
  both off by default: *Bend scale* multiplies incoming bends (×6 for a rip
  written for 12 semitones), and *Bend range override* forces one range on all
  channels for a host that drops the RPN altogether. Which one FL needs, and at
  what value, has not been tried in FL: report what worked.

## Per-channel loudness

- **Echo channels loud from the second play on — fixed in 0.6.1-beta.2, not
  yet re-heard in a DAW.** A rip's echo channel is often quieter than its
  melody only through CC11 expression (BGM_3C: echo 74, melody 104, identical
  notes and velocities). Cubase and FL send Reset All Controllers on stop,
  which returns expression to 127, and a VST3 host's parameter cache never
  resends an unchanged CC11 — so every play after the first had the echo at
  full level. Both resets now re-assert the expression the stream last set on
  that channel, as they already re-assert programs, CC7, CC10 and the bend
  range. **Trade-off:** a file that sends CC121 itself to return expression to
  127 mid-song gets its previous expression back instead. No file in the
  4,146-rip corpus sends CC121.
- **A channel loud from the very first play is not this.** Velocity, CC7 and
  CC11 all measured correct in the engine, in the plugin's processing path and
  through the built VST3 — a CC7-attenuated echo (SEQ_BGM_C_03: 45 against 97)
  sits 13.4 dB under its melody in every one of them, first play and replay.
  If it does not in a DAW, the CC7 is not arriving, and the row's Vol knob
  will say so.

What is known, and what a useful report contains:

- Velocity, CC7 and CC11 curves match VGMTrans's BASSMIDI within 0.26 dB and
  per-instrument balance within 0.66 dB, on SF2. FluidSynth 2.5.5's native DLS
  loader applies the same volume, expression, velocity and pan modulators to
  DLS banks, so a DLS is not exempt from that measurement.
- An incoming CC7 or CC10 moves that channel's row knob at the event's
  timestamp, and a reset SysEx re-asserts the latest values. If a row's knob
  does **not** show the value the file sent, that is a plugin defect: say so.
- Things on the host side that change a channel's volume without the file
  asking: in Cubase, a MIDI track's inspector *Volume* and *Pan* (CC7/CC10 when
  not "Off"), and the MIDI-file import preferences *Extract First Volume/Pan*
  and *Extract First Patch*, which move a file's first CC7/CC10/Program Change
  off the part and onto the track; in FL Studio, whether an imported file's
  CC7/CC11 reach the plugin at all depends on how the tracks were routed.
- A useful report names the file, the bank, the channel and instrument, the
  host and format, what the row's Vol knob shows at the moment it sounds wrong,
  and what "should be" is measured against (VGMTrans, Fruity LSD, a recording).
  With that, the difference can be rendered offline and measured.

## What is and is not validated

Beta 1 ships with these tested, on the packaged artifacts:

- Per-channel Bank Select and Program Change on all 16 channels, in **FL Studio
  and Cubase**, in both **AU and VST3**.
- Pitch-bend range through RPN, confirmed in Cubase on 2026-08-24 — then
  reported wrong again by testers. `0.6.1` fixes three defects behind that
  (see *Pitch bend* above and `CHANGELOG.md`), verified offline; the Cubase
  re-confirmation is still to do.
- `auval -strict` on the AU, both as built and as extracted from the package.
- The strict portable Release suite, 15/15, from a clean checkout: arm64-only,
  `minos 11.0` on every Mach-O, no developer-only dynamic library, SF2/SF3/DLS
  loading, and the 16-channel VST3 unit and program-list structure.

These are **not** validated, and a report about any of them is genuinely new
information:

- **macOS 11 itself.** Every binary declares `minos 11.0` and strict validation
  proves it, but the only Mac available runs macOS 26. Nothing has booted this on
  11. That is why the claim is written as "built for 11.0+, validated on current
  macOS".
- **Logic Pro, and any AU host other than FL Studio.** Logic is not owned. AU
  coverage is `auval -strict` plus FL Studio.
- **Any VST3 host other than FL Studio and Cubase.**
- **Intel Macs.** Out of scope, not built.
- **Clean-machine installation.** Every install so far has been on the
  development machine, which already has the dependencies a user will not have.
  The plugin links its dependencies statically and validation checks for exactly
  this, but "checked" is not "someone did it".
- **Windows.** Not part of Beta 1 at all; see below.
- **Redistribution rights for the private bank corpus.** The compatibility corpus
  is local and is never packaged.

## Licensing

JUCE 8 is used under AGPLv3, with the inherited application code remaining
GPLv3. Qualified external legal review was **waived by owner decision on
2026-08-23** in favour of an owner self-review against
[LICENSING.md](LICENSING.md), on the basis of a small named tester list and
public source at the beta tag. That is a recorded risk taken deliberately, not
an oversight.

- **REMEDIATED — CVE-2025-52194 in libsndfile 1.2.2.** A buffer overflow in `ircam_read_header`, reachable because FluidSynth passes a SoundFont's embedded sample bytes to libsndfile's automatic format detection without pre-validating the format and only *warns* when the result is not OGG — so a crafted `.sf3`, the plugin's primary untrusted input, could route into the vulnerable reader. Upstream has no release carrying the fix, so `vendor/libsndfile_patched/` backports it and both dependency recipes bracket the edit with pre- and post-edit `src/ircam.c` hashes. As of 2026-08-23 the patched closure is **built and linked**, not merely specified: the strict portable Release gate passed 15/15 against it, including the SF3 load path that reaches libsndfile. No proof-of-concept existed before or after, so this closes a reachable code path rather than disproving a demonstrated exploit. The related MPEG advisories are not reachable, because `ENABLE_MPEG=OFF` keeps that code out of the binary.

## Intentional limitations

- One stereo output for all 16 channels.
- FluidSynth 2.5.5 renders no higher than 96 kHz. Above that Juicy16 renders at an integer fraction of the host rate and interpolates each block up, so a 192 kHz project plays; MIDI event timing then quantises to one internal sample (about 10 microseconds at 96 kHz) instead of one host sample. Below FluidSynth's 8 kHz floor there is no equivalent path and playback is still muted rather than detuned.
- A drum channel's bank reaches 255, because FluidSynth adds its 128 drum offset to the Bank Select MSB. That is the number the engine, the UI, the host parameter, and the saved state all report; the font itself still defines only banks 0-128, so the kit heard on such a bank is FluidSynth's substitution.

- Beta 1 is **ad-hoc signed by decision**, not Developer ID signed and not notarized. Every macOS install therefore requires clearing the quarantine attribute; see [BETA_TESTER_GUIDE.md](BETA_TESTER_GUIDE.md). The `ADHOC` label in the package filename is expected for Beta 1 and is not a disqualifier. `LOCAL-DIRTY` still is.
- Standalone is a development/QA target, not a primary Beta format.
- **Windows is not part of Beta 1.** Windows VST3 moved to Beta 2 on 2026-08-24: the MSVC/CI pipeline, clean-machine dependency check, DLS capability and host matrix are all unproven, and the legacy LLVM-MinGW Docker path is unsupported. No Windows artifact is published, so there is nothing to test.
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
