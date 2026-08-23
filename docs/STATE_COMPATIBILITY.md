# Beta state compatibility policy

Juicy16 Beta 1 writes state schema version 6. Beta 2 and the first stable release must continue to read version 6 unless a stop-ship defect makes that unsafe. The automated suite must retain the older-version migration and version 6 round-trip cases for as long as those versions are supported.

Version 6 added the reverb control surface: `reverbOn`, `reverbProfile`, and the
four engine parameters. A version 5 save has none of them, so it opens with the
reverb enabled on the Universal profile — a deliberate default, recorded in
[CONTROLLER_SUPPORT.md](CONTROLLER_SUPPORT.md), rather than FluidSynth's
inherited one. Pinned by a regression that writes a version 5 envelope and
asserts the profile reaches the engine on reload.

Version 5 made volume and pan per channel. Where version 4 had a single
`volume`/`pan` parameter pair describing whichever channel the editor had
selected, version 5 has `volCh1`-`volCh16` and `panCh1`-`panCh16`, and adds
`muteCh1`-`muteCh16` and `soloCh1`-`soloCh16`. The retired `volume` and `pan`
parameters are gone.

A version 4 save is migrated from its per-channel records, not from those two
parameters: `channelPrograms` already stored every channel's volume and pan, so
each channel's saved values become that channel's own parameter and reach the
engine as before. Mute and solo do not exist in a version 4 save and arrive off.
Loading a version 4 project and re-saving it writes version 5. Pinned by a
regression that writes a version 4 envelope, reads it back, and asserts all 16
channels on both the parameters and the engine.

Version 4 widened the `bank` parameter from 0-128 to 0-255, so that a drum
channel's runtime bank — FluidSynth's 128 drum offset plus the Bank Select MSB,
up to 255 — is representable on every surface instead of only in the engine.
Parameters are stored normalised, so the same stored value means a different bank
number under the new range: a version 3 save's `bank` is rescaled through the
bank number on the way in, and a channel saved on bank 128 restores on bank 128.
Per-channel bank values are stored as plain integers and need no rescaling.

Version 3 replaced the six per-channel CC71-79 sound-controller values with the mixer controls `volume` (CC7) and `pan` (CC10), and added the global `outputLevel` trim. A version 1 or 2 save has no volume/pan attributes, so those channels keep the GM defaults (volume 100, pan centre); the retired attributes are ignored rather than migrated, because an envelope control has no meaningful mapping onto a mixer control. Bank and preset assignments still restore from those older saves.

Compatibility is forward-reading, not backward-reading: a newer build should migrate an older supported state, but an older beta is not expected to understand state written by a newer beta. The processor rejects a state whose schema number is newer than it supports, leaves its current engine state untouched, and shows a visible error. Testers must keep matching project and plugin backups when moving between candidates.

Any future incompatible state change must:

1. increment the schema version;
2. add an explicit migration or safe-rejection test before the writer ships;
3. preserve channel, bank, preset, controller, bookmark/path, and parameter semantics unless the release notes identify the exact intentional change;
4. add a migration entry to `CHANGELOG.md`; and
5. update this document and the Beta tester guide.

Beta 1 establishes a new host identity: product `Juicy16`, bundle ID `com.pokestir.juicy16`, manufacturer code `Pkst`, and plugin code `Jc16`. Pre-Beta JuicySF/Juicy16 sessions are not guaranteed to locate or migrate to this identity; that intentional break was approved before Beta 1 because of the larger architectural changes.

Beginning with Beta 1, the AU/VST3 identifiers, VST3 unit IDs, parameter IDs, and parameter versions are frozen compatibility surfaces. The exact values are recorded in [BETA1_IDENTITY_CONTRACT.md](BETA1_IDENTITY_CONTRACT.md). They remain stable throughout the Beta line unless an approved B0/B1 correction requires a change. Any such change needs a candidate migration note and host save/reopen validation.
