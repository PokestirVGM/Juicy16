# Open investigation: decaying instruments sound too quiet

Status: **unresolved**. Cause isolated to a class of instrument and a mechanism,
but the responsible difference has not been identified. Written as a self-
contained brief so another engineer or agent can pick it up cold.

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
that model. Whether that is what the music should sound like is the open
question.

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
LSD**, which is a *third* engine. These may or may not agree with each other.
**Nobody has yet measured either reference.**

## 7. The open question

> On the same SF2, with `decayVolEnv = 2.947 s` and `sustainVolEnv = 100 dB`,
> what amplitude contour does BASSMIDI (and/or DirectMusic) produce, and how does
> it differ from FluidSynth's linear-dB ramp?

Until that is measured, any change to Juicy16's envelope is a guess.

### The experiment that settles it

Render **one sustained note of program 10** from `SEQ_BGM_N_CASTLE.sf2` through
VGMTrans (BASSMIDI), and the same note through Juicy16 or stock FluidSynth.
Divide one amplitude contour by the other. Because the **sample content is
identical in both**, it cancels, and what remains is purely the ratio of the two
envelope curves. That is a direct measurement, not a curve fit.

A full-track bounce will **not** work — it is a mix, and no single instrument's
envelope can be recovered from it. This was raised and is correct.

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

The one-note MIDI generator, the DLS articulation parser, the SF2 `igen` parser
and the decay-measurement script used above were written ad hoc in `/tmp` during
the investigation and are not committed. They are small; regenerating them is
straightforward, and the numbers they produced are all quoted above.

## 9. Constraints on any fix

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

## 10. Related records

- [KNOWN_ISSUES.md](KNOWN_ISSUES.md) — the user-facing entry, with the same
  ruled-out list in short form.
- [CONTROLLER_SUPPORT.md](CONTROLLER_SUPPORT.md) — what the engine does with each
  controller, including the GM default reverb send added in this release.
- [../CHANGELOG.md](../CHANGELOG.md) — `0.6.0-alpha.3` carries the reverb and
  measurement work done alongside this investigation.
