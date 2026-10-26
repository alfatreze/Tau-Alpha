#!/usr/bin/env python3
"""The meter helpers (B-814): the cell-grid core (fw2/cores/cellgrid_core.h: fit whole cells into a box, centred, capped; the nibble state store) and the history-ring core (fw2/cores/histring_core.h: a byte ring in
word-addressed memory, newest at the head, push = one word read-modify-write, unwrap by word reads) equal independent Python models over random inputs. Mutants: the grid is not centred; the nibble store clobbers its
neighbour; the ring head wraps to the wrong slot; a push only sets bits."""
import random, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(info)[:300]))
    fails += 0 if ok else 1
def build(td, src, bug):
    exe = Path(td) / ("%s%d" % (src, bug))
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", "-DBUG=%d" % bug, "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim" / ("p2_%s_harness.c" % src))], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, text): return subprocess.run([str(exe)], input=text, capture_output=True, text=True).stdout.strip().split("\n")
def fit(bx, by, bw, bh, cell, mc, mr):
    p = cell + 1; cols = min(bw // p, mc); rows = min(bh // p, mr)
    if not cols or not rows: return "0 %d %d %d %d %d %d w %d h %d" % (bx, by, cols, rows, p, cell, cols * p, rows * p)
    return "1 %d %d %d %d %d %d w %d h %d" % (bx + (bw - cols * p) // 2, by + (bh - rows * p) // 2, cols, rows, p, cell, cols * p, rows * p)
def grid_ok(exe):
    r = random.Random(3); cases = [(r.randrange(0, 40), r.randrange(0, 40), r.randrange(0, 420), r.randrange(0, 340), r.randrange(1, 14), r.choice([40, 48, 67]), r.choice([40, 54])) for _ in range(500)] + [(0, 0, 400, 323, 5, 67, 54), (16, 100, 368, 122, 8, 67, 54), (0, 0, 5, 5, 8, 67, 54)]
    got = run(exe, "".join("g %d %d %d %d %d %d %d\n" % c for c in cases))
    return got == [fit(*c) for c in cases]
def nibble_ok(exe):
    r = random.Random(4); model = [0] * 200; ops = []
    for _ in range(600): i = r.randrange(0, 200); v = r.randrange(0, 16); ops.append((i, v)); model[i] = v
    txt = "z 100\n" + "".join("s %d %d\n" % o for o in ops) + "d 100\n" + "".join("q %d\n" % i for i in (0, 1, 2, 3, 198, 199))
    got = run(exe, txt)
    return got[0] == "".join("%02x" % (model[2 * k] | (model[2 * k + 1] << 4)) for k in range(100)) and got[1:] == [str(model[i]) for i in (0, 1, 2, 3, 198, 199)]
def ring_ok(exe):
    r = random.Random(5); out = True
    for ln in (8, 16, 404, 100):
        ring = [0] * ln; head = 0; txt = "i %d\n" % ln; want = []
        for _ in range(3 * ln):
            v = r.randrange(0, 256); head = ln - 1 if head == 0 else head - 1; ring[head] = v; txt += "p %d\n" % v
            if r.random() < 0.15:
                n = r.randrange(1, ln - 3); txt += "u %d\nh\na %d\n" % (n, r.randrange(0, ln))
                want += [" ".join(str(ring[(head + j) % ln]) for j in range(n)) + " "]
                want += [str(head)]; want += [None]
                last = (txt, len(want))
        got = run(exe, txt)
        gi = 0; ok = True
        # replay the model alongside to compare get(age) too
        ring = [0] * ln; head = 0; k = 0; lines = txt.strip().split("\n"); pend = []
        for ln_ in lines:
            t = ln_.split()
            if t[0] == "p": head = ln - 1 if head == 0 else head - 1; ring[head] = int(t[1])
            elif t[0] == "u": pend.append(" ".join(str(ring[(head + j) % ln]) for j in range(int(t[1]))) + " ")
            elif t[0] == "h": pend.append(str(head))
            elif t[0] == "a": pend.append(str(ring[(head + int(t[1])) % ln]))
        out = out and got == pend
    return out
with tempfile.TemporaryDirectory() as td:
    g0 = build(td, "cellgrid", 0); r0 = build(td, "histring", 0)
    check("the grid fit equals the model over 500 random boxes (centred, capped, whole cells; the fullscreen box at cell 5 is 66 x 53 cells)", grid_ok(g0))
    check("the nibble store equals the model over 600 random sets and every neighbour is untouched", nibble_ok(g0))
    check("the ring equals the model over pushes, unwraps and gets, for lengths 8, 16, 100 and 404 (wraps included)", ring_ok(r0))
    check("mutant (the grid is not centred) is caught", not grid_ok(build(td, "cellgrid", 1)))
    check("mutant (the nibble store clobbers its neighbour) is caught", not nibble_ok(build(td, "cellgrid", 2)))
    check("mutant (the ring head wraps to the wrong slot) is caught", not ring_ok(build(td, "histring", 1)))
    check("mutant (a push only sets bits) is caught", not ring_ok(build(td, "histring", 2)))
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
