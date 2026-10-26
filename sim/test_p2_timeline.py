#!/usr/bin/env python3
"""Player 2 timeline core (fw2/cores/timeline_core.h): red-then-green against an independent model of the ring.

The model knows, for every pair ever put, its track and source index (ground truth the core never sees), advances `heard` the way the reader does (monotone, never past the
put pairs, flushes jump it to the push count) and checks after every step: which track is heard, the position in it, the swap count, the queue size, the accounting
verdict of every closed segment, and the put count -- including starts a few pairs below 2^32 so the wrap is crossed. Mutants (-DBUG=n) must each fail."""
import random, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
M = 0xFFFFFFFF
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name)
    if not ok: fails += 1

def build(d, bug=0):
    exe = d / f"tl{bug}"
    r = subprocess.run(["cc", "-O1", "-Wall", "-Werror", f"-DBUG={bug}", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_timeline_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe

def scenario(seed, steps=400):
    """Returns (ops text, expected lines) -- expected is None where the model makes no claim."""
    rnd = random.Random(seed)
    base = rnd.choice([0, 1000, 0x7FFFFF00, M - 3000, M - 40, M - 100000]) & M
    ops, exp = [f"INIT {base}"], []
    put_total = base; heard = base                  # unwrapped Python ints would drift; keep everything mod 2^32 only when printing
    P = base; H = base                              # absolute ints (can exceed 2^32)
    labels = {}                                     # absolute index -> (gen, src)
    segs = []                                       # dicts: gen, start, end, src, expect, tol, num, den
    gen = 0; swaps = 0; acct = 0; last_delta = 0; flushes = 0
    for _ in range(steps):
        r = rnd.random()
        open_seg = segs and segs[-1]["end"] is None
        if r < 0.10 and not open_seg:
            queued = [s for s in segs if s["end"] is None or s["end"] > H]
            if len(queued) < 4:
                gen += 1; src = rnd.choice([0, 0, 0, rnd.randrange(0, 100000)])
                expect = rnd.choice([0, rnd.randrange(500, 6000)]); tol = rnd.choice([0, 0, 2])
                num, den = rnd.choice([(1, 1), (1, 1), (1, 1), (2, 3), (3, 2)])
                segs.append(dict(gen=gen, start=P, end=None, src=src, put=0, expect=expect, tol=tol, num=num, den=den))
                ops.append(f"OPEN {gen} {src} {expect} {tol} {num} {den} 0"); exp.append(("O", 1))
        elif r < 0.55 and open_seg:
            n = rnd.randrange(1, 900)
            if rnd.random() < 0.35 and segs[-1]["expect"]:
                n = min(n, max(0, segs[-1]["expect"] - segs[-1]["put"]) or n)
            s = segs[-1]
            for k in range(n): labels[P + k] = (s["gen"], s["src"] + s["put"] + k)
            s["put"] += n; P += n; ops.append(f"PUT {n}")
        elif r < 0.62 and open_seg:
            s = segs[-1]; s["end"] = P
            if s["put"] == 0: segs.pop()                  # a track with no pair is never heard: it leaves the queue
            bad = 1 if (s["expect"] and abs(s["put"] - s["expect"]) > s["tol"]) else 0
            if bad: acct += 1; last_delta = s["put"] - s["expect"]
            ops.append("CLOSE"); exp.append(("C", bad))
        elif r < 0.70 and P > H:
            # a flush: pushes happened up to cnt in [H, P]; the FIFO and the unread ring are dropped
            cnt = rnd.randrange(H, P + 1)
            for k in range(cnt, P): labels.pop(k, None)
            P = cnt; H = cnt; flushes += 1
            segs = []
            ops.append(f"FLUSH {cnt & M}")
        elif P > H:
            h2 = min(P - 1, H + rnd.choice([1, 5, 50, 400, 3000, 9000])) if rnd.random() < 0.9 else P - 1
            if h2 < H: continue
            # swaps expected: segment starts (not the first of the epoch) in (H, h2] among the segments that exist now
            starts = [s["start"] for s in segs[1:]] if segs else []
            # segments already popped before: only those still queued count (a start <= H was handled by the earlier query)
            sw = sum(1 for st in starts if H < st <= h2 and any(x["start"] == st for x in segs))
            H = h2
            swaps += sw
            lab = labels.get(H)
            g, pos = lab if lab else (-1, 0)
            # position under the segment's ratio
            seg = next((x for x in segs if x["start"] <= H and (x["end"] is None or H < x["end"])), None)
            if seg:
                off = H - seg["start"]
                pos = seg["src"] + (off if seg["num"] == seg["den"] else (off * seg["den"]) // seg["num"])
            queued = [s for s in segs if s["end"] is None or s["end"] > H]
            segs = queued
            ops.append(f"Q {H & M}")
            exp.append(("Q", g, pos & M, swaps, len(queued), acct, last_delta, P & M, sw))
    return "\n".join(ops) + "\n", exp

def run(exe, text):
    return subprocess.run([str(exe)], input=text, capture_output=True, text=True, timeout=20).stdout.strip().splitlines()

def verdict(exe, seeds):
    bad = 0
    for sd in seeds:
        text, exp = scenario(sd)
        out = run(exe, text)
        if len(out) != len(exp): bad += 1; continue
        swaps_prev = 0
        for line, e in zip(out, exp):
            f = line.split()
            if e[0] in ("O", "C"):
                if int(f[1]) != e[1]: bad += 1; break
            else:
                got = (int(f[1]), int(f[2]), int(f[4]), int(f[5]), int(f[6]), int(f[7]), int(f[8]))
                want = (e[1], e[2], e[3], e[4], e[5], e[6], e[7])
                # fields: gen, pos, swaps_total, n, acct, last_delta, put_total
                if got[0] != want[0] or got[1] != want[1] or got[2] != want[2] or got[3] != want[3] or got[4] != want[4] or got[5] != want[5] or got[6] != want[6]:
                    bad += 1; break
    return bad

with tempfile.TemporaryDirectory() as td:
    d = Path(td)
    good = build(d, 0)
    seeds = range(300)
    bad = verdict(good, seeds)
    check(f"timeline core equals the model over {len(seeds)} scenarios (heard track, position, swaps, queue, accounting, put count; wrap crossed)", bad == 0)
    names = {1: "the first pair of a segment is not heard at heard == ring_start", 2: "the accounting check never fires", 3: "unsigned compare breaks across the 2^32 wrap",
             4: "a flush keeps the old put count", 5: "the position ignores the segment's source start", 6: "puts are counted on the oldest segment", 7: "the put count forgets the pairs",
             8: "an overlong segment passes the accounting", 9: "an empty segment stays queued (a swap to a track that was never heard)"}
    for b, nm in names.items():
        exe = build(d, b)
        check(f"mutant caught: {nm}", verdict(exe, seeds) > 0)
print("PASSED" if not fails else f"{fails} FAILED")
sys.exit(1 if fails else 0)
