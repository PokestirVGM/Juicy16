# Beta 1 tester guide

## Required warning

JuicySF Rack Beta 1 is pre-release audio software. Use copies of important DAW projects and banks, save incremental project versions, and do not depend on Beta state for irreplaceable work. A Beta candidate may be withdrawn for crashes, state corruption, invalid signatures/dependencies, licensing problems, or incorrect multichannel MIDI behavior.

Do not download a candidate unless its release notes list your operating system, CPU architecture, DAW, and plugin format as tested. “Cross-platform” does not mean every OS/host combination is supported.

## Intended tester audience

The first canary should be experienced macOS AU/VST3 and Windows VST3 users who can preserve 16-channel MIDI routing, identify Bank Select/Program Change events, restore a backup, and submit a minimal reproduction. The group should include FL Studio, Cubase, Logic, one additional AU host, and one additional VST3 host, plus users of SF2, SF3, conventional DLS, and known malformed DLS exports.

The tester count, invitation channel, support hours, feedback destination, and withdrawal owner require owner approval before launch.

## Supported workflow

1. Back up the project and any previously installed JuicySF Rack plugin.
2. Verify the archive checksum and candidate number.
3. Install the correct AU/VST3 bundle for the listed platform/architecture.
4. Start with a new empty project and confirm plugin discovery.
5. Load a legally obtained DLS/SF2/SF3 bank.
6. Route one original 16-channel MIDI file to a single instance without manual patch assignments.
7. Record initial and mid-song instruments, controllers, bends, and channel 10 percussion.
8. Repeat after rewind, loop, stop/start, save/reopen, and beginning playback mid-song.
9. Only then test a copy of an existing project.

## Support boundary

Beta 1 is intended to cover only the exact OS versions, architectures, formats, and DAWs named in the candidate release notes. One stereo mix output is intentional. VST2, AUv3, Linux, 32-bit Windows, and unlisted architectures are unsupported unless a later approved matrix says otherwise. Bank-specific modulators can change how pressure and controllers sound even when messages are delivered correctly.

## Uninstall and rollback

Close all DAWs before replacing or removing a plugin.

On macOS, remove only the JuicySF Rack bundles installed for the Beta from:

- `~/Library/Audio/Plug-Ins/Components/` (AU)
- `~/Library/Audio/Plug-Ins/VST3/` (VST3)

On Windows, remove only `JuicySF Rack.vst3` from the VST3 directory used during installation. Rescan the DAW afterward. Restore the prior backed-up bundle if rollback is required; never overwrite the backup. Projects saved with Beta-specific state may not work in an older build, so restore the corresponding project backup too.

Beta 2 and the first stable release are expected to read Beta 1 schema-v2 state. Older betas deliberately reject newer schemas rather than guessing at their meaning. See [STATE_COMPATIBILITY.md](STATE_COMPATIBILITY.md) for the versioning and identifier policy.

No uninstaller should delete user DLS/SF2/SF3 files or DAW projects. Temporary repaired DLS copies are stored in the operating system temporary area and normally removed with the plugin instance.

## Reporting

Use the Beta issue form and include version/candidate, OS/architecture, DAW/version, format, sample rate/block size, bank type, MIDI routing, affected channel, exact reproduction, expected/actual behavior, fresh-project result, and logs/screenshots where safe. Do not upload copyrighted game assets, private projects, personal paths, or confidential banks without authorization and an approved retention policy.

Channel-1-only Cubase behavior, missed later Program Changes, state corruption, invalid signatures, missing dependencies, crashes, and severe audio corruption are priority regressions. Stop using the candidate on important projects until triaged.
