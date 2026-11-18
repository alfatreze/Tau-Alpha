#!/usr/bin/env python3
"""YOUR meter's own behaviour tests, on top of the conformance suite (`make test` runs both). The checks below are generic examples: add checks for what YOUR meter must do and update this comment.
It shows how to drive the SDK's host runner from Python (docs/TESTING.md, "Driving the runner"): one Runner per run, one .tick() per picture, and each tick returns the host-table calls the meter made.
Generic checks kept here (true of most meters): the meter reacts to loud music; a forced repaint gives the same picture whatever came before.
Usage: python3 test/test_meter.py [path to the SDK]"""
import glob, sys, tempfile, zipfile
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
    box, bg, QUIET, LOUD = suite.BOXES["main"], suite.BG, bytes(16), bytes([255] * 16)
    # a tick(frame, dt_ms, box, bg, force, paused, peak_l, peak_r, degrade, spec) returns .status ("ok", "cut", "fault"), .calls (tuples of strings: ('rect', x, y, w, h, colour), ('bar', ...)), .cmds, .cycles
    with runner.Runner(str(binf), params=params, limit=d["max_cycles"], roles=suite.ROLES_DARK) as r:
        quiet = r.tick(1, 26, box, bg, 1, 1, 0, 0, 0, QUIET)
        loud = r.tick(2, 26, box, bg, 0, 0, 24575, 24575, 0, LOUD)
        again = r.tick(3, 26, box, bg, 1, 1, 0, 0, 0, QUIET)
    ok = True
    def check(name, cond):
        global ok
        print(("ok   " if cond else "FAIL ") + name); ok = ok and cond
    check("every picture completes", all(t.status == "ok" for t in (quiet, loud, again)))
    check("loud music draws something different from silence", loud.calls != quiet.calls)
    check("a forced repaint is the same picture whatever came before", quiet.calls == again.calls)
    sys.exit(0 if ok else 1)
