# Juicy16 Beta 1 identity contract

This file records the host-facing identifiers frozen when Beta 1 becomes the compatibility baseline. The current development version `0.6.0-alpha.2` already uses them, so no identifier change is pending; only the version label differs. Pre-Beta sessions are outside this contract. Beta 1 and later releases must retain these values unless an explicitly approved stop-ship migration changes the contract and adds host save/reopen coverage.

## Product and plugin identity

| Surface | Frozen value |
| --- | --- |
| Product and executable | `Juicy16` |
| Vendor | `Pokestir` |
| Bundle ID | `com.pokestir.juicy16` |
| AU manufacturer | `Pkst` |
| AU subtype | `Jc16` |
| VST3 audio class CID | `ABCDEF019182FAEB506B73744A633136` |
| VST3 controller class CID | `ABCDEF011234ABCD506B73744A633136` |

## Parameters and state

The parameter version hint is `1`. This avoids JUCE's Audio Unit assertion for
unversioned parameters and establishes the first public ordering baseline. The
parameter order and string IDs are:

```text
bank, preset, outputLevel,
reverbOn, reverbProfile, reverbSize, reverbDamp, reverbWidth, reverbLevel,
volCh1 .. volCh16,
panCh1 .. panCh16,
muteCh1 .. muteCh16,
soloCh1 .. soloCh16,
progCh1 .. progCh16
```

That is 89 parameters. The selected-channel `volume` and `pan` parameters that
schema 4 carried are **retired**: volume and pan are per channel now, so every
channel is automatable rather than only whichever row the editor had selected.

Neither the six reverb parameters nor the 64 mixer parameters belongs to a
parameter group. JUCE derives a
VST3 parameter's `unitId` from its group, and the vendored wrapper serves a fixed
17-unit structure that hosts cache before the component connection exists — a
group here would publish parameters pointing at an 18th unit the host was never
told about. Ungrouped, they report the root unit alongside `bank`, `preset`, and
`outputLevel`, and the 16 `chUnit` groups still hold exactly one `progChN` each.
`vst3_smoke` asserts both halves of that.

The corresponding VST3 `ParamID` values are:

```text
bank             0x002E063C    preset          0x4594E2DF
outputLevel      0x4DCA0B03

reverbOn         0x703BC751    reverbProfile   0x5D46E777
reverbSize       0x506904F3    reverbDamp      0x506213D2
reverbWidth      0x3CEFA714    reverbLevel     0x3C5314D2

volCh1           0x4FAA2A99    volCh2          0x4FAA2A9A
volCh3           0x4FAA2A9B    volCh4          0x4FAA2A9C
volCh5           0x4FAA2A9D    volCh6          0x4FAA2A9E
volCh7           0x4FAA2A9F    volCh8          0x4FAA2AA0
volCh9           0x4FAA2AA1    volCh10         0x259B28B7
volCh11          0x259B28B8    volCh12         0x259B28B9
volCh13          0x259B28BA    volCh14         0x259B28BB
volCh15          0x259B28BC    volCh16         0x259B28BD

panCh1           0x44A8B68F    panCh2          0x44A8B690
panCh3           0x44A8B691    panCh4          0x44A8B692
panCh5           0x44A8B693    panCh6          0x44A8B694
panCh7           0x44A8B695    panCh8          0x44A8B696
panCh9           0x44A8B697    panCh10         0x506E1B81
panCh11          0x506E1B82    panCh12         0x506E1B83
panCh13          0x506E1B84    panCh14         0x506E1B85
panCh15          0x506E1B86    panCh16         0x506E1B87

muteCh1          0x543FD393    muteCh2         0x543FD394
muteCh3          0x543FD395    muteCh4         0x543FD396
muteCh5          0x543FD397    muteCh6         0x543FD398
muteCh7          0x543FD399    muteCh8         0x543FD39A
muteCh9          0x543FD39B    muteCh10        0x33BA9EFD
muteCh11         0x33BA9EFE    muteCh12        0x33BA9EFF
muteCh13         0x33BA9F00    muteCh14        0x33BA9F01
muteCh15         0x33BA9F02    muteCh16        0x33BA9F03

soloCh1          0x06FBF30D    soloCh2         0x06FBF30E
soloCh3          0x06FBF30F    soloCh4         0x06FBF310
soloCh5          0x06FBF311    soloCh6         0x06FBF312
soloCh7          0x06FBF313    soloCh8         0x06FBF314
soloCh9          0x06FBF315    soloCh10        0x58826EC3
soloCh11         0x58826EC4    soloCh12        0x58826EC5
soloCh13         0x58826EC6    soloCh14        0x58826EC7
soloCh15         0x58826EC8    soloCh16        0x58826EC9

progCh1          0x6D8E6EB2    progCh2         0x6D8E6EB3
progCh3          0x6D8E6EB4    progCh4         0x6D8E6EB5
progCh5          0x6D8E6EB6    progCh6         0x6D8E6EB7
progCh7          0x6D8E6EB8    progCh8         0x6D8E6EB9
progCh9          0x6D8E6EBA    progCh10        0x443F67BE
progCh11         0x443F67BF    progCh12        0x443F67C0
progCh13         0x443F67C1    progCh14        0x443F67C2
progCh15         0x443F67C3    progCh16        0x443F67C4
```

The 16 `progChN` IDs and all 16 channel unit IDs are **unchanged** from schema 4.
Adding the mixer parameters did not disturb them, which is what keeps existing
Cubase and FL Studio sessions' program automation intact.

A parameter's range is part of this contract too, because hosts store automation
normalised: `bank` spans 0-255; `preset`, every `volChN`, every `panChN`, and
every `progChN` span 0-127; every `muteChN` and `soloChN` is a two-state boolean;
and `outputLevel` spans -24 to +12 dB. `reverbOn` is a two-state boolean,
`reverbProfile` is a 3-entry choice (`Universal`, `Soft`, `Custom`, in that
order — the index is what a host stores, so the order is frozen), and
`reverbSize`, `reverbDamp`, `reverbWidth` and `reverbLevel` each span 0 to 1.
`reverbWidth` is deliberately narrowed from FluidSynth's own 0-100: everything
musically useful lives below 1, and the full range would put it all inside the
first one percent of a knob's travel. `bank` reaches 255 rather than 128 because
a channel's runtime bank is FluidSynth's 128 drum offset plus the Bank Select
MSB; it was widened on 2026-08-23, before the freeze, and moves no further.

The state root is `MYPLUGINSETTINGS`, the Beta 1 schema is version `6`, and the
writer persists all 89 parameter values, 16 channel records (each carrying
`bank`, `preset`, `volume`, `pan`, `mute`, and `solo`), UI state, and the
SoundFont path/bookmark record. See
[STATE_COMPATIBILITY.md](STATE_COMPATIBILITY.md) for migration policy.

## VST3 multitimbral identity

The shared program-list ID is `0x50524F47` (`PROG`) and it contains 128 entries. Channel units 1 through 16 use these IDs in order:

```text
0x2B6251C8 0x2B6251C9 0x2B6251CA 0x2B6251CB
0x2B6251CC 0x2B6251CD 0x2B6251CE 0x2B6251CF
0x2B6251D0 0x40E7E768 0x40E7E769 0x40E7E76A
0x40E7E76B 0x40E7E76C 0x40E7E76D 0x40E7E76E
```

Automated metadata, engine, and VST3 smoke tests enforce this manifest. Host session save/reopen remains required for the exact packaged candidate in FL Studio and Cubase.
