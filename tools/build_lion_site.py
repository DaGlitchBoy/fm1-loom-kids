#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Assemble the LION KIDS GitHub Pages site from verified release bundles."""
import argparse
from hashlib import sha256
from pathlib import Path
import shutil
import subprocess
import tempfile
from zipfile import ZipFile

from build_lion_device import PREVIEW_VERSION

ROOT = Path(__file__).resolve().parents[1]
TEMPLATES = ROOT / "app" / "web" / "site"
SHOTS = (
    "lion-kids-home.png", "lion-kids-performance.png", "lion-kids-steps.png", "kids-shaping-lfo.png",
)


def verify(folder):
    for line in (folder / "SHA256SUMS").read_text().splitlines():
        expected, name = line.split("  ", 1)
        path = folder / name
        if not path.resolve().is_relative_to(folder.resolve()):
            raise SystemExit(f"Checksum path outside bundle: {name}")
        if sha256(path.read_bytes()).hexdigest() != expected:
            raise SystemExit(f"Checksum mismatch: {name}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("firmware", type=Path, help="unpacked firmware installer artifact")
    parser.add_argument("browser", type=Path, help="directory from tools/package_lion_kids.sh")
    parser.add_argument("output", type=Path, help="a new or empty site output directory")
    args = parser.parse_args()
    firmware, browser, out = args.firmware, args.browser, args.output
    if out.exists() and (not out.is_dir() or any(out.iterdir())):
        parser.error("output must be a new or empty directory")
    for bundle in (firmware, browser):
        verify(bundle)
    installer = (firmware / "index.html").read_text()
    if installer.count("</head>") != 1 or f"device preview {PREVIEW_VERSION}" not in installer:
        raise SystemExit("Expected the installer for the current firmware preview.")

    shutil.copytree(firmware, out, dirs_exist_ok=True, ignore=shutil.ignore_patterns(".git"))
    (out / ".nojekyll").touch()
    (out / "install").mkdir()
    installer = installer.replace("</head>", '  <base href="../">\n</head>', 1)
    installer = installer.replace('href="../backup/"', 'href="./export/"')
    installer = installer.replace('href="../" data-t="manual"', 'href="./" data-t="manual"')
    installer = installer.replace("</h1>", '</h1>\n  <p><a href="./">LION KIDS home</a></p>', 1)
    (out / "install" / "index.html").write_text(installer)
    export = out / "export" / "index.html"
    export.write_text(export.read_text().replace('href="../">Firmware installer', 'href="../install/">Firmware installer'))
    (out / "index.html").write_text((TEMPLATES / "index.html").read_text().replace("{{VERSION}}", PREVIEW_VERSION))
    shutil.copy2(TEMPLATES / "site.css", out)
    for name in ("LICENSE", "COPYING", "LICENSING.md", "docs/FM1_DEVICE_PREVIEW.md"):
        shutil.copy2(ROOT / name, out / Path(name).name)
    images = out / "img"
    images.mkdir()
    for name in SHOTS:
        shutil.copy2(ROOT / "docs" / "img" / name, images)
    browser_zip = browser / "lion-kids-web.zip"
    shutil.copy2(browser_zip, out)
    shutil.copy2(browser / "lion-kids-source.tar.gz", out / "lion-kids-emulator-source.tar.gz")
    with tempfile.TemporaryDirectory() as temp:
        temp = Path(temp)
        with ZipFile(browser_zip) as archive:
            archive.extractall(temp)
        shutil.move(temp / "lion-kids-web", out / "emu")
        template = (TEMPLATES / "manual.html").read_text().replace("{{MANUAL}}", '<nav id="TOC" aria-label="On this page">$toc$</nav>\n$body$')
        shell = temp / "manual.html"
        shell.write_text(template)
        manual = (ROOT / "docs" / "LION_KIDS_MANUAL.md").read_text().split("\n", 1)[1]
        html = subprocess.run(
            ["pandoc", "-f", "gfm", "-t", "html5", "--standalone", "--toc", "--toc-depth=2",
             "--template", str(shell)],
            input=manual, capture_output=True, text=True, check=True,
        ).stdout.replace('src="img/', 'src="../img/')
    (out / "manual").mkdir()
    (out / "manual" / "index.html").write_text(html)
    checksums = out / "SHA256SUMS"
    paths = sorted(p for p in out.rglob("*") if p.is_file() and p != checksums and p.name != ".nojekyll")
    checksums.write_text("".join(f"{sha256(p.read_bytes()).hexdigest()}  {p.relative_to(out)}\n" for p in paths))


if __name__ == "__main__":
    main()
