# Changelog

## 1.0.0-beta.2 — 2026-10-02 (macOS)

- Added Standalone MIDI loading and drag/drop, play/pause, stop, seek, elapsed
  and total time, playback speed, whole-song loops and A/B section markers.
- Reused the existing sixteen-channel rack for automatic instrument changes,
  live activity and mute/solo. The transport follows the existing UI style.
- The header folder can load a bank and MIDI together. Added a clean seek
  timeline, draggable section-loop boundaries and current/effective BPM.
- Restore controllers and held/pedal notes on seek and resume; release notes
  on pause/stop and retain the current song if a new import fails.
- MIDI formats 0/1, tempo maps and SMPTE timing; bounded, strict file validation.
  AU/VST3 parameter/state identities and host program routes are unchanged.

Standalone is included with the macOS AU/VST3 package. Controls and current
limits are in [standalone playback](docs/STANDALONE.md). Windows remains on
1.0.0-beta.1 until rebuilt and tested.

### Audit and performance fixes

- Fixed same-bank project recall for all sixteen channels, custom reverb recall,
  and older projects inheriting mixer, mute/solo or reverb settings.
- Fixed pedal-held notes continuing after mute/solo exclusion, SysEx reset
  recognition, offset-bank drum fallback, and MIDI in empty high-rate blocks.
- Fixed high-rate event carry and silent tails in ordinary uneven audio blocks.
- Fixed audition velocity, shared mouse/QWERTY holds, focus/keyboard cleanup,
  worker-thread recall updates, and accent updates throughout the
  editor. Accent choices now survive saved projects (state schema 11; the 131
  parameter identities are unchanged).
- Removed reverb callback string allocation and a profile-application data race;
  later knob edits or profile choices now supersede earlier pending changes.
- Reject corrupt normalized state values and unsafe host sample rates safely.
- Strengthened fresh-build, leak, sanitizer, strict AU and source-package checks;
  added regression coverage for gate failures and Windows archive integrity.
- Reduced Apple Silicon mixer work, cached repeated meter calculations and
  unchanged UI text, and limited keyboard drawing to dirty regions. Audio
  quality settings, all sixteen channels and effect processing are preserved.
- Healthy DLS banks avoid a full-file repair copy. Saved bank paths and bookmarks
  restore together with one load; failed selections retain their fallback.

Current DAW, Windows and minimum-OS validation remain pending; see the
[audit report](docs/AUDIT.md) for evidence and limitations.

## 1.0.0-beta.1 — 2026-09-27

This update brings GBA-style DLS playback closer to Fruity LSD.

- **Interpolation setting:** choose Linear (the default for new instances),
  7th-order or None under Settings → Sound. Older projects keep 7th-order.
- **Balance CC8/CC40 is ignored**, matching LSD. It previously fought pan and
  could nearly mute one side.
- **Full-range pan on stereo DLS banks.** Left/right sample pairs now reach both
  edges. SF2 behavior is unchanged.
- **Channel 10 fallback:** Bank Select no longer silences a kit that isn't
  flagged as percussion; the same program in the melodic bank is used instead.
- Shorter documentation and source comments.
- Integrated the native Windows build, installer, portable/source packaging,
  compiler fixes and automated gates. Its recorded 0.6.1-beta.4 results predate
  this update; fresh Windows validation is pending.

The interpolation setting is saved with projects and survives MIDI resets.
State schema is now 10 with 131 parameters; existing parameter identities stay
unchanged. Rebuild the patched FluidSynth dependency when building from source.

Current DAW playback/save-reopen checks and a side-by-side LSD comparison remain
open. Small known leaks and FluidSynth's internal timing delay remain; see
[known issues](docs/KNOWN_ISSUES.md). Windows evidence and remaining checks are
in [WINDOWS_RELEASE.md](docs/WINDOWS_RELEASE.md).

## 0.6.1-beta.4 BC2 — 2026-09-12

- Reduced redundant mixing, controller bookkeeping and rack repaints.
- Dense-automation benchmarks used 36–45% less processing time on Apple Silicon;
  ordinary playback gains were smaller. Audio and diagnostics matched across
  80 comparison scenarios.
- Added render/buffer regression coverage. Strict Release passed 17/17 tests,
  and extracted AU/VST3 smoke tests and strict AU validation passed.

## 0.6.1-beta.4 — 2026-09-06

- Updated FluidSynth to 2.5.7 with a patched per-channel CC1 vibrato-strength
  control (×1–×24).
- Added optional global chorus, following MIDI CC93, and independent channel
  audio trims, including effects contributions.
- Added DAW recovery and Standard MIDI reset policies; saved applied programs,
  volume, pan, expression and bend ranges without waiting for the editor.
- Refreshed the 16-channel rack, effect readouts, MIDI/audio meters, fallback
  instrument diagnostics and master overload indication.
- Improved static dependency builds, CI configuration and packaged source patches.
- State schema advanced to 9. Older saves receive defaults for added controls;
  older plugin versions cannot read newer state schemas.

## 0.6.1-beta.3 — unreleased

- Fixed interpolation and master-trim defaults not reaching the synth.
- Fixed reset/replay expression and pitch-bend-range handling.
- Added bend-range override and bend scaling for host-imported MIDI.
- Bounded same-timestamp event groups to keep processing predictable.

## 0.6.0-beta.1

- First macOS beta with AU/VST3, an ad-hoc signed package and a double-click
  installer. Per-channel program selection was tested in FL Studio and Cubase;
  those historical results do not validate later builds.
- Added twelve accent colors and improved settings, focus rings and layout.
- Fixed RPN handling, static linkage, documentation checks and package notices.
- Kept reverb off by default and set master trim to +1.5 dB.

## Earlier development

The 0.5.1 and 0.6.0 alpha series established the 16-channel rack, automatic
Bank Select/Program Change, state recall, controller handling, DLS size-header
repair, optional reverb, keyboard navigation and automated regression coverage.
They also introduced the pinned dependency recipes and patched JUCE VST3 wrapper
for multichannel program selection. VST2, Intel macOS and Linux stayed outside
the release scope.

Detailed historical changes remain in Git history. See
[state compatibility](docs/COMPATIBILITY.md) for saved-project guarantees and
[controller support](docs/CONTROLLER_SUPPORT.md) for MIDI behavior.
