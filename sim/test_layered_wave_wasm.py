#!/usr/bin/env python3
"""Layered Wave as WebAssembly (B-959): the firmware's own drawing source (fw2/ui/meter_layered_wave.c) compiled by tools/meters/preview/wasm/build_wasm.py and committed as
tools/meters/preview/meters/layered_wave_wasm.js must
  1. be current: none of the files it includes has changed since it was built (and, where the Homebrew llvm + lld toolchain is installed, a fresh build is byte-identical);
  2. issue exactly the engine commands that the JS twin issues, frame by frame, for every golden scenario (tools/meters/preview/golden_lw.js: every preset, the grids of views, draw modes, layers, splits, colours,
     the experimental group, setting changes mid-run, forced repaints, a moved box, fullscreen): so the lab and Tau Omega can run the module instead of a hand-written twin;
  3. control: a scenario whose parameter differs from the twin's is reported as a mismatch (the comparison does compare).
Needs node. Host-simulated only."""
import json, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(info)))
    fails += 0 if ok else 1
if subprocess.run(["node", "--version"], capture_output=True).returncode:
    print("node not found: skipped"); sys.exit(0)
r = subprocess.run([sys.executable, str(ROOT / "tools/meters/preview/wasm/build_wasm.py"), "--check", "layered_wave"], capture_output=True, text=True)
check("the committed module is current (%s)" % r.stdout.strip().splitlines()[-1][:120], r.returncode == 0, r.stdout + r.stderr)
with tempfile.TemporaryDirectory() as td:
    scen = subprocess.run(["node", str(ROOT / "tools/meters/preview/golden_lw.js")], capture_output=True, text=True, cwd=ROOT)
    if scen.returncode: print(scen.stderr); sys.exit(1)
    f = Path(td) / "scen.json"; f.write_text(scen.stdout)
    r = subprocess.run(["node", str(ROOT / "tools/meters/preview/wasm/parity_lw.js"), str(f)], capture_output=True, text=True)
    check("every golden scenario draws identically through the module and the JS twin (%s)" % r.stdout.strip().splitlines()[-1][:100], r.returncode == 0, r.stdout[-300:])
    sc = json.loads(scen.stdout); keys = [p["key"] for p in json.loads((ROOT / "tools/meters_schema.json").read_text())["meters"][0:0] or []]
    names = [m for m in json.loads((ROOT / "tools/meters_schema.json").read_text())["meters"] if m["key"] == "layered_wave"][0]["params"]
    ti = [p["key"] for p in names].index("taper")
    sc[0]["params"][ti] = 0 if sc[0]["params"][ti] else 60                       # the first scenario's taper differs from the one the twin ran
    g = Path(td) / "bad.json"; g.write_text(json.dumps(sc[:1]))
    r = subprocess.run(["node", str(ROOT / "tools/meters/preview/wasm/parity_lw.js"), str(g)], capture_output=True, text=True)
    check("control: a scenario with another taper is a mismatch", r.returncode != 0 and "MISMATCH" in r.stdout, r.stdout[-200:])
print("FAILED" if fails else "PASSED"); sys.exit(1 if fails else 0)
