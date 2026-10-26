#!/usr/bin/env python3
"""B-641: fw/halcyon_prst.h (the real C, compiled here) reads PRST sections exactly as tools/halcyon_assets.py writes them: the same names, controls, preamp and Q2.22 words; every
corruption is refused (any single byte of the section, a bad CRC, truncation, extension, a count out of range); the reader's extra rules (control ranges, preamp above unity or zero,
an unstable stage, a name without its NUL, flags) refuse a file whose CRC is valid; the integer stability test equals the writer's pole test on random and boundary values; and four
mutants of the header (no CRC check, no stability check, no range check, no unity check) are killed."""
import io, contextlib, os, random, struct, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools")); sys.path.insert(0, str(ROOT / "tools" / "lab"))
import halcyon_assets as h
import halcyon_engine_model as em

fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + (" " + info if info else ""))
    fails += 0 if ok else 1

def stage(kind, f, q, g): return tuple(em.rbj(kind, f, q, g))
PRESETS = [
    {"name": "SOFT", "controls": {"warmth": 1, "air": -1}},
    {"name": "SIB 3", "controls": {"warmth": -5, "bass": 5, "vocal": 0, "punch": -5, "sibilance": 5, "air": 5}},
    {"name": "MY HEADPHONE", "preamp": 0.8, "stages": [stage("low", 100, 0.7, 4.0), stage("peak", 3000, 1.2, -6.0), stage("high", 9000, 0.7, 3.0)]},
    {"name": "TEN-STAGE", "preamp": 3_000_000, "stages": [stage("peak", 200 * (i + 1), 1.0, (-1) ** i * 3.0) for i in range(10)]},
]
good = h.pack_presets(PRESETS)

def recrc(b):
    return b[:8] + struct.pack("<I", h.crc(b[12:])) + b[12:]

_exe = {}
_tmp = tempfile.TemporaryDirectory()
def run(blob, fwdir, pairs=()):
    key = str(fwdir)
    if key not in _exe:
        exe = Path(_tmp.name) / f"x{len(_exe)}"
        r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-I", key, "-o", str(exe), str(ROOT / "sim/halcyon_prst_harness.c")], capture_output=True, text=True)
        if r.returncode:
            print(r.stdout, r.stderr); sys.exit(1)
        _exe[key] = exe
    f = Path(_tmp.name) / "f.bin"
    f.write_bytes(blob)
    inp = "".join(f"{a} {b}\n" for a, b in pairs)
    return subprocess.run([str(_exe[key]), str(f)], input=inp, capture_output=True, text=True, check=True).stdout.splitlines()

def accepted(out): return out[0].split()[0] == "0"

out = run(good, ROOT / "fw")
same = out[0] == "0 4"
if same:
    for line, p in zip(out[1:], h.parse_presets(good)):
        typ, rest = line.split(" ", 1)
        if not rest.startswith(p["name"] + " "):
            same = False; break
        vals = list(map(int, rest[len(p["name"]) + 1:].split()))
        if "controls" in p:
            same &= typ == "0" and vals == [p["controls"][k] for k in h.CONTROLS]
        else:
            flat = [v for st in p["stages"] for v in st]
            same &= typ == "1" and vals[0] == len(p["stages"]) and vals[1] == p["preamp"] and vals[2:] == flat
check("the C reader returns the writer's 4 presets exactly (names, controls, preamp, Q2.22 words)", same)

bad = 0
for i in range(len(good)):
    b = bytearray(good); b[i] ^= 0x5A
    if accepted(run(bytes(b), ROOT / "fw")): bad += 1
check("every single-byte corruption of a %d-byte section is refused" % len(good), bad == 0, "(%d accepted)" % bad)
for name, blob in (("truncated", good[:-1]), ("extended", good + b"\0"), ("empty", b""), ("count 0", recrc(good[:7] + b"\0" + good[8:])),
                   ("count 9", recrc(good[:7] + b"\x09" + good[8:])), ("version 2", recrc(good[:4] + b"\2\0" + good[6:]))):
    check("refused: " + name, not accepted(run(blob, ROOT / "fw")))

def first_entry_edit(off, val):
    b = bytearray(good); b[12 + off] = val; return recrc(bytes(b))
# valid CRC, but the reader's own rules
check("refused: a control above +5 (CRC valid)", not accepted(run(first_entry_edit(18, 6), ROOT / "fw")))
check("refused: sibilance below 0 (CRC valid)", not accepted(run(first_entry_edit(22, 0xFF), ROOT / "fw")))
check("refused: a name without its NUL (CRC valid)", not accepted(run(first_entry_edit(16, ord("X")), ROOT / "fw")))
check("refused: non-zero flags (CRC valid)", not accepted(run(first_entry_edit(17, 1), ROOT / "fw")))
check("refused: an unknown preset type (CRC valid)", not accepted(run(first_entry_edit(0, 2), ROOT / "fw")))
raw_at = 12 + 24 + 24 + 18                          # third entry's payload (after two control entries and its own 18-byte head), its first byte = nstages
def edit_raw(off, data):
    b = bytearray(good); b[raw_at + off:raw_at + off + len(data)] = data; return recrc(bytes(b))
check("refused: preamp above unity (CRC valid)", not accepted(run(edit_raw(1, (1 << 22 | 1).to_bytes(3, "little")), ROOT / "fw")))
check("refused: preamp zero (CRC valid)", not accepted(run(edit_raw(1, b"\0\0\0"), ROOT / "fw")))
check("refused: a stage with a1 = -2.0 (a pole on the unit circle, CRC valid)", not accepted(run(edit_raw(4 + 9, h._i24(-(1 << 23))), ROOT / "fw")))
unst = bytearray(good); unst[raw_at + 4 + 12:raw_at + 4 + 15] = h._i24((1 << 22) + 5)       # a2 above 1.0 in the first stage
check("refused: an unstable first raw stage (CRC valid)", not accepted(run(recrc(bytes(unst)), ROOT / "fw")))
check("refused: more than 10 raw stages (CRC valid)", not accepted(run(edit_raw(0, bytes([11])), ROOT / "fw")))

import make_halcyon_test_assets as mt
check("the hardware-test presets (tools/make_halcyon_test_assets.py) are accepted by the C reader", run(h.pack_presets(mt.PRESETS), ROOT / "fw")[0] == "0 2")
rnd = random.Random(3)
pairs = [(rnd.randint(-(1 << 23), (1 << 23) - 1), rnd.randint(-(1 << 23), (1 << 23) - 1)) for _ in range(3000)]
pairs += [(a, b) for b in (-(1 << 22) - 1, -(1 << 22), -(1 << 22) + 1, 0, (1 << 22) - 1, 1 << 22) for a in (0, (1 << 22) + b - 1, (1 << 22) + b, (1 << 22) + b + 1, -(1 << 22) - b, -(1 << 22) - b + 1, -(1 << 22) - b - 1)]
out = run(good, ROOT / "fw", pairs)
got = [l for l in out if l.startswith("S ")]
want = ["S %d" % int(h.stable_q(a, b)) for a, b in pairs]
check("the integer stability test equals the writer's pole test for %d random and boundary pairs" % len(pairs), got == want, "(%d differ)" % sum(1 for x, y in zip(got, want) if x != y))

src = (ROOT / "fw/halcyon_prst.h").read_text()
mutants = {
    "no CRC check": ("if (LIB_CRC_DONE(lib_crc_update(LIB_CRC_INIT, d + 12, n - 12u)) != lib_ld32(d + 8)) return HP_E_CRC;", ""),
    "no stability check": ("if (!hp_stable(hp_i24(d + pos + 15u * s + 9u), hp_i24(d + pos + 15u * s + 12u))) return HP_E_ENTRY;", "(void)0;"),
    "no control range check": ("if (v < (i == 4u ? 0 : -5) || v > 5) return HP_E_ENTRY;", ""),
    "preamp above unity allowed": ("pre <= 0 || pre > HP_Q22_ONE", "pre <= 0"),
    "trailing bytes allowed": ("if (pos != n) return HP_E_SIZE;", ""),
}
killed_all = True
for name, (a, b) in mutants.items():
    assert a in src, name
    with tempfile.TemporaryDirectory(dir=_tmp.name) as d:
        for f in ("halcyon_core.h", "halcyon_tab.h", "library_core.h"):
            (Path(d) / f).write_text((ROOT / "fw" / f).read_text())
        (Path(d) / "halcyon_prst.h").write_text(src.replace(a, b, 1))
        # re-run the refusal checks quickly against the mutant: each must accept something it should refuse
        cases = [bytes(b2 ^ (0x5A if j == 40 else 0) for j, b2 in enumerate(good)),
                 first_entry_edit(18, 6), edit_raw(1, (1 << 22 | 1).to_bytes(3, "little")), recrc(bytes(unst)), recrc(good + b"\0")]
        survived = all(not accepted(run(c, Path(d))) for c in cases)
    check("mutant killed: " + name, not survived)
sys.exit(1 if fails else 0)
