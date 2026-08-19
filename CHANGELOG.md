# Changelog

## 0.5.1-alpha.1 — unreleased

Deliberately labelled alpha, not beta. The engine, build, packaging, and automated gates are in good shape, but the Beta 1 bar in [MILESTONE_PLAN.md](MILESTONE_PLAN.md) has not been met: no DAW host has run this build, no Windows artifact exists, and no hosted CI run has occurred.

### Changed

- Renamed the product to Juicy16 and established new Pokestir AU/VST3 identifiers as the Beta 1 compatibility baseline; pre-Beta host-session discovery is intentionally not preserved.
- Selected macOS 11+ Apple Silicon and Windows 10 1607+ x64 as the Beta 1 platform matrix; Intel macOS, Windows ARM64, and Linux are deferred.
- Selected JUCE's AGPLv3 path while preserving GPLv3 coverage and historical notices for inherited application code.
- Defined AU and VST3 as release formats; Standalone is QA-only and VST2 is excluded.
- Removed the legacy VST2 build option and unused Projucer-generated AAX/RTAS/AUv3/VST2/Unity and JUCE module translation units from the CMake project tree.
- Pinned JUCE 8.0.14 and centralized the visible version, now `0.5.1-alpha.1`, on a single CMake source of truth.
- Froze the Beta 1 AU/VST3 class, parameter, program-list, unit, and state-schema identifiers and added regression checks for them.
- Assigned version hint 1 to every Beta 1 parameter, eliminating JUCE's Audio Unit unversioned-parameter assertion before the public compatibility baseline.
- Added a reproducible normalized diff for the JUCE 8.0.14 VST3 wrapper and configure-time hashes for both upstream inputs, vendored outputs, and the reviewed patch.
- Consolidated VST3 `IUnitInfo` ownership into the patched JUCE component/controller, removing the duplicate-interface assertion while preserving Cubase's early discovery and host program-list refresh.
- Preserved every VST3 `progChN` automation point as a timestamped channel Program Change, avoiding JUCE's normal last-point/block-start parameter collapse.
- Reworked audio rendering so MIDI events take effect at their sample positions within each block.
- Preserved deterministic input order for Bank Select, Program Change, reset SysEx, controllers, and notes sharing a timestamp.
- Pinned Beta 1 Bank Select to FluidSynth's GS mode and added DLS coverage proving pending CC0, ignored-for-selection CC32, Program Change, UI, note, and saved-state convergence.
- Recreated FluidSynth at supported host sample rates and preallocated/chunked mono-render scratch storage; rates above FluidSynth's 96 kHz ceiling now fail safely to silence instead of rendering at a stale rate and wrong pitch.
- Added General MIDI channel 10 percussion-bank defaults.
- Made reset recovery use current atomic program/controller state so stale asynchronous state cannot overwrite newer events.
- Made bank replacement transactional and added structured load status/error properties.
- Made FluidSynth preset-name and editor status-label Unicode conversion explicit, avoiding Debug assertions on non-ASCII text.
- Restored the last working bank path/bookmark after a rejected replacement so the next project save cannot persist the failed candidate.
- Added macOS path fallback when bookmark creation fails and released Core Foundation errors.
- Made static FluidSynth linkage usable for portable macOS Release artifacts.
- Ordered bundle resources, VST3 metadata, and final signing deterministically.

### Fixed

- Bounded the DLS repair path, which read any file with a DLS-looking header entirely into memory. Repair is now capped at 512 MB, and an unrepaired bank whose RIFF header claims more data than the file holds is rejected before FluidSynth sees it — that parser took 72.5 seconds to fail on an 805 MB malformed image, blocking the message thread.
- Raised selected-channel row contrast from 1.53:1 to 16.69:1. The row was filled with a fixed pale blue while its text stayed near-white, making the row the user works with the hardest one to read. It now uses the colour scheme's highlighted fill and text.
- Raised the error status label from 4.40:1 to 5.27:1, clearing the WCAG AA threshold it previously sat just below.
- Gave the six sound-control sliders keyboard focus. JUCE sliders decline it by default, which left parameter editing mouse-only.
- Gave every bank-load failure message a recovery action instead of only naming the problem.
- Fixed the published checksum sidecar, which recorded the packaging machine's absolute path. The documented `shasum -a 256 -c` verification would have failed on a tester's machine, and the file published the developer's home directory. Both packagers now emit a relative checksum, and the macOS packager fails if the sidecar contains an absolute path.
- Made the Windows DLS strategy explicit in the legacy cross-build configuration (`enable-native-dls=on`), which otherwise risked producing a Windows build with no DLS support at all.
- Restored-state selected-channel bounds and per-channel engine-call validation.
- Full 16-channel Program Change routing in the engine and VST3 unit/mapping smoke paths.
- Full 14-bit pitch-bend forwarding and per-channel RPN bend range.
- Path-only font restoration on macOS.
- Failed bank loads no longer silently destroy a working setup.
- State created by a newer schema is rejected visibly instead of being silently reinterpreted.

### Tests

- Registered CTest coverage for DLS repair/loading, offline engine/MIDI rendering, transactional load failure, and VST3 multitimbral discovery/mapping.
- Added all-channel synchronization coverage for the six exposed sound controllers, including exact timestamps, selected-only slider mirroring, channel switching, duplicate-send prevention, and state reopening.
- Added a checked-in controller conformance fixture and offline expected-trace comparison for common CCs, MSB/LSB pairs, channel modes, RPN/NRPN ordering, and representative full-range pitch bends.
- Froze and tested the six sound-control modulator destinations, neutral point, directions, and reset semantics, and documented FluidSynth's exact controller interpretation boundaries.
- Added effect-level sustain, sostenuto, All Notes Off, All Sound Off, and Reset All Controllers regression scenarios using isolated engine/voice-state checks.
- Added transactional rejection coverage for moved/missing, non-file, unreadable, unsupported, corrupt, and zero-instrument bank inputs while preserving audio and saved state.
- Added state-and-audio coverage for a real DLS loaded from a nested Unicode path longer than 200 characters, including exact path serialization and restoration.
- Added explicit accessibility metadata and descriptive help for the bank picker, channel table/dropdowns, keyboard, status label, and all six sound-control sliders, with a headless metadata regression check.
- Added headless minimum/default/maximum editor-size checks, including essential-control bounds, all-row fit at the natural default height, and first/last-row scroll reachability at minimum height.
- Extended the VST3 harness with pre/post-connection query equivalence, invalid-input rejection, repeated lifecycles, state-driven DLS program-name refresh, and host refresh-notification checks.
- Extended the VST3 harness through concrete stereo/event processing for both `IMidiMapping` and unit/program-parameter routes, including all-channel audio, host edit observation, and serialized state convergence.
- Added a checked-in synthetic VST3 multichannel fixture covering all 16 channels, Bank Select, percussion, simultaneous and mid-block Program Changes, same-block notes, framed GM/GS/XG resets, stop/restart restoration, and duplicate host-edit suppression.
- Extended every VST3 fixture checkpoint with isolated per-channel auditions after all-channel All Sound Off, proving each exact state-verified melodic/percussion program reaches an independently sounding engine channel.
- Verified the exact static Release loader against FluidSynth's pinned, licensed upstream SF3 fixture (136 presets) without copying the bank into the repository or package.
- Extended audio-domain pitch coverage to 88.2 kHz and added 192 kHz fail-silent plus supported-rate recovery regression checks.
- Added an audio-domain GM channel-10 regression proving a fresh instance plays its default percussion kit without Bank Select or Program Change.
- Added an automated internal Markdown-link gate covering the active source documentation.
- Verified Debug and source-built statically linked Release suites on arm64 macOS on 2026-08-19.
- Added strict macOS artifact and required SF3-load gates; all nine registered Release tests pass, including architecture, deployment target, signatures, dependencies, embedded paths, DLS, and SF3.
- Made runtime DLS loading a non-skippable strict-release gate: macOS uses the system DLS, while Windows requires a private corpus DLS probe.
- Documented and packaged the exact safe DLS RIFF-size repair boundary, rollback rules, non-goals, and temporary-file lifecycle.
- Installed the exact local Release AU and passed `auval -v aumu Jc16 Pkst` on arm64 macOS 26.5.2.
- Added deterministic macOS AU/VST3 archive staging with artifact-specific notices, internal/external hashes, dirty/ad-hoc labelling, post-extraction manifest verification, and repeated metadata/portability checks.
- Added GitHub Actions CI covering documentation links, a macOS Debug build, sanitized offline harnesses, and the strict portable macOS Release gate including `auval` and candidate packaging; the Windows VST3 job is explicitly non-gating until its toolchain is validated.
- Added `tools/ci_gates.sh` so every CI gate is reproducible locally, and made the workflows call it rather than duplicating commands.
- Added `JUICYSF_WARNINGS_AS_ERRORS`, applied per source file to first-party translation units only, and marked JUCE's bundled Steinberg VST3 SDK as a system include so third-party warnings are no longer attributed to Juicy16.
- Passed strict `auval -strict -q -v aumu Jc16 Pkst` against both the current strict-Release AU and the copy extracted from its staged package.
- Reproduced the strict Release candidate byte-for-byte from an independent clean copy at a different path with a freshly built dependency closure — identical AU, VST3, and archive hashes.
- Added audio-domain sample-accuracy coverage: two GM programs render measurably different waveforms for the same note, and a Program Change one sample before a note renders identically to the same change at block start, proving the timestamp is honoured by synthesis rather than only recorded.
- Added four macOS bookmark-restore regression scenarios covering unresolvable bookmarks, the audible recovered bank, a missing fallback path, and a bookmark resolving to an unloadable file; the last was confirmed to fail against the pre-fix code.
- Added SysEx dispatch-boundary regression scenarios: an unknown SysEx on either dispatch route is forwarded without reasserting programs, and a framed GM reset dispatched from buffer storage still reasserts the current program.

- Removed the last avoidable audio-thread allocation: `processBlock` copied every MIDI event through `MidiMessage`, which heap-allocates above four bytes, so each SysEx allocated in the real-time path — and game rips carry a GM/GS/XG reset at tick 0. SysEx now dispatches directly from the `MidiBuffer`'s storage.
- Fixed a read-after-free in the macOS security-scoped bookmark path: the resolved path was held in a `StringRef` bound to a temporary `String`, so every bookmark-based bank load read freed memory.
- Fixed saved sessions losing their bank when a bookmark resolved to a file that no longer loads. Resolution succeeding was treated as success outright, so the stored path was never tried; the load result is now honoured and the stored path retried.
- Recorded `CFURLCreateByResolvingBookmarkData`'s stale flag as the runtime-only `bookmarkStale` font-state property. Automatic refresh is intentionally deferred until it can be exercised in a sandboxed host.
- Refused release configuration from a path containing whitespace. pkg-config emits unquoted `-L` flags, so a space silently discarded the pinned dependency prefix and resolved FluidSynth from Homebrew, producing a non-portable candidate. Strict CMake configuration, the dependency recipe, and the release gate now fail with an explicit message.

- Documented supported bank formats, the DLS repair boundary, the one-stereo-output limit, and per-channel Bank Select/Program Change behaviour in the support matrix, with the evidence backing each claim.
- Quarantined the legacy LLVM-MinGW cross-build with an explicit notice, dropped dead VST2 SDK ignore rules, and stopped a stray FluidSynth renderer output from being committable.
- Added an offline game-rip regression: a real multichannel MIDI file is played end to end and every channel must reach the instrument its own Program Change selected, with no manual patch assignment. Registered per bank format against a configured private corpus.
- Added a performance and resource probe covering load time, render throughput, repeated bank loads, processor and editor lifecycles, and concurrent instances, with thresholds and severities published in `docs/PERFORMANCE.md`.
- Added hostile-input coverage for zero-byte, truncated, oversized, read-only, and concurrently removed banks.
- Added computed WCAG contrast checks and keyboard-focus assertions to the headless UI suite.
- Settled cents-level RPN 0,0 bend range by measurement: FluidSynth 2.5.5 honours the Data Entry LSB, which the documentation previously declined to claim.
- Added `docs/DEPENDENCIES.md` recording the macOS release closure, a version-currency review, and the parsing attack surface.
- Added extraction-safety, executable-permission, and binary string-scan gates to the macOS packager, plus configure-time rejection of a second architecture and of an unresolvable code-signing identity.
- Added installation instructions, including macOS Gatekeeper handling for an unnotarized beta, and a tester-submitted asset handling policy.

### Required before public Beta 1

See [MILESTONE_PLAN.md](MILESTONE_PLAN.md). Major remaining gates include final qualified license/package review, a licensed SF3 corpus fixture, a first hosted CI run, real-host validation, the Windows MSVC pipeline and DLS proof, Developer ID/notarization policy, minimum-OS runtime checks, and clean-machine installation.
