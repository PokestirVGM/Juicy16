# Beta 1 tester guide

## Required warning

Juicy16 Beta 1 is pre-release audio software. Use copies of important DAW projects and banks, save incremental project versions, and do not depend on Beta state for irreplaceable work. A Beta candidate may be withdrawn for crashes, state corruption, invalid signatures/dependencies, licensing problems, or incorrect multichannel MIDI behavior.

Check the table below before downloading: it lists exactly which operating system, CPU, and plugin formats Beta 1 covers. “Cross-platform” does not mean every OS/host combination is supported — Windows is not part of Beta 1 at all.

## Is your setup supported? Check before downloading

Answer all four. If any answer is no, this candidate is not for your machine, and
nothing below applies.

| | Supported | Not supported |
| --- | --- | --- |
| **Operating system** | macOS 11.0 or later | Anything older; Windows; Linux |
| **CPU** | Apple Silicon (`arm64`) | Intel Macs; everything else |
| **Plugin format** | AU and VST3 | VST2; AUv3; standalone as a release format |
| **Bank file** | SF2, SF3, DLS | Anything else |

`Apple menu > About This Mac` names the chip: "Apple M1/M2/M3/M4" is supported,
"Intel" is not.

**Beta 1 is macOS only.** Windows VST3 moved to Beta 2 on 2026-08-24: that
toolchain has never produced a host-validated artifact, so there is nothing to
test yet.

Two further limits worth knowing before you spend time on this:

- **The candidate is ad-hoc signed**, not Developer ID signed or notarized, so
  macOS will refuse it until you clear quarantine. The exact command is in the
  Gatekeeper section below. If you are not willing to do that, stop here.
- **Beta 1 is validated in FL Studio and Cubase only**, in both AU and VST3.
  Logic Pro and every other host are untested rather than known-good — approved
  *scope* is not the same as tested. `auval -strict` passes, which is not the
  same as Logic working.

The full matrix — including sample rates, bank-format evidence, and what is
explicitly out of scope — is [../README.md](../README.md). Known
limitations you should read before reporting a bug are in
[KNOWN_ISSUES.md](KNOWN_ISSUES.md).

## Intended tester audience

The first canary should be experienced macOS AU/VST3 users who can preserve 16-channel MIDI routing, identify Bank Select/Program Change events, restore a backup, and submit a minimal reproduction. The group should include FL Studio and Cubase, plus users of SF2, SF3, conventional DLS, and known malformed DLS exports. Logic Pro is not owned and is untested in Beta 1; a Logic report is welcome but is exploring an unvalidated host rather than confirming a tested one.

Send reports to `contact@pokestir.com` with a subject beginning `[Juicy16 VST]` so they can be identified and routed.

## The window

The plugin is a 16-channel rack. Every row is one MIDI channel and owns
everything belonging to that channel, left to right: the channel number, mute and
solo, the instrument dropdown, and volume and pan knobs with their values. All 16
are visible at once — nothing needs a row selected first. Selecting a row only
chooses which channel the on-screen keyboard auditions.

The panel on the right holds what is global rather than per channel: the master
output trim, the reverb, and a summary of the loaded bank. The header carries the bank picker
and a settings button; settings holds the accent colour and the build details
worth quoting in a bug report (version, FluidSynth version, plugin format, sample
rate).

Two things to know about the row controls:

- **Volume and pan follow the MIDI file.** Setting a knob is a starting point.
  The next CC7 or CC10 the file sends on that channel replaces it, at that
  event's timestamp — the same rule the instrument dropdowns follow for Program
  Change. This is intended; please do not report it as a defect.
- **Mute and solo do not follow the MIDI file.** Nothing in a MIDI file changes
  them, and no reset clears them. A muted channel stops sounding new notes but
  still receives everything else, so unmuting mid-song picks up correctly.
- **Mute wins over solo.** A channel sounds if it is not muted and either nothing
  is soloed or it is one of the soloed ones. Muting the only soloed channel does
  produce silence — that is deliberate, so that pressing M always does something.
  Every silenced row visibly recedes, and a lit mute is red while a lit solo is
  the accent colour, so you can always see why a channel is quiet.

Every one of those controls is a host parameter, so a host's automation and
controller-link menus reach all 16 channels.

### The reverb, and one thing that will surprise you

Juicy16 discarded FluidSynth's effects buses until 0.6.0-alpha.1, so its reverb
was never audible. It is now mixed in. **A project you made with an earlier build
will sound different** if its MIDI sends CC91 — that is the file being played as
written, but please do not report it as a defect.

- You set the reverb: enable, a profile (Universal or Soft), and size, damping,
  width and level. Selecting a profile moves all four controls; editing any of
  them selects Custom.
- The MIDI file sets how much of each channel goes into it, through CC91. It
  cannot change your settings — GS and XG reverb SysEx is deliberately ignored.
- Every channel starts at the General MIDI default reverb send (CC91 = 40), so
  the reverb is audible without the file asking for it. A file that sends its own
  CC91 overrides that, per channel.
- Chorus is switched off. CC93 still reaches the engine but drives nothing yet.

Nobody has listened to these profiles on real rips yet — they were chosen by
measurement. Reports on how they actually sound are exactly what is wanted.

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

### Working without a mouse

Every core workflow is reachable from the keyboard, so please report anything
that is not — especially a host that swallows Tab before it reaches the editor.

- Tab moves between the bank browse button, the settings button, the 16-channel
  rack, each row's own controls, and the master trim.
- On the channel rack, up and down arrows select the MIDI channel the audition
  keyboard and status line follow.
- Return on the selected row opens that channel's instrument list; arrows and
  Return pick from it, and focus comes back to the rack.
- On a focused knob, arrows change the value; a knob's number can also be typed.
- Mute and solo are ordinary buttons: Space or Return toggles the focused one.

Screen-reader announcements are untested. Every control carries an accessible
name, but nobody has yet run Juicy16 under VoiceOver or Narrator, so reports from
that angle are especially useful.

## Installation

### macOS will block it on first use — this is expected

Beta 1 is **ad-hoc signed**, not signed with an Apple Developer ID and not
notarized. That is a deliberate choice for a small beta, not an oversight, and it
means Gatekeeper will refuse the plugin the first time your host scans it. The
plugin is not damaged and nothing is wrong with the download.

Clear the quarantine flag after copying the bundles into place:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Juicy16.component
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Juicy16.vst3
```

Then rescan plugins in your host. If a host still refuses to load it, quit the
host, run the two commands again, and reopen — some hosts cache a failed scan.

Verify the checksum first, below: an ad-hoc signature does not tell you the
download arrived intact, so the SHA-256 matters more here than it would for a
notarized build.

Verify the download first. The archive ships with a `.sha256` file beside it, and `SHA256SUMS` inside covering every packaged file:

```bash
shasum -a 256 -c Juicy16-0.6.0-beta.1-BC1-macos-arm64-ADHOC.zip.sha256
```

Two labels can appear in the filename, and they mean different things:

- `LOCAL-DIRTY` — built from an uncommitted working tree. **Never install one.** It cannot be traced to a commit, so a bug report against it is unreproducible.
- `ADHOC` — ad-hoc signed rather than Developer ID signed. **Expected for Beta 1**, by an explicit release decision. It is why the Gatekeeper step below is required.

Unpack the archive and copy the bundles into your user plug-in folders:

- macOS AU → `~/Library/Audio/Plug-Ins/Components/Juicy16.component`
- macOS VST3 → `~/Library/Audio/Plug-Ins/VST3/Juicy16.vst3`

Back up any existing bundle at those paths before replacing it. Close every DAW first, then rescan plug-ins after copying.

### macOS Gatekeeper — required for every Beta 1 install

Beta 1 is **ad-hoc signed by decision**: it carries a valid signature, but not a
Developer ID one, and it is not notarized. macOS will therefore refuse to load it
until you clear the quarantine attribute the download added. This is expected, not
a fault in the download.

**Verify the checksum first**, then:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Juicy16.component
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Juicy16.vst3
```

Only ever do this for a bundle whose checksum you have verified against the one
published with the candidate. Clearing quarantine tells macOS you vouch for the
file, so the checksum is the thing standing in for the missing Developer ID
signature.

What you may see if you skip it:

| Symptom | Meaning |
|---|---|
| The plug-in never appears in the DAW's list after a rescan | The host silently skipped a quarantined bundle |
| *"Juicy16 cannot be opened because the developer cannot be verified"* | Quarantine is still set |
| *"Juicy16 is damaged and can't be opened"* | Also usually quarantine, not actual corruption — verify the checksum, then clear it |
| Logic or GarageBand reports the AU as failing validation | `auval` ran against a quarantined bundle |

Confirm it worked with `xattr -p com.apple.quarantine <bundle>`, which should
report that the attribute does not exist.

If a DAW still refuses to load or validate the plug-in **after** clearing
quarantine on a checksum-verified bundle, that is a real defect: report it with
the exact host and macOS version. Do not disable System Integrity Protection or
switch Gatekeeper off entirely — no Juicy16 problem requires that, and we will not
ask you to.

## Support boundary

Beta 1 covers only the operating system, architecture, formats, and hosts named above: macOS 11+ on Apple Silicon, AU and VST3, validated in FL Studio and Cubase. One stereo mix output is intentional. Windows, VST2, AUv3, Linux, Intel Macs, and unlisted architectures are unsupported unless a later approved matrix says otherwise. Bank-specific modulators can change how pressure and controllers sound even when messages are delivered correctly; see the exact [MIDI controller support contract](CONTROLLER_SUPPORT.md).

## Uninstall and rollback

Close all DAWs before replacing or removing a plugin.

On macOS, remove only the Juicy16 bundles installed for the Beta from:

- `~/Library/Audio/Plug-Ins/Components/` (AU)
- `~/Library/Audio/Plug-Ins/VST3/` (VST3)

Rescan the DAW afterward. Restore the prior backed-up bundle if rollback is required; never overwrite the backup. Projects saved with Beta-specific state may not work in an older build, so restore the corresponding project backup too.

Beta 2 and the first stable release are expected to read Beta 1 schema-v6 state. Older betas deliberately reject newer schemas rather than guessing at their meaning. See this document for the versioning and identifier policy.

No uninstaller should delete user DLS/SF2/SF3 files or DAW projects. Temporary repaired DLS copies are stored in the operating system temporary area and normally removed with the plugin instance.

## Reporting

Use the Beta issue form or email `contact@pokestir.com` with a subject beginning `[Juicy16 VST]`. Include version/candidate, OS/architecture, DAW/version, format, sample rate/block size, bank type, MIDI routing, affected channel, exact reproduction, expected/actual behavior, fresh-project result, and logs/screenshots where safe. Do not upload copyrighted game assets, private projects, personal paths, or confidential banks without authorization and an approved retention policy.

Channel-1-only Cubase behavior, missed later Program Changes, state corruption, invalid signatures, missing dependencies, crashes, and severe audio corruption are priority regressions. Stop using the candidate on important projects until triaged.

Beta 1 displays its version and latest bank-load result but does not collect or copy a diagnostic bundle. Include only the requested non-sensitive facts manually; remove private paths and project/font contents unless explicit submission terms have been approved.
