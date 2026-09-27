# Known issues

Please read this before reporting a bug. Some of these are deliberate.

## Not yet tested

- **Other hosts.** I test in FL Studio and Cubase. Logic and everything else are
  untested.
- **macOS 11.** The builds target macOS 11, but I've only run them on current
  macOS.
- **Windows 1.0.0-beta.1.** The native pipeline has [automated evidence](WINDOWS_RELEASE.md)
  for 0.6.1-beta.4. The merged version still needs Windows build/artifact checks,
  DAW playback and save/reopen, and clean Windows 10 minimum-OS testing.
- **Screen readers.** Everything has accessible names, but I haven't tried
  VoiceOver or Narrator.

## Sound

- **Repeated notes can overlap.** When the same key is played again, Fruity LSD
  cuts the previous note but Juicy16 lets it fade underneath. Fast repeated chip
  notes can sound a little phasey.
- **Juicy16 is quieter than VGMTrans**, by about 8.6 dB. That's on purpose:
  VGMTrans gets its extra level by clipping. Turn up the master trim (up to +12 dB)
  if you want more.
- **On DLS drum kits, pan moves the whole kit.** CC10=0 puts every drum hard left
  instead of shifting the kit's stereo image.
- **Reverb and chorus are off by default.** Once you turn them on, the file's
  CC91 and CC93 decide how much of each channel goes in, and GS/XG reverb SysEx
  can't change your settings.
- **CC71–79 do nothing**, same as in stock FluidSynth.
- How pressure and some controllers sound depends on the bank's own modulators.

## Pitch bend

- **FL Studio squashes imported bends** to ±2 semitones. Use *Bend scale* in
  settings (×6 for a rip written for 12 semitones) or *Bend range* to force one
  range. I haven't confirmed which works best in FL yet.
- **A reset keeps the file's bend range.** If you play a 12-semitone rip and then
  a file that relies on the default ±2 in the same instance, the second file
  bends 12. Reload the plugin or use the bend range override.

## Levels

If a channel sounds too loud or too quiet, check its volume knob. It shows the
last CC7 the file sent. If the knob doesn't match the file, that's a bug. If it
does, the host may be changing the level: Cubase's track Volume/Pan and its
"Extract first volume/pan/patch" import options are the usual culprits.

## Limits

- One stereo output for all 16 channels.
- Up to 96 kHz natively. Above that it renders at a lower rate and upsamples.
  Below 8 kHz it stays silent rather than playing out of tune.
- Note timing can be up to 63 samples late because of FluidSynth's internal
  buffering.
- DLS repair only fixes bad size headers, and only on files up to 512 MB.
- Selecting a bank/program the loaded bank doesn't have shows your choice but
  plays a substitute until you load the right bank.
- On a drum channel the bank number can read up to 255 (FluidSynth adds 128 to
  Bank Select). That's expected.
- The macOS builds are ad-hoc signed, so you have to clear quarantine when
  installing.
- Not supported: Intel Macs, VST2, AUv3, Linux, 32-bit or ARM Windows.

## Leaks

The leak check still reports two tiny leaks: 32 bytes inside FluidSynth when a
bank unloads, and 192 bytes in JUCE's AU parameter setup. They're harmless but
not fixed yet.

## Licensing

JUCE 8 is used under AGPLv3 and the rest is GPLv3. I reviewed the licensing
myself rather than getting a legal review. A known libsndfile vulnerability
(CVE-2025-52194) is patched in the bundled copy.
