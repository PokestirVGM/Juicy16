# Standalone MIDI playback

The macOS 1.0.0-beta.2 package includes a Standalone app that plays MIDI files
through the same sixteen-channel rack as the plugins. The macOS installer puts
it at `~/Applications/Juicy16.app`; see the [tester guide](BETA_TESTER_GUIDE.md)
for installation and quarantine handling. The Windows beta.1 Standalone remains
a development/QA build and does not include this player.

1. Click the folder in the header. Select one `.dls`, `.sf2`, or `.sf3` bank
   and one `.mid` / `.midi` file together, or select either file on its own.
2. You can also click **Load MIDI**, or drop one MIDI file onto the window.
3. Click **Play**, or press Space. The rack shows MIDI activity, audio levels,
   and each channel's automatic instrument changes.

**Pause** keeps the position and releases notes and pedals. **Play** resumes
there. **Stop** releases notes and returns to the beginning. Drag or click the
timeline to seek; elapsed and total time appear beside it. The speed selector
changes timing without changing pitch. Mute and solo use the existing rack
controls; MIDI still controls instruments, volume, pan and other controllers.
The timeline shows time ticks, progress and a playhead. Dragging previews the
new position; releasing the mouse applies the seek. The BPM display follows
the file's current tempo, including tempo changes. At other playback speeds,
it shows the effective BPM and the original tempo in parentheses. Files
without tempo events use the Standard MIDI default of 120 BPM; SMPTE tempo
metadata is displayed while playback keeps its frame-based timing.

Turn on **Loop** to repeat the song. Choose **Section** to expose draggable
**Start** and **End** handles on the timeline and editable time fields. Click a
field to enter `mm:ss` (with optional fractional seconds) or plain seconds;
Enter or leaving the field commits the value. Right-click a field to set its
boundary to the current playhead position. These controls appear only in
**Section** mode. Arrow keys adjust a focused handle
by 0.1 seconds; Shift adjusts it by one second. **Whole song** or **Reset**
restores the complete range. Sections need at least 0.05 seconds. Files
shorter than that can play once.
Space controls the transport in Standalone; text fields and the settings
popover retain their normal keyboard behavior.

Seeking and loop restarts restore the preceding bank/program selection,
controllers, pitch bend, pressure, RPN/NRPN writes and supported resets.
Notes held by keys, sustain or sostenuto are retriggered. Their envelopes start
again; seeking does not reproduce the earlier audio, effects tail, or original
patch of a note held across a later program change. Release-only voices are
not chased. Reopening the editor keeps the transport; quitting the app clears
the selected MIDI and transport settings. Audio-device reconfiguration pauses
playback, ready to resume at its current position.

The reader supports Standard MIDI formats 0 and 1, PPQ tempo maps and SMPTE
timing (including 29.97 fps). It preserves event order at equal timestamps.
Format 2, escaped or split SysEx, incomplete tracks, files over 16 MiB,
more than 250,000 events, and durations over 24 hours are rejected with an
inline error. The combined folder selection accepts at most one bank and one
MIDI; invalid MIDI is checked before replacing the bank, and a failed bank
load retains the preceding bank and MIDI selection. Individual SysEx packets
must fit the MIDI buffer's 65,535-byte
message limit. Failed imports retain the current song. Very complex A/B setup
histories over 8,192 chased events retain the previous valid loop range.

File playback uses Standard MIDI ordering and reset semantics while active in
Standalone, starting in Juicy16's default GS bank mode. A file's own GM/GS/XG
reset can change that mode. AU and VST3 have no file transport and keep their
existing DAW recovery policy, parameter identities and program-change routes.
FluidSynth's existing internal rendering quantum and interpolation delay still
apply; scheduling events at their sample offsets does not remove those delays.
