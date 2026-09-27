# FluidSynth 2.5.7 patches

Juicy16 builds FluidSynth 2.5.7 with two small changes. They're LGPL-2.1 like
FluidSynth itself.

- **`cc1-vibrato-scale.patch`** adds `fluid_synth_set_cc1_vibrato_scale()`,
  which multiplies only the CC1-driven part of pitch vibrato (×1–×24) on one
  channel. The CC1 value itself, other modulators and the bank's own settings
  stay the same, and it survives MIDI resets.
- **`dls-full-range-pan.patch`** gives every DLS region a CC10 pan range of the
  normal amount plus that region's own pan. Banks built from hard-left/hard-right
  sample pairs can then pan fully, and CC10=64 still leaves each region where the
  bank put it. SF2 playback is unchanged.

`apply.cmake` makes the same edits on macOS and Windows, checking every file's
SHA-256 before and after. Both dependency scripts run it, and the plugin won't
compile against a FluidSynth that doesn't have these patches. The `.patch`
files are the readable version of the same changes.

Upstream: https://github.com/FluidSynth/fluidsynth/tree/v2.5.7
(archive SHA-256 `ce27840221ab00dd59bf27e85ecbba480c6c2a7c9fbec4243658f68f59c07f4a`)
