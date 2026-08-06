# Troubleshooting

## The plugin is not discovered

- Confirm the format matches the DAW: AU or VST3 on macOS, VST3 on Windows. Cubase does not load AU.
- Put the complete bundle in the user or system plugin directory; do not copy only its inner executable.
- Rescan plugins and check the host's rejected/blacklisted list.
- On macOS, run `codesign --verify --deep --strict --verbose=2` on the installed bundle. For AU, run `auval -v aumu Jsfr Blbs`.
- Confirm the binary architecture matches the DAW with `file`. The currently verified local build is arm64 only.

## Only MIDI channel 1 plays or Program Changes do not follow the song

- Verify the imported/routed MIDI retains channels 1–16 and is not forced to channel 1 by the DAW track or MIDI transform.
- Use one plugin instance and route every source to its input/event bus. Sixteen separate plugin instances do not test the intended workflow.
- Start from before the file's setup events. Starting mid-song depends on the host's Bank Select/Program Change chase behavior.
- In Cubase, ensure the VST3 build is used. Record the Cubase version and whether the instrument exposes channel units/programs.
- In FL Studio, verify the MIDI Out port targets the plugin and each source keeps its intended MIDI channel.
- If FL Studio works but Cubase does not, treat it as a VST3 unit/program-parameter regression and include a minimal project plus MIDI routing screenshots.

## The wrong instrument plays

- Check that the MIDI file and bank belong together.
- Bank Select CC0/32 changes the bank used by a following Program Change; it does not select a complete patch by itself.
- Channel 10 defaults to percussion bank 128. A bank without that convention may fall back differently and should be reported with a legally shareable fixture.
- A manual row selection is a starting value. Later incoming Program Change events intentionally override it.
- GM/GS/XG reset followed by setup events should work in the same block. Include the reset bytes and event order in a bug report if it does not.

## Controllers, pedals, or pitch bend sound wrong

- Confirm the DAW is not filtering, remapping, normalizing, or chasing the controller.
- Pitch bend is 14-bit with center 8192. Bend range is normally selected using RPN 0,0 (CC101/100 followed by Data Entry CC6/38) and is independent per channel.
- The audible effect of pressure and many CCs depends on modulators in the loaded bank even though the MIDI is forwarded.
- The six UI sound controls use CC71, 72, 73, 74, 75, and 79. Value 64 is neutral; they are per channel.
- Compare playback from the beginning with playback from the middle to isolate host chase behavior.

## A bank does not load

- Supported filename types are SF2, SF3, and DLS, but an extension does not guarantee a valid bank.
- Confirm the file exists, is readable, and contains at least one preset.
- A failed replacement leaves the last working bank active. Hover the file control for the load-result message.
- On macOS, reselect a moved file so the saved path/security bookmark can be renewed.
- DLS repair handles only known RIFF-size inconsistencies and uses a temporary copy; it cannot repair arbitrary corrupt sample or instrument data.

## What to include in a Beta report

- JuicySF Rack version and candidate number.
- OS, CPU architecture, DAW name/version, plugin format, sample rate, and block size.
- Bank format and whether it can legally be shared; MIDI file or minimal event list.
- Expected and actual bank/program per affected MIDI channel.
- Exact steps from a fresh plugin instance, including transport position and routing.
- Whether the problem survives project save/reopen and occurs in another host.
- Crash log, `auval`/validator output, or signature/dependency output where relevant.

Do not submit copyrighted game assets or private projects unless you have permission and the feedback channel has an approved retention policy.
