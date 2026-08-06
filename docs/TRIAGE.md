# Beta report triage and reproduction

## Minimum report

Reject or request more information when a report lacks the JuicySF version/candidate, artifact checksum or source, OS/architecture, DAW/version, AU/VST3 format, sample rate/block size, bank format, MIDI routing/channel, exact steps, expected/actual result, and whether it reproduces in a fresh project.

Ask for the smallest legally shareable MIDI/event sequence and bank only when needed. Store tester files only in an owner-approved restricted location, record who can access them, and delete them on the published schedule. Never copy private assets into the repository.

## Reproduction order

1. Preserve the frozen candidate and original report before changing code.
2. Verify artifact checksum, signature, architecture, deployment target, and dependencies.
3. Reproduce in a new project at the reported sample rate/block size.
4. Separate host routing/chase from plugin behavior using the offline trace and another host.
5. For Program Change, record reset/CC0/CC32/PC/note order and expected bank/program on every channel.
6. For CC/bend, record raw number/value/channel/timestamp; do not rely only on audible bank behavior.
7. Repeat save/reopen and transport conditions when state or chase is involved.
8. Classify severity and link duplicate reports before implementing a fix.
9. Run the relevant regression subset, then the complete candidate gate for a replacement public artifact.

## Severity

- B0: crash, corruption, security/privacy/license blocker, discovery failure, missing runtime dependency, invalid signature, or severe audio corruption.
- B1: wrong MIDI timing/routing, wrong channel/program, reset/state failure, broken advertised format, or required host/font matrix failure.
- B2: approved documented workaround with low tester burden and no destructive impact.
- B3: cosmetic or low-impact backlog.

No B0/B1 may ship. An owner must approve and publish every B2.

## Crash logs

On macOS, obtain the DAW crash report from Console or `~/Library/Logs/DiagnosticReports/`, then redact unrelated process/user paths before submission. On Windows, use the DAW's crash report and Windows Reliability Monitor/Event Viewer; collect a minidump only through an approved private channel. Never post a private project or full bank publicly by default.

## Labels

Use severity (`B0`–`B3`), platform (`macOS`, `Windows`), format (`AU`, `VST3`), host, font (`SF2`, `SF3`, `DLS`, `DLS-repair`), and subsystem (`discovery`, `MIDI-routing`, `CC-bend`, `state`, `font-load`, `UI`, `performance`, `packaging`, `privacy-license`).
