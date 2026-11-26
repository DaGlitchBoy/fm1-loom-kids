#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare every saved object, not just the currently displayed project."""
from pathlib import Path
import sys

root = Path(sys.argv[1])
original = (root / "original.store").read_bytes()
assert original, "baseline store missing"
for name in ("after_kids", "after_reboot"):
    assert (root / f"{name}.store").read_bytes() == original, f"saved objects changed: {name}"
print("Kids edits, SAVE and reboot preserve every saved byte")
