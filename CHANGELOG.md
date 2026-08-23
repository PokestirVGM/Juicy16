# Changelog

## 0.5.1-alpha.5 — unreleased

Fixes the "dynamics sound wrong" report. Two independent causes, both found by measurement rather than inspection, and both confirmed with `tools/dynamics_probe.cpp` (new: it renders the plugin and a stock FluidSynth loaded with the same bank side by side, so a level or timbre difference can be attributed instead of guessed at).

### Fixed

- **The plugin had no headroom.** `synth.gain` was 1.0, five times FluidSynth's documented default of 0.2. Measured on a real 52-second game rip, that peaked at **+7.32 dBFS with 0.39% of samples past full scale**; a four-note chord on all 16 channels reached +20 dBFS. Once anything downstream clips, quiet notes rise relative to loud ones, CC7 automation stops doing anything above the ceiling, and hard clipping collapses the L/R difference that carries pan — which is every symptom reported. The same rip now peaks at **-6.66 dBFS with zero samples over full scale**. FluidSynth documents 0.2 as low deliberately "to avoid the saturation of the output when many notes are played", and their own attempt to raise the default to 0.6 in 2.4.0 drew clipping reports; Juicy16 was at 1.0.
- **Removed the CC71-79 modulators, which no other SoundFont player applies.** Stock FluidSynth ignores those controllers entirely — measured, its time-to-peak was constant at 50.2 ms for every value of every one of them. Juicy16 mapped them onto filter and volume-envelope generators at ±12000 timecents and ±960/1000 centibels, and the result was wildly out of scale: CC73=127 stretched attack from 50 ms to **868 ms**, CC75=127 raised a note's tail by **43 dB**, CC72=127 left a note ringing **48 dB** above neutral a second after note-off, and CC71=127 attenuated the signal by **46 dB**. Below 64 most of them did nothing at all, because the bank's own generator value was already at the spec limit. On DLS banks all six were inert, because FluidSynth's native DLS loader does not apply the default modulator list — so the same rip behaved differently depending on bank format. GS/XG game rips routinely send these controllers, which is why material sounded flat and compressed in this plugin and nowhere else. Every CC still reaches the synth; there is simply no Juicy16-specific modulator listening for it.

### Changed

- **The six sound-control sliders are replaced by Volume, Pan, and Output Level.** The retired sliders drove the modulators above, so with those gone they would have done nothing. Volume and Pan edit the selected channel's CC7/CC10 and follow the same rule as the instrument dropdowns: what you set is a starting point, and the next CC7/CC10 on that channel replaces it at the event's timestamp and moves the slider. Output Level is a master trim in decibels (-24 to +12, default 0), applied after rendering with 20 ms smoothing so host automation cannot step the gain mid-block; it is not a MIDI controller, so nothing in a MIDI file moves it.
- Parameter set: 24 to 21 (`bank`, `preset`, `volume`, `pan`, `outputLevel`, `progCh1`-`progCh16`). State schema: version 2 to **3**. A pre-v3 save restores its bank and per-channel instruments; the retired sound-controller values are ignored rather than migrated, and those channels open at the GM defaults (volume 100, pan centre). Pre-Beta session compatibility was already explicitly waived. `docs/BETA1_IDENTITY_CONTRACT.md` carries the new frozen VST3 ParamIDs.

### Fixed (packaging)

- **Both packagers had stopped reading the prerelease label.** They parsed it with a regex written for the `set(JUICYSF_PRERELEASE_LABEL "..." CACHE STRING` line that alpha.4 replaced, so the match silently yielded nothing and the candidate was named and stamped `0.5.1` — a release-looking version — while the binary inside it displayed `0.5.1-alpha.5`. The parse now targets `JUICYSF_PRERELEASE_LABEL_DEFAULT` and *fails* rather than defaulting to empty if that line cannot be read. `release_metadata_consistency` no longer merely prints the display version it is handed: it extracts the version string compiled into the shipped AU and requires the caller's to match, which is what would have caught this. Confirmed to fail on the bare `0.5.1` and pass on `0.5.1-alpha.5`.

### Security

- **The libsndfile CVE patch is now built and linked, not just written.** alpha.4 could not download the pinned tarballs, so the hardened `ircam.c` had never been compiled into anything. The closure was rebuilt on 2026-08-23 with every checksum and both `src/ircam.c` hashes verified, and the strict portable Release gate passed on it: 15/15 CTests including `font_load_release_sf3`, the SF3 path that actually reaches libsndfile. The AU packaged from that build passes `codesign --deep --strict` and `auval -strict -q -v aumu Jc16 Pkst`.

## 0.5.1-alpha.4 — unreleased

Keyboard operation of the whole editor, and the libsndfile advisory found in the previous alpha's dependency review is now patched rather than documented. Still an alpha for the same reasons: no DAW host has run this build, no Windows artifact exists, and no hosted CI run has occurred.

### Fixed

- The version shown in the status bar could be stale. `JUICYSF_PRERELEASE_LABEL` was a plain CMake `CACHE` default, which is written once and then ignored, so bumping the label in `CMakeLists.txt` changed nothing for a build directory that already existed. `build-ci-debug` was still producing and displaying `0.5.1-alpha.2` while every document said `alpha.3`, and the metadata test could not catch it because it compares the artifact against the same stale cache value. Since the status-bar version is how you tell a rebuilt plugin from a DAW's cached copy, a stale one is worse than none. The label is now a normal variable that `-DJUICYSF_PRERELEASE_LABEL=...` still overrides.

### Added

- **Channel and instrument selection work without a mouse.** The 16-channel table now takes keyboard focus, up and down arrows select the MIDI channel the sliders and status line follow, and Return opens the selected row's instrument dropdown as an ordinary keyboard-driven menu. The table previously declined focus, justified as stopping arrow keys from fighting MIDI-driven row selection — but nothing drives that: incoming MIDI changes a channel's *program*, not which row is selected. `uiState.selectedChannel` remains the single source of truth, with a re-entrancy guard breaking the two-way notify loop, and restored state opens on the channel it was editing rather than row 0. That closes the last mouse-only workflow; bank loading and the six sound sliders were already reachable. The headless editor suite drives all 16 rows, including rows scrolled out of view at the minimum window height.
- The [tester guide](docs/BETA_TESTER_GUIDE.md) documents the keyboard workflow and asks for reports on hosts that swallow Tab before it reaches the editor.
- **Importable MIDI fixtures and a host test protocol**, which Phase 7 needed and did not have. Its 35 host-matrix items say to "run the canonical CC/pitch-bend fixture" and "record the bank/program on all 16 channels at defined checkpoints" — but that fixture was a CSV of sample offsets only the offline harness can replay, and the sole `.mid` in the repository is the private, non-redistributable game rip. There was nothing to import into a DAW. `tests/fixtures/host/host_program_matrix.mid` now drives 16 independent Program Changes at three checkpoints, with a note on the same tick as each change; `host_controllers.mid` walks CC, 14-bit bend, per-channel RPN bend range, pedals, and channel mode one marked bar at a time, at 120 BPM so a bar is exactly two seconds. Both are original General MIDI content and redistributable.
- [docs/HOST_TEST_PROTOCOL.md](docs/HOST_TEST_PROTOCOL.md) tabulates the expected instrument on all 16 channels at every checkpoint and the expected observation for every controller step, plus per-host routing setup and a fill-in results template. Three new tests keep it honest: the two fixtures are played through the engine against the platform's system GM bank, and the committed `.mid` files are pinned byte-for-byte to their generator, so a host that disagrees is the host's problem rather than the fixture's.

### Documented

- The tester guide now opens with a four-row "is your setup supported?" check — operating system, CPU, plugin format, bank format — with the commands that reveal chip and architecture on each OS, the ad-hoc signing consequence stated *before* download rather than after, and the distinction between approved scope and an actually validated host. `SUPPORT_MATRIX.md` already held the answer, but nothing a tester reads first pointed at it: the guide told them not to download an unsupported candidate without telling them how to tell.

### Security

- **CVE-2025-52194 (libsndfile) is patched instead of documented.** The previous alpha's advisory review found a reachable buffer overflow in `ircam_read_header`: FluidSynth hands an SF3's embedded sample bytes to `sf_open_virtual()` without pre-validating the format and only *warns* when the detected format is not OGG, so a crafted bank — the product's primary untrusted input — reaches the IRCAM reader. Waiting for a fixed release was ruled out; libsndfile `master` carries the fix but 1.2.2 is still the newest release. `vendor/libsndfile_patched/` backports `master`'s `psf_lrintf` conversion for the exact line the CVE names, and adds the lower channel bound 1.2.2 is missing in both the little-endian read and the big-endian retry — without it a zero or negative channel count from the file reaches `channels * bytewidth`, signed overflow for a large negative value and a later divide by zero for zero.
- Both dependency recipes bracket the edit with the pre- and post-edit `src/ircam.c` hashes and fail the build on any mismatch, so a release closure cannot be produced without the patch and upstream source drift is caught rather than patched around. Windows substitutes the same two changes directly, because it has no guaranteed `patch.exe`. A new `dependency_patch_contract` CTest pins the patch, both recipes, and the vendored README to the same three hashes.
- **Not yet verified:** the dependency closure has not been rebuilt with the patch — this machine could not download the pinned tarballs — so a patched-closure build and strict Release run remain required before candidate freeze. No crafted-SF3 proof-of-concept exists either, before or after.

## 0.5.1-alpha.3 — unreleased

Cross-bank Bank Select coverage, a FluidSynth polyphony fix found by the new dense-playback measurements, and the performance scenarios the milestone plan asks for. Still an alpha: no DAW host has run this build, no Windows artifact exists, and no hosted CI run has occurred.

### Fixed

- **Found by the MIDI soak:** a channel-mode message no longer disables MIDI channels. FluidSynth implements MIDI 1.0 basic-channel semantics faithfully, so one CC124 (Omni Off) on channel 1 left only channel 1 responding — silent and unreadable everywhere else — until the next reset, and CC126/CC127 with group size N left exactly N channels enabled. Correct for a MIDI 1.0 sound module, incompatible with a fixed 16-channel instrument. Juicy16 now forwards the controller and then restores its own layout, so every CC0–127 still reaches FluidSynth at its own sample position and there are still exactly 16 channels afterwards. Mono mode is deliberately not honoured as a per-channel monophonic setting; MIDI's basic-channel mechanism cannot express both that and 16-channel routing.
- Raised the voice ceiling correctly. FluidSynth sizes its rvoice event queue once in `new_fluid_synth`, as `polyphony * 64`, and `fluid_synth_set_polyphony` afterwards grows only the voice array. Polyphony was being raised to 512 after construction, so the queue stayed sized for FluidSynth's default 256: above roughly 256 sounding voices it overflowed continuously, dropping engine events and emitting thousands of `Ringbuffer full` warnings per second — 16,695 in a single probe run. Polyphony is now a setting applied before the synth exists.
- Made `getChannelProgram` and `getLastDispatchedNoteOnProgram` report the loaded font's own bank numbering, matching the saved channel state and the bank parameter. They previously reported the raw engine bank, which differs whenever a FluidSynth bank offset is installed.

### Added

- A synthesised multi-bank SF2 fixture (`tests/SyntheticSf2.h`), written during the test run rather than committed, with presets in banks 0, 1, 8, and 128 that each sound a different pitch. Cross-bank Bank Select is now proven in the audio domain instead of inferred: CC0 into banks 1 and 8, CC32 retained but ignored under the pinned GS mode, the return to bank 0, and channel 10's default percussion bank, with engine, saved state, editor parameters, and audio agreeing at every step.
- Bank-offset coverage. Juicy16 never installs a FluidSynth bank offset, so the raw/logical conversions in the program paths ran only at offset 0; the harness can now install one and the suite asserts that manual selection, MIDI Bank Select, saved state, editor parameters, and the sounding preset all resolve to the font's own bank numbering.
- A voice-ceiling regression test asserting that the configured and reported polyphony agree and that 512 simultaneous note-ons allocate, sustain, and fully release.
- Performance scenarios for one-channel playback, the 512-voice ceiling, and continuous Program Change and controller automation, measured across three banks and recorded in [docs/PERFORMANCE.md](docs/PERFORMANCE.md).
- A randomised MIDI soak (`JuicySFEngineMidiTests --midi-soak`, CTest `engine_midi_soak`, [docs/MIDI_SOAK.md](docs/MIDI_SOAK.md)). The existing fuzz pass covered malformed files and state blobs; nothing covered the MIDI input path, which is what a game rip actually drives and the only place an arbitrary SysEx payload reaches Juicy16's own parser. It asserts finite bounded audio, in-range program and pitch-bend state on all 16 channels, the 512-voice ceiling, serialisable saved state, and that All Sound Off leaves nothing running — then prints the offending block's whole event list, because a reproducer-less fuzz finding is unactionable. 40 seeds of 200,000 blocks — roughly 156 million MIDI events — with zero failures, plus 782,123 events under ASan+UBSan with no findings.
- A `leaks` quality gate (`tools/ci_gates.sh leaks`, CI job `macos-leaks`). LeakSanitizer is unavailable on Darwin arm64, so the sanitizer gate runs with leak detection off; every offline harness now also runs under macOS `leaks -atExit` and must report zero leaked bytes, which covers the Core Foundation objects the security-scoped bookmark path owns. All four harnesses are clean.
- Editor-view coverage in the VST3 harness: the view is created through `IPlugView` without a window, and the suite checks the platform type, the 500x547 default size, that nonsense host size requests are constrained to 500x300 / 1216x1000 rather than accepted, and what `setContentScaleFactor` answers. On macOS it answers `kResultFalse` — correct, because the window server applies the backing scale factor — and the test asserts that platform-specific answer rather than a portable one.
- Audio-domain proof that a VST3 `progChN` automation point takes effect at its own in-block sample. Four isolated renderings show two programs correlating at -0.0364 on the same note, a switch one sample before the note matching the new program at 1.0000, and a switch one sample after it leaving that note on the old program at 1.0000. JUCE's ordinary parameter collapse would have failed the last case, so the vendored wrapper's timestamp preservation is now proven by audio rather than only by state readback.
- An in-process AU host harness (`tools/au_smoke.cpp`, CTest `au_host_smoke`). It registers the built component from its own bundle, so nothing is installed and an already-installed copy cannot be tested by mistake, then drives it through `MusicDeviceMIDIEvent` and `AudioUnitRender`: the frozen component description, instantiation and stream format, the 24 published parameters, per-channel Program Change and its parameter mirror, independent audio on all 16 channels, ClassInfo save/restore, disposal, and reinstantiation. AU coverage was previously `auval` only.

### Windows (all unproven — the recipe has never run)

- `tools/verify_windows.ps1`: one-shot verification for a real Windows machine. Dependency closure, configure, build, tests, the DLS capability probe against `C:\Windows\System32\drivers\gm.dls`, the `Contents/x86_64-win` module layout, `dumpbin /headers` and `/dependents`, and artifact hashes — all into a single pasteable report. It deliberately does not stop at the first failure, because nothing in this path has ever executed and a run that stops early wastes the trip.

- Replaced the Windows CI job's `vcpkg install fluidsynth:x64-windows` with `tools/build_windows_dependencies.ps1`, a checksum-pinned recipe building the same components, versions, and configuration as the macOS closure. The vcpkg install was never an approved dependency source: unpinned version, and not configured for FluidSynth's native C++17 DLS loader, so it may have produced a Windows plugin that built and ran but refused every DLS bank.
- Windows links the static MSVC C runtime for both the closure and the plugin, so the VST3 needs no Visual C++ redistributable. CMake derives the plugin's setting from `FLUIDSYNTH_LINK_STATIC` under MSVC, because a mismatch is a link failure rather than a silent difference.
- Windows finds FluidSynth through its CMake package config instead of pkg-config (`JUICYSF_FLUIDSYNTH_CMAKE_CONFIG`, default `ON` under MSVC). The path is exercised on macOS too — a static build through it passed all eight registered tests — so it is not Windows-only code that nothing has run. macOS release validation still refuses it, because its per-archive architecture and deployment-target checks need pkg-config's static link list.
- `font_load_system_dls` now runs on Windows against `C:\Windows\System32\drivers\gm.dls`, so DLS capability is proved at runtime rather than inferred. The dependency recipe separately fails if FluidSynth built without the native DLS loader.
- Rewrote [building.win32.md](building.win32.md) from a status note into the pinned recipe, with an explicit list of what remains before it counts as a release procedure.

### Documented

- Beta 1 ships **ad-hoc signed** by decision — no Developer ID, no notarization. Rewrote the tester guide's Gatekeeper section accordingly: it now distinguishes the expected `ADHOC` package label from the never-install `LOCAL-DIRTY` label (the guide previously told testers not to install either, which would have excluded every Beta 1 package), tabulates the four symptoms of skipping the quarantine step including the misleading "is damaged and can't be opened", gives the command to confirm it worked, and explicitly refuses to recommend disabling SIP or Gatekeeper.

- **Found by the MIDI soak:** Bank Select on channel 10 reports a bank the UI cannot show. A drum channel adds FluidSynth's 128 offset on top of the Bank Select MSB, so the XG drum convention CC0=127 reports bank 255. Engine and saved state record it while the 0–128 `bank` parameter keeps its old value, and a reload moves the channel back to 128. The audio is unaffected — the substituted kit measures 1.0000 waveform correlation — so this is a state and UI inconsistency, not an audible defect. Recorded as B2; widening the parameter range would move a frozen compatibility surface.
- Selecting a bank or program the loaded bank file does not define is accepted, not refused: FluidSynth records the request and substitutes bank 0 program 0 for synthesis, so the visible patch and the audible one differ while the wrong bank is loaded. Juicy16 keeps the requested value on purpose, so reopening the project with the intended bank plays what the MIDI asked for. Recorded as a B2 limitation.
- The pinned SF3 test fixture defines only bank 0 and percussion bank 128, so SF3 Bank Select is proven for percussion-versus-melodic selection only.

## 0.5.1-alpha.1

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
