#!/usr/bin/env python3
"""Player 2 FLAC core (fw2/cores/flac2_core.h), piece by piece (S8-1; spec docs/features/FLAC_DECODER_V2_REVIEW.md section 5 and 6).

Piece 1 -- bit reader and partitioned Rice residuals, against an INDEPENDENT encoder (this file writes the bitstream and knows the truth):
  * every value and the exact bit position equal what was encoded (methods 0 and 1, partition orders 0-6, k 0..30, escape partitions with raw width 0..31, empty partitions, unary runs around 31/32/33+,
    first partition shortened by the predictor order);
  * the output is IDENTICAL for any input chunking (1 byte at a time to the whole blob) and any slicing (1 residual per call to all) -- the resumable-decoder property;
  * a window that ends mid-value returns NEED with exactly the values that were complete (truncated blobs), never a wrong value;
  * the refusals: reserved coding methods, a block size not divisible by 2^order, a first partition shorter than the predictor order, a unary run over the cap, a value over 32 bits;
  * every mutant (BUG=1..7) is caught.
"""
import os, random, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ((" " + str(extra)) if (extra and not ok) else ""))
    if not ok: fails += 1
class Bits:
    def __init__(self): self.b = []
    def put(self, v, n): self.b += [(v >> i) & 1 for i in range(n - 1, -1, -1)]
    def n(self): return len(self.b)
    def bytes(self):
        b = self.b + [0] * (-len(self.b) % 8)
        return bytes(int("".join(map(str, b[i:i + 8])), 2) for i in range(0, len(b), 8))
def zz_dec(v): return -((v >> 1) + 1) if v & 1 else v >> 1
FNV = lambda vals: __import__("functools").reduce(lambda h, v: ((h ^ (v & 0xFFFFFFFF)) * 16777619) & 0xFFFFFFFF, vals, 2166136261)

def gen(rng):
    method = rng.randrange(2); pbits, esc = (5, 31) if method else (4, 15); kmax = 30 if method else 14
    po = rng.randrange(7); parts = 1 << po
    per = rng.choice([1, 2, 3, 5, 8, 17, 33, 64, 100]); order = rng.randrange(0, min(per, 33)) if per > 1 else 0
    bs = per * parts
    w = Bits(); w.put(method, 2); w.put(po, 4); truth = []; ends = []
    for p in range(parts):
        count = per - (order if p == 0 else 0)
        if rng.random() < 0.08:
            raw = rng.choice([0, 0, 1, 2, 5, 13, 16, 17, 24, 31])
            w.put(esc, pbits); w.put(raw, 5)
            for _ in range(count):
                x = rng.randrange(-(1 << (raw - 1)), 1 << (raw - 1)) if raw else 0
                w.put(x & ((1 << raw) - 1) if raw else 0, raw); truth.append(x); ends.append(w.n())
            continue
        k = rng.randrange(0, kmax + 1); w.put(k, pbits)
        for _ in range(count):
            r = rng.random()
            q = rng.choice([31, 32, 33, 40, 63, 64, 65, 100]) if r < 0.05 else rng.choice([0, 1, 2, 3]) if r < 0.10 else int(min(rng.expovariate(0.7), 60))
            lim = (1 << 32) - 1
            if (q << k) > lim: q = lim >> k
            rem = rng.randrange(1 << k) if k else 0
            v = (q << k) | rem
            w.put(0, q); w.put(1, 1)
            if k: w.put(rem, k)
            truth.append(zz_dec(v)); ends.append(w.n())
    total = w.n(); blob = w.bytes() + bytes(rng.randrange(256) for _ in range(rng.randrange(0, 12)))
    M = len(truth)
    if rng.random() < 0.2:
        cut = rng.randrange(0, len(blob)); blob = blob[:cut]; M = sum(1 for e in ends if e <= cut * 8)
        if cut * 8 < 6: M = 0
    return dict(order=order, bs=bs, blob=blob, truth=truth, M=M, total=total, trunc=M != len(truth))

def build(d, bug):
    exe = d / f"h{bug}"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", f"-DBUG={bug}", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_flac2_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, lines):
    inp = "".join(lines)
    out = subprocess.run([str(exe)], input=inp, capture_output=True, text=True, timeout=600).stdout.splitlines()
    res = []
    for i in range(0, len(out), 2):
        st, cnt, bits, h = map(int, out[i].split()); vals = list(map(int, out[i + 1].split())); res.append((st, cnt, bits, h, vals))
    return res
def line(c, chunks, slices):
    return f"{c['order']} {c['bs']} {c['blob'].hex() or '-'} {len(chunks)} {' '.join(map(str, chunks))} {len(slices)} {' '.join(map(str, slices))}\n"

FULL = int(os.environ.get('FLAC2_N', '400'))      # blocks per run (1,500 in the full proof: FLAC2_N=1500)
def suite(exe, seed=11, N=None):
    N = N or FULL
    rng = random.Random(seed); cases = [gen(rng) for _ in range(N)]
    ok_truth = ok_pos = ok_trunc = ok_inv = True; bad = []
    cfgs = [([1 << 20], [1 << 20]), ([1], [1]), None, None, ([3, 1, 7], [1, 2, 3, 5])]
    results = []
    for ci, cfg in enumerate(cfgs):
        ls = []
        for c in cases:
            if cfg is None: ch = [rng.choice([1, 2, 3, 4, 5, 7, 16, 31, 64, 257]) for _ in range(rng.randrange(1, 6))]; sl = [rng.choice([1, 2, 3, 8, 17, 100, 1000]) for _ in range(rng.randrange(1, 6))]
            else: ch, sl = cfg
            ls.append(line(c, ch, sl))
        results.append(run(exe, ls))
    for i, c in enumerate(cases):
        base = results[0][i]
        for cfg_i in range(len(cfgs)):
            r = results[cfg_i][i]
            if (r[0], r[1], r[2], r[3]) != (base[0], base[1], base[2], base[3]) or r[4] != base[4]: ok_inv = False; bad.append(("invariance", i, cfg_i)); break
        st, cnt, bits, h, vals = base
        if not c["trunc"]:
            if st != 0 or vals != c["truth"]: ok_truth = False; bad.append(("truth", i, st, cnt, len(c["truth"])))
            elif bits != c["total"]: ok_pos = False; bad.append(("pos", i, bits, c["total"]))
        else:
            if vals != c["truth"][:c["M"]] or st == 2: ok_trunc = False; bad.append(("trunc", i, st, cnt, c["M"]))
    return ok_truth, ok_pos, ok_trunc, ok_inv, bad[:3]

def refusals(exe):
    def blob(method, po, extra=b"\x00\x00"):
        w = Bits(); w.put(method, 2); w.put(po, 4); return w
    cases = []; want = []
    w = Bits(); w.put(2, 2); w.put(0, 4); w.put(0, 8); cases.append(dict(order=0, bs=16, blob=w.bytes())); want.append(2)            # reserved method 2
    w = Bits(); w.put(3, 2); w.put(0, 4); cases.append(dict(order=0, bs=16, blob=w.bytes() + b"\0\0")); want.append(2)               # reserved method 3
    w = Bits(); w.put(0, 2); w.put(2, 4); w.put(0, 40); cases.append(dict(order=0, bs=18, blob=w.bytes())); want.append(2)            # 18 not divisible by 4
    w = Bits(); w.put(0, 2); w.put(4, 4); w.put(0, 40); cases.append(dict(order=2, bs=16, blob=w.bytes())); want.append(2)            # first partition (1) shorter than order 2
    w = Bits(); w.put(0, 2); w.put(0, 4); w.put(0, 4); w.put(0, 4200); cases.append(dict(order=0, bs=4, blob=w.bytes())); want.append(2)  # unary run over the cap
    w = Bits(); w.put(1, 2); w.put(0, 4); w.put(30, 5); w.put(0, 4); w.put(1, 1); w.put(0, 30); cases.append(dict(order=0, bs=1, blob=w.bytes() + b"\0\0\0\0")); want.append(2)  # q=4 at k=30: 4<<30 passes 32 bits (q=3 is legal)
    got = [r[0] for r in run(exe, [line(c, [1000], [1000]) for c in cases])]
    return got == want, got
with tempfile.TemporaryDirectory() as td:
    d = Path(td); good = build(d, 0)
    t, p, tr, inv, bad = suite(good)
    check("Rice values equal the encoded truth (random blocks: methods 0/1, partition orders 0-6, k 0..30, escapes of width 0..31, unary runs past 31)", t, bad)
    check("the exact bit position after the block equals the number of bits written", p, bad)
    check("truncated input: exactly the complete values come out, status NEED, never a wrong value", tr, bad)
    check("output identical for any chunking (1 byte .. whole) and any slicing (1 residual .. all)", inv, bad)
    ok, got = refusals(good); check("refusals: reserved methods, block size not divisible, first partition shorter than the order, unary over the cap, value over 32 bits", ok, got)
    for bug, name in ((1, "escape width 0 read as 1 bit"), (2, "remainder shifted without the stop bit"), (3, "partial partition header consumed on NEED"), (4, "zigzag off by one"), (5, "first partition ignores the order"),
                      (6, "32-bit read halves swapped"), (7, "slow-path reader not restored on NEED")):
        r = suite(build(d, bug), N=max(200, FULL // 2))
        check(f"mutant {bug} ({name}) is caught", not (r[0] and r[1] and r[2] and r[3]))
print("FAILED" if fails else "all ok", fails); sys.exit(1 if fails else 0)
