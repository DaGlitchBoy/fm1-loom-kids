# SPDX-License-Identifier: MPL-2.0
"""Measure the Kids gate, bar-rate LFO and effects from simulator WAVs."""
import math
from pathlib import Path
import struct
import sys
import wave


def read(name):
    with wave.open(str(Path(sys.argv[1]) / (name + ".wav"))) as wav:
        assert wav.getnchannels() == 2 and wav.getsampwidth() == 2
        return wav.getframerate(), list(struct.iter_unpack("<hh", wav.readframes(wav.getnframes())))


def frames(name, start, end):
    rate, signal = read(name)
    return signal[round(start * rate):round(end * rate)]


def rms(name, start, end, difference=False):
    data = frames(name, start, end)
    assert data, name
    if difference:
        data = [(l2 - l1, r2 - r1) for (l1, r1), (l2, r2) in zip(data, data[1:])]
    return math.sqrt(sum(l * l + r * r for l, r in data) / (2 * len(data)))


def stereo_difference(name, start, end):
    signal = frames(name, start, end)
    return math.sqrt(sum((l - r) ** 2 for l, r in signal) / len(signal))


# Attack = 98 ms, Hold = 300 ms, Decay = 60 ms, Sustain = 48/127, Release = 205 ms.
held = rms("envelope", 0.20, 0.27)
assert held > 500
assert rms("envelope", 0.01, 0.024) < held * 0.25, "attack did not ramp"
assert 0.90 < rms("envelope", 0.31, 0.38) / held < 1.10, "hold plateau moved"
assert 0.32 < rms("envelope", 0.56, 0.65) / held < 0.43, "sustain differs from the graph"
assert rms("envelope", 0.82, 0.86) > held * 0.05, "release ended prematurely"
assert rms("envelope", 0.90, 0.96) < 2, "release overflowed its 205 ms"
assert rms("early", 0.10, 0.14) < rms("early", 0.038, 0.05) * 0.8, "release did not start during attack"
assert rms("early", 0.27, 0.31) < 2
assert rms("preset-envelope", 0.01, 0.024) > held * 0.7, "bypass did not return to the preset attack"
assert rms("kit-envelope", 0.01, 0.06) < rms("kit-dry", 0.01, 0.06) * 0.25, "kit gate does not shape the hit"

for name, period in (("tremolo", 2), ("tremolo-90", 8 / 3)):
    high = rms(name, 0.27, 0.42)
    assert high > 400
    for offset in (0, period):
        assert 0.15 < rms(name, offset + 0.68 * period, offset + 0.83 * period) / high < 0.30, name
    assert 0.85 < rms(name, period + 0.27, period + 0.42) / high < 1.15, name

assert rms("lfo-cutoff", 1.60, 1.83, True) > 1.15 * rms("lfo-cutoff", 0.24, 0.42, True)
assert rms("lfo-drive", 1.20, 1.40) > 1.25 * rms("lfo-drive", 0.27, 0.42)

dry = rms("zero", 0.13, 0.35)
assert 0.9 < rms("bypass", 0.13, 0.35) / dry < 1.1
assert rms("filter", 0.13, 0.35, True) < 0.85 * rms("zero", 0.13, 0.35, True)
assert rms("drive", 0.13, 0.35) < 0.8 * dry
samples = frames("crush", 0.12, 0.18)
holds = sum(a == b for a, b in zip(samples, samples[1:]))
assert holds > len(samples) * 0.65, "the crusher did not hold reduced-rate samples"
assert stereo_difference("zero", 0.13, 0.35) < 2
assert stereo_difference("chorus", 0.13, 0.35) > dry * 0.12, "chorus did not create a stereo modulation"
assert stereo_difference("kit-quick", 0.01, 0.08) > 1.5 * stereo_difference("kit-dry", 0.01, 0.08)
assert rms("stack", 0.86, 1.08) > 10, "wet returns missing with all four inserts active"
assert max(abs(v) for pair in frames("stack", 0, 1.6) for v in pair) < 0.9 * 32768
assert rms("bypass", 0.46, 0.53) < 2
ref = rms("isolated", 0.16, 0.28)
assert ref > 100
assert 0.8 < rms("isolated", 0.60, 0.70) / ref < 1.2, "effect toggle changed another track"
print("Measured the Kids envelope, bar timing at 120 and 90 BPM, wet and dry effects, kit shaping and track isolation")
