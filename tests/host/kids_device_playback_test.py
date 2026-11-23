# SPDX-License-Identifier: MPL-2.0
"""Check dense Kids playback with active LFOs on the one- and two-core render paths."""
from array import array
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import wave

from PIL import Image, ImageChops


def playback(binary, output, cores):
    output.mkdir()
    events = ["wait 300", f"cores {cores}", f"expect cores {1 if cores else 2}", "master 1900"]
    for track in (1, 2, 4, 5, 6, 7, 8):
        for step in (1, 5, 9, 13):
            root = 48 + track * 2 + (step - 1) // 4
            chord = f"{root} {root + 3} {root + 7}" if track in (2, 4, 5) else str(root)
            events.append(f"notes {track} 1 {step} 3.5 75 {chord}")
    for step in (1, 5, 9, 13):
        events.append(f"notes 3 1 {step} 1 100 1")
    events.extend([
        "spin SELECT 4", "tap EDIT", "spin K2 10", "spin K4 12", "tap HOME",
        "spin SELECT -1", "wav dense.wav", "tap PLAY", "wait 530",
        "expect voices >8", "tap LFO", "expect kids 5", "spin K1 63", "spin K3 3",
        "wait 48", "expect kids_dsp.4.5 1",
        f"expect core2_jobs {'>0' if cores else '0'}",
        "shot lfo-first.png", "wait 230",
        "shot lfo-second.png", "tap HOME", "expect kids 1", "wait 300",
        "tap LFO", "wait 250", "tap LFO", "wait 500", "tap PLAY", "wavstop",
    ])
    scenario = output / "dense.xy"
    scenario.write_text("\n".join(events) + "\n")
    # Real-time CPU contention on the host should not trip the device voice cap.
    env = dict(os.environ, LOOM_LOAD="0")
    result = subprocess.run([str(binary), str(scenario), str(output)], env=env, capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    with wave.open(str(output / "dense.wav")) as wav:
        assert wav.getsampwidth() == 2
        signal = array("h", wav.readframes(wav.getnframes()))
        if sys.byteorder == "big":
            signal.byteswap()
    return signal


binary = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory() as temp:
    out = Path(temp)
    single = playback(binary, out / "single", 0)
    dual = playback(binary, out / "dual", 1)
    assert len(single) == len(dual) > 100_000
    assert max(map(abs, dual)) < 32767, "the dense fixture saturated"
    assert math.sqrt(sum(x * x for x in dual) / len(dual)) > 250, "dense playback went quiet"
    error = math.sqrt(sum((a - b) ** 2 for a, b in zip(single, dual)) / len(dual))
    assert error < 3, f"switching audio cores changed effect routing (RMS difference {error:.1f})"
    with Image.open(out / "dual/lfo-first.png") as first, Image.open(out / "dual/lfo-second.png") as second:
        difference = ImageChops.difference(first.convert("RGB"), second.convert("RGB"))
        assert difference.crop((0, 56, 240, 184)).getbbox() is None, "phase animation repainted the plot"
        assert difference.crop((0, 184, 240, 200)).getbbox(), "the LFO phase marker stopped moving"
print("Dense playback matched across both render paths, and only the LFO phase strip animated.")
