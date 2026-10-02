# Windows beta.2 and UI follow-up — 2026-10-02

The Windows checkout was clean before fetching and fast-forwarding `main` from
`7be3697` to `03bf14c2cd0d72bc368c935e06e13cac17aa37c4`. Work continues on
`codex/windows-beta2-ui-audit`. No pre-existing local edits were discarded.
Commit `92354a6`, `AGENTS.md`, and `docs/audits/2026-10-01/README.md` are absent
from this checkout and the fetched origin branches/tags. The owner's clarification
identifies `03bf14c` and the published beta.2 as the implemented improvement plan.
[The existing audit](../../AUDIT.md) supplies its implementation and validation history.

This run uses Windows 11 x64 build 26200, MSVC 14.44, Windows SDK 10.0.26100,
CMake 3.31.6, pinned JUCE 8.0.14, and the patched static dependency closure.
There is no Mac executor in this session. The macOS results in the earlier audit
are historical evidence for the published Mac binary, not results of this run.
The shared-source fixes below require a fresh Mac build and native verification.

## Confirmed fixes

| Defect | Change | Reproduction and verification |
| --- | --- | --- |
| Folder browse and loop Start/End controls accepted focus without showing it. | Custom painters draw the existing neutral focus-ring token during keyboard use. | A hidden native Windows JUCE peer focuses each control; before/after snapshots differ only when the ring is enabled. All three reproduced before the fix and pass afterward. |
| Arrow-key interaction after a click could leave focus rings hidden. | Editor key listeners observe keyboard use in both Standalone and plugin editors, including settings and controls rebuilt after an accent change. Tab focus also restores the indicator. | Actual JUCE peer key dispatch to the playback-speed control restores focus-ring visibility. Existing transport and text-editing behavior checks pass. |
| The editor's first paint could steal focus from a child control. | Initial focus checks include descendants. | Focus remains on the selected folder or loop-time control across full-editor painting. |
| Rejecting a short, unrelated `.sf2` left the rejected file locked on Windows. | A scoped 12-byte RIFF/form check rejects incomplete or unrelated bank headers before backend loading. Existing parsers still validate the payload. | The failed combined import retains the working bank/MIDI pair, and the rejected file can be deleted immediately. Debug reports zero JUCE assertions in the player harness. |

No UI redesign, new network access, telemetry, remote storage, or host parameter
changes were introduced. The dark palette, twelve accents, sixteen-channel rack,
runtime-only Standalone transport and local file architecture are retained.

## Surface and state inventory

This is the first-party UI inventory, including wrapper-provided surfaces. Code
and automated checks cover the following; the last column explicitly records
native interaction that remains unverified.

| Surface/states | Evidence in this run | Native gaps |
| --- | --- | --- |
| Header and bank picker: empty, loaded, cleared, Unicode/long paths, missing/unreadable/corrupt/unsupported bank, paired bank/MIDI selection, cancellation and duplicate selections | Engine/player tests cover transactional errors, paired selection in either order, path clearing, refreshed browse callbacks and accessibility metadata. Folder focus has a regression check. | Windows/macOS file dialog filtering, multi-select, cancellation, file permissions and screen-reader announcements. |
| Standalone player: empty, MIDI-only, ready, playing, paused, stopped, song end, whole song, section, loop on/off, speed/BPM, seeks, malformed input and import rollback | MIDI formats 0/1, PPQ/SMPTE/tempo, controller/note chase, pause/stop, loop boundaries, keyboard/drag callbacks, editable time values and accessibility interfaces pass. State snapshots include the visible transport variants. | Physical keyboard/mouse delivery, drag/drop and native file chooser; VoiceOver/Narrator announcements during playback. |
| Sixteen-channel rack: loaded/missing/fallback instruments, selected channel, mute/solo, volume/pan/trim, activity and scrolling | Engine tests cover all sixteen named controls and programs, row-arrow navigation, Return-to-instrument selection, bank/patch fallback, mute/solo, MIDI changes, state recall and minimum/default/maximum layout. | Native Tab/Shift-Tab traversal and scrolling in a DAW; screen-reader table ordering, live values and popup behavior. |
| Master and diagnostics: trim, output peak/overload, bank facts, channel expression/sustain/bend/chorus send | Parameter, render, metadata, keyboard reachability, contrast, dynamic diagnostics and state tests pass; snapshots show playing and stopped output. | Pointer/keyboard overload reset and live announcements in actual hosts. |
| Reverb: enable/bypass, profiles, Custom reconciliation and four controls | Profile/custom/recall and accessible keyboard controls pass; enabled/bypassed snapshots inspected. | Native combo popup, pointer editing, screen-reader value announcements and host automation interaction. |
| Chorus: enable/bypass, waveform and four controls | Controller/parameter/effects tests pass; enabled/bypassed snapshots inspected. | Native popup/keyboard/reader behavior and host automation interaction. |
| On-screen piano: held/released notes, selected channel, mouse/QWERTY overlap, multiple pointers, focus loss and teardown | Synthetic audition tests verify velocity, ownership/release ordering, channel change and focus/destruction cleanup without stopping unrelated MIDI. | Actual native QWERTY delivery, pointer capture and assistive technology navigation. |
| Settings callout: twelve accents, interpolation, bend range/scale, CC1 channel/scale/live value, reset policy and build facts | Recall, message-thread refresh, popup palette, all accents, interpolation audio/state and per-channel behavior pass. Settings snapshots use the real accent selector callbacks. | Native opening/closing, Escape, focus return, popup selection, full reader traversal and the wrapper's scaling. |
| Loop time editor and playhead context menu; instrument/effect/settings dropdown menus; tooltips | Source review of named values, Return editing, bounds/commit validation, safe asynchronous callbacks and palette inheritance; player interaction tests pass. | Native menu dismissal, text selection/caret, assistive editing and tooltip timing. |
| JUCE Standalone wrapper: window chrome, Options menu, audio/MIDI-device settings, reset/save/load state and quit dialogs | Compiled against pinned JUCE; first-party processor/editor isolation and local architecture reviewed. | These native wrapper dialogs were not interactively exercised, nor were device changes or physical audio/MIDI outputs. |

## Appearance, sizing and accessibility

The snapshot harness produces **56 PNGs** (28 states at 1× and 2×) under
`build-win/ui-audit/`: empty, MIDI-only, ready/minimum and wider, playing,
paused, stopped, song end, section/minimum and wider, load error, reverb/chorus
enabled and bypassed, settings, and all twelve settings accents. Timers and
queued UI updates settle before capture. Repeated runs replace previous PNGs.
Representative transport, effects and settings images were visually inspected.
No additional clipping or overlapping controls were confirmed; the minimum
width intentionally permits rack scrolling. Vector controls and text render at
both raster densities. These are Windows offscreen renders, not a Retina test.

Existing palette checks pass for every accent: text reaches 4.5:1 and active
accent shapes reach 3:1 on the backgrounds covered by the checks. Accessible
names/roles/value interfaces and keyboard-focus flags pass in the JUCE harness.
Metadata and programmatic focus checks do not establish screen-reader usability.

The product has one deliberate dark palette; it does not offer separate light
and dark themes. Its fonts use fixed logical heights. This run did not change
OS appearance or text-size settings. Native light/dark OS dialogs, high-contrast
mode, text-only enlargement, host zoom, mixed-DPI monitor moves and macOS Retina
rendering remain unverified. Enlarging a PNG is not evidence of text scaling.

## Synthetic-only verification

No system DLS, downloaded sample bank, private corpus, or real MIDI file was
loaded during these checks. The generated corpus contains an SF2, a DLS with
distinct program tones, and a genuine Ogg-compressed SF3. The DLS includes a
band-limited high harmonic to make interpolation comparisons meaningful; the
existing audio pass criteria were preserved. Test names containing `system_dls`
refer to the configured input slot, which was overridden with the synthetic DLS.

Release and Debug static dependencies were rebuilt with the beta.2 timer-thread
patch. Strict Release VST3/Standalone and Debug VST3/Standalone builds pass with
first-party warnings treated as errors and plugin copying disabled. **19/19
CTest checks pass in each configuration**, including portability, font loading,
VST3 multichannel smoke, MIDI/soak/controllers/state, 82-case render equivalence,
playback reliability, package-script contracts and the Standalone MIDI player.

Reproduce the synthetic inputs after initially building the player harness:

```powershell
./build-win/Release/JuicySFMidiFilePlayerTests.exe --write-fixtures "$PWD/build-win/synthetic-fixtures"
./tools/verify_windows.ps1 -SkipDependencies -SkipJuce `
  -FontCorpus "$PWD/build-win/synthetic-fixtures" `
  -Sf3Fixture "$PWD/build-win/synthetic-fixtures/compressed.sf3" `
  -DlsFixture "$PWD/build-win/synthetic-fixtures/general-midi.dls"
./build-win/Release/JuicySFMidiFilePlayerTests.exe --ui-audit "$PWD/build-win/ui-audit"
```

For Debug, also pass `-Configuration Debug -BuildDir "$PWD/build-win-debug"
-DepsPrefix "$PWD/build-win-deps-debug"`. Both fixtures and screenshots are
generated locally and excluded from binary packages. The source includes their
generators. Packaging and installer smoke use the same explicit DLS input.

Logs: `build-win/verification-Release.txt`,
`build-win-debug/verification-Debug.txt`, each build's `Testing/` directory,
and `build-win-logs/ui-audit.log`. Before-fix focus/import evidence is retained
in `build-win-logs/ui-keyboard-before.log`.

`pnpm check` was attempted and returned `ERR_PNPM_NO_IMPORTER_MANIFEST_FOUND`:
this C++/JUCE repository has no `package.json`. A macOS build cannot run on this
Windows host and was not claimed. AU, Apple strict validation, Mac sanitizers
and native VoiceOver/Retina checks must run on a Mac against these changes.

## Release gate and remaining work

Before Windows assets are published, run portable/source archive creation,
extraction/checksums, extracted VST3 smoke and isolated installer
install/upgrade/uninstall with the synthetic DLS. Preserve the existing macOS
assets and identify the Windows source commit separately; the release tag still
identifies the already published macOS build.

Real FL Studio/Cubase scenarios, clean Windows 10 1607, Windows Narrator, native
input/dialog/appearance/text/DPI checks, and the Mac checks above remain open.
Current Windows native build results do not complete the requested macOS UI
audit. See [known issues](../../KNOWN_ISSUES.md) and
[Windows validation](../../WINDOWS_RELEASE.md).
