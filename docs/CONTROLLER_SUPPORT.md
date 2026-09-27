# MIDI and controller behaviour

This is how Juicy16 handles MIDI. It runs on FluidSynth 2.5.7 with a couple of
small patches ([details](../vendor/fluidsynth_patched/README.md)).

## The basics

- Every controller reaches the synth on its own channel at its exact position in
  the block, except **balance (CC8/CC40)**, which is ignored like in Fruity LSD.
- Pitch bend keeps its full 14-bit range (0–16383, centre 8192).
- Channel and key pressure pass through unchanged.
- Whether a controller is *audible* can depend on the bank's own modulators.
- FluidSynth renders in 64-sample chunks, so a note can start up to 63 samples
  late.

## Controllers

| Controller | What it does |
|---|---|
| CC1, CC2 | Modulation and breath. What they do depends on the bank. |
| CC7, CC10, CC11 | Volume, pan and expression. CC7 and CC10 also move the row's knobs. |
| CC8, CC40 | Balance. Ignored. |
| CC5/37, CC65, CC68 | Portamento time, portamento and legato, handled by FluidSynth. |
| CC64, CC66, CC67 | Sustain, sostenuto and soft pedals. |
| CC71–79 | Passed through but do nothing, same as stock FluidSynth. |
| CC91, CC93 | Reverb and chorus send per channel. CC91 starts at 40 (the GM default), CC93 at 0. |
| CC98–101, CC6/38 | NRPN/RPN and Data Entry. RPN 0,0 sets the bend range, cents included. |
| CC120 | All Sound Off: silences the channel at once. |
| CC121 | Reset All Controllers. Volume, pan, sends and bend range stay. Juicy16 then restores the channel's last CC11, because hosts send CC121 on stop. |
| CC123 | All Notes Off: releases notes normally. |
| CC124–127 | Mode messages. They reach FluidSynth, then Juicy16 restores its 16 independent channels, so mono mode isn't supported. |

## Bank Select and Program Change

- CC0 picks the bank for the next Program Change (GS style). CC32 is stored but
  doesn't change the bank.
- Nothing is saved or shown until the Program Change actually succeeds.
- On channel 10 the bank reads as 128 + CC0, so XG's CC0=127 shows 255. That's
  expected.
- If channel 10 finds no drum kit, it falls back to the same program in the
  normal bank. Banks converted from GBA or SF2 often keep their kit at 0:0.
- A bank or program the loaded bank doesn't have plays a substitute, but your
  choice is kept so the right bank plays it later.

## Resets

A GM, GS or XG reset from the file sets each channel back to its defaults, then
Juicy16 puts back the channel's program, volume, pan, expression and bend range.
That's because VST3 hosts never resend those on replay, so without it the second
play would sound different from the first.

**Settings → Reset policy** switches this off: *Standard MIDI* follows the reset
exactly, while *DAW recovery* (the default) restores as above.

## Messages at the same moment

In VST3 every controller is a separate parameter, so the host can deliver
messages that share a timestamp in the wrong order. Juicy16 sorts each moment
into resets first, then Bank Select, Program Change, ordinary messages, and RPN
messages last. It also rebuilds RPN select → write → deselect order. A file
that's already in order plays as written.

## Mixer and channel controls

| Control | Parameter | Range | Default |
|---|---|---|---|
| Volume (CC7) | `volCh1`–`volCh16` | 0–127 | 100 |
| Pan (CC10) | `panCh1`–`panCh16` | 0 left, 64 centre, 127 right | 64 |
| Mute / Solo | `muteCh1`–`muteCh16`, `soloCh1`–`soloCh16` | on/off | off |
| Trim | `trimCh1`–`trimCh16` | -24 to +12 dB | 0 |
| Master trim | `outputLevel` | -24 to +12 dB | +1.5 dB |

- Volume and pan are just the channel's CC7 and CC10, so the file's next message
  replaces what you set.
- **Mute and solo** belong to the plugin. A silenced channel drops new notes but
  still gets everything else, so unmuting mid-song just works. Mute always beats
  solo.
- **Trim** is a plain audio gain after the synth, including that channel's
  reverb and chorus. MIDI doesn't touch it.
- The master trim defaults to +1.5 dB, the most my test rips allow without
  clipping.

**Full pan on DLS banks:** many converted DLS banks play each note as a hard-left
and hard-right sample pair and give CC10 only a small range. Juicy16 widens each
DLS region's pan range so pan reaches both sides. SF2 banks are unchanged.

## Reverb and chorus

Both are FluidSynth's built-in effects, and both are **off by default** so old
projects don't change. The file's CC91/CC93 decide how much of each channel goes
in; GS/XG reverb SysEx can't change your settings.

| Reverb | Parameter | Range | Universal | Soft |
|---|---|---|---|---|
| On | `reverbOn` | on/off | off | — |
| Profile | `reverbProfile` | Universal / Soft / Custom | — | — |
| Size | `reverbSize` | 0–1 | 0.45 | 0.20 |
| Damping | `reverbDamp` | 0–1 | 0.35 | 0.60 |
| Width | `reverbWidth` | 0–1 | 0.85 | 1.00 |
| Level | `reverbLevel` | 0–1 | 0.55 | 0.55 |

Choosing a profile sets the four knobs; moving a knob switches to Custom.

| Chorus | Parameter | Range | Default |
|---|---|---|---|
| On | `chorusOn` | on/off | off |
| Voices | `chorusVoices` | 1–8 | 3 |
| Level | `chorusLevel` | 0–1 | 0.6 |
| Rate | `chorusRate` | 0.1–5 Hz | 0.2 Hz |
| Depth | `chorusDepth` | 0–21 ms | 4.25 ms |
| Waveform | `chorusWaveform` | Sine / Triangle | Sine |

## Settings

| Setting | Parameter | Range | Default |
|---|---|---|---|
| Interpolation | `interpolation` | 7th-order / Linear / None | Linear |
| Bend range | `bendRange` | follow file, or 1–24 semitones | follow file |
| Bend scale | `bendScale` | ×1–×24 | ×1 |
| CC1 vibrato strength | `vibratoScaleCh1`–`vibratoScaleCh16` | ×1–×24 | ×1 |
| Reset policy | `resetPolicy` | DAW recovery / Standard MIDI | DAW recovery |

- **Interpolation** is how samples are stretched to other pitches. Linear is what
  Fruity LSD uses and keeps the bright grit of low-rate GBA samples. 7th-order is
  cleaner, None is rawest. Projects saved before this setting existed open on
  7th-order.
- **Bend range** forces one bend range on every channel, for hosts that drop the
  file's RPN.
- **Bend scale** multiplies incoming bends. FL Studio imports every bend as ±2
  semitones, so a rip written for 12 semitones needs ×6.
- **CC1 vibrato strength** boosts weak CC1 vibrato per channel without changing
  the CC1 value. It can't create vibrato when CC1 is 0; *CC1 received* shows what
  the host is sending.
