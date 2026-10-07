#!/usr/bin/env python3
"""B-644: fw/halcyon_curve.h (the real C) draws the response the model says: for the eight presets and 150 random control sets, the 40 curve values (the six selected table rows
added in dB, the infrasonic stage, minus the preamp's attenuation) match the exact response of the quantised cascade (six tone stages plus the infrasonic stage, Q2.22) minus the
preamp, within 0.07 dB (a sum of seven 1/64 dB roundings); and three mutants (preamp ignored, infrasonic stage ignored, one stage's row dropped) are killed."""
import os, random, subprocess, sys, tempfile
os.environ["EQ_COEF_BITS"] = "24"
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools")); sys.path.insert(0, str(ROOT / "tools" / "lab"))
import gen_eq_coeffs as g
import halcyon_model as m
import gen_halcyon_tab as tab

rnd = random.Random(9)
sets = [dict(p[1]) for p in m.PRESETS]
sets += [{k: (rnd.randint(0, 5) if k == "sibilance" else rnd.randint(-5, 5)) for k in m.MACROS} for _ in range(150)]
sets += [dict(m.ZERO, **{k: v}) for k in m.MACROS for v in ((5,) if k == "sibilance" else (-5, 5))]
tmp = tempfile.TemporaryDirectory()
inp = "\n".join(" ".join(str(c[k]) for k in m.MACROS) for c in sets)

def run(fwdir):
    exe = Path(tmp.name) / ("h" + str(abs(hash(str(fwdir)))))
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-I", str(fwdir), "-o", str(exe), str(ROOT / "sim/halcyon_curve_harness.c")], capture_output=True, text=True)
    if r.returncode:
        print(r.stdout, r.stderr); sys.exit(1)
    return subprocess.run([str(exe)], input=inp, capture_output=True, text=True, check=True).stdout.splitlines()

def worst(out):
    ic = m.infra_coeffs(); w = 0.0
    for c, line in zip(sets, out):
        v = list(map(int, line.split())); n, curve = v[0], v[1:]
        q = m.coeffs(m.stage_gains(c), True) + [ic]
        flat = all(v == 0 for v in c.values())
        for k, f in enumerate(tab.FREQS):
            ref = 0.0 if flat else g.response_db(q, f) - n / 8.0
            w = max(w, abs(curve[k] / 64.0 - ref))
    return w

fails = 0
w = worst(run(ROOT / "fw"))
ok = w <= 0.07
print(("ok   " if ok else "FAIL ") + "curve equals the exact quantised cascade response minus the preamp for %d settings (worst error %.3f dB)" % (len(sets), w)); fails += not ok
src = (ROOT / "fw/halcyon_curve.h").read_text()
for name, (a, b) in {"preamp ignored": ("int32_t sum = hal_infra_mag[k] - n * 8;", "int32_t sum = hal_infra_mag[k];"),
                     "infrasonic stage ignored": ("int32_t sum = hal_infra_mag[k] - n * 8;", "int32_t sum = -n * 8;"),
                     "one stage dropped": ("for (uint32_t s = 0; s < HAL_NSTAGE; s++)", "for (uint32_t s = 1; s < HAL_NSTAGE; s++)")}.items():
    assert a in src, name
    d = Path(tmp.name) / ("m" + name.replace(" ", "_")); d.mkdir()
    for f in ("halcyon_core.h", "halcyon_tab.h"): (d / f).write_text((ROOT / "fw" / f).read_text())
    (d / "halcyon_curve.h").write_text(src.replace(a, b, 1))
    killed = worst(run(d)) > 0.07
    print(("ok   mutant killed: " if killed else "FAIL mutant survived: ") + name); fails += not killed
sys.exit(1 if fails else 0)
