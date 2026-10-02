# Juicy16 audit — 2026-10-02

This audit covers the local application source, editor, MIDI/audio processing,
saved state, AU/VST3 integration, automated checks, dependency recipes, packaging
and user documentation. Review covered audio/documentation, editor behavior
and build/packaging; failures were reproduced, fixes integrated and verification
run locally.

The checkout began clean at `aea4fb0`. Findings concern that source and these
local changes, which are included in the macOS 1.0.0-beta.2 source. The later
[Windows/UI follow-up](audits/2026-10-02/README.md) records beta.2 native builds,
packages and shared-source focus fixes separately. The Mac checks below cover the extracted
macOS validation candidate; real DAW, minimum-OS, Windows and hosted-CI
validation remain pending.

## Concrete fixes

| Area | Defect and change | Regression evidence |
| --- | --- | --- |
| Project recall | Same-bank recall could retain MIDI-modified synth programs/CC7/CC10 when matching parameter values emitted no callback. Saved channel records are now reapplied explicitly. Minimal bank-only states keep existing instrument assignments. | Same-instance restore across all 16 channels, AU save/restore and failed-bank transaction fixtures. |
| Reverb recall | A save between a knob edit and deferred Custom reconciliation could be overwritten by its saved profile. Saved values now take precedence and the profile label is reconciled. | Saved custom value restored before and after async updates. |
| Live reverb changes | A pending profile could overwrite a later knob edit or a later selection of Custom. Queued changes now cancel superseded intent. | Profile→knob, knob→profile and profile→Custom sequences. |
| Old projects | Absent mixer, mute/solo and reverb fields inherited settings from an already-used instance. Schema-specific defaults now reset these fields. | Pre-mixer/pre-reverb state restored into a configured, previously used instance. |
| Accent persistence | The selected accent was omitted from saved projects; recalled colors did not refresh every private palette/open settings control. Schema 11 saves the accent and propagates updates. | All 12 choices round-trip, schema 10 defaults to Sage, editor/file picker/open settings follow recall. |
| Pedals and mute | Sustain/sostenuto could hold notes indefinitely after mute or solo exclusion. Pedal-held voices are cut while controller values are preserved; ordinary notes keep their release. | CC64/CC66 × mute/solo voice-count and controller fixtures. |
| Reset SysEx | GM Off, malformed GS resets and invalid XG messages triggered recovery incorrectly; valid XG factory reset was missed. Recovery now requires backend acceptance and matching reset payload/checksum. | Invalid-message preservation plus valid XG reset recovery for programs, mixer, expression and bend. |
| Drum fallback | A kitless bank at a nonzero load offset could select bank zero from the wrong namespace. Fallback now uses the loaded font's melodic-bank offset. | Synthetic offset bank verifies accepted logical patch and rendered pitch. |
| Empty audio blocks | MIDI was dropped from zero-sample blocks above the native engine-rate ceiling. Empty blocks now dispatch events without rendering. | Channel 16 Program Change and CC7 at 192 kHz. |
| High-rate rendering | Artificial input read-ahead shifted MIDI, and nondivisible block sizes ended with silence. Rendering now tracks remaining host frames, including fractional input held by the interpolator, before requesting input or mapping timestamps. | Split/continuous stereo comparisons at 176.4/192/256/768 kHz with even, odd and 1/2/3-frame blocks. |
| Host/state bounds | Nonfinite/extreme rates and corrupt normalized state values could reach integer conversions or gains. Unsafe rates mute and recover safely; finite state values clamp and nonfinite values are ignored. FIFO capacity multiplication uses 64-bit arithmetic. | NaN/Inf/negative/zero/extreme rates and corrupt parameter XML fixtures. |
| Realtime callbacks | Reverb ID strings allocated during unrelated automation callbacks, and a shared profile guard raced across threads. IDs are cached before playback; profile guards are thread-local and instance-specific. | Source review plus existing automation/profile/audio-equivalence checks. |
| Audition keyboard | Fixed mouse velocity ignored its configured value; destruction/focus loss could leave notes held; overlapping mouse/QWERTY input could release each other. Local inputs now share tracked ownership, start once and release on the last owner or teardown. | Velocity, channel switch, focus loss, both overlap release orders, multiple pointers and unrelated MIDI preservation. |
| UI thread affinity | Worker-thread host recall directly updated editor/rack/settings/picker widgets. These callbacks now coalesce work onto the message thread and cancel pending work on teardown. | Worker recall leaves widgets untouched until message-thread handling, then updates accent, rack, keyboard channel and open settings. |
| File picker | Clearing a bank path left the previous filename visible. Empty paths now clear the field. | Display-path clear fixture. |
| Build gates | Leak checks could reuse stale executables or accept failed/incomplete harnesses; sanitizer coverage omitted two playback harnesses; empty CTest runs could pass. Gates now rebuild appropriately, verify completion/status and reject empty test sets. | Stub-driven orchestration/failure tests and registered CTest checks. |
| AU/release workflow | AU validation was not strict, and a candidate input was interpolated directly into shell source. Workflows use strict AU validation and a quoted environment value. | Script/source review; hosted workflow execution remains pending. |
| Windows source archives | ZIPs lost executable bits and accepted missing/wrong dependency source archives. Packaging preserves indexed/native modes and validates every pinned archive before staging. | Deterministic ZIP/checksum, permission, missing/corrupt archive and recipe-parse fixtures on macOS. Native Windows verification remains pending. |
| Documentation | Historical host, leak and Windows results were presented too broadly; local protocols referred to retired controls and wrong parameter counts. Claims now distinguish published artifacts, current local evidence and pending checks. | Documentation/reference/metadata checks. |

The 131 host parameter identities/order, 16 channel-program parameters and
units, shared 128-entry program list, early unit discovery and pinned wrapper
patch remain intact. No speculative features were added.

## App-audit verification — 2026-10-02

The test platform is Apple Silicon macOS 26.6.2, with pinned JUCE 8.0.14 and the
patched static FluidSynth 2.5.7 dependency closure. Debug/Release builds keep
plugin copying disabled. AU smoke registration requires execution outside the
sandbox; it loads the built component in-process without installing it.

- Debug AU/VST3/Standalone built with first-party warnings treated as errors;
  CTest passed **17/17** (16 in the sandbox plus AU registration outside it).
- Strict arm64/macOS 11-targeted Release AU/VST3/Standalone built; CTest passed
  **19/19**, including SF3 and system DLS loading, AU/VST3 smoke, metadata,
  portability, host fixtures, MIDI/soak, performance and render equivalence.
  This confirms local artifact checks, not execution on macOS 11.
- ASan/UBSan passed **4/4**: font repair, engine MIDI/system DLS, render
  equivalence and playback reliability. The dependency closure is not sanitizer
  instrumented, and leak detection is disabled for Darwin arm64.
- The updated standalone leak gate rebuilt Debug, passed its complete **17/17**
  preflight, and reported **0 leaked bytes** in font, engine, VST3 and AU
  harnesses. Each harness also completed with zero test failures.
- Minimum-size editor, settings, reverb and chorus snapshots were generated and
  visually inspected; no clipped/overlapping controls or JUCE assertions appeared.
  Automated accessibility-name, palette-contrast, focus and resize checks pass.

Build/test logs are retained locally. The before-fix playback
reproduction logged three failures for same-bank recall, pending custom reverb
recall and high-rate zero-sample MIDI; these regressions now pass.

## Performance follow-up — 2026-10-02

The subsequent performance pass reduces settled mixer work on Apple Silicon,
caches exact meter decay and unchanged smoothing targets, avoids repeated MIDI
decoding, eliminates
unchanged UI formatting/repaints, clips keyboard drawing to dirty regions, and
avoids full-file repair copies for healthy DLS banks. Path/bookmark recall now
applies the saved pair before resolving/loading, avoiding duplicate bank loads.
Audio quality, voice limits, tails, event ordering and compatibility routes are
retained.

Seven alternating Release before/after rounds cover eleven workloads at four
sample rates and two block sizes. Median paired render CPU reductions across
those eight configurations are **24.8% for one note**, **24.4% for silence**,
**6.6% for sixteen-channel chords**, and **2.8–4.6% for voice-ceiling/effects
workloads**. Dense MIDI and parameter workloads improve by approximately
1.6–3.6%; one dense-MIDI configuration is effectively flat. These are local
offline measurements against the source after the app audit, with shipping
Linear interpolation explicitly selected. Smaller changes can overlap timing
noise and do not guarantee the same gains in a DAW.

The 82-case render suite passed, and its 80-case raw audio/diagnostic snapshot
matched the pre-performance baseline byte for byte. Opt-in UI instrumentation
found no redundant patch/text formatting or signal repaints across 100 unchanged
polls; 387 keyboard clips matched full-render pixels. These UI checks measure work
avoided, not a GUI CPU percentage. A valid padded 128 MiB DLS fixture reduces
peak setup memory by about 128 MiB by eliminating the repair copy; ordinary
small-bank setup time is effectively unchanged.

At the end of this performance pass, AU/VST3/Standalone Debug and strict
Release builds passed **17/17** and **19/19** registered tests; ASan/UBSan passed
**4/4** offline harnesses. Fresh
native font/engine/VST3/AU leak checks each completed with zero failures and zero
leaked bytes. Detailed measurements and reproduction evidence are retained locally.
At that point, real DAW, minimum-OS, Windows and packaged-artifact checks remained
pending; the later macOS candidate checks are recorded below.

## Standalone follow-up — 2026-10-02

The macOS beta.2 source adds format 0/1 MIDI playback with PPQ/SMPTE timing,
current/effective BPM, paired bank/MIDI selection, seeking and section looping.
The transport is Standalone-only; the 131 host parameters and existing AU/VST3
program-change routes remain unchanged.

Final macOS 26.6.2 verification after the combined-loader, tempo and revised
time-field changes passed **19/19** Debug registered checks: 17 through CTest,
the direct player/render harness and AU smoke separately. Strict Release passed
**21/21**: 19 through CTest, the direct player harness with UI snapshots and AU
smoke separately. ASan/UBSan player coverage passed **1/1**, with dependencies
uninstrumented and leak detection disabled. Final minimum-size whole-song and
section-loop snapshots were inspected. Coverage includes
default/format 1/SMPTE BPM, paired selection in either order, corrupt-input
rollback with empty bookmarks, palette-refresh browse callbacks, loop
accessibility and drag/keyboard behavior.

Extracted bundles from the local dirty-worktree validation archive passed strict
signature verification, arm64/macOS 11 metadata and dependency checks. AU and
VST3 DLS smoke tests completed with zero failures. Apple `auval -strict` passed
with the extracted AU installed and its executable hash verified; the previous
AU was then restored and its hash verified.

The extracted beta.2 Standalone app launched natively. A user-driven session
loaded a DLS bank and MIDI file and played a loop: the display showed nine
tracks, nine channels and 116 BPM, with advancing position, output activity and
channel patch updates. Native paired-SF2 multi-selection and pointer interaction
with the revised loop controls remain unverified. These checks cover that macOS
validation candidate, not a clean-system or real-DAW run; see
[standalone playback](STANDALONE.md) for transport approximations and limits.

## Remaining limits

- Run the current binaries in FL Studio and Cubase: both multichannel program
  routes, imported Bank Select/Program Change timing, all-channel playback,
  save/reopen, reset/replay, pedals with mute/solo and editor-open automation.
  Offline wrapper tests do not substitute for these DAW scenarios.
- Native Windows beta.2 build, PowerShell, installer and package checks now pass;
  see the [separate Windows evidence](WINDOWS_RELEASE.md). Windows host and native
  UI scenarios remain pending. Python packaging tests in this Mac audit run on macOS; the
  Unix-only shell orchestration test is excluded on Windows.
- Clean macOS 11 and Windows 10 execution, accessibility with VoiceOver/Narrator,
  native QWERTY delivery/focus behavior and sandboxed-host bookmark recall still
  need interactive validation.
- Widget callbacks now run on the message thread, but state restore still uses
  shared ValueTrees. Concurrent UI reads during restoration need host stress
  validation; deferring callbacks alone does not prove full thread safety.
- FluidSynth's internal buffer can delay audible notes by up to 63 engine
  samples. Higher-rate rendering also has interpolator latency. A strict
  zero-delay/sample-exact audible-onset claim is not supported.
- Repeated same-key overlap and the established single-output design are
  unchanged. Licensed bank/host combinations not supplied here are unverified.
- Keyboard MIDI tracking can still allocate for long SysEx packets.
- Hosted CI remains pending. Extracted macOS candidate checks do not establish
  Windows, real-DAW or minimum-OS execution. macOS beta.2 uses ad-hoc signatures;
  Developer ID signing and notarization remain deferred. The published archive
  must preserve the verified binaries and identify their corresponding source.

See [compatibility](COMPATIBILITY.md), [controller behavior](CONTROLLER_SUPPORT.md)
and [known issues](KNOWN_ISSUES.md) for the retained contracts and limitations.
