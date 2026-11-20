#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Assemble a local, single-candidate FM-1 web installer from a checked build."""
import hashlib
import html
import json
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "base" / "tools"))
from make_site import product_of, read_conf, strip_module

SOURCE = "https://github.com/vmphilemon-glitch/fm1-loom-kids"
PRODUCT = "FM-1_73600"

def main():
    if len(sys.argv) != 2:
        sys.exit("usage: make_lion_installer.py OUTPUT_DIRECTORY")
    out = Path(sys.argv[1])
    build = ROOT / "app" / "build" / "lion-device"
    package = build / "lion-kids.fwsc"
    resources = build / "lion-kids-res.bin"
    raw = package.read_bytes()
    if product_of(raw) != PRODUCT or b"FELUCCA-LOADER-1" not in raw:
        sys.exit("The package does not match the LION KIDS device profile.")
    web = ROOT / "base" / "web"
    page = (web / "installer.html").read_text()
    check = "    if (productOf(pkg) !== meta.product)"
    if page.count(check) != 1:
        sys.exit("The base installer package check changed.")
    page = page.replace(check, '''    const hash = Array.from(new Uint8Array(await crypto.subtle.digest("SHA-256", pkg)),
        b => b.toString(16).padStart(2, "0")).join("");
    if (hash !== meta.sha256 || productOf(pkg) !== meta.product)''')
    lib = "\n".join(strip_module((web / file).read_text()) for file in ("fm1ports.js", "fm1pkg.js", "fm1ota.js"))
    meta = {
        "version": "device preview",
        "product": PRODUCT,
        "pkg": package.name,
        "sha256": hashlib.sha256(raw).hexdigest(),
        "res": resources.name,
        "backup": False,
    }
    config = read_conf(ROOT / "app" / "fm1.toml")
    values = {
        "NAME": "LION KIDS", "REPO": SOURCE, "REPO_SHORT": SOURCE.removeprefix("https://"),
        "LICENSE": "GPL-3.0-only", "CREDITS": "Based on LOOM-8 by Charles Vestal. " + config["credits"],
        "RES": config["resources_what"], "RES_JA": config["resources_what_ja"],
    }
    for key, value in values.items():
        page = page.replace("{{" + key + "}}", html.escape(value))
    for marker, value in (("/*LIB*/", lib), ("/*META*/", json.dumps(meta)), ("/*APP*/", "null")):
        if page.count(marker) != 1:
            sys.exit(f"Unexpected installer template marker {marker}")
        page = page.replace(marker, value)
    out.mkdir(parents=True, exist_ok=True)
    (out / "index.html").write_text(page, encoding="utf-8")
    for path in (package, resources, ROOT / "COPYING", ROOT / "LICENSING.md",
                 ROOT / "docs" / "FM1_DEVICE_PREVIEW.md"):
        shutil.copy2(path, out / path.name)
    licenses = out / "platform-licenses"
    licenses.mkdir(exist_ok=True)
    for path in (ROOT / "base" / "LICENSES").glob("*.txt"):
        shutil.copy2(path, licenses / path.name)
    with (out / "SHA256SUMS").open("w") as sums:
        for path in (package, resources, out / "index.html"):
            sums.write(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n")

if __name__ == "__main__":
    main()
