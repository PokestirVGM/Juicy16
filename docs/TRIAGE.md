# Beta report triage and reproduction

## Minimum report

Reject or request more information when a report lacks the Juicy16 version/candidate, artifact checksum or source, OS/architecture, DAW/version, AU/VST3 format, sample rate/block size, bank format, MIDI routing/channel, exact steps, expected/actual result, and whether it reproduces in a fresh project.

Email reports go to `contact@pokestir.com` with the subject prefix `[Juicy16 VST]`.

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

## Handling tester-submitted files

Bank files, MIDI files, and project files a tester attaches to a report are third-party material, frequently licensed and sometimes personal. They are handled as follows.

**Ask for the minimum.** Prefer a description, a screenshot, or a synthetic reproduction over the tester's own assets. Request a bank or project only when the defect cannot be reproduced without it, and say why. Never ask for a commercial bank a tester cannot legally share.

**Storage.** Submitted files are kept only in the report they arrived with — the issue attachment or the mailbox — and copied at most once, into a working directory on the maintainer machine used to reproduce the defect. They are never committed to this repository, added to `JUICYSF_FONT_CORPUS`, redistributed, included in any package, or uploaded anywhere else. `.gitignore` excludes `/testfiles/` so a local corpus cannot be committed by accident.

**Access.** Only the person triaging the report opens the file. Contents are never quoted in a public issue; describe the defect instead. If a bank must be shared with another maintainer to reproduce, ask the submitter first.

**Deletion.** The working copy is deleted as soon as the defect is reproduced and understood, or when the report is closed, whichever comes first. If a defect needs a permanent regression fixture, do not keep the tester's file: build a synthetic fixture that reproduces the same defect and check that in instead, as `tests/fixtures/` already does.

**On request.** A tester may ask for their submitted material to be deleted at any time, and it is removed from the working directory and the report itself.

> **Owner approval required before Beta distribution.** Two values are deliberately not set here, because they are commitments to third parties rather than engineering choices: the maximum retention period for material kept in the report itself, and whether the feedback mailbox is subject to any separate retention policy. `PRIVACY.txt` states that the feedback channel's operator must publish its own policy; that statement is not satisfied until these are filled in.
