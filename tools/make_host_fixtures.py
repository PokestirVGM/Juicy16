#!/usr/bin/env python3
"""Generate the importable MIDI fixtures used for Phase 7 host testing.

The offline harness drives MIDI from CSV at exact sample offsets, which no DAW
can reproduce. Phase 7 asks for the same coverage *through a host*, so it needs
real Standard MIDI Files a tester can drag into Cubase, FL Studio, or Logic.
These are those files.

They are written by a generator rather than hand-authored so the expected-value
tables in docs/HOST_TEST_PROTOCOL.md can be derived from the same source as the
notes, and so a regression in one cannot silently drift from the other. The
`host_fixtures_reproducible` test regenerates and byte-compares the committed
files.

Everything here is original content targeting General MIDI program numbers, so
the fixtures are redistributable — unlike the private game rip.

    python3 tools/make_host_fixtures.py [output-dir]
"""

import os
import struct
import sys

PPQ = 480
BEAT = PPQ
BAR = 4 * BEAT  # 4/4
TEMPO_BPM = 120  # one bar = 2 seconds, slow enough to read the UI between steps

# ---------------------------------------------------------------------------
# General MIDI program numbers, 0-based on the wire. The protocol document shows
# them 1-based, because that is how the plugin and most DAWs display them.
# ---------------------------------------------------------------------------

GM_NAMES = {
    0: "Acoustic Grand Piano", 4: "Electric Piano 1", 11: "Vibraphone",
    14: "Tubular Bells", 19: "Church Organ", 21: "Accordion",
    24: "Acoustic Guitar (nylon)", 30: "Distortion Guitar",
    33: "Electric Bass (finger)", 38: "Synth Bass 1", 40: "Violin",
    42: "Cello", 48: "String Ensemble 1", 52: "Choir Aahs", 56: "Trumpet",
    60: "French Horn", 65: "Alto Sax", 71: "Clarinet", 73: "Flute",
    75: "Pan Flute", 80: "Lead 1 (square)", 87: "Lead 8 (bass + lead)",
    88: "Pad 1 (new age)", 95: "Pad 8 (sweep)", 104: "Sitar", 108: "Kalimba",
    112: "Tinkle Bell", 119: "Reverse Cymbal", 120: "Guitar Fret Noise",
    127: "Gunshot",
}

# channel (1-based) -> (checkpoint A program, checkpoint B program, note)
# Channel 10 is percussion: its program selects a drum kit, and which kits exist
# is bank-dependent, so it stays on program 0 and is verified by ear instead.
PROGRAM_MATRIX = {
    1:  (0,   4,   60),
    2:  (11,  14,  64),
    3:  (19,  21,  67),
    4:  (24,  30,  72),
    5:  (33,  38,  36),
    6:  (40,  42,  69),
    7:  (48,  52,  62),
    8:  (56,  60,  65),
    9:  (65,  71,  70),
    10: (0,   0,   36),  # bass drum
    11: (73,  75,  74),
    12: (80,  87,  76),
    13: (88,  95,  55),
    14: (104, 108, 59),
    15: (112, 119, 79),
    16: (120, 127, 61),
}

CHECKPOINT_BARS = [1, 5, 9]  # A, B, C(=A again)

# ---------------------------------------------------------------------------
# Standard MIDI File writing
# ---------------------------------------------------------------------------


def vlq(value):
    """MIDI variable-length quantity."""
    if value < 0:
        raise ValueError("negative delta time")
    out = bytearray([value & 0x7F])
    value >>= 7
    while value:
        out.insert(0, (value & 0x7F) | 0x80)
        value >>= 7
    return bytes(out)


class Track:
    """Absolute-tick event collector that serialises to an MTrk chunk."""

    def __init__(self):
        self.events = []  # (tick, order, payload)

    def add(self, tick, payload, order=0):
        # `order` breaks ties at the same tick deterministically. Program Change
        # must precede a note-on at the same tick, which is exactly the case the
        # offline suite proves in the audio domain; the fixture reproduces it.
        self.events.append((tick, order, len(self.events), bytes(payload)))

    # -- channel messages (channel is 1-based) --
    def note_on(self, tick, channel, note, velocity=100):
        self.add(tick, [0x90 | (channel - 1), note, velocity], order=10)

    def note_off(self, tick, channel, note):
        self.add(tick, [0x80 | (channel - 1), note, 0], order=1)

    def cc(self, tick, channel, controller, value):
        self.add(tick, [0xB0 | (channel - 1), controller, value], order=5)

    def program(self, tick, channel, value):
        self.add(tick, [0xC0 | (channel - 1), value], order=6)

    def bend(self, tick, channel, value):
        """value is the full 14-bit range: 0 low, 8192 centre, 16383 high."""
        if not 0 <= value <= 16383:
            raise ValueError("pitch bend out of range")
        self.add(tick, [0xE0 | (channel - 1), value & 0x7F, (value >> 7) & 0x7F],
                 order=5)

    def bank_select(self, tick, channel, msb, lsb=0):
        self.cc(tick, channel, 0, msb)
        self.cc(tick, channel, 32, lsb)

    def rpn(self, tick, channel, rpn_msb, rpn_lsb, data_msb, data_lsb=0):
        self.cc(tick, channel, 101, rpn_msb)
        self.cc(tick, channel, 100, rpn_lsb)
        self.cc(tick, channel, 6, data_msb)
        self.cc(tick, channel, 38, data_lsb)
        # RPN Null, so a later stray Data Entry cannot land on this parameter.
        self.cc(tick, channel, 101, 127)
        self.cc(tick, channel, 100, 127)

    # -- meta events --
    def marker(self, tick, text):
        raw = text.encode("ascii")
        self.add(tick, b"\xff\x06" + vlq(len(raw)) + raw, order=-10)

    def track_name(self, text):
        raw = text.encode("ascii")
        self.add(0, b"\xff\x03" + vlq(len(raw)) + raw, order=-20)

    def tempo(self, bpm):
        us = int(round(60_000_000 / bpm))
        self.add(0, b"\xff\x51\x03" + us.to_bytes(3, "big"), order=-19)

    def time_signature(self, numerator=4, denominator_pow2=2):
        self.add(0, bytes([0xFF, 0x58, 0x04, numerator, denominator_pow2, 24, 8]),
                 order=-18)

    def serialise(self, end_tick):
        self.add(end_tick, b"\xff\x2f\x00", order=100)
        body = bytearray()
        previous = 0
        for tick, _order, _seq, payload in sorted(
                self.events, key=lambda e: (e[0], e[1], e[2])):
            body += vlq(tick - previous)
            body += payload
            previous = tick
        return b"MTrk" + struct.pack(">I", len(body)) + bytes(body)


def write_midi(path, tracks, end_tick):
    header = b"MThd" + struct.pack(">IHHH", 6, 1, len(tracks), PPQ)
    with open(path, "wb") as handle:
        handle.write(header)
        for track in tracks:
            handle.write(track.serialise(end_tick))


def conductor(name, markers, end_tick):
    track = Track()
    track.track_name(name)
    track.tempo(TEMPO_BPM)
    track.time_signature()
    for bar, text in markers:
        track.marker((bar - 1) * BAR, text)
    return track


# ---------------------------------------------------------------------------
# Fixture A — 16-channel Program Change matrix
# ---------------------------------------------------------------------------


def build_program_matrix(directory):
    """Every channel takes its own program at three checkpoints, with no manual
    assignment. This is the non-negotiable workflow: if a host collapses program
    changes to channel 1, this fixture shows it immediately in the channel list.
    """
    music = Track()
    music.track_name("Juicy16 16-channel program matrix")

    markers = []
    for index, bar in enumerate(CHECKPOINT_BARS):
        label = "ABC"[index]
        tick = (bar - 1) * BAR
        markers.append((bar, "CHECKPOINT %s" % label))

        for channel, (program_a, program_b, note) in PROGRAM_MATRIX.items():
            program = program_b if label == "B" else program_a
            music.bank_select(tick, channel, 0, 0)
            music.program(tick, channel, program)

            # Same-tick note: proves the program change is applied before the
            # note it shares a timestamp with, rather than one event late.
            music.note_on(tick, channel, note)
            music.note_off(tick + BEAT, channel, note)

            # Settled note two beats later: audible confirmation once every
            # host-side reordering has had time to happen.
            music.note_on(tick + 2 * BEAT, channel, note)
            music.note_off(tick + 4 * BEAT - 1, channel, note)

    end = (CHECKPOINT_BARS[-1] - 1 + 4) * BAR
    write_midi(os.path.join(directory, "host_program_matrix.mid"),
               [conductor("Juicy16 program matrix", markers, end), music], end)


# ---------------------------------------------------------------------------
# Fixture B — controllers, pitch bend, pedals, channel mode
# ---------------------------------------------------------------------------

# CC numbers the editor mirrors onto its six sliders. Watching these is how a
# tester verifies exact controller values without an event trace.
SLIDER_CCS = [
    (73, "Attack"), (75, "Decay"), (79, "Sustain level"), (72, "Release"),
    (74, "Filter cutoff"), (71, "Filter resonance"),
]

SUSTAINED_PROGRAM = 48  # String Ensemble 1: holds indefinitely, filter-responsive


def build_controllers(directory):
    music = Track()
    music.track_name("Juicy16 controller and pitch-bend conformance")
    markers = []
    bar = 1

    def step(text):
        nonlocal bar
        markers.append((bar, text))
        tick = (bar - 1) * BAR
        bar += 1
        return tick

    # -- setup ---------------------------------------------------------------
    tick = step("SETUP - reset all 16 channels")
    for channel in range(1, 17):
        music.cc(tick, channel, 121, 0)  # Reset All Controllers
        music.bank_select(tick, channel, 0, 0)
        music.program(tick, channel, 0 if channel == 10 else SUSTAINED_PROGRAM)

    def held(channel, note, tick, bars=1):
        music.note_on(tick, channel, note)
        music.note_off(tick + bars * BAR - 1, channel, note)

    def sweep(channel, controller, tick):
        """0, 64, 127 at beats 1, 2.5 and 4 of the bar."""
        for offset, value in ((0, 0), (3 * BEAT // 2, 64), (3 * BEAT, 127)):
            music.cc(tick + offset, channel, controller, value)

    # -- the six exposed sound controllers -----------------------------------
    # Cutoff and resonance are audible on one held note. The four envelope
    # controllers only take effect on the next note, so those bars retrigger.
    for controller, name in [(74, "Filter cutoff"), (71, "Filter resonance")]:
        tick = step("CC%d %s 0-64-127 (held note)" % (controller, name))
        held(1, 60, tick)
        sweep(1, controller, tick)

    for controller, name in [(73, "Attack"), (75, "Decay"),
                             (79, "Sustain level"), (72, "Release")]:
        tick = step("CC%d %s 0-64-127 (retriggered)" % (controller, name))
        for offset, value in ((0, 0), (3 * BEAT // 2, 64), (3 * BEAT, 127)):
            music.cc(tick + offset, 1, controller, value)
            music.note_on(tick + offset + 1, 1, 60)
            music.note_off(tick + offset + BEAT - 1, 1, 60)

    # -- volume, expression, pan --------------------------------------------
    tick = step("CC7 volume then CC11 expression 127-64-16")
    held(1, 60, tick)
    for offset, value in ((0, 127), (BEAT, 64), (2 * BEAT, 16)):
        music.cc(tick + offset, 1, 7, value)
    music.cc(tick + 3 * BEAT, 1, 7, 100)
    for offset, value in ((3 * BEAT, 127), (3 * BEAT + BEAT // 2, 64)):
        music.cc(tick + offset, 1, 11, value)

    tick = step("CC10 pan left-centre-right")
    held(1, 60, tick)
    music.cc(tick, 1, 11, 127)
    for offset, value in ((0, 0), (3 * BEAT // 2, 64), (3 * BEAT, 127)):
        music.cc(tick + offset, 1, 10, value)

    tick = step("CC1 mod, CC91 reverb, CC93 chorus 0-127")
    held(1, 60, tick)
    music.cc(tick, 1, 10, 64)
    for offset, controller in ((0, 1), (BEAT, 91), (2 * BEAT, 93)):
        music.cc(tick + offset, 1, controller, 0)
        music.cc(tick + offset + BEAT // 2, 1, controller, 127)

    # -- pedals --------------------------------------------------------------
    tick = step("CC64 sustain pedal - note released under pedal must ring on")
    music.cc(tick, 1, 64, 127)
    music.note_on(tick + BEAT // 2, 1, 60)
    music.note_off(tick + BEAT, 1, 60)  # released early; the pedal holds it
    music.cc(tick + 3 * BEAT, 1, 64, 0)  # release: the note must stop here

    tick = step("CC66 sostenuto and CC67 soft pedal")
    music.note_on(tick, 1, 60)
    music.cc(tick + BEAT // 4, 1, 66, 127)  # sostenuto captures the held note
    music.note_off(tick + BEAT // 2, 1, 60)
    music.note_on(tick + BEAT, 1, 67)  # a later note is NOT captured
    music.note_off(tick + 3 * BEAT // 2, 1, 67)
    music.cc(tick + 2 * BEAT, 1, 66, 0)
    music.cc(tick + 5 * BEAT // 2, 1, 67, 127)  # soft pedal
    music.note_on(tick + 3 * BEAT, 1, 60)
    music.note_off(tick + 4 * BEAT - 1, 1, 60)

    # -- pitch bend ----------------------------------------------------------
    tick = step("BEND ch1 full down - centre - full up (default 2 semitones)")
    held(1, 60, tick)
    for offset, value in ((BEAT // 2, 0), (3 * BEAT // 2, 8192),
                          (5 * BEAT // 2, 16383)):
        music.bend(tick + offset, 1, value)
    music.bend(tick + 4 * BEAT - 2, 1, 8192)

    tick = step("RPN 0,0 sets ch2 bend range to 12 - ch2 bends an octave")
    music.rpn(tick, 2, 0, 0, 12)
    held(2, 60, tick)
    held(1, 60, tick)
    for offset, value in ((BEAT, 16383), (2 * BEAT, 0), (3 * BEAT, 8192)):
        music.bend(tick + offset, 2, value)
        music.bend(tick + offset, 1, value)  # ch1 still 2 semitones: compare

    tick = step("BEND ch10 and ch16, plus opposite simultaneous bend on ch1")
    held(10, 36, tick)
    held(16, 61, tick)
    held(1, 60, tick)
    for offset, value in ((BEAT, 16383), (2 * BEAT, 0)):
        music.bend(tick + offset, 10, value)
        music.bend(tick + offset, 16, value)
        music.bend(tick + offset, 1, 16383 - value)  # independent, opposite
    for channel in (1, 10, 16):
        music.bend(tick + 3 * BEAT, channel, 8192)

    # -- channel mode --------------------------------------------------------
    tick = step("CC120 All Sound Off - must cut instantly")
    for channel in (1, 2, 16):
        music.note_on(tick, channel, 60)
        music.note_off(tick + 4 * BEAT - 1, channel, 60)
    for channel in (1, 2, 16):
        music.cc(tick + 2 * BEAT, channel, 120, 0)

    tick = step("CC123 All Notes Off, then CC121 Reset All Controllers")
    for channel in (1, 2, 16):
        music.note_on(tick, channel, 60)
        music.note_off(tick + 4 * BEAT - 1, channel, 60)
    music.cc(tick + BEAT, 1, 74, 20)  # move a slider away from neutral first
    for channel in (1, 2, 16):
        music.cc(tick + 2 * BEAT, channel, 123, 0)
        music.cc(tick + 3 * BEAT, channel, 121, 0)

    tick = step("TAIL - silence")
    end = tick + BAR
    write_midi(os.path.join(directory, "host_controllers.mid"),
               [conductor("Juicy16 controller conformance", markers, end), music],
               end)
    return markers


def main():
    directory = sys.argv[1] if len(sys.argv) > 1 else \
        os.path.join(os.path.dirname(os.path.abspath(__file__)),
                     "..", "tests", "fixtures", "host")
    directory = os.path.abspath(directory)
    os.makedirs(directory, exist_ok=True)
    build_program_matrix(directory)
    build_controllers(directory)
    print("Wrote host fixtures to %s" % directory)


if __name__ == "__main__":
    main()
