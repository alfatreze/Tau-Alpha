#!/usr/bin/env python3
"""Player 2 skip-silence table (fw2/cores/skiptab_core.h): files written the way Tau Omega will write them (FNV-1a of the file name, CRC-32 of the entries) are read back exactly; a bad magic,
a bad CRC, a truncated file, a count over the limit and a missing name all mean 'play the whole track'; the last entry is found; 3 mutants must be caught."""
import random, struct, subprocess, sys, tempfile, zlib
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ((" " + str(extra)) if (extra and not ok) else ""))
    if not ok: fails += 1
def fnv(s):
    h = 2166136261
    for b in s.encode(): h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h
def make(entries, magic=b"TSK1", bad_crc=False, count=None):
    body = b"".join(struct.pack("<III", fnv(n), a, b) for n, a, b in entries)
    crc = zlib.crc32(body) & 0xFFFFFFFF
    if bad_crc: crc ^= 1
    return magic + struct.pack("<HHI", len(entries) if count is None else count, 0, crc) + body
def build(d, bug):
    exe = d / f"k{bug}"
    r = subprocess.run(["cc", "-O1", "-Wall", "-Werror", f"-DBUG={bug}", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_skiptab_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, cases):
    inp = "".join(n + "\n" + f.hex() + "\n" for n, f in cases)
    return [tuple(map(int, l.split())) for l in subprocess.run([str(exe)], input=inp, capture_output=True, text=True).stdout.strip().split("\n")]
def suite(exe):
    r = random.Random(4); names = [f"{i:02d} Track {r.getrandbits(16):x}.mp3" for i in range(40)]
    ents = [(n, r.randrange(0, 5_000_000), r.randrange(0, 5_000_000)) for n in names]
    f = make(ents); cases = []; want = []
    for n, a, b in ents: cases.append((n, f)); want.append((1, a, b))                      # every entry, the last included
    cases.append(("not in there.mp3", f)); want.append((0, 0, 0))
    cases.append((names[3], make(ents, magic=b"TSK2"))); want.append((0, 0, 0))
    cases.append((names[3], make(ents, bad_crc=True))); want.append((0, 0, 0))
    cases.append((names[3], f[:40])); want.append((0, 0, 0))                                # truncated
    cases.append((names[3], make(ents, count=341))); want.append((0, 0, 0))
    big = [(f"t{i}.mp3", i, i) for i in range(340)]; cases.append(("t339.mp3", make(big))); want.append((1, 339, 339))
    cases.append(("x.mp3", b"")); want.append((0, 0, 0))
    return [(g[0], g[1], g[2]) for g in run(exe, cases)] == want
with tempfile.TemporaryDirectory() as td:
    d = Path(td)
    check("tables written as Omega will write them read back exactly; every refusal is 'play the whole track'", suite(build(d, 0)))
    for bug, name in ((1, "truncated hash"), (2, "CRC not checked"), (3, "last entry never found")):
        check(f"mutant {bug} ({name}) is caught", not suite(build(d, bug)))
print("FAILED" if fails else "all ok", fails); sys.exit(1 if fails else 0)
