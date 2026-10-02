# Roadmap

## Now: 1.0.0-beta.2 on macOS

This macOS beta adds a Standalone MIDI player alongside AU and VST3. Load a
bank and MIDI together, seek through the song, see its current BPM and repeat
the whole song or a section. It also includes the recall, playback, leak and
performance fixes described in the [changelog](CHANGELOG.md).

Windows remains on 1.0.0-beta.1 while these changes are rebuilt and tested.

## Next

- Rebuild the latest fixes on Windows and complete host testing. The
  [Windows evidence](docs/WINDOWS_RELEASE.md) records local 1.0.0-beta.1
  build, package and installed-plugin checks; itemized DAW and clean Windows 10
  checks remain pending.
- Test in FL Studio and Cubase, and compare side by side with Fruity LSD
- Cut repeated same-key notes the way LSD does
- Verify the existing leak and portability fixes in hosted CI. Local checks
  pass; current real-host and minimum-OS checks remain open.
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
