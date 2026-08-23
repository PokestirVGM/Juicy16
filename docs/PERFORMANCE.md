# Performance baseline and limits

What Juicy16 costs, measured, so a tester can tell an expected limit from a regression. Reproduce with:

```bash
ctest --test-dir build -C Debug -R performance_baseline
# or directly, against any bank:
build/JuicySFPerfProbe /path/to/bank.dls
```

`tools/perf_probe.cpp` measures load time, render throughput, and **current** resident memory (not peak, which never falls and so cannot show a release) across five representative loads — one channel, sixteen channels, the 512-voice ceiling, continuous program-change and controller automation, and eight concurrent instances — plus repeated bank loads, processor lifecycles, and editor lifecycles.

## Reference measurements

Apple M-series arm64, macOS 26.5.2, Debug build, 48 kHz, 2026-08-20. A Debug build is the pessimistic case; Release is faster.

| Bank | Size | Load time | Resident after load |
| --- | --- | --- | --- |
| macOS system GM DLS | 1.9 MB | 6 ms | +17 MB |
| Corpus game-rip SF2 | 1.5 MB | 1 ms | +11 MB |
| Awave-style DLS (repaired) | 25 MB | 18 ms | +92 MB |

Render cost per scenario, at a 64-sample block, as a share of realtime:

| Scenario | System DLS | Corpus SF2 | 25 MB DLS |
| --- | --- | --- | --- |
| One channel, one note | 0.1% | 0.1% | 0.1% |
| Sixteen channels, four-note chords | 1.2% | 1.2% | 1.1% |
| 512 simultaneous note-ons | 8.6% | 9.1% | 1.5% |
| Sixteen channels plus 112 automation events per block | 11.5% | 12.6% | 11.5% |
| Eight concurrent instances, sixteen channels each | 9.3% | 9.8% | 9.9% |

The automation scenario sends a Program Change, five controllers, and a pitch bend on every channel in **every** 64-sample block — 84,000 events per second, far past anything musical. It is the most expensive case measured, more so than filling the voice ceiling.

The 25 MB DLS is cheap at the voice ceiling because its presets are short and sparse; voice cost is a property of the bank's samples, not of the plugin.

Block size does not measurably affect throughput at any density, which is expected: the engine renders in per-event segments rather than per-block chunks. Across 64, 128, 256, 512, and 1024 the sixteen-channel figure stays within 1.1–1.2% of realtime.

### Voice ceiling and the rvoice event queue

The 512-voice ceiling must be configured as a FluidSynth **setting**, before the synth is created. `new_fluid_synth` sizes its rvoice event queue once, as `polyphony * 64`, and `fluid_synth_set_polyphony` afterwards grows only the voice array. Juicy16 previously raised polyphony after construction, which left the queue sized for FluidSynth's default 256 while up to 512 voices fed it: above roughly 256 sounding voices the queue overflowed continuously, dropping engine events and emitting thousands of `Ringbuffer full` warnings per second. The probe now runs the ceiling case clean, and the offline engine suite asserts that the configured and reported limits agree.

## Memory behaviour

- **Repeated bank loads** (20 alternating reloads): grows 1.6 MB (1.5 MB SF2), 7.6 MB (1.9 MB DLS), 29.7 MB (25 MB DLS). Growth is bounded and far below the multiple-of-bank-size signature of a per-load leak.
- **Processor create/destroy** (20 cycles): +0.1 MB. Flat.
- **Editor open/close**: the first costs 22–26 MB of one-time JUCE font, graphics, and window initialisation. The following 19 cycles cost **+0.2 MB total** — flat, so nothing leaks per cycle. The probe asserts the steady state separately from the first cycle, because a single before/after delta would hide a per-cycle leak behind that one-time cost.

The 25 MB DLS is the worst case because it goes through the repair path, which builds an in-memory image of the whole file. Repair is capped at 512 MB; see [DLS_REPAIR.md](DLS_REPAIR.md).

## Thresholds and severity

The probe fails, rather than merely reporting, when:

| Check | Threshold | Severity if it fails |
| --- | --- | --- |
| Render faster than realtime in every scenario, at every tested block size | 100% of realtime | B1 — unusable for its purpose |
| Dense material reaches a substantial fraction of the voice ceiling | more than 256 voices sounding | B1 — silent voice-stealing or a mis-sized engine queue |
| Eight instances render faster than realtime combined | 100% of realtime | B2 — documented instance limit |
| Repeated bank loads | growth < max(64 MB, 2× bank size) | B1 — leak in the primary workflow |
| Processor create/destroy | growth < 128 MB over 20 cycles | B1 — leak on every plugin instance |
| Editor open/close after the first | growth < 8 MB over 19 cycles | B1 — leak on every editor open |

Thresholds are deliberately generous. They exist to catch an unbounded leak, not to police allocator noise, and are not a performance contract.

## Known limits

- Measurements are macOS arm64 only. No Windows baseline exists, matching the rest of the Beta 1 Windows position.
- The probe runs offline, not under a DAW. Host buffer scheduling, plugin bridging, and other plugins in the graph are not represented.
- The voice ceiling is 512. Dense material above that drops voices by FluidSynth's own policy; that is an engine limit, not a Juicy16 defect. Not every note produces a voice — 512 note-ons across sixteen channels allocate 506 voices on the macOS system DLS, because a percussion bank has no sample on every key.
- Eight concurrent instances is the documented probe limit, not a measured maximum. The headroom implies substantially more are viable, which is deliberately not claimed without a host test.
- 512-voice and automation figures are Debug-build offline numbers. A Release build and a real host will differ.
- Rates above 96 kHz are unsupported by FluidSynth 2.5.5 and render silence by design. See [SUPPORT_MATRIX.md](SUPPORT_MATRIX.md).
- One stereo output. CPU does not scale with the number of channels used, only with sounding voices.
