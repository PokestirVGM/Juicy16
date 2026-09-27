# Juicy16

Juicy16 is a 16-channel DLS and SoundFont player. I made it to play game-rip and
GBA-style MIDI the way Fruity LSD does: load one `.dls`, `.sf2` or `.sf3` bank,
send a multichannel MIDI file to one instance, and every channel picks its own
instrument from the file's Bank Select and Program Change messages. Everything
mixes to one stereo output.

## Download

The latest release is [1.0.0-beta.1](https://github.com/PokestirVGM/Juicy16/releases/tag/v1.0.0-beta.1).

- **macOS 11+ on Apple Silicon:** AU and VST3. Unzip, double-click
  `install_macos.command`, then rescan plug-ins in your DAW.
- **Windows 10+ x64:** VST3. It's being tested now and will be added to the same
  release page when it's ready. The [native Windows pipeline](building.win32.md)
  includes portable, installer and source packages; [recorded Windows tests](docs/WINDOWS_RELEASE.md)
  cover the earlier 0.6.1-beta.4 candidate, with a fresh 1.0 build and DAW tests pending.

Nothing else needs installing. The builds are ad-hoc signed, so macOS will block
them until you clear quarantine; the [beta tester guide](docs/BETA_TESTER_GUIDE.md)
shows how.

## What's new in 1.0.0-beta.1

- **Interpolation setting** (Settings → Sound): Linear by default, which is what
  Fruity LSD uses, plus 7th-order and None. Linear keeps the bright grit of
  low-rate GBA samples.
- **Balance (CC8) is ignored**, like LSD. It used to mute one side and fight pan.
- **Pan works fully on stereo DLS banks.** Banks built from left/right sample
  pairs used to barely move.
- **Channel 10 no longer goes silent** after Bank Select on banks whose drum kit
  isn't flagged as drums.

The full list is in the [changelog](CHANGELOG.md).

## Using it

1. Add one Juicy16 instance.
2. Load the bank that goes with your MIDI file.
3. Route MIDI channels 1–16 to it without merging them onto one channel.
4. Play from the start so the file's setup messages arrive.

Incoming MIDI always wins: a Program Change, CC7 or CC10 replaces whatever you
picked by hand, at the moment it happens. If a channel sounds wrong in a DAW,
[troubleshooting](docs/TROUBLESHOOTING.md) covers the usual routing problems.

## The interface

- **Rack:** one row per MIDI channel with mute, solo, instrument, volume, pan and
  trim. Every control is a real parameter, so all 16 channels can be automated.
- **Right panel:** master trim, reverb and chorus, the loaded bank and details
  for the selected channel.
- **Keyboard:** plays the selected channel and lights up for incoming notes.
- **Settings** (click the Juicy16 logo): interpolation, pitch-bend fixes for
  hosts that mangle bends, CC1 vibrato strength, reset behaviour and accent colour.

Mute and solo belong to the plugin, so nothing in a MIDI file changes them.

## Good to know

- One stereo output; no separate per-channel outputs.
- Sample rates from 8 to 192 kHz. Above 96 kHz it renders at a lower rate and
  upsamples.
- Some DLS files have broken size headers. Juicy16 repairs a temporary copy and
  never touches your file.
- I test in FL Studio and Cubase. Other hosts should work but I haven't checked
  them.

The rest is in [known issues](docs/KNOWN_ISSUES.md) and the [roadmap](ROADMAP.md).

## More docs

- [MIDI and controller behaviour](docs/CONTROLLER_SUPPORT.md)
- [Project and automation compatibility](docs/COMPATIBILITY.md)
- [How it works](docs/ARCHITECTURE.md)
- Building: [macOS](building.macos.md) · [Windows](building.win32.md)

To run the full test gate on macOS: `tools/ci_gates.sh all`.

## Privacy and licences

Juicy16 doesn't connect to the internet or collect anything ([PRIVACY.txt](PRIVACY.txt)).
It's GPLv3, built on JUCE 8 (AGPLv3) and FluidSynth (LGPL). See
[LICENSE.txt](LICENSE.txt), [NOTICE.md](NOTICE.md) and [licensing](docs/LICENSING.md).

Juicy16 started from Birchlabs' JuicySF plugin. It's inspired by Fruity LSD but
isn't affiliated with it or an exact emulation.
