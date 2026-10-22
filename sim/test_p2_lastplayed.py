#!/usr/bin/env python3
"""The last played track (fw2/cores/lastplayed_core.h): pack then unpack is the identity for every kind/parent/track; an unset record packs to zeros and is refused; a record with a wrong check word (stale, foreign,
a flipped bit in any of the three words) is refused; the track is found in the rebuilt queue (first, middle, last) and a track that is no longer in it is not found. Mutants: the check word is ignored; the search misses
the last track."""
import random, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(extra)))
    if not ok: fails += 1
def build(td, bug):
    exe = Path(td) / ("l%d" % bug)
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", "-DBUG=%d" % bug, "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_lastplayed_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, text): return subprocess.run([str(exe)], input=text, capture_output=True, text=True).stdout.strip().split("\n")
def props(exe):
    rnd = random.Random(5); ok = {}
    cases = [(rnd.randrange(0, 16), rnd.randrange(0, 65536), rnd.randrange(0, 16384)) for _ in range(300)] + [(0, 0, 0), (15, 65535, 16383), (1, 0, 0)]
    packs = run(exe, "".join("p 1 %d %d %d\n" % c for c in cases))
    ups = run(exe, "".join("u %s\n" % p for p in packs))
    ok["pack then unpack is the identity (300 random records and the corners)"] = all(u == "%d %d %d" % c for u, c in zip(ups, cases))
    ok["no record packs to zeros and is refused"] = run(exe, "p 0 3 4 5\n") == ["0 0 0"] and run(exe, "u 0 0 0\n") == ["-"]
    flips = []
    for c, p in zip(cases[:100], packs[:100]):
        w = [int(x) for x in p.split()]
        for wi in range(3):
            for bit in (0, 5, 17, 31):
                v = list(w); v[wi] ^= 1 << bit
                if wi == 0 and bit == 31: continue                       # clearing the valid bit is a refusal by itself
                flips.append("u %d %d %d\n" % tuple(v))
    ok["a flipped bit in any of the three words is refused"] = all(r == "-" for r in run(exe, "".join(flips)))
    ok["a foreign word set (the old player's library words) is refused"] = run(exe, "u 2147483648 12345 99\n") == ["-"]
    ids = [7, 3, 9, 2, 40, 5]
    ok["the track is found in the rebuilt queue (first, middle, last)"] = run(exe, "".join("f %d %d %s\n" % (i, len(ids), " ".join(map(str, ids))) for i in (7, 9, 5))) == ["0", "2", "5"]
    ok["a track that is no longer in the queue is not found"] = run(exe, "f 8 %d %s\n" % (len(ids), " ".join(map(str, ids)))) == ["-1"]
    return ok
with tempfile.TemporaryDirectory() as td:
    g = build(td, 0)
    for k, v in props(g).items(): check(k, v)
    for bug, name in ((1, "the check word is ignored"), (2, "the search misses the last track")):
        check("mutant (%s) is caught" % name, not all(props(build(td, bug)).values()))
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
