# SPDX-License-Identifier: MPL-2.0
"""Check the audio recorded by the native Kids effects scenario."""
import math
from pathlib import Path
import struct
import sys
import wave


def read(name):
    with wave.open(str(Path(sys.argv[1]) / (name + ".wav"))) as wav:
        rate = wav.getframerate() * wav.getnchannels()
        pcm = wav.readframes(wav.getnframes())
    return rate, [v for (v,) in struct.iter_unpack("<h", pcm)]


def rms(recording, start, end):
    rate, samples = recording
    window = samples[round(start * rate):round(end * rate)]
    return math.sqrt(sum(v * v for v in window) / len(window))


dry, reverb, delay, both = (read(name) for name in ("dry", "reverb", "delay", "both"))
assert rms(dry, 0.00, 0.16) > 40, "the source drum hit must play"
assert rms(dry, 0.24, 0.34) < 1, "the dry drum must have ended"
assert rms(reverb, 0.16, 0.23) > 15, "reverb tail missing after bypass"
assert rms(delay, 0.16, 0.23) < 6, "delay must wait for its synced 1/8 echo"
assert rms(delay, 0.24, 0.34) > 20, "delay echo missing after bypass"
assert rms(both, 0.16, 0.23) > 15, "reverb absent with both enabled"
assert rms(both, 0.24, 0.34) > 1.5 * rms(reverb, 0.24, 0.34), "echo absent with both enabled"
low, high = (rms(read(name), 0, 0.16) for name in ("click-low", "click-high"))
assert low > 20 and 2.9 < high / low < 3.1, "75% click must be three times 25%"
print("Kids effect tails, combined returns and metronome gain verified")
