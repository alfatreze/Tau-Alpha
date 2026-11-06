#!/usr/bin/env python3
"""Layered Wave's SCROLL view (docs/features/LAYERED_WAVE_PERF_PLAN.md, P3; B-952): the picture scrolls by the engine's copy and only the new columns are drawn.

The pack (built as shipped, run under tools/rv32sim.py through the loader) runs eight presets' parameters in the SCROLL view (view = 2) over the shared input trace (368 x 122 box, 26 ms a picture, a forced repaint at
frame 0), next to the same presets in the HISTORY view, and checks:
  1. a normal picture issues exactly one scroll command (the box moved left by the pixels the speed asks for) and no more than 3 + layers x (columns + 1) commands in all; a picture with nothing owed issues none;
  2. a forced repaint (frame 0) issues no scroll command: the whole box is redrawn from the history ring;
  3. nothing is drawn outside the box;
  4. the average picture costs at most a third of the HISTORY view's commands for the same preset (the point of the view);
  5. control: the pack with the scroll command removed fails (1).
Host-simulated only: it counts commands; it does not check pixels, and the engine's own copy is not simulated."""
import os, subprocess, sys, tempfile
from concurrent.futures import ProcessPoolExecutor
import multiprocessing
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/player2")); sys.path.insert(0, str(ROOT / "tools"))
import pack_meter as pm
import json
SIM_ORG, SIM_SCRATCH = 0x00400000, 0x00300000
SIMREGS = ["-include", str(ROOT / "sim/p2_pack_sim_regs.h")]
NF = 60
fails = 0
def check(name, cond, info=""):
    global fails
    print(("ok   " if cond else "FAIL ") + name + ("  " + info if info and not cond else ""))
    fails += 0 if cond else 1
_src = (ROOT / "tools/rv32sim.py").read_text()
import importlib.util
_ns = {"__name__": "rv32plain"}; exec(compile(_src, "rv32plain", "exec"), _ns)
_sp = importlib.util.spec_from_file_location("devmodel", ROOT / "tools/p2_dev_model.py")
def run(elf, blob):
    m = _ns["Machine"](); m.dev = importlib.util.module_from_spec(_sp); _sp.loader.exec_module(m.dev)
    m.load_elf(str(elf)); m.blob = blob
    import contextlib, io
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf): m.run()
    return [tuple(int(x) for x in ln.split()[1:]) for ln in buf.getvalue().splitlines() if ln.startswith("F ")]
def harness(tmp, preset, view):
    elf = tmp / ("h_%d_%d.elf" % (preset, view))
    cmd = [pm.tool("gcc"), "-march=rv32im", "-mabi=ilp32", "-mno-relax", "-O2", "-ffreestanding", "-nostdlib", "-nostartfiles", "-w"] + SIMREGS + [
           "-DPACK_LW", "-DTR_NOHASH", "-DLW_TRACE", "-DLW_P=%d" % preset, "-DLW_FS=0", "-DNF=%d" % NF, "-DLW_VIEWOVR=%d" % view, "-DPACK_ORG=0x%X" % SIM_ORG, "-DSCRATCH_ORG=0x%X" % SIM_SCRATCH,
           "-DPACK_METER=%d" % pm.METER_IDS["layered_wave"], "-DPACK_SLOT=%d" % pm.SLOT["layered_wave"], "-Wl,--no-warn-rwx-segments", "-T", str(ROOT / "tools/host/link.ld"),
           str(ROOT / "tools/host/start.S"), str(ROOT / "tools/host/p2_lw_cost_harness.c"), "-I", str(ROOT / "fw2/ui"), "-I", str(ROOT / "fw2/cores"), "-I", str(ROOT / "fw"), "-I", str(ROOT / "tools/host"), "-o", str(elf), "-lgcc"]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return elf
def _job(a): return run(*a)
def sweep(tmp, blob, presets, view):
    elfs = [harness(tmp, p, view) for p in presets]
    with ProcessPoolExecutor(max_workers=os.cpu_count() or 4, mp_context=multiprocessing.get_context("fork")) as ex:
        return list(ex.map(_job, [(e, blob) for e in elfs]))
def mutant_blob(tmp):
    T = tmp / "mut"; (T / "fw2/ui").mkdir(parents=True)
    for e in (ROOT / "fw2").iterdir():
        if e.name != "ui": os.symlink(e, T / "fw2" / e.name)
    for e in (ROOT / "fw2/ui").iterdir(): os.symlink(e, T / "fw2/ui" / e.name)
    for n in ("fw", "meters", "toolchain"): os.symlink(ROOT / n, T / n)
    inc = (ROOT / "fw2/ui/layered_wave.inc").read_text(); old = "lw_scroll_left((uint32_t)X, (uint32_t)Y, (uint32_t)W, (uint32_t)H, (uint32_t)d);"
    assert old in inc; (T / "fw2/ui/layered_wave.inc").unlink(); (T / "fw2/ui/layered_wave.inc").write_text(inc.replace(old, "(void)d;"))
    real = pm.ROOT; pm.ROOT = T
    try: blob, _ = pm.build("layered_wave", SIM_ORG, tmp / "mut.elf", SIMREGS, scratch=SIM_SCRATCH)
    finally: pm.ROOT = real
    return blob
def verdict(rows, preset_vals):
    """(ok_scroll_frames, ok_repaint_frames, ok_inside, bad detail)"""
    bad = []
    for pi, (frames, pv) in enumerate(zip(rows, preset_vals)):
        layers, speed = pv["layers"], pv["speed"]
        dmax = (26 * speed + 999) // 1000 + 1
        for n, cmds, cyc, scr, viol in frames:
            if n in (0,):
                if scr != 0: bad.append((pi, n, "repaint scrolled"))
            else:
                if scr > 1: bad.append((pi, n, "more than one scroll"))
                if scr == 0 and cmds != 0: bad.append((pi, n, "commands without a scroll"))
                if scr == 1 and cmds > 3 + layers * (dmax + 1): bad.append((pi, n, "too many commands: %d" % cmds))
            if viol: bad.append((pi, n, "drawn outside the box"))
        if sum(f[3] for f in frames) < NF // 2: bad.append((pi, -1, "hardly any scroll commands"))
    return bad
def main():
    d = json.loads((ROOT / "meters/layered_wave/meter.json").read_text())
    PRESETS = [0, 1, 2, 3, 4, 5, 8, 12]
    pv = [d["presets"][p]["values"] for p in PRESETS]
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        blob, info = pm.build("layered_wave", SIM_ORG, tmp / "lw.elf", SIMREGS, scratch=SIM_SCRATCH)
        rs, rh = sweep(tmp, blob, PRESETS, 2), sweep(tmp, blob, PRESETS, 0)
        for i, p in enumerate(PRESETS):
            ms = sum(f[1] for f in rs[i] if f[0] not in (0,)) / (NF - 1); mh = sum(f[1] for f in rh[i] if f[0] not in (0,)) / (NF - 1)
            print("     preset %2d (%s, %d layers, %d px/s): SCROLL %5.1f commands a picture, HISTORY %6.1f" % (p, d["presets"][p]["name"], pv[i]["layers"], pv[i]["speed"], ms, mh))
        bad = verdict(rs, pv)
        check("every normal picture is one scroll plus a few columns, a forced repaint scrolls nothing, nothing is drawn outside the box", not bad, str(bad[:4]))
        check("the average SCROLL picture costs at most a third of the HISTORY view's commands for the same preset", all(sum(f[1] for f in rs[i] if f[0] not in (0,)) * 3 <= sum(f[1] for f in rh[i] if f[0] not in (0,)) for i in range(len(PRESETS))))
        mres = sweep(tmp, mutant_blob(tmp), PRESETS, 2)
        check("control: a pack that never issues the scroll command is caught", bool(verdict(mres, pv)))
    print("FAILED" if fails else "PASSED"); sys.exit(1 if fails else 0)
if __name__ == "__main__": main()
