# Roadmap and release status

Local performance work on 2026-09-12 removes redundant mixer clearing, pointer
setup, meter calculations, RPN scans and rack repaints. Three alternating Release
benchmarks measured 36–45% lower processing time for dense automation at 512/1024
frames; ordinary playback gains were smaller. Audio and diagnostics are
byte-identical to the original across an 80-case matrix; 82 buffer-equivalence
cases, all 17 Release tests, all 15 Debug tests and 3/3 ASan/UBSan checks pass,
including AU/VST3 smoke tests in both build modes. This update is now published
in the refreshed Beta 4 BC2 package. Fresh strict Release validation passes all
17 tests; extracted AU/VST3 smoke tests and strict AU validation pass. Existing
timing, leak and DAW-validation gaps remain open.

## Latest prerelease — 0.6.1-beta.4, refreshed 2026-09-12

The [published prerelease](https://github.com/PokestirVGM/Juicy16/releases/tag/v0.6.1-beta.4)
contains the validated BC2 macOS AU/VST3 ZIP and SHA-256 sidecar, built from
`17b3c1e`. BC2 replaces BC1 on the same release page, and the tag now points to
the updated source. The extracted AU and VST3 both pass their host smoke tests.
The complete extracted AU matches the temporarily installed copy used for the
strict AU validation pass; the prior installation was then restored. The
September 12 hosted CI run (34679396901) has a failed macOS Debug job; it is not
a green hosted gate. The original September 6 preparation evidence follows: The dependency recipe
now links the FluidSynth 2.5.7 CLI's codec closure through the C++ linker, all
macOS CI build modes use the required patched dependency, and the package
includes the linked FluidSynth patch documentation. Fresh local Debug passes
14/14 and ASan/UBSan 2/2; strict Release passes 16/16 and strict AU validation
passes on an identical installed bundle. The leak gate is not green: the engine
harness reports 2 allocations / 32 bytes in FluidSynth's bank-unload thread path,
and the AU harness reports 4 CFString allocations / 192 bytes in JUCE parameter
setup. A new hosted CI pass, leak resolution, and the FL Studio/Cubase checks
remain open. The owner explicitly authorized this experimental prerelease on
2026-09-06 despite the documented leak, timing, CI and host-validation gaps;
publication does not mark those checks as passed.

Playback reliability work adds FluidSynth 2.5.7, explicit reset policies, immediate controller/project recall, independent channel audio trims, and MIDI/audio/fallback/overload diagnostics. Global chorus now shares the effects panel with reverb, with explicit controls and MIDI CC93 routing. The interface cleanup adds readable effect values, a master peak meter, aligned channel diagnostics and properly synchronized row layout. Per-channel CC1 vibrato strength adds a ×1–×24 multiplier without rewriting MIDI values. State advances to schema 9 with migration from older saves. The internal MIDI player, separate host outputs and per-channel reverb-send controls remain out of scope.

The local beta.4 AU and VST3 were refreshed on 2026-09-06 after the CC1 controls moved beside pitch controls in MIDI settings, with an explicit channel picker and live received-CC1 readout. Strict Release passed 16/16 tests and both installed bundles passed their format smoke tests. A channel-1 render of the private Shitenno MIDI/SF2 confirms ×24 changes the audio after its first nonzero CC1 at 17.052606 seconds, with identical audio beforehand; this does not establish hardware parity or FL Studio controller delivery. This work is included in the published beta.4 prerelease. FL Studio and Cubase replay/save/reopen validation remains open. Audio-onset tests expose an existing FluidSynth limit of up to 63 additional engine samples; sample-accurate synthesis is still an unresolved requirement.

## Where the project is

**`0.6.0-beta.1` — Beta 1, released.** macOS 11 or later on Apple Silicon, AU
and VST3, ad-hoc signed. All four gate items below are met.

Gate item 1 was re-established rather than inherited: the gates were re-run from
a clean checkout at a whitespace-free path on 2026-08-24, which is how three
silent failures were found — the strict Release build linked no FluidSynth, its
per-archive dependency validation was passing over an empty list, and a clean
clone could not package at all. All three are fixed and recorded in
`CHANGELOG.md`. Results from that checkout: docs link closure, Debug 13/13, ASan
2/2, `leaks` clean across four harnesses, strict Release 15/15, and
`distribute/bundle_macos.sh` producing an archive that passes its own
revalidation including link closure over the staged package.

Gate item 2 was the owner's pass in FL Studio and Cubase. It found two real
defects, both fixed before the tag: pitch-bend range was wrong in Cubase because
VST3 delivers each CC as a separate host parameter, so an RPN Data Entry could
overtake the selector it belonged to; and selecting an accent only half-applied
it, because controls cache their colours. Per-channel Program Change and
pitch-bend range are both confirmed working in FL Studio and Cubase, in AU and
VST3.

Four more fixes landed after the first tag was cut, so the tag was re-cut on the
commit that carries them: the two failures the first hosted CI run found, the
double-clickable installer, the rewritten install procedure, and a linked-
dependency check that had been parsing nothing. A licensing review of the
packaged candidate then found the package claiming an Independent JPEG Group
notice it did not contain; the notice now ships. Gate item 1 was re-run from a
clean checkout on the final commit: docs link closure over 44 links, Debug 13/13,
ASan 2/2, `leaks` clean across four harnesses, strict Release 15/15, and
`Juicy16-0.6.0-beta.1-BC4-macos-arm64-ADHOC.zip` passing its own revalidation,
including link closure and full artifact re-validation over the extracted
package. `ADHOC` is expected: Beta 1 is ad-hoc signed by decision.

## Beta 1 scope

macOS 11 or later on Apple Silicon (`arm64`), AU and VST3.

Windows VST3 (Windows 10 1607+, `x86_64`) is **Beta 2**. The cross-build pipeline
has never produced a host-validated artifact, and holding an otherwise-ready
macOS beta behind it was the wrong trade.

Standalone is a development and QA build, not a release format. VST2 is out of
scope and is not built.

## The Beta 1 gate — all met

1. **`ctest` is green** and `distribute/bundle_macos.sh` produces a package that
   passes its own validation. ✔
2. **Real material played through AU and VST3, in FL Studio and Cubase.** ✔
3. **[docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md) is current**, including what
   (2) found. ✔
4. **Tag, package, send.** ✔ tagged and packaged; sending is the owner's step.

## Open after Beta 1

- **Clean-machine installation.** Every install so far has been on the
  development machine. A tester doing this is the first real test of the static
  dependency closure.
- **macOS 11 itself.** Every binary declares `minos 11.0` and validation proves
  it, but nothing has booted this on 11.
- **Logic Pro and any host beyond FL Studio and Cubase.**
- **What Beta 1 testing turned up**, as of 2026-09-02:
  - *Pitch-bend range wrong in Cubase, intermittently* — three defects found and
    fixed offline for `0.6.1` (same-timestamp ordering under VST3, the RPN Null
    regression from the Beta 1 fix, and a reset undoing the range on replay).
    Re-confirmation in Cubase is the open step. See `CHANGELOG.md`.
  - *Pitch bends weak in FL Studio* — FL's MIDI import squashes every bend to
    plus or minus two semitones. `0.6.1` adds a bend scale and a bend range
    override in the settings popover. Whether ×6 is the right recipe for FL
    is for a tester with FL to say.
  - *Some channels louder than they should be* — root cause found and fixed
    in `0.6.1-beta.2`: hosts send Reset All Controllers on stop, which
    returned every CC11-attenuated echo channel to full expression on replay.
    Not yet re-heard in a DAW. See [docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md).
  - *Too quiet, and rips sound slightly dull* — two defects found and fixed in
    `0.6.1-beta.3`. A reset SysEx was resetting the interpolation quality on
    every channel, so no rip ever played at the 7th-order method the plugin
    asks for; and the master trim's own +1.5 dB default never reached the
    audio. Neither closes the deliberate level gap to VGMTrans below. Not yet
    re-heard in a DAW.

## Open, deliberately deferred

- **Loudness parity with VGMTrans.** Juicy16 renders about 8.6 dB quieter than
  VGMTrans plays the same material, once `0.6.1-beta.3` restores the +1.5 dB
  default trim that was never being applied. The trim is the most the test
  corpus allows without clipping — the loudest of 24 rips peaks at -1.61 dBFS.
  Closing the rest needs a limiter, because VGMTrans reaches its loudness by
  running past full scale: measured across ten rips it peaks over 0 dBFS on six
  of them, up to +7.6 dBFS. Matching it by turning Juicy16 up would reproduce
  that clipping, and a limiter is a real design change for a plugin whose claim
  is faithful playback.
- **Per-channel reverb sends.** The reverb is global for Beta 1; incoming CC91
  still drives each channel's send from the MIDI file.
- **Sandbox entitlements.** Declared in the build but discarded by the post-build
  re-sign. Inert today; see [docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md).
- **`WebKit` is linked but unused.** Low severity; see
  [docs/DEPENDENCIES.md](docs/DEPENDENCIES.md).

## Beta 2

- Windows VST3: a supported MSVC build, a repaired build context, packaging, and
  host validation including Cubase.
- CI coverage for Windows.
- Whatever Beta 1 testing turns up.

## Decisions that shaped the current design

| Decision | Why |
| --- | --- |
| Apple Silicon only for Beta 1 | Focuses validation on available hardware. Intel macOS deferred. |
| Windows moved to Beta 2 | The toolchain has never produced a host-validated artifact. |
| Ad-hoc signing for Beta 1 | Developer ID and notarization need a paid account and add a release step. Proportionate for a small beta; the Gatekeeper workaround is documented in the tester guide. |
| Reverb off by default | Juicy16's reverb was inaudible before `0.6.0-alpha.1` because the effects buses were discarded. Enabling it for everyone would change how existing projects sound without being asked. |
| Mute wins over solo | Under the alternative, pressing mute on the only soloed channel does nothing. |
| Volume and pan are real host parameters | So a host can automate any channel, and a right-click on a knob offers the host's own automation menu. |
| GS/XG reverb macro SysEx ignored | The reverb is a plugin setting a MIDI file cannot reprogram. Per-channel CC91 sends still reach the engine. |
| JUCE used under AGPLv3 | No commercial JUCE licence is claimed. See [docs/LICENSING.md](docs/LICENSING.md). |

## Compatibility surfaces that are frozen

Parameter IDs, their order and ranges, the VST3 unit and program-list identity,
and the state schema version are recorded in
[docs/COMPATIBILITY.md](docs/COMPATIBILITY.md). Changing any of them
breaks saved projects and needs a migration.
