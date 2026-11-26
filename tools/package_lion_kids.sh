#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
cd "$(dirname "$0")/.."
test -s app/build/emu/app.wasm
test "$(git rev-parse HEAD:base)" = "$(git -C base rev-parse HEAD)"
test "$(git -C base rev-parse HEAD:bsp)" = "$(git -C base/bsp rev-parse HEAD)"
git diff --quiet HEAD
git -C base diff --quiet HEAD
git -C base/bsp diff --quiet HEAD

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
out="$PWD/build/lion-kids-release"
mkdir -p "$out" "$tmp/lion-kids-source/base/bsp" "$tmp/lion-kids-web"
git archive HEAD > "$tmp/main.tar"
git -C base archive HEAD > "$tmp/base.tar"
git -C base/bsp archive HEAD > "$tmp/bsp.tar"
tar -xf "$tmp/main.tar" -C "$tmp/lion-kids-source"
tar -xf "$tmp/base.tar" -C "$tmp/lion-kids-source/base"
tar -xf "$tmp/bsp.tar" -C "$tmp/lion-kids-source/base/bsp"
tar -czf "$out/lion-kids-source.tar.gz" -C "$tmp" lion-kids-source

cp app/build/emu/index.html app/build/emu/app.wasm app/build/emu/*.js "$tmp/lion-kids-web/"
mkdir -p "$tmp/lion-kids-web/fonts"
cp base/assets/fonts/BarlowSemiCondensed-SemiBold.ttf base/assets/fonts/BarlowSemiCondensed-Bold.ttf "$tmp/lion-kids-web/fonts/"
cp base/assets/fonts/Barlow-OFL.txt "$tmp/lion-kids-web/fonts/"
python3 - "$tmp/lion-kids-web/index.html" <<'PYFONT'
from pathlib import Path
import re
import sys

page = Path(sys.argv[1])
source = re.sub(r'^<link [^>]*fonts\.(?:googleapis|gstatic)\.com[^>]*>\n', '', page.read_text(), flags=re.M)
styles = '''<style>
@font-face { font-family: "Barlow Semi Condensed"; font-weight: 600; font-display: swap; src: url("./fonts/BarlowSemiCondensed-SemiBold.ttf"); }
@font-face { font-family: "Barlow Semi Condensed"; font-weight: 700; font-display: swap; src: url("./fonts/BarlowSemiCondensed-Bold.ttf"); }
</style>
'''
page.write_text(source.replace("</head>", styles + "</head>", 1))
PYFONT
for assets in parts reader; do
    if [ -d "app/build/emu/$assets" ]; then
        cp -R "app/build/emu/$assets" "$tmp/lion-kids-web/"
    fi
done
cp COPYING LICENSE LICENSING.md docs/LION_KIDS_MANUAL.md "$tmp/lion-kids-web/"
mkdir -p "$tmp/lion-kids-web/img"
cp docs/img/lion-kids-steps.png docs/img/lion-kids-performance.png "$tmp/lion-kids-web/img/"
git rev-parse HEAD > "$tmp/lion-kids-web/SOURCE-COMMIT.txt"
(
    cd "$tmp"
    python3 -m zipfile -c "$out/lion-kids-web.zip" lion-kids-web
)
(
    cd "$out"
    sha256sum lion-kids-web.zip lion-kids-source.tar.gz > SHA256SUMS
)
