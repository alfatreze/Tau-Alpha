#!/usr/bin/env python3
"""Host test for tools/halcyon_assets.py (Halcyon PRST section and APO importer, B-626), and that tools/tau_assets.py parses a container carrying it."""
import os, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import halcyon_assets as h
import tau_assets as ta
fails = h.selftest(verbose=True)
blob = h.pack_presets([{"name": "SOFT", "controls": {"warmth": 1, "air": -1}}])
ok = ta.parse(ta.pack_container([(b"PRST", blob)]))["presets"][0]["name"] == "SOFT"
print(("ok   " if ok else "FAIL ") + "tau-assets.bin container carrying PRST is parsed by tau_assets.parse")
sys.exit(1 if fails or not ok else 0)
