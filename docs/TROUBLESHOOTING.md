# Troubleshooting

## The plugin is not discovered

- Confirm the format matches the DAW: AU or VST3 on macOS, VST3 on Windows. Cubase does not load AU.
- Put the complete bundle in the user or system plugin directory; do not copy only its inner executable.
- Rescan plugins and check the host's rejected/blacklisted list.
- On macOS, run `codesign --verify --deep --strict --verbose=2` on the installed bundle. For AU, run `auval -v aumu Jc16 Pkst`.
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
- Compare the expected engine semantics with [CONTROLLER_SUPPORT.md](CONTROLLER_SUPPORT.md); exact delivery does not make every controller audible in every bank.
- Pitch bend is 14-bit with center 8192. Bend range is normally selected using RPN 0,0 (CC101/100 followed by Data Entry CC6/38) and is independent per channel.
- The audible effect of pressure and many CCs depends on modulators in the loaded bank even though the MIDI is forwarded.
- CC71-79 are forwarded to the engine but drive nothing: Juicy16's own modulators for them were removed in 0.5.1-alpha.5 because no other SoundFont player applies them. The per-channel controls the UI exposes are CC7 volume and CC10 pan.
- Compare playback from the beginning with playback from the middle to isolate host chase behavior.

## A bank does not load

- Supported filename types are SF2, SF3, and DLS, but an extension does not guarantee a valid bank.
- Confirm the file exists, is readable, and contains at least one preset.
- A failed replacement leaves the last working bank active. Hover the file control for the load-result message.
- On macOS, reselect a moved file so the saved path/security bookmark can be renewed.
- DLS repair handles only the RIFF-size cases defined in [TROUBLESHOOTING.md](TROUBLESHOOTING.md) and uses a temporary copy; it cannot repair arbitrary corrupt sample or instrument data.

## The plugin loads but produces no audio

- Confirm the host sample rate is 96 kHz or lower. FluidSynth 2.5.5 cannot render above 96 kHz, so Juicy16 intentionally mutes at higher rates rather than producing incorrectly pitched audio. The unsupported rate is written to the host/plugin log.
- Confirm the bank loaded successfully, the MIDI reaches the intended channel, and the channel has a valid program in that bank.
- Try a note at 44.1 or 48 kHz in a fresh instance before investigating host-specific routing.

## What to include in a Beta report

- Juicy16 version and candidate number.
- OS, CPU architecture, DAW name/version, plugin format, sample rate, and block size.
- Bank format and whether it can legally be shared; MIDI file or minimal event list.
- Expected and actual bank/program per affected MIDI channel.
- Exact steps from a fresh plugin instance, including transport position and routing.
- Whether the problem survives project save/reopen and occurs in another host.
- Crash log, `auval`/validator output, or signature/dependency output where relevant.

Do not submit copyrighted game assets or private projects unless you have permission and the feedback channel has an approved retention policy.

## Repairing a malformed DLS

Juicy16 does not edit a selected DLS file. When a file has a `RIFF`/`DLS ` header, the plugin may create a private operating-system temporary copy, apply the narrowly defined size corrections below, and ask FluidSynth to validate that copy. The original path and bytes remain untouched.

## Corrections Juicy16 may make

The repair routine changes only 32-bit little-endian RIFF size fields:

1. If the outer RIFF size claims bytes beyond the physical end of the file, it is clamped to the available byte count.
2. While walking complete top-level chunks, if the next claimed chunk runs past the physical end and there is a preceding complete chunk, that preceding chunk may be grown to absorb the remaining bytes. This targets the observed Awave-style undersized-chunk/phantom-chunk pattern.

RIFF word alignment is respected. A well-formed DLS is not changed, a second repair pass is a no-op, and any non-DLS RIFF—including SF2—is byte-identical.

## What it does not repair

Juicy16 does not reconstruct or guess:

- missing/truncated sample or instrument data;
- invalid nested chunk layouts, pool tables, wave links, regions, or articulation data;
- arbitrary corrupt chunk identifiers or sizes;
- a first corrupt chunk when no preceding complete chunk provides a safe repair target;
- unsupported encodings or files that merely use a `.dls` extension;
- an empty bank with no playable preset.

A repaired copy is not considered successful merely because its size fields changed. FluidSynth must load it and enumerate at least one preset. If validation still fails, Juicy16 deletes the candidate copy, reports the failure, and preserves the previously active bank and saved successful path.

## Size boundary

Repair rewrites an in-memory image of the whole file, and the file is chosen by the user, so the size is bounded before any of it is read. Banks larger than 512 MB are never repaired; they are handed to FluidSynth unchanged, which streams rather than buffers them.

That leaves a gap the repair pass would otherwise have absorbed, so an unrepaired bank is also checked for a RIFF header whose declared payload extends past the end of the file. Such a container cannot be valid and is rejected immediately with a specific message. Without that check, FluidSynth's parser can spend over a minute scanning a large malformed image while the message thread waits — measured at roughly 72 seconds for an 805 MB file declaring a 4 GB payload. A well-formed bank passes this check at any size, so legitimate large SoundFonts are unaffected.

## Temporary-file lifecycle

The repair candidate uses JUCE's unique temporary-file facility in the operating-system temporary directory. A successful repaired copy remains private to the plugin instance while it is the active bank and is removed when replaced or when the model is destroyed. Failed candidates are removed immediately. These files are not included in packages, diagnostics, or telemetry.

## Test evidence and reporting

`font_repair_unit` covers empty/truncated buffers, outer and inner size inconsistencies, odd-byte padding, unsafe first-chunk refusal, non-DLS identity, idempotence, and 6,000 deterministic malformed/non-DLS property cases. The engine test also proves failed corrupt, unsupported, unreadable, and zero-instrument replacements do not replace audible or serialized state, and separately covers zero-byte, truncated, oversized, read-only, and concurrently removed banks. A private Awave-style DLS currently passes after temporary repair on arm64 macOS; that file is not redistributable evidence and Windows validation remains open.

If a DLS still fails, keep the original file, report whether Juicy16 said repair was attempted, and include only a file you have permission to share. Do not rewrite the source file in place.
