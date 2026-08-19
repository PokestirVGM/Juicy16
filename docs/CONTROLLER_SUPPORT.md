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
The SF2/SF3/DLS cross-bank fixture remains a release gate, so do not infer
arbitrary-bank support from the DLS and controller-delivery tests alone.

Pitch bend itself is not a paired 7-bit CC and retains its complete 14-bit range.

## Common controller interpretation

| Controllers | Beta 1 behavior |
|---|---|
| CC1/2, CC7/10/11 | Delivered exactly. Volume, pan, and expression use FluidSynth's standard channel behavior; modulation/breath audibility can depend on bank modulators. |
| CC5/37, CC65, CC68 | Portamento time pair, portamento switch, and legato switch are delegated to FluidSynth. Their audible result depends on channel mode and material. |
| CC64 | FluidSynth sustain pedal. Releasing it damps voices held by sustain. |
| CC66 | FluidSynth sostenuto pedal. It captures voices already active when the pedal is pressed. |
| CC67 | Delivered as the soft-pedal controller; audible behavior depends on bank modulators. |
| CC91/93 | Reverb and chorus sends are delivered; audible depth depends on engine effects and bank/program behavior. |
| CC98/99, CC100/101, CC6/38 | FluidSynth NRPN/RPN selection and Data Entry. RPN 0,0 bend range and RPN Null are regression-tested. |
| CC120 | All Sound Off immediately silences the addressed channel. |
| CC121 | Reset All Controllers resets switches, expression, RPN/NRPN selection, pressure, and pitch wheel. FluidSynth intentionally preserves bank, volume, pan, effects sends, sound controls CC70–79, and the configured bend range. |
| CC122 | Local Control is accepted/stored by FluidSynth but intentionally has no synthesis action in this plugin engine. |
| CC123 | All Notes Off releases the addressed channel's notes according to pedal/envelope state; it is not the immediate-kill behavior of CC120. |
| CC124–127 | FluidSynth handles Omni/Mono/Poly mode changes only where the addressed channel is a valid basic channel. Juicy16 forwards them but makes no broader channel-group guarantee. |

## Exposed sound controls

The six editor sliders use linear bipolar default modulators. MIDI value 64 maps
to exactly zero modulation. The installed-modulator contract and all-channel
engine/UI/state synchronization are automated.

| CC | UI control | FluidSynth generator | Below 64 | Above 64 |
|---:|---|---|---|---|
| 71 | Resonance | Filter Q | less resonance | more resonance |
| 72 | Release | Volume-envelope release time | shorter | longer |
| 73 | Attack | Volume-envelope attack time | shorter | longer |
| 74 | Cutoff | Filter cutoff frequency | darker/lower | brighter/higher |
| 75 | Decay | Volume-envelope decay time | shorter | longer |
| 79 | Sustain | Volume-envelope sustain attenuation | quieter sustain | louder sustain |

CC121 preserves these sound controls, as required by FluidSynth's Reset All
Controllers behavior. Juicy16's GM/GS/XG reset handling reapplies the latest
per-channel values so the editor, saved state, and engine remain converged. A
fresh instance starts every channel at neutral 64.
