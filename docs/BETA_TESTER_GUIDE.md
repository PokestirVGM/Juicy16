# Beta 1 tester guide

## Required warning

Juicy16 Beta 1 is pre-release audio software. Use copies of important DAW projects and banks, save incremental project versions, and do not depend on Beta state for irreplaceable work. A Beta candidate may be withdrawn for crashes, state corruption, invalid signatures/dependencies, licensing problems, or incorrect multichannel MIDI behavior.

Do not download a candidate unless its release notes list your operating system, CPU architecture, DAW, and plugin format as tested. “Cross-platform” does not mean every OS/host combination is supported.

## Intended tester audience

The first canary should be experienced macOS AU/VST3 and Windows VST3 users who can preserve 16-channel MIDI routing, identify Bank Select/Program Change events, restore a backup, and submit a minimal reproduction. The group should include FL Studio, Cubase, Logic, one additional AU host, and one additional VST3 host, plus users of SF2, SF3, conventional DLS, and known malformed DLS exports.

The tester count, invitation channel, support hours, and withdrawal owner require owner approval before launch. The approved feedback address is `contact@pokestir.com`; email subjects must begin with `[Juicy16 VST]` so reports can be identified and routed.

## Supported workflow

1. Back up the project and any previously installed Juicy16 plugin.
2. Verify the archive checksum and candidate number.
3. Install the correct AU/VST3 bundle for the listed platform/architecture.
4. Start with a new empty project and confirm plugin discovery.
5. Load a legally obtained DLS/SF2/SF3 bank.
6. Route one original 16-channel MIDI file to a single instance without manual patch assignments.
7. Record initial and mid-song instruments, controllers, bends, and channel 10 percussion.
8. Repeat after rewind, loop, stop/start, save/reopen, and beginning playback mid-song.
9. Only then test a copy of an existing project.

## Installation

Verify the download first. The archive ships with a `.sha256` file beside it, and `SHA256SUMS` inside covering every packaged file:

```bash
shasum -a 256 -c Juicy16-<version>-<candidate>-macos-arm64.zip.sha256
```

Do not install a package whose filename contains `LOCAL-DIRTY` or `ADHOC`. Those labels mark local validation builds, not candidates for testing.

Unpack the archive and copy the bundles into your user plug-in folders:

- macOS AU → `~/Library/Audio/Plug-Ins/Components/Juicy16.component`
- macOS VST3 → `~/Library/Audio/Plug-Ins/VST3/Juicy16.vst3`
- Windows VST3 → the VST3 directory your DAW scans, commonly `C:\Program Files\Common Files\VST3\Juicy16.vst3`

Back up any existing bundle at those paths before replacing it. Close every DAW first, then rescan plug-ins after copying.

### macOS Gatekeeper

Beta candidates are not yet Developer ID signed or notarized, so macOS will refuse to load the plug-in until you clear its quarantine attribute:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Juicy16.component
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Juicy16.vst3
```

Only do this for a bundle whose checksum you have verified against the one published with the candidate. If a DAW still refuses to load or validate the plug-in, report it with the exact host and macOS version rather than disabling wider security settings.

## Support boundary

Beta 1 is intended to cover only the exact OS versions, architectures, formats, and DAWs named in the candidate release notes. One stereo mix output is intentional. VST2, AUv3, Linux, 32-bit Windows, and unlisted architectures are unsupported unless a later approved matrix says otherwise. Bank-specific modulators can change how pressure and controllers sound even when messages are delivered correctly; see the exact [MIDI controller support contract](CONTROLLER_SUPPORT.md).

## Uninstall and rollback

Close all DAWs before replacing or removing a plugin.

On macOS, remove only the Juicy16 bundles installed for the Beta from:

- `~/Library/Audio/Plug-Ins/Components/` (AU)
- `~/Library/Audio/Plug-Ins/VST3/` (VST3)

On Windows, remove only `Juicy16.vst3` from the VST3 directory used during installation. Rescan the DAW afterward. Restore the prior backed-up bundle if rollback is required; never overwrite the backup. Projects saved with Beta-specific state may not work in an older build, so restore the corresponding project backup too.

Beta 2 and the first stable release are expected to read Beta 1 schema-v2 state. Older betas deliberately reject newer schemas rather than guessing at their meaning. See [STATE_COMPATIBILITY.md](STATE_COMPATIBILITY.md) for the versioning and identifier policy.

No uninstaller should delete user DLS/SF2/SF3 files or DAW projects. Temporary repaired DLS copies are stored in the operating system temporary area and normally removed with the plugin instance.

## Reporting

Use the Beta issue form or email `contact@pokestir.com` with a subject beginning `[Juicy16 VST]`. Include version/candidate, OS/architecture, DAW/version, format, sample rate/block size, bank type, MIDI routing, affected channel, exact reproduction, expected/actual behavior, fresh-project result, and logs/screenshots where safe. Do not upload copyrighted game assets, private projects, personal paths, or confidential banks without authorization and an approved retention policy.

Channel-1-only Cubase behavior, missed later Program Changes, state corruption, invalid signatures, missing dependencies, crashes, and severe audio corruption are priority regressions. Stop using the candidate on important projects until triaged.

Beta 1 displays its version and latest bank-load result but does not collect or copy a diagnostic bundle. Include only the requested non-sensitive facts manually; remove private paths and project/font contents unless explicit submission terms have been approved.
