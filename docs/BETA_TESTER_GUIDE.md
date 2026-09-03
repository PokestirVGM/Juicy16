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
  Installation section. The bundled installer does it for you; by hand it is one
  Terminal command. If you are not willing to do either, stop here.
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

## Installation

**Nothing to install first.** Juicy16 has no third-party dependencies — the
synthesis engine and every audio codec are built into the plugin itself. You do
not need Homebrew, FluidSynth, or anything else on your machine. The
`building.macos.md` file in the archive is for compiling from source and can be
ignored.

### The easy way

Unpack the `.zip`, then **double-click `install_macos.command`** inside it.

macOS will refuse to open it the first time and say it is from an unidentified
developer — that is the same ad-hoc signing situation as the plugin itself. To
get past it: **right-click the file, choose Open, then click Open** in the
dialog. You only do this once.

The installer checks the download against its checksums, asks whether you want
AU, VST3 or both, backs up any Juicy16 already installed, copies the bundles in,
and clears the quarantine flag that would otherwise make the plugin invisible to
your host. Then quit your DAW, reopen it, and rescan.

If it reports a problem, it stops before copying anything.

### The manual way

If you would rather not run a script, do the same five things by hand. They are
in this order for a reason, and step 4 is not optional: skip it and the plugin
will simply never appear in your host, with no error to tell you why.

### 1. Verify the download

```bash
shasum -a 256 -c Juicy16-0.6.0-beta.1-BC1-macos-arm64-ADHOC.zip.sha256
```

You want `OK`. This matters more here than for most downloads: Beta 1 is ad-hoc
signed rather than Developer ID signed, so the checksum is the only thing
standing in for a signature that would otherwise vouch for the file. Do not
continue if it does not match.

Two labels can appear in the filename, and they mean different things:

- **`ADHOC`** — ad-hoc signed. **Expected for Beta 1**, by an explicit release
  decision. It is why step 4 exists.
- **`LOCAL-DIRTY`** — built from an uncommitted working tree. **Never install
  one.** It cannot be traced to a commit, so a bug report against it is
  unreproducible. You should never receive one of these.

### 2. Quit every DAW

Hosts cache their plugin scans. Installing underneath a running DAW is the most
common reason a correct install still looks broken.

### 3. Copy in the formats you want

Unpack the archive. It contains `AU/Juicy16.component` and
`VST3/Juicy16.vst3` — install **either or both**, whichever your host uses:

```bash
# AU — FL Studio on macOS (also the format Logic and GarageBand use)
cp -R AU/Juicy16.component ~/Library/Audio/Plug-Ins/Components/

# VST3 — Cubase, and most other hosts
cp -R VST3/Juicy16.vst3 ~/Library/Audio/Plug-Ins/VST3/
```

Create those folders if they do not exist. If a Juicy16 bundle is already there,
move it somewhere safe first — you will want it back if you need to roll away
from the beta.

### 4. Clear the quarantine flag — required

macOS tags every downloaded file as quarantined, and refuses to load an ad-hoc
signed plugin that still carries the tag. Run this for whichever formats you
installed:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Juicy16.component
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Juicy16.vst3
```

Confirm with `xattr -p com.apple.quarantine <bundle>` — it should report that the
attribute does not exist.

Only ever do this for a bundle whose checksum you verified in step 1. Clearing
quarantine tells macOS *you* vouch for the file.

### 5. Rescan in your host

Reopen your DAW and rescan plugins. Juicy16 appears as an instrument.

### If it still does not show up

Every symptom below is the same missing step 4:

| Symptom | What it actually means |
|---|---|
| Never appears in the plugin list after a rescan | The host silently skipped a quarantined bundle |
| *"Juicy16 cannot be opened because the developer cannot be verified"* | Quarantine is still set |
| *"Juicy16 is damaged and can't be opened"* | Also quarantine, not corruption — verify the checksum, then clear it |
| Logic or GarageBand reports the AU as failing validation | `auval` ran against a quarantined bundle |

Some hosts cache a failed scan, so after fixing it: quit the host completely,
then reopen and rescan.

If a host still refuses **after** clearing quarantine on a checksum-verified
bundle, that is a real defect — report it with the exact host and macOS version.

Do not disable System Integrity Protection or turn Gatekeeper off entirely. No
Juicy16 problem requires that, and we will not ask you to.

## The window

The plugin is a 16-channel rack. Every row is one MIDI channel and owns
everything belonging to that channel, left to right: the channel number, mute and
solo, the instrument dropdown, and volume and pan knobs with their values. All 16
are visible at once — nothing needs a row selected first. Selecting a row only
chooses which channel the on-screen keyboard auditions.

The panel on the right holds what is global rather than per channel: the master
output trim, the reverb, and a summary of the loaded bank. The header carries the bank picker
and a settings button; settings holds the accent colour, two pitch-bend
compensations for hosts that damage bends on the way in (*Bend range* and
*Bend scale* — leave both off unless bends are plainly too small; see
[CONTROLLER_SUPPORT.md](CONTROLLER_SUPPORT.md)), and the build details worth
quoting in a bug report (version, FluidSynth version, plugin format, sample
rate).

Two things to know about the row controls:

- **Volume and pan follow the MIDI file.** Setting a knob is a starting point.
  The next CC7 or CC10 the file sends on that channel replaces it, at that
  event's timestamp — the same rule the instrument dropdowns follow for Program
  Change. This is intended; please do not report it as a defect.
- **If a channel sounds too loud or too quiet, look at its Vol knob first.**
  It shows the CC7 the file last sent. A knob that disagrees with the file is
  a plugin defect; a knob that agrees means the level is what the file asked
  for, and the report needs the file, channel, instrument, host, and what you
  are comparing against. [KNOWN_ISSUES.md](KNOWN_ISSUES.md) lists host-side
  settings that change a channel's volume behind the file's back.
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

## Support boundary

Beta 1 covers only the operating system, architecture, formats, and hosts named above: macOS 11+ on Apple Silicon, AU and VST3, validated in FL Studio and Cubase. One stereo mix output is intentional. Windows, VST2, AUv3, Linux, Intel Macs, and unlisted architectures are unsupported unless a later approved matrix says otherwise. Bank-specific modulators can change how pressure and controllers sound even when messages are delivered correctly; see the exact [MIDI controller support contract](CONTROLLER_SUPPORT.md).

## Uninstall and rollback

Close all DAWs before replacing or removing a plugin.

On macOS, remove only the Juicy16 bundles installed for the Beta from:

- `~/Library/Audio/Plug-Ins/Components/` (AU)
- `~/Library/Audio/Plug-Ins/VST3/` (VST3)

Rescan the DAW afterward. Restore the prior backed-up bundle if rollback is required; never overwrite the backup. Projects saved with Beta-specific state may not work in an older build, so restore the corresponding project backup too.

Beta 2 and the first stable release are expected to read Beta 1 schema-v6 state. Older builds deliberately reject newer schemas rather than guessing at their meaning. The versioning and identifier policy is in [COMPATIBILITY.md](COMPATIBILITY.md).

Removing the bundles never touches your DLS/SF2/SF3 files or DAW projects. Temporary repaired DLS copies are stored in the operating system temporary area and normally removed with the plugin instance.

## Reporting

Use the Beta issue form or email `contact@pokestir.com` with a subject beginning `[Juicy16 VST]`. Include version/candidate, OS/architecture, DAW/version, format, sample rate/block size, bank type, MIDI routing, affected channel, exact reproduction, expected/actual behavior, fresh-project result, and logs/screenshots where safe. Do not upload copyrighted game assets, private projects, personal paths, or confidential banks without authorization and an approved retention policy.

Channel-1-only Cubase behavior, missed later Program Changes, state corruption, invalid signatures, missing dependencies, crashes, and severe audio corruption are priority regressions. Stop using the candidate on important projects until triaged.

Beta 1 displays its version and latest bank-load result but does not collect or copy a diagnostic bundle. Include only the requested non-sensitive facts manually; remove private paths and project/font contents unless explicit submission terms have been approved.
