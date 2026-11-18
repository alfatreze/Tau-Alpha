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
    if mutant == "noexit": a = "void configure_view_exit(void) { meters_cfg_end(); }"; assert a in src; src = src.replace(a, "void configure_view_exit(void) { }")
    if mutant == "noremember": a = "meters_cfg_remember(); return V_REPORT;"; assert a in src; src = src.replace(a, "return V_REPORT;")
    if mutant == "noreopen": a = "if (r != 0xFFFFFFFFu && r != meters_cur()) meters_cfg_begin(r);"; assert a in src; src = src.replace(a, "(void)r;")
    if mutant == "nolost": a = "static int lost(void) { return meters_cur() != meter; }"; assert a in src; src = src.replace(a, "static int lost(void) { return 0; }")
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
with tempfile.TemporaryDirectory() as td: out = build(td, None)
check("the page draws the expected rows after each key", out[:6] == GOOD)
EXTRA = ["exit-before 0", "exit-after 1", "export 4 remembers 1", "reopen begins 1 arg 5 cur 5", "reopen-same begins 1"]
check("W1 (D-M25): leaving the page asks the registry to bring the set meter back, exactly once; X (the Report) remembers the page's meter; coming back reopens it, unless it is on screen already", out[6:11] == EXTRA)
check("... and when the page's meter is struck out while the page is open (the meter on screen is another one) the list area says so, and any key goes back", len(out) > 13 and out[11].startswith("PRESET=") and out[12].startswith("THIS METER WAS STOPPED=") and out[13] == "lost-key -2")
for m in ("nodirty", "noreport", "noexit", "noremember", "noreopen", "nolost"):
    with tempfile.TemporaryDirectory() as td: check(f"mutant {m} is caught", build(td, m) != out)
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
