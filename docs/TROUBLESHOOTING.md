# Troubleshooting

## The plugin doesn't show up

- Use the right format: AU or VST3 on macOS, VST3 on Windows. Cubase doesn't
  load AU.
- Copy the whole `.component` or `.vst3` bundle, not just the file inside it.
- On macOS, clear quarantine (see the [tester guide](BETA_TESTER_GUIDE.md)),
  then quit the DAW completely and rescan.
- Check your host's list of rejected or blacklisted plug-ins.
- `auval -v aumu Jc16 Pkst` tests the AU on macOS.

## Only channel 1 plays, or instruments don't change

- Make sure your DAW keeps the MIDI on channels 1–16 and isn't forcing
  everything onto channel 1.
- Use one Juicy16 instance for all 16 channels.
- Play from the start of the song, so the file's Bank Select and Program Change
  messages arrive.
- In Cubase, use the VST3 version.
- In FL Studio, check each MIDI Out targets Juicy16 on the right channel.

## The wrong instrument plays

- Check the MIDI file and the bank belong together.
- Bank Select only picks the bank; the next Program Change picks the instrument.
- Picking an instrument by hand is only a starting point. The file's next
  Program Change on that channel replaces it.
- Channel 10 looks for a drum kit first. If the bank has no flagged kit, Juicy16
  uses the same program from the normal bank.

## Controllers or bends sound wrong

- Make sure the DAW isn't filtering or remapping controllers.
- In FL Studio, imported bends are squashed to ±2 semitones. Use *Bend scale* or
  *Bend range* in settings.
- Some controllers only do something if the bank's own modulators use them. See
  [controller behaviour](CONTROLLER_SUPPORT.md).
- If it's fine from the start but wrong when you start mid-song, it's the host's
  chase behaviour.

## A bank won't load

- The file must be a real `.sf2`, `.sf3` or `.dls` with at least one instrument.
- A failed load keeps your previous bank playing. Hover over the file field for
  the error.
- On macOS, if you moved the bank, select it again.

### Broken DLS files

Some DLS exporters (Awave Studio, for one) write wrong size headers. Juicy16
fixes those in a temporary copy and loads that; your original file is never
changed. It only fixes the outer RIFF size and an undersized chunk before the
end of the file. It can't fix missing or corrupt sample data, and it doesn't
repair files over 512 MB. A file whose header claims more data than it holds is
rejected straight away rather than stalling the DAW.

## No sound at all

- Check the bank loaded and the MIDI reaches the right channel.
- Check the channel isn't muted, or silenced by a solo on another channel.
- Try a fresh instance at 44.1 or 48 kHz.

## Reporting a problem

See [reporting bugs](BETA_TESTER_GUIDE.md#reporting-bugs) in the tester guide.
