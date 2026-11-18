#!/usr/bin/env python3
"""A test of THIS meter's own behaviour, on top of the conformance suite (`make test` runs both). It shows how to drive the SDK's host runner from Python:
  - silence must light no bar: with the music stopped (paused, all bands 0) every `bar` call has `lit` = 0;
  - loud music must light some bar;
  - the picture must not depend on the previous tick when the box is repainted (a forced picture equals a second forced picture).
Usage: python3 test/test_meter.py [path to the SDK]"""
import glob, json, os, sys, tempfile, zipfile
from pathlib import Path
here = Path(__file__).resolve().parent.parent
sdk = Path(sys.argv[1]) if len(sys.argv) > 1 and sys.argv[1] else here.parent / "tau-alpha-meter-sdk"
sys.path.insert(0, str(sdk))
from tau_meter import runner
from conformance import suite

pkgs = glob.glob(str(here / "dist" / "*.tmeter"))
if not pkgs: sys.exit("no package in dist/: run `make` first")
with tempfile.TemporaryDirectory() as td:
    binf = Path(td) / "meter.bin"; binf.write_bytes(zipfile.ZipFile(pkgs[0]).read("meter.bin"))
    d = suite.descriptor(binf.read_bytes()); params = [p.get("def", p["min"]) for p in d["params"]]
    box = suite.BOXES["main"]
    with runner.Runner(str(binf), params=params, limit=d["max_cycles"]) as r:
        quiet = r.tick(1, 26, box, 0x0841, 1, 1, 0, 0, 0, bytes(16))
        loud = r.tick(2, 26, box, 0x0841, 0, 0, 24575, 24575, 0, bytes([255] * 16))
        again = r.tick(3, 26, box, 0x0841, 1, 1, 0, 0, 0, bytes(16))
    lit = lambda t: [int(c[5]) for c in t.calls if c[0] == "bar"]
    ok = True
    def check(name, cond):
        global ok
        print(("ok   " if cond else "FAIL ") + name); ok = ok and cond
    check("silence lights no bar", quiet.status == "ok" and lit(quiet) and max(lit(quiet)) == 0)
    check("loud music lights bars", loud.status == "ok" and max(lit(loud) or [0]) > 0)
    check("a forced repaint is the same picture whatever came before", quiet.calls == again.calls)
    sys.exit(0 if ok else 1)
