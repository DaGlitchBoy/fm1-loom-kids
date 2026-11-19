#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Exercise the actual emulator page composition without compiling WASM."""
import importlib.util
from pathlib import Path
from tempfile import TemporaryDirectory

spec = importlib.util.spec_from_file_location("emu_page", "tools/emu_page.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
with TemporaryDirectory() as directory:
    module.PAGE = Path(directory) / "index.html"
    base = Path("base/web/emu/index.html").read_text()
    panel = Path("base/web/fm1panel.js").read_text()
    module.PAGE.write_text(base.replace("{{NAME}}", "loom").replace("{{ID}}", "loom").replace("{{PANEL_JS}}", panel))
    module.main()
    page = module.PAGE.read_text()
    assert "<title>LOOM Kids in the browser</title>" in page
    assert page.count('id="go">LOOM Kids starten</button>') == 1
    assert 'id="kids-entry"' not in page
    assert "{{" not in page
    assert "PRESETS</b> die Klanggruppe" in page
print("Kids browser page: real template composition, direct entry, matching control help")
