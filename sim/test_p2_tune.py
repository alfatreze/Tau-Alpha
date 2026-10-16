#!/usr/bin/env python3
"""Saving a tuned meter (fw2/cores/tune_core.h): the C packer must equal an independent Python implementation of the layout (check 8 | meter 3 | preset 4 | count 4 | count x { index 6 | value n bits } in
4 words of 31 bits), the C reader must give back what the Python writer stored, and anything else (zeros, a flipped bit, an index past the table, a value past its maximum) is refused.
Mutant: the check byte ignores the data."""
import random, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name)
    if not ok: fails += 1
def build(td, bug):
    exe = Path(td) / f"t{bug}"
    r = subprocess.run(["cc", "-O1", "-Wall", "-Werror", f"-DBUG={bug}", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_tune_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def nbits(mx):
    n = 1
    while n < 16 and (mx >> n): n += 1
    return n
def py_pack(meter, preset, mv, base, mx):
    bits = [0] * 128
    def put(pos, v, n):
        for i in range(n): bits[pos + i] = (v >> i) & 1
        return pos + n
    pos, n = 19, 0
    py_pack.last = []
    for i in range(len(mv)):
        if mv[i] == base[i] or n >= 15: continue
        nb = nbits(mx[i])
        if pos + 6 + nb > 124: continue
        pos = put(pos, i, 6); pos = put(pos, mv[i], nb); n += 1; py_pack.last.append((i, mv[i]))
    if not n: return 0, [0, 0, 0, 0]
    put(8, meter & 7, 3); put(11, preset & 15, 4); put(15, n, 4)
    b = [sum(bits[8 * k + j] << j for j in range(8)) for k in range(16)]
    c = 0xA5
    for k in range(1, 16): c = (((c << 1) | (c >> 7)) & 255); c = (c + b[k]) & 255
    put(0, c, 8)
    return n, [sum(bits[31 * k + j] << j for j in range(31)) for k in range(4)]
def run(exe, text): return subprocess.run([str(exe)], input=text, capture_output=True, text=True).stdout.strip().split("\n")
rng = random.Random(7)
def rand_case():
    np_ = rng.randint(1, 40)
    mx = [rng.choice([1, 1, 3, 11, 15, 100, 255, 400, 65535]) for _ in range(np_)]
    base = [rng.randint(0, m) for m in mx]
    mv = [rng.randint(0, m) if rng.random() < 0.3 else b for m, b in zip(mx, base)]
    return rng.randint(0, 7), rng.randint(0, 15), mv, base, mx
def scenario(exe):
    ok = {}
    cases = [rand_case() for _ in range(300)]
    text = "".join(f"P {m} {p} {len(mv)}\n" + " ".join(map(str, mx)) + "\n" + " ".join(map(str, mv)) + "\n" + " ".join(map(str, base)) + "\n" for m, p, mv, base, mx in cases)
    out = run(exe, text)
    ok["the C packer equals the independent Python layout on 300 random tunings"] = all(out[i] == " ".join(map(str, (lambda r: [r[0]] + r[1])(py_pack(m, p, mv, base, mx)))) for i, (m, p, mv, base, mx) in enumerate(cases))
    good = True; stored = 0
    for m, p, mv, base, mx in cases:
        n, w = py_pack(m, p, mv, base, mx); stored_list = list(py_pack.last)
        r = run(exe, f"U {' '.join(map(str, w))} {len(mx)}\n" + " ".join(map(str, mx)) + "\n")
        if n == 0:
            good = good and r[0].split()[0] == "0"; continue
        stored += 1
        head = r[0].split()
        ents = [tuple(map(int, l.split())) for l in r[1:1 + int(head[0])]]
        want = stored_list
        good = good and int(head[0]) == n and int(head[1]) == m and int(head[2]) == p and ents == want
    ok["the C reader gives back the stored meter, preset and the changed parameters"] = good and stored > 100
    mx = [10, 255, 65535, 3]; w = py_pack(2, 5, [3, 255, 40000, 1], [0, 0, 0, 0], mx)[1]
    r = run(exe, f"U {' '.join(map(str, w))} 4\n{' '.join(map(str, mx))}\n")
    ok["a small example round-trips (meter 2, preset 5, four changed values)"] = r[0] == "4 2 5" and r[1:] == ["0 3", "1 255", "2 40000", "3 1"]
    r = run(exe, "U 0 0 0 0 4\n10 255 65535 3\n")
    ok["all zeros (no tuning) is refused"] = r[0].startswith("0")
    flipped = [w[0] ^ 4, w[1], w[2], w[3]]
    r = run(exe, f"U {' '.join(map(str, flipped))} 4\n{' '.join(map(str, mx))}\n")
    ok["a flipped bit is refused by the check"] = r[0].startswith("0")
    r = run(exe, f"U {' '.join(map(str, w))} 2\n10 255\n")
    ok["an entry whose index is past the meter's table is refused"] = r[0].startswith("0")
    r = run(exe, f"U {' '.join(map(str, w))} 4\n2 255 65535 3\n")
    ok["a value above the parameter's maximum is refused"] = r[0].startswith("0")
    n, w = py_pack(0, 0, [1] * 40, [0] * 40, [1] * 40)
    ok["only what fits is stored (40 changed flags: the rest are dropped, not corrupted)"] = 0 < n < 40
    return ok
with tempfile.TemporaryDirectory() as td:
    for k, v in scenario(build(td, 0)).items(): check(k, v)
    m = scenario(build(td, 1))
    check("mutant (the check ignores the data) is caught", not all(m.values()))
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
