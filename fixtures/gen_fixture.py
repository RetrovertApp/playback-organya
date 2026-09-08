#!/usr/bin/env python3
"""Generate the self-authored playback fixture retrovert_selftest.org.

An Org-02 song built from scratch: four melody channels playing an
arpeggiated figure over 64 steps, looping. Deterministic output — the
committed fixture and its sha256 in harness.toml must match what this
script emits.
"""

import struct
from pathlib import Path

OUT = Path(__file__).parent / "retrovert_selftest.org"

MELODY_CHANNELS = 8
PERCUSSION_CHANNELS = 8
CHANNELS = MELODY_CHANNELS + PERCUSSION_CHANNELS

STEPS = 64
TEMPO_MS = 160  # 64 steps ~= 10.2 s per pass before the loop point

# Pitches are semitone indices 0..95. 48 is the middle of the range.
FIGURE = [48, 52, 55, 60]

VOLUME = 200  # 0..254
PAN = 6  # centre, 0..12
VOICES = 4  # melody channels that carry events


def channel_events(voice):
    """Events for one melody channel: a note every four steps, offset per voice."""
    events = []
    for n, step in enumerate(range(voice, STEPS, VOICES)):
        pitch = FIGURE[(n + voice) % len(FIGURE)]
        events.append((step, pitch, VOICES, VOLUME, PAN))
    return events


def encode_events(events):
    # Events are stored column-major: all positions, then all pitches,
    # lengths, volumes and pans.
    out = b"".join(struct.pack("<I", e[0]) for e in events)
    for column in range(1, 5):
        out += bytes(e[column] for e in events)
    return out


def build():
    tracks = [channel_events(v) for v in range(VOICES)]
    tracks += [[] for _ in range(CHANNELS - VOICES)]

    out = bytearray()
    out += b"Org-02"
    out += struct.pack("<HBB", TEMPO_MS, 4, 4)  # tempo, beats, steps per bar
    out += struct.pack("<II", 0, STEPS)  # loop start, loop end

    for i, events in enumerate(tracks):
        instrument = i % 4 if i < VOICES else 0
        # finetune, instrument (wavetable index), pizzicato, event count
        out += struct.pack("<HBBH", 1000, instrument, 0, len(events))

    for events in tracks:
        out += encode_events(events)

    return bytes(out)


def main():
    data = build()
    OUT.write_bytes(data)
    print(f"wrote {OUT} ({len(data)} bytes)")


if __name__ == "__main__":
    main()
