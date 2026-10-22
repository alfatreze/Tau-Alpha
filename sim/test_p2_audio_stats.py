#!/usr/bin/env python3
"""The audio-statistics core (fw2/cores/audio_stats_core.h: rms, L/R correlation, spectral centroid, crest factor, flux and onset) against independent float/Python models over random and special inputs, plus mutants
(rms without the shared shift, correlation sign, centroid divisor, onset without its refractory time)."""
import math, random, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(info)[:300]))
    fails += 0 if ok else 1
def build(td, bug):
    exe = Path(td) / ("as%d" % bug)
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", "-DBUG=%d" % bug, "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_audio_stats_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, text): return subprocess.run([str(exe)], input=text, capture_output=True, text=True).stdout.split("\n")[:-1]
def sums(l, r): return sum(a * a for a in l), sum(b * b for b in r), sum(a * b for a, b in zip(l, r))
def rms_ok(exe):
    rnd = random.Random(1); cases = []; txt = ""
    for _ in range(300):
        n = rnd.choice([1, 50, 800, 4800, 9600]); amp = rnd.choice([1, 100, 3000, 20000, 32768])
        s = [rnd.randint(-amp, amp) for _ in range(n)]; ss = sum(x * x for x in s); cases.append((ss, n)); txt += "r %d %d\n" % (ss, n)
    got = run(exe, txt)
    return all(abs(int(g) - math.sqrt(ss / n)) <= max(2, 0.003 * math.sqrt(ss / n)) for g, (ss, n) in zip(got, cases)), len(got)
def corr_ok(exe):
    rnd = random.Random(2); txt = ""; want = []
    for _ in range(400):
        n = rnd.choice([200, 800, 4800]); amp = rnd.choice([300, 5000, 30000]); mix = rnd.random() * 2 - 1
        a = [rnd.randint(-amp, amp) for _ in range(n)]; b = [round(mix * x + (1 - abs(mix)) * rnd.randint(-amp, amp)) for x in a]
        ll, rr, lr = sums(a, b); txt += "c %d %d %d\n" % (ll, rr, lr); want.append(256 * lr / math.sqrt(ll * rr) if ll and rr else 0)
    got = run(exe, txt)
    return all(abs(int(g) - w) <= 4 for g, w in zip(got, want))
def corr_special(exe):
    x = [int(20000 * math.sin(i / 7)) for i in range(2000)]
    mono, inv = sums(x, x), sums(x, [-v for v in x])
    got = run(exe, "c %d %d %d\nc %d %d %d\nc 0 5 0\nc 5 0 0\nc 100 100 0\n" % (*mono, *inv))
    return got[0] == "256" and got[1] == "-256" and got[2] == "0" and got[3] == "0" and got[4] == "0"
def cent_ok(exe):
    rnd = random.Random(3); sp = [[rnd.randint(0, 255) for _ in range(16)] for _ in range(300)] + [[0] * 16, [0] * 15 + [200], [200] + [0] * 15]
    got = run(exe, "".join("m " + " ".join(map(str, s)) + "\n" for s in sp))
    want = [int(255 * sum(i * v for i, v in enumerate(s)) / (15 * sum(s))) if sum(s) else 0 for s in sp]
    return got == [str(w) for w in want] and got[-2] == "255" and got[-1] == "0" and got[-3] == "0"
def crest_ok(exe):
    got = run(exe, "k 100 100\nk 0 0\nk 24000 100\nk 22 16\nk 20000 10000\n")
    return got == ["16", "0", "255", "22", "32"]
def onset_ok(exe):
    q = [0] * 16; loud = [200] * 16; txt = "z\n"; t = 0; seq = []
    for k in range(40):
        s = loud if k % 8 == 4 else q; t += 26; txt += "o " + " ".join(map(str, s)) + " %d\n" % t; seq.append(k % 8 == 4)
    got = run(exe, txt)
    fire = [g.split()[1] == "1" for g in got]; flux = [int(g.split()[0]) for g in got]
    steady = run(exe, "z\n" + "".join("o " + " ".join(["80"] * 16) + " %d\n" % (500 + 26 * k) for k in range(30)))
    # a hit on the first rise of each burst; nothing in silence; a steady level stops firing; flux is the summed rise capped at 255
    return all(f for f, s in zip(fire, seq) if s) and not any(f for f, s in zip(fire, seq) if not s) and max(flux) == 255 and steady[-1].endswith(" 0") and steady[-1].startswith("0 ")
def refract_ok(exe):
    rows = []; t = 0
    for k in range(6):
        t += 20; rows.append("o " + " ".join(["255" if k % 2 == 0 else "0"] * 16) + " %d" % t)
    got = run(exe, "z\n" + "\n".join(rows) + "\n")
    return sum(g.endswith(" 1") for g in got) <= 2
with tempfile.TemporaryDirectory() as td:
    exe = build(td, 0)
    ok, n = rms_ok(exe); check("rms equals sqrt(sum/n) over %d random windows (a 32-bit divide after a shared shift)" % n, ok)
    check("correlation Q8 within 4 of the float value over 400 random pairs", corr_ok(exe))
    check("correlation: identical 256, opposite -256, silence and unrelated 0", corr_special(exe))
    check("centroid equals the weighted mean band (random spectra, silence, edges)", cent_ok(exe))
    check("crest factor Q4 (and its cap and silence)", crest_ok(exe))
    check("onset fires on every burst, never in silence or at a steady level; flux caps at 255", onset_ok(exe))
    check("onset refractory time limits the rate of hits", refract_ok(exe))
    for bug, name, fn in ((1, "rms without the shared shift", lambda e: rms_ok(e)[0]), (2, "correlation sign", corr_special), (3, "centroid divisor", cent_ok), (4, "onset without its refractory time", refract_ok)):
        check("mutant caught: " + name, not fn(build(td, bug)))
print("all ok" if not fails else "%d FAILED" % fails, fails)
sys.exit(1 if fails else 0)
