#!/usr/bin/env python3
"""settings_init() on the register file (fw2/settings/settings.c): a legacy card (TAU DEV 181 and older) is migrated at boot; every word is written and WORD 0 LAST; a power loss after any number of writes (the
rest dropped) leaves a card that the next boot migrates to exactly the same result; the wide-file probe no longer leaves a probe value in word 16 (a tuning lives there now) and puts words 0 and 16 back; a card that
is already on the new layout is not touched (no write at all); on a narrow (16-word) file nothing is written to an index that would alias onto word 0. Mutants: word 0 written first; the probe not restoring word 16."""
import random, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
PROBE = 4
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ((" " + str(extra)[:300]) if (extra and not ok) else ""))
    if not ok: fails += 1
def build(d, mutant):
    (d / "fw2/settings").mkdir(parents=True); (d / "fw2/platform").mkdir(); (d / "fw2/cores").mkdir(); (d / "sim").mkdir()
    src = (ROOT / "fw2/settings/settings.c").read_text()
    if mutant == 1:
        a = "    if (rd(0u) != w[0]) wr(0u, w[0]);\n"; assert a in src
        src = src.replace(a, "")
        b = "static void write_words(const uint32_t *w)\n{\n"; assert b in src
        src = src.replace(b, b + "    if (rd(0u) != w[0]) wr(0u, w[0]);\n")                 # word 0 first
    if mutant == 2:
        a = "wr(16u, save16); wr(0u, save0);"; assert a in src; src = src.replace(a, "wr(0u, save0);")
    (d / "fw2/settings/settings.c").write_text(src)
    for f in ("settings/settings.h", "platform/mmio.h", "cores/settings_core.h"): (d / "fw2" / f).write_text((ROOT / "fw2" / f).read_text())
    (d / "sim/h.c").write_text((ROOT / "sim/p2_settings_boot_harness.c").read_text())
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-o", str(d / "h"), str(d / "sim/h.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return d / "h"
def run(exe, text): return subprocess.run([str(exe)], input=text, capture_output=True, text=True).stdout.strip().splitlines()
def parse(out):
    d = {"log": [], "regs": None, "vals": None, "migrated": None}
    for l in out:
        p = l.split()
        if p[0] == "w": d["log"].append(int(p[1]))
        elif p[0] == "regs": d["regs"] = list(map(int, p[1:]))
        elif p[0] == "vals": d["vals"] = list(map(int, p[1:]))
        elif p[0] == "migrated": d["migrated"] = int(p[1])
    return d
def legacy(r):
    w = [0] * 32
    w[0] = r.randint(0, 100); w[1] = r.randint(0, 18); w[2] = r.randint(0, 2); w[5] = r.randint(0, 14); w[6] = r.randint(0, 30); w[15] = r.randint(0, 15)
    w[16] = r.randint(0, 31); w[17] = r.randint(0, 1) | (r.randint(0, 2) << 4); w[18] = r.getrandbits(27); w[19] = r.getrandbits(31); w[20] = r.getrandbits(31)
    for k in (8, 9, 10, 11): w[k] = r.getrandbits(31)
    return w
def fmt(w): return " ".join(map(str, w))
def tests(exe):
    ok = {}
    r = random.Random(3); good = True; order = True; bad = ""
    for n in range(40):
        w = legacy(r)
        want = list(map(int, run(exe, "E " + fmt(w) + "\n")[0].split()))
        p = parse(run(exe, "R " + fmt(w) + "\nI\n"))
        if p["regs"] != want or p["migrated"] != 1: good = False; bad = (n, "result")
        mig = p["log"][PROBE:]                                                  # after the probe's own four writes (word 0, 16, 16 back, 0 back)
        if not mig or mig[-1] != 0 or 0 in mig[:-1]: order = False
    ok["a legacy card is migrated at boot: the register file ends as the pure migration says"] = good
    ok["word 0 is written last, once, and only after every other word"] = order
    # a power loss after k writes, then a normal boot
    w = legacy(r); want = list(map(int, run(exe, "E " + fmt(w) + "\n")[0].split()))
    total = len(parse(run(exe, "R " + fmt(w) + "\nI\n"))["log"]); ok_loss = True
    for k in range(PROBE, total + 1):                                                       # (a loss inside the probe itself is a few microseconds a boot: not covered)
        first = parse(run(exe, "R " + fmt(w) + "\nW %d\nI\n" % k))                           # the writes after the k-th are lost; this boot's own view
        regs_after = first["regs"]
        second = parse(run(exe, "R " + fmt(regs_after) + "\nI\n"))                            # the next boot, power stable
        if second["regs"] != want: ok_loss = False; bad = ("loss", k, total)
        if k < total and (regs_after[0] >> 27) != 0 and k < total: ok_loss = False; bad = ("word 0 marked early", k)
    ok["a power loss after any number of migration writes (%d total, probe excluded) is repaired by the next boot to the same result" % total] = ok_loss
    # an up-to-date card: no write, values unchanged; word 16 holds a tuning and survives the probe
    new = list(map(int, run(exe, "E " + fmt(legacy(r)) + "\n")[0].split())); new[16] = 123456789; new[21] = 987654321; new[8] = 55555
    p = parse(run(exe, "R " + fmt(new) + "\nI\n"))
    ok["a card already on the new layout is read as it is (migrated 0) and word 0 and the tuning words 16, 21, 8 survive the wide probe"] = p["migrated"] == 0 and p["regs"][0] == new[0] and p["regs"][16] == 123456789 and p["regs"][21] == 987654321 and p["regs"][8] == 55555
    nolog = [x for x in p["log"] if x not in (0, 16)]
    ok["it writes nothing but the probe's own words 0 and 16 (put back to what they held)"] = not nolog and p["regs"][0] == new[0]
    # a narrow file: nothing is written at 16 and up, word 0 keeps its marker
    wl = legacy(r); p = parse(run(exe, "N 1\nR " + fmt(wl) + "\nI\n"))
    ok["on a narrow (16-word) file the legacy card is still migrated and no write aliases onto word 0 (word 0 ends as version 1)"] = p["regs"][0] >> 27 == 1 and p["migrated"] == 1
    return ok
with tempfile.TemporaryDirectory() as td:
    d = Path(td); exe = build(d / "a", 0)
    res = tests(exe)
    for k, v in res.items(): check(k, v)
    e1 = build(d / "b", 1); r1 = tests(e1); check("mutant 1 (word 0 written first) is caught", not all(r1.values()))
    e2 = build(d / "c", 2); r2 = tests(e2); check("mutant 2 (the probe leaves word 16 as a probe value) is caught", not all(r2.values()))
print("FAILED" if fails else "all ok", fails); sys.exit(1 if fails else 0)
