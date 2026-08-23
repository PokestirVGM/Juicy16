# MIDI controller support contract

This document describes Juicy16 Beta 1 with the pinned FluidSynth 2.5.5 engine.
It separates message delivery—which Juicy16 controls—from audible interpretation,
which may depend on FluidSynth mode and modulators in the loaded DLS/SF2/SF3 bank.

## Delivery

Juicy16 forwards every valid CC0–CC127 message unchanged to the originating MIDI
channel at its event sample. Pitch bend remains a 14-bit value from 0 through
16383, centered at 8192. Channel pressure and polyphonic key pressure are also
forwarded unchanged. The automated suite proves these delivery properties; it
does not imply that every bank gives every controller an audible destination.

## Paired and high-resolution controllers

FluidSynth stores both halves of MSB/LSB pairs, but its general synthesis
modulators use the 7-bit MSB only. Portamento time is the general-controller
exception: FluidSynth combines its MSB and LSB. Data Entry CC6/38 is combined for
supported RPN/NRPN operations. RPN 0,0 whole-semitone bend ranges and RPN Null are
automated.

Cents-level RPN 0,0 bend range **is** honoured, measured rather than assumed. With
a range of 2 semitones plus 50 cents, a full-up bend raises pitch by a factor of
1.1596; 2.5 semitones predicts 1.1553 and 2 semitones alone predicts 1.1224, so
the Data Entry LSB is clearly in effect. `getPitchWheelSensitivity` reports whole
semitones only, which is a limit of that diagnostic accessor and not of the
engine. The offline suite asserts this in the audio domain at 48 kHz.

Bank Select is mode-dependent state for the next Program Change, never a patch
change by itself. Juicy16 explicitly pins FluidSynth's initial mode to GS for
Beta 1: CC0 selects the pending bank, while CC32 is still delivered/stored but
does not contribute to the GS bank number. GM, GS, and XG reset messages can
change the live convention as required by those reset families. The editor and
saved channel state update only after Program Change succeeds; they store the
logical bank reported by the accepted font program, not the pending CC bytes.
VST3 `progChN` changes only the program and retains that channel's current bank.

Cross-bank selection is automated rather than inferred. The offline suite
synthesises an SF2 whose presets sit in banks 0, 1, 8, and 128 and each sound a
different pitch, so the rendered note names the bank and program FluidSynth
actually chose. It proves CC0 reaches banks 1 and 8, that CC32 does not move the
channel out of the bank CC0 chose, that a return to CC0 = 0 restores the melodic
bank, and that channel 10 reaches bank 128 with no Bank Select at all — with the
engine, the saved channel state, the editor parameters, and the audio agreeing at
every step. The macOS system DLS covers the same path across banks 0 and 1 on a
real DLS bank. The pinned SF3 fixture defines only banks 0 and 128, so SF3 is
proven for percussion-versus-melodic selection only; no SF3 bank with a melodic
bank above 0 has been tested. Bank selection happens in FluidSynth's shared preset
lookup, above the format-specific sample decoding that distinguishes SF3 from SF2,
but that is reasoning rather than measurement.

Selecting a bank/program the bank file does not define is accepted, not refused.
FluidSynth 2.5.5 records the requested bank and program on the channel and
substitutes bank 0 program 0 for synthesis, so the editor and saved state show
what the MIDI asked for while a different patch sounds. Juicy16 deliberately keeps
the requested value, because it is what should be restored when the project is
reopened with the intended bank. See `KNOWN_ISSUES.md`.

FluidSynth supports a per-font bank offset, which shifts the engine bank numbers a
font's banks answer to. Juicy16 never installs one, so its raw and logical bank
numbers are always identical in practice. The conversion is still tested: the
offline suite installs an offset and asserts that manual selection, MIDI Bank
Select, the persisted channel state, the editor parameters, and the sounding
preset all resolve to the font's own bank numbering.

Pitch bend itself is not a paired 7-bit CC and retains its complete 14-bit range.

## Common controller interpretation

| Controllers | Beta 1 behavior |
|---|---|
| CC1/2, CC7/10/11 | Delivered exactly. Volume, pan, and expression use FluidSynth's standard channel behavior; modulation/breath audibility can depend on bank modulators. |
| CC5/37, CC65, CC68 | Portamento time pair, portamento switch, and legato switch are delegated to FluidSynth. Their audible result depends on channel mode and material. |
| CC64 | FluidSynth sustain pedal. Releasing it damps voices held by sustain. |
| CC66 | FluidSynth sostenuto pedal. It captures voices already active when the pedal is pressed. |
| CC67 | Delivered as the soft-pedal controller; audible behavior depends on bank modulators. |
| CC91/93 | Per-channel reverb and chorus sends, delivered exactly. CC91 feeds the reverb described below. CC93 reaches the engine but the chorus is switched off, so it does nothing audible — see below. |
| CC98/99, CC100/101, CC6/38 | FluidSynth NRPN/RPN selection and Data Entry. RPN 0,0 bend range and RPN Null are regression-tested. |
| CC120 | All Sound Off immediately silences the addressed channel. |
| CC121 | Reset All Controllers resets switches, expression, RPN/NRPN selection, pressure, and pitch wheel. FluidSynth intentionally preserves bank, volume, pan, effects sends, sound controls CC70–79, and the configured bend range. |
| CC122 | Local Control is accepted/stored by FluidSynth but intentionally has no synthesis action in this plugin engine. |
| CC123 | All Notes Off releases the addressed channel's notes according to pedal/envelope state; it is not the immediate-kill behavior of CC120. |
| CC124–127 | Delivered to FluidSynth, then Juicy16 restores its 16-channel layout. See the section below. |

## Channel-mode messages and the 16-channel layout

CC124–127 are MIDI 1.0 channel-mode messages, and FluidSynth implements them
faithfully: Omni Off and Mono On assign a group of consecutive channels to a
basic channel and **disable the rest**. On a MIDI 1.0 sound module that is
correct. On a fixed 16-channel multitimbral instrument it is destructive — a
single CC124 on channel 1 used to leave only channel 1 responding, silent and
unreadable everywhere else, until the next reset.

Juicy16 therefore **forwards the controller and then restores its own layout**.
Both contracts hold at once: every CC0–127 still reaches FluidSynth at its own
sample position, and there are still exactly 16 independent channels afterwards.
The restored layout is FluidSynth's own default — one basic channel at 0 in
Omni-On Poly whose group covers every MIDI channel.

The regression suite renders all four controllers across six values each, checks
that no channel is ever disabled, that the controller was genuinely delivered
rather than filtered, and that channel 16 still sounds immediately afterwards.
A burst of interleaved mode messages across different channels is covered too,
because that is what a host's reset burst actually looks like.

Mono mode is therefore not honoured as a per-channel monophonic setting. That is
a deliberate trade: the 16-channel routing model is the product, and MIDI's
basic-channel mechanism cannot express both.

## Bank Select on the percussion channel

On a drum channel FluidSynth adds its 128 drum offset on top of the Bank Select
MSB, so the reported bank is `128 + MSB`. The XG drum convention CC0=127
therefore reports bank **255**.

SF2 2.04 section 7.2 limits a *file's* bank numbers to 0–127 melodic plus 128
percussion, and every fixture used here obeys that; this is a runtime channel
bank, not a malformed font.

The engine, the saved channel state, the visible `bank` parameter, and the host
automation value all carry that number: the parameter spans 0–255 as of
`0.5.1-alpha.6`, and a drum-range bank is restored through Bank Select rather
than program select, so reopening a project keeps the channel on the bank it was
saved with. Until then the parameter stopped at 128 and the three surfaces
disagreed, which shipped as a B2 until the owner declined it on 2026-08-23.

No font defines a bank above 128, so what sounds on one is FluidSynth's
substituted drum kit — measured at 1.0000 waveform correlation against CC0=0,
which is why this was ever a state-only problem.

## Exposed mixer controls

Every channel row carries its own volume and pan knob — all 16 visible at once,
with no row to select first. They are plain MIDI controllers handled by
FluidSynth's own default modulators; Juicy16 adds no modulator of its own.

| CC | UI control | Parameter | Effect | Default |
|---:|---|---|---|---|
| 7 | row Vol knob | `volCh1`-`volCh16` | Channel volume | 100 |
| 10 | row Pan knob | `panCh1`-`panCh16` | Channel pan; 0 hard left, 64 centre, 127 hard right | 64 |

Each is a real host parameter, so a host can automate any channel and a
right-click on a knob offers the host's own automation and controller-link menu.

Setting a knob is a starting point only. Incoming CC7/CC10 on that channel
replaces the value at the event's timestamp and moves that row's knob, exactly as
an incoming Program Change overrides a manually picked instrument. CC121
preserves both, as the MIDI spec requires, and Juicy16's GM/GS/XG reset handling
reapplies the latest per-channel values so the editor, saved state, and engine
stay converged.

## Reverb

Juicy16 has a reverb, and until 0.6.0-alpha.1 you could not hear it.

FluidSynth's reverb was always running — `synth.reverb.active` defaults to on —
but the plugin asked FluidSynth for audio in a way that **discarded the effects
buses**. Measured against FluidSynth directly on 2026-08-23 with reverb on, level
1.0, room 0.9 and CC91=127, the tail energy after note-off was 0.0000046 through
the call Juicy16 used and 7.467 through one that mixes the effects in. The
effects bus is now mixed into the output, so material that asks for reverb gets
it.

**This changes how existing projects sound.** A rip that sends CC91 will now have
reverb where it previously had none. That is the file being played as written,
but it is an audible change and worth knowing before you reopen old work.

| Control | Parameter | Range | Universal | Soft |
|---|---|---|---|---|
| Enable | `reverbOn` | on/off | on | on |
| Profile | `reverbProfile` | Universal / Soft / Custom | — | — |
| Size | `reverbSize` | 0–1 | 0.45 | 0.20 |
| Damp | `reverbDamp` | 0–1 | 0.35 | 0.60 |
| Width | `reverbWidth` | 0–1 | 0.85 | 1.00 |
| Level | `reverbLevel` | 0–1 | 0.55 | 0.55 |

- **What the engine does.** FluidSynth 2.5.5's reverb is jjceresa's FDN late
  reverb, which replaced Freeverb in 2.0. Juicy16 adds no DSP of its own; these
  controls set that reverb.
- **What you control, and what the MIDI file controls.** You set the reverb.
  The file sets how much of each channel goes into it, through CC91, at that
  event's own timestamp. **A MIDI file cannot change your reverb settings** —
  GS and XG reverb macro SysEx is deliberately ignored, so a rip asking for a
  hall gets whatever profile you selected. That is a scope decision, recorded in
  the milestone plan; tell us if a rip sounds wrong in a way the manual controls
  cannot fix.
- **A rip that never sends CC91 gets no reverb**, whatever these controls say.
  That is not a defect: nothing is being sent to the reverb. `SEQ_BGM_C_03`, a
  real VGMTrans rip in the test corpus, sends no CC91 at all.
- **Bypass is genuine.** Turning the reverb off removes the unit rather than
  turning its level down, so nothing keeps computing a tail. Bypassed output is
  bit-identical to a signal that was never sent to the reverb, which is asserted
  rather than assumed.
- **Profiles move the controls.** Selecting one sets all four visible values;
  editing any of them selects Custom. Nothing is hidden from you or from host
  automation.
- **Width is narrowed on purpose.** FluidSynth accepts 0–100 there, but its own
  default is 0.8 and everything useful lives below 1. The full range would put
  the entire useful span inside the first one percent of the knob.

### Where the defaults came from

Measured on a real VGMTrans rip at CC91 = 80, against the same material dry:
FluidSynth's inherited `0.50/0.30/0.80/0.70` adds **+1.64 dB** RMS, Universal
adds **+0.92 dB**, and Soft adds **+0.47 dB**. Universal is a present but not
dominant space; Soft is a much smaller room at full width — width without a long
tail. Neither clips, and neither raises the peak above the dry material's.

### Chorus is off

FluidSynth's chorus was being discarded by the same bug. Rather than un-mute a
chorus nobody chose the moment the effects bus started working, it is switched
off explicitly. CC93 still reaches the engine and is still delivered exactly; it
simply has nothing to drive until the chorus gets controls of its own.

## Mute and solo are not MIDI controllers

Each row also carries mute and solo (`muteCh1`-`muteCh16`, `soloCh1`-`soloCh16`).
These are the plugin's own controls: **nothing in a MIDI file changes them**, and
no controller reset or GM/GS/XG reset SysEx clears them.

- A silenced channel drops incoming note-ons. It is not turned down, so the
  file's own CC7 value survives being muted.
- Everything else still reaches the engine while a channel is silenced —
  note-offs, controllers, program changes, pitch bend — so unmuting mid-song
  needs no resync.
- Muting a channel that is already sounding sends it All Notes Off, so held notes
  release naturally rather than ringing on or cutting off with a click.
- **A channel sounds if it is not muted, and either nothing is soloed or it is
  one of the soloed ones.** Mute always wins; solo only restricts which channels
  are candidates. So:
  - Muting the only soloed channel produces silence. Pressing M always does what
    it says — under the obvious alternative, "solo overrides mute", that press
    would have done nothing at all.
  - Soloing a channel that is muted also produces silence, which is the same
    statement in the other order.
  - Soloing every channel is the same as soloing none: the solo set stops
    excluding anything and only the mutes remain.
  - Clearing the last solo restores exactly the mute picture that was there
    before, because solo never altered it.
- **You can always see why a channel is quiet.** A silenced row recedes, and a
  lit mute is red while a lit solo is the accent colour, so a muted channel and a
  not-soloed channel never look alike.

### CC71-79 are forwarded but do nothing

Juicy16 used to add its own modulators mapping CC71/72/73/74/75/79 onto filter
and volume-envelope generators. **They were removed in 0.5.1-alpha.5.** No other
SoundFont player applies those controllers — stock FluidSynth ignores them
entirely — and the amounts were wildly out of scale: measured against a real SF2,
CC73=127 stretched attack from 50 ms to 868 ms, CC75=127 raised a note's tail by
43 dB, CC72=127 left a note ringing 48 dB above neutral a second after note-off,
and CC71=127 attenuated the signal by 46 dB. On DLS banks they did nothing at all,
because FluidSynth's native DLS loader does not apply the default modulator list.
Game rips commonly send these controllers, so material sounded flat and
compressed only in this plugin.

They are still delivered to FluidSynth like every other controller, and the
engine still stores and reports them; there is simply no Juicy16-specific
modulator listening for them.

## Master output level

`outputLevel` is not a MIDI controller and is not per channel. It is a master
trim in decibels (-24 to +12, default 0) applied to the rendered output with
20 ms smoothing, so host automation cannot step the gain mid-block. Nothing in a
MIDI file changes it. FluidSynth's own `synth.gain` stays at its documented
default of 0.2.
