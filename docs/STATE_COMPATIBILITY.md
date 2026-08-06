# Beta state compatibility policy

JuicySF Rack Beta 1 writes state schema version 2. Beta 2 and the first stable release must continue to read version 2 unless a stop-ship defect makes that unsafe. The automated suite must retain the version 1 migration and version 2 round-trip cases for as long as those versions are supported.

Compatibility is forward-reading, not backward-reading: a newer build should migrate an older supported state, but an older beta is not expected to understand state written by a newer beta. The processor rejects a state whose schema number is newer than it supports, leaves its current engine state untouched, and shows a visible error. Testers must keep matching project and plugin backups when moving between candidates.

Any future incompatible state change must:

1. increment the schema version;
2. add an explicit migration or safe-rejection test before the writer ships;
3. preserve channel, bank, preset, controller, bookmark/path, and parameter semantics unless the release notes identify the exact intentional change;
4. add a migration entry to `CHANGELOG.md`; and
5. update this document and the Beta tester guide.

The existing AU/VST3 bundle identifiers, VST3 unit IDs, parameter IDs, and parameter versions are compatibility surfaces. They remain frozen throughout the Beta line unless an approved B0/B1 correction requires a change. Any such change needs a candidate migration note and host save/reopen validation. Plugin identifiers remain subject to the Phase 0 ownership decision; once approved, changing them is a separate compatibility event rather than routine metadata cleanup.
