# Juicy16 Beta 1 identity contract

This file records the host-facing identifiers frozen when Beta 1 becomes the compatibility baseline. The current development version `0.5.1-alpha.1` already uses them, so no identifier change is pending; only the version label differs. Pre-Beta sessions are outside this contract. Beta 1 and later releases must retain these values unless an explicitly approved stop-ship migration changes the contract and adds host save/reopen coverage.

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
bank, preset, attack, decay, sustain, release, filterCutOff, filterResonance,
progCh1, progCh2, progCh3, progCh4, progCh5, progCh6, progCh7, progCh8,
progCh9, progCh10, progCh11, progCh12, progCh13, progCh14, progCh15, progCh16
```

The corresponding VST3 `ParamID` values are:

```text
bank             0x002E063C    preset          0x4594E2DF
attack           0x2C1EEE48    decay           0x05B097BA
sustain          0x119E6223    release         0x41012807
filterCutOff     0x63EBB165    filterResonance 0x14ED43B6
progCh1          0x6D8E6EB2    progCh2         0x6D8E6EB3
progCh3          0x6D8E6EB4    progCh4         0x6D8E6EB5
progCh5          0x6D8E6EB6    progCh6         0x6D8E6EB7
progCh7          0x6D8E6EB8    progCh8         0x6D8E6EB9
progCh9          0x6D8E6EBA    progCh10        0x443F67BE
progCh11         0x443F67BF    progCh12        0x443F67C0
progCh13         0x443F67C1    progCh14        0x443F67C2
progCh15         0x443F67C3    progCh16        0x443F67C4
```

The state root is `MYPLUGINSETTINGS`, the Beta 1 schema is version `2`, and the writer persists all 24 parameter values, 16 channel-program records, UI state, and the SoundFont path/bookmark record. See [STATE_COMPATIBILITY.md](STATE_COMPATIBILITY.md) for migration policy.

## VST3 multitimbral identity

The shared program-list ID is `0x50524F47` (`PROG`) and it contains 128 entries. Channel units 1 through 16 use these IDs in order:

```text
0x2B6251C8 0x2B6251C9 0x2B6251CA 0x2B6251CB
0x2B6251CC 0x2B6251CD 0x2B6251CE 0x2B6251CF
0x2B6251D0 0x40E7E768 0x40E7E769 0x40E7E76A
0x40E7E76B 0x40E7E76C 0x40E7E76D 0x40E7E76E
```

Automated metadata, engine, and VST3 smoke tests enforce this manifest. Host session save/reopen remains required for the exact packaged candidate in FL Studio and Cubase.
