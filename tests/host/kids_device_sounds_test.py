# SPDX-License-Identifier: MPL-2.0
"""Exercise every factory sample preset on the native device profile."""
from array import array
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import wave

from PIL import Image, ImageChops

root = Path(__file__).resolve().parents[2]
presets = json.loads(subprocess.check_output(
    [sys.executable, str(root / "tools/gen_presets.py"), "--json"]
))
host = Path(sys.argv[1]).resolve()
# PRESET detents relative to the first track's Bass category. Kits have their own list.
categories = {
    -1: {"keys", "chord", "stab"},
    0: {"bass"},
    2: {"pad", "strings", "voice"},
    3: {"lead", "brass"},
    4: {"pluck", "bell"},
}
named_roles = set().union(*categories.values())
categories[5] = {p["role"] for p in presets} - named_roles

with tempfile.TemporaryDirectory(prefix="lion-native-sounds-") as temp:
    out = Path(temp)
    script = out / "check.xy"

    def run(lines):
        script.write_text("wait 300\n" + "\n".join(lines) + "\n")
        result = subprocess.run([host, script, out], text=True, capture_output=True)
        if result.returncode:
            sys.exit(result.stdout + result.stderr)

    tested = 0
    for detents, roles in categories.items():
        catalog = [p for p in presets if p["role"] in roles and p["engine"] not in ("midi", "drumline")]
        for index, preset in enumerate(catalog):
            if preset["engine"] not in ("sample", "slice"):
                continue
            name = preset["name"].replace(" ", "_")
            keys = range(16) if preset["engine"] == "slice" else (7,)
            phrase = [action for key in keys for action in
                      (f"key w{key} down", "wait 700", f"key w{key} up", "wait 300")]
            run([
                f"spin PRESET {detents}", f"expect kids_sounds {len(catalog)}",
                "spin ALGO -1000", f"spin ALGO {index}", f"expect kids_sound {index + 1}",
                f"expect kids_preset.{name} 1",
                "wav note.wav", *phrase, "wavstop",
            ])
            with wave.open(str(out / "note.wav"), "rb") as wav:
                samples = array("h", wav.readframes(wav.getnframes()))
                if sys.byteorder == "big":
                    samples.byteswap()
                assert max(abs(s) for s in samples) > 200, f"Silent preset: {preset['name']}"
            tested += 1
    assert tested == sum(p["engine"] in ("sample", "slice") for p in presets), tested

    run([
        "shot home.png", "tap LFO", "wait 150", "expect kids 5", "shot lfo.png",
        "tap HOME", "wait 150", "expect kids 1", "shot returned.png",
        "turn K1 -12 20", "expect par1.level <76", "turn K2 1", "expect kids_length 32",
        "turn PRESET -1", "expect kids_category 0", "spin ALGO -1000",
        "turn ALGO 3 20", "expect kids_sound 4",
    ])
    home = Image.open(out / "home.png").convert("RGB")
    assert ImageChops.difference(home, Image.open(out / "lfo.png").convert("RGB")).getbbox()
    assert not ImageChops.difference(home, Image.open(out / "returned.png").convert("RGB")).getbbox()
    print(f"{tested} factory sample presets produced audio. HOME redraw and rotary speed checks passed.")
