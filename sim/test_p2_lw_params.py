#!/usr/bin/env python3
"""Layered Wave parameters reach the picture in the pack as shipped (B-964): the real pack under tools/rv32sim.py, one parameter changed at a time on top of a preset, the commands hashed per frame.
  1. Direction changes the HISTORY picture in every frame (AURORA, history view); it changes nothing in the SPECTRUM view (TIDE: nothing scrolls);
  2. Head length, Tail length and Taper curve change the HISTORY picture when the Tail taper is above 0 (AURORA) and nothing when it is 0 (NEON);
  3. they change nothing in the SPECTRUM view (its window is symmetric; the length controls are HISTORY and SCROLL only).
These are properties of the design, written down so that "the setting does nothing" can be told from "the setting is not for this view". Host-simulated only."""
import contextlib, importlib.util, io, json, re, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/player2"))
import meter_manifest as mm; sys.path.insert(0, str(ROOT / "tools"))
import pack_meter as pm
SIMREGS = ["-include", str(ROOT / "sim/p2_pack_sim_regs.h")]
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(info)))
    fails += 0 if ok else 1
ns = {"__name__": "rv"}; exec(compile((ROOT / "tools/rv32sim.py").read_text(), "rv", "exec"), ns)
sp = importlib.util.spec_from_file_location("devmodel", ROOT / "tools/p2_dev_model.py")
man = mm.load(ROOT / "meters/layered_wave/meter.json")
enum = (ROOT / "fw2/generated/lw_presets.h").read_text()
names = re.search(r"enum \{ (MP_LAYERED_WAVE_VIEW.*?) \};", enum).group(1).split(", ")
IDX = {x.replace("MP_LAYERED_WAVE_", ""): i for i, x in enumerate(names)}
PRESET = {p["name"]: i for i, p in enumerate(man["presets"])}
n = [0]
def harness(tmp, preset, extra):
    n[0] += 1; elf = tmp / ("h%d.elf" % n[0])
    cmd = [pm.tool("gcc"), "-march=rv32im", "-mabi=ilp32", "-mno-relax", "-O2", "-ffreestanding", "-nostdlib", "-nostartfiles", "-w"] + SIMREGS + [
        "-DPACK_LW", "-DLW_TRACE", "-DLW_P=%d" % preset, "-DLW_FS=0", "-DNF=14", "-DPACK_ORG=0x400000", "-DSCRATCH_ORG=0x300000", "-DPACK_METER=%d" % pm.METER_IDS["layered_wave"], "-DPACK_SLOT=%d" % pm.SLOT["layered_wave"],
        "-Wl,--no-warn-rwx-segments", "-T", str(ROOT / "tools/host/link.ld"), str(ROOT / "tools/host/start.S"), str(ROOT / "tools/host/p2_lw_cost_harness.c"),
        "-I", str(ROOT / "fw2/ui"), "-I", str(ROOT / "fw2/cores"), "-I", str(ROOT / "fw"), "-I", str(ROOT / "tools/host"), "-o", str(elf), "-lgcc"] + extra
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode: print(r.stderr[:600]); sys.exit(1)
    return elf
def hashes(tmp, blob, preset, key=None, val=None):
    elf = harness(tmp, preset, [] if key is None else ["-DLW_SETIDX=%d" % IDX[key], "-DLW_SETVAL=%d" % val])
    m = ns["Machine"](); m.dev = importlib.util.module_from_spec(sp); sp.loader.exec_module(m.dev)
    m.load_elf(str(elf)); m.blob = blob
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf): m.run()
    return [l.split()[-1] for l in buf.getvalue().splitlines() if l.startswith("F ")]
def differing(tmp, blob, preset, key, val):
    a, b = hashes(tmp, blob, preset), hashes(tmp, blob, preset, key, val)
    return sum(1 for x, y in zip(a, b) if x != y), len(a)
with tempfile.TemporaryDirectory() as td:
    tmp = Path(td); blob, _ = pm.build("layered_wave", 0x400000, tmp / "lw.elf", SIMREGS, scratch=0x300000)
    A, N, T = PRESET["AURORA"], PRESET["NEON"], PRESET["TIDE"]
    check("AURORA is a history preset with the taper on, NEON has the taper off, TIDE is a spectrum preset", man["presets"][A]["values"]["view"] == 0 and man["presets"][A]["values"]["taper"] > 0 and man["presets"][N]["values"]["taper"] == 0 and man["presets"][T]["values"]["view"] == 1)
    d, t = differing(tmp, blob, A, "DIRECTION", 1); check("Direction LEFT changes the AURORA picture in every frame (%d of %d)" % (d, t), d == t)
    for key, val in (("TAPER_HEAD", 40), ("TAPER_TAIL", 30), ("TAPER_CURVE", 2)):
        d, t = differing(tmp, blob, A, key, val); check("%s = %d changes the AURORA picture (%d of %d frames)" % (key, val, d, t), d >= t - 1)
        d, t = differing(tmp, blob, N, key, val); check("%s = %d changes nothing with the taper off (NEON): %d of %d" % (key, val, d, t), d == 0)
    d, t = differing(tmp, blob, T, "DIRECTION", 1); check("Direction changes nothing in the SPECTRUM view (TIDE): %d of %d" % (d, t), d == 0)
    for key, val in (("TAPER_HEAD", 40), ("TAPER_TAIL", 30)):
        d, t = differing(tmp, blob, T, key, val); check("%s changes nothing in the SPECTRUM view (TIDE): %d of %d" % (key, d, t), d == 0)
print("FAILED" if fails else "PASSED"); sys.exit(1 if fails else 0)
