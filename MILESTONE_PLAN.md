# JuicySF Rack Beta 1 Candidate — Comprehensive Remediation and Release Plan

This document is the authoritative execution plan and candidate checklist for the **first public beta of JuicySF Rack**. It brings the repository into alignment with the intended product:

> A cross-platform, 16-channel DLS/SoundFont player inspired by Fruity LSD, distributed primarily as AU on macOS and VST3 on macOS and Windows.

It converts the repository audit into sequenced, checkable milestones and adds the operational work required to distribute, observe, support, and evaluate a large first beta. Contributors and agents should update this file in the same change that completes a task so it remains the source of truth for Beta 1 readiness.

## Beta 1 identity

Complete these fields before producing the first candidate:

- Working version: `0.5.0-beta.1` unless another version is approved in Phase 0.
- Candidate number: `BC1` initially; increment for every rebuilt candidate.
- Candidate commit:
- Candidate build date:
- Candidate coordinator:
- Supported macOS versions:
- Supported Windows versions:
- Supported architectures:
- Included formats:
- Tester group or distribution channel:
- Feedback destination:
- Emergency withdrawal owner:

## Beta quality policy

“Beta” permits clearly documented limitations and incomplete polish. It does **not** permit known data loss, project-state corruption, unsafe memory access, fundamentally incorrect MIDI timing, unusable advertised formats, undisclosed dependency requirements, or artifacts that fail platform validation.

The Beta 1 bar is:

- Safe enough for testers to use in copied or non-critical projects.
- Correct enough that feedback reflects the intended 16-channel product rather than already-known engine defects.
- Portable enough to install on every advertised test platform without developer dependencies.
- Observable enough that failures can be reproduced and triaged.
- Explicit enough that testers understand compatibility limits, backup expectations, and how to report a problem.

Beta 1 is not a general-availability release. Notarization, broad host coverage, installer polish, and performance tuning may remain incomplete only when the limitation is documented, does not block installation, and has an owner for a later beta or stable release.

## How to use this plan

### Status convention

- `[ ]` Not started or not yet proven complete.
- `[x]` Complete and verified against the listed acceptance criteria.
- `BLOCKED:` Add this immediately below a task when progress requires a decision, unavailable environment, dependency, sample corpus, host, certificate, or other external input.
- `OWNER:` Optionally add an agent, contributor, or workstream name below an active task.
- `EVIDENCE:` Add links to tests, logs, commits, pull requests, screenshots, or release artifacts when completing a task.

Do not check off a task solely because code was written. A task is complete only when its acceptance criteria and required verification have passed.

### Agent workflow

Before starting work:

1. Read this entire plan and the files named by the selected task.
2. Confirm that all prerequisite tasks are complete.
3. Add an `OWNER:` note under the task when parallel work might overlap.
4. Keep the task narrowly scoped; create a separate commit or pull request when practical.

Before handing work off:

1. Run the task's verification steps.
2. Record any deviations, environment limits, and remaining risks under the task.
3. Check off only the tasks actually completed.
4. Update the milestone exit criteria if the completed work satisfies them.
5. Leave the worktree buildable and do not erase unrelated user or agent changes.

### Definition of done

The Beta 1 candidate is ready only when:

- MIDI playback is sample-accurate and correct across all 16 channels.
- SF2, SF3, and DLS loading work on supported macOS and Windows releases.
- AU and VST3 artifacts are portable, correctly signed where applicable, and host-validated.
- The licensing and privacy position is explicit and internally consistent.
- Documentation, metadata, build scripts, UI terminology, and packaging describe the same product and support matrix.
- Automated tests cover the core engine, state, font-loading, and VST3 routing behavior.
- A reproducible beta candidate passes the final validation matrix and the Beta 1 go/no-go review.

### Beta 1 severity policy

Use these levels consistently in code reviews, issue tracking, candidate notes, and tester feedback:

- **B0 — Stop-ship:** crash in a normal workflow; project/state corruption; security/privacy issue; license or distribution blocker; plugin cannot be discovered; advertised format cannot load on an advertised platform; missing runtime dependency; invalid signature; severe audio corruption; or repeatable host instability.
- **B1 — Must fix before Beta 1:** incorrect MIDI timing or routing; wrong channel/program state; reset recovery failure; broken save/restore; major UI workflow failure; unsupported-platform claim; or failure of the required host/font matrix.
- **B2 — May ship only if documented and approved:** non-destructive host-specific limitation, moderate UI defect, performance issue outside the agreed stress envelope, or workaround with low tester burden.
- **B3 — Backlog:** cosmetic, cleanup, or low-impact developer-experience issue that does not undermine beta evaluation.

No B0 or B1 issue may be open at the Beta 1 gate. Every open B2 issue must appear in the beta release notes with a workaround or impact statement.

## Beta 1 master checklist

This is the high-level candidate dashboard. Check a phase here only after every required exit criterion in that phase is complete or an explicit Beta 1 exception is recorded in the Decision log.

- [ ] Phase 0: product, platforms, formats, identity, and licensing approved.
- [ ] Phase 1: real-time MIDI, GM behavior, synchronization, bounds, and thread-safety work complete.
- [ ] Phase 2: SF2/SF3/DLS loading and cross-platform font corpus complete.
- [ ] Phase 3: AU/VST3 architecture and host integration stable.
- [ ] Phase 4: portable, reproducible, signed/validated artifacts produced.
- [ ] Phase 5: automated tests and CI quality gates operational.
- [ ] Phase 6: documentation, metadata, privacy, licensing, and notices consistent.
- [ ] Phase 7: exact Beta 1 candidate passes the technical validation matrix.
- [ ] Phase 8: tester program, package, diagnostics, triage, rollback, and launch readiness complete.
- [ ] Zero open B0 issues.
- [ ] Zero open B1 issues.
- [ ] Every open B2 issue approved and published as a known limitation.
- [ ] Beta 1 go/no-go decision recorded.

## Target release contract

The following decisions define the expected end state. Tasks that depend on unresolved choices are called out in Phase 0.

### Product behavior

- Exactly 16 MIDI channels per plugin instance.
- One independently selectable bank/preset assignment per channel.
- General MIDI channel 10 defaults to percussion behavior.
- Incoming Bank Select, Program Change, notes, controllers, pitch bend, pressure, and supported SysEx preserve their timestamps within the audio block.
- All MIDI CC numbers and the full 14-bit pitch-bend range are delivered to the intended channel without truncation, remapping, channel leakage, or block-start quantization.
- CCs that are not represented by plugin UI controls still reach FluidSynth correctly; UI mirroring is required only for the explicitly exposed sound-controller parameters.
- Manual patch selection and host automation remain synchronized with engine state and saved state.
- GM/GS/XG reset messages do not silently destroy saved channel assignments.
- All channels mix to one stereo output for this release.
- Supported bank files are `.sf2`, `.sf3`, and `.dls` on every advertised platform.

### Non-negotiable game-rip playback workflow

Beta 1 must preserve the workflow that motivated the 16-channel VST3 implementation:

1. Insert one JuicySF Rack instance.
2. Load the DLS, SF2, or SF3 bank associated with a game-rip MIDI.
3. Route or import the MIDI so its original 16 MIDI channels reach the single plugin instance.
4. Start playback without manually assigning each channel's patch.
5. Bank Select and Program Change events in the MIDI automatically select the correct instrument on the correct channel at the correct musical timestamp.
6. Program Changes later in the song take effect exactly when encountered.
7. GM/GS/XG reset SysEx at the beginning of a game-rip MIDI does not leave all channels on program 0 or make the UI disagree with the sound engine.

This workflow is required in both FL Studio and Cubase. A candidate where FL Studio works but Cubase only responds on channel 1, ignores later Program Changes, or requires manual patch selection is a **B1 must-fix regression**.

### Cubase VST3 compatibility invariant

The existing Cubase workaround is intentional and must not be removed as “unnecessary” without an end-to-end replacement that passes the same tests. It currently depends on all of the following:

- Sixteen discrete `progCh1` through `progCh16` parameters.
- One VST3 unit per MIDI channel, with parameter group IDs matching the unit-ID hash derivation.
- A shared 128-entry VST3 program list attached to all 16 channel units.
- `getUnitByBus` mapping event-bus MIDI channel N to unit N.
- `kIsProgramChange`/`kIsList` flags and `stepCount == 127` for every channel program parameter.
- Per-channel `IMidiMapping` from `kCtrlProgramChange` to the corresponding `progChN` parameter.
- Valid unit/program-list answers before component/controller connection, because Cubase may query and cache this structure early.
- Rejection of out-of-range VST3 controller numbers instead of the stock wrapper's historical out-of-bounds lookup.
- Engine, UI, saved state, and host-parameter synchronization after Cubase delivers a program parameter change.

FL Studio and Cubase exercise different portions of this contract. Passing in one host is not evidence that the other path remains functional.

### Product formats

- macOS: AU and VST3 are release formats.
- Windows: VST3 is a release format.
- Standalone may remain as a development and QA utility, but should not be described as a primary release format unless explicitly approved.
- VST2 is legacy/out of scope by default and should be removed from normal release documentation unless explicitly approved.

### Quality rules

- Release artifacts must not depend on developer-machine Homebrew paths.
- Release artifacts must declare an intentional minimum operating-system version.
- Release builds must have no known undefined behavior, bounds violations, or failed validation checks.
- Tests must fail the build when core behavior regresses.
- Documentation must distinguish implemented, tested, and merely intended behavior.

---

# Phase 0 — Resolve product, platform, and licensing decisions

## Goal

Eliminate decisions that would otherwise cause later agents to implement incompatible solutions.

## Tasks

- [ ] Confirm the supported platform and architecture matrix.
  - BLOCKED: Product owner must approve minimum OS versions and whether macOS Beta 1 is Apple Silicon-only or universal. The locally verified artifact is `arm64` only.
  - Decide the minimum macOS version.
  - Decide whether macOS releases are universal `arm64;x86_64` or Apple Silicon only.
  - Decide the minimum Windows version; JUCE 8 documentation currently starts at Windows 10 version 1607.
  - Decide whether Windows ARM64 is in scope now, later, or not planned.
  - Decide whether Linux VST3 is in scope; do not imply it through the phrase “cross-platform” unless it is tested.

- [ ] Confirm release-format scope.
  - Approve AU and VST3 as primary release formats.
  - Decide whether standalone remains a QA-only target.
  - Decide whether all VST2 code, build options, and documentation should be removed or retained as unsupported legacy functionality.
  - Decide whether AUv3 is explicitly out of scope.

- [ ] Confirm product ownership and metadata.
  - BLOCKED: The repository does not establish whether the legacy Birchlabs identity and plugin identifiers remain approved.
  - Confirm whether `Birchlabs`, `birchlabs.co.uk`, `Blbs`, and `com.Birchlabs.JuicySFPlugin` remain correct.
  - If ownership changed, choose the company name, website, support email, bundle identifier, manufacturer code, plugin code, copyright text, and signing identity.
  - Document compatibility consequences before changing plugin identifiers; identifier changes can break existing DAW sessions.

- [ ] Resolve the JUCE licensing model.
  - BLOCKED: Distribution requires an owner/legal decision between an applicable JUCE 8 commercial license and an AGPL-compatible release. The existing GPLv3-only text is not sufficient evidence for JUCE 8 distribution.
  - Choose one of: a commercial JUCE 8 license, AGPLv3-compatible project distribution, or a technically and legally viable framework/version alternative.
  - Have the chosen position reviewed by an appropriately qualified person before public distribution.
  - Decide the resulting top-level project license and source-offer obligations.
  - Record the decision in a short licensing note or architecture decision record.

- [ ] Confirm the intended relationship to Fruity LSD.
  - Approve wording such as “inspired by” or “Fruity LSD-style workflow.”
  - Avoid claims of exact emulation unless a documented compatibility matrix supports them.
  - List any Fruity LSD behaviors explicitly outside this release.

## Exit criteria

- [ ] A support matrix is written and approved.
- [ ] Release formats and development-only formats are unambiguous.
- [ ] Product identity values are approved.
- [ ] The licensing path is approved and documented.
- [ ] Later phases have no unresolved scope question that would invalidate their implementation.

---

# Phase 1 — Correct the real-time MIDI and synthesis engine

## Goal

Make the audio engine trustworthy before investing in packaging and release work.

## 1.1 Sample-accurate MIDI rendering

- [x] Refactor `FluidSynthModel::processBlock` to render audio in timestamped segments.
  - Sort or consume JUCE MIDI events in their existing timestamp order.
  - Render from the current sample position up to the next event timestamp.
  - Apply all events at that timestamp in deterministic order.
  - Render the remaining samples after the final event.
  - Preserve stereo rendering and the mono-output downmix path.
  - Clamp or safely handle timestamps outside the current block.
  - Remove the unused `processedMidi` variable.

- [x] Define event ordering at equal timestamps.
  - Ensure Bank Select occurs before Program Change when delivered in that order.
  - Ensure reset SysEx and immediately following Program Changes/notes behave predictably.
  - Preserve JUCE buffer ordering unless a documented MIDI rule requires otherwise.

- [x] Add automated timing tests.
  - Note-on halfway through a block must produce silence before its timestamp.
  - Note-off halfway through a block must not truncate the first half of the block.
  - Note-on and note-off within the same block must produce the expected bounded note.
  - Events on different MIDI channels must retain independent timing.
  - Program Change followed by a note in the same block must use the new program only after the Program Change timestamp.

### Acceptance criteria

- [x] No MIDI event is unconditionally applied at block start.
- [x] Timing tests pass at multiple block sizes, including 32, 64, 256, 512, and 1024 samples.
- [x] Stereo and mono output tests pass.

  EVIDENCE: `tests/EngineMidiTests.cpp`; ASan+UBSan Debug and static Release tests pass at 32, 64, 256, 512, and 1024 samples, including mono chunking above the prepared maximum, on 2026-08-05.

## 1.2 General MIDI channel behavior

- [x] Implement and document channel 10 percussion defaults.
  - Determine the correct FluidSynth bank convention for each supported font type.
  - Initialize channel 10 to the appropriate percussion bank without changing the other 15 channels.
  - Preserve an explicitly saved channel 10 assignment when restoring state.
  - Confirm behavior after a font load, GM reset, GS reset, and XG reset.

- [x] Add General MIDI behavior tests.
  - Fresh GM font load gives channel 10 a drum kit.
  - Channel 10 Program Change selects kits rather than melodic bank-zero patches where the bank supports GM percussion.
  - Other channels continue to default to melodic bank 0/program 0.

### Acceptance criteria

- [ ] A normal GM MIDI file that relies on the channel 10 convention plays percussion without requiring an explicit Bank Select.
- [x] Manual channel 10 bank/preset selection still works and persists.

  EVIDENCE: The offline engine suite verifies melodic bank 0 defaults on other channels, channel 10 bank 128, percussion-bank preservation across Program Change and GM/GS/XG reset, and manual channel 10 assignment across state round-trip using the macOS system DLS.

## 1.3 Program, bank, reset, and state synchronization

- [ ] Unify all program-change paths behind one engine-state function.
  - Cover MIDI Program Change, `progChN` host automation, global selected-channel parameters, manual dropdown selection, state restoration, and font reload.
  - Update `engineBank` and `enginePreset` consistently on every successful engine change.
  - Update UI/state mirrors only on the message thread.
  - Return or record errors from failed FluidSynth calls.

- [ ] Make reset recovery deterministic.
  - Immediately restore the correct engine bank/preset after supported reset SysEx.
  - Restore sound-controller state without racing newer MIDI events.
  - Ensure the asynchronous full resync cannot overwrite a more recent Program Change.
  - Test multiple resets and transport restarts.

- [ ] Decide how Bank Select state is represented.
  - Verify CC0/CC32 behavior for SF2, SF3, and DLS banks.
  - Ensure the UI displays the bank actually selected by the engine.
  - Document the limitation that VST3 `progChN` exposes only a 0–127 program number unless bank automation is added.

### Acceptance criteria

- [ ] Every program-change entry point produces the same engine, UI, parameter, and persisted state.
- [ ] Reset tests demonstrate no stale-program window before subsequent notes.
- [ ] Bank-offset handling is covered by tests.

## 1.4 Bounds, initialization, and thread safety

- [x] Initialize `focusInitialized` explicitly.
- [x] Validate and clamp restored `selectedChannel` before storing or indexing with it.
- [x] Add bounds checks before every per-channel array access and FluidSynth channel call.
- [x] Validate `setChannelProgram` arguments even when called outside the UI.
- [x] Audit shared values accessed by both audio and message threads.
  - `loadingChannel`
  - `sfont_id`
  - loaded-font lifetime and temporary file state
  - program-name storage and callbacks
  - channel state during restore and editor changes
- [x] Establish a documented locking/atomic policy that does not block the real-time thread unnecessarily.
- [x] Remove avoidable audio-thread allocations.
  - Preallocate sufficient render scratch space.
  - Define a safe strategy when a host exceeds the prepared maximum block size.
- [x] Run AddressSanitizer and UndefinedBehaviorSanitizer test builds where host/plugin loading permits.
- [x] Run ThreadSanitizer on an executable harness where practical.

### Acceptance criteria

- [x] Corrupt or out-of-range saved state cannot access memory outside channel arrays.
- [x] Sanitizer runs have no unresolved first-party findings.
- [ ] The real-time path performs no known unbounded allocation or unsafe UI/state mutation.

  EVIDENCE: `docs/THREADING_MODEL.md`; separate ASan+UBSan and TSan engine/font harness builds passed on arm64 macOS on 2026-08-05.

## 1.5 Complete MIDI CC, pitch-bend, and expressive-control accuracy

### Scope and behavior contract

The beta must distinguish between MIDI processing and UI automation:

- Every valid channel CC message, CC0 through CC127, must be forwarded to FluidSynth on the original MIDI channel and at the correct sample timestamp.
- CC71, CC72, CC73, CC74, CC75, and CC79 are additionally mirrored into the plugin's per-channel UI/state because the plugin exposes those sound controls.
- Bank Select CC0/CC32 affects the next Program Change according to FluidSynth and the loaded bank; it must not be mistaken for a standalone patch change.
- Undefined or unexposed CCs do not require a visible control, but they must not be dropped, converted into Program Changes, or redirected to the selected UI channel.
- Pitch bend must retain its full 14-bit MIDI value from 0 through 16383, with 8192 as center, independently for all 16 channels.
- Pitch-bend range and tuning behavior controlled by RPN messages must remain intact.

### Controller forwarding

- [x] Audit the complete CC0–CC127 path from JUCE `MidiMessage` through `fluid_synth_cc`.
  - Confirm no CC number is reserved internally as a hidden Program Change workaround.
  - Confirm the removed CC85 workaround cannot regress.
  - Confirm CC values remain integer 0–127 without normalization errors.
  - Confirm messages are sent to `message.channel - 1`, never the currently selected UI channel.
  - Confirm multiple CCs at the same sample retain input order.

- [x] Add a parameterized CC forwarding test covering every CC number 0–127.
  - Run the test on channels 1, 2, 10, and 16 at minimum.
  - Include values 0, 1, 63, 64, 65, 126, and 127.
  - Assert channel isolation and message ordering.
  - Where FluidSynth exposes observable controller state, compare the exact engine value.
  - For controllers with defined side effects, verify the effect rather than assuming a stored value.

- [ ] Add focused tests for commonly used musical controllers.
  - CC0/32: Bank Select MSB/LSB.
  - CC1: Modulation wheel.
  - CC2: Breath controller.
  - CC5: Portamento time.
  - CC6/38: Data Entry MSB/LSB.
  - CC7: Channel volume.
  - CC10: Pan.
  - CC11: Expression.
  - CC64: Sustain pedal, including notes released while held.
  - CC65: Portamento on/off.
  - CC66: Sostenuto.
  - CC67: Soft pedal.
  - CC68: Legato footswitch.
  - CC71–75 and CC79: exposed sound controls and UI/state mirroring.
  - CC91/93: Reverb and chorus send behavior where supported by FluidSynth.
  - CC98/99 and CC100/101: NRPN/RPN selection.
  - CC120: All Sound Off.
  - CC121: Reset All Controllers.
  - CC122: Local Control, documenting engine behavior if intentionally ignored.
  - CC123: All Notes Off.
  - CC124–127: Omni/Mono/Poly channel-mode messages, documenting FluidSynth support and expected behavior.

- [ ] Verify high-resolution controller pairs where supported.
  - MSB/LSB pairs must retain ordering and channel identity.
  - Do not claim 14-bit CC support for pairs the engine does not implement; document exact observed behavior.

### Exposed sound-controller synchronization

- [ ] Verify CC71/72/73/74/75/79 on every MIDI channel.
  - Engine receives the exact value at the MIDI timestamp.
  - The affected channel's saved state updates on the message thread.
  - Sliders update only when that channel is selected.
  - Switching channels reveals each channel's most recent independent value.
  - Saving and reopening restores all six values for all 16 channels.
  - Incoming CCs do not create recursive or duplicate engine sends.

- [ ] Verify the neutral point and direction of every exposed sound controller.
  - Value 64 produces the documented neutral behavior.
  - Values below and above 64 move the destination in the expected direction.
  - Cutoff is not inverted.
  - Sustain-level direction matches the UI label.
  - Reset All Controllers and GM/GS/XG reset behavior are explicitly tested and documented.

### Pitch bend

- [x] Audit the pitch-bend path from JUCE through `fluid_synth_pitch_bend`.
  - Preserve values 0–16383 exactly.
  - Preserve center value 8192 exactly.
  - Do not convert the value to 7-bit or normalized floating point.
  - Apply to the source MIDI channel, not the selected UI channel.

- [x] Add exact-value pitch-bend tests.
  - Values: 0, 1, 4096, 8191, 8192, 8193, 12288, 16382, and 16383.
  - Channels: 1, 2, 10, and 16 at minimum.
  - Simultaneous opposite bends on two channels must remain independent.
  - Center reset must restore pitch without affecting another channel.
  - Pitch bend before note-on and during a sustained note must both behave correctly.
  - Multiple bends within one block must take effect at their individual sample timestamps.

- [x] Verify pitch-bend range through MIDI RPN.
  - Send RPN 0,0 using CC101/100 followed by Data Entry CC6/38.
  - Test default range and at least ±2, ±12, and ±24 semitones where supported.
  - Verify range is independent per MIDI channel.
  - Verify RPN Null prevents later Data Entry from changing the range accidentally.
  - Verify reset messages restore the expected range.
  - Document FluidSynth-specific limits or rounding.

- [x] Add audio-domain pitch verification.
  - Render a stable test tone or sustaining preset.
  - Measure fundamental frequency before and after bend.
  - Compare measured ratios with the expected semitone/cents values within an approved tolerance.
  - Repeat at 44.1, 48, and 96 kHz to catch sample-rate-related pitch errors.

  EVIDENCE: The system-DLS engine test renders note 69 at full-down, center, and full-up with a 12-semitone RPN range, estimates periodic frequency from audio, and verifies the expected ratios at 44.1, 48, and 96 kHz under ASan+UBSan.

### Pressure and related channel expression

- [x] Test channel pressure across all 16 channels.
- [x] Test polyphonic key pressure with independent note numbers.
- [x] Verify pressure messages retain timestamp, channel, key, and 0–127 value.
- [x] Document whether the audible result depends on modulators present in the loaded bank.

  EVIDENCE: `README.md`, `docs/BETA_TESTER_GUIDE.md`, and `docs/KNOWN_ISSUES.md` distinguish exact pressure delivery from bank-dependent audible modulation.

### Host and file-playback coverage

- [ ] Add a deterministic MIDI fixture containing the full CC/pitch-bend test sequence.
- [ ] Run the fixture through the offline processor harness and compare against an expected event/state trace.
- [ ] Run musically representative CC and pitch-bend passages in FL Studio and Cubase.
- [ ] Verify the DAW does not filter, chase incorrectly, normalize, or collapse controller data before it reaches the plugin.
- [ ] Test transport start, stop, rewind, loop boundaries, chase, project reload, and playback beginning mid-song.
- [ ] Record host-specific controller-chase behavior separately from plugin defects.

### Acceptance criteria

- [x] The parameterized CC0–CC127 suite passes without channel or value corruption.
- [ ] All six exposed sound controllers remain engine/UI/state-consistent on all 16 channels.
- [x] Full-range 14-bit pitch bend and per-channel RPN pitch-bend range tests pass.
- [x] CCs and bends occurring within one audio block affect audio at the correct sample offsets.

  EVIDENCE: The audio-domain suite measures four bend segments in one sustained-note block and verifies that timestamped All Sound Off creates only the intended silent interval before a later note resumes.
- [ ] FL Studio and Cubase reproduce the same expected musical controller and pitch-bend sequence, accounting only for documented host chase behavior.
- [ ] Any unsupported FluidSynth controller behavior is documented precisely and is not misrepresented as plugin support.

## Phase 1 exit criteria

- [x] Sample-accurate rendering is implemented and tested.
- [x] GM percussion defaults are correct.
- [ ] All program/reset paths share consistent state handling.
- [x] Bounds, initialization, and high-risk threading issues are resolved.
- [ ] Complete CC, pitch-bend, RPN, pressure, pedal, and channel-mode coverage passes the Phase 1.5 acceptance criteria.
- [x] Core engine tests run automatically through CTest or the chosen test runner.

  EVIDENCE: `FluidSynthModel::dispatchMidiEvent` forwards unscaled 14-bit values; the offline test covers specified edge/center values and independent RPN ranges on channels 1, 2, 10, and 16.

---

# Phase 2 — Make font loading reliable on every platform

## Goal

Guarantee that every advertised format—SF2, SF3, and DLS—loads through the same well-defined, user-visible workflow on macOS and Windows.

## 2.1 Transactional loading and error reporting

- [x] Make bank replacement transactional.
  - Attempt to validate/load the new bank without first destroying the working bank where FluidSynth permits.
  - If an atomic swap is not possible, preserve enough state to restore the previous bank after failure.
  - Do not update the displayed path as successfully loaded until the engine accepts the file.

- [x] Add a structured load result.
  - Success/failure status.
  - User-facing error text.
  - Original path and actual path loaded.
  - Whether DLS repair was attempted and whether it changed the file.
  - FluidSynth error context where available.

- [x] Surface failures in the plugin UI without blocking the audio thread.
- [ ] Handle missing, unreadable, moved, zero-preset, unsupported, and corrupt files.
- [x] Decide whether the current font remains active after every failure class and test that behavior.

### Acceptance criteria

- [x] Selecting a bad file never silently clears a working setup.
- [x] The UI clearly identifies failed loads and successful DLS repairs.
- [x] State restore with a missing bank fails gracefully.

  EVIDENCE: Replacement loads are validated before swapping `sfont_id`; `engine_midi_system_dls` verifies a missing replacement reports `error`, preserves the prior bank in both rendered audio and newly serialized state, and keeps its `loadedPath`. The editor status bar and file-control tooltip expose the structured result, including repaired-copy success text.

## 2.2 macOS path and sandbox behavior

- [x] Make path loading work when bookmark creation fails.
- [x] Release any `CFErrorRef` values and handle Core Foundation failures explicitly.
- [ ] Verify security-scoped bookmark creation and restoration in AU, VST3, and standalone contexts.
- [ ] Handle stale bookmarks and moved files.
- [ ] Verify that sandbox declarations match actual file access requirements.

### Acceptance criteria

- [ ] A file selected through the picker loads whether bookmark creation succeeds or the safe fallback path is required.
- [ ] Saved sessions restore access after the plugin and DAW restart.
- [ ] No Core Foundation leaks are reported in the tested workflows.

## 2.3 DLS implementation and repair safety

- [ ] Choose the Windows FluidSynth strategy.
  - Preferred: upgrade to a current FluidSynth release with native C++17 DLS support.
  - Alternative: build the pinned version with `libinstpatch` and package its obligations correctly.

- [ ] Add a build-time or runtime DLS capability check.
  - A build that cannot load DLS must fail release validation.
  - Do not rely only on the file-picker extension filter.

- [x] Expand `repairDlsImage` tests.
  - Empty and truncated images.
  - Oversized and undersized top-level chunks.
  - Odd-byte padding.
  - First-chunk corruption.
  - Very large sizes and 32-bit overflow behavior if 32-bit remains in scope.
  - Non-DLS RIFF files must remain byte-identical.
  - Fuzz or property-test malformed RIFF structures.

- [ ] Define the limits of repair.
  - Repair only known-safe structural errors.
  - Report when repair was attempted but the result still fails.
  - Avoid promising that every malformed DLS “just works.”

- [ ] Build a licensed font compatibility corpus.
  - At least one conventional SF2.
  - At least one compressed SF3.
  - At least one well-formed DLS.
  - At least one known Awave-style malformed DLS.
  - Sparse/non-zero banks and percussion banks.
  - Large banks and unusual preset names.
  - Record license and redistribution status for every fixture.

### Acceptance criteria

- [ ] The corpus passes on supported macOS and Windows builds.
- [ ] DLS capability is proven, not inferred, on each release artifact.
- [x] Repair is idempotent and does not modify the user's original file.

  EVIDENCE: `font_repair_unit` covers empty/truncated input, inner/outer size errors, odd padding, unsafe first-chunk refusal, non-DLS identity, idempotence, and 6,000 deterministic malformed/non-DLS property cases; ASan+UBSan passes. Plugin repair writes a JUCE unique temporary copy only.

## Phase 2 exit criteria

- [x] Font loading is transactional and errors are visible.
- [ ] macOS bookmark/path restoration is reliable.
- [ ] Windows and macOS both pass the SF2/SF3/DLS corpus.
- [ ] DLS repair behavior and limits are tested and documented.

---

# Phase 3 — Stabilize AU and VST3 architecture

## Goal

Retain the proven 16-channel host integration while removing assertion-driven or version-fragile behavior.

## 3.1 VST3 unit and Program Change design

- [x] Document the intended VST3 contract in one design note.
  - Root unit plus 16 channel units.
  - Unit ID derivation.
  - Shared 128-program list.
  - `progCh1` through `progCh16` parameter mapping.
  - `IMidiMapping` behavior.
  - Component-side and controller-side `IUnitInfo` responsibilities.

- [ ] Remove duplicate ownership of `IUnitInfo`.
  - Investigate whether the vendored wrapper, `VST3ClientExtensions`, or a smaller targeted patch can be the sole implementation.
  - Eliminate the expected `extractResult()` assertions.
  - Preserve pre-connection unit discovery required by Cubase.
  - Preserve component-side queries and host program-list refresh notifications.

- [ ] Minimize and version the JUCE wrapper patch.
  - Pin the exact compatible JUCE version in CMake.
  - Fail configuration when a different JUCE version is found unless explicitly supported.
  - Maintain a clean patch file or clearly documented vendored diff.
  - Add an automated comparison that detects unexpected upstream drift.
  - Correct all comments describing the patch's scope and line count.

- [ ] Extend the VST3 smoke test.
  - Fail if JUCE assertions occur in the tested path.
  - Exercise component/controller creation and destruction repeatedly.
  - Test parameter-to-unit mapping before and after connection.
  - Test program-list name refresh after a font load.
  - Test invalid channels, controllers, units, and program indices.
  - Process actual MIDI/audio and state if the harness can safely support it.

- [ ] Preserve both VST3 Program Change delivery paths.
  - Path A: a host sends or emulates Program Change through `IMidiMapping`.
  - Path B: a host uses `IUnitInfo`, program lists, and the unit's `kIsProgramChange` parameter.
  - Prove that channel N always updates `progChN`, never the global program or channel 1 by accident.
  - Prove that all 16 channels can change programs independently in one processing sequence.
  - Prove that a parameter-delivered Program Change updates `engineBank`/`enginePreset`, channel state, selected-channel UI, and saved state exactly like a raw MIDI Program Change.

- [ ] Add a Cubase query-order regression test.
  - Instantiate the VST3 controller and query `IUnitInfo` before connecting component and controller.
  - Require 17 units and one 128-entry program list during that early query.
  - Query every event-bus channel and verify its unit ID.
  - Connect component/controller, repeat all queries, and require identical answers.
  - Recreate the plugin and repeat to detect stateful or initialization-order failures.

- [ ] Add an end-to-end multichannel Program Change fixture.
  - Use a legally redistributable MIDI fixture with activity across all 16 channels.
  - Include distinct Program Changes for every channel near the beginning.
  - Include Bank Select where needed, channel 10 percussion, and at least four mid-song instrument changes.
  - Include variants with GM, GS, and XG reset SysEx before the initial Program Changes.
  - Include simultaneous Program Changes on different channels.
  - Include Program Change and note-on within the same audio block to verify timing and instrument choice.
  - Record the expected bank, program, and sounding preset at every checkpoint.

- [ ] Add program-parameter observation hooks to the test harness.
  - Capture which `progChN` parameter changes, its normalized value, and its sample/block position.
  - Assert that no unrelated channel parameter changes.
  - Assert that the selected UI row does not redirect changes intended for another channel.
  - Assert that transport restart and host parameter deduplication do not prevent the engine from restoring the intended program after reset SysEx.

### Acceptance criteria

- [x] `vst3_smoke` exits successfully without assertion output.

  EVIDENCE: `vst3_multitimbral_smoke` passed in Debug and Release on 2026-08-05, including pre-connection discovery of 17 units, all 16 program parameters/mappings, component/controller queries, and bounds rejection.
- [ ] Cubase and at least one additional VST3 host correctly route Program Change independently on all 16 channels.
- [ ] The patch is reproducible against the pinned JUCE source.
- [ ] Both the `IMidiMapping` and VST3 unit/program-parameter paths pass independently.
- [ ] The multichannel game-rip fixture reaches every expected instrument without manual patch assignment.

## 3.2 AU behavior

- [ ] Add AU-focused integration tests or a host harness where practical.
- [ ] Verify per-channel Program Change delivery in Logic and at least one additional AU host.
- [ ] Verify state save/restore, resizing, keyboard focus, file access, and 16-channel MIDI routing.
- [ ] Run `auval` as part of every macOS release build.

### Acceptance criteria

- [ ] `auval` passes the exact release AU artifact.
- [ ] Host tests demonstrate independent channel behavior and state restoration.

## Phase 3 exit criteria

- [ ] VST3 routing works without assertions or duplicate-interface ambiguity.
- [ ] AU passes validation and host tests.
- [ ] JUCE version and patch compatibility are enforced automatically.

---

# Phase 4 — Build reproducible, portable release artifacts

## Goal

Produce AU and VST3 bundles that work on clean supported systems rather than only on the developer machine.

## 4.1 CMake cleanup and dependency policy

- [ ] Make release dependency linkage explicit.
  - Decide whether FluidSynth and its required dependencies are statically linked or embedded and relocated.
  - Ensure no release binary references `/opt/homebrew`, `/usr/local`, a developer home directory, or build-tree paths.
  - Rename or remove misleading `BUILD_SHARED_LIBS` comments and unused qualifier variables.
  - Make release configuration fail when portable linkage requirements are not met.

- [ ] Set explicit deployment targets.
  - Apply the approved minimum macOS version.
  - Apply the approved Windows target/version macros and toolchain requirements.
  - Verify the resulting load commands and metadata.

- [ ] Enforce architecture expectations.
  - Produce and verify universal macOS binaries if approved in Phase 0.
  - Produce x64 Windows artifacts and any other approved architectures.
  - Remove unused x86/ARM scripts and documentation if those architectures are out of scope.

- [ ] Reduce CMake to supported formats and modules.
  - Keep AU conditional on Apple platforms.
  - Keep VST3 on supported desktop platforms.
  - Mark standalone as development-only if retained.
  - Remove or gate VST2/AUv3/AAX/RTAS/Unity remnants.
  - Remove unused generated JUCE translation units or replace the legacy `JuceLibraryCode` header with normal JUCE module includes/generated headers.

- [ ] Add configure-time checks.
  - Exact JUCE version.
  - Minimum FluidSynth version and required features.
  - Supported compiler and generator.
  - Required architecture dependencies.
  - Signing inputs for release builds.

### Acceptance criteria

- [x] A clean configure gives a concise, accurate summary of formats, architectures, dependency linkage, and feature support.
- [ ] Unsupported configurations fail early with actionable errors.

## 4.2 macOS signing and validation

- [x] Fix the resource/signing dependency order.
  - Ensure `AppIcon.icns`, plist files, VST3 module metadata, and all resources exist before signing.
  - Ensure copy-after-build occurs only after the final signature.
  - Test parallel builds to rule out ordering races.

- [ ] Add release signing and optional notarization workflow.
- [ ] Verify every artifact with:

  ```bash
  codesign --verify --deep --strict --verbose=2 "path/to/JuicySF Rack.component"
  codesign --verify --deep --strict --verbose=2 "path/to/JuicySF Rack.vst3"
  auval -v aumu Jsfr Blbs
  ```

- [x] Inspect portability with `otool -L` and deployment targets with `otool -l`.
- [ ] Test installation and first launch on a clean supported Mac without Homebrew FluidSynth.

### Acceptance criteria

- [x] Strict code-signature verification passes after a parallel clean build.
- [ ] AU validation passes.
- [x] No prohibited dependency path appears in `otool -L`.

  EVIDENCE: Clean static Release AU/VST3 build under `/private/tmp/juicysf-release-build` passed `codesign --verify --deep --strict`; `otool -L` reported only system libraries/frameworks, with no Homebrew, repository, or build paths. Artifact is currently arm64; deployment-target approval remains blocked by Phase 0.
- [ ] Artifacts load on both the minimum supported macOS version and the current version.

## 4.3 Windows toolchain and packaging

- [ ] Replace or formally validate the unsupported JUCE 8 MinGW path.
  - Preferred: establish a supported MSVC/Visual Studio build, locally or in CI.
  - If LLVM-MinGW remains, document the risk and prove it through host validation; do not claim upstream support.

- [ ] Repair the Docker/build context.
  - Do not unconditionally copy an absent `VST2_SDK/` directory.
  - Remove VST2 from the default pipeline if out of scope.
  - Update pinned Ubuntu, LLVM/toolchain, JUCE, FluidSynth, and dependencies.
  - Remove stale JUCE 6 and “no VST3 target” comments.

  EVIDENCE: The context no longer requires `VST2_SDK`, copies the registered test sources, pins JUCE 8.0.14/FluidSynth 2.5.5, uses native C++17 DLS-capable FluidSynth configuration, and builds only the VST3 target.
  BLOCKED: The legacy LLVM-MinGW image has not been rebuilt or host-validated and remains unsupported.

- [ ] Make packaging deterministic.
  - Validate the version argument.
  - Derive the package version from the canonical project version where possible.
  - Start from an empty staging directory.
  - Fail when the expected VST3 artifact is absent.
  - Avoid carrying stale files from earlier packages.
  - Include only approved formats and architectures.

  EVIDENCE: `distribute/bundle_win32.sh` validates canonical SemVer, clears versioned staging, requires the x64 VST3 artifact, packages notices/docs only, and emits SHA-256; syntax/error paths were verified locally.
  BLOCKED: A real Docker artifact is required to verify the complete staging/archive path.

- [ ] Validate Windows artifacts.
  - Inspect binary architecture and runtime DLL dependencies.
  - Run the official Steinberg VST3 validator if licensing/availability permits.
  - Test in at least two VST3 hosts, including Cubase where Program Change behavior matters.
  - Test on a clean minimum-version Windows VM without developer dependencies.

### Acceptance criteria

- [ ] A clean clone can produce the Windows VST3 using documented commands.
- [ ] The artifact loads without missing DLLs.
- [ ] SF2, SF3, and DLS corpus tests pass on Windows.
- [ ] Sixteen-channel Program Change routing passes host validation.

## Phase 4 exit criteria

- [ ] macOS and Windows builds are reproducible from clean environments.
- [ ] Release artifacts are portable and architecture-correct.
- [ ] Signing and validation checks pass.
- [ ] Packaging contains only approved formats, documentation, and notices.

---

# Phase 5 — Establish automated quality gates

## Goal

Turn currently manual smoke checks into repeatable gates that prevent regressions.

## 5.1 Test infrastructure

- [x] Enable CTest in the top-level CMake project.
- [x] Convert `tools/font_qa.cpp` into registered tests.
  - Keep pure DLS repair unit tests separate from optional corpus tests.
  - Make the no-corpus case explicit rather than reporting `0 ok` as if compatibility was exercised.

- [x] Register and automate `vst3_smoke` on supported macOS builds.
- [x] Add a core processor/engine test target capable of rendering blocks offline.
- [x] Add state serialization and migration tests.
  - Current state round trip.
  - Pre-v2 sound-controller migration.
  - Missing and malformed XML.
  - Out-of-range selected channels, banks, presets, and controller values.
  - Missing or moved font files.

- [x] Add patch-list tests.
  - Sparse banks.
  - Duplicate bank/preset entries.
  - Sorting.
  - Unicode and long names.
  - Empty banks.

  EVIDENCE: The offline engine target exercises empty and sparse bank trees, duplicate presets, unsorted input, percussion bank 128, and preserved long Unicode names.

- [x] Add release-metadata tests.
  - CMake version, UI version, AU/VST3 metadata, and package filename agree.
  - Plugin identifiers match the approved identity manifest.
  - Required formats are present and forbidden formats absent.

## 5.2 Continuous integration

- [ ] Add CI for macOS and Windows.
- [ ] Build Debug and Release configurations.
- EVIDENCE: Debug and static Release builds and test suites passed locally on arm64 macOS on 2026-08-05.
- [ ] Treat first-party compiler warnings as tracked debt.
  - Remove extra semicolons and unused parameters.
  - Mark overriding destructors correctly.
  - Migrate deprecated JUCE constructors and MIDI iteration APIs.
  - Fix signedness at FluidSynth API boundaries.
  - Remove unused functions, fields, variables, and commented-out code.

  EVIDENCE: The local Debug build emits no first-party compiler warnings; one warning remains in the bundled Steinberg VST3 SDK header under sanitizer compilation.

- [ ] Add formatting or lint checks appropriate to the project.
- [ ] Add sanitizer jobs where supported.
- [ ] Archive validation logs and unsigned test artifacts where appropriate.
- [ ] Protect release tags so they can only be created from a passing commit.

### Acceptance criteria

- [x] `ctest --output-on-failure` runs meaningful tests and passes on supported platforms.

  EVIDENCE: Five registered tests pass in ASan+UBSan Debug and static Release: DLS repair unit, system DLS load, VST3 multitimbral smoke, release metadata consistency, and offline engine/MIDI behavior.
- [ ] CI catches timing, state, DLS, VST3-routing, metadata, and packaging regressions.
- [ ] Release builds contain no unresolved first-party warnings selected as errors by policy.

## Phase 5 exit criteria

- [ ] Core tests and platform integration tests run automatically.
- [ ] CI covers all approved release platforms.
- [ ] Release creation depends on passing quality gates.

---

# Phase 6 — Consolidate documentation, metadata, privacy, and notices

## Goal

Ensure every user-facing and developer-facing surface describes the same implemented and tested product.

## 6.1 Canonical documentation structure

- [x] Rewrite `README.md` around the approved product contract.
  - Concise product description.
  - Exact supported platforms and formats.
  - Sixteen-channel routing model.
  - SF2/SF3/DLS support and tested limitations.
  - Program Change and Bank Select behavior.
  - GM channel 10 behavior.
  - Single stereo-output limitation.
  - Installation links and build links.

- [x] Replace `building.macos.md` with a tested macOS developer guide.
  - Exact tool and dependency versions.
  - Debug versus portable Release builds.
  - Architecture and deployment target.
  - Signing, validation, and installation.
  - AU and VST3 targets only, plus clearly labeled QA standalone if retained.

- [ ] Replace `building.win32.md` with a tested Windows guide.
  - Native or CI toolchain requirements.
  - Clean-clone build commands.
  - VST3 packaging.
  - Dependency and runtime verification.
  - Host-validation commands and manual test matrix.

- [x] Add a troubleshooting guide.
  - Plugin not discovered.
  - Invalid signature/quarantine.
  - Missing bank after reopening a session.
  - Unsupported or corrupt font.
  - Program Change routing differences among hosts.
  - Channel 10/percussion expectations.

- [x] Add release notes or a changelog beginning with the remediation release.

## 6.2 Terminology and claims

- [ ] Standardize `SoundFont`, `SF2`, `SF3`, and `DLS` capitalization.
- [x] Use “16-channel multitimbral DLS/SoundFont player” consistently.
- [ ] Keep “General MIDI sound module” only if the completed GM tests justify it.
- [ ] Use approved Fruity LSD comparison language.
- [ ] Remove absolute compatibility claims that exceed the corpus and host tests.
- [x] Clearly label standalone and any legacy formats.

## 6.3 Version and identity single source of truth

- [x] Eliminate manually duplicated version values where possible.
  - Generate the UI version from the CMake project version.
  - Remove or regenerate stale `ProjectInfo` and `AppConfig` values.
  - Derive package versions from the same source.

- [ ] Apply the approved company, website, email, bundle identifier, manufacturer code, and plugin code consistently.
- [ ] Add a non-empty copyright statement if appropriate.
- [x] Add an automated metadata consistency test.

## 6.4 Privacy and licensing documents

- [x] Replace `PRIVACY.txt` with an accurate privacy statement.
  - State whether the plugin itself performs network access or telemetry.
  - Distinguish build-time services from runtime behavior.
  - Remove obsolete JUCE 5/ROLI statements.

- [ ] Update the top-level license to match the Phase 0 decision.
- [ ] Replace the obsolete JUCE GPLv3 notice with the applicable JUCE 8/commercial notice.
- [ ] Inventory all code and binary dependencies actually present in each release.
  - JUCE embedded dependencies such as HarfBuzz and SheenBidi.
  - FluidSynth and all statically linked or bundled dependencies.
  - VST3 SDK licensing notice where required.
  - DLS-related dependencies if `libinstpatch` is retained.

- [ ] Include source-offer and relinking materials required by the chosen licenses.
- [ ] Ensure package scripts include all required notices and no obsolete ones.

### Acceptance criteria

- [ ] A reader can determine exactly what the app supports without reading source code.
- [ ] Build guides reproduce the CI/release builds.
- [ ] UI, source, binary metadata, package names, and docs report the same version and identity.
- [ ] Privacy and licensing documents accurately describe the distributed artifacts.

## Phase 6 exit criteria

- [ ] All active documentation is current and tested.
- [ ] Stale generated files and legacy-format references are removed or clearly quarantined.
- [ ] Licensing and privacy materials are complete for distribution.

---

# Phase 7 — Beta-candidate validation

## Goal

Prove that the exact Beta 1 candidate artifacts meet the product contract on clean systems and real hosts.

## 7.1 Automated release checks

- [ ] Build all candidate artifacts from a clean tagged commit.
- [ ] Confirm the worktree and submodule/dependency state are recorded.
- [ ] Run the complete unit and integration test suite.
- [ ] Run the SF2/SF3/DLS compatibility corpus on each platform artifact.
- [ ] Run VST3 smoke/validator checks.
- [ ] Run `auval` against the packaged AU.
- [ ] Verify signatures, architectures, deployment targets, and linked dependencies.
- [ ] Verify package contents and license notices.
- [ ] Verify all artifact hashes are recorded.

## 7.2 Host matrix

Test the exact packaged artifacts, not a separate local build.

### macOS AU

- [ ] Logic Pro: discovery, load, 16-channel MIDI, Program Change, state restore, UI resize, file restore.
- [ ] At least one additional AU host, such as FL Studio: the same critical workflow.

### macOS VST3

- [ ] Cubase: pre-connection unit discovery and per-channel Program Change.
  - Confirm Cubase discovers 16 channel units, not only a root unit.
  - Confirm Cubase associates each incoming MIDI channel with the corresponding VST3 unit.
  - Confirm Program Change on channels 1, 2, 10, and 16 changes only that channel.
  - Confirm Program Changes on all 16 channels during one playback pass.
  - Confirm Program Changes at tick 0, after transport restart, and later in the song.
  - Confirm the plugin UI and Cubase-visible program parameters follow the sounding engine state.
  - Save, close, reopen, and verify that Cubase restores the same programs before playback.
- [ ] At least one additional VST3 host: automation, state, and MIDI/audio behavior.

### Windows VST3

- [ ] Cubase: repeat the complete macOS Cubase Program Change matrix and test all font formats.
- [ ] At least one additional Windows VST3 host.

### Required FL Studio versus Cubase comparison

- [ ] Run the same multichannel game-rip MIDI and bank fixture in FL Studio and Cubase.
- [ ] Begin each test from a fresh plugin instance with no manual channel assignments.
- [ ] Record the bank/program selected on all 16 channels at defined checkpoints.
- [ ] Require identical instrument assignments and change timing in both hosts.
- [ ] Confirm FL Studio exercises the expected `IMidiMapping` behavior.
- [ ] Confirm Cubase exercises the expected unit/program-parameter behavior rather than collapsing to channel 1.
- [ ] Repeat after stop/start, rewind, loop restart, project reload, and plugin window close/reopen.
- [ ] Capture evidence: host versions, routing screenshots, expected/actual matrix, and an audio or event trace where practical.
- [ ] Classify any “FL works, Cubase fails” or “only channel 1 works” result as B1 and reject the candidate.

### Every host/platform combination

- [ ] Load SF2, SF3, well-formed DLS, and repaired DLS.
- [ ] Route all 16 MIDI channels simultaneously.
- [ ] Confirm channel 10 percussion.
- [ ] Confirm timestamped notes and Program Changes.
- [ ] Exercise dense playback near the configured 512-voice limit.
- [ ] Change sample rate among 44.1, 48, 88.2/96 kHz where supported.
- [ ] Test common block sizes.
- [ ] Save, close, reopen, and verify complete state restoration.
- [ ] Send GM, GS, and XG resets followed by notes in the same and subsequent blocks.
- [ ] Confirm no crash or state corruption when a font is missing or invalid.
- [ ] Play the canonical game-rip fixture from the beginning without manual patch selection and verify every instrument checkpoint.
- [ ] Run the canonical CC/pitch-bend fixture and verify exact per-channel controller values.
- [ ] Verify modulation, volume, pan, expression, sustain, sostenuto, soft pedal, reverb send, chorus send, All Sound Off, Reset All Controllers, and All Notes Off.
- [ ] Verify full-down, center, and full-up 14-bit pitch bend on channels 1, 2, 10, and 16.
- [ ] Verify per-channel RPN pitch-bend ranges and simultaneous independent bends.
- [ ] Verify controller and bend timing within a block, at loop boundaries, and when playback begins mid-song with host chase enabled.

## 7.3 Clean-system installation

- [ ] Install on the minimum supported macOS system without Homebrew dependencies.
- [ ] Install on the current macOS release.
- [ ] Install on the minimum supported Windows system without developer runtimes beyond documented requirements.
- [ ] Install on the current Windows release.
- [ ] Verify uninstall/removal instructions.

## 7.4 Final documentation review

- [ ] Follow installation and build instructions exactly from a clean environment.
- [ ] Check every internal link and command.
- [ ] Confirm version, support matrix, filenames, and checksums.
- [ ] Confirm known limitations are honest and complete.
- [ ] Obtain final licensing/privacy approval.

## Beta 1 technical gate

- [ ] All earlier phase exit criteria are complete.
- [ ] No open B0 or B1 defect remains.
- [ ] Every supported artifact passes automated and host validation.
- [ ] Clean-system installation succeeds.
- [ ] Documentation, privacy, licensing, and package contents are approved.
- [ ] The beta tag and artifacts are produced from the same verified commit.

---

# Phase 8 — Beta 1 program readiness and controlled launch

## Goal

Launch the first beta as a controlled, supportable evaluation rather than simply uploading binaries.

## 8.1 Tester contract and safety messaging

- [x] Define the intended tester audience.
  - DAW familiarity expected.
  - Required operating systems and architectures.
  - Required ability to route multiple MIDI channels.
  - Whether Cubase, Logic, FL Studio, and other host-specific testers are being recruited.

- [x] Write a short Beta 1 warning shown on the download page and in the package.
  - This is pre-release software.
  - Testers should back up projects and avoid relying on the beta for irreplaceable work.
  - State migration compatibility with later betas is a goal but not guaranteed unless explicitly promised.
  - List known B2 limitations and host-specific caveats.
  - Explain that font files are never intentionally modified; malformed DLS repair uses an internal temporary copy.

- [x] Define the support boundary.
  - Supported versus untested hosts.
  - Supported operating-system versions.
  - Supported bank formats and known corpus coverage.
  - Whether unsigned, modified, or third-party-repackaged builds receive support.

- [x] Publish concise uninstall and rollback instructions.

  EVIDENCE: `docs/BETA_TESTER_GUIDE.md` covers format-specific removal, backup restoration, DAW rescan, and project-state rollback without touching user banks/projects.
  - AU location.
  - VST3 locations on macOS and Windows.
  - DAW rescan/cache steps.
  - How to restore the prior plugin build if compatible identifiers are retained.

### Acceptance criteria

- [ ] A tester can determine whether their setup is supported before downloading.
- [x] Backup, state-compatibility, known-risk, and uninstall expectations are visible.

  EVIDENCE: `docs/BETA_TESTER_GUIDE.md` defines the experienced multichannel-DAW tester audience, mandatory backup warning, candidate-specific support boundary, unsupported formats, reporting expectations, uninstall, and rollback.

## 8.2 Beta package contents

- [ ] Include the exact AU/VST3 artifacts approved by Phase 7.
- [ ] Include `README`, installation instructions, known issues, privacy statement, project license, third-party notices, and changelog/release notes.
- [ ] Include version and candidate number in package filenames.
- [ ] Include SHA-256 checksums outside and, optionally, inside the package.
- [ ] Do not include build directories, object files, developer paths, stale architectures, unsupported formats, or unrelated SDK material.
- [ ] Confirm archive extraction preserves macOS bundle structure and executable permissions.
- [ ] Scan the final archive for secrets, signing credentials, usernames, absolute developer paths, and unintended personal data.
- [ ] Extract the uploaded archive into a clean directory and rerun signature, metadata, dependency, and host-discovery checks.

### Acceptance criteria

- [ ] The downloaded artifact is byte-for-byte the approved artifact or has a documented packaging transformation.
- [ ] Package contents match the published manifest.
- [ ] Checksums match after upload and download.

## 8.3 Diagnostic and feedback design

- [ ] Choose the feedback channel and publish a structured issue template.
  - Beta/candidate version.
  - Plugin format.
  - Operating system and architecture.
  - DAW name and exact version.
  - Audio sample rate and block size.
  - Font type and, when redistributable, a link or checksum.
  - MIDI routing setup and affected channel.
  - Reproduction steps.
  - Expected and observed behavior.
  - Whether the issue reproduces in a new empty project.
  - Crash log, validator output, screenshots, and minimal project where safe.

- [ ] Decide what diagnostics the plugin exposes in Beta 1.
  - Keep the visible plugin version.
  - Consider an opt-in “copy diagnostic information” action containing only non-sensitive build/runtime facts.
  - Do not collect or transmit telemetry without the approved privacy design and explicit documentation.
  - Never include full user paths, project names, or font contents by default.

- [x] Define crash-log collection instructions for macOS and Windows.
- [x] Create a reproducibility checklist for maintainers.
- [ ] Define how tester-submitted fonts/projects are stored, accessed, and deleted.
- [x] Establish labels for B0/B1/B2/B3, host, platform, format, font type, state, MIDI routing, UI, and performance.

  EVIDENCE: `docs/TRIAGE.md` and `.github/ISSUE_TEMPLATE/beta_bug.yml` define the minimum dataset, reproduction order, crash-log handling, severity, and labels. Feedback destination/retention owner remain open.

### Acceptance criteria

- [x] Every beta report can be triaged using a consistent minimum dataset.
- [x] Diagnostic guidance is compatible with the approved privacy statement.

## 8.4 Beta compatibility and migration policy

- [x] Document whether Beta 1 state must load in Beta 2 and the stable release.
- [x] Assign a new state schema version for any incompatible remediation change.
- [x] Preserve tests for older state versions retained by policy.
- [x] Decide what happens when a newer state is opened by an older beta.
- [x] Document plugin identifier stability across beta candidates.
- [x] Maintain a migration note for every state, parameter, unit-ID, plugin-ID, or file-path change.

### Acceptance criteria

- [x] Candidate updates do not silently reinterpret saved bank, preset, controller, or channel values.
- [x] Any intentional incompatibility is detected and explained rather than producing corrupt state.

  EVIDENCE: `docs/STATE_COMPATIBILITY.md` freezes schema-v2 semantics and compatibility surfaces; automated tests cover v1 migration, v2 round-trip, malformed/bounded state, and visible safe rejection of a newer schema.

## 8.5 Performance baseline

- [ ] Select representative performance projects.
  - One-channel light playback.
  - Sixteen-channel typical GM arrangement.
  - Dense 512-voice stress case.
  - Large SF2/SF3 and representative DLS loads.
  - Frequent Program Changes and controller automation.

- [ ] Record CPU usage, peak CPU, memory after load, load time, and glitch behavior on representative macOS and Windows systems.
- [ ] Test common sample rates and buffer sizes.
- [ ] Test repeated bank loads for leaks or unbounded memory growth.
- [ ] Test repeated editor open/close and plugin instance create/destroy cycles.
- [ ] Test multiple plugin instances up to a documented reasonable limit.
- [ ] Define Beta 1 performance thresholds and classify failures using the severity policy.

### Acceptance criteria

- [ ] Normal 16-channel playback is stable at the agreed minimum system and buffer size.
- [ ] Stress limits are documented so testers can distinguish expected limits from regressions.

## 8.6 Accessibility and UI beta pass

- [ ] Verify every interactive control has a useful accessible name and role.
- [ ] Verify keyboard navigation does not interfere with the on-screen MIDI keyboard unexpectedly.
- [ ] Verify focus initialization is deterministic.
- [ ] Check light/dark appearance and host-provided scaling where supported.
- [ ] Check minimum, default, and maximum editor sizes.
- [ ] Confirm all 16 rows remain reachable at minimum height.
- [ ] Check long font paths, long Unicode preset names, sparse banks, and missing presets.
- [ ] Check error messages for readability and recovery actions.
- [ ] Check color contrast for selected rows, labels, sliders, and disabled states.
- [ ] Verify the UI identifies the selected MIDI channel and loaded bank clearly enough for beta diagnosis.

### Acceptance criteria

- [ ] Core loading, channel selection, patch selection, and parameter editing workflows are usable without a mouse where the framework/host permits.
- [ ] No supported resize or text case makes essential controls inaccessible.

## 8.7 Security and robustness pass

- [x] Fuzz or stress malformed state blobs and RIFF/DLS headers within a safe harness.
- [ ] Verify huge, empty, truncated, read-only, inaccessible, and concurrently removed font files fail safely.
- [ ] Verify archive/package extraction does not rely on unsafe paths or executable installers not covered by signing policy.
- [x] Confirm no runtime networking occurs unless explicitly approved and documented.
- [x] Confirm temporary repaired DLS copies use safe unique names, restricted locations, and reliable cleanup.

  EVIDENCE: The ASan+UBSan harness covers 1,000 malformed state blobs and 6,000 RIFF/DLS property inputs; JUCE curl/web-browser support is compiled out and no runtime networking call exists; DLS repair uses `File::createTempFile`, never edits the source, and deletes the active temp on replacement/destruction.
- [x] Confirm logs and diagnostic output do not expose sensitive paths or file contents by default.
- [ ] Review dependency versions for known critical security advisories before candidate freeze.

### Acceptance criteria

- [ ] No known B0 security or privacy issue remains.
- [ ] Malformed user-controlled files and state fail without memory corruption or destructive modification.

## 8.8 Go/no-go review

- [ ] Freeze the candidate commit and candidate number.
- [ ] Confirm all Phase 0–7 exit criteria required for Beta 1 are complete.
- [ ] Review every open issue by severity.
- [ ] Confirm zero open B0 and B1 issues.
- [ ] Confirm every open B2 issue is documented, approved, and assigned.
- [ ] Confirm candidate artifacts, package manifest, checksums, validation logs, and host results refer to the same commit.
- [ ] Confirm licensing/privacy approval and distribution authority.
- [ ] Confirm feedback and withdrawal owners are available during launch.
- [ ] Record the go/no-go decision in the Decision log.

## 8.9 Controlled launch

- [ ] Upload artifacts to the approved beta distribution channel.
- [ ] Download them again and verify checksums.
- [ ] Publish Beta 1 release notes, installation instructions, known issues, and feedback link together.
- [ ] Start with a small canary group if the tester pool is large.
- [ ] Confirm at least one successful install and plugin discovery on each platform/format before broadening access.
- [ ] Monitor initial reports closely for discovery failures, crashes, missing dependencies, state corruption, and severe audio faults.
- [ ] Be prepared to withdraw the candidate immediately if a B0 issue appears.

## 8.10 Beta feedback cycle

- [ ] Acknowledge and classify incoming reports.
- [ ] Reproduce B0/B1 reports against the frozen candidate before changing code where possible.
- [ ] Link duplicates and preserve the clearest reproduction.
- [ ] Separate product-scope requests from regressions and defects.
- [ ] Update the known-issues document when an approved workaround exists.
- [ ] Maintain a candidate-to-candidate changelog.
- [ ] Rerun the full relevant regression subset for every fix candidate.
- [ ] Require the complete Beta 1 technical gate again for any replacement public candidate.

## Phase 8 exit criteria

- [ ] Beta 1 is distributed with a complete tester contract and support boundary.
- [ ] Every public artifact is traceable to the validated candidate commit.
- [ ] Feedback intake, privacy handling, triage, rollback, and candidate replacement processes are operating.
- [ ] Initial canary installs succeed on every advertised platform and plugin format.
- [ ] No B0 issue is active; any replacement candidate has passed the same gate.

---

# Recommended workstream split

These workstreams may run in parallel after Phase 0, but their integration order should follow the phase gates above.

## Workstream A — Audio engine

- Phase 1 sample-accurate rendering.
- General MIDI and channel 10 behavior.
- Program/reset synchronization.
- Offline render and state tests.

## Workstream B — Font compatibility

- Transactional font loading and UI errors.
- DLS repair hardening.
- Licensed compatibility corpus.
- Cross-platform FluidSynth feature validation.

## Workstream C — Plugin formats

- VST3 units, mapping, and wrapper-patch cleanup.
- AU validation.
- Host integration harnesses.

## Workstream D — Build and release engineering

- Portable dependency linkage.
- macOS deployment, signing, and notarization.
- Supported Windows toolchain and packaging.
- CI and artifact validation.

## Workstream E — Governance and documentation

- Product scope and metadata decisions.
- Licensing and privacy review.
- User/developer documentation.
- Final release checklist and evidence collection.

---

# Decision log

Record decisions that affect more than one task. Do not delete superseded decisions; mark them superseded and link to the replacement.

| Date | Decision | Rationale | Owner | Status |
|---|---|---|---|---|
| YYYY-MM-DD | Example: minimum macOS version | Host/support rationale | Name | Proposed/Approved/Superseded |

# Risk register

| Risk | Impact | Mitigation | Status |
|---|---|---|---|
| JUCE license incompatible with declared project license | Distribution may be legally blocked | Resolve Phase 0 licensing decision before release | Open |
| Vendored VST3 wrapper is tied to a specific JUCE implementation | JUCE updates may break builds or routing | Pin JUCE, minimize patch, add diff and host tests | Open |
| FL Studio and Cubase use different VST3 Program Change paths | A change may appear correct in FL Studio while Cubase collapses to channel 1 or ignores changes | Preserve both mapping/unit paths and require the identical 16-channel game-rip fixture in both hosts | Open |
| Cubase caches unit/program-list information queried before component connection | Returning the stock root-only structure even briefly can break Program Change for the plugin instance | Keep the pre-connection smoke test and require identical unit data before and after connection | Open |
| CC or pitch-bend messages are value-correct but block-quantized or sent to the selected UI channel | Expression, pedals, bends, and game-rip automation sound wrong despite basic note playback working | Parameterize CC0–127 and 14-bit bend tests across channels and validate audio-domain timing | Open |
| Windows DLS support is disabled in the pinned FluidSynth build | Product claim fails on Windows | Upgrade FluidSynth or enable a DLS-capable build | Open |
| MIDI timestamps are ignored | Audible timing errors and incorrect same-block behavior | Complete Phase 1 segmented rendering | Open |
| macOS artifacts reference Homebrew libraries | Plugins fail on clean user systems | Static/embed dependencies and inspect every artifact | Open |
| Signing occurs before all resources are stable | AU/VST3 may fail discovery or validation | Fix build dependencies and verify after parallel clean builds | Open |
| Toolchain-default macOS deployment target is 26.0 | Current local artifact will not load on older supported Macs and is not a distributable Beta | Approve an explicit minimum in Phase 0, set `CMAKE_OSX_DEPLOYMENT_TARGET`, and rebuild/test with strict release validation | Open |
| No redistributable font corpus exists | Format compatibility cannot be regression-tested | Curate licensed fixtures with provenance | Open |
| Plugin identifier changes break existing sessions | Users may lose project recall | Approve identity once; document and test migration strategy | Open |

# Beta 1 completion record

When the Beta 1 gate passes, record:

- Beta version:
- Candidate number:
- Git commit:
- Beta launch date:
- macOS artifacts and SHA-256 hashes:
- Windows artifacts and SHA-256 hashes:
- CI run:
- Host-validation evidence:
- Licensing/privacy approval:
- Known limitations:
- Go/no-go decision owner:
- Withdrawal/rollback owner:
