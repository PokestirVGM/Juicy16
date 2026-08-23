# Juicy16 Beta 1 Candidate — Comprehensive Remediation and Release Plan

This document is the authoritative execution plan and candidate checklist for the **first public beta of Juicy16**. It brings the repository into alignment with the intended product:

> A cross-platform, 16-channel DLS/SoundFont player inspired by Fruity LSD, distributed primarily as AU on macOS and VST3 on macOS and Windows.

It converts the repository audit into sequenced, checkable milestones and adds the operational work required to distribute, observe, support, and evaluate a large first beta. Contributors and agents should update this file in the same change that completes a task so it remains the source of truth for Beta 1 readiness.

## Beta 1 identity

Complete these fields before producing the first candidate. The per-candidate
ones (commit, build date) are filled in by `BUILD_INFO.txt` when the package is
built, not typed in here by hand.

- Working version: `0.5.1-alpha.5`. Deliberately not `beta` — the Beta 1 bar below has not been met, and the label should not claim otherwise. Rename to `0.5.1-beta.1` (or the then-current version) when the technical gate passes.
- Candidate number: `BC1` initially; increment for every rebuilt candidate.
- Candidate commit:
- Candidate build date:
- Candidate coordinator: the author (single-developer project — see Project scale)
- Supported macOS versions: macOS 11.0 or later (Apple Silicon only)
- Supported Windows versions: Windows 10 version 1607 or later
- Supported architectures: macOS `arm64`; Windows `x86_64`
- Included formats: macOS AU/VST3; Windows VST3
- Tester group or distribution channel: to be named at publish time; not a
  precondition for building a candidate
- Feedback destination: `contact@pokestir.com` with subject prefix `[Juicy16 VST]`
- Emergency withdrawal owner: the author

## Project scale

Juicy16 is built and released by **one developer**. This plan was written in the
register of a corporate release process, and a good deal of that register does not
apply: there is no release board, no separate QA function, no on-call rotation,
and no tester pool large enough to need a canary phase. The author is the
candidate coordinator, the withdrawal owner, and the support channel.

Items that only exist to coordinate between people, or to manage volume that will
not occur, are marked `[-]` with a `DESCOPED:` reason rather than left open to rot.

What is deliberately **not** descoped, because it protects someone other than the
author or is a legal obligation:

- Licensing, notices, and complete corresponding source. GPLv3/AGPLv3 are binding
  on distribution regardless of project size.
- The privacy statement, for the same reason.
- Checksums on published artifacts. Cheap, and they catch a corrupt upload.
- Gatekeeper instructions. Without them an ad-hoc signed build simply will not
  open on a tester's machine.
- The host test matrix, clean-system installation, and Windows proof. These are
  the actual product risks, and no amount of process substitutes for running it.
- Zero open B0 or B1 defects.

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
- `[-]` Descoped: deliberately not done, because it does not apply at this
  project's scale. Every one carries a `DESCOPED:` line giving the reason. This is
  a real decision, not a shortcut — descoped items are excluded from the Beta 1
  gate, so the reason has to survive being read back later.
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
- CCs that are not represented by plugin UI controls still reach FluidSynth correctly; UI mirroring is required only for the explicitly exposed mixer parameters (CC7 volume, CC10 pan).
- Manual patch selection and host automation remain synchronized with engine state and saved state.
- GM/GS/XG reset messages do not silently destroy saved channel assignments.
- All channels mix to one stereo output for this release.
- Supported bank files are `.sf2`, `.sf3`, and `.dls` on every advertised platform.

### Non-negotiable game-rip playback workflow

Beta 1 must preserve the workflow that motivated the 16-channel VST3 implementation:

1. Insert one Juicy16 instance.
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

- [x] Confirm the supported platform and architecture matrix.
  - DECISION: Beta 1 targets macOS 11.0 or later on Apple Silicon `arm64`, and Windows 10 version 1607 or later on `x86_64`. Intel macOS may be added in a later release when build dependencies and physical validation are available. Windows ARM64 and Linux are out of scope.
  - Decide the minimum macOS version.
  - Decide whether macOS releases are universal `arm64;x86_64` or Apple Silicon only.
  - Decide the minimum Windows version; JUCE 8 documentation currently starts at Windows 10 version 1607.
  - Decide whether Windows ARM64 is in scope now, later, or not planned.
  - Decide whether Linux VST3 is in scope; do not imply it through the phrase “cross-platform” unless it is tested.

  EVIDENCE: Owner decisions on 2026-08-19; canonical matrix is `docs/SUPPORT_MATRIX.md`. These are approved targets, not claims that candidate artifacts have passed their platform matrices.

- [x] Confirm release-format scope.
  - Approve AU and VST3 as primary release formats.
  - Decide whether standalone remains a QA-only target.
  - Decide whether all VST2 code, build options, and documentation should be removed or retained as unsupported legacy functionality.
  - Decide whether AUv3 is explicitly out of scope.

  EVIDENCE: Owner decision on 2026-08-19 approves macOS AU/VST3 and Windows VST3 as the only Beta release formats; Standalone is development/QA-only; VST2 and AUv3 are out of scope.

- [x] Confirm product ownership and metadata.
  - DECISION: Product `Juicy16`; public brand `Pokestir`; website `https://pokestir.com`; support `contact@pokestir.com`; bundle ID `com.pokestir.juicy16`; manufacturer code `Pkst`; plugin code `Jc16`; new-work notice `Copyright (c) 2026 Pokestir`. The original Birchlabs/Alex Birch notices and lineage credit remain. A release signing identity is still unavailable.
  - Replace the pre-Beta `Birchlabs`, `birchlabs.co.uk`, `Blbs`, and `com.Birchlabs.JuicySFPlugin` metadata while retaining historical attribution.
  - If ownership changed, choose the company name, website, support email, bundle identifier, manufacturer code, plugin code, copyright text, and signing identity.
  - Document compatibility consequences before changing plugin identifiers; identifier changes can break existing DAW sessions.

  EVIDENCE: The owner approved a Beta 1 identity reset and explicitly waived pre-Beta session compatibility. `docs/STATE_COMPATIBILITY.md` freezes the new identifiers beginning with Beta 1; metadata tests enforce their CMake values. Release signing remains a Phase 4 blocker.

- [ ] Resolve the JUCE licensing model.
  - DECISION: Use JUCE 8 under AGPLv3; no commercial JUCE license is claimed. The inherited application code remains GPLv3. GNU GPLv3/AGPLv3 section 13 permits the combined work while each license continues to cover its respective parts.
  - OWNER DECISION (2026-08-23): qualified external review is **waived**; the owner self-reviews the exact package against `docs/LICENSING.md` before candidate freeze. Recorded in the Decision log as an explicit override, with a small named tester list and public source at the beta tag as the mitigations. The item stays open until that self-review has actually been performed against the frozen package.
  - Choose one of: a commercial JUCE 8 license, AGPLv3-compatible project distribution, or a technically and legally viable framework/version alternative.
  - Have the chosen position reviewed by an appropriately qualified person before public distribution.
  - Decide the resulting top-level project license and source-offer obligations.
  - Record the decision in a short licensing note or architecture decision record.

  PARTIAL EVIDENCE: `docs/LICENSING.md`, `NOTICE.md`, the retained GPLv3 `LICENSE.txt`, and `licenses_of_dependencies/JUCE-framework_AGPL3.txt` document the selected path and source-distribution obligations. Package staging includes these materials. Final qualified review remains open.

- [x] Confirm the intended relationship to Fruity LSD.
  - Approve wording such as “inspired by” or “Fruity LSD-style workflow.”
  - Avoid claims of exact emulation unless a documented compatibility matrix supports them.
  - List any Fruity LSD behaviors explicitly outside this release.

  EVIDENCE: Owner approved “inspired by Fruity LSD” on 2026-08-19. Documentation limits the comparison to automatic multichannel patch selection and explicitly disclaims exact emulation or affiliation.

## Exit criteria

- [x] A support matrix is written and approved.
- [x] Release formats and development-only formats are unambiguous.
- [x] Product identity values are approved.
- [x] The licensing path is approved and documented.
- [x] Later phases have no unresolved scope question that would invalidate their implementation.

  EVIDENCE: Owner decisions on 2026-08-19 establish the Juicy16/Pokestir identity, formats, platforms, GPLv3 application license, and JUCE AGPLv3 path. `docs/BETA1_IDENTITY_CONTRACT.md` records the frozen host identifiers. Final qualified review of the exact distribution package remains an explicit release gate, but no implementation-defining Phase 0 choice is unresolved.

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

  Sample accuracy is now also proven in the audio domain rather than only through
  reported engine state. Two scenarios added on 2026-08-19 render the same note
  under different programs from isolated processor instances and compare the
  waveforms by normalized cross-correlation: two different GM programs correlate
  below 0.9, and a Program Change placed one sample before the note renders
  identically (above 0.999) to the same change at block start. Together these
  show the timestamp is honoured by the synthesis, not merely recorded.

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

- [x] A normal GM MIDI file that relies on the channel 10 convention plays percussion without requiring an explicit Bank Select.
- [x] Manual channel 10 bank/preset selection still works and persists.

  EVIDENCE: The offline engine suite verifies melodic bank 0 defaults on other channels, channel 10 bank 128, percussion-bank preservation across Program Change and GM/GS/XG reset, and manual channel 10 assignment across state round-trip using the macOS system DLS. Its fresh-instance GM scenario sends only a timestamped channel-10 drum note—no Bank Select or Program Change—and proves silence before the event, audible percussion afterward, and bank 128/program 0 at dispatch. The strict arm64 macOS Release test passed on 2026-08-19.

## 1.3 Program, bank, reset, and state synchronization

- [x] Unify all program-change paths behind one engine-state function.
  - Cover MIDI Program Change, `progChN` host automation, global selected-channel parameters, manual dropdown selection, state restoration, and font reload.
  - Update `engineBank` and `enginePreset` consistently on every successful engine change.
  - Update UI/state mirrors only on the message thread.
  - Return or record errors from failed FluidSynth calls.

  EVIDENCE: `FluidSynthModel::applyProgramToEngine` is the sole FluidSynth program mutation point for raw MIDI Program Change, `progChN` host automation, selected-channel bank/preset parameters, manual dropdown assignment, state/font reload, and reset reassertion. `tests/EngineMidiTests.cpp` verifies engine, per-channel persisted state, and host-parameter convergence for the independently routed entry points, plus failed-assignment rollback and per-channel error recording. All five registered Debug and static Release tests passed on arm64 macOS on 2026-08-19; AU, VST3, and Standalone built in both configurations.

- [x] Make reset recovery deterministic.
  - Immediately restore the correct engine bank/preset after supported reset SysEx.
  - Restore sound-controller state without racing newer MIDI events.
  - Ensure the asynchronous full resync cannot overwrite a more recent Program Change.
  - Test multiple resets and transport restarts.

  EVIDENCE: GM/GS/XG resets synchronously reapply the latest engine program and six exposed sound-controller atomics before MIDI dispatch continues; resets do not queue a stale full-state resync. The offline engine suite captures the program present immediately before note-on and verifies reset-before-note ordering at an equal timestamp, three consecutive reset families, newer same-block Program Change/CC events, and repeated restart-style reset/setup playback. All five registered Debug and static Release tests passed on arm64 macOS on 2026-08-19; AU, VST3, and Standalone built in both configurations.

- [x] Decide how Bank Select state is represented.
  - Verify CC0/CC32 behavior for SF2, SF3, and DLS banks.
  - Ensure the UI displays the bank actually selected by the engine.
  - Document the limitation that VST3 `progChN` exposes only a 0–127 program number unless bank automation is added.

  EVIDENCE: Beta 1 explicitly configures FluidSynth 2.5.5 for initial GS Bank
  Select semantics instead of inheriting its default. The persisted value is the
  font's logical bank, never the pending CC bytes, and
  `docs/CONTROLLER_SUPPORT.md` defines that contract along with the VST3
  `progChN` program-only limitation.

  Cross-bank coverage no longer depends on acquiring a bank. `tests/SyntheticSf2.h`
  writes a minimal valid SF2 during the test run with presets in banks 0, 1, 8,
  and 128, each a looped sine at its own frequency with scale tuning disabled, so
  the rendered pitch names the preset FluidSynth actually chose. The suite proves
  CC0 reaches banks 1 and 8, CC32 does not move the channel out of the bank CC0
  chose, CC0 = 0 restores the melodic bank, and channel 10 reaches bank 128 with
  no Bank Select — with engine, saved channel state, editor parameters, and audio
  agreeing at each step. The macOS system DLS covers banks 0 and 1 on a real DLS.

  Two findings came out of writing it. `getChannelProgram` and
  `getLastDispatchedNoteOnProgram` reported the raw engine bank while the state
  and UI reported the logical one; both now report the font's own numbering, so
  the whole public surface speaks one vocabulary. And an undefined bank/program is
  accepted rather than refused: FluidSynth records the request and substitutes
  bank 0 program 0 for synthesis, so the visible patch and the audible one differ
  while the wrong bank is loaded. Juicy16 keeps the requested value deliberately —
  it is what should be restored when the project is reopened with the intended
  bank — and the behaviour is asserted in both directions and published as a B2
  limitation in `docs/KNOWN_ISSUES.md`.

  Residual: the pinned SF3 fixture defines only banks 0 and 128, so SF3 is proven
  for percussion-versus-melodic selection only. Bank lookup is FluidSynth code
  shared above the format-specific sample decoding, but no SF3 bank with a melodic
  bank above 0 has been measured. Recorded in `docs/TEST_CORPUS.md`.

- [x] Represent the full drum-channel Bank Select range in the visible state.
  - Widen the `bank` parameter beyond 0-128 so a drum channel's 128 + MSB value
    (up to 255) is representable, or carry the drum offset outside the parameter.
  - Keep engine, UI, host parameter, and saved state in agreement after CC0=127
    on channel 10.
  - Extend the drum-bank scenario to assert the three surfaces match rather than
    documenting that they do not.

  OWNER DECISION (2026-08-23): this is no longer accepted as a B2. The 2026-08-20
  acceptance argued against moving a frozen automation surface — but Beta 1 has
  not shipped, alpha.5 already moved that surface (24 parameters to 21, state
  schema 2 to 3), and `0.6.0-beta.1` is the release that freezes it. The audio is
  unaffected either way; the cost of fixing it after the freeze is much higher
  than the cost of fixing it now.

  EVIDENCE (2026-08-23, `0.5.1-alpha.6`): `bank` now spans 0-255 through
  `MidiConstants::maxChannelBank`, and the same limit governs the restore clamp,
  the UI mirror, and `setChannelProgram`. The drum-bank scenario asserts the
  agreement instead of the divergence it used to pin: with channel 10 selected,
  CC0=127 reports 255 on the engine, the saved state, and the visible parameter,
  and reopening the project restores 255 rather than falling back to 128. A third
  defect surfaced while fixing it — no font defines bank 255, so the reload path's
  absent-program fallback would have put a restored drum channel on the font's
  first melodic preset. A drum-range bank is now restored through Bank Select then
  Program Change, the route live MIDI takes, so FluidSynth substitutes the kit and
  the channel keeps its bank. State schema 3 to 4 rescales an older save's
  normalised bank, pinned by its own regression; `docs/BETA1_IDENTITY_CONTRACT.md`
  now freezes parameter ranges alongside IDs. All 16 CTests pass.

### Acceptance criteria

- [x] Every program-change entry point produces the same engine, UI, parameter, and persisted state.
- [x] Reset tests demonstrate no stale-program window before subsequent notes.
- [x] Bank-offset handling is covered by tests.

  EVIDENCE: Juicy16 never installs a FluidSynth bank offset, which left every
  raw/logical conversion in the program paths running only at offset 0.
  `FluidSynthModel::getLoadedFontBankOffset`/`setLoadedFontBankOffset` let the
  offline harness install one; with an offset of 10 the suite asserts that manual
  exact-bank selection, MIDI Bank Select, the persisted channel state, the editor
  parameters, and the sounding preset all resolve to the font's own bank numbering,
  and that clearing the offset restores ordinary selection. This is what exposed
  the raw-versus-logical reporting split in the diagnostics above.

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
- [x] Handle host sample rates above FluidSynth's 96 kHz ceiling without silence.
  - Current behavior: rates above 96 kHz mute deliberately, so the plugin loads,
    reports the rate in the status bar, and produces nothing. `auval` surfaced it
    on 2026-08-23 at 192 kHz.
  - Preferred fix: render at a supported rate and resample to the host rate, so a
    192 kHz project plays rather than reporting why it does not.
  - Minimum acceptable fix: make the condition unmissable at the point of failure
    rather than a status line a user may never read.

  OWNER DECISION (2026-08-23): not accepted as a B2. A tester at 192 kHz hears a
  dead plugin, and "change your project rate" is not an answer a beta tester
  should have to find.

  EVIDENCE (2026-08-23, `0.5.1-alpha.6`): the preferred fix was taken. Above the
  ceiling the engine renders at the largest integer fraction of the host rate it
  accepts — 96 kHz for a 192 kHz project, 88.2 for 176.4 — and each block is
  interpolated back up, with rendered-but-unconsumed internal samples carried to
  the next block so the interpolator's fractional position neither drops nor
  repeats a sample. Proved against a synthesised 441 Hz fixture: pitch holds
  within 2% at both rates over a window spanning five block boundaries, and
  amplitude matches the directly rendered 96 kHz control, which a path that
  dropped or repeated samples would not. Event timing quantises to one internal
  sample (about 10 microseconds) at those rates, which is recorded in
  `docs/KNOWN_ISSUES.md`. Rates below FluidSynth's floor still mute rather than
  detune, pinned by its own test. The normal path is untouched: it takes the
  same branch it always did whenever the host rate is one FluidSynth accepts.

  Confirmed on the symptom that raised it: `auval -strict -q -v aumu Jc16 Pkst`
  against the installed strict-Release AU now logs "host sample rate 192000.0 Hz
  is above FluidSynth's 96000.0 Hz ceiling; rendering at 96000.0 Hz and
  interpolating up" where it used to log "so audio is muted", and still reports
  AU VALIDATION SUCCEEDED. The full strict portable Release gate passes 15/15 at
  `0.5.1-alpha.6`.

### Acceptance criteria

- [x] Corrupt or out-of-range saved state cannot access memory outside channel arrays.
- [x] Sanitizer runs have no unresolved first-party findings.
- [x] The real-time path performs no known unbounded allocation or unsafe UI/state mutation.

  EVIDENCE: `docs/THREADING_MODEL.md`; separate ASan+UBSan and TSan engine/font
  harness builds passed on arm64 macOS on 2026-08-05, and the ASan+UBSan gate
  passed again on 2026-08-19.

  A line-by-line audit of the audio-thread entry points on 2026-08-19 found one
  remaining allocation and removed it. `processBlock` called
  `MidiBufferIterator::getMessage()`, and `MidiMessage` heap-copies any message
  longer than four bytes — so every SysEx allocated on the audio thread, and game
  rips carry a GM/GS/XG reset at tick 0. SysEx is now dispatched directly from
  the `MidiBuffer`'s own storage via `FluidSynthModel::dispatchSysEx`, with
  identical framing semantics to `MidiMessage::getSysExData()`; messages of four
  bytes or fewer keep the inline, non-allocating `MidiMessage` route.

  The rest of the path is allocation-free by construction: `renderSamples` writes
  into the host buffer or the preallocated `stereoScratch`, chunking when a host
  exceeds the prepared block size; every audio-thread state write targets a
  fixed-size atomic; and no `ValueTree` or parameter write occurs there —
  mapped sound controllers and program changes set atomics and defer to
  `handleAsyncUpdate` on the message thread. Remaining allocation is inside
  FluidSynth itself and is not under this project's control.

  Two new regression scenarios pin the dispatch boundary: an unknown SysEx on
  either route is forwarded without reasserting programs, and a framed GM reset
  dispatched from buffer storage still reasserts the current program.

## 1.5 Complete MIDI CC, pitch-bend, and expressive-control accuracy

### Scope and behavior contract

The beta must distinguish between MIDI processing and UI automation:

- Every valid channel CC message, CC0 through CC127, must be forwarded to FluidSynth on the original MIDI channel and at the correct sample timestamp.
- CC7 (volume) and CC10 (pan) are additionally mirrored into the plugin's per-channel UI/state because the plugin exposes those two mixer controls. As of `0.5.1-alpha.5` these replaced CC71/72/73/74/75/79, whose Juicy16-specific modulators were removed; see the amendment under "Exposed sound-controller synchronization".
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

- [x] Add focused tests for commonly used musical controllers.
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

  EVIDENCE: The canonical fixture covers every listed common CC and the engine
  suite verifies exact raw traces plus observable RPN state. Dedicated
  disposable-engine scenarios prove sustain and sostenuto hold/release semantics,
  distinguish All Notes Off from immediate All Sound Off, and verify Reset All
  Controllers target/preservation behavior. `docs/CONTROLLER_SUPPORT.md` documents
  bank/modulator-dependent cases and basic-channel limitations.

  The remaining gap was mode-dependent CC0/32-to-Program-Change behaviour, which
  the synthesised multi-bank SF2 now covers in the audio domain on SF2 and the
  system DLS on DLS. SF3 stays bounded by its fixture — see the Bank Select task
  above for the exact residual.

- [x] Verify high-resolution controller pairs where supported.
  - MSB/LSB pairs must retain ordering and channel identity.
  - Do not claim 14-bit CC support for pairs the engine does not implement; document exact observed behavior.

  EVIDENCE: The canonical fixture preserves same-sample MSB/LSB ordering and
  channel identity, with the resulting 12-semitone RPN state proving ordered Data
  Entry. `docs/CONTROLLER_SUPPORT.md` records FluidSynth's general 7-bit-MSB
  behavior and the portamento/Data Entry exceptions.

  The one open question — whether cents-level Data Entry LSB actually reaches
  synthesis — was settled on 2026-08-19 by measurement rather than assumption.
  `getPitchWheelSensitivity` reports whole semitones, so only the audio can
  answer it. With RPN 0,0 set to 2 semitones plus 50 cents, a full-up bend raises
  pitch by a factor of 1.1596; 2.5 semitones predicts 1.1553 and 2 semitones alone
  predicts 1.1224. The cents are therefore honoured, and the suite now asserts it
  at 48 kHz. The documentation previously declined to claim this and has been
  corrected.

  Portamento CC5/CC65 remains delivered-and-documented rather than
  audio-verified: FluidSynth combines its MSB and LSB, but the audible result
  depends on bank modulators, so no Beta 1 claim is made about it.

### Exposed controller synchronization

**AMENDED 2026-08-20 (`0.5.1-alpha.5`).** The six exposed sound controllers were
removed. Juicy16 was the only SoundFont player applying modulators to CC71-79 —
stock FluidSynth ignores them entirely, measured — and its amounts were wildly out
of scale: CC73=127 stretched attack from 50 ms to 868 ms, CC75=127 raised a note
tail by 43 dB, CC72=127 left a note ringing 48 dB above neutral a second after
note-off, and CC71=127 attenuated the signal by 46 dB, while on DLS banks all six
were inert. Game rips commonly send those controllers, so material sounded flat
and compressed only in this plugin. The exposed per-channel controls are now CC7
volume and CC10 pan, which FluidSynth's own default modulators implement.

The evidence below describes the retired design and is kept as the record of what
was verified at the time. The equivalent coverage for volume and pan is asserted
by the same suite; see the amended item immediately after it.

- [x] Verify CC7 volume and CC10 pan on every MIDI channel.
  - Engine receives the exact value at the MIDI timestamp.
  - The affected channel's saved state updates on the message thread.
  - Sliders update only when that channel is selected.
  - Switching channels reveals each channel's most recent independent value.
  - Saving and reopening restores both values for all 16 channels.
  - Incoming CCs do not create recursive or duplicate engine sends.
  - Incoming MIDI overrides a value set in the editor, and the visible parameter
    follows the MIDI rather than the other way round.

  EVIDENCE: The offline engine suite drives distinct volume and pan values on all
  16 channels at distinct sample offsets and requires the engine value, the raw
  dispatch timestamp, the mirrored parameter, the channel isolation, and the state
  round-trip to agree on every channel. A separate assertion sets a value through
  the editor path and then overrides it with an incoming CC7, requiring the engine
  and the visible parameter to follow the MIDI. A further assertion renders two
  fresh instances with every CC71-79 controller at 0 and at 127 and requires
  bit-identical audio, which is what proves no Juicy16 modulator remains. Debug,
  ASan+UBSan, and `leaks` gates all passed on arm64 macOS on 2026-08-20.

- [x] SUPERSEDED — Verify CC71/72/73/74/75/79 on every MIDI channel.
  - Engine receives the exact value at the MIDI timestamp.
  - The affected channel's saved state updates on the message thread.
  - Sliders update only when that channel is selected.
  - Switching channels reveals each channel's most recent independent value.
  - Saving and reopening restores all six values for all 16 channels.
  - Incoming CCs do not create recursive or duplicate engine sends.

  EVIDENCE: `tests/EngineMidiTests.cpp` sends distinct values for all six controls
  on all 16 channels and verifies their exact engine values and source sample
  positions. It then uses the same `FluidSynthModel::selectChannelForEditing`
  path as the channel-list UI to prove unselected messages do not move the visible
  parameters, every channel reveals its latest independent values when selected,
  selection emits no duplicate MIDI dispatch, and a save/reopen preserves both
  the per-channel engine values and selected-channel parameter mirrors. The strict
  arm64 macOS Release `engine_midi_system_dls` test passed on 2026-08-19.

- [x] Verify the neutral point and direction of every exposed sound controller.
  - Value 64 produces the documented neutral behavior.
  - Values below and above 64 move the destination in the expected direction.
  - Cutoff is not inverted.
  - Sustain-level direction matches the UI label.
  - Reset All Controllers and GM/GS/XG reset behavior are explicitly tested and documented.

  EVIDENCE: The six default-modulator definitions are now one frozen contract
  used both to install them and to expose read-only test diagnostics. The engine
  suite requires every `fluid_synth_add_default_mod` call to succeed, verifies
  exact controller/parameter/generator/amount/flag mappings, proves FluidSynth's
  pinned linear-bipolar algebra maps 64 to zero with the documented sign on both
  sides, and checks fresh engine/UI values are 64. Separate scenarios prove CC121
  preserves sound controls while resetting its MIDI-defined targets, and GM/GS/XG
  reset handling reapplies all six latest values. `docs/CONTROLLER_SUPPORT.md`
  publishes the exact directions and reset semantics. The strict arm64 macOS
  Release engine test passed on 2026-08-19.

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
  - Repeat at 44.1, 48, 88.2, and 96 kHz to catch sample-rate-related pitch errors.

  EVIDENCE: The system-DLS engine test renders note 69 at full-down, center, and full-up with a 12-semitone RPN range, estimates periodic frequency from audio, and verifies the expected ratios at 44.1, 48, 88.2, and 96 kHz. It also proves that FluidSynth 2.5.5's unsupported 192 kHz rate fails silent instead of running at a stale rate, and that returning to 48 kHz recreates the engine and resumes audio. The strict arm64 macOS Release test passed on 2026-08-19; the earlier 44.1/48/96 subset also passed under ASan+UBSan.

### Pressure and related channel expression

- [x] Test channel pressure across all 16 channels.
- [x] Test polyphonic key pressure with independent note numbers.
- [x] Verify pressure messages retain timestamp, channel, key, and 0–127 value.
- [x] Document whether the audible result depends on modulators present in the loaded bank.

  EVIDENCE: `README.md`, `docs/BETA_TESTER_GUIDE.md`, and `docs/KNOWN_ISSUES.md` distinguish exact pressure delivery from bank-dependent audible modulation.

### Host and file-playback coverage

- [x] Add a deterministic MIDI fixture containing the full CC/pitch-bend test sequence.
- [x] Run the fixture through the offline processor harness and compare against an expected event/state trace.

  EVIDENCE: `tests/fixtures/controller_conformance.csv` is a checked-in,
  human-reviewable event fixture covering the common controllers, MSB/LSB pairs,
  all six exposed sound controls, RPN/NRPN selection and Data Entry, channel-mode
  messages, and representative 14-bit bend values across channels 1, 2, 3, 10,
  and 16. `tests/EngineMidiTests.cpp` validates the fixture before use, runs it as
  one timestamped `MidiBuffer`, compares every CC's value/channel/sample trace and
  every bend's engine value, and proves the same-sample RPN sequence produces the
  expected 12-semitone state. The strict arm64 macOS Release test passed on
  2026-08-19.
- [ ] Run musically representative CC and pitch-bend passages in FL Studio and Cubase.
- [ ] Verify the DAW does not filter, chase incorrectly, normalize, or collapse controller data before it reaches the plugin.
- [ ] Test transport start, stop, rewind, loop boundaries, chase, project reload, and playback beginning mid-song.
- [ ] Record host-specific controller-chase behavior separately from plugin defects.

### Acceptance criteria

- [x] The parameterized CC0–CC127 suite passes without channel or value corruption.
- [x] The exposed mixer controllers (CC7 volume, CC10 pan) remain engine/UI/state-consistent on all 16 channels, and incoming MIDI stays authoritative over editor-set values.

  EVIDENCE: The offline engine suite covers the real parameter values used by the
  slider attachments, the editor's centralized channel-selection path, per-channel
  `ValueTree` persistence, and FluidSynth controller state for every exposed
  controller and MIDI channel. See the detailed evidence above.
- [x] Full-range 14-bit pitch bend and per-channel RPN pitch-bend range tests pass.
- [x] CCs and bends occurring within one audio block affect audio at the correct sample offsets.

  EVIDENCE: The audio-domain suite measures four bend segments in one sustained-note block and verifies that timestamped All Sound Off creates only the intended silent interval before a later note resumes.
- [ ] FL Studio and Cubase reproduce the same expected musical controller and pitch-bend sequence, accounting only for documented host chase behavior.
- [x] Any unsupported FluidSynth controller behavior is documented precisely and is not misrepresented as plugin support.

  EVIDENCE: `docs/CONTROLLER_SUPPORT.md` separates exact plugin delivery from
  FluidSynth/bank-dependent audible behavior, documents general 7-bit MSB
  interpretation and its exceptions, mode-dependent Bank Select, CC121/122/123,
  basic-channel restrictions for CC124–127, and the remaining unclaimed RPN/bank
  cases. The tester guide and known-issues page state the same support boundary.

## Phase 1 exit criteria

- [x] Sample-accurate rendering is implemented and tested.
- [x] GM percussion defaults are correct.
- [x] All program/reset paths share consistent state handling.

  EVIDENCE: `FluidSynthModel::applyProgramToEngine` is the sole program mutation path, and the offline suite verifies raw MIDI, host parameters, manual assignments, state/font reload, GM/GS/XG resets, rejected changes, and same-block note ordering converge on engine, parameter, and saved state.
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
- [x] Handle missing, unreadable, moved, zero-preset, unsupported, and corrupt files.
- [x] Decide whether the current font remains active after every failure class and test that behavior.

### Acceptance criteria

- [x] Selecting a bad file never silently clears a working setup.
- [x] The UI clearly identifies failed loads and successful DLS repairs.
- [x] State restore with a missing bank fails gracefully.

  EVIDENCE: Replacement loads are validated before swapping `sfont_id`.
  `engine_midi_system_dls` now exercises a moved/missing path, a directory/non-file,
  a permission-denied file, unsupported text, corrupt SF2-shaped RIFF data, and a
  structurally empty zero-instrument DLS. Every case reports `error`, records the
  attempted path and a nonempty user message, preserves the prior engine program,
  keeps the successful `loadedPath` in newly serialized state, and leaves that
  bank audible. The direct Debug run passed without JUCE assertions on 2026-08-19.
  The editor status bar and file-control tooltip expose the structured result,
  including repaired-copy success text. Preset names returned by FluidSynth and
  the visible Unicode status separator now cross into JUCE through explicit UTF-8
  conversion, avoiding ambiguous-ASCII assertions during reload/editor creation.

## 2.2 macOS path and sandbox behavior

- [x] Make path loading work when bookmark creation fails.
- [x] Release any `CFErrorRef` values and handle Core Foundation failures explicitly.
- [ ] Verify security-scoped bookmark creation and restoration in AU, VST3, and standalone contexts.

  BLOCKED: Requires a real sandboxed host per format. The offline harness covers
  resolution and every fallback branch, but it cannot prove that a host actually
  grants sandbox access to the resolved URL.

- [x] Handle stale bookmarks and moved files.

  EVIDENCE: Two defects were found and fixed in the macOS bookmark path on
  2026-08-19, neither reachable from the previous tests because both need a real
  bookmark rather than the path-only state the harness used.

  1. `StringRef path {String::fromCFString(...)}` bound a `StringRef` to a
     temporary `String`. `StringRef` stores a borrowed character pointer, so the
     path was read after free on every bookmark-based load — latent undefined
     behavior in the normal DAW session-restore path. It now takes an owning
     `String`.
  2. A bookmark that resolved was treated as success regardless of whether the
     bank actually loaded, so a resolved-but-unloadable target (replaced,
     truncated, or permission-denied file) skipped the stored-path fallback and
     the user's bank silently disappeared on project reload.
     `unloadAndLoadFont`'s result is now honoured, and the stored path is retried
     when it differs from the resolved one.

  `CFURLCreateByResolvingBookmarkData`'s `isStale` flag is now captured and
  published as the runtime-only `bookmarkStale` font-state property, readable
  through `FluidSynthModel::isBookmarkStale()`. It is not serialised, so the
  frozen state schema is unaffected. Automatic bookmark refresh on staleness is
  deliberately NOT implemented: it cannot be exercised without a sandboxed host
  and a genuinely relocated file, and writing untestable rewrite logic into the
  session-restore path is the larger risk. Resolution already follows a moved
  file, so staleness currently costs nothing but a missed refresh.

  Four regression scenarios cover the branches: an unresolvable bookmark falls
  back to the saved path, the recovered bank audibly sounds, an unresolvable
  bookmark with a missing path reports an error without setting the stale flag,
  and a bookmark that resolves to an unloadable file still falls back. The last
  was confirmed to fail against the pre-fix code and pass after it.

- [ ] Verify that sandbox declarations match actual file access requirements.

  BLOCKED: Requires running under each host's sandbox.

### Acceptance criteria

- [x] A file selected through the picker loads whether bookmark creation succeeds or the safe fallback path is required.

  EVIDENCE: Bookmark creation failure was already covered by the path fallback in
  `FilePicker`; the restore side is now covered by the four scenarios above,
  including the previously broken resolved-but-unloadable case.
- [ ] Saved sessions restore access after the plugin and DAW restart.

  BLOCKED: Needs a real DAW session in each format.
- [x] No Core Foundation leaks are reported in the tested workflows.

  EVIDENCE: `CFErrorRef` is released on every branch, and the resolution code owns
  its `CFURL`/`CFData` through `CFUniquePtr`. That is now measured rather than
  reviewed. LeakSanitizer is unavailable on Darwin arm64, so the ASan gate runs
  with leak detection off; the new `tools/ci_gates.sh leaks` gate closes that hole
  by running every offline harness under macOS `leaks -atExit`. All four —
  `JuicySFFontQA`, `JuicySFEngineMidiTests` (which includes the four bookmark
  scenarios), `JuicySFVST3Smoke`, and `JuicySFAUSmoke` — report `0 leaks for 0
  total leaked bytes` on arm64 macOS on 2026-08-20. The gate reads the summary
  line rather than the tool's exit status, because `leaks` exits non-zero when it
  finds leaks and so cannot distinguish "leaked" from "failed to run"; a missing
  summary fails the gate. Verified against a deliberately leaking binary.

  This covers the harness workflows only. A leak that occurs solely under a real
  DAW's sandbox and session lifecycle is still not represented.

## 2.3 DLS implementation and repair safety

- [x] Choose the Windows FluidSynth strategy.
  - Preferred: upgrade to a current FluidSynth release with native C++17 DLS support.
  - Alternative: build the pinned version with `libinstpatch` and package its obligations correctly.

  DECISION: The preferred option. Windows uses the same FluidSynth 2.5.5 as macOS
  with `osal=cpp11` and the native DLS loader, and `enable-libinstpatch=off`. That
  keeps one engine version and one DLS code path across both platforms, and avoids
  taking on libinstpatch's additional licensing and packaging obligations for a
  capability FluidSynth now provides itself.

  EVIDENCE: `tools/build_macos_dependencies.sh` sets `enable-native-dls=ON` and
  `enable-libinstpatch=OFF`, and the macOS artifact proves the resulting loader
  against Apple's system DLS and a private Awave-style DLS on every strict run.
  The legacy Windows context already used `osal=cpp11` and disabled libinstpatch
  but never stated `enable-native-dls` explicitly, which is exactly how a Windows
  build ends up with no DLS support at all — the open risk in the register. That
  flag is now set there too.

  Choosing the strategy does not prove it on Windows. Strict Windows configuration
  still requires a real `.dls` probe as a non-skippable test, and no Windows
  artifact has run it; that remains open under Phase 4.3.

- [x] Add a build-time or runtime DLS capability check.
  - A build that cannot load DLS must fail release validation.
  - Do not rely only on the file-picker extension filter.

  EVIDENCE: Strict release configuration now requires `BUILD_TESTING=ON` so the
  capability gate cannot be omitted. macOS strict builds require Apple's actual
  system DLS and register its `JuicySFFontQA` load as a `font;dls;release` test;
  Windows strict builds require `JUICYSF_FONT_CORPUS` to contain a real `.dls`
  file and register that exact file as `font_load_release_dls`. A negative macOS
  configuration with tests disabled failed at configure time as intended, and
  the real system-DLS release test plus the complete nine-test macOS 11 arm64
  suite passed on 2026-08-19. Windows execution remains an artifact gate below.

- [x] Expand `repairDlsImage` tests.
  - Empty and truncated images.
  - Oversized and undersized top-level chunks.
  - Odd-byte padding.
  - First-chunk corruption.
  - Very large sizes and 32-bit overflow behavior if 32-bit remains in scope.
  - Non-DLS RIFF files must remain byte-identical.
  - Fuzz or property-test malformed RIFF structures.

- [x] Define the limits of repair.
  - Repair only known-safe structural errors.
  - Report when repair was attempted but the result still fails.
  - Avoid promising that every malformed DLS “just works.”

  EVIDENCE: `docs/DLS_REPAIR.md` defines the only two allowed little-endian RIFF
  size edits, word-alignment behavior, safe-target requirement, validation and
  rollback rule, non-goals, and temporary-file lifecycle. It explicitly excludes
  missing sample/instrument data, arbitrary/nested corruption, unsafe first-chunk
  guesses, unsupported encodings, and zero-preset banks. The README and
  troubleshooting guide link this boundary, and the document is included in the
  macOS package. `font_repair_unit` proves non-DLS identity, well-formed identity,
  idempotence, bounded malformed inputs, and unsafe-target refusal; the engine
  suite proves failed candidates preserve active audio/state.

- [ ] Build a licensed font compatibility corpus.
  - At least one conventional SF2.
  - At least one compressed SF3.
  - At least one well-formed DLS.
  - At least one known Awave-style malformed DLS.
  - Sparse/non-zero banks and percussion banks.
  - Large banks and unusual preset names.
  - Record license and redistribution status for every fixture.

  PARTIAL EVIDENCE: The local private `testfiles/` corpus contains one conventional
  DLS, one Awave-style DLS requiring safe temporary repair, one SF2, and a matching
  MIDI. The opt-in CTest loaded all three banks (99 total presets) in Debug and
  static Release on arm64 macOS. The exact static Release QA tool also loaded
  FluidSynth 2.5.5's pinned `VintageDreamsWaves-v2.sf3` fixture (136 presets,
  SHA-256 `bbb921fa98a3705d304f05904f06952b75e1cfe1ada086590d36cbd6efec1a40`)
  directly from the dependency source. Its adjacent notice records explicit SF3
  conversion permission and redistribution terms; it was not copied into this
  repository/package. `docs/TEST_CORPUS.md` records hashes, provenance, and gaps.
  The private banks' rights, sparse/large/unusual-name coverage, Windows, and
  final-candidate coverage remain open.

### Acceptance criteria

- [ ] The corpus passes on supported macOS and Windows builds.
- [ ] DLS capability is proven, not inferred, on each release artifact.

  PARTIAL EVIDENCE: The strict macOS 11 arm64 artifact's required runtime DLS
  probe loads the system bank successfully and is labeled as a release test.
  The equivalent Windows gate is configured but has not run on an MSVC x64
  artifact, so cross-platform acceptance remains unchecked.
- [x] Repair is idempotent and does not modify the user's original file.

  EVIDENCE: `font_repair_unit` covers empty/truncated input, inner/outer size errors, odd padding, unsafe first-chunk refusal, non-DLS identity, idempotence, and 6,000 deterministic malformed/non-DLS property cases; ASan+UBSan passes. Plugin repair writes a JUCE unique temporary copy only.

## Phase 2 exit criteria

- [x] Font loading is transactional and errors are visible.
- [ ] macOS bookmark/path restoration is reliable.

  PARTIAL EVIDENCE: Two real defects were fixed on 2026-08-19 — a read-after-free
  on every bookmark-based load, and a resolved-but-unloadable bookmark silently
  losing the user's bank — and four regression scenarios now cover the fallback
  branches. Reliability under a real sandboxed host across AU, VST3, and
  standalone still needs a DAW, so this stays open.
- [ ] Windows and macOS both pass the SF2/SF3/DLS corpus.
- [x] DLS repair behavior and limits are tested and documented.

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

- [x] Remove duplicate ownership of `IUnitInfo`.
  - Investigate whether the vendored wrapper, `VST3ClientExtensions`, or a smaller targeted patch can be the sole implementation.
  - Eliminate the expected `extractResult()` assertions.
  - Preserve pre-connection unit discovery required by Cubase.
  - Preserve component-side queries and host program-list refresh notifications.

  EVIDENCE: The pinned wrapper is now the sole `IUnitInfo` owner on both the
  VST3 component and controller. `JuicyVST3Extensions` retains only program-name
  storage and `IUnitHandler` refresh notification. Direct Debug smoke output on
  2026-08-19 contained no JUCE assertion, while pre-connection discovery,
  component-side queries, a real DLS name refresh, and the host notification all
  passed.

- [x] Minimize and version the JUCE wrapper patch.
  - Pin the exact compatible JUCE version in CMake.
  - Fail configuration when a different JUCE version is found unless explicitly supported.
  - Maintain a clean patch file or clearly documented vendored diff.
  - Add an automated comparison that detects unexpected upstream drift.
  - Correct all comments describing the patch's scope and line count.

  EVIDENCE: `vendor/juce_patched/juce-8.0.14-vst3-multitimbral.patch` is a seven-hunk wrapper diff plus the platform shim against exact JUCE 8.0.14 inputs. Its README records base/result/patch SHA-256 values and reproduction steps. CMake rejects any upstream, vendored, or patch hash drift before source swapping; applying the normalized patch reproduced both vendored files byte-for-byte after line-ending normalization, and the strict VST3 smoke test passed.

- [x] Extend the VST3 smoke test.
  - Fail if JUCE assertions occur in the tested path.
  - Exercise component/controller creation and destruction repeatedly.
  - Test parameter-to-unit mapping before and after connection.
  - Test program-list name refresh after a font load.
  - Test invalid channels, controllers, units, and program indices.
  - Process actual MIDI/audio and state if the harness can safely support it.

  EVIDENCE: The smoke harness covers repeated creation/destruction, identical
  unit/list/bus answers before and after connection, invalid unit/list/program/
  bus/controller inputs, component state loading, real program-name refresh, and
  `IUnitHandler` notification. It now configures the concrete `IAudioProcessor`,
  processes stereo audio plus timestamped note events and all 16 channel program
  queues, pumps the message-thread mirror, observes host parameter edits, and
  reads component state back to prove parameter/channel-state convergence. A
  direct Debug run and strict arm64 macOS Release CTest passed on 2026-08-19 with
  no JUCE assertion output.

- [x] Preserve both VST3 Program Change delivery paths.
  - Path A: a host sends or emulates Program Change through `IMidiMapping`.
  - Path B: a host uses `IUnitInfo`, program lists, and the unit's `kIsProgramChange` parameter.
  - Prove that channel N always updates `progChN`, never the global program or channel 1 by accident.
  - Prove that all 16 channels can change programs independently in one processing sequence.
  - Prove that a parameter-delivered Program Change updates `engineBank`/`enginePreset`, channel state, selected-channel UI, and saved state exactly like a raw MIDI Program Change.

  EVIDENCE: The smoke harness obtains one frozen ParamID set through
  `IMidiMapping` and a second through `IUnitInfo`/program-parameter discovery,
  then runs each route independently through the concrete VST3 processor. Each
  route assigns 16 distinct supported programs, includes multiple points on four
  queues and same-block notes, produces audio, reports exactly the 16 expected
  `progChN` host edits, and serializes the expected bank/program for every channel
  without redirecting unselected channels to channel 1. The pinned wrapper now
  converts every program queue point to a channelized MIDI Program Change at its
  original sample offset; stock JUCE's last-point/block-start collapse is bypassed.
  Direct Debug and strict Release runs passed on 2026-08-19. Exact Cubase/FL
  Studio validation remains a separate host gate and is not claimed by this test.

- [x] Add a Cubase query-order regression test.
  - Instantiate the VST3 controller and query `IUnitInfo` before connecting component and controller.
  - Require 17 units and one 128-entry program list during that early query.
  - Query every event-bus channel and verify its unit ID.
  - Connect component/controller, repeat all queries, and require identical answers.
  - Recreate the plugin and repeat to detect stateful or initialization-order failures.

  EVIDENCE: `vst3_multitimbral_smoke` queries all 17 units, the 128-entry list,
  and every event-bus channel before connection; requires identical answers after
  connection; and creates/connects/destroys two additional pairs to detect leaked
  initialization state. Debug and strict Release runs passed on 2026-08-19.

- [x] Add an end-to-end multichannel Program Change fixture.
  - Use a legally redistributable MIDI fixture with activity across all 16 channels.
  - Include distinct Program Changes for every channel near the beginning.
  - Include Bank Select where needed, channel 10 percussion, and at least four mid-song instrument changes.
  - Include variants with GM, GS, and XG reset SysEx before the initial Program Changes.
  - Include simultaneous Program Changes on different channels.
  - Include Program Change and note-on within the same audio block to verify timing and instrument choice.
  - Record the expected bank, program, and sounding preset at every checkpoint.

  EVIDENCE: `tests/fixtures/vst3_multichannel_programs.csv` is a checked-in,
  synthetic, redistributable event fixture. It drives both independently acquired
  VST3 parameter routes; assigns all 16 channels; covers default-GS Bank Select,
  channel 10 bank 128, simultaneous initial changes, four mid-block changes,
  same-block notes, and framed GM/GS/XG reset SysEx; and records all-channel final
  bank/program checkpoints. A final stop/restart-at-sample-zero scenario resends
  the same 16 programs after GM reset. At every all-channel checkpoint, the
  concrete processor now converges the exact bank/program values across host
  parameters and serialized engine-backed state, then isolates and auditions each
  channel after All Sound Off on all 16 channels. Every expected melodic program
  and the channel-10 percussion program produces audio independently, so aggregate
  audio from another channel cannot mask a routing failure. The direct Debug run
  passed with zero failures/assertions on arm64 macOS on 2026-08-19, followed by
  the complete nine-test strict macOS 11 arm64 Release gate.

- [x] Add program-parameter observation hooks to the test harness.
  - Capture which `progChN` parameter changes, its normalized value, and its sample/block position.
  - Assert that no unrelated channel parameter changes.
  - Assert that the selected UI row does not redirect changes intended for another channel.
  - Assert that transport restart and host parameter deduplication do not prevent the engine from restoring the intended program after reset SysEx.

  PARTIAL EVIDENCE: The fake `IComponentHandler` records every ParamID and
  normalized value reported through `performEdit`. Before each checked-in fixture
  scenario, the harness reads the engine-backed component state; afterward it
  requires exactly one final `progChN` edit for each channel whose program changed,
  zero for unchanged channels, the expected normalized values, and no unrelated
  program parameter. The sequential Bank Select/GM-reset case proves 12 unchanged
  channel parameters are deduplicated while four changed channels still notify,
  and component-state readback proves selected-row independence. The explicit
  transport-stop/restart-at-sample-zero case then applies reset SysEx and all 16
  unchanged programs, requires engine/state/audio restoration, and observes zero
  duplicate program edits. Per-checkpoint isolation now proves every channel's
  state-verified program sounds independently.

  The last gap — direct observation of the timbre transition at its exact
  in-block sample — is now closed in the audio domain. Four trials render one
  512-sample block each on a fresh component, so the synth state at the note is
  identical and the renderings compare directly: program 0 at block start,
  program 19 at block start, program 0 at block start plus program 19 one sample
  *before* the note, and program 0 plus program 19 one sample *after* it. On
  2026-08-20 the two programs correlated at **-0.0364** on the same note, the
  before-note switch matched the new program at **1.0000**, and the after-note
  switch matched the old program at **1.0000**.

  The after-note trial is the discriminating one: JUCE's ordinary parameter
  collapse hoists every automation point to block start, which would have made
  that note sound the new program and dropped the correlation to about -0.04. The
  vendored wrapper's timestamp preservation is therefore proven by audio, not
  only by state readback.

### Acceptance criteria

- [x] `vst3_smoke` exits successfully without assertion output.

  EVIDENCE: Direct Debug runs and the strict Release CTest passed on 2026-08-19,
  including pre-connection discovery of 17 units, all 16 program parameters/
  mappings, component/controller queries, DLS program-name refresh, host
  notification, repeated lifecycles, and bounds rejection. The latest direct
  Debug run also passed the checked-in all-channel Bank Select and GM/GS/XG reset
  fixture with no `JUCE Assertion failure` line. The same data-driven fixture then
  passed in the complete nine-test strict macOS 11 arm64 Release suite.
- [ ] Cubase and at least one additional VST3 host correctly route Program Change independently on all 16 channels.
- [x] The patch is reproducible against the pinned JUCE source.
- [x] Both the `IMidiMapping` and VST3 unit/program-parameter paths pass independently.

  EVIDENCE: `vst3_multitimbral_smoke` processes both independently acquired
  ParamID routes through `IAudioProcessor` and requires all-channel audio, host
  edits, parameters, and serialized state to converge. Debug and strict Release
  passed on 2026-08-19; real-host coverage remains open above.
- [x] The multichannel game-rip fixture reaches every expected instrument without manual patch assignment.

  EVIDENCE: A real game rip is now played end to end through the plugin offline.
  `JuicySFEngineMidiTests --game-rip <bank> <file.mid>` reads the MIDI with
  `juce::MidiFile`, merges its tracks, converts timestamps to sample positions,
  and feeds them block by block exactly as a host would, with no manual patch
  assignment anywhere. Expectations are derived from the file itself — the last
  Program Change per channel and the set of channels that play notes — so the
  test generalises to any rip rather than encoding one song.

  It requires every channel to end on the program its own Program Change
  selected, every channel that plays to have sounded with that program, a zero
  program-apply failure mask, audible output, and channel 10 to remain in the
  percussion bank when the file uses it. The local corpus rip drives eleven
  channels through eleven distinct programs and carries two reset SysEx events,
  so it also exercises the reset self-heal. It passes against both the DLS and
  SF2 form of the same bank.

  Registered by CMake as `engine_game_rip_<stem>_<format>`, one test per bank
  format, discovered by pairing any `.mid` under `JUICYSF_FONT_CORPUS` with a
  same-named bank. Game rips are not redistributable, so the tests exist only
  when a private corpus is configured; a clean clone reports none rather than
  silently passing. Host-side confirmation in FL Studio and Cubase remains open
  under Phase 7.

## 3.2 AU behavior

- [x] Add AU-focused integration tests or a host harness where practical.

  EVIDENCE: `tools/au_smoke.cpp` is an in-process AU host, registered as the
  `au_host_smoke` CTest. It reads the component description from the built
  bundle's own `Info.plist`, registers the factory that plist names with
  `AudioComponentRegister`, and drives the unit through `MusicDeviceMIDIEvent`
  and `AudioUnitRender`. Because the component comes from its own bundle, nothing
  is installed and an already-installed copy cannot be tested by mistake — the
  weakness of validating only through `auval`.

  It covers the frozen `aumu`/`Jc16`/`Pkst` description, executable load and
  factory export, instantiation with 48 kHz non-interleaved stereo, the 24
  published parameters including one program parameter per MIDI channel,
  per-channel Program Change delivered as AU MIDI and mirrored onto the matching
  `progChN` parameter with no cross-channel leakage, independent audio on all 16
  channels, `kAudioUnitProperty_ClassInfo` save and restore returning every
  channel to its saved program, clean uninitialize/dispose, and reinstantiation.
  All fifteen checks passed on arm64 macOS on 2026-08-20.

- [ ] Verify per-channel Program Change delivery in Logic and at least one additional AU host.
- [ ] Verify state save/restore, resizing, keyboard focus, file access, and 16-channel MIDI routing.

  PARTIAL EVIDENCE: State save/restore and 16-channel MIDI routing are covered by
  `au_host_smoke` above, through the real AU wrapper rather than the processor
  alone. Resizing, keyboard focus, and native file access need a window and a host
  focus chain, which a headless harness cannot create, so this stays open.
- [x] Run `auval` as part of every macOS release build.

  EVIDENCE: The `macos-release-strict` job in `.github/workflows/ci.yml` and the
  `macos-candidate` job in `.github/workflows/release.yml` both install the built
  AU and run `auval -v aumu Jc16 Pkst` immediately after the strict Release gate,
  so no macOS release build can complete without it.

### Acceptance criteria

- [x] `auval` passes the exact release AU artifact.

  EVIDENCE: The current strict-Release AU, executable SHA-256
  `5789db92cde136fd31a866babc7fa392a4c569d497de36f0cc5bccdb3018bb8b`, was
  installed byte-for-byte and passed `auval -strict -q -v aumu Jc16 Pkst` on
  arm64 macOS 26.5.2 on 2026-08-19, including Apple's render tests and the
  intentionally muted unsupported 192 kHz probe. The AU extracted from the staged
  BC1 package was then installed and passed the same strict invocation at the
  identical hash. The earlier AU
  (`afdd7d2d69502d029e31adf2fbab3909b5bb0e060b81893f5ef77219e45e9fa0`) had also
  passed; this closes the gate that was reopened for the rebuilt hash. Validation
  ran on macOS 26.5.2 only — the macOS 11 minimum-OS run remains open in Phase 4.2.
- [ ] Host tests demonstrate independent channel behavior and state restoration.

## Phase 3 exit criteria

- [x] VST3 routing works without assertions or duplicate-interface ambiguity.

  EVIDENCE: The wrapper is the only component/controller `IUnitInfo` owner, and
  direct Debug plus strict Release smoke runs complete early discovery, both
  processing routes, repeated lifecycles, audio, host edits, and state readback
  without assertion output. Cubase/FL Studio candidate validation remains open.
- [ ] AU passes validation and host tests.
- [x] JUCE version and patch compatibility are enforced automatically.

---

# Phase 4 — Build reproducible, portable release artifacts

## Goal

Produce AU and VST3 bundles that work on clean supported systems rather than only on the developer machine.

## 4.1 CMake cleanup and dependency policy

- [x] Make release dependency linkage explicit.
  - Decide whether FluidSynth and its required dependencies are statically linked or embedded and relocated.
  - Ensure no release binary references `/opt/homebrew`, `/usr/local`, a developer home directory, or build-tree paths.
  - Rename or remove misleading `BUILD_SHARED_LIBS` comments and unused qualifier variables.
  - Make release configuration fail when portable linkage requirements are not met.

  EVIDENCE: Strict validation requires static FluidSynth and `BUILD_SHARED_LIBS=OFF`; the checksum-pinned macOS dependency recipe builds the complete static codec closure and remaps build/source paths. The `macos_artifact_portability` CTest rejects prohibited linked or embedded paths in AU, VST3, and Standalone binaries.

- [ ] Set explicit deployment targets.
  - Apply the approved minimum macOS version.
  - Apply the approved Windows target/version macros and toolchain requirements.
  - Verify the resulting load commands and metadata.

  PARTIAL EVIDENCE: Strict macOS configuration requires 11.0 and the artifact
  test verifies every Mach-O load command reports `minos 11.0`. Windows targets
  now compile with `WINVER/_WIN32_WINNT=0x0A00` and
  `NTDDI_VERSION=0x0A000002` (`NTDDI_WIN10_RS1`, version 1607), and the configure
  summary reports that floor. A real MSVC artifact and minimum-Windows runtime
  test are still required before this cross-platform parent item can close.

- [ ] Enforce architecture expectations.
  - Produce and verify universal macOS binaries if approved in Phase 0.
  - Produce x64 Windows artifacts and any other approved architectures.
  - Remove unused x86/ARM scripts and documentation if those architectures are out of scope.

  PARTIAL EVIDENCE: Universal binaries were not approved, so the single approved
  macOS architecture is arm64: strict configuration requires exactly that, and
  `macos_artifact_portability` proves every built Mach-O is arm64-only.
  The `EXTRA_ARCH_PKG_CONFIG_PATH` second-architecture scaffolding is retained for
  a possible later universal build but now fails configuration outright under
  strict release validation, so a candidate cannot be produced as a fat binary
  while claiming arm64 — verified by a negative configure on 2026-08-19. Windows
  x64 is enforced at configure time but no artifact exists, so this stays open.

- [x] Reduce CMake to supported formats and modules.
  - Keep AU conditional on Apple platforms.
  - Keep VST3 on supported desktop platforms.
  - Mark standalone as development-only if retained.
  - Remove or gate VST2/AUv3/AAX/RTAS/Unity remnants.
  - Remove unused generated JUCE translation units or replace the legacy `JuceLibraryCode` header with normal JUCE module includes/generated headers.

  EVIDENCE: The CMake format list is now exactly AU/VST3/Standalone on Apple and
  VST3/Standalone on Windows, with Standalone explicitly labelled development/QA.
  The opt-in VST2 SDK/build path was removed rather than merely disabled; AUv3,
  AAX, RTAS, and Unity are absent from the build. Forty-one unreferenced legacy
  Projucer translation units—including obsolete format wrappers and duplicate JUCE
  module shims—were removed, while the temporary `JuceHeader.h` compatibility
  aliases remain documented. A strict parallel arm64 macOS Release rebuild and all
  nine CTests passed on 2026-08-19.

- [ ] Add configure-time checks.
  - Exact JUCE version.
  - Minimum FluidSynth version and required features.
  - Supported compiler and generator.
  - Required architecture dependencies.
  - Signing inputs for release builds.

  PARTIAL EVIDENCE: CMake already requires JUCE 8.0.14 exactly and now requires
  FluidSynth 2.5.5 exactly in strict validation. It rejects unsupported release
  platforms/architectures/toolchains, non-static dependencies, newer dependency
  deployment targets, drifted JUCE wrapper inputs/outputs/patch hashes, missing
  VST3 SDK headers, a second architecture, and a whitespace build path that would
  silently discard the pinned dependency prefix.

  Signing inputs are now checked too: a `JUICYSF_CODE_SIGN_IDENTITY` other than
  ad-hoc must resolve against `security find-identity`, so a release build cannot
  quietly produce an unsigned or differently signed artifact from a typo. Verified
  by a negative configure with a fabricated identity on 2026-08-19. The item stays
  open because which identity Beta 1 ships under is still an unresolved policy
  decision, not because the check is missing.

### Acceptance criteria

- [x] A clean configure gives a concise, accurate summary of formats, architectures, dependency linkage, and feature support.
- [x] Unsupported configurations fail early with actionable errors.

  EVIDENCE: Strict release validation rejects shared dependencies, non-static
  FluidSynth, wrong macOS deployment target/architecture, non-MSVC or non-x64
  Windows, and platforms outside macOS arm64/Windows x64. Negative configure tests
  on 2026-08-19 confirmed the wrong-architecture and shared-library cases stop at
  configure time with the expected remediation text. VST2 is no longer a build
  option.

## 4.2 macOS signing and validation

- [x] Fix the resource/signing dependency order.
  - Ensure `AppIcon.icns`, plist files, VST3 module metadata, and all resources exist before signing.
  - Ensure copy-after-build occurs only after the final signature.
  - Test parallel builds to rule out ordering races.

- [x] Add release signing and optional notarization workflow.

  DECISION (2026-08-20, product owner): Beta 1 ships **ad-hoc signed**. No
  Developer ID is held for this release, so a Developer ID signature and
  notarization are out of scope rather than pending.

  EVIDENCE: The signing workflow for that decision is complete and enforced.
  `distribute/bundle_macos.sh` detects the signature kind from `codesign -dv`,
  labels an ad-hoc package `ADHOC` in its filename, records the kind in
  `BUILD_INFO.txt`, and refuses to package at all when
  `JUICY16_REQUIRE_DISTRIBUTION_SIGNATURE=1` is set and the artifact is ad-hoc —
  so the door to a distribution-signed candidate stays open without pretending
  one exists. Strict configuration separately requires any non-ad-hoc
  `JUICYSF_CODE_SIGN_IDENTITY` to resolve against `security find-identity`, so a
  typo cannot silently produce an unsigned artifact.

  The user-visible half is the part that actually matters for an ad-hoc release:
  `docs/BETA_TESTER_GUIDE.md` now states that ad-hoc is expected for Beta 1,
  distinguishes the `ADHOC` label from the never-install `LOCAL-DIRTY` label,
  gives the quarantine-clearing commands, tabulates the four symptoms a tester
  sees if they skip the step — including the misleading "is damaged and can't be
  opened" — gives the command to confirm it worked, and refuses to recommend
  disabling SIP or Gatekeeper. `docs/KNOWN_ISSUES.md` records the position.

  Notarization remains available as a later-beta upgrade path; nothing in the
  packaging assumes its absence permanently.
- [x] Verify every artifact with:

  ```bash
  codesign --verify --deep --strict --verbose=2 "path/to/Juicy16.component"
  codesign --verify --deep --strict --verbose=2 "path/to/Juicy16.vst3"
  auval -v aumu Jc16 Pkst
  ```

  EVIDENCE: All three commands pass on the exact packaged candidate. Both bundles
  report `valid on disk` and `satisfies its Designated Requirement` under
  `--deep --strict`, and `auval -strict -q -v aumu Jc16 Pkst` reports
  `AU VALIDATION SUCCEEDED` against the AU installed from the package, at
  executable hash `0681063528b4d8b89c1c903412235eed29fafaaf8845895e6e3cec0d0290ca5b`
  (VST3 `6c88fb43d5c1cc91234eda5dee3ca966c86b87c96ca59871b0960ed229ca5c5b`), on
  2026-08-23. The signature is ad-hoc by approved decision, not by omission;
  Developer ID and notarization are out of Beta 1 scope per the 2026-08-20
  decision-log entry rather than pending under this item.

  Every recorded hash must be regenerated from the frozen commit at candidate
  freeze; the ones above belong to the `0.5.1-alpha.5` working-tree build.

  Every executable hash recorded elsewhere in this plan (`5789db92...`,
  `5af9c124...`) belongs to the `0.5.1-alpha.1` build. The current
  `0.5.1-alpha.3` strict Release build, rebuilt from a clean space-free copy on
  2026-08-20, is AU `2ff6176d6b8c4de0ef29af9d672e482377aa10db63432346b561b719cf4097c2`
  and VST3 `61b51c29477ad4af3fedc297895e4a6521faa0e3b2a91490665c2f988d1b8b18`;
  `auval -strict -q -v aumu Jc16 Pkst` succeeded against the installed AU at that
  hash. Any candidate freeze must regenerate every recorded hash from the frozen
  commit.

- [x] Inspect portability with `otool -L` and deployment targets with `otool -l`.
- [ ] Test installation and first launch on a clean supported Mac without Homebrew FluidSynth.

### Acceptance criteria

- [x] Strict code-signature verification passes after a parallel clean build.
- [x] AU validation passes.

  EVIDENCE: `auval -strict -q -v aumu Jc16 Pkst` succeeded on 2026-08-19 against
  both the strict-Release AU and the copy extracted from the staged BC1 package,
  at executable SHA-256 `5789db92...`. Ad-hoc signature only; Developer ID and
  minimum-OS runs remain open.
- [x] No prohibited dependency path appears in `otool -L`.

  EVIDENCE: A fresh renamed AU/VST3/Standalone build under `/private/tmp/juicy16-release-macos11` used checksum-pinned, source-built macOS 11 arm64 FluidSynth/libogg/libvorbis/FLAC/Opus/libsndfile archives. Strict validation accepted every archive; all nine tests passed, including the required licensed SF3 fixture; metadata and ad-hoc signatures verified; `otool -L` reported system libraries/frameworks only; the final binary declares macOS 11.0; and the automated string scan found no developer/build prefixes after disabling FluidSynth's unused default-bank path and remapping dependency source roots. Runtime testing on macOS 11 and a clean system remains required. The rebuilt AU has since passed strict `auval`, closing the gate above.
- [ ] Artifacts load on both the minimum supported macOS version and the current version.

## 4.3 Windows toolchain and packaging

- [ ] Replace or formally validate the unsupported JUCE 8 MinGW path.
  - Preferred: establish a supported MSVC/Visual Studio build, locally or in CI.
  - If LLVM-MinGW remains, document the risk and prove it through host validation; do not claim upstream support.

  PARTIAL EVIDENCE: The preferred option is now written down rather than absent.
  `tools/build_windows_dependencies.ps1` builds the Windows closure from the same
  components, versions, and SHA-256 checksums as the macOS recipe — FluidSynth
  2.5.5 with `osal=cpp11`, native C++17 DLS on, libinstpatch off — statically
  linked including the MSVC C runtime, so the VST3 needs no Visual C++
  redistributable. `CMakeLists.txt` derives the plugin's `/MT` setting from
  `FLUIDSYNTH_LINK_STATIC` under MSVC, because a mismatch is a link failure
  rather than a silent difference. The `windows-vst3` CI job builds that closure
  instead of the ad-hoc `vcpkg install fluidsynth:x64-windows` it used before.

  Windows no longer needs pkg-config: the closure installs FluidSynth's own CMake
  package config and `JUICYSF_FLUIDSYNTH_CMAKE_CONFIG` defaults to `ON` under
  MSVC. That discovery path is not untested Windows-only code — it was exercised
  on macOS on 2026-08-20 with a static link against the pinned closure, and all
  eight registered tests passed through it, including both the VST3 and AU host
  harnesses. A negative configure confirmed macOS release validation still
  refuses the config path, because its per-archive architecture and
  deployment-target checks read pkg-config's static link list.

  `tools/verify_windows.ps1` is the one-shot verification for a real Windows
  machine: dependency closure, configure, build, tests, the DLS capability probe
  against `C:\Windows\System32\drivers\gm.dls`, the `Contents/x86_64-win`
  module layout, `dumpbin /headers` and `/dependents`, and artifact hashes — all
  into a single pasteable report. It does not stop at the first failure, because
  nothing in this path has ever executed and a run that stops early wastes the
  trip.

  BLOCKED: The recipe has never been executed and no Windows artifact exists. The
  MinGW path stays quarantined, and the job stays `continue-on-error`, until
  either the first hosted CI run or a `verify_windows.ps1` report comes back
  clean. The owner has a Windows machine available for this.

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

  PARTIAL EVIDENCE: macOS is proven. Rebuilding the strict Release candidate on
  2026-08-19 from an independent clean copy at a different path, with a freshly
  built pinned dependency closure, reproduced byte-identical AU, VST3, and
  archive hashes. Windows has no validated pipeline (Phase 4.3).
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
  - Pre-v3 state migration (older saves keep bank/preset; retired sound-controller values are ignored).
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

  PARTIAL EVIDENCE: `.github/workflows/ci.yml` defines six jobs — documentation
  links, macOS Debug, macOS ASan+UBSan, macOS leaks, macOS strict portable
  Release, and an explicitly non-gating Windows VST3 job. Every macOS job
  delegates to `tools/ci_gates.sh`, so the hosted run executes the same commands a
  developer runs locally, and all five locally reproducible gates passed on arm64
  macOS on 2026-08-20. `docs/CI.md` records the gate matrix and its limits.

  The Windows job is materially different from before. It used to run a bare
  `vcpkg install fluidsynth:x64-windows` — never an approved dependency source,
  at whatever version vcpkg carried, and not configured for FluidSynth's native
  DLS loader, so the plugin it produced may not have loaded DLS banks at all. It
  now builds the pinned closure, proves DLS at runtime against
  `C:\Windows\System32\drivers\gm.dls`, and reports the VST3's DLL
  dependencies.

  Two items keep this unchecked: no hosted GitHub Actions run has executed yet,
  and the Windows job stays `continue-on-error` until its first green run and
  Phase 4.3 host validation.

- [x] Build Debug and Release configurations.

  EVIDENCE: `tools/ci_gates.sh debug` and `tools/ci_gates.sh release` are wired
  into CI as separate jobs and both passed locally on arm64 macOS on 2026-08-19.
  Debug: 6/6 CTest from a clean build directory with first-party warnings as
  errors. Strict Release: 8/8 CTest from a clean copy at a space-free path,
  including `font_load_release_sf3`, `font_load_system_dls`,
  `vst3_multitimbral_smoke`, `release_metadata_consistency`, and
  `macos_artifact_portability`; `auval -v aumu Jc16 Pkst` then passed against the
  exact built AU (executable SHA-256
  `5789db92cde136fd31a866babc7fa392a4c569d497de36f0cc5bccdb3018bb8b`), and
  `distribute/bundle_macos.sh` packaged and revalidated that artifact set.
  Minimum-OS runtime testing remains open.

- [x] Treat first-party compiler warnings as tracked debt.
  - Remove extra semicolons and unused parameters.
  - Mark overriding destructors correctly.
  - Migrate deprecated JUCE constructors and MIDI iteration APIs.
  - Fix signedness at FluidSynth API boundaries.
  - Remove unused functions, fields, variables, and commented-out code.

  EVIDENCE: `JUICYSF_WARNINGS_AS_ERRORS` applies `-Werror` (`/WX` on MSVC) per
  source file to Juicy16's own translation units only, because JUCE modules and
  the bundled Steinberg VST3 SDK compile into the same targets. Enabling it first
  failed on the known `funknown.h` `-Wshadow-field-in-constructor` warning
  reaching us through `Source/VST3Multitimbral.cpp`; the bundled SDK is now
  included as a `SYSTEM` directory, which is the correct attribution rather than
  a suppression of our own code. Debug and strict Release both build clean with
  the policy on, and every CI job enables it.

- [x] Add formatting or lint checks appropriate to the project.

  EVIDENCE: The `docs` job runs `tools/ci_gates.sh docs`, which gates every
  internal Markdown link across 58 active documentation files. No C++ formatter
  is imposed: the inherited Birchlabs code and the vendored JUCE wrapper do not
  share one style, so a repository-wide reformat would obscure the
  compatibility-critical wrapper diff. Recorded as a deliberate choice rather
  than an omission; revisit if the vendored patch is ever retired.

- [x] Add sanitizer jobs where supported.

  EVIDENCE: A separate `macos-leaks` job runs `tools/ci_gates.sh leaks`, because
  LeakSanitizer is unavailable on Darwin arm64 and the ASan job therefore runs
  with `detect_leaks=0`. It runs all four offline harnesses under macOS
  `leaks -atExit` and requires zero leaked bytes from each.

  The `macos-sanitizers` job runs `tools/ci_gates.sh asan`, building
  `JuicySFFontQA` and `JuicySFEngineMidiTests` with
  `-fsanitize=address,undefined` and running them under
  `abort_on_error=1`/`halt_on_error=1`. Both passed locally on 2026-08-19 with no
  findings. Only the offline harnesses are sanitized, because an unsanitized host
  cannot load a sanitized plugin bundle; bundle-loading coverage stays in the
  ordinary Debug job.

- [x] Archive validation logs and unsigned test artifacts where appropriate.

  EVIDENCE: The Debug job uploads `build-ci-debug/Testing/**` on success or
  failure. The strict Release job uploads the staged candidate archive and its
  SHA-256 from `distribute/out/`. Packaged candidates built from a dirty
  worktree or ad-hoc signature are filename-labelled `LOCAL-DIRTY`/`ADHOC` by
  `distribute/bundle_macos.sh`, so an archived artifact cannot be mistaken for a
  publishable one.

- [-] Protect release tags so they can only be created from a passing commit.

  DESCOPED: The remaining half was a GitHub ruleset restricting who may create a
  `v*` tag. There is one person with push access. `release.yml` already refuses to
  build a candidate from a failing commit, which is the half that catches a real
  mistake.

  PARTIAL EVIDENCE: `.github/workflows/release.yml` runs on `v*` tags, calls the
  CI workflow as a required prerequisite job, and builds a candidate only after
  those gates pass, so a tag cannot produce artifacts from a failing commit. The
  remaining half is a GitHub repository ruleset on the `v*` tag pattern
  restricting who may create such a tag; that lives in repository settings
  outside this worktree and has not been applied. See `docs/CI.md`.

### Acceptance criteria

- [x] `ctest --output-on-failure` runs meaningful tests and passes on supported platforms.

  EVIDENCE: Re-confirmed for `0.5.1-alpha.5` on 2026-08-20. Debug: 13/13 from a
  clean build directory with first-party warnings as errors, the thirteenth being
  the new `dependency_patch_contract` guard. The strict Release figures below are
  the `0.5.1-alpha.3` run; that gate could not be rerun for `alpha.4` because the
  pinned dependency tarballs would not download on this machine (Phase 8.7).
  Strict static Release, from a clean space-free copy with warnings as errors:
  11/11 —
  documentation links, DLS repair unit, the licensed SF3 fixture, system DLS load,
  VST3 multitimbral smoke, the in-process AU host smoke, release metadata,
  macOS artifact portability, the offline engine/MIDI suite, the randomised MIDI
  soak, and the performance baseline. `auval -v` and `auval -strict -q -v aumu Jc16 Pkst` then passed against
  the installed Release AU, and all four offline harnesses reported zero leaks.
  Earlier sets also passed under ASan+UBSan and TSan harnesses.
- [ ] CI catches timing, state, DLS, VST3-routing, metadata, and packaging regressions.

  PARTIAL EVIDENCE: The gates cover all six categories — the engine/MIDI suite
  covers timing and state, `font_load_*` covers DLS and SF3,
  `vst3_multitimbral_smoke` covers VST3 routing,
  `release_metadata_consistency` and `macos_artifact_portability` cover metadata
  and artifact shape, and the release job runs `distribute/bundle_macos.sh` with
  its post-extraction manifest verification. Checking this off requires a hosted
  run demonstrating the gates actually failing a regression, which has not
  happened yet.
- [x] Release builds contain no unresolved first-party warnings selected as errors by policy.

  EVIDENCE: The strict Release gate configures with `JUICYSF_WARNINGS_AS_ERRORS=ON`
  and built clean on 2026-08-19; no first-party warning remains.

## Phase 5 exit criteria

- [ ] Core tests and platform integration tests run automatically.

  PARTIAL EVIDENCE: The workflows and the local gate script exist and pass, but
  "automatically" requires a hosted run on push/PR, which has not occurred.
- [ ] CI covers all approved release platforms.

  BLOCKED: macOS arm64 is fully covered. Windows x86_64 is an approved Beta 1
  release platform, but its job cannot gate anything until Phase 4.3 approves an
  MSVC FluidSynth dependency policy and proves DLS capability there.
- [ ] Release creation depends on passing quality gates.

  PARTIAL EVIDENCE: `release.yml` depends on the CI workflow passing. The
  matching GitHub tag ruleset has not been applied.

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

  PARTIAL EVIDENCE: `building.win32.md` is no longer a status document. It now
  records the pinned component table, the toolchain requirements, the
  clean-clone commands, the DLS capability position, packaging, and the explicit
  list of what remains before it counts as a release procedure. The word
  "tested" is what still fails: every command in it is a proposal until the
  `windows-vst3` job runs green, and the document says so at the top.

- [x] Add a troubleshooting guide.
  - Plugin not discovered.
  - Invalid signature/quarantine.
  - Missing bank after reopening a session.
  - Unsupported or corrupt font.
  - Program Change routing differences among hosts.
  - Channel 10/percussion expectations.

- [x] Add release notes or a changelog beginning with the remediation release.

## 6.2 Terminology and claims

- [x] Standardize `SoundFont`, `SF2`, `SF3`, and `DLS` capitalization.
- [x] Use “16-channel multitimbral DLS/SoundFont player” consistently.
- [x] Keep “General MIDI sound module” only if the completed GM tests justify it.
- [x] Use approved Fruity LSD comparison language.
- [x] Remove absolute compatibility claims that exceed the corpus and host tests.
- [x] Clearly label standalone and any legacy formats.

  EVIDENCE: Active user/developer documentation consistently uses `SoundFont`,
  `SF2`, `SF3`, and `DLS` in prose (lowercase appears only in literal filename
  extensions). The broader “General MIDI sound module” marketing claim is absent;
  docs instead describe the narrower implemented GM channel-10/reset behavior and
  its remaining corpus/host gates.

## 6.3 Version and identity single source of truth

- [x] Eliminate manually duplicated version values where possible.
  - Generate the UI version from the CMake project version.
  - Remove or regenerate stale `ProjectInfo` and `AppConfig` values.
  - Derive package versions from the same source.

  EVIDENCE, and a defect found on 2026-08-20: there was one source of truth, but
  an existing build directory could not see changes to it.
  `JUICYSF_PRERELEASE_LABEL` was a plain `CACHE STRING` default, which CMake
  writes once and thereafter ignores, so bumping the label in `CMakeLists.txt`
  changed nothing for any build tree already configured. `build-ci-debug` was
  still producing and displaying `0.5.1-alpha.2` while the plan, README, and
  changelog all said `alpha.3` — and `release_metadata_consistency` could not
  catch it, because it compares the artifact against the same stale cache value.
  The label is now a normal variable with a `-D` override, so editing
  `CMakeLists.txt` reaches every build directory. Verified: a reconfigure of the
  existing tree reports `Juicy16 0.5.1-alpha.5`, and the built AU binary carries
  `Juicy16 v0.5.1-alpha.5`.

- [x] Apply the approved company, website, email, bundle identifier, manufacturer code, and plugin code consistently.
- [x] Add a non-empty copyright statement if appropriate.
- [x] Add an automated metadata consistency test.

## 6.4 Privacy and licensing documents

- [x] Replace `PRIVACY.txt` with an accurate privacy statement.
  - State whether the plugin itself performs network access or telemetry.
  - Distinguish build-time services from runtime behavior.
  - Remove obsolete JUCE 5/ROLI statements.

- [x] Update the top-level license to match the Phase 0 decision.
- [x] Replace the obsolete JUCE GPLv3 notice with the applicable JUCE 8 open-source notice.
- [ ] Inventory all code and binary dependencies actually present in each release.

  PARTIAL EVIDENCE: `docs/DEPENDENCIES.md` is the single inventory for the macOS
  arm64 closure, listing every statically linked or embedded component with the
  version actually built and its role, cross-referenced to the pinned checksums
  in `tools/build_macos_dependencies.sh`. It also records the parsing attack
  surface and the absence of runtime networking.

  Windows is now specified to the same component list, versions, and checksums by
  `tools/build_windows_dependencies.ps1`, with the two necessary differences
  recorded: a static MSVC C runtime, and no `-ffile-prefix-map` equivalent, so
  the macOS developer-path scan has no Windows counterpart yet. That closure has
  not been built, so the section is labelled intended rather than measured and
  this item stays open.
  - JUCE embedded dependencies such as HarfBuzz and SheenBidi.
  - FluidSynth and all statically linked or bundled dependencies.
  - VST3 SDK licensing notice where required.
  - DLS-related dependencies if `libinstpatch` is retained.

  PARTIAL EVIDENCE: The macOS arm64 inventory now covers JUCE/AGPL, JUCE-embedded HarfBuzz, SheenBidi, zlib, libpng and IJG JPEG, AU/VST3 SDK interfaces, FluidSynth, GCEM, libsndfile, FLAC, Ogg, Vorbis/Vorbisenc, and Opus. `distribute/bundle_macos.sh` stages only this macOS allowlist. Windows remains unverified.

- [x] Include source-offer and relinking materials required by the chosen licenses.

  EVIDENCE: FluidSynth and libsndfile are LGPL-2.1 and statically linked, so
  LGPL-2.1 section 6 requires a recipient be able to relink against a modified
  library. `docs/LICENSING.md` now names the exact materials that provide this
  through complete corresponding source rather than shipped object files — which
  is available because the whole work is already GPLv3/AGPLv3: the candidate
  source at the commit recorded in `BUILD_INFO.txt`, the checksum-pinned
  dependency recipe that rebuilds the identical closure from upstream source, the
  vendored JUCE wrapper sources with their reproducible patch and hashes, and the
  exact configure/build commands. It also states that the corresponding source
  must be published no later than the binary and stay available as long as the
  binary is offered.

- [x] Ensure package scripts include all required notices and no obsolete ones.

  EVIDENCE: The fifteen staged notices were cross-checked against the actual
  linked closure recorded in `docs/DEPENDENCIES.md`: JUCE/AGPL, the JUCE-embedded
  HarfBuzz, SheenBidi, zlib, libpng and IJG JPEG, the AU and VST3 SDK interfaces,
  FluidSynth, GCEM, libsndfile, FLAC, Ogg, Vorbis, Vorbisenc, and Opus. Every
  component in the inventory has a notice and every notice corresponds to a
  present component, with no leftovers from removed formats. Staging is an
  explicit list rather than a directory sweep, so an obsolete notice cannot
  reappear silently, and `docs/DEPENDENCIES.md` itself is now packaged so the
  obligation can be checked against the binary.

### Acceptance criteria

- [x] A reader can determine exactly what the app supports without reading source code.

  EVIDENCE: `docs/SUPPORT_MATRIX.md` is the single answer surface and now states
  platforms and architectures, release formats, minimum OS versions, sample-rate
  scope, bank-format support with the evidence backing each claim, the DLS repair
  boundary, the one-stereo-output limitation, and per-channel Bank Select and
  Program Change behaviour. Controller detail lives in
  `docs/CONTROLLER_SUPPORT.md` and current defects in `docs/KNOWN_ISSUES.md`, both
  linked from it. Every claim is stated with its status, so an unproven target
  (Windows DLS) reads as unproven rather than supported.
- [x] Build guides reproduce the CI/release builds.

  EVIDENCE: `building.macos.md` and CI are the same commands, not two
  descriptions of one process: every macOS CI job runs `tools/ci_gates.sh`, and
  the guide documents that script alongside the equivalent explicit invocations.
  On 2026-08-19 the guide's Release recipe was followed from a clean copy and
  reproduced the candidate byte-for-byte. `building.win32.md` deliberately
  documents a status, not a recipe, and stays open until Phase 4.3 lands.
- [x] UI, source, binary metadata, package names, and docs report the same version and identity.

  EVIDENCE: Fresh AU/VST3 metadata reports Juicy16 0.5.0, Pokestir, `com.pokestir.juicy16`, `Pkst`/`Jc16`, pokestir.com, and contact@pokestir.com. The metadata test checks the built files, and package naming is derived from the same CMake version.
- [x] Privacy and licensing documents accurately describe the distributed artifacts.

  EVIDENCE: Reviewed against the actual package on 2026-08-20. `PRIVACY.txt`'s
  claims are each verifiable: no runtime networking (JUCE cURL and web-browser
  support compiled out, no socket call in the closure), macOS security-scoped
  bookmarks stored in host state, DLS repair writing a uniquely named temporary
  copy without modifying the original, and no diagnostics or telemetry surface
  beyond the visible version and bank-load status. `docs/LICENSING.md` lists the
  dependencies actually present, which now matches `docs/DEPENDENCIES.md` and the
  staged notice set component for component. Accuracy is confirmed; the remaining
  gate is the qualified reviewer's sign-off recorded in Phase 0, not a
  discrepancy.

## Phase 6 exit criteria

- [x] All active documentation is current and tested.

  EVIDENCE: Every internal Markdown link across 61 files is checked by CTest on
  every run. The macOS build guide and the tester guide's installation section
  were executed as written on 2026-08-19/20, which is what surfaced the
  developer-path defect in the published checksum sidecar.
  `docs/KNOWN_ISSUES.md` was reconciled with the current state in both
  directions — stale pessimism removed, newly found limitations added — and the
  support matrix, controller contract, DLS repair boundary, dependency inventory,
  performance baseline, threading model, CI gates, and triage policy all describe
  behaviour that an automated check or a recorded measurement backs.
  `building.win32.md` is deliberately a status document rather than a recipe and
  says so.
- [x] Stale generated files and legacy-format references are removed or clearly quarantined.

  EVIDENCE: The obsolete Projucer `include_juce_*` translation units were removed
  and `JuceLibraryCode/ReadMe.txt` marks the remaining generated headers as
  non-canonical. The legacy LLVM-MinGW cross-build now carries an explicit
  quarantine notice at `win32_cross_compile/README.md` stating that nothing in
  CMake, CI, or the gate script invokes it and that its output must not be
  published. Dead `VST2_SDK` ignore rules were dropped, since VST2 is out of
  scope and the directory does not exist. A stray 44-byte `fluidsynth.wav` — the
  FluidSynth file-renderer default, never a build product — is ignored so it
  cannot be committed; it is inert and can be deleted at any time. Build
  directories are ignored via `/build/` and `/build-*/`.
- [ ] Licensing and privacy materials are complete for distribution.

---

# Phase 7 — Beta-candidate validation

## Goal

Prove that the exact Beta 1 candidate artifacts meet the product contract on clean systems and real hosts.

## 7.1 Automated release checks

- [ ] Build all candidate artifacts from a clean tagged commit.

  BLOCKED: No candidate tag exists. Creating one freezes the candidate and is an
  owner decision (Phase 8.8). Everything else in this section has been exercised
  against an untagged working-tree build, which is why its package is filename-
  labelled `LOCAL-DIRTY`.

- [x] Confirm the worktree and submodule/dependency state are recorded.

  EVIDENCE: `BUILD_INFO.txt` inside every package records the product, version,
  candidate number, source commit, source URL, whether the worktree was dirty,
  the signature kind, and both executable hashes. The packager refuses a dirty
  worktree outright unless `JUICY16_ALLOW_DIRTY_PACKAGE=1` is set, which then
  stamps `LOCAL-DIRTY` into the filename. Dependency state is not a submodule but
  a checksum-pinned recipe: `tools/build_macos_dependencies.sh` fixes every
  version and SHA-256, and CMake rejects a FluidSynth or JUCE version, or a
  vendored wrapper hash, that drifts from it.

- [x] Run the complete unit and integration test suite.

  EVIDENCE: The full suite passes in both configurations — Debug and strict
  portable Release — via `tools/ci_gates.sh`, which is the same entry point CI
  uses. Coverage spans documentation links, DLS repair, corpus and system DLS
  loading, the licensed SF3 fixture, VST3 multitimbral routing, release metadata,
  macOS artifact portability, the offline engine/MIDI suite, the performance
  baseline, and the game-rip regression.

- [ ] Run the SF2/SF3/DLS compatibility corpus on each platform artifact.

  PARTIAL EVIDENCE: The corpus runs against the macOS artifact — private SF2/DLS,
  the macOS system DLS, and FluidSynth's licensed upstream SF3. No Windows
  artifact exists to run it against, so this cannot close.

- [x] Run VST3 smoke/validator checks.

  EVIDENCE: `vst3_multitimbral_smoke` passes against the built bundle in both
  configurations and was additionally run against the bundle installed from the
  package into `~/Library/Audio/Plug-Ins/VST3`. It covers pre-connection unit
  discovery, all 16 units and program parameters, both the `IMidiMapping` and
  unit/program-parameter routes, concrete stereo event processing, host edit
  observation, repeated lifecycles, and bounds rejection. Steinberg's own
  `validator` binary is not built by this project and has not been run; the smoke
  harness is the substitute and is more specific to the multitimbral contract.

- [x] Run `auval` against the packaged AU.
- [x] Verify signatures, architectures, deployment targets, and linked dependencies.

  EVIDENCE: `macos_artifact_portability` asserts arm64-only binaries, a declared
  macOS 11.0 minimum on every Mach-O load command, strict `codesign --verify`
  success, and the absence of prohibited linked or embedded paths. The packager
  reruns the same checks on the extracted copy, and separately scans the compiled
  binaries with `strings` for developer and build paths.

- [x] Verify package contents and license notices.

  EVIDENCE: Staging copies an explicit allowlist and the packager fails on any
  unsupported format, private corpus entry, prohibited path, absolute or
  traversing archive entry, more than one top-level directory, or a bundle binary
  that lost its executable bit. Fifteen third-party notices cover every component
  in the linked closure, cross-checked against the inventory in
  `docs/DEPENDENCIES.md`.

- [x] Verify all artifact hashes are recorded.

  EVIDENCE: Three independent records. `SHA256SUMS` inside the package covers
  every file and is re-verified after clean extraction; `BUILD_INFO.txt` records
  both executable hashes; and a sidecar `.sha256` covers the archive. The sidecar
  is now written relative to the archive, so a tester's
  `shasum -a 256 -c` succeeds and it cannot publish a developer path — the
  packager fails if it contains an absolute path.

  HASH VALIDITY: The hashes below belong to one specific validation run and go
  stale on any source change. They are evidence that the pipeline records and
  verifies hashes, not a frozen candidate identity. Regenerate them at candidate
  freeze by rerunning `tools/ci_gates.sh release`, `auval`, and
  `distribute/bundle_macos.sh` on the tagged commit.

  PARTIAL EVIDENCE: The local dirty/ad-hoc BC1 validation package was built from
  the strict macOS 11 arm64 Release tree and revalidated after extraction. All
  nine registered tests passed, including the required SF3 load and data-driven concrete VST3 smoke;
  metadata, architecture, deployment target, signatures, dynamic dependencies,
  embedded paths, the internal file manifest, and the curated notice set passed
  their automated gates. The current package archive SHA-256 is
  `63c1b3bef83b702ef64f72357e57a1f7996da62def1477feb0bb7bca473aa8e8`;
  its AU and VST3 executable hashes are respectively
  `5789db92cde136fd31a866babc7fa392a4c569d497de36f0cc5bccdb3018bb8b`
  and `5af9c1248fd0e43048954d06f8ccd12cda17784082acf754be450c9c38355e8a`.
  The AU extracted from this exact package was installed and passed
  `auval -strict -q -v aumu Jc16 Pkst` on arm64 macOS 26.5.2 on 2026-08-19 at the
  hash above.

  REPRODUCIBILITY: Before the audio-thread SysEx change, the package was rebuilt
  on 2026-08-19 from an independent clean copy at a different filesystem path,
  with a freshly built pinned dependency closure, and produced byte-identical
  results — the same archive SHA-256 (`9b14bc1b...`) and the same AU and VST3
  executable hashes as the earlier `/private/tmp/juicy16-release-macos11` build.
  The macOS build is therefore path-independent and reproducible in this
  environment. The hashes recorded above are the later rebuild that includes
  that change.
  These items remain unchecked because this is explicitly not a clean tagged,
  distribution-signed, uploaded candidate and Windows/real-host gates are open.

## 7.2 Host matrix

Test the exact packaged artifacts, not a separate local build.

**Run this section from [docs/HOST_TEST_PROTOCOL.md](docs/HOST_TEST_PROTOCOL.md).**
It exists because the coverage below was previously unrunnable as written: the
"canonical CC/pitch-bend fixture" was a CSV of sample offsets that only the
offline harness can replay, and the only `.mid` in the repository is the private,
non-redistributable game rip. There was nothing a tester could import into a DAW.

Two original General MIDI fixtures now cover it, generated by
`tools/make_host_fixtures.py` and committed under `tests/fixtures/host/`:
`host_program_matrix.mid` drives 16 independent Program Changes at three defined
checkpoints, with a note on the same tick as each change; `host_controllers.mid`
walks CC, 14-bit pitch bend, per-channel RPN bend range, pedals, and channel mode
one marked bar at a time. The protocol document tabulates the exact expected
instrument on every channel at every checkpoint, and the expected observation for
every controller step, so each item below has one unambiguous answer.

Both fixtures are validated offline before anyone carries them into a DAW —
`engine_host_program_matrix` and `engine_host_controllers` play them through the
engine against the platform's system GM bank and require every channel to end on
its own program, channel 10 to keep the percussion bank, and every played channel
to have sounded with its selected program. `host_fixtures_reproducible` pins the
committed files to their generator so the tables cannot drift from the bytes. A
host that produces a different result is therefore the host's problem, not the
fixture's.

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

  PARTIAL EVIDENCE: The current strict-Release VST3 exercised by the concrete
  processing smoke test has executable SHA-256
  `5af9c1248fd0e43048954d06f8ccd12cda17784082acf754be450c9c38355e8a`
  and passes strict ad-hoc signature verification. An earlier build was installed,
  but approval to replace it with this current artifact was unavailable. Cubase
  has not scanned or exercised the current artifact, so no host sub-item is checked.
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

- [x] Follow installation and build instructions exactly from a clean environment.

  EVIDENCE: Both guides were executed verbatim on 2026-08-19/20.

  The `building.macos.md` Release recipe was run from a clean copy of the tree at
  a space-free path: pinned dependency closure from source, strict configure,
  build, and the full CTest suite. It reproduced the candidate byte-for-byte.

  The `docs/BETA_TESTER_GUIDE.md` installation steps were then run against the
  packaged archive as a tester would: verify the sidecar checksum, unpack, copy
  both bundles to `~/Library/Audio/Plug-Ins/{Components,VST3}`, clear the
  quarantine attribute, and rescan. `auval -strict -q -v aumu Jc16 Pkst` passed
  against the installed AU, and the installed VST3 passed the full
  `vst3_multitimbral_smoke` contract — all 16 programs routed independently
  through both the `IMidiMapping` and unit/program-parameter paths, plus the
  reset, Bank Select, transport-restart, and mid-song checkpoints.

  The walkthrough found a real defect. The published `.sha256` sidecar was
  written with the packaging machine's absolute path, so the documented
  `shasum -a 256 -c` command would have failed on a tester's machine, and the
  file published the developer's home directory — which the packager's own path
  scan could not catch, since it inspects the archive rather than the sidecar
  beside it. Both packagers now emit a relative checksum, and the macOS packager
  fails if the sidecar contains an absolute path. Re-verified from an unrelated
  directory.

- [x] Check every internal link and command.

  EVIDENCE: `documentation_internal_links` scans every in-scope Markdown file
  during CTest and rejects missing relative-link targets; 61 files pass. The shell
  commands in `building.macos.md` and the tester guide's installation section were
  executed as written, which is what surfaced the checksum defect above. Commands
  in `building.win32.md` are deliberately not executed: that document records a
  status rather than an endorsed recipe.

- [x] Confirm version, support matrix, filenames, and checksums.

  EVIDENCE: One CMake version and one prerelease label produce the display
  version, the AU/VST3 binary metadata, the package filename, and
  `BUILD_INFO.txt`; `release_metadata_consistency` asserts the agreement against
  the built artifacts. Recorded package hashes elsewhere in this plan were taken
  from the `0.5.1-alpha.1` build and must be regenerated for any later candidate. Checksums verify both inside
  the package (`SHA256SUMS`, re-checked after clean extraction) and outside it
  (the sidecar, now verified from an unrelated directory).
  `docs/SUPPORT_MATRIX.md` states each platform, format, bank format, and
  sample-rate claim with its status, so unproven targets read as unproven.

- [x] Confirm known limitations are honest and complete.

  EVIDENCE: `docs/KNOWN_ISSUES.md` was reviewed against the current state on
  2026-08-20 and corrected in both directions. Stale pessimism was removed — the
  AU `auval` entry claimed the current rebuild was unvalidated, and the RPN entry
  declined to claim cents-level Data Entry that measurement has since confirmed.
  Newly discovered limitations were added: the DLS repair size boundary and RIFF
  overrun rejection, and the fact that the interface has one fixed appearance
  rather than following the system light/dark setting. Every remaining
  stop-ship/open gate corresponds to an unchecked item in this plan.
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

- [x] A tester can determine whether their setup is supported before downloading.

  EVIDENCE: `docs/SUPPORT_MATRIX.md` had the answer but nothing a tester reads
  first pointed at it — the tester guide told them not to download an
  unsupported candidate without telling them how to tell. It now opens with a
  four-row check covering operating system, CPU, plugin format, and bank format,
  with the OS commands that reveal chip and architecture, the ad-hoc signing
  consequence stated before download rather than after, and the distinction
  between approved scope and a validated host. It links onward to the full matrix
  and to the known-issues list.
- [x] Backup, state-compatibility, known-risk, and uninstall expectations are visible.

  EVIDENCE: `docs/BETA_TESTER_GUIDE.md` defines the experienced multichannel-DAW tester audience, mandatory backup warning, candidate-specific support boundary, unsupported formats, reporting expectations, uninstall, and rollback.

## 8.2 Beta package contents

- [ ] Include the exact AU/VST3 artifacts approved by Phase 7.
- [x] Include `README`, installation instructions, known issues, privacy statement, project license, third-party notices, and changelog/release notes.

  EVIDENCE: The staged package carries `README.md`, `CHANGELOG.md`,
  `LICENSE.txt`, `NOTICE.md`, `PRIVACY.txt`, `BUILD_INFO.txt`, `SHA256SUMS`,
  `building.macos.md`, eight documents under `docs/` including
  `KNOWN_ISSUES.md`, `SUPPORT_MATRIX.md` and `BETA_TESTER_GUIDE.md`, and fifteen
  third-party notices covering every component in the linked closure.
  Installation instructions were the one gap and were added to
  `docs/BETA_TESTER_GUIDE.md` on 2026-08-19: checksum verification, a refusal to
  install `LOCAL-DIRTY`/`ADHOC` labelled archives, exact per-format destination
  paths for both platforms, backup and rescan steps, and the macOS Gatekeeper
  quarantine removal an unnotarized beta requires.

- [x] Include version and candidate number in package filenames.

  EVIDENCE: The archive is named
  `Juicy16-<display version>-<candidate>-macos-arm64[-LOCAL-DIRTY][-ADHOC].zip`,
  with the version derived from the same CMake value the metadata test checks and
  the candidate required to match `BC[1-9][0-9]*`.

- [x] Include SHA-256 checksums outside and, optionally, inside the package.

  EVIDENCE: `<archive>.sha256` sits beside the archive, and `SHA256SUMS` inside
  covers every packaged file. The packager re-verifies the internal manifest
  after extracting to a clean temporary directory.

- [x] Do not include build directories, object files, developer paths, stale architectures, unsupported formats, or unrelated SDK material.

  EVIDENCE: Staging copies an explicit allowlist, never a directory sweep. The
  packager fails on any `*standalone*`, `*vst2*`, or `testfiles` entry, and on
  prohibited text matching a `/Users/<name>/` path, `/private/tmp`, or a private
  key marker. Architecture and deployment target are asserted by
  `MacArtifactTests.cmake` on the extracted copy.

- [x] Confirm archive extraction preserves macOS bundle structure and executable permissions.

  EVIDENCE: The packager rejects any archive entry with an absolute path or a
  `..` traversal, requires exactly one top-level directory, and after extracting
  to a clean directory requires both `Contents/MacOS/Juicy16` binaries to still
  be executable. Bundle structure is then revalidated in place by the metadata
  and artifact tests.

- [x] Scan the final archive for secrets, signing credentials, usernames, absolute developer paths, and unintended personal data.

  EVIDENCE: The existing text scan used `grep -I`, which skips binaries — the
  place a build path is most likely to survive. The compiled binaries are now
  scanned explicitly with `strings` for `/Users/<name>`, `/private/tmp`,
  `/opt/homebrew`, and `/usr/local/{opt,Cellar}`. The only match is JUCE's
  shared-folder literal, which is allowlisted; the source-path remapping in the
  dependency recipe keeps everything else out. Both scans fail the package rather
  than reporting advisorily.

- [x] Extract the uploaded archive into a clean directory and rerun signature, metadata, dependency, and host-discovery checks.

  PARTIAL EVIDENCE: After the checked-in VST3 fixture, DLS release gate,
  transactional rejection coverage, and Unicode fixes were added,
  `distribute/bundle_macos.sh` produced
  the refreshed local-only
  `Juicy16-0.5.1-alpha.1-BC1-macos-arm64-LOCAL-DIRTY-ADHOC.zip`, SHA-256
  `63c1b3bef83b702ef64f72357e57a1f7996da62def1477feb0bb7bca473aa8e8`.
  A second run produced the identical hash. The script verified the internal
  manifest after clean temporary extraction and reran metadata, signature,
  architecture, deployment-target, dependency, and embedded-path checks. The
  script also proved dirty-worktree and required-distribution-signature refusal
  paths. The packaged AU was extracted, installed, and passed
  `auval -strict -q -v aumu Jc16 Pkst`. The exact signed, clean-built, uploaded
  candidate and real-host discovery remain open, so the parent item above stays
  unchecked pending Phase 7 approval of the artifacts themselves.

### Acceptance criteria

- [-] The downloaded artifact is byte-for-byte the approved artifact or has a documented packaging transformation.

  DESCOPED: This is the checksum check below, stated a second way. There is no
  packaging transformation between build and upload — the archive that is built is
  the archive that is published.
- [-] Package contents match the published manifest.

  DESCOPED: `SHA256SUMS` inside the package already lists every packaged file and
  is verified after clean extraction. A second, separately published manifest is
  the same information maintained twice.
- [ ] Checksums match after upload and download.

## 8.3 Diagnostic and feedback design

- [x] Choose the feedback channel and publish a structured issue template.
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

  EVIDENCE: `.github/ISSUE_TEMPLATE/beta_bug.yml` provides structured intake. Email reports go to `contact@pokestir.com` with required subject prefix `[Juicy16 VST]`, documented in the tester and triage guides.

- [x] Decide what diagnostics the plugin exposes in Beta 1.
  - Keep the visible plugin version.
  - Consider an opt-in “copy diagnostic information” action containing only non-sensitive build/runtime facts.
  - Do not collect or transmit telemetry without the approved privacy design and explicit documentation.
  - Never include full user paths, project names, or font contents by default.

  DECISION: Beta 1 exposes only its visible version and latest local bank-load
  status/message. It will not add a copy-diagnostics action, automatic crash
  reporter, telemetry, or background upload. Testers manually submit the minimum
  requested facts through the separate feedback channel.

  EVIDENCE: `PRIVACY.txt` and `docs/BETA_TESTER_GUIDE.md` publish this boundary;
  JUCE web/curl integrations remain disabled at build time. The UI status label
  shows version/load state without exposing a full path in its visible text.

- [x] Define crash-log collection instructions for macOS and Windows.
- [x] Create a reproducibility checklist for maintainers.
- [-] Define how tester-submitted fonts/projects are stored, accessed, and deleted.

  DESCOPED as a published retention policy. The operative rule is already in
  `docs/TRIAGE.md` and is the strict one: ask for the minimum, never ask for a bank
  the tester cannot legally share, delete the working copy once the defect is
  reproduced, build a synthetic fixture rather than keeping a tester's file, and
  honour deletion requests. A formal retention period is a commitment a
  single-developer project does not need to publish to keep.

  PARTIAL EVIDENCE: `docs/TRIAGE.md` now defines the handling rules — ask for the
  minimum and never for a bank the tester cannot legally share; keep submitted
  files only in the report plus one working copy; never commit, redistribute, or
  package them; restrict access to the person triaging; delete the working copy
  once the defect is reproduced; build a synthetic fixture rather than keeping a
  tester's file for regression use; and honour deletion requests.

  Two values are deliberately left for the owner, because they are commitments to
  third parties rather than engineering choices: the maximum retention period for
  material held in the report itself, and whether the feedback mailbox carries a
  separate policy. `PRIVACY.txt` already promises that the feedback channel's
  operator publishes its own retention and access policy, so this item cannot be
  closed until those are set.
- [x] Establish labels for B0/B1/B2/B3, host, platform, format, font type, state, MIDI routing, UI, and performance.

  EVIDENCE: `docs/TRIAGE.md` and `.github/ISSUE_TEMPLATE/beta_bug.yml` define the minimum dataset, reproduction order, crash-log handling, severity, and labels. The destination is `contact@pokestir.com` with `[Juicy16 VST]`. Submitted-asset handling is now defined in the same document; only the retention period awaits owner approval.

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

- [x] Select representative performance projects.
  - One-channel light playback.
  - Sixteen-channel typical GM arrangement.
  - Dense 512-voice stress case.
  - Large SF2/SF3 and representative DLS loads.
  - Frequent Program Changes and controller automation.

  EVIDENCE: `tools/perf_probe.cpp` now runs all five, and `docs/PERFORMANCE.md`
  tabulates each against three banks — the macOS system GM DLS, a 1.5 MB corpus
  SF2, and a 25 MB repaired Awave-style DLS. At a 64-sample block on Apple Silicon
  in Debug: one channel 0.1% of realtime, sixteen channels 1.1–1.2%, the 512-voice
  ceiling 1.5–9.1%, continuous automation 11.5–12.6%, and eight concurrent
  instances 9.3–9.9%. Continuous Program Change and controller automation is the
  most expensive case measured, more so than filling the voice ceiling.

  Adding the dense case found a real defect. FluidSynth sizes its rvoice event
  queue once in `new_fluid_synth`, as `polyphony * 64`, and
  `fluid_synth_set_polyphony` afterwards grows only the voice array. Juicy16 raised
  polyphony to 512 after construction, so the queue stayed sized for FluidSynth's
  default 256: above roughly 256 sounding voices it overflowed continuously,
  dropping engine events and emitting thousands of `Ringbuffer full` warnings per
  second — 16,695 in a single ten-second probe run. Polyphony is now a setting
  applied before the synth exists, the probe runs clean, and the engine suite
  asserts the configured and reported limits agree and that 512 simultaneous
  note-ons allocate, sustain, and fully release.

- [ ] Record CPU usage, peak CPU, memory after load, load time, and glitch behavior on representative macOS and Windows systems.

  PARTIAL EVIDENCE: macOS arm64 is measured and recorded in
  `docs/PERFORMANCE.md` across all five representative loads and three banks,
  re-measured on 2026-08-20 after the polyphony fix. Windows has no baseline,
  matching the rest of the Beta 1 Windows position, so this stays open. Glitch
  behaviour under a real host scheduler is also not represented by an offline
  probe.

- [x] Test common sample rates and buffer sizes.

  EVIDENCE: `tools/perf_probe.cpp` renders sixteen channels of four-note chords
  at block sizes 64, 128, 256, 512, and 1024, each costing 57–60 ms of CPU per
  five seconds of audio — a flat 1.1–1.2% of realtime. Block size does not measurably
  affect throughput, which follows from rendering in per-event segments rather
  than per-block chunks. Sample-rate correctness at 44.1, 48, 88.2, and 96 kHz,
  plus fail-silent above 96 kHz, is covered by the engine suite.

- [x] Test repeated bank loads for leaks or unbounded memory growth.

  EVIDENCE: Twenty alternating reloads grow 1.7 MB (1.5 MB SF2), 6.0 MB (1.9 MB
  DLS), and 28.8 MB (25 MB DLS) — bounded, and far below the multiple-of-bank-size
  signature of a per-load leak. The probe fails if growth reaches
  `max(64 MB, 2x bank size)`. Reloads alternate between the bank and an identical
  temporary copy, because restoring an unchanged path would not notify.

- [x] Test repeated editor open/close and plugin instance create/destroy cycles.

  EVIDENCE: Twenty processor create/destroy cycles grow 0.1 MB — flat. Editor
  cycles needed care: the first editor costs ~26 MB of one-time JUCE font,
  graphics, and window initialisation, which a single before/after delta would
  let hide a per-cycle leak behind. Measured separately, the following nineteen
  cycles cost +0.3 MB total, and the probe asserts that steady state at an 8 MB
  ceiling. Confirmed flat out to forty cycles during investigation.

- [x] Test multiple plugin instances up to a documented reasonable limit.

  EVIDENCE: Eight concurrent instances, each with all sixteen channels sounding,
  render in 8.9–9.4% of realtime combined and cost 4.8–26.7 MB each depending on
  bank size. Eight is the documented probe limit; the measured headroom implies
  substantially more are viable, which is deliberately not claimed without a host
  test.

- [x] Define Beta 1 performance thresholds and classify failures using the severity policy.

  EVIDENCE: `docs/PERFORMANCE.md` tabulates each automated check, its threshold,
  and its severity if it fails — B1 for realtime rendering and for any of the
  three leak checks, B2 for the concurrent-instance limit. The thresholds are
  stated as leak detectors rather than a performance contract, and the known
  limits section separates engine limits (512-voice ceiling, 96 kHz maximum, one
  stereo output) from defects so testers can tell them apart.

### Acceptance criteria

- [ ] Normal 16-channel playback is stable at the agreed minimum system and buffer size.

  PARTIAL EVIDENCE: Sixteen-channel playback costs 1.1% of realtime at a 64-sample
  block in a Debug build on Apple Silicon. The agreed minimum system is macOS 11
  arm64, which has not been measured — no such machine is available here.

- [x] Stress limits are documented so testers can distinguish expected limits from regressions.

  EVIDENCE: `docs/PERFORMANCE.md` publishes the reference measurements, the
  automated thresholds with severities, and a known-limits section covering the
  512-voice ceiling, the 96 kHz rate maximum, the single stereo output, and the
  fact that CPU scales with sounding voices rather than channels used.

## 8.6 Accessibility and UI beta pass

- [ ] Verify every interactive control has a useful accessible name and role.

  PARTIAL EVIDENCE: The headless suite verifies that the bank picker, channel
  table, on-screen keyboard, status label, and all six sliders carry accessible
  names, titles, and descriptions, and that the sliders and table are the
  concrete JUCE classes whose accessibility handlers supply their roles. What
  remains is inspection with a real screen reader: JUCE only creates native
  handlers once a component has a window peer, so a headless run cannot prove
  what VoiceOver actually announces.

- [x] Verify keyboard navigation does not interfere with the on-screen MIDI keyboard unexpectedly.

  EVIDENCE: `SurjectiveMidiKeyboardComponent` requests keyboard focus in its own
  constructor, which would let the on-screen keyboard swallow typed input meant
  for the controls; the editor clears it afterwards. The headless suite now
  asserts the resulting arrangement rather than trusting construction order — the
  editor takes focus while the on-screen keyboard and the channel table both
  decline it.

  Checking that also exposed a real gap: JUCE sliders decline keyboard focus by
  default, so all six sound controls were mouse-only. They now request focus
  explicitly, which makes JUCE's built-in arrow-key handling reachable, and the
  suite asserts it for every slider.

- [x] Verify focus initialization is deterministic.

  EVIDENCE: `focusInitialized` is explicitly initialised false, and the editor
  grabs focus once, only when it is visible and does not already hold focus,
  latching the flag only after focus actually lands on the editor. There is no
  path that depends on construction order or on a race with the host.
- [ ] Check light/dark appearance and host-provided scaling where supported.

  PARTIAL EVIDENCE: Juicy16 draws from JUCE's default `LookAndFeel_V4` colour
  scheme and does not switch on the system appearance, so there is one fixed
  appearance rather than a light and a dark variant. That is a deliberate
  limitation, recorded in `docs/KNOWN_ISSUES.md`, not a defect to fix for Beta 1.

  Host-provided scaling is now measured on macOS rather than assumed.
  `vst3_multitimbral_smoke` creates the editor view through `IPlugView` without a
  window and checks what a host can ask before attaching: the view exists,
  supports the platform type, reports a 500x547 default size — which matches
  `GuiConstants::defaultHeight` exactly — and constrains nonsense requests to
  500x300 minimum and 1216x1000 maximum rather than accepting them verbatim.

  The scaling answer is a real finding: `IPlugViewContentScaleSupport` is
  exposed, and `setContentScaleFactor` returns `kResultFalse` on macOS. That is
  JUCE behaving correctly — the window server applies the backing scale factor,
  so there is nothing for the plugin to apply — and the suite now asserts that
  platform-specific answer, its consistency across 1.0, 1.25, and 2.0, and that
  the reported logical size does not move. Windows and Linux are where a host
  actually drives this, and the test expects `kResultTrue` there.

  Open: Windows has no artifact, so the branch that matters for plugin-side
  scaling has never run. A real window and host focus chain are still required
  for the rest of the UI pass.
- [x] Check minimum, default, and maximum editor sizes.
- [x] Confirm all 16 rows remain reachable at minimum height.
- [x] Check long font paths, long Unicode preset names, sparse banks, and missing presets.

  EVIDENCE: The engine suite copies the real system DLS to a nested Unicode path
  over 200 characters, loads it, renders audio, verifies the exact path survives
  state serialisation, and restores the prior bank. Patch-list coverage separately
  preserves long Unicode preset names, sparse banks, duplicate entries, and
  missing program slots, and a bank with zero playable presets is rejected rather
  than loaded empty. Native file-picker display of such a path still needs a real
  window and is covered by the parent UI item.

- [x] Check error messages for readability and recovery actions.

  EVIDENCE: Every bank-load failure message stated the problem but not what to do
  about it. Each now names a recovery action: a missing or unreadable file asks
  the user to check it is still in place and readable; an unloadable bank
  suggests it may be corrupt or an unsupported variant and to try another; a bank
  with no presets asks for one containing at least one instrument; and a RIFF
  overrun explains the file is truncated or corrupt and suggests re-exporting or
  re-downloading. Messages surface in the status label and its tooltip, and the
  label meets WCAG AA contrast in both its normal and error colours.
- [x] Check color contrast for selected rows, labels, sliders, and disabled states.

  EVIDENCE: Contrast is now computed rather than eyeballed. The headless suite
  resolves the actual colours from the live `LookAndFeel` and measures WCAG 2.x
  relative-luminance contrast ratios, asserting the 4.5:1 AA threshold for
  normal-size text. It found two real defects on 2026-08-19.

  The selected channel row was filled with a fixed `Colours::lightblue` while its
  text kept `ListBox::textColourId`, which is near-white in the default scheme —
  **1.53:1**, effectively illegible, and on the one row the user is working with.
  The row now uses the scheme's `highlightedFill`/`highlightedText` pair, which is
  designed to contrast: **16.69:1**.

  The error status label used plain `Colours::salmon` at **4.40:1**, just under
  the threshold. Brightened by 0.25 it reaches **5.27:1** while remaining an
  obvious error colour.

  Unselected row text was already 13.16:1 and the normal status label 7.35:1.
  Sliders and disabled states draw entirely from JUCE's default scheme with no
  Juicy16 override, so they carry the framework's own contrast decisions.
- [x] Verify the UI identifies the selected MIDI channel and loaded bank clearly enough for beta diagnosis.

  EVIDENCE: The selected channel is shown by a highlighted row in the channel
  table, which was the one place this failed: the highlight left its text at
  1.53:1 contrast, so the selected row was the hardest row to read. It now uses
  the scheme's highlighted fill and text at 16.69:1. The status label reports the
  running version together with the latest bank-load status and message, with the
  full message in its tooltip, so a tester can state both the version and what the
  plugin thinks it loaded without opening a log.

  PARTIAL EVIDENCE: The bank picker, channel list/table, per-row instrument
  dropdowns, on-screen keyboard, version/load-status label, groups, and all six
  sliders now have explicit accessible names/titles/descriptions/help. Slider
  names spell out the abbreviated visual labels, include their CC numbers, and
  explain direction/neutral behavior. The headless engine suite instantiates the
  full editor and verifies accessible metadata plus JUCE's built-in slider/table
  role-bearing component types. The engine suite also copies the real system DLS
  to a nested Unicode path longer than 200 characters, loads it, renders audio,
  verifies the exact path survives state serialization, and restores the prior
  bank. Patch-list coverage separately preserves long Unicode preset names,
  sparse banks, and missing program slots. Native file-picker/display inspection,
  VoiceOver/Narrator, keyboard/focus, contrast, and scale testing remain open, so
  the long-path item and parent UI pass are not checked off. The headless editor
  regression also exercises the enforced minimum, natural default, and maximum
  bounds; requires nonempty bounds for the bank picker, channel table, sound
  controls, keyboard, and status; proves rows 1 and 16 can each be scrolled into
  view at minimum height; and proves all 16 rows fit at the default height. The
  direct Debug run and strict arm64 macOS Release test passed on 2026-08-19.

### Acceptance criteria

- [x] Core loading, channel selection, patch selection, and parameter editing workflows are usable without a mouse where the framework/host permits.

  EVIDENCE: The headless suite inspects what actually accepts keyboard focus
  instead of assuming from control types, which corrected an earlier assumption:
  `FilenameComponent` is a container and does not take focus itself, but its
  browse button does, so bank loading is reachable. All six sound parameters
  accept focus, so parameter editing is reachable.

  Channel and patch selection were the real gap, and both are now closed rather
  than documented as limitations. The channel table declined focus, justified as
  stopping arrow keys from fighting MIDI-driven row selection — but nothing drives
  it: `selectChannelForEditing` had one caller, a mouse click, and incoming MIDI
  changes a channel's *program*, not which row is selected. The table therefore
  takes focus, `selectedRowsChanged` turns an arrow-key row change into the
  selected channel, and `uiState.selectedChannel` stays the single source of truth
  with a re-entrancy guard breaking the two-way notify loop. Restored state opens
  on the channel it was editing, so the first arrow key moves from there rather
  than from row 0.

  Patch selection follows the standard table idiom: `returnKeyPressed` opens the
  selected row's instrument dropdown, which is then an ordinary keyboard-driven
  menu, and JUCE returns focus to the table when it closes. The suite drives all
  16 rows — including rows scrolled out of view at the minimum height, which have
  no cell component until `patchComboForRow` scrolls them in — and requires each
  to resolve to a populated, focusable dropdown whose selection actually assigns
  that channel's program; an out-of-range row must resolve to nothing rather than
  to a neighbour's dropdown. Verified on 2026-08-20: Debug gate 13/13 with
  first-party warnings as errors, the ASan+UBSan gate clean, and all four offline
  harnesses still at zero leaked bytes under `leaks -atExit`.

  Open, and recorded in `docs/KNOWN_ISSUES.md` rather than here: rendering the
  popup, what VoiceOver/Narrator announce, and whether a given host passes Tab
  through to the plugin editor all need a real window.
- [x] No supported resize or text case makes essential controls inaccessible.

  EVIDENCE: Resize was already covered at minimum, default, and maximum bounds.
  The text half is now covered too, which matters because every string the editor
  shows is user-controlled — a bank path, a FluidSynth preset name, a load-error
  message. The headless suite drives the status text through empty, a
  4000-character run, a 500-character CJK and emoji mix, and a long narrow-glyph
  string, at all three supported sizes, and requires every essential control to
  keep non-empty bounds AND stay inside the editor. A control pushed off-window is
  unreachable even when its bounds look fine, so both are checked.

## 8.7 Security and robustness pass

- [x] Fuzz or stress malformed state blobs and RIFF/DLS headers within a safe harness.

  EVIDENCE: Extended on 2026-08-20 to the input path the file and state fuzzers
  never touched. `JuicySFEngineMidiTests --midi-soak` generates randomised but
  well-formed MIDI — the domain a host actually delivers — across all 16
  channels: notes, CC0-CC123, Program Change, 14-bit bend, both pressure kinds,
  and SysEx weighted towards the real GM/GS/XG reset payloads, near misses one
  bit away from them, and arbitrary payloads, which is the only
  attacker-controlled byte stream reaching Juicy16's own parser.

  After every block it requires finite bounded audio, in-range program and bend
  state on every channel, the 512-voice ceiling, serialisable saved state, and
  that All Sound Off leaves nothing running. The seed is an argument and the
  generator is `std::mt19937`, so any finding is exactly reproducible; when a
  block first records a program-apply failure the soak prints that block's whole
  event list, because a fuzz harness that cannot name its input produces
  unactionable findings.

  Coverage on 2026-08-20, against a frozen binary with the full CC0-127 range
  including channel-mode messages: **40 seeds of 200,000 blocks — 8,000,000
  blocks, roughly 156 million MIDI events of which about 15.6 million were SysEx
  — with zero failing seeds.** Separately, a 40,000-block run under ASan+UBSan
  rendered 782,123 events including 78,068 SysEx payloads with no sanitizer
  findings. Registered as the deterministic `engine_midi_soak` CTest at 2,000
  blocks. Documented in `docs/MIDI_SOAK.md`.

  An earlier sweep found a defect in the harness itself rather than the plugin:
  the engine invariant allowed a bank up to 255 while the saved-state invariant
  still required 0-128, so the two contradicted each other and the drum-channel
  bank legitimately tripped it at seed 16, block 58880. Both now share one
  constant.

  It found two defects on its first run, both now pinned by deterministic
  scenarios and recorded as B2 in `docs/KNOWN_ISSUES.md`:

  1. **Channel-mode messages disabled MIDI channels — FIXED.** FluidSynth
     implements MIDI 1.0 basic-channel semantics faithfully, so one CC124 on
     channel 1 left only channel 1 responding, which is the exact shape of
     failure this plan calls a blocker. Severity was bounded by measurement
     rather than assumption — any GM/GS/XG reset recovered it, and an earlier
     assumption that resets would *not* recover was wrong and the test caught it.
     Resolved on the owner's decision by forwarding the controller and then
     restoring the 16-channel layout, so the Phase 1.5 "every CC0-127 reaches
     FluidSynth" contract and the "exactly 16 MIDI channels" product contract
     both hold. Verified across all four controllers at six values each, plus an
     interleaved burst, and the soak generator now covers CC0-127 again.
  2. **Drum-channel Bank Select exceeds the documented bank range.** A drum
     channel adds FluidSynth's 128 offset on top of the Bank Select MSB, so the
     XG convention CC0=127 reports bank 255. Engine and saved state record it;
     the 0-128 `bank` parameter cannot, so the three diverge and a reload moves
     the channel back to 128. Audio is unaffected — the substituted kit measured
     1.0000 waveform correlation — so it is a state and UI inconsistency, not an
     audible defect. Widening the parameter range would move a frozen
     compatibility surface, so it is recorded rather than changed.

- [x] Verify huge, empty, truncated, read-only, inaccessible, and concurrently removed font files fail safely.

  EVIDENCE: Six scenarios were added to the engine suite on 2026-08-19 and found
  a real defect. `writeRepairedTempCopy` called `loadFileAsData` on any file
  whose first twelve bytes looked like a DLS RIFF header, reading the whole
  user-selected file into memory; an 805 MB input would have been buffered
  entirely. Repair is now capped at 512 MB.

  Capping alone was not sufficient. Above the cap the file goes to FluidSynth
  unrepaired, and a sparse 805 MB image declaring a 4 GB RIFF payload took
  **72.5 seconds** to fail inside FluidSynth's parser — a message-thread hang in
  a host. An unrepaired bank whose RIFF container overruns the file is now
  rejected before FluidSynth sees it, with a specific message. A well-formed bank
  passes at any size, so legitimate large SoundFonts are unaffected, and
  repairable Awave-style DLS files are unaffected because repair runs first.

  Scenarios: zero-byte, truncated, oversized-sparse, read-only (which must
  *succeed*, since repair writes to a temporary copy), and removed-after-loading
  (verified through a fresh instance, because restoring an unchanged path into
  the same instance does not notify). Inaccessible (`chmod 000`) and moved or
  missing files were already covered. Peak RSS across the suite is 110 MB and the
  oversized case now fails in well under a second. `docs/DLS_REPAIR.md` records
  the boundary.

- [x] Verify archive/package extraction does not rely on unsafe paths or executable installers not covered by signing policy.

  EVIDENCE: `distribute/bundle_macos.sh` now rejects an archive containing an
  absolute path or a `..` traversal entry, and requires exactly one top-level
  directory, so extraction cannot write outside the chosen destination. After
  extraction it requires both bundle binaries to retain the executable bit. The
  package contains no installer, script, or executable other than the two plugin
  binaries: it is a plain archive the user copies into their plugin folders, so
  nothing outside the code-signing policy runs.
- [x] Confirm no runtime networking occurs unless explicitly approved and documented.
- [x] Confirm temporary repaired DLS copies use safe unique names, restricted locations, and reliable cleanup.

  EVIDENCE: The ASan+UBSan harness covers 1,000 malformed state blobs and 6,000 RIFF/DLS property inputs; JUCE curl/web-browser support is compiled out and no runtime networking call exists; DLS repair uses `File::createTempFile`, never edits the source, and deletes the active temp on replacement/destruction.
- [x] Confirm logs and diagnostic output do not expose sensitive paths or file contents by default.
- [ ] Review dependency versions for known critical security advisories before candidate freeze.

  PARTIAL EVIDENCE: `docs/DEPENDENCIES.md` records the complete macOS release
  closure with the version actually built, and a version-currency review dated
  2026-08-19. libogg 1.3.6, libvorbis 1.3.7, FLAC 1.5.0 and libsndfile 1.2.2 are
  exactly upstream's latest releases; Opus 1.6.1 is ahead of the GitHub tag.
  FluidSynth (2.5.5 vs 2.6.0) and JUCE (8.0.14 vs 9.0.1) are deliberately behind,
  each pinned by a recorded project decision and enforced at configure time.

  An actual advisory review was then performed on 2026-08-20 and is recorded in
  `docs/DEPENDENCIES.md`. It found one open issue, which is exactly why version
  currency is not a substitute: **CVE-2025-52194**, a buffer overflow in
  libsndfile's `ircam_read_header`, affects the pinned 1.2.2 — upstream's own
  latest release — and the vulnerable handler is linked into the artifact.

  Reachability was established by reading the call path, not assumed. FluidSynth
  hands a SoundFont's embedded sample bytes to `sf_open_virtual()` with no format
  pre-validation and only *warns* when the detected format is not OGG, so a
  crafted `.sf3` reaches the vulnerable reader. A bank file is the product's
  primary untrusted input. No proof-of-concept was built and no crash was
  observed, so this is reachability rather than a demonstrated exploit, and the
  severity call is the owner's.

  The same review confirmed the libsndfile MPEG advisories are **not** reachable:
  `ENABLE_MPEG=OFF` keeps that code out of the binary, verified by symbol scan.

  **Resolved on 2026-08-20 by taking the recommended option: patch the pinned
  source.** Waiting for a release was ruled out — libsndfile `master` carries the
  fix, but 1.2.2 is still the newest release and no release contains it.
  `vendor/libsndfile_patched/libsndfile-1.2.2-ircam-hardening.patch` backports
  `master`'s `psf_lrintf` conversion for the exact line the CVE names, and adds
  the lower channel bound 1.2.2 is missing in both the little-endian read and the
  big-endian retry — without it a zero or negative count from the file reaches
  `channels * bytewidth`, which is signed overflow for a large negative value and
  a later divide by zero for zero.

  `tools/build_macos_dependencies.sh` applies the diff and
  `tools/build_windows_dependencies.ps1` makes the same two substitutions, since
  Windows has no guaranteed `patch.exe`. Both bracket the edit with the pre- and
  post-edit `src/ircam.c` hashes and fail the build on any mismatch, so a closure
  cannot be produced without the patch and upstream source drift is caught rather
  than patched around. The `dependency_patch_contract` CTest pins the patch, both
  recipes, and `vendor/libsndfile_patched/README.md` to the same three hashes; it
  was confirmed to fail against a tampered patch.

  Verified on 2026-08-20: `patch -p1` applies cleanly to the pristine upstream
  `src/ircam.c` and produces exactly the reviewed hash
  `27c25a59...`. `psf_lrintf` is a `static inline` in 1.2.2's own `common.h`,
  which `ircam.c` already includes, so the backport needs nothing else.

  Built and linked on 2026-08-23, closing the count that was outstanding. The
  recipe fetched every pinned tarball with its checksum verified, reported both
  the pre- and post-edit `src/ircam.c` hashes as matching, and FluidSynth linked
  the patched libsndfile. `tools/ci_gates.sh release` then passed end to end on
  that closure — 15/15 CTests, including `font_load_release_sf3`, which is the
  SF3 path that actually reaches libsndfile, plus `macos_artifact_portability`
  and `dependency_patch_contract`. No crafted-SF3 proof-of-concept exists, so the
  reachable path is closed rather than a demonstrated exploit disproven.

  The task stays open on one count: the advisory review must be repeated against
  a live source at candidate freeze rather than relied on from today.

### Acceptance criteria

- [ ] No known B0 security or privacy issue remains.
- [x] Malformed user-controlled files and state fail without memory corruption or destructive modification.

  EVIDENCE: 1,000 malformed state blobs, 6,000 RIFF/DLS property inputs, and the
  hostile-file scenarios above all fail visibly while preserving the active bank,
  its audio, and saved state. No input path modifies the user's file: DLS repair
  writes only to a temporary copy. ASan+UBSan passes across the engine and font
  harnesses.

## 8.8 Go/no-go review

- [ ] Freeze the candidate commit and candidate number.
- [ ] Confirm all Phase 0–7 exit criteria required for Beta 1 are complete.
- [ ] Review every open issue by severity.
- [ ] Confirm zero open B0 and B1 issues.
- [ ] Confirm every open B2 issue is documented and approved. ("Assigned" is
      dropped: there is one developer.)
- [ ] Confirm candidate artifacts, package manifest, checksums, validation logs, and host results refer to the same commit.
- [ ] Confirm licensing and privacy approval.

  NOT descoped, and deliberately left open: GPLv3/AGPLv3 obligations bind on
  distribution regardless of team size, and this is the last gate that checks them
  against the actual package. Only the original clause's "distribution authority"
  was dropped — that authority is the author's own.
- [-] Confirm feedback and withdrawal owners are available during launch.

  DESCOPED: Both are the author, who is by definition present when they publish.
- [ ] Record the go/no-go decision in the Decision log.

## 8.9 Controlled launch

- [ ] Upload artifacts to the approved beta distribution channel.
- [ ] Download them again and verify checksums.
- [ ] Publish Beta 1 release notes, installation instructions, known issues, and feedback link together.
- [-] Start with a small canary group if the tester pool is large.

  DESCOPED: The tester pool is not large. The condition in the task never fires.
- [ ] Confirm at least one successful install and plugin discovery on each platform/format before broadening access.
- [ ] Monitor initial reports closely for discovery failures, crashes, missing dependencies, state corruption, and severe audio faults.
- [ ] Be prepared to withdraw the candidate immediately if a B0 issue appears.

## 8.10 Beta feedback cycle

- [-] Acknowledge and classify incoming reports.

  DESCOPED as a process. Reports go to one mailbox that one person reads;
  `docs/TRIAGE.md` records the severity vocabulary for when it helps.
- [ ] Reproduce B0/B1 reports against the frozen candidate before changing code where possible.
- [-] Link duplicates and preserve the clearest reproduction.

  DESCOPED: Issue-tracker hygiene for a report volume that will not occur.
- [-] Separate product-scope requests from regressions and defects.

  DESCOPED: Triage bookkeeping between people. One person reading the reports
  already knows which is which.
- [ ] Update the known-issues document when an approved workaround exists.
- [-] Maintain a candidate-to-candidate changelog.

  DESCOPED: `CHANGELOG.md` already is this, and is updated in the same change as
  the work it describes.
- [ ] Rerun the full relevant regression subset for every fix candidate.
- [ ] Require the complete Beta 1 technical gate again for any replacement public candidate.

## Phase 8 exit criteria

- [ ] Beta 1 is distributed with a complete tester contract and support boundary.
- [ ] Every public artifact is traceable to the validated candidate commit.
- [-] Feedback intake, privacy handling, triage, rollback, and candidate replacement processes are operating.

  DESCOPED as "processes". The operative version is: a mailbox exists and is read,
  `docs/TRIAGE.md` says how submitted material is handled, and a bad candidate is
  withdrawn by deleting the download. The real gates are the technical ones above.
- [ ] A first install succeeds on every advertised platform and plugin format.
      (Was "canary installs"; the canary phase itself is descoped.)
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
| 2026-08-19 | Beta release formats are macOS AU/VST3 and Windows VST3; Standalone is QA-only; VST2/AUv3 are out | Matches the intended multichannel host workflow without advertising development or legacy formats | Product owner | Approved |
| 2026-08-19 | Apple Silicon is required; Windows ARM64 and Linux are out; Intel macOS is deferred until practical build and physical validation are available | Focuses Beta 1 validation on available hardware | Product owner | Approved |
| 2026-08-19 | Public brand metadata uses Juicy16, Pokestir, pokestir.com, contact@pokestir.com, `com.pokestir.juicy16`, and `Pkst`/`Jc16`; preserve Birchlabs lineage credit | Establishes the Beta 1 identity while acknowledging the original SoundFont-engine base | Product owner | Approved |
| 2026-08-19 | Fruity LSD comparison is workflow inspiration only | Avoids unsupported exact-emulation or affiliation claims | Product owner | Approved |
| 2026-08-19 | Beta feedback email uses `[Juicy16 VST]` in the subject | Makes reports identifiable and routable | Product owner | Approved |
| 2026-08-19 | Rename the product to Juicy16 and reset binary identifiers at Beta 1 | Architectural pre-Beta sessions need not remain compatible; Beta 1 becomes the compatibility baseline | Product owner | Approved |
| 2026-08-19 | Beta 1 supports macOS 11+ arm64 and Windows 10 1607+ x64; Intel macOS is deferred | No Intel validation hardware is currently available | Product owner | Approved |
| 2026-08-19 | Use JUCE 8 under AGPLv3 while retaining GPLv3 on inherited application code | No commercial JUCE license is held; GPLv3/AGPLv3 section 13 permits the combination | Product owner | Approved; qualified package review pending |
| 2026-08-20 | Development version is `0.5.1-alpha.N`, not `0.5.1-beta.N` | The Beta 1 quality bar is explicit and unmet — no host validation, no Windows artifact, no hosted CI run — so a `beta` label would overclaim. Host identifiers are unchanged, so this is a label decision only and no compatibility baseline moves | Product owner | Approved |
| 2026-08-20 | Windows FluidSynth uses the native C++17 DLS loader (`enable-native-dls=on`, `enable-libinstpatch=off`) on the same pinned 2.5.5 as macOS | One engine version and one DLS code path across platforms; avoids libinstpatch's extra licensing and packaging obligations | Engineering | Approved; unproven until a Windows artifact runs the DLS probe |
| 2026-08-20 | Windows dependencies come from `tools/build_windows_dependencies.ps1`, not from vcpkg | The CI job's `vcpkg install fluidsynth:x64-windows` was never an approved source: unpinned version, and not configured for the native DLS loader, so it may have produced a Windows plugin that could not load DLS at all. The recipe uses the same components, versions, and checksums as macOS, so one inventory covers both platforms | Engineering | Approved; recipe unexecuted |
| 2026-08-20 | Windows links the static MSVC C runtime (`/MT`) for both the dependency closure and the plugin | A tester must not need a Visual C++ redistributable to load the VST3. The two must match or the link fails, so CMake derives the plugin's setting from `FLUIDSYNTH_LINK_STATIC` rather than exposing a second knob | Engineering | Approved; unproven until a Windows artifact exists |
| 2026-08-20 | Release-process items that only coordinate between people, or manage volume that will not occur, are descoped `[-]` rather than left open | Juicy16 is a single-developer project and the plan was written in a corporate release register. Leaving inapplicable items open makes the real gates harder to see. Licensing, privacy, checksums, Gatekeeper instructions, host testing, clean-system installation, and the B0/B1 bar are explicitly NOT descoped — they protect someone other than the author or are binding on distribution | Product owner | Approved 2026-08-20 |
| 2026-08-20 | CC124-127 are forwarded to FluidSynth, then Juicy16 restores its 16-channel layout | FluidSynth honours MIDI 1.0 basic-channel semantics, so a single Omni Off left only channel 1 responding. Filtering the controllers would have broken the Phase 1.5 rule that every CC0-127 reaches the engine; neutralising after delivery keeps both that rule and the "exactly 16 MIDI channels" product contract. Mono mode is consequently not honoured as a per-channel monophonic setting | Product owner | Approved 2026-08-20 |
| 2026-08-20 | ~~The drum-channel Bank Select range (128 + MSB, up to 255) ships as a documented B2~~ **Superseded 2026-08-23** by the row below | The audio is provably unaffected — the substituted kit measures 1.0000 waveform correlation — so the only harm is a state/UI inconsistency. Widening the `bank` parameter to 0-255 would move a Beta 1 automation surface that is meant to freeze at release, for no audible benefit | Product owner | Approved 2026-08-20 |
| 2026-08-20 | Beta 1 ships ad-hoc signed; no Developer ID signature or notarization | No Developer ID is held for this release. The package filename records `ADHOC`, and tester documentation must carry first-launch Gatekeeper instructions or testers cannot open the plugin at all | Product owner | Approved 2026-08-20 |
| 2026-08-20 | Windows finds FluidSynth through its CMake package config rather than pkg-config | MSVC has no pkg-config by default and the pinned closure installs FluidSynth's own config. The path is not Windows-only code: it is exercised on macOS, where the whole suite runs against it, and macOS release validation still refuses it because its per-archive checks need pkg-config's static link list | Engineering | Approved; exercised on macOS 2026-08-20 |
| 2026-08-23 | If Windows cannot be validated in time, Beta 1 ships macOS-only and Windows moves to Beta 2 | A Windows PC is available, so this is a fallback rather than the plan; it exists so an unavailable machine cannot silently become an untested platform claim | Product owner | Approved |
| 2026-08-23 | The macOS 11.0 floor ships declared but runtime-untested, as a documented B2 | Only one Mac is available and it runs macOS 26. Strict validation proves every Mach-O declares `minos 11.0`; nothing proves one boots there. Raising the floor to 26 would exclude nearly every tester, so the claim ships qualified: "built for 11.0+, validated on current macOS" | Product owner | Approved |
| 2026-08-23 | FL Studio serves as both the additional AU host and the additional VST3 host; Logic Pro is untested in Beta 1 | Logic is not owned and the trial was declined. AU coverage is therefore `auval -strict` plus FL Studio; the Logic-specific row stays open as a published gap rather than an implied pass | Product owner | Approved |
| 2026-08-23 | The Phase 7.2 host matrix runs core-subset-first, then completes before launch | The game-rip fixture, the FL-vs-Cubase comparison, and state save/restore are what catch B1s; sample rates, block sizes, voice limits, and reset behavior follow. The full matrix is still required before publishing — only the order changed | Product owner | Approved |
| 2026-08-23 | Qualified legal review is waived; the owner self-reviews the package against `docs/LICENSING.md` before freeze | **Explicit override of a gate the plan marks as binding.** GPLv3/AGPLv3 obligations bind on distribution regardless. The mitigations are a small named tester list and public source at the beta tag; the risk is the owner's and is recorded here rather than absorbed silently | Product owner | Approved as an override |
| 2026-08-23 | The repository becomes public at the beta tag, satisfying the corresponding-source offer | The tag the artifacts came from is then publicly fetchable and `BUILD_INFO.txt` already records the exact commit, so the source offer is self-evidencing rather than a promise to honour requests | Product owner | Approved |
| 2026-08-23 | Beta 1 is distributed from pokestir.com | Own the presentation, so the Gatekeeper procedure is read before download rather than after. Checksum publication and download-and-verify become the owner's responsibility | Product owner | Approved |
| 2026-08-23 | The candidate is frozen and tagged after the Windows artifact and the core host subset pass | Freezing earlier means re-freezing after the first host fix; freezing later means the tag is genuinely the candidate every artifact traces to | Product owner | Approved |
| 2026-08-23 | Beta 1 is versioned `0.6.0-beta.1` | A minor bump marks the compatibility break alpha.5 already made: parameters 24 to 21 and state schema 2 to 3. Beta 1 remains the frozen compatibility baseline; only the number changes | Product owner | Approved |
| 2026-08-23 | Hosted CI runs both the macOS and Windows jobs on the next push | The Windows job is the cheapest first test of `tools/build_windows_dependencies.ps1` — it proves or fails the recipe before time is spent on the physical PC. The current work is committed but deliberately not pushed, so this fires when the owner pushes | Product owner | Approved; push pending |
| 2026-08-23 | Beta 1 goes to 3-5 close testers, with reports by email to `contact@pokestir.com` | Matches the private-invite posture the licensing override depends on, and keeps "confirm one successful install per platform before broadening" a conversation rather than a process | Product owner | Approved |
| 2026-08-23 | The fixed single appearance and the unhonoured mono mode ship as documented B2s | Both are deliberate consequences of decisions already recorded, neither is audible in normal use, and both are published in `docs/KNOWN_ISSUES.md` | Product owner | Approved |
| 2026-08-23 | The drum-channel bank 255 mismatch and the silent-above-96 kHz behavior are **not** accepted as B2s and must be fixed before Beta 1 | Supersedes the 2026-08-20 drum-bank acceptance, whose premise no longer holds: it argued against moving a frozen automation surface, but Beta 1 has not shipped, alpha.5 already moved that surface, and `0.6.0-beta.1` is the release that freezes it. Fixing both now is cheaper than freezing them in | Product owner | Approved |
| 2026-08-23 | No target date for Beta 1; it ships when the gates pass | Single developer, no external commitment. A date would only pressure the host matrix, which is the part that catches B1s | Product owner | Approved |

# Risk register

| Risk | Impact | Mitigation | Status |
|---|---|---|---|
| GPLv3 application/JUCE AGPLv3 combination is packaged or described incorrectly | Distribution may be legally blocked | Preserve GPLv3 for project code, JUCE's AGPLv3 notice, complete corresponding source, and obtain qualified review of the exact package | Open pending qualified review |
| Vendored VST3 wrapper is tied to a specific JUCE implementation | JUCE updates may break builds or routing | Exact JUCE/base/result/patch hashes and a reproducible diff now fail early; real-host tests remain mandatory after any rebase | Mitigated locally; host revalidation open |
| FL Studio and Cubase use different VST3 Program Change paths | A change may appear correct in FL Studio while Cubase collapses to channel 1 or ignores changes | Preserve both mapping/unit paths and require the identical 16-channel game-rip fixture in both hosts | Mitigated by independent concrete VST3 processing tests; exact-host validation open |
| Cubase caches unit/program-list information queried before component connection | Returning the stock root-only structure even briefly can break Program Change for the plugin instance | Keep the pre-connection smoke test and require identical unit data before and after connection | Mitigated by repeatable pre/post-connection test; exact Cubase validation open |
| CC or pitch-bend messages are value-correct but block-quantized or sent to the selected UI channel | Expression, pedals, bends, and game-rip automation sound wrong despite basic note playback working | Parameterize CC0–127 and 14-bit bend tests across channels and validate audio-domain timing | Mitigated by exhaustive trace/audio tests; DAW chase validation open |
| Windows DLS support is disabled in the pinned FluidSynth build | Product claim fails on Windows | Strategy chosen and made explicit: FluidSynth 2.5.5 with `osal=cpp11`, `enable-native-dls=on`, `enable-libinstpatch=off` on both platforms; strict Windows configuration requires a real `.dls` probe as a non-skippable test | Mitigated in configuration; unproven until a Windows artifact runs the probe |
| MIDI timestamps are ignored | Audible timing errors and incorrect same-block behavior | Complete Phase 1 segmented rendering and retain VST3 program-queue sample offsets | Mitigated by engine and concrete VST3 tests; exact-host validation open |
| macOS artifacts reference developer-only dynamic libraries | Plugins fail on clean user systems | Static/embed dependencies and inspect every artifact | Mitigated locally; exact package pending |
| Signing occurs before all resources are stable | AU/VST3 may fail discovery or validation | Fix build dependencies and verify after parallel clean builds | Mitigated locally; exact package pending |
| Prebuilt static dependencies target macOS 26 even when Juicy16 declares macOS 11 | Artifact may call unavailable APIs and fail on the supported minimum OS | Strict validation rejects newer archive deployment targets; use the checksum-pinned macOS 11 source recipe and test on macOS 11 | Mitigated locally; minimum-OS runtime test pending |
| A whitespace character in the build path silently mislinks the candidate | pkg-config emits unquoted `-L` flags, so the pinned prefix is dropped and FluidSynth resolves from Homebrew instead, producing a non-portable artifact | Strict CMake configuration, `tools/build_macos_dependencies.sh`, and the release gate all refuse a whitespace path with an explicit message; `docs/CI.md` and `building.macos.md` document it | Mitigated 2026-08-19 |
| No redistributable font corpus exists | Format compatibility cannot be regression-tested | Curate licensed fixtures with provenance | Open |
| Plugin identifier changes break existing sessions | Users may lose project recall | Intentional one-time pre-Beta reset; freeze Juicy16 identifiers starting with Beta 1 and add session-recall tests | Mitigated by approved baseline; Beta 1 recall test pending |
| The Windows CI job silently built a different engine than the approved policy | `vcpkg install fluidsynth:x64-windows` pinned no version and enabled no native DLS loader, so a Windows plugin could have shipped that builds and runs but refuses every DLS bank — the product's headline format | Replaced with `tools/build_windows_dependencies.ps1`, the same pinned closure as macOS; the recipe fails if FluidSynth built without the DLS loader, and `font_load_system_dls` proves DLS at runtime against `C:\Windows\System32\drivers\gm.dls` | Found and replaced 2026-08-20; recipe unexecuted |
| A statically linked Windows VST3 still needs the Visual C++ redistributable | Testers hit a missing-DLL failure the developer never sees, on a machine that has the redistributable already | Static MSVC C runtime for both the closure and the plugin, derived from `FLUIDSYNTH_LINK_STATIC` so the two cannot disagree; the CI job runs `dumpbin /dependents` and archives the result | Mitigated in configuration; unproven until a Windows artifact exists |

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
