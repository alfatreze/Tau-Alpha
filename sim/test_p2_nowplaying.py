#!/usr/bin/env python3
"""Player 2 Now Playing arithmetic (fw2/cores/nowplaying_core.h): clock text, bar width, tempo and rate labels against independent Python statements; two mutants must be caught."""
import random, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ((" " + str(extra)) if (extra and not ok) else ""))
    if not ok: fails += 1
def m_time(pairs, rate):
    s = pairs // rate if rate else 0
    h, m, sec = s // 3600, (s // 60) % 60, s % 60
    return f"{min(h, 9)}:{m:02d}:{sec:02d}" if h else f"{m}:{sec:02d}"
def m_bar(pos, total, w): return 0 if not total else (w if pos >= total else pos * w // total)
def m_speed(q8): return f"{(q8 * 100 + 128) // 256 / 100:.2f}X"
def m_khz(hz):
    t = (hz + 50) // 100
    return (f"{t // 10}" if t % 10 == 0 else f"{t // 10}.{t % 10}") + "K"
def build(d, bug):
    exe = d / f"n{bug}"
    r = subprocess.run(["cc", "-O1", "-Wall", "-Werror", f"-DBUG={bug}", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_np_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def agrees(exe):
    r = random.Random(5); cases = []; want = []
    for _ in range(400):
        rate = r.choice([22050, 32000, 44100, 48000]); pairs = r.choice([0, 1, rate - 1, rate, rate * 59 + rate // 2, rate * 600 + 7, rate * 3600, rate * 3661 + rate // 2, r.randrange(0, rate * 40000)])
        cases.append(f"t {pairs} {rate} 0"); want.append(m_time(pairs, rate))
        tot = r.choice([0, 1, 1000, r.randrange(1, 2**31)]); pos = r.choice([0, tot // 2, tot, tot + 5, r.randrange(0, 2**31)])
        cases.append(f"b {pos} {tot} 360"); want.append(str(m_bar(pos, tot, 360)))
    for q in (192, 218, 256, 282, 320, 384, 448, 512): cases.append(f"s {q} 0 0"); want.append(m_speed(q))
    for hz in (22050, 32000, 44100, 48000): cases.append(f"k {hz} 0 0"); want.append(m_khz(hz))
    got = subprocess.run([str(exe)], input="\n".join(cases) + "\n", capture_output=True, text=True).stdout.split("\n")
    return [g for g in got if g != ""] == want
with tempfile.TemporaryDirectory() as td:
    d = Path(td)
    check("clock, bar, tempo and rate labels equal the independent statements", agrees(build(d, 0)))
    for bug, name in ((1, "clock rounds up"), (2, "bar not clamped")):
        check(f"mutant {bug} ({name}) is caught", not agrees(build(d, bug)))
print("FAILED" if fails else "all ok", fails); sys.exit(1 if fails else 0)
