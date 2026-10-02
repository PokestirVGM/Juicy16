# Compatibility

These are the things I keep fixed so saved projects and automation keep working
across updates. Changing any of them would break existing sessions.

## Plugin identity

| | Value |
| --- | --- |
| Product | `Juicy16` |
| Vendor | `Pokestir` |
| Bundle ID | `com.pokestir.juicy16` |
| AU manufacturer / subtype | `Pkst` / `Jc16` |
| VST3 processor CID | `ABCDEF019182FAEB506B73744A633136` |
| VST3 controller CID | `ABCDEF011234ABCD506B73744A633136` |

## Parameters

New parameters are only ever added at the end, so existing indices never move.
There are 131, in this order:

```text
bank, preset, outputLevel,
reverbOn, reverbProfile, reverbSize, reverbDamp, reverbWidth, reverbLevel,
volCh1 .. volCh16,
panCh1 .. panCh16,
muteCh1 .. muteCh16,
soloCh1 .. soloCh16,
progCh1 .. progCh16,
bendRange, bendScale,
resetPolicy, trimCh1 .. trimCh16,
chorusOn, chorusVoices, chorusLevel, chorusRate, chorusDepth, chorusWaveform,
vibratoScaleCh1 .. vibratoScaleCh16,
interpolation
```

AU version hints: the first 91 use `1`, the playback group (`resetPolicy`, trims)
`2`, chorus `3`, vibrato `4`, and `interpolation` `5`.

Hosts store automation as 0–1, so ranges are fixed too: `bank` 0–255, `preset`,
volume, pan and `progChN` 0–127, master trim -24 to +12 dB, reverb values 0–1,
`bendRange` 0–24 (0 follows the file), `bendScale` 1–24. Choice parameters keep
their order: `reverbProfile` is Universal / Soft / Custom and `interpolation` is
7th-order / Linear / None.

The mixer and reverb parameters are deliberately ungrouped. In VST3 a group
becomes a unit, and the plugin serves a fixed set of 17 units that Cubase caches
early. Only the 16 `progChN` parameters sit in groups (`chUnit1`–`chUnit16`).

VST3 ParamIDs:

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

bendRange        0x284C8F84    bendScale       0x285B5F91
interpolation    0x2156B9A4
```

## VST3 units

The shared program list is `0x50524F47` (`PROG`) with 128 entries. Channel units
1–16 use these IDs:

```text
0x2B6251C8 0x2B6251C9 0x2B6251CA 0x2B6251CB
0x2B6251CC 0x2B6251CD 0x2B6251CE 0x2B6251CF
0x2B6251D0 0x40E7E768 0x40E7E769 0x40E7E76A
0x40E7E76B 0x40E7E76C 0x40E7E76D 0x40E7E76E
```

The tests check these IDs and the parameter manifest.

## Saved state

The state root is `MYPLUGINSETTINGS`, currently schema **11**. It stores every
parameter, one record per channel (`bank`, `preset`, `volume`, `pan`, `mute`,
`solo`, `expression`, `bendRange`), the window and accent, and the bank's path and bookmark.

Newer builds read older projects; older builds refuse newer ones with an error
rather than guessing. Keep a project backup if you might roll back.

| Schema | Added | Older projects open with |
| --- | --- | --- |
| 11 | saved UI accent | Sage for projects without an accent; all twelve choices round-trip |
| 10 | `interpolation` | 7th-order, the sound they were made with (new instances default to Linear) |
| 9 | CC1 vibrato strength | ×1 |
| 8 | chorus | chorus off |
| 7 | reset policy, channel trims, remembered expression and bend range | DAW recovery, 0 dB trims |
| 6 | reverb controls (the Beta 1 schema) | reverb off, Universal profile |
| 5 | per-channel volume, pan, mute, solo | values taken from the channel records |
| 4 | `bank` widened to 0–255 | bank rescaled so the same bank loads |
| 3 | volume and pan replaced the old CC71–79 values | GM defaults for volume and pan |

Any future state change needs a new schema number, a migration test, a
changelog entry and an update here.

Recall reapplies saved channel programs and mixer values even when the bank
path and parameter values match the current instance. Older projects reset
controls absent from their schema rather than inheriting a previous project's
mute/solo, mixer or reverb settings. Nonfinite normalized values are ignored;
finite out-of-range values are clamped before migration.
