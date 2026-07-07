#!/usr/bin/env python3
"""
Convert MIDI Program Change events to CC 85 for JuicySF Rack's Cubase mode.

Why: Cubase strips MIDI Program Change entirely for VST3 instruments (it never
delivers PC as an event, never routes it via IMidiMapping, and never reads the
plugin's unit/program-list structure — verified empirically). Per-channel CCs DO
reach VST3 plugins in Cubase, so JuicySF Rack (>= 0.3.8) interprets CC 85
(undefined in the MIDI spec, unused by FluidSynth) as "program select": the CC
value is the program number, per channel.

This script rewrites every Program Change (0xCn pp) in a .mid file as a CC 85
event (0xBn 55 pp) on the same channel at the same time. Everything else
(notes, other CCs, bank selects, sysex, meta events, tempo map) is preserved.
Output is written next to the input as "<name> (cc85).mid"; the original is
never modified.

Usage:
    python3 tools/pc_to_cc85.py "Song.mid" [more.mid ...]
"""
import struct
import sys
import os


def read_varlen(data, pos):
    value = 0
    while True:
        b = data[pos]
        pos += 1
        value = (value << 7) | (b & 0x7F)
        if not (b & 0x80):
            return value, pos


def write_varlen(value):
    out = bytearray([value & 0x7F])
    value >>= 7
    while value:
        out.insert(0, 0x80 | (value & 0x7F))
        value >>= 7
    return bytes(out)


def convert_track(data):
    """Parse one MTrk body; return (new_body, pc_count). Re-serialized without
    running status so event sizes are self-contained."""
    out = bytearray()
    pos = 0
    running = None
    pc_count = 0
    n = len(data)
    while pos < n:
        delta, pos = read_varlen(data, pos)
        out += write_varlen(delta)

        b = data[pos]
        if b & 0x80:
            status = b
            pos += 1
            if status < 0xF0:
                running = status
        else:
            if running is None:
                raise ValueError("running status with no prior status byte")
            status = running

        if status == 0xFF:  # meta
            meta_type = data[pos]
            pos += 1
            length, pos = read_varlen(data, pos)
            body = data[pos:pos + length]
            pos += length
            out += bytes([0xFF, meta_type]) + write_varlen(length) + body
        elif status in (0xF0, 0xF7):  # sysex
            length, pos = read_varlen(data, pos)
            body = data[pos:pos + length]
            pos += length
            out += bytes([status]) + write_varlen(length) + body
        else:
            kind = status & 0xF0
            channel = status & 0x0F
            if kind in (0xC0, 0xD0):  # 1 data byte
                d1 = data[pos]
                pos += 1
                if kind == 0xC0:  # Program Change -> CC 85
                    out += bytes([0xB0 | channel, 85, d1])
                    pc_count += 1
                else:
                    out += bytes([status, d1])
            else:  # 2 data bytes
                d1 = data[pos]
                d2 = data[pos + 1]
                pos += 2
                out += bytes([status, d1, d2])
    return bytes(out), pc_count


def convert_file(path):
    data = open(path, "rb").read()
    if data[0:4] != b"MThd":
        raise SystemExit(f"{path}: not a MIDI file")
    hdr_len = struct.unpack(">I", data[4:8])[0]
    header = data[8:8 + hdr_len]
    ntrks = struct.unpack(">H", header[2:4])[0]

    out = bytearray(data[0:8 + hdr_len])
    pos = 8 + hdr_len
    total_pcs = 0
    for _ in range(ntrks):
        if data[pos:pos + 4] != b"MTrk":
            raise SystemExit(f"{path}: malformed track chunk at {pos}")
        length = struct.unpack(">I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + length]
        pos += 8 + length
        new_body, pcs = convert_track(body)
        total_pcs += pcs
        out += b"MTrk" + struct.pack(">I", len(new_body)) + new_body

    base, ext = os.path.splitext(path)
    dst = f"{base} (cc85){ext}"
    open(dst, "wb").write(out)
    print(f"{os.path.basename(path)}: {total_pcs} program change(s) -> CC85; wrote {os.path.basename(dst)}")
    return total_pcs


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    for p in sys.argv[1:]:
        convert_file(p)
