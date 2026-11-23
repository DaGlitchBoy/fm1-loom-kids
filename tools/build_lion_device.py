#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Build the separate LION KIDS FM-1 profile with the pinned fm1-base builder."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DEVICE_PREFIX = "FM-1_736"
PREVIEW_VERSION = "0.0.2"
PRODUCT = DEVICE_PREFIX + "".join(f"{int(part):02d}" for part in PREVIEW_VERSION.split("."))
INSTALL_URL = "https://vmphilemon-glitch.github.io/fm1-loom-kids/"


def main():
    args = argparse.ArgumentParser(description=__doc__)
    args.add_argument("--host", action="store_true", help="build this profile's simulator")
    options = args.parse_args()
    output = ROOT / "app" / "build" / "lion-device"
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="lion-device-", dir=output) as tmp:
        stage = Path(tmp)
        for name in ("app", "base", "tools"):
            shutil.copytree(ROOT / name, stage / name, ignore=shutil.ignore_patterns(".git", "build", "__pycache__"))
        if options.host:
            tests = stage / "tests" / "host"
            tests.mkdir(parents=True)
            shutil.copyfile(ROOT / "tests" / "host" / "instr_count.h", tests / "instr_count.h")
        docs = stage / "docs" / "img"
        docs.mkdir(parents=True)
        shutil.copyfile(ROOT / "docs" / "img" / "lion-kids-home.png", docs / "lion-kids-home.png")

        # The app header is included in the platform unity build. The O2 units
        # need the same flag because it selects their pattern layout too.
        app_header = stage / "app" / "app.h"
        app_header.write_text("#define LION_KIDS_DEVICE 1\n" + app_header.read_text())
        config = stage / "app" / "fm1.toml"
        profile = {
            "name": '"lion-kids"',
            "id_prefix": f'"{DEVICE_PREFIX}"',
            "usb_product": '"LION KIDS FM-1"',
            "usb_manufacturer": '"LION KIDS"',
            "pool_spare": "8192",
            "screenshot": '"docs/img/lion-kids-home.png"',
            "description": '"An eight-track groovebox with live playing and a step sequencer. Based on LOOM-8."',
            "repo": '"https://github.com/vmphilemon-glitch/fm1-loom-kids"',
            "install": f'"{INSTALL_URL}"',
            "site": f'"{INSTALL_URL}"',
        }
        for feature in ("ble", "ble_pairing", "ble_central", "bt_audio", "usb_audio_in", "usb_host", "mirror"):
            profile[feature] = "false"
        lines = []
        for line in config.read_text().splitlines():
            key = line.partition("=")[0].strip()
            if key in ("usb_audio_first", "usb_audio_channels"):
                continue
            lines.append(f"{key} = {profile[key]}" if key in profile else line)
        lines.append('o2_cflags = ["-DLION_KIDS_DEVICE=1"]')
        config.write_text("\n".join(lines) + "\n")
        flags = ["--host"] if options.host else ["--release", PREVIEW_VERSION]
        subprocess.run([sys.executable, "base/tools/build.py", "app", *flags], cwd=stage, check=True)
        names = ("host/lion-kids_host",) if options.host else (
            f"lion-kids-{PREVIEW_VERSION}.fwsc", "lion-kids.bin",
            f"lion-kids-{PREVIEW_VERSION}-res.bin", "lion-kids.elf",
            "loader/ota.bin", "firmware.json")
        for name in names:
            source = stage / "app" / "build" / name
            dest = output / source.name.replace(f"-{PREVIEW_VERSION}", "")
            shutil.copy2(source, dest)
            print(dest.relative_to(ROOT))

if __name__ == "__main__":
    main()
