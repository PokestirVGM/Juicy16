# Beta state compatibility policy

Juicy16 Beta 1 writes state schema version 3. Beta 2 and the first stable release must continue to read version 3 unless a stop-ship defect makes that unsafe. The automated suite must retain the older-version migration and version 3 round-trip cases for as long as those versions are supported.

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
