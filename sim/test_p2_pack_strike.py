#!/usr/bin/env python3
"""Pack cycle limits (fw2/cores/pack_strike_core.h used by fw2/ui/meters.c, manifest p2.max_cycles; decision D-M20): a drawn tick of a pack over its limit is an overrun; the third strikes the pack out for the session
(PACK ERR 40) and the built-in code draws from the next picture; a forced repaint is not judged; a tick exactly at the limit is fine; one strike is forgiven for every 64 clean ticks, so a single stall cannot disable a
good pack over a long session. Mutants: exactly at the limit counts; a fourth strike is needed; no forgiveness; forced repaints are judged."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/player2"))
import pack_meter as pm, pack_bundle as pb
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("  " + str(info) if info and not ok else ""))
    fails += 0 if ok else 1
def build(d, mutant=None):
    d.mkdir(parents=True)
    src = (ROOT / "fw2/ui/meters.c").read_text(); flags = []
    if mutant == "forced": a = "        if (!in.force) {                                                              /* a forced repaint"; assert a in src; src = src.replace(a, "        if (1) {                                                                      /* a forced repaint")
    if mutant == "atlimit": flags = ["-DBUG=1"]
    if mutant == "fourth": flags = ["-DBUG=2"]
    if mutant == "noforgive": flags = ["-DBUG=3"]
    for sub in ("ui", "gfx", "platform", "cores", "generated"): (d / "fw2" / sub).mkdir(parents=True, exist_ok=True)
    (d / "sim").mkdir(exist_ok=True)
    (d / "fw2/ui/meters.c").write_text(src)
    for f in ("ui/meter.h", "ui/theme.h", "ui/meter_pack.h", "gfx/gfx.h", "platform/mmio.h", "platform/platform.h", "cores/spectrum_core.h", "cores/metr_core.h", "cores/tune_core.h", "cores/meter_budget_core.h", "cores/pack_strike_core.h", "cores/meter_desc_core.h", "cores/helios_core.h", "cores/gfx_touch_core.h", "cores/meter_pack_core.h", "cores/library_core.h", "generated/meter_presets.h", "generated/lw_presets.h", "generated/meter_registry.h"):
        (d / "fw2" / f).write_text((ROOT / "fw2" / f).read_text())
    (d / "fw2/gfx/gfx.h").write_text((ROOT / "fw2/gfx/gfx.h").read_text())
    (d / "sim/h.c").write_text((ROOT / "sim/p2_pack_strike_harness.c").read_text())
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-DP2_HOST"] + flags + ["-include", str(ROOT / "sim/p2_host_regs.h"), "-I", str(ROOT / "fw"), "-o", str(d / "h"), str(d / "sim/h.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return d / "h"
with tempfile.TemporaryDirectory() as td:
    tmp = Path(td)
    blob, _ = pm.build("vu_master", pm.SLOT_BASE + pm.SLOT["vu_master"] * pm.SLOT_SIZE, tmp / "vu.elf")
    bundle = tmp / "b.bin"; bundle.write_bytes(pb.bundle([blob]))
    def run(exe, costs): return subprocess.run([str(exe), str(bundle)] + [str(c) for c in costs], capture_output=True, text=True).stdout.strip().splitlines()
    exe = build(tmp / "good")
    lim = int(run(exe, [])[0].split()[1]); check("the Master VU limit comes from its manifest", lim == 200000, lim)
    o = run(exe, ["F0", lim + 1, lim + 1, lim + 1, 0, 0])[1:]
    check("a forced first tick runs the pack and is not judged; two overruns keep the pack running", o[0] == "pack state 0 struck 0 overruns 0" and o[1] == "pack state 0 struck 0 overruns 1" and o[2] == "pack state 0 struck 0 overruns 2", o)
    check("the third overrun strikes the pack out (state 40) and from the next picture the built-in draws", o[3] == "pack state 40 struck 1 overruns 3" and o[4] == "built-in state 40 struck 1 overruns 3" and o[5] == "built-in state 40 struck 1 overruns 3", o)
    o = run(exe, ["F0", lim, lim, lim, lim])[1:]
    check("a tick exactly at the limit is not an overrun", all(l == "pack state 0 struck 0 overruns 0" for l in o), o)
    o = run(exe, ["F0"] + ["F%d" % (lim * 5)] * 5 + [lim + 1])[1:]
    check("a forced repaint is never judged, however slow", all(l.startswith("pack state 0 struck 0 overruns 0") for l in o[:6]) and o[6] == "pack state 0 struck 0 overruns 1", o)
    seq = ["F0", lim + 1, lim + 1] + [0] * 128 + [lim + 1, lim + 1]
    o = run(exe, seq)[1:]
    check("64 clean ticks forgive one strike: two strikes, 128 clean ticks (both forgiven), two more strikes do not strike the pack out", o[-1].startswith("pack state 0 struck 0") and o[-2].startswith("pack state 0 struck 0"), o[-3:])
    o = run(exe, ["F0", lim + 1, lim + 1] + [0] * 10 + [lim + 1])[1:]
    check("without that many clean ticks the third overrun still strikes the pack out", o[-1].startswith("pack state 40 struck 1"), o[-2:])
    for m, name in (("atlimit", "exactly at the limit counts"), ("fourth", "a fourth strike is needed"), ("noforgive", "strikes are never forgiven"), ("forced", "forced repaints are judged")):
        e2 = build(tmp / m, m)
        probe = {"atlimit": ["F0", lim, lim, lim], "fourth": ["F0", lim + 1, lim + 1, lim + 1], "noforgive": seq, "forced": ["F0", "F%d" % (lim * 5), "F%d" % (lim * 5), "F%d" % (lim * 5)]}[m]
        check("mutant (%s) is caught" % name, run(e2, probe) != run(exe, probe))
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
