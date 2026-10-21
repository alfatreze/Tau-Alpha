#!/usr/bin/env python3
"""Golden frames for Player 2: the JS preview module of Winamp Bars and the REAL fw2/ui/meter_winamp_bars.c must draw the same picture (compared pixel by pixel after every frame) for the same input trace (every preset, 90 frames, Dark and Light,
paused frames included). Winamp Scope too: the JS twin and fw2/ui/meter_winamp_scope.c, fed the same 256-column captures through a model of the capture block, issue the same rectangles command for command (the wipe
path: the blended trail is one lab command on the JS side and four strip blits on the Pocket, and is checked against the engine model by test_p2_scope_trail.py). Chladni has no twin (plan M1b). Mutants: the bar
height rounding; the scope's smoothing arithmetic. Each must fail."""
import json, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
def build(td, mutant):
    d = Path(td)
    for sub in ("ui", "gfx", "generated", "cores", "sim"): (d / sub).mkdir()
    src = (ROOT / "fw2/ui/meter_winamp_bars.c").read_text()
    src = src.replace('#include "../cores/meter_core.h"', f'#include "{ROOT}/fw2/cores/meter_core.h"')
    if mutant:
        a = "uint32_t bh = (disp[b] * h) / 255u;"; assert a in src
        src = src.replace(a, "uint32_t bh = (disp[b] * h) / 256u;")
    (d / "ui/meter_winamp_bars.c").write_text(src)
    for f in ("ui/meter.h", "ui/theme.h", "gfx/gfx.h", "generated/meter_presets.h"): (d / f).write_text((ROOT / "fw2" / f).read_text())
    (d / "sim/h.c").write_text((ROOT / "sim/p2_bars_golden_harness.c").read_text().replace('"../fw2/ui/meter_winamp_bars.c"', '"../ui/meter_winamp_bars.c"'))
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-o", str(d / "h"), str(d / "sim/h.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return d / "h"
def run(exe, scen, td):
    lines = [str(len(scen))]
    for s in scen:
        c, b = s["colors"], s["box"]
        lines.append("%d %d %d %d %d %d %d %d %d %d" % (len(s["params"]), c["accent"], c["prim"], c["track"], c["bg"], b["x"], b["y"], b["w"], b["h"], len(s["frames"])))
        lines.append(" ".join(str(v) for v in s["params"]))
        for fr in s["frames"]: lines.append(str(fr["paused"]) + " " + " ".join(str(v) for v in fr["spec"]) + " " + " ".join(str(v) for v in fr["wave"]))
    (Path(td) / "in.txt").write_text("\n".join(lines) + "\n")
    out = subprocess.run([str(exe), str(Path(td) / "in.txt")], capture_output=True, text=True, check=True).stdout.splitlines()
    got, cur = [], None
    for ln in out:
        if ln.startswith("F "): cur = [] if cur is None else cur; frame = []; cur.append(frame)
        elif ln == "S": got.append(cur); cur = None
        else: frame.append(ln)
    return got
W, H = 400, 360
def paint(fb, ln):
    t = ln.split(); op, v = t[0], [int(x) for x in t[1:]]
    if op == "rect": x, y, w, h, c = v; rows = [(y, h, c)]
    elif op == "bar": x, y, w, h, lit, lc, uc = v; lit = min(lit, h); rows = [(y, h - lit, uc), (y + h - lit, lit, lc)]
    else: raise SystemExit("unknown command " + ln)
    for (ry, rh, c) in rows:
        for j in range(ry, min(ry + rh, H)):
            fb[j * W + x:j * W + min(x + w, W)] = [c] * (min(x + w, W) - x)
def mismatches(scen, got, quiet=False):
    """The JS module may use the hardware bar opcode where fw2 issues two rectangles: the pictures must be equal after every frame."""
    n = 0
    for s, cf in zip(scen, got):
        a, b = [0] * (W * H), [0] * (W * H)
        for i, (jl, cl) in enumerate(zip(s["log"], cf)):
            for ln in jl: paint(a, ln)
            for ln in cl: paint(b, ln)
            if a != b: n += 1; (not quiet) and print(f"MISMATCH {s['name']} frame {i}: {sum(1 for p, q in zip(a, b) if p != q)} pixels differ"); break
    return n
extra = ["--trace", sys.argv[sys.argv.index("--trace") + 1]] if "--trace" in sys.argv else []
node = subprocess.run(["node", str(ROOT / "tools/meters/preview/golden.js")] + extra, capture_output=True, text=True, cwd=ROOT)
if node.returncode: print("golden.js failed:\n" + node.stderr); sys.exit(1)
scen = [s for s in json.loads(node.stdout) if s["key"] == "winamp_bars"]
with tempfile.TemporaryDirectory() as td:
    bad = mismatches(scen, run(build(td, False), scen, td))
with tempfile.TemporaryDirectory() as td:
    caught = mismatches(scen, run(build(td, True), scen, td), True) > 0
total = sum(len(x) for s in scen for x in s["log"])
print(f"ok   {len(scen)} Winamp Bars scenarios, {total} commands paint identical pictures in fw2 and the JS module" if not bad else f"FAIL {bad} scenarios differ")
print("ok   mutant (bar height rounding) is caught" if caught else "FAIL mutant not caught")

# ---- Winamp Scope: command-for-command against the same captures
def build_scope(td, mutant):
    d = Path(td)
    for sub in ("ui", "gfx", "generated", "platform", "sim"): (d / sub).mkdir()
    src = (ROOT / "fw2/ui/meter_winamp_scope.c").read_text()
    if mutant:
        a = "(((mid - sc_mid[c]) * (100 - (int32_t)smooth)) / 100)"; assert a in src
        src = src.replace(a, "(((mid - sc_mid[c]) * (100 - (int32_t)smooth)) / 101)")
    (d / "ui/meter_winamp_scope.c").write_text(src)
    for f in ("ui/meter.h", "ui/theme.h", "gfx/gfx.h", "platform/mmio.h", "generated/meter_presets.h"): (d / f).write_text((ROOT / "fw2" / f).read_text())
    (d / "sim/h.c").write_text((ROOT / "sim/p2_scope_golden_harness.c").read_text().replace('"../fw2/ui/meter_winamp_scope.c"', '"../ui/meter_winamp_scope.c"'))
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-DP2_HOST", "-include", str(ROOT / "sim/p2_host_regs.h"), "-o", str(d / "h"), str(d / "sim/h.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return d / "h"
def run_scope(exe, scen, td):
    lines = [str(len(scen))]
    for s in scen:
        c, b = s["colors"], s["box"]
        lines.append("%d %d %d %d %d %d %d %d %d %d" % (len(s["params"]), c["accent"], c["prim"], c["track"], c["bg"], b["x"], b["y"], b["w"], b["h"], len(s["frames"])))
        lines.append(" ".join(str(v) for v in s["params"]))
        for fr in s["frames"]: lines.append(str(fr["paused"]) + " " + " ".join("%d %d" % (a, b) for a, b in fr["capture"]))
    (Path(td) / "sin.txt").write_text("\n".join(lines) + "\n")
    out = subprocess.run([str(exe), str(Path(td) / "sin.txt")], capture_output=True, text=True, check=True).stdout.splitlines()
    got, cur = [], None
    for ln in out:
        if ln.startswith("F "): cur = [] if cur is None else cur; frame = []; cur.append(frame)
        elif ln == "S": got.append(cur); cur = None
        else: frame.append(ln)
    return got
def scope_diffs(scen, got, quiet=False):
    n = 0
    for s, cf in zip(scen, got):
        for i, (jl, cl) in enumerate(zip(s["log"], cf)):
            if jl != cl: n += 1; (not quiet) and print(f"MISMATCH {s['name']} frame {i}: js {jl[:3]} vs c {cl[:3]}"); break
    return n
sscen = [s for s in json.loads(node.stdout) if s["key"] == "winamp_scope"]
with tempfile.TemporaryDirectory() as td:
    sbad = scope_diffs(sscen, run_scope(build_scope(td, False), sscen, td))
with tempfile.TemporaryDirectory() as td:
    scaught = scope_diffs(sscen, run_scope(build_scope(td, True), sscen, td), True) > 0
stotal = sum(len(x) for s in sscen for x in s["log"])
print(f"ok   {len(sscen)} Winamp Scope scenarios, {stotal} commands identical between fw2 and the JS module" if not sbad else f"FAIL {sbad} scope scenarios differ")
print("ok   mutant (scope smoothing arithmetic) is caught" if scaught else "FAIL scope mutant not caught")
good = not bad and caught and not sbad and scaught
print("PASSED" if good else "FAILED"); sys.exit(0 if good else 1)
