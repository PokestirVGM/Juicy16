# Beta tester guide

Thanks for trying Juicy16. It's beta software, so keep backups of any project
you care about and save new versions as you go.

## What you need

| | Supported |
| --- | --- |
| macOS | macOS 11 or later on Apple Silicon (M1 and newer), AU or VST3 |
| Windows | Windows 10 or later, 64-bit, VST3 (coming to the release page soon) |
| Banks | `.dls`, `.sf2`, `.sf3` |

Intel Macs, VST2, AUv3 and Linux aren't supported. I test in FL Studio and
Cubase; other hosts are welcome but untested.

## Installing on macOS

The builds are ad-hoc signed rather than notarized, so macOS blocks them until
you clear the quarantine flag. The installer does that for you.

**Easy way:** unzip, then right-click `install_macos.command` → Open → Open. It
checks the download, asks which formats you want, backs up any old copy, and
installs. Then quit your DAW, reopen it and rescan.

**By hand:**

```bash
# 1. Check the download (you want "OK")
shasum -a 256 -c Juicy16-*.zip.sha256

# 2. Quit your DAW, then copy in the formats you use
cp -R AU/Juicy16.component ~/Library/Audio/Plug-Ins/Components/
cp -R VST3/Juicy16.vst3 ~/Library/Audio/Plug-Ins/VST3/

# 3. Clear quarantine (the plugin won't show up without this)
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Juicy16.component
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Juicy16.vst3
```

Then reopen your DAW and rescan.

If a zip's name contains `LOCAL-DIRTY`, don't install it; it's a local test build.

**If it doesn't show up:** a missing plugin, "developer cannot be verified" or
"is damaged" all mean quarantine is still set. Clear it, quit the DAW fully, and
rescan. If it still fails after that, please report it.

## Installing on Windows

When the Windows release is available, run its Setup EXE or unzip the portable
package and copy the complete `VST3\Juicy16.vst3` bundle into
`C:\Program Files\Common Files\VST3`, then rescan in your DAW.
`INSTALL-WINDOWS.txt` in the package has the details. The installer and binaries
are unsigned. The optional Standalone app is for development/QA.

The [recorded Windows checks](WINDOWS_RELEASE.md) cover 0.6.1-beta.4;
1.0.0-beta.1 validation and Windows DAW/minimum-OS checks remain pending.

## Using it

The window is a 16-channel rack: each row is one MIDI channel with mute, solo,
instrument, volume, pan and trim. The right panel has the master trim, reverb,
chorus and the loaded bank. Click the Juicy16 logo for settings.

A few things that are intentional:

- **MIDI wins.** A Program Change, CC7 or CC10 from the file replaces whatever
  you set by hand, at that moment.
- **Mute and solo are yours.** Nothing in the MIDI file changes them. Mute beats
  solo, and silenced rows dim so you can see why a channel is quiet.
- **Reverb is off by default.** Turn it on in the right panel. The file's CC91
  decides how much of each channel goes in.
- **Chorus is off by default** and follows CC93.
- **Interpolation defaults to Linear**, like Fruity LSD. Try 7th-order for a
  cleaner sound or None for a rawer GBA feel. Older projects keep 7th-order.

If a channel's level seems wrong, check its volume knob first. It shows the last
CC7 the file sent.

## What to test

1. Start a fresh project and check the plugin loads.
2. Load a bank and route a 16-channel MIDI file to one instance.
3. Check instruments, volume, pan, bends and drums on channel 10, both at the
   start and mid-song.
4. Repeat after stop/play, looping, and save/close/reopen.
5. Then try a copy of an existing project.

Keyboard control works too: Tab moves around, arrows pick a channel, Return
opens its instrument list, and Space toggles mute or solo.

## Uninstalling

Quit your DAW and delete Juicy16 from `~/Library/Audio/Plug-Ins/Components` and
`~/Library/Audio/Plug-Ins/VST3` (or `C:\Program Files\Common Files\VST3`). Your
banks and projects aren't touched. If you used the Windows installer, uninstall
Juicy16 through Windows Settings → Apps. A project saved with a newer build may not
open in an older one, so keep a project backup if you might roll back.

## Reporting bugs

Use the GitHub issue form, or email `contact@pokestir.com` with a subject
starting `[Juicy16 VST]`. Please include:

- Juicy16 version (shown in the status bar), OS and DAW version, AU or VST3
- sample rate and buffer size
- the bank type and which channel is affected
- steps to reproduce, and what you expected

Please don't send copyrighted banks or game files unless I ask. Check
[known issues](KNOWN_ISSUES.md) first, and see [troubleshooting](TROUBLESHOOTING.md)
for common routing problems.
