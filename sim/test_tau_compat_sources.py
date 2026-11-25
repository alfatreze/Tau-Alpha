#!/usr/bin/env python3
"""D-T20: tools/tau_compat.py (the release's tau-compat.json for Tau Omega) reads its firmware facts from Player 2 (fw2/) and the headers that stay in fw/,
never from the retired old player (fw/*.inc, fw/player.c).

  - assets_sections = the 4-letter sections Player 2 looks up in tau-assets.bin (as_find / assets_section calls in fw2/): THEM, PRST, METR, the same three the
    old player's as_find calls gave, so the manifest does not change;
  - the cover sidecar name = the one in fw2/stream/library.c (the old fw/timg.inc held the same string);
  - nothing under fw/ except *.h is opened (an .inc or player.c read is the old player coming back)."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import tau_compat as tc

fails = 0
def check(msg, cond):
    global fails
    print(("ok   " if cond else "FAIL ") + msg)
    fails += not cond

opened = []
real = Path.read_text
def spy(self, *a, **k):
    s = str(self).replace("\\", "/")
    if "/fw/" in s and not s.endswith(".h") or s.endswith("/player.c"):
        opened.append(s)
    return real(self, *a, **k)
Path.read_text = spy
try:
    facts = tc.tree_facts(ROOT)
    fmts = tc.firmware_formats(ROOT)
finally:
    Path.read_text = real
check("assets_sections are METR, PRST, THEM", facts["assets_sections"] == ["METR", "PRST", "THEM"])
check("the cover sidecar name is tau-art/cover_128.pal256.timg", fmts["cover"]["tail"] == "tau-art/cover_128.pal256.timg")
check("no .inc or player.c under fw/ is opened", not opened)
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
