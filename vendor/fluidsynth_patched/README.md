# Juicy16 CC1 vibrato extension — FluidSynth 2.5.7

`cc1-vibrato-scale.patch` is a local LGPL-compatible modification to FluidSynth, not an upstream API. It adds `fluid_synth_set_cc1_vibrato_scale(synth, channel, scale)` (1–24). Only modulator contributions with CC1 as either source and pitch LFO depth as destination are multiplied, once, after source curves and transforms. CC1 remains unchanged. Constant depth, pressure-only vibrato, filter/volume destinations and explicit bank zero overrides remain intact. The setting survives MIDI resets and the API updates held voices under FluidSynth's normal lock.

`apply.cmake` applies the same exact edits on macOS and Windows, verifying every source file's SHA-256 before and after. It accepts an already-patched tree but rejects unexpected bases/results. The unified diff is the reviewable equivalent. Both dependency recipes invoke this script before compiling. Rebuild the dependency prefix; stock FluidSynth cannot supply this API. The plugin deliberately fails to compile against an unpatched header.

Upstream source: https://github.com/FluidSynth/fluidsynth/tree/v2.5.7
Upstream archive SHA-256: `ce27840221ab00dd59bf27e85ecbba480c6c2a7c9fbec4243658f68f59c07f4a`.

This changes depth only. It does not reconstruct modulation rate, delay, type, or other DS sequence data omitted by an exporter. Windows runtime and hardware/game-reference comparisons remain unverified.
