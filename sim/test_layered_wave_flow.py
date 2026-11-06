#!/usr/bin/env python3
"""Layered Wave's Direction in HISTORY and in SCROLL (B-965), end to end: the firmware's drawing source as WebAssembly draws into a framebuffer that implements the engine's rect, scroll-left and blit; the top edge of the picture
is measured (tools/meters/preview/wasm/flow_lw.js). The rule: the HEAD (pointed start, Head length) is where the newest sound enters and the TAIL (Tail length, the dots) where it leaves, so Direction RIGHT = the newest enters
at the left, the picture flows right (positive shift), head on the left; LEFT = the exact mirror, in both views.
  1. the picture moves to the right with RIGHT and to the left with LEFT, in HISTORY and in SCROLL (this is the check that was missing when DEV 212 shipped a SCROLL view that always flowed left);
  2. with a long head (40 %) and a short tail (30 %) the picture is lower at the head end than at the tail end in HISTORY, and the reverse for the other Direction; in SCROLL (no head ramp) it is lower at the tail end.
Needs node. Host-simulated only (the engine's copy is modelled by a plain copy)."""
import json, subprocess, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(info)))
    fails += 0 if ok else 1
if subprocess.run(["node", "--version"], capture_output=True).returncode:
    print("node not found: skipped"); sys.exit(0)
r = subprocess.run(["node", str(ROOT / "tools/meters/preview/wasm/flow_lw.js")], capture_output=True, text=True)
if r.returncode: print(r.stderr); sys.exit(1)
o = json.loads(r.stdout); print("    ", o)
check("HISTORY flows right with RIGHT and left with LEFT", o["history0"]["shift"] > 0 and o["history1"]["shift"] < 0, (o["history0"]["shift"], o["history1"]["shift"]))
check("SCROLL flows right with RIGHT and left with LEFT (it used to flow left whatever the setting)", o["scroll0"]["shift"] > 0 and o["scroll1"]["shift"] < 0, (o["scroll0"]["shift"], o["scroll1"]["shift"]))
check("HISTORY: head and tail swap sides with the Direction (lower at the head end than at the tail end, mirrored)", o["history0"]["h10"] < o["history0"]["h90"] and o["history1"]["h10"] > o["history1"]["h90"], (o["history0"], o["history1"]))
check("SCROLL: the tail end is the lower one and it swaps sides with the Direction", o["scroll0"]["h90"] < o["scroll0"]["h10"] and o["scroll1"]["h10"] < o["scroll1"]["h90"], (o["scroll0"], o["scroll1"]))
print("FAILED" if fails else "PASSED"); sys.exit(1 if fails else 0)
