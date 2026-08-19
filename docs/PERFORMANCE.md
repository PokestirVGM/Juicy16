# Performance baseline and limits

What Juicy16 costs, measured, so a tester can tell an expected limit from a regression. Reproduce with:

```bash
ctest --test-dir build -C Debug -R performance_baseline
# or directly, against any bank:
build/JuicySFPerfProbe /path/to/bank.dls
```

`tools/perf_probe.cpp` measures load time, render throughput, and **current** resident memory (not peak, which never falls and so cannot show a release) across repeated bank loads, processor lifecycles, editor lifecycles, and concurrent instances.

## Reference measurements

Apple M-series arm64, macOS 26.5.2, Debug build, 48 kHz, 2026-08-19. A Debug build is the pessimistic case; Release is faster.

| Bank | Size | Load time | Resident after load |
| --- | --- | --- | --- |
| macOS system GM DLS | 1.9 MB | 5 ms | +17 MB |
| Corpus game-rip SF2 | 1.5 MB | 1 ms | +10 MB |
| Awave-style DLS (repaired) | 25 MB | 19 ms | +91 MB |

Render cost with **all sixteen channels holding four-note chords**:

| Block size | CPU for 5 s of audio | Share of realtime |
| --- | --- | --- |
| 64 | 55 ms | 1.1% |
| 128 | 55 ms | 1.1% |
| 256 | 56 ms | 1.1% |
| 512 | 54 ms | 1.1% |
| 1024 | 54 ms | 1.1% |

Block size does not measurably affect throughput, which is expected: the engine renders in per-event segments rather than per-block chunks.

Eight concurrent instances, each with all sixteen channels sounding, render in **8.9–9.4% of realtime** combined and cost 4.8–26.7 MB each depending on bank size.

## Memory behaviour

- **Repeated bank loads** (20 alternating reloads): grows 1.7 MB (1.5 MB SF2), 6 MB (1.9 MB DLS), 28.8 MB (25 MB DLS). Growth is bounded and far below the multiple-of-bank-size signature of a per-load leak.
- **Processor create/destroy** (20 cycles): +0.1 MB. Flat.
- **Editor open/close**: the first costs ~26 MB of one-time JUCE font, graphics, and window initialisation. The following 19 cycles cost **+0.3 MB total** — flat, so nothing leaks per cycle. The probe asserts the steady state separately from the first cycle, because a single before/after delta would hide a per-cycle leak behind that one-time cost.

The 25 MB DLS is the worst case because it goes through the repair path, which builds an in-memory image of the whole file. Repair is capped at 512 MB; see [DLS_REPAIR.md](DLS_REPAIR.md).

## Thresholds and severity

The probe fails, rather than merely reporting, when:

| Check | Threshold | Severity if it fails |
| --- | --- | --- |
| Render faster than realtime at every tested block size | 100% of realtime | B1 — unusable for its purpose |
| Eight instances render faster than realtime combined | 100% of realtime | B2 — documented instance limit |
| Repeated bank loads | growth < max(64 MB, 2× bank size) | B1 — leak in the primary workflow |
| Processor create/destroy | growth < 128 MB over 20 cycles | B1 — leak on every plugin instance |
| Editor open/close after the first | growth < 8 MB over 19 cycles | B1 — leak on every editor open |

Thresholds are deliberately generous. They exist to catch an unbounded leak, not to police allocator noise, and are not a performance contract.

## Known limits

- Measurements are macOS arm64 only. No Windows baseline exists, matching the rest of the Beta 1 Windows position.
- The probe runs offline, not under a DAW. Host buffer scheduling, plugin bridging, and other plugins in the graph are not represented.
- FluidSynth's voice ceiling is 512. Dense material above that drops voices by FluidSynth's own policy; that is an engine limit, not a Juicy16 defect.
- Rates above 96 kHz are unsupported by FluidSynth 2.5.5 and render silence by design. See [SUPPORT_MATRIX.md](SUPPORT_MATRIX.md).
- One stereo output. CPU does not scale with the number of channels used, only with sounding voices.
