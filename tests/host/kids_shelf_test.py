# SPDX-License-Identifier: MPL-2.0
"""Exercise the native journal against fresh processes and separate slot files."""
import os
from pathlib import Path
from random import Random
import struct
import subprocess
import sys
import tempfile
import zlib


host = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="loom-kids-shelf-") as tmp:
    root = Path(tmp)
    env = dict(os.environ, LOOM_KIDS="1", LOOM_KIDS_SHELF=str(root))
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

    open_one = "tap GLO\nspin K4 -3\ntap EDIT\ntap HOME\n"
    names = "tap GLO\npress EDIT\nwait 420\nrelease EDIT\n"
    run("tap SAVE\ntap GLO\nspin K4 1\ntap EDIT\ntap SAVE\nspin K4 1\ntap EDIT\ntap SAVE\nexpect kids_shelf_mask 7")
    one, two, three = (slot(i) for i in (1, 2, 3))
    run(open_one + "spin K1 -17\ntap SAVE\npress HOME\nspin K1 -1\n" +
        f"expect kids_slot_crc.1 {crc(one)}\nrelease HOME\n")
    assert slot(1)[12:] == one[12:]
    assert slot(2) == two and slot(3) == three

    run(open_one + "spin K1 -24\ntap GLO\nspin K4 2\ntap SAVE\npress HOME\nspin K1 -1\n" +
        f"expect kids_slot_crc.1 {crc(one)}\nspin K1 1\nrelease HOME\n")
    changed = slot(1)
    assert changed[12:] != one[12:]
    assert slot(2) == two and slot(3) == three

    run("tap GLO\nspin K4 -3\npress GLO\nwait 5050\nrelease GLO\ntap EDIT\nexpect kids_slot_size.1 0\npress HOME\nspin K1 -1\n" +
        f"expect kids_slot_crc.1 {crc(changed)}\nspin K1 1\nexpect kids_slot_size.1 0\n" +
        "spin K1 -1\nrelease HOME\n")
    assert slot(1)[12:] == changed[12:]
    assert slot(2) == two and slot(3) == three

    # Clearing the loaded song resets its live notes and sounds. Undo also
    # restores unsaved changes, and the saved picture survives a restart.
    clear = "press GLO\nwait 5050\nrelease GLO\ntap EDIT\n"
    run(open_one + names + "spin K1 10\ntap EDIT\ntap HOME\ntap HOME\n" +
        "tap SEQ\ntap SEQ\ntapkey w0\ntap HOME\ntap SAVE\nspin K1 -5\ntap GLO\n" +
        "kids-shelf-fail\n" + clear + "expect kids_shelf_mask 7\nexpect par1.level 71\n" +
        clear + "expect kids_shelf_mask 6\nexpect par1.level 100\n" +
        "expect note 1 1 1 0\nexpect kids_name_id 11\nexpect dirty 0\n" +
        "press HOME\nspin K1 -1\nexpect kids_shelf_mask 7\nexpect par1.level 71\n" +
        "expect note 1 1 1 60\nexpect dirty 1\nspin K1 1\nexpect kids_shelf_mask 6\n" +
        "expect par1.level 100\nexpect note 1 1 1 0\nexpect dirty 0\nrelease HOME")
    run("expect kids_loaded 1\nexpect kids_shelf_mask 6\nexpect kids_name_id 11\n" +
        "expect par1.level 100\nexpect note 1 1 1 0")
    assert slot(2) == two and slot(3) == three
    (root / "song-1.ks").write_bytes(changed)

    before = slot(1)
    run(open_one + "spin K1 -11\nkids-shelf-fail\ntap SAVE\nexpect kids_undo 2\nexpect dirty 1\n")
    assert slot(1) == before and slot(2) == two and slot(3) == three

    # A rename commits only the picture. It leaves the saved music bytes exact.
    run(open_one + "spin K1 -10\n" + names + "spin K1 10\ntap EDIT\nexpect dirty 1\ntap HOME\n")
    renamed = slot(1)
    assert renamed[12:73] != before[12:73] and renamed[73:] == before[73:]
    assert slot(2) == two and slot(3) == three
    run(open_one + "expect par1.level 76\nexpect kids_name_id 11")

    # A v1 song still opens with its music and can acquire a v3 pixel name.
    legacy = bytearray(three[:12 + 24] + three[73:])
    struct.pack_into("<I", legacy, 8, len(legacy) - 12)
    struct.pack_into("<I", legacy, 12 + 4, 1)
    (root / "song-3.ks").write_bytes(legacy)
    run("tap GLO\nspin K4 3\ntap EDIT\nexpect kids_loaded 3\n" +
        "press EDIT\nwait 420\nrelease EDIT\nspin K1 11\ntap EDIT\n")
    migrated = slot(3)
    assert struct.unpack_from("<I", migrated, 16)[0] == 3
    assert migrated[73:] == legacy[36:]
    assert slot(2) == two

    # Preserve an older 12x12 custom name and its music when the song boots.
    fixed = bytearray(three[:36])
    struct.pack_into("<I", fixed, 16, 2)
    pixels = bytearray(72)
    pixels[(5 * 12 + 4) // 2] = 6
    payload = fixed + struct.pack("<I", zlib.crc32(pixels)) + pixels + three[73:]
    struct.pack_into("<I", payload, 8, len(payload) - 12)
    (root / "song-3.ks").write_bytes(payload)
    run("expect kids_loaded 3\nexpect kids_pixel.0.2.3 6\ntap SAVE")
    migrated = slot(3)
    assert struct.unpack_from("<I", migrated, 16)[0] == 3
    assert migrated[73:] == three[73:]

    # A corrupt slot and a stale write leave the working song and history intact.
    corrupt = bytearray(slot(1))
    corrupt[12 + 16] ^= 1
    (root / "song-1.ks").write_bytes(corrupt)
    run("spin K1 -18\ntap GLO\nspin K4 -3\ntap EDIT\nexpect kids_dialog 1\ntap EDIT\n" +
        "expect par1.level 82\nexpect kids_undo 1\nexpect dirty 1\nexpect kids_loaded 3")
    assert slot(1) == corrupt and slot(2) == two and slot(3) == migrated
    run("tap GLO\nspin K4 -1\ntap EDIT\nkids-shelf-conflict 2\ntap SAVE\nexpect kids_undo 1")
    assert slot(2)[12:] == two[12:]

    kits = root / "kits"
    kits.mkdir()
    env["LOOM_KIDS_SHELF"] = str(kits)
    run("param 1 engine 8\nparam 2 engine 8\nparam 3 engine 8\ntap SAVE\ntap GLO\nspin K4 1\ntap EDIT\ntap HOME\n" +
        "param 1 engine 0\nparam 2 engine 0\nparam 3 engine 0\n" +
        "param 6 engine 8\nparam 7 engine 8\nparam 8 engine 8\ntap SAVE\n" +
        "tap GLO\nspin K4 -1\ntap EDIT\nexpect kids_kit_live.1 1\n" +
        "expect kids_kit_live.2 1\nexpect kids_kit_live.3 1\nexpect kids_kit_live.8 0\n" +
        "spin K4 1\ntap EDIT\nexpect kids_kit_live.1 0\n" +
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
    run("tap REC\ntap PLAY\ntapkey w0\ntap SAVE\nexpect kids_rec 2\nexpect kids_shelf_mask 1")
    run("expect note 1 1 2 53")

    dense = root / "dense"
    dense.mkdir()
    env["LOOM_KIDS_SHELF"] = str(dense)
    actions = ["tap GLO", "spin K4 1", "tap EDIT", "tap HOME", "expect kids_loaded 2"]
    checks = ["expect kids_loaded 2", "expect kids_shelf_mask 2"]
    rng = Random(12345)
    for track in range(1, 7):
        if track > 1:
            actions.append("spin SELECT 1")
        actions += ["tap FX", "spin K1 18", "tap SEL", "spin K1 24",
                    "tap EDIT", "spin K1 26", "spin K2 15", "spin K3 10",
                    "spin K4 16", "tap HOME", f"pat {track} 1 64"]
        if track == 3:
            for sound in (1, 3, 5):
                hits = "".join(rng.choice("xogo") for _ in range(64))
                actions.append(f"hits 3 1 {sound} {hits}")
        else:
            for step in range(1, 65):
                notes = sorted(rng.sample(range(47, 83), 3))
                length = rng.choice((0.5, 1, 1.5))
                velocity = rng.randrange(48, 115)
                pitches = " ".join(map(str, notes))
                actions.append(f"notes {track} 1 {step} {length} {velocity} {pitches}")
                for index, note in enumerate(notes, start=1):
                    checks.append(f"expect noteat {track} 1 {step} {index} {note}")
                checks.append(f"expect stepvel {track} 1 {step} {velocity}")
        checks += [f"expect kids_send.{track}.1 72", f"expect kids_send.{track}.2 70"]
    run("\n".join(actions + ["kids-flash-check", "tap SAVE", "expect kids_shelf_mask 2"]))
    record = dense / "song-2.ks"
    before = record.read_bytes()
    run("\n".join(checks + ["tap PLAY", "wait 10000", "tap PLAY"]))
    assert record.read_bytes() == before
print("Compared exact slot payloads, unaffected slot files, CRC rejection and atomic failure recovery")
