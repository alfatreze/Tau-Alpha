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
        want = [(0x17C, 0)] + [(0x180, v & 0xFFFFFF) for st in q for v in st]
        if wr[:len(want)] != want or wr[len(want):len(want) + 1] != [(0x17C, 85)]:
            bad_bank += 1; continue
        byp = 1 if flat else 0
        if wr[-2:] != [(0x178, 1 | (byp << 1) | 4 | (6 << 8)), (0x178, 1 | (byp << 1) | 8 | (6 << 8))] or len(wr) != len(want) + 4:
            bad_ctl += 1; continue
        words = [x[1] for x in wr if x[0] == 0x180]
        pre = sx(words[30])
        if not (0 < pre <= (1 << 22)):
            bad_pre += 1
        # the engine runs the written words: the same impulse through a bank written from the log and one written from the model's own numbers
        e, f = em.Engine(16), em.Engine(16)
        for i, v in enumerate(words[:30]): e.write(i, sx(v))
        e.write(em.PRE, pre); e.commit(6)
        for i, st in enumerate(q):
            for k, v in enumerate(st): f.write(i * 5 + k, v)
        f.write(em.PRE, pre); f.commit(6)
        ys = [e.sample(20000 if n == 0 else 0, 0, byp)[0] for n in range(40)]
        if ys != [f.sample(20000 if n == 0 else 0, 0, byp)[0] for n in range(40)]: bad_engine += 1
        if flat and ys[0] != 20000: bad_flat += 1
    check("write log = the model's six stages, preamp index 85, then COMMIT (nact 6) and CLEAR, for %d settings" % len(sets), bad_bank == 0 and bad_ctl == 0, "(bank %d, control %d)" % (bad_bank, bad_ctl))
    check("the logged bank runs in the engine model exactly as the model's own numbers do", bad_engine == 0)
    check("FLAT is bypassed (input passes unchanged)", bad_flat == 0)
    check("the preamp word is positive and at most unity", bad_pre == 0)
    check("present reads bit 31 of the control register and off writes enable 0", out[-2] == "present=1" and out[-1].strip() == "178:0")
    return fails


total = run(ROOT / "fw")
# mutants: each must fail the checks
src = (ROOT / "fw/halcyon_hw.h").read_text()
mutants = {
    "preamp one index off": ("(HAL_NST * 5u)", "(HAL_NST * 5u + 1u)"),
    "coefficients not masked to 24 bits": ("(uint32_t)coef[i] & 0xFFFFFFu", "(uint32_t)coef[i]"),
    "no clear after the commit": ("    HAL_WR(R_HAL_CTRL, HAL_CTRL(1u, bypass, 0u, 1u, nstage));\n", ""),
    "FLAT not bypassed": ("hal_hw_commit_bank(bank, HAL_NSTAGE, hal_preamp_q22(hal_atten_eighths(step)), flat);", "hal_hw_commit_bank(bank, HAL_NSTAGE, hal_preamp_q22(hal_atten_eighths(step)), 0u);"),
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
