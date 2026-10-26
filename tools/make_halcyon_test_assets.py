#!/usr/bin/env python3
"""Adds a Halcyon PRST section (two test presets) to a tau-assets.bin, for the first hardware A/B of Diagnostics > HALCYON (B-643).
    python3 tools/make_halcyon_test_assets.py [IN tau-assets.bin] OUT        (IN keeps its THEM/METR sections; without IN the container carries PRST only)
User presets the row then cycles after the eight built-ins:
  CLARITY     control preset: warmth -2, vocal +2, punch +1, air +1
  TEST IEM    raw preset, three biquads (a 4 dB low shelf at 80 Hz, a -5 dB peak at 3.2 kHz Q 2.0, a +3 dB high shelf at 9 kHz) with a preamp of -4 dB (attenuate only)
`selftest` checks the file parses back (tau_assets.parse and halcyon_assets.parse_presets)."""
import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools")); sys.path.insert(0, str(ROOT / "tools" / "lab"))
import tau_assets as ta
import halcyon_assets as ha
import halcyon_engine_model as em

PRESETS = [
    {"name": "CLARITY", "controls": {"warmth": -2, "vocal": 2, "punch": 1, "air": 1}},
    {"name": "TEST IEM", "preamp": 10 ** (-4 / 20), "stages": [tuple(em.rbj("low", 80, 0.7, 4.0)), tuple(em.rbj("peak", 3200, 2.0, -5.0)), tuple(em.rbj("high", 9000, 0.7, 3.0))]},
]

def build(src=None):
    secs = []
    if src:
        blob = Path(src).read_bytes()
        parsed = ta.parse(blob)["sections"]
        secs = [(t if isinstance(t, bytes) else t.encode(), d) for t, d in parsed.items() if t not in ("PRST", b"PRST")]
    secs.append((b"PRST", ha.pack_presets(PRESETS)))
    return ta.pack_container(secs)

def selftest():
    for src in (None, str(ROOT / "work/diagnostics/tau-0_6_0_a_6/tau-assets.bin")):
        if src and not Path(src).exists(): continue
        out = ta.parse(build(src))
        names = [p["name"] for p in out["presets"]]
        assert names == ["CLARITY", "TEST IEM"], names
        if src: assert "THEM" in out["sections"] and "METR" in out["sections"], list(out["sections"])
    print("make_halcyon_test_assets selftest OK")

if __name__ == "__main__":
    a = sys.argv[1:]
    if a == ["selftest"]: selftest()
    elif len(a) in (1, 2):
        Path(a[-1]).write_bytes(build(a[0] if len(a) == 2 else None)); print("wrote", a[-1])
    else: sys.exit(__doc__)
