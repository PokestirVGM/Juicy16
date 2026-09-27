# Roadmap

## Now: 1.0.0-beta.1

This release brings GBA-style DLS banks closer to Fruity LSD: an interpolation
setting (Linear by default), balance ignored, full-range pan on stereo DLS banks,
and channel 10 no longer going silent without a flagged drum kit. It's out for
macOS; the Windows VST3 follows on the same release page.

## Next

- Rebuild and validate Windows VST3 for 1.0.0-beta.1, then complete host testing.
  The native build and packaging pipeline is implemented; [Windows evidence](docs/WINDOWS_RELEASE.md)
  covers the earlier 0.6.1-beta.4 candidate, not this merged version.
- Test in FL Studio and Cubase, and compare side by side with Fruity LSD
- Cut repeated same-key notes the way LSD does
- Fix the two small leaks the leak check reports
- Get hosted CI green

## Later

- A limiter, so Juicy16 can match VGMTrans's loudness without clipping
- Per-channel reverb sends
- Developer ID signing and notarization
- Dropping the unused WebKit link on macOS

## Not planned

Intel Macs, VST2, AUv3, Linux, and separate per-channel outputs.

## Decisions

| Decision | Why |
| --- | --- |
| Apple Silicon only on macOS | It's the hardware I can test on. |
| Ad-hoc signing for now | Notarization needs a paid account; the tester guide covers the workaround. |
| Reverb and chorus off by default | Turning them on would change existing projects without asking. |
| Mute beats solo | Otherwise muting the only soloed channel would do nothing. |
| GS/XG reverb SysEx ignored | The reverb is a plugin setting, not something a file should reprogram. |
| JUCE under AGPLv3 | No commercial JUCE licence. See [licensing](docs/LICENSING.md). |

Frozen IDs, ranges and state versions are in [compatibility](docs/COMPATIBILITY.md).
