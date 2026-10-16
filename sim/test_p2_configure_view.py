#!/usr/bin/env python3
"""Configure page view (fw2/ui/configure_view.c): the rows it draws for a fake meter after entering and after key presses: PRESET / parameters / RESET, a number steps and clamps, a choice changes its label, switching PEAK off removes
the HOLD row that depends on it, the PRESET row shows CUSTOM after an edit and the next preset's name after Right on it (which also restores its values). Mutants: input does not mark the list for redraw; an edit is not reported to the meter."""
import shutil, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name)
    if not ok: fails += 1
def build(td, mutant):
    d = Path(td); shutil.copytree(ROOT / "fw2", d / "fw2"); (d / "sim").mkdir()
    p = d / "fw2/ui/configure_view.c"; src = p.read_text()
    if mutant == "nodirty": a = "    dirty = 1;\n    switch (e.kind)"; assert a in src; src = src.replace(a, "    switch (e.kind)")
    if mutant == "noreport": a = "case CFE_PARAM: meters_params_changed(meter); break;"; assert a in src; src = src.replace(a, "case CFE_PARAM: break;")
    p.write_text(src)
    (d / "sim/h.c").write_text((ROOT / "sim/p2_configure_view_harness.c").read_text())
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-DP2_HOST", "-include", str(ROOT / "sim/p2_host_regs.h"), "-o", str(d / "h"), str(d / "sim/h.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return subprocess.run([str(d / "h")], capture_output=True, text=True).stdout.strip().split("\n")
GOOD = ["PRESET=FLUID|BANDS=16|EASE=AA|PEAK=ON|HOLD=200|RESET TO PRESET=",
        "PRESET=CUSTOM|BANDS=16|EASE=AA|PEAK=ON|HOLD=200|RESET TO PRESET=",
        "PRESET=CUSTOM|BANDS=14|EASE=AA|PEAK=ON|HOLD=200|RESET TO PRESET=",
        "PRESET=CUSTOM|BANDS=14|EASE=BB|PEAK=ON|HOLD=200|RESET TO PRESET=",
        "PRESET=CUSTOM|BANDS=14|EASE=BB|PEAK=OFF|RESET TO PRESET=",
        "PRESET=CLASSIC|BANDS=16|EASE=AA|PEAK=ON|HOLD=200|RESET TO PRESET="]
with tempfile.TemporaryDirectory() as td: check("the page draws the expected rows after each key", build(td, None) == GOOD)
for m in ("nodirty", "noreport"):
    with tempfile.TemporaryDirectory() as td: check(f"mutant {m} is caught", build(td, m) != GOOD)
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
