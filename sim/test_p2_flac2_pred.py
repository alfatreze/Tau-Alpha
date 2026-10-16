#!/usr/bin/env python3
"""Player 2 FLAC core piece 2 (fw2/cores/flac2_core.h): FIXED and LPC predictors, three properties against exact big-integer arithmetic.
 * decode = original: signals inside the declared bit range are turned into residuals with exact Python integers; the core must give the signal back (all orders 1..32, precision 5..15, shifts 0..31,
   16 to 25-bit samples, full-scale noise, alternating extremes, and coefficient sets built to sit AT the 32-bit bound);
 * the tier choice is safe: every predictor the bound sends to the 32-bit tier is exact, the directed case S = 2^(32-b) with extreme samples (a sum of exactly +2^31) must go to the 64-bit tier, and forcing the
   32-bit tier on it is demonstrably wrong (so the bound is necessary, not decoration);
 * slicing: the same output however the samples are cut into calls;
 * corrupt data (arbitrary 32-bit residuals and warm-up, 15-bit coefficients) runs clean under UBSan/ASan (no signed-overflow undefined behaviour), a hard requirement for a decoder fed from an SD card;
 * mutants: bound off by one, shift off by one.
"""
import os, random, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ((" " + str(extra)) if (extra and not ok) else ""))
    if not ok: fails += 1
FIX = {0: [], 1: [1], 2: [2, -1], 3: [3, -3, 1], 4: [4, -6, 4, -1]}
N = int(os.environ.get("FLAC2_PRED_N", "600"))
def build(d, bug, san=False):
    exe = d / f"p{bug}{'s' if san else ''}"
    fl = ["-fsanitize=undefined,address", "-fno-sanitize-recover=all"] if san else []
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", f"-DBUG={bug}", *fl, "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_flac2_pred_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, cases):
    inp = "".join(f"{k} {o} {sh} {b} {fo} {len(x)} {sp} {' '.join(map(str, co))} {' '.join(map(str, x))}\n" for (k, o, sh, b, fo, sp, co, x) in cases)
    r = subprocess.run([str(exe)], input=inp, capture_output=True, text=True, timeout=600)
    if r.returncode: return None
    ls = r.stdout.splitlines(); return [(int(ls[i]), list(map(int, ls[i + 1].split()))) for i in range(0, len(ls), 2)]
def residuals_lpc(sig, order, coef, shift):
    res = list(sig[:order])
    for i in range(order, len(sig)):
        res.append(sig[i] - (sum(coef[j] * sig[i - 1 - j] for j in range(order)) >> shift))
    return res
def residuals_fixed(sig, order):
    res = list(sig[:order])
    for i in range(order, len(sig)):
        res.append(sig[i] - sum(c * sig[i - 1 - j] for j, c in enumerate(FIX[order])))
    return res
def gen_signal(rng, b, n):
    lo, hi = -(1 << (b - 1)), (1 << (b - 1)) - 1; kind = rng.randrange(5)
    if kind == 0: return [rng.randrange(lo, hi + 1) for _ in range(n)]
    if kind == 1: return [lo if i % 2 else hi for i in range(n)]
    if kind == 2: return [lo] * n
    if kind == 3: return [hi if (i // 3) % 2 else lo for i in range(n)]
    import math; return [max(lo, min(hi, int(hi * math.sin(i * 0.05 + 1)))) for i in range(n)]
def suite(exe, seed=3):
    rng = random.Random(seed); cases = []; want = []; tiers = []
    for _ in range(N):
        b = rng.choice([8, 12, 16, 16, 16, 17, 20, 24, 25]); order = rng.randrange(1, 33); prec = rng.randrange(5, 16); shift = rng.randrange(0, 32); n = order + rng.randrange(1, 120)
        cm = (1 << (prec - 1)) - 1
        if rng.random() < 0.5: coef = [rng.randrange(-cm - 1, cm + 1) for _ in range(order)]
        else:                                                           # push S toward the bound
            tgt = (1 << (32 - b)) * rng.choice([0.5, 0.9, 0.99, 1.0, 1.01, 1.5]); coef = [max(-cm - 1, min(cm, int(rng.choice([-1, 1]) * tgt / order))) for _ in range(order)]
        sig = gen_signal(rng, b, n)
        res = residuals_lpc(sig, order, coef, shift)
        if any(abs(r) >= 1 << 31 for r in res): continue               # a residual a real encoder could not code
        sp = rng.choice([1, 3, 17, 10000])
        cases.append((2, order, shift, b, 0, sp, coef, res)); want.append(sig)
    for _ in range(N // 3):
        b = rng.choice([8, 16, 24]); order = rng.randrange(0, 5); n = order + rng.randrange(1, 100); sig = gen_signal(rng, b, n)
        res = residuals_fixed(sig, order)
        cases.append((1, order, 0, b, 0, rng.choice([1, 5, 10000]), [], res)); want.append(sig)
    out = run(exe, cases)
    return cases, want, out
def directed(exe):
    b = 16; c = [-16384] * 4; x = [-32768] * 12                         # S = 65536 = 2^(32-b): sum = +2^31 exactly
    res = residuals_lpc(x, 4, c, 1)                                     # shift 1: the prediction is +2^30 and the residual fits 32 bits (at shift 0 no real stream could hold it)
    auto = run(exe, [(2, 4, 1, b, 0, 10000, c, res)])[0]
    forced = run(exe, [(2, 4, 1, b, 2, 10000, c, res)])[0]
    ok_auto = auto[1] == x and auto[0] == 2
    ok_forced_wrong = forced[1] != x                                      # the 32-bit tier cannot be right here
    c2 = [-16384] * 3 + [-16383]; res2 = residuals_lpc(x, 4, c2, 1)
    below = run(exe, [(2, 4, 1, b, 0, 10000, c2, res2)])[0]               # S = 65535 < 2^16: 32-bit tier, exact
    return ok_auto, ok_forced_wrong, below[1] == x and below[0] == 1
def corrupt(exe):
    rng = random.Random(9); cases = []
    for _ in range(300):
        order = rng.randrange(1, 33); n = order + rng.randrange(1, 80)
        cases.append((2, order, rng.randrange(0, 32), rng.choice([8, 16, 24]), rng.choice([0, 1, 2]), rng.choice([1, 7, 1000]), [rng.randrange(-16384, 16384) for _ in range(order)], [rng.choice([rng.randrange(-2**31, 2**31), 2**31 - 1, -2**31]) for _ in range(n)]))
    for _ in range(100):
        order = rng.randrange(0, 5); n = order + rng.randrange(1, 60)
        cases.append((1, order, 0, 16, 0, 5, [], [rng.choice([rng.randrange(-2**31, 2**31), 2**31 - 1, -2**31]) for _ in range(n)]))
    return run(exe, cases) is not None
with tempfile.TemporaryDirectory() as td:
    d = Path(td); good = build(d, 0)
    cases, want, out = suite(good)
    okdec = out is not None and all(o[1] == w for o, w in zip(out, want))
    check(f"decode = original for {len(cases)} predictors/signals (FIXED 0..4; LPC order 1..32, precision 5..15, shift 0..31, 8 to 25-bit samples, sets built at the 32-bit bound)", okdec)
    n32 = sum(1 for o in out if o[0] == 1) if out else 0
    check(f"both tiers are exercised ({n32} on the 32-bit tier, {len(out) - n32 if out else 0} on the 64-bit tier / FIXED counted as 32)", out is not None and 0 < n32 < len(out))
    a, f, bl = directed(good)
    check("S = 2^(32-b) with extreme samples (a sum of exactly +2^31) goes to the 64-bit tier and is exact", a)
    check("... and the 32-bit tier forced on it is wrong (the bound is necessary)", f)
    check("S just below the bound takes the 32-bit tier and is exact", bl)
    check("corrupt data (arbitrary 32-bit residuals, any tier) is clean under UBSan and ASan", corrupt(build(d, 0, san=True)))
    for bug, name in ((8, "bound off by one"), (9, "64-bit shift off by one")):
        c2, w2, o2 = suite(build(d, bug)); caught = o2 is None or any(o[1] != w for o, w in zip(o2, w2)) or not directed(build(d, bug))[0]
        check(f"mutant {bug} ({name}) is caught", caught)
print("FAILED" if fails else "all ok", fails); sys.exit(1 if fails else 0)
