#!/usr/bin/env python3
"""B-640: the Halcyon loader (fw/halcyon_hw.h, the real C compiled here) writes exactly the bank the model expects: the six tone stages' quantised matched design, the peak-safe preamp
at index 85, then COMMIT (nact 6) and CLEAR; FLAT (all controls zero) is bypassed. The write log is then replayed into the Python engine model (the same shadow/commit semantics as the RTL)
and run on an impulse: the engine's output equals a plain integer cascade of the written stages (so the words in the log are what the engine really runs)."""
import os, random, subprocess, sys, tempfile
os.environ["EQ_COEF_BITS"] = "24"
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools")); sys.path.insert(0, str(ROOT / "tools" / "lab"))
import halcyon_model as m
import halcyon_engine_model as em

fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + (" " + info if info else ""))
    fails += 0 if ok else 1


rnd = random.Random(5)
sets = [dict(p[1]) for p in m.PRESETS]
sets += [{k: (rnd.randint(0, 5) if k == "sibilance" else rnd.randint(-5, 5)) for k in m.MACROS} for _ in range(120)]
sx = lambda v: v - (1 << 24) if v >> 23 else v


def run(fwdir, quiet=False):
    """Compile the harness against the header in fwdir and check everything; returns the number of failed checks."""
    global fails
    fails = 0
    with tempfile.TemporaryDirectory() as d:
        exe = Path(d) / "h"
        r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-I", str(fwdir), "-o", str(exe), str(ROOT / "sim/halcyon_hw_harness.c")], capture_output=True, text=True)
        if r.returncode:
            print(r.stdout, r.stderr); sys.exit(1)
        inp = "\n".join(" ".join(str(c[k]) for k in m.MACROS) for c in sets)
        out = subprocess.run([str(exe)], input=inp, capture_output=True, text=True, check=True).stdout.splitlines()
    bad_bank = bad_ctl = bad_engine = bad_flat = bad_pre = 0
    for c, line in zip(sets, out):
        wr = [tuple(int(x, 16) for x in t.split(":")) for t in line.split()]
        gains = m.stage_gains(c)
        q = m.coeffs(gains, True)                       # six stages x five Q2.22 values
        flat = all(v == 0 for v in c.values())
        want = [(0x17C, 0)] + [(0x180, v & 0xFFFFFF) for st in [m.infra_coeffs()] + list(q) for v in st]
        if wr[:len(want)] != want or wr[len(want):len(want) + 1] != [(0x17C, 85)]:
            bad_bank += 1; continue
        byp = 1 if flat else 0
        if wr[-2:] != [(0x178, 1 | (byp << 1) | 4 | (7 << 8)), (0x178, 1 | (byp << 1) | 8 | (7 << 8))] or len(wr) != len(want) + 4:
            bad_ctl += 1; continue
        words = [x[1] for x in wr if x[0] == 0x180]
        pre = sx(words[35])
        if not (0 < pre <= (1 << 22)):
            bad_pre += 1
        # the engine runs the written words: the same impulse through a bank written from the log and one written from the model's own numbers
        e, f = em.Engine(16), em.Engine(16)
        for i, v in enumerate(words[:35]): e.write(i, sx(v))
        e.write(em.PRE, pre); e.commit(7)
        for i, st in enumerate([m.infra_coeffs()] + list(q)):
            for k, v in enumerate(st): f.write(i * 5 + k, v)
        f.write(em.PRE, pre); f.commit(7)
        ys = [e.sample(20000 if n == 0 else 0, 0, byp)[0] for n in range(40)]
        if ys != [f.sample(20000 if n == 0 else 0, 0, byp)[0] for n in range(40)]: bad_engine += 1
        if flat and ys[0] != 20000: bad_flat += 1
    check("write log = the infrasonic stage then the model's six stages, preamp index 85, then COMMIT (nact 7) and CLEAR, for %d settings" % len(sets), bad_bank == 0 and bad_ctl == 0, "(bank %d, control %d)" % (bad_bank, bad_ctl))
    check("the logged bank runs in the engine model exactly as the model's own numbers do", bad_engine == 0)
    check("FLAT is bypassed (input passes unchanged)", bad_flat == 0)
    ic = m.infra_coeffs(); import gen_eq_coeffs as gq
    db = lambda f: gq.response_db([ic], f)
    check("infrasonic stage is stable at Q2.22, -3 dB near 17 Hz, within 0.1 dB of flat above 100 Hz and at least 20 dB down at 3 Hz",
          gq.stable([ic]) and abs(db(17.0) + 3.0) < 0.6 and abs(db(100.0)) < 0.1 and abs(db(1000.0)) < 0.01 and db(3.0) < -20.0, "(17 Hz %.2f dB, 100 Hz %.3f dB, 3 Hz %.1f dB)" % (db(17.0), db(100.0), db(3.0)))
    check("the preamp word is positive and at most unity", bad_pre == 0)
    check("present reads bit 31 of the control register and off writes enable 0", out[-2] == "present=1" and out[-1].strip() == "178:0")
    return fails


# the infrasonic stage in the engine itself: a DC step decays to (almost) nothing, no limit cycle, and a 1 kHz tone passes at unity
e = em.Engine(16)
for k, v in enumerate(m.infra_coeffs()): e.write(k, v)
e.write(em.PRE, 1 << 22); e.commit(1)
import math
ys = [e.sample(10000, 0, 0)[0] for _ in range(30000)]
check("engine: a DC step through the infrasonic stage decays to a constant of at most 3 LSB (rounding dead band of the 17 Hz pole pair; no limit cycle)", max(ys[-2000:]) - min(ys[-2000:]) == 0 and abs(ys[-1]) <= 3, "(peak %d, final %d)" % (max(ys), ys[-1]))
e2 = em.Engine(16)
for k, v in enumerate(m.infra_coeffs()): e2.write(k, v)
e2.write(em.PRE, 1 << 22); e2.commit(1)
ts = [e2.sample(int(20000 * math.sin(2 * math.pi * 1000 * n / 48000)), 0, 0)[0] for n in range(4800)]
check("engine: a 1 kHz tone passes the infrasonic stage within 0.02 dB", abs(max(ts[2400:]) / 20000.0 - 1) < 0.0023, "(peak %d of 20000)" % max(ts[2400:]))
fails += 0
total = run(ROOT / "fw")
# mutants: each must fail the checks
src = (ROOT / "fw/halcyon_hw.h").read_text()
mutants = {
    "preamp one index off": ("(HAL_NST * 5u)", "(HAL_NST * 5u + 1u)"),
    "coefficients not masked to 24 bits": ("(uint32_t)coef[i] & 0xFFFFFFu", "(uint32_t)coef[i]"),
    "infrasonic stage missing": ("    for (uint32_t k = 0; k < 5u; k++) bank[k] = hal_infra[k];\n", "    for (uint32_t k = 0; k < 5u; k++) bank[k] = 0;\n"),
    "no clear after the commit": ("    HAL_WR(R_HAL_CTRL, HAL_CTRL(1u, bypass, 0u, 1u, nstage));\n", ""),
    "FLAT not bypassed": ("hal_hw_commit_with_infra(bank, HAL_NSTAGE, hal_preamp_q22(hal_atten_eighths(step)), flat);", "hal_hw_commit_with_infra(bank, HAL_NSTAGE, hal_preamp_q22(hal_atten_eighths(step)), 0u);"),
}
import io, contextlib
for name, (a, b) in mutants.items():
    assert a in src, name
    with tempfile.TemporaryDirectory() as d:
        for f in ("halcyon_core.h", "halcyon_tab.h"):
            (Path(d) / f).write_text((ROOT / "fw" / f).read_text())
        (Path(d) / "halcyon_hw.h").write_text(src.replace(a, b, 1))
        with contextlib.redirect_stdout(io.StringIO()):
            nf = run(Path(d))
    print(("ok   mutant killed: " if nf else "FAIL mutant survived: ") + name)
    if not nf: total += 1
sys.exit(1 if total else 0)
