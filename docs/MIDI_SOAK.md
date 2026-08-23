# Randomised MIDI soak

The Phase 8.7 robustness pass fuzzes malformed **files** and **state blobs**.
Neither covers the MIDI input path, which is the surface a game rip actually
drives and the only place an arbitrary SysEx payload reaches Juicy16's own
parser. `JuicySFEngineMidiTests --midi-soak` closes that gap.

```bash
# The deterministic CI gate (also registered as the engine_midi_soak CTest):
build/JuicySFEngineMidiTests --midi-soak /path/to/bank.dls 2000 1

# A wider local sweep:
for seed in $(seq 1 40); do
  build/JuicySFEngineMidiTests --midi-soak /path/to/bank.dls 200000 "$seed"
done
```

## What it generates

The domain is deliberately **well-formed but adversarial**. A host hands the
plugin messages JUCE has already parsed, so malformed status bytes are not the
plugin's contract; random values, random channels, random in-block timestamps,
hostile orderings, and arbitrary SysEx payloads are.

Each block picks a random size from 32, 64, 128, 256, 512, and 1024 and emits up
to 40 events across all 16 channels: notes, CC0–CC123, Program Change, 14-bit
pitch bend, channel and polyphonic pressure, and SysEx. SysEx is weighted to
include the real GM, GS, and XG reset payloads, **near misses** one bit away from
them, and arbitrary random payloads.

The full CC0–CC127 range is covered, channel-mode messages included. They were
excluded at first, because CC124–127 disabled MIDI channels and broke the
invariants below for what was then a real defect. The engine now restores its
16-channel layout after each one, so they are back in the generator and the
invariants cover them. See [CONTROLLER_SUPPORT.md](CONTROLLER_SUPPORT.md).

## Invariants, checked after every block

1. Every rendered sample is finite and within an amplitude ceiling. A denormal
   storm, a runaway filter, or an uninitialised voice shows up here first.
2. Every channel reports a program: bank within 0–255, preset within 0–127.
   The ceiling is 255 rather than 128 because a drum channel adds FluidSynth's
   128 drum offset on top of the Bank Select MSB. That was a known B2 when this
   invariant was written; since `0.5.1-alpha.6` every surface carries the same
   0–255 range, so the invariant and the parameter finally agree.
3. Every channel's pitch bend stays within 0–16383.
4. Total sounding voices never exceed the 512-voice ceiling.
5. Saved state stays serialisable, with every channel's bank and preset in the
   same 0–255 / 0–127 range. Checked periodically, being the expensive one.
   Requiring 0–128 here while allowing 0–255 above made the invariant contradict
   itself, which a 40-seed sweep caught at seed 16, block 58880.
6. After All Sound Off on all 16 channels, no voice is still running.

The bank is required to still be loaded at the end, and the run reports peak
amplitude, peak voices, and the program-apply failure mask.

## Reproducers

A fuzz harness that cannot name its input is a liability: the finding is
unactionable and gets ignored. The seed is an argument and the generator is
`std::mt19937`, so a run is exactly reproducible. When a block first sets a
program-apply failure bit, the soak prints that block's entire event list.

## Reading the program-apply failure mask

A non-zero mask is **not** by itself a defect. It records that FluidSynth
rejected a program change, which legitimately happens when random MIDI selects a
bank or program the loaded bank file does not contain — for example a drum
Program Change with no matching kit, or a Program Change after a Bank Select to
an absent bank. The plugin's contract is that engine, saved state, and UI stay
consistent when that happens, and the soak asserts that rather than asserting an
empty mask.

## What it found

Two defects on its first run, both now pinned by deterministic tests in the
engine suite and recorded in [KNOWN_ISSUES.md](KNOWN_ISSUES.md):

- **Channel-mode messages disabled MIDI channels.** One CC124 on channel 1 left
  only channel 1 responding until the next reset. **Fixed**: the engine now
  forwards the controller and then restores its 16-channel layout.
- **Drum-channel Bank Select exceeds the documented bank range.** CC0 on
  channel 10 reports 128 + MSB, so the XG drum convention CC0=127 reaches 255,
  which the then 0–128 `bank` parameter could not represent. **Fixed** in
  `0.5.1-alpha.6`: the parameter spans 0–255 and reload preserves it.

## Coverage to date

On 2026-08-20, against a frozen binary with the full CC0–127 range: **40 seeds of
200,000 blocks each — 8,000,000 blocks, roughly 156 million MIDI events, about
15.6 million of them SysEx — with zero failing seeds.** Separately, 40,000 blocks
under ASan+UBSan rendering 782,123 events with no sanitizer findings.

## Limits

It is a single-threaded offline harness. It does not model a host's audio and
message threads racing, transport jumps, or sample-rate changes mid-stream, and
it renders faster than realtime, so it proves nothing about scheduling.
