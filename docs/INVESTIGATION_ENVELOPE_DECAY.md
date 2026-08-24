# Closed investigation: decaying instruments sound too quiet

Status: **resolved without an engine change (2026-08-23)**. Juicy16's decay is
not the source of a VGMTrans playback mismatch. VGMTrans's bundled BASSMIDI was
finally measured on the affected SF2 and uses substantially the same fast,
concave decay as FluidSynth. Its optional linear-volume mode produces the longer
tail originally proposed as a fix, but VGMTrans does not enable that mode.

The remaining audible difference against Fruity LSD is a comparison with a
different synth target (DirectMusic on the DLS), whose contour has not been
captured. Changing Juicy16 globally, or only for DLS, would therefore be an
unverified compatibility mode and would regress the stated goal of matching
VGMTrans playback. No FluidSynth patch or decay compensation was applied.

Everything below that is labelled *measured* was measured in this repository with
the tools named. Everything labelled *inferred* is not proven. One earlier
inference in this investigation was confidently wrong and nearly produced a bad
patch; see [What was disproved](#what-was-disproved).

---

## 1. The report

The owner — who works in audio and states this with certainty — reports that in
Juicy16, **some instruments are too quiet**, while others in the same piece are
correct. Named example, in `SEQ_BGM_N_CASTLE`:

- the **vibraphone** is too quiet
- the **timpani** is too quiet
- "some other channels off too"
- "some attack sounds off in other midis too"

The same material played through **Fruity LSD** sounds correct to them. This is
not subtle to them and it is reproducible.

## 2. What Juicy16 is, and the material

Juicy16 is a 16-channel DLS/SoundFont player built on **FluidSynth 2.5.5**
(pinned; `JUICYSF_RELEASE_VALIDATION` enforces the exact version). It is a fork
of juicysfplugin, and its purpose is playing game-rip MIDI + bank pairs.

The material is **VGMTrans** output: a `.mid` sequence plus a bank exported as
both `.dls` and `.sf2` from the same source data. The owner's corpus is 24 such
pairs at `~/Documents/Game MIDI Rips/Tools/VGMTrans Output`. The affected file is
`SEQ_BGM_N_CASTLE.{mid,dls,sf2}`.

VGMTrans source is available locally at `~/Documents/_Repositories/vgmtrans`.

## 3. The mechanism, isolated

The split is exact and is the single most useful fact here.

Measured on `SEQ_BGM_N_CASTLE.dls`, one note per program at velocity 100,
rendered through stock `fluidsynth` CLI at gain 0.2 with reverb and chorus off:

| program | declared EG1 decay | declared sustain | rendered to −20 dB |
|---|---|---|---|
| 10 (vibraphone) | 2.947 s | ~0% | **0.442 s** |
| 14 (timpani) | 2.947 s | ~0% | **0.603 s** |
| 38 (bass) | 4.846 s | ~0% | **1.010 s** |
| 30 | — | 99.4% | 1.092 s |
| 63 | — | 99.4% | 1.738 s |
| 75 | — | 99.4% | 1.797 s |

**Every instrument declared with ~0% sustain decays far faster than its declared
decay time suggests. Every instrument that sustains is correct.** That is why
some parts of a piece sound right and the struck ones do not — a decaying
instrument never reaches its declared tail, so it sits too low against the rest
of the mix.

### What the file actually declares

From the SF2's `igen` chunk (parsed directly):

```
attackVolEnv    raw [-9075, -5475]                   -> 0.005 s, 0.042 s
holdVolEnv      raw [-32768]                         -> 0.000 s
decayVolEnv     raw [-11959, 873, 1439, 1871, 2204, 2732]
                                                     -> 0.001, 1.656, 2.296, 2.947, 3.572, 4.846 s
sustainVolEnv   raw [0, 1, 3, 571, 1000]             -> 0.0, 0.1, 0.3, 57.1, 100.0 dB
releaseVolEnv   raw [-1173, -787, -471]              -> 0.508, 0.635, 0.762 s
```

So the vibraphone/timpani case is `decayVolEnv = 2.947 s` with
`sustainVolEnv = 1000 cB = 100 dB` (i.e. decay to silence).

The DLS carries the equivalent values in its `art1`/`art2` connection blocks.

### FluidSynth is behaving to the SF2 specification

SF2 defines the volume envelope decay as **linear in decibels**: the decay ramps
from 0 dB toward the sustain attenuation, and `decayVolEnv` is the time for a
100% change (0 → 100 dB).

Predicted time to −20 dB for the above: `2.947 × 20/100 = 0.589 s`.
Measured for program 14: **0.603 s** — within 2.4%.
Measured for program 38: predicted 1.009 s, measured **1.010 s** — exact.

So FluidSynth is implementing the SF2 model correctly, and Juicy16 reproduces
FluidSynth exactly (see §4). The material simply *is* 20 dB down in 0.6 s under
that model. Section 7 confirms that VGMTrans's player behaves the same way.

Program 10 renders faster than the model predicts (0.442 s vs 0.589 s). Not
explained; possibly the sample itself is short or unlooped. **Worth checking.**

## 4. What has been ruled out, and how

All measured.

| Ruled out | Evidence |
|---|---|
| Juicy16 differing from FluidSynth | Same rip rendered by Juicy16 and by stock `fluidsynth` CLI at matched settings: RMS **−19.79 vs −19.82 dBFS** (0.03 dB). |
| Velocity response | Juicy16 vs stock across 11 velocity steps: within **0.3 dB**. |
| CC7 channel volume | Within **0.22 dB** across 9 steps; also within ~0.3 dB of the DLS spec's own `40·log10(cc/127)` curve. |
| CC10 pan | **Identical** (0.00 dB) across 9 steps. |
| Amplitude envelope being modified by Juicy16 | Attack time identical to stock at **37.7 ms**, and flat across every CC73 value — the CC71-79 modulators removed in 0.5.1-alpha.5 are genuinely gone. |
| DLS vs SF2 export difference | 19 programs compared per-instrument; **maximum difference 0.20 dB**. Decay contours identical to the millisecond. |
| Missing DLS attenuation data | Every region `wsmp` attenuation is 0 dB, there are **no** instrument-level `CONN_DST_GAIN` blocks, and the wave pool carries **no** `wsmp` chunks. There is no attenuation data to lose. |
| `EG1_SUSTAIN` being ignored | Honoured: instruments declared ~0% sustain render at ~1%; those declared 100% render at 94–102%. |
| Reverb | Corpus-wide, CC91 appears **8 times across all 24 rips**. Reverb is also now off by default. |
| Chorus | Corpus-wide, CC93 appears **0 times**. |
| Clipping / headroom | Full rip peaks −4.63 dBFS, 0 samples over full scale. |
| FluidSynth terminating voices early | The kill test in `fluid_rvoice.c` compares `amp_max = cb2amp(min_attenuation) * volenv_val` against the noise floor. It uses `volenv_val` **linearly** while rendering uses it as **dB**, so it over-estimates amplitude and kills voices **late**, never early. |

## 5. What was disproved

An earlier hypothesis in this investigation was that DLS envelopes are **linear
in amplitude** (because the DLS spec expresses EG1 sustain as a percent) while
SF2 is linear in dB, and that patching FluidSynth to a linear-amplitude decay
would restore the intended sound. A patch was about to be written on that basis.

**Reading VGMTrans's source disproved it.** `src/main/conversion/DLSConversion.cpp`:

```c
// the DLS envelope is a range from 0 to -96db.
double attenInDB = ampToDb(rgn->sustain_level);
convSustainLev = static_cast<long>(((96.0 - attenInDB) / 96.0) * 0x03e80000);
```

VGMTrans writes a **dB-domain fraction** into the DLS sustain field, and
`src/main/conversion/SF2Conversion.cpp` + `SF2File.cpp` store attenuation in
centibels the same way. **VGMTrans assumes the linear-dB model, which is exactly
what FluidSynth implements.** A linear-amplitude patch would have made Juicy16
*less* faithful to VGMTrans, not more.

Lesson for whoever continues: verify against the reference implementation's
source before changing envelope semantics.

## 6. The reference implementations — and why this is confusing

Three different synths are in play, and they are not the same target:

- **Juicy16** — FluidSynth 2.5.5, SF2 envelope model, linear in dB.
- **VGMTrans's own player** — **BASSMIDI** (`src/ui/qt/SequencePlayer.h` includes
  `bass.h` and `bassmidi.h`), playing the **SF2** it generates. Closed source.
- **Fruity LSD** — the owner's reference for "correct". A DirectMusic/DLS
  wrapper, playing the **DLS**.

The owner's stated goal is "100% accurate to VGMTrans playback", which means
matching **BASSMIDI on the SF2**. Their evidence of wrongness comes from **Fruity
LSD**, which is a *third* engine. BASSMIDI has now been measured and agrees with
FluidSynth on the disputed behavior; DirectMusic has not been measured.

## 7. Reference experiment completed

Measured on the local VGMTrans checkout at commit
`e82843e23db56b76054e314f121d9b6f1eaf47e5`, using its bundled BASS 2.4.16.7 and
BASSMIDI 2.4.13 libraries. A small offline decoder rendered MIDI note 60 at
velocity 100 through program 10 of `SEQ_BGM_N_CASTLE.sf2`, with effects disabled,
at 48 kHz. The same process rendered FluidSynth 2.5.5 and a second BASSMIDI font
handle with `BASS_MIDI_FONT_LINDECVOL` enabled. Each contour was normalised to
its first 50 ms RMS window.

| window centre | BASSMIDI as VGMTrans uses it | FluidSynth | BASSMIDI linear option |
|---|---:|---:|---:|
| 0.075 s | -3.96 dB | -4.03 dB | -2.45 dB |
| 0.275 s | -14.33 dB | -14.21 dB | -6.70 dB |
| 0.575 s | -20.36 dB | -19.83 dB | -3.42 dB |
| 1.025 s | -39.46 dB | -38.27 dB | -9.29 dB |
| 1.525 s | -51.95 dB | -50.07 dB | -7.24 dB |

The default BASSMIDI and FluidSynth contours stay within 0.53 dB through the
first 0.575 s and cross -20 dB in the same window. BASSMIDI is then slightly
*quieter*, not louder, than FluidSynth. That small difference cannot explain a
report that Juicy16's struck instruments are too quiet relative to VGMTrans.

The linear option is the proposed long-tail behavior, and is dramatically
different: it is only 3.42 dB down when the normal VGMTrans path is 20.36 dB
down. But VGMTrans calls `BASS_MIDI_FontInitUser` with only
`BASS_MIDI_FONT_XGDRUMS`; it does **not** pass `BASS_MIDI_FONT_LINDECVOL`.
BASSMIDI's own documentation also describes linear decay/release as an explicit
font-init option, not the default. Enabling the equivalent behavior in Juicy16
would therefore move it away from VGMTrans.

DirectMusic/Fruity LSD may still use a different contour. If Fruity LSD
compatibility is promoted to a product target later, the required evidence is
still a one-note bounce of this exact program from Fruity LSD. A full-track bounce
will not isolate an envelope. Until that reference exists, a DirectMusic mode
would still be a guess.

## 8. How to reproduce everything here

All from the repository root, with the debug build in `build/`.

```bash
# Juicy16 vs stock FluidSynth on a whole rip
./build/JuicySFDynamicsProbe "<bank>.dls" --midi "<rip>.mid"
fluidsynth -ni -g 0.2 -R 0 -C 0 -r 48000 -F /tmp/stock.wav "<bank>.dls" "<rip>.mid"

# velocity / CC7 / CC10 / envelope comparison against stock, per program
./build/JuicySFDynamicsProbe "<bank>.dls" <program>

# per-rip conformance across a corpus (24/24 pass as of this writing)
./build/JuicySFEngineMidiTests --game-rip "<bank>.dls" "<rip>.mid"
```

The one-note MIDI generator, DLS articulation parser, SF2 `igen` parser, original
decay-measurement script, and BASSMIDI comparison decoder were written ad hoc in
`/tmp` during the investigation and are not committed. They are small; the exact
inputs, flags, versions, and resulting contour points are recorded above.

## 9. Why no fix was applied

Stated by the owner:

- **No compensation, no estimates, no approximation.** It must be exact.
- **Lightweight.** This runs per voice, per buffer, on the audio thread.

From the repository:

- FluidSynth is a **pinned external dependency**, fetched and built by
  `tools/build_macos_dependencies.sh`. That script already has a reviewed,
  SHA-verified `apply_patch` mechanism, used today for libsndfile — see
  [vendor/libsndfile_patched/](../vendor/libsndfile_patched/README.md). A
  FluidSynth patch would follow the same pattern and is enforced by the
  `dependency_patch_contract` test.
- The relevant code is the volume-envelope application in
  `src/rvoice/fluid_rvoice.c`:
  ```c
  target_amp = fluid_cb2amp(voice->dsp.attenuation)
             * fluid_cb2amp(FLUID_PEAK_ATTENUATION * (1.0f - volenv_val))
  ```
  `volenv_val` ramps 1 → 0 linearly; `cb2amp` makes the result linear in dB.
  (Line quoted from a local 2.3.7 checkout; **verify against 2.5.5** before
  patching. `apply_patch` verifies the base file's SHA, so drift fails loudly.)
- **Any envelope change affects every bank**, including ordinary General MIDI
  SoundFonts where linear-dB is correct per spec. Deviating would make Juicy16
  render normal SoundFonts differently from every other player. If a change is
  made it should almost certainly be a setting, not the default.

The measurement closes this issue under the current product contract: the exact
reference named by that contract does not use the proposed curve. A future
Fruity LSD/DirectMusic compatibility setting would require all of the following:

1. a captured one-note DirectMusic contour proving its curve;
2. a non-default, clearly named mode so ordinary SF2/SF3 banks stay conformant;
3. a pinned FluidSynth patch with its base/result hashes in both dependency
   recipes and `dependency_patch_contract`;
4. audio-domain regression coverage for decay and release; and
5. an explicit Beta parameter/state compatibility decision.

## 10. Related records

- [KNOWN_ISSUES.md](KNOWN_ISSUES.md) — the user-facing entry, with the same
  ruled-out list in short form.
- [CONTROLLER_SUPPORT.md](CONTROLLER_SUPPORT.md) — what the engine does with each
  controller, including the GM default reverb send added in this release.
- [../CHANGELOG.md](../CHANGELOG.md) — `0.6.0-alpha.3` carries the reverb and
  measurement work done alongside this investigation.
