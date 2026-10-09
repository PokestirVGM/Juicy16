# Juicy16

Juicy16 is a 16-channel DLS and SoundFont player. I made it to play game-rip and
GBA-style MIDI the way Fruity LSD does: load one `.dls`, `.sf2` or `.sf3` bank,
send a multichannel MIDI file to one instance, and every channel picks its own
instrument from the file's Bank Select and Program Change messages. Everything
mixes to one stereo output.

## Download

The latest macOS release is [1.0.0-beta.3](https://github.com/PokestirVGM/Juicy16/releases/tag/v1.0.0-beta.3).
Windows remains on [1.0.0-beta.2](https://github.com/PokestirVGM/Juicy16/releases/tag/v1.0.0-beta.2).

- **Apple Silicon macOS:** Standalone, AU and VST3. The build targets macOS 11;
  current checks run on macOS 26.6.2. Unzip and run `install_macos.command`.
  Open Juicy16 from your Applications folder, or rescan plug-ins in your DAW.
- **Windows 10+ x64:** VST3 and Standalone MIDI player.
  Run the Setup EXE, or extract the portable ZIP and
  copy the complete `Juicy16.vst3` folder into your DAW's VST3 folder, then rescan.
  Unsigned; includes matching source and checksums. [Windows validation](docs/WINDOWS_RELEASE.md).

Nothing else needs installing. The macOS builds are ad-hoc signed, so macOS will block
them until you clear quarantine; the [beta tester guide](docs/BETA_TESTER_GUIDE.md)
shows how.

## What's new in 1.0.0-beta.3

- **CC1 vibrato strength** applies to all sixteen channels and reaches ×64.
- **CC1 vibrato rate:** Bank, ×1.5, ×2, ×2.4, ×3 or ×4; retained through MIDI resets.
- **Saved projects:** matching older per-channel strengths migrate to the global
  control; independent per-channel values remain automatable. Schema 12 saves
  cannot be opened by older builds, so keep a backup before upgrading.

## From 1.0.0-beta.2

- **Standalone MIDI playback:** select a sound bank and MIDI together, then
  play, pause, seek and change speed. The timeline shows elapsed/total time
  and current BPM. Loop the whole song or drag a section's Start/End handles.
- **The same sixteen-channel rack:** automatic instruments, live activity,
  mute/solo and the existing controls. [Standalone playback](docs/STANDALONE.md).
- **Recall and playback fixes:** saved channel settings, pedal-held mute,
  high-rate rendering, accent persistence and timer-thread cleanup.
- **Less rendering and UI work:** audio settings, voices and effect tails
  stay intact. Offline measurements are in the [audit](docs/AUDIT.md).

Current FL Studio/Cubase, minimum-OS and native accessibility checks remain pending;
see [known issues](docs/KNOWN_ISSUES.md).

## From 1.0.0-beta.1

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
  hosts that mangle bends, CC1 vibrato strength and rate, reset behaviour and accent colour.

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
