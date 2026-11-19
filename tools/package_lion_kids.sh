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

cp -R app/build/emu/. "$tmp/lion-kids-web/"
cp COPYING LICENSE LICENSING.md docs/LION_KIDS_MANUAL.md "$tmp/lion-kids-web/"
mkdir -p "$tmp/lion-kids-web/img"
cp docs/img/lion-kids-steps.png docs/img/lion-kids-performance.png "$tmp/lion-kids-web/img/"
git rev-parse HEAD > "$tmp/lion-kids-web/SOURCE-COMMIT.txt"
for file in app/build/emu/index.html app/build/emu/app.wasm; do test -s "$file"; done
(
    cd "$tmp"
    python3 -m zipfile -c "$out/lion-kids-web.zip" lion-kids-web
)
(
    cd "$out"
    sha256sum lion-kids-web.zip lion-kids-source.tar.gz > SHA256SUMS
)
