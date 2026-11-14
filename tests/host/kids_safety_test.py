#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare every saved object, not just the currently displayed project."""
from pathlib import Path
import sys

root = Path(sys.argv[1])
original = (root / "original.store").read_bytes()
assert original, "baseline store missing"
for name in ("after_kids", "after_unlock", "after_cancel"):
    assert (root / f"{name}.store").read_bytes() == original, f"saved objects changed: {name}"
saved = (root / "adult_saved.store").read_bytes()
assert saved != original, "explicit adult save did not persist edits"
assert (root / "after_reboot.store").read_bytes() == saved, "locked reboot changed saved objects"
print("Kids, unlock and cancelled save preserve every saved byte; adult save persists explicitly")
