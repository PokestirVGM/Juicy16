# Roadmap and release status

## Where the project is

`0.6.0-alpha.3`. The engine, build and automated gates are in good shape. Beta 1
has not shipped, so the version is deliberately not labelled beta.

## Beta 1 scope

macOS 11 or later on Apple Silicon (`arm64`), AU and VST3.

Windows VST3 (Windows 10 1607+, `x86_64`) is **Beta 2**. The cross-build pipeline
has never produced a host-validated artifact, and holding an otherwise-ready
macOS beta behind it was the wrong trade.

Standalone is a development and QA build, not a release format. VST2 is out of
scope and is not built.

## The Beta 1 gate

Beta 1 ships when all four are true.

1. `ctest` is green and `distribute/bundle_macos.sh` produces a package that
   passes its own validation.
2. Real material has been played through both AU and VST3, in FL Studio and
   Cubase. Actual use, looking for anything that feels wrong — not a checkpoint
   matrix.
3. [docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md) is current, including anything
   found in (2).
4. Tag, package, send.

## Open before Beta 1

- The owner's pass through FL Studio and Cubase (gate item 2).
- Clean-machine installation, which a tester will do after release rather than
  as a gate before it.

## Open, deliberately deferred

- **Loudness parity with VGMTrans.** Juicy16 renders about 10.4 dB quieter than
  VGMTrans plays the same material. The default master trim is +1.5 dB, which is
  the most the test corpus allows without clipping — the loudest of 24 rips peaks
  at -1.61 dBFS. Closing the rest needs a limiter, which is a real design change
  for a plugin whose claim is faithful playback. See
  [docs/INVESTIGATION_ENVELOPE_DECAY.md](docs/INVESTIGATION_ENVELOPE_DECAY.md).
- **Chorus.** FluidSynth's chorus is switched off rather than left at an unchosen
  default. It needs controls of its own before it is turned on.
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
[docs/BETA1_IDENTITY_CONTRACT.md](docs/BETA1_IDENTITY_CONTRACT.md) and
[docs/STATE_COMPATIBILITY.md](docs/STATE_COMPATIBILITY.md). Changing any of them
breaks saved projects and needs a migration.
