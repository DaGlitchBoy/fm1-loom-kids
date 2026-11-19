# SPDX-License-Identifier: MPL-2.0
"""Exercise the native journal against fresh processes and separate slot files."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import zlib


host = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="loom-kids-shelf-") as tmp:
    root = Path(tmp)
    env = dict(os.environ, LOOM_KIDS="1", LOOM_KIDS_SHELF=str(root), LOOM_KIDS_PIN="1234")
    script = root / "shelf.xy"

    def run(lines):
        script.write_text("wait 300\n" + lines + "\n")
        result = subprocess.run([str(host), str(script), str(root)], env=env, text=True, capture_output=True)
        if result.returncode:
            print(result.stdout, result.stderr)
            raise SystemExit(result.returncode)
        return result.stdout

    def slot(n):
        return (root / f"song-{n}.ks").read_bytes()

    def crc(data):
        value = zlib.crc32(data[12:])
        return value if value < 2**31 else value - 2**32

    adult = "tap GLO\n" * 5
    pin = "".join(f"tapkey w{i}\n" for i in range(4)) + "tap SAVE\n"
    run("tap SAVE\nspin K1 -12\ntap SAVE\nspin K1 -8\ntap SAVE\nexpect kids_shelf_mask 7")
    one, two, three = (slot(i) for i in (1, 2, 3))
    # The undo restores the exact old payload, with a new revision to reject stale clients.
    run("spin K1 -17\ntap SAVE\nexpect kids_dialog 2\n" + pin +
        "expect kids_dialog 4\ntap SAVE\npress HOME\nspin K1 -1\n" +
        f"expect kids_slot_crc.1 {crc(one)}\nrelease HOME\n")
    assert slot(1)[12:] == one[12:]
    assert slot(2) == two and slot(3) == three

    run(adult + "tap HOME\nspin K1 -24\ntap SAVE\npress HOME\nspin K1 -1\n" +
        f"expect kids_slot_crc.1 {crc(one)}\nspin K1 1\nrelease HOME\n")
    changed = slot(1)
    assert changed[12:] != one[12:]
    assert slot(2) == two and slot(3) == three

    run(adult + "spin K4 1\ntap SAVE\nexpect kids_slot_size.1 0\npress HOME\nspin K1 -1\n" +
        f"expect kids_slot_crc.1 {crc(changed)}\nspin K1 1\nexpect kids_slot_size.1 0\n" +
        "spin K1 -1\nrelease HOME\n")
    assert slot(1)[12:] == changed[12:]
    assert slot(2) == two and slot(3) == three

    # File I/O failure leaves the old file and the current editing history intact.
    before = slot(1)
    run(adult + "tap HOME\nspin K1 -11\nkids-shelf-fail\ntap SAVE\nexpect kids_undo 1\nexpect dirty 1\n")
    assert slot(1) == before and slot(2) == two and slot(3) == three

    # Keep a dirty working copy in a free slot before opening a different song.
    run(adult + "spin K3 2\nspin K4 1\ntap SAVE\nspin K3 1\n" + adult +
        "tap HOME\nspin K1 -26\ntap GLO\ntap EDIT\nexpect kids_dialog 1\ntap SAVE\n" +
        "expect kids_dialog 0\nexpect kids_shelf_mask 7\nexpect par1.level 76\n" +
        "spin K3 2\ntap EDIT\nexpect par1.level 74\n")
    copy = slot(3)
    assert copy[12:] != three[12:]
    assert slot(1)[12:] == before[12:] and slot(2) == two

    # CRC failure leaves the dirty song, selected slot, and history unchanged.
    corrupt = bytearray(slot(1))
    corrupt[12 + 16] ^= 1  # corrupt the raw checksum in the compressed song header
    (root / "song-1.ks").write_bytes(corrupt)
    run("spin K1 -18\ntap GLO\ntap EDIT\nexpect kids_dialog 1\ntap EDIT\n" +
        "expect par1.level 82\nexpect kids_undo 1\nexpect dirty 1\n")
    assert slot(1) == corrupt and slot(2) == two and slot(3) == copy

    # A new process without PIN has no protected overwrite path until the adult gesture.
    del env["LOOM_KIDS_PIN"]
    run("tap SAVE\nexpect kids_dialog 0\nexpect kids_undo 0\n" +
        "tap GLO\nspin K4 1\nexpect kids_dialog 0\nexpect kids_shelf_mask 7\n")
    assert slot(1) == corrupt and slot(2) == two and slot(3) == copy
    kits = root / "kits"
    kits.mkdir()
    env["LOOM_KIDS_SHELF"] = str(kits)
    run("param 2 engine 8\nparam 3 engine 8\ntap SAVE\n" +
        "param 1 engine 0\nparam 2 engine 0\nparam 3 engine 0\n" +
        "param 6 engine 8\nparam 7 engine 8\nparam 8 engine 8\ntap SAVE\n" +
        "tap GLO\nspin K3 -1\ntap EDIT\nexpect kids_kit_live.1 1\n" +
        "expect kids_kit_live.2 1\nexpect kids_kit_live.3 1\nexpect kids_kit_live.8 0\n" +
        "spin K3 1\ntap EDIT\nexpect kids_kit_live.1 0\n" +
        "expect kids_kit_live.6 1\nexpect kids_kit_live.7 1\nexpect kids_kit_live.8 1")

    samples = root / "samples"
    samples.mkdir()
    env["LOOM_KIDS_SHELF"] = str(samples)
    run("param 2 engine 6\nparam 2 e1 16\ntap SAVE")
    run("spin K1 -25\ntap GLO\ntap EDIT\ntap EDIT\n" +
        "expect par1.level 75\nexpect kids_undo 1\nexpect dirty 1")
    live = root / "live"
    live.mkdir()
    env["LOOM_KIDS_SHELF"] = str(live)
    run("tap REC\ntap PLAY\ntapkey b0\ntap SAVE\nexpect kids_rec 2\nexpect kids_shelf_mask 1")
    run("tap GLO\ntap EDIT\nexpect note 1 1 2 1")
print("Compared exact slot payloads, unaffected slot files, CRC rejection and atomic failure recovery")
