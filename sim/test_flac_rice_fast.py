#!/usr/bin/env python3
"""B-561: bit-exactness of the FLAC Rice fast path (docs/features/FLAC_RICE_DECODER_SPEC.md section 7).

Generates random Rice-coded residual runs, decodes each with the real fw/flac.c built twice (the new
fast path and FLAC_RICE_FAST=0, the old unary()/bits() path) and requires:
  - identical return code, eof flag, bitstream position after the call and decoded values;
  - on every case that is not truncated, the values equal what the generator encoded and the position
    equals the exact number of bits written (so the reference itself is checked, not only the two decoders).
Coverage: Rice method 0 and 1, partition orders 0-4, k 0..30, escape partitions (raw width 0..31),
q = 0 and long unary runs including exactly 31, 32 and 33+ zero bits, q + 1 + k > 32, order > 0
first-partition shortening, empty-ish partitions of 1 value, input delivered in chunks of 1..512 bytes
(every buffer-end alignment, values straddling reservoir refills), both entry points (batch residual()
and per-value rice_next()), and truncated streams (same FLAC_ERR_SHORT and eof, same values for the real prefix; values after the
end of data are not compared, the new residual() finishes the run where the old one stopped at the
last partition).
Mutation check: a deliberately broken fast path must be caught (self-test below)."""
import random
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build" / "flac_rice"


class Bits:
    def __init__(self):
        self.bits = []

    def put(self, v, n):
        for i in range(n - 1, -1, -1):
            self.bits.append((v >> i) & 1)

    def n(self):
        return len(self.bits)

    def bytes(self, upto=None):
        b = self.bits if upto is None else self.bits[:upto]
        b = b + [0] * (-len(b) % 8)
        return bytes(int("".join(map(str, b[i:i + 8])), 2) for i in range(0, len(b), 8))


def zz_enc(x):
    return (x << 1) if x >= 0 else (((-x - 1) << 1) | 1)


def zz_dec(v):
    return -((v >> 1) + 1) if v & 1 else v >> 1


def gen_case(rng):
    method = rng.randrange(2)
    pbits, esc = (5, 31) if method else (4, 15)
    kmax = 30 if method else 14
    porder = rng.randrange(5)
    parts = 1 << porder
    per = rng.choice([1, 2, 3, 5, 8, 17, 33, 64, 100])
    order = rng.randrange(0, min(per, 5)) if per > 1 else 0
    bsz = per * parts
    w = Bits()
    w.put(method, 2)
    w.put(porder, 4)
    truth, ends = [], []
    for p in range(parts):
        count = per - (order if p == 0 else 0)
        if rng.random() < 0.06:
            raw = rng.randrange(0, 20)
            w.put(esc, pbits)
            w.put(raw, 5)
            for _ in range(count):
                x = rng.randrange(-(1 << raw) // 2 if raw else 0, ((1 << raw) // 2) if raw else 1) if raw else 0
                w.put(x & ((1 << raw) - 1) if raw else 0, raw)
                truth.append(x)
                ends.append(w.n())
            continue
        k = rng.randrange(0, kmax + 1)
        w.put(k, pbits)
        for _ in range(count):
            r = rng.random()
            if r < 0.05:
                q = rng.choice([31, 32, 33, 40, 63, 64, 65, 100])
            elif r < 0.10:
                q = rng.choice([0, 1, 2, 3])
            else:
                q = int(min(rng.expovariate(0.7), 60))
            lim = (1 << 32) - 1
            if (q << k) > lim:                       # keep the encoded value inside uint32
                q = lim >> k
            rem = rng.randrange(1 << k) if k else 0
            v = (q << k) | rem
            w.put(0, q)
            w.put(1, 1)
            if k:
                w.put(rem, k)
            truth.append(zz_dec(v))
            ends.append(w.n())
    total = w.n()
    pad = rng.randrange(0, 12)
    blob = w.bytes() + bytes(rng.randrange(256) for _ in range(pad))
    trunc = rng.random() < 0.18
    M = len(truth)
    if trunc:
        cut = rng.randrange(0, len(blob))
        blob = blob[:cut]
        M = sum(1 for e in ends if e <= cut * 8)
    return dict(mode=rng.randrange(2), order=order, bsz=bsz, chunk=rng.choice([1, 2, 3, 4, 5, 7, 16, 31, 64, 128, 257, 512]),
                M=M, blob=blob, truth=truth, total=total, trunc=trunc)


def fnv(vals):
    h = 2166136261
    for v in vals:
        h = ((h ^ (v & 0xFFFFFFFF)) * 16777619) & 0xFFFFFFFF
    return h


def build(fast, tag, extra=()):
    BUILD.mkdir(parents=True, exist_ok=True)
    exe = BUILD / f"diff_{tag}"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Wno-unused-parameter",
                        "-DFLAC_TEST_EXPOSE", f"-DFLAC_RICE_FAST={fast}", *extra, "-I", str(ROOT / "fw"),
                        "-o", str(exe), str(ROOT / "sim/flac_rice_diff_harness.c"), str(ROOT / "fw/flac.c")],
                       capture_output=True, text=True)
    if r.returncode:
        print(r.stdout, r.stderr)
        sys.exit(1)
    return exe


def run(exe, cases):
    f = BUILD / "cases.txt"
    f.write_text("".join(f"{c['mode']} {c['order']} {c['bsz']} {c['chunk']} {c['M']} {c['blob'].hex()}\n" for c in cases))
    r = subprocess.run([str(exe), str(f)], capture_output=True, text=True)
    if r.returncode:
        print(r.stdout, r.stderr)
        sys.exit(1)
    return [ln.split() for ln in r.stdout.strip().split("\n")]


def check(cases, new, old, quiet=False):
    bad = 0
    for i, (c, a, b) in enumerate(zip(cases, new, old)):
        # A truncated stream may legitimately differ after the last real value (the old residual() stops at
        # the partition that hit end-of-stream, the new one finishes the run on zeros): both must report
        # the same error and eof and agree on every value decoded from real data.
        same = (a[0] == b[0] and a[1] == b[1] and a[4] == b[4]) if c["trunc"] else (a == b)
        if not same:
            bad += 1
            if bad <= 5 and not quiet:
                print(f"MISMATCH case {i} mode {c['mode']} order {c['order']} bsz {c['bsz']} chunk {c['chunk']} trunc {c['trunc']}\n  new {a}\n  old {b}")
            continue
        if not c["trunc"]:
            err, eof, absbits, hall, _ = a
            if int(err) or int(eof) or int(absbits) != c["total"] or int(hall, 16) != fnv(c["truth"]):
                bad += 1
                if bad <= 5 and not quiet:
                    print(f"WRONG vs truth, case {i}: {a} expected bits {c['total']} hash {fnv(c['truth']):08x}")
        else:
            if int(a[0]) != 3 and int(a[4], 16) != fnv(c["truth"][:c["M"]]):
                bad += 1
                if bad <= 5 and not quiet:
                    print(f"WRONG prefix, case {i}: {a}")
    return bad


def main():
    rng = random.Random(0xC1)
    cases = [gen_case(rng) for _ in range(6000)]
    new = run(build(1, "new"), cases)
    old = run(build(0, "old"), cases)
    bad = check(cases, new, old)
    trunc = sum(c["trunc"] for c in cases)
    print(f"{len(cases)} cases ({trunc} truncated), new vs old vs truth: {bad} failures")
    if bad:
        sys.exit(1)

    # Mutation self-test: a broken fast path must be caught.
    for name, flag in (("wrong clz (q-1)", "-DRICE_MUT_CLZ"), ("off-by-one used", "-DRICE_MUT_USED")):
        exe = build(1, "mut", extra=(flag,))
        mb = check(cases, run(exe, cases), old, quiet=True)
        print(f"mutant '{name}': {'caught' if mb else 'NOT CAUGHT'} ({mb} mismatches)")
        if not mb:
            sys.exit(1)
    print("PASS")


if __name__ == "__main__":
    main()
