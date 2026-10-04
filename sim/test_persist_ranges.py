#!/usr/bin/env python3
"""The Pocket clamps every persisted word to the range declared for it in interact.json (B-600). ReplayGain's mode lives in bits 4-5 of the SW_POL word (id 28 'theme mode'), and that
variable used to declare max 1: the saved value was clamped to polarity alone, the settings poll then saw a changed word and reset ReplayGain to Off, so the setting always read Off.
This test refuses a declared range that is smaller than the largest value the firmware writes into a packed word."""
import json, re, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
iv = {v["id"]: v for v in json.loads((ROOT / "dist/Cores/alfatreze.TAU/interact.json").read_text())["interact"]["variables"]}
settings = (ROOT / "fw/settings.inc").read_text()
m = re.search(r"set_wr32\(SW_POL,\s*\(th_pol \? 1u : 0u\) \| \(\(uint32_t\)rg_mode << (\d+)\)\)", settings)
fails = 0
def check(name, ok, detail=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(detail))); fails += 0 if ok else 1
check("SW_POL is still written as polarity | rg_mode << 4 (update this test if the packing changes)", bool(m) and m.group(1) == "4")
need = 1 | (2 << 4)                                           # polarity Light + ReplayGain Album
word = iv[28]
check(f"'{word['name']}' (id 28) allows the largest packed value {need} (declared max {word['graphical']['max']})", word["graphical"]["max"] >= need, word["graphical"])
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
