#!/usr/bin/env python3
"""The normal build against the Diagnostic Build (D-U01/D-U02): fw2/build.sh DIAG=0 really leaves the Check, the Check's engine tests and the raw Info page out (their symbols are not in the ELF, the About page and the
Report are), the normal build is smaller, and the Settings home loses DIAGNOSTICS and INFO and ends with ABOUT; the register fw2/diag_features.json equals what build.sh filters."""
import json, re, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(extra)))
    if not ok: fails += 1
NM = str(ROOT / "toolchain/xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-nm")
reg = json.loads((ROOT / "fw2/diag_features.json").read_text())
bs = (ROOT / "fw2/build.sh").read_text()
filt = re.search(r'case "\$s" in (.*?)\) ;;', bs).group(1)
check("the register lists exactly the sources build.sh filters when DIAG=0", sorted(re.findall(r"\*/(diagnostics/\w+\.c)", filt)) == sorted(s.replace("fw2/", "") for s in reg["sources_left_out_when_off"]), filt)
def build(diag, td):
    env = {"PATH": "/usr/bin:/bin:/usr/local/bin:/opt/homebrew/bin", "HOME": str(Path.home()), "TAU_BUILD_OUT": td, "DIAG": str(diag)}
    r = subprocess.run(["bash", "fw2/build.sh"], cwd=ROOT, env=env, capture_output=True, text=True)
    if r.returncode: print(r.stdout[-2000:], r.stderr[-2000:]); sys.exit(1)
    elf = Path(td) / "player2/fw2.elf"
    syms = {l.split()[-1] for l in subprocess.run([NM, str(elf)], capture_output=True, text=True).stdout.splitlines() if l.strip()}
    rom = (Path(td) / "player2/tau.rom").stat().st_size; cold = (Path(td) / "player2/tau-cold.bin").stat().st_size
    return syms, rom, cold
with tempfile.TemporaryDirectory() as t0, tempfile.TemporaryDirectory() as t1:
    s0, rom0, cold0 = build(0, t0); s1, rom1, cold1 = build(1, t1)
    for sym in reg["symbols_absent_when_off"]:
        check(f"the normal build has no {sym}; the Diagnostic Build has it", sym not in s0 and sym in s1)
    for sym in reg["symbols_present_when_off"]:
        check(f"the normal build has {sym}", sym in s0)
    check(f"the normal build is smaller: hot image {rom0} against {rom1} bytes, cold image {cold0} against {cold1}", rom0 <= rom1 and cold0 < cold1)
def mbuild(td, diag):
    exe = Path(td) / f"m{diag}"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-Wno-unused-variable", f"-DP2_DIAG={diag}", "-DBUG=0", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_menupages_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def mrun(exe, keys):
    o = subprocess.run([str(exe)], input=keys, capture_output=True, text=True).stdout.strip().split("\n")
    return [tuple(int(x) if x.isdigit() else x for x in re.match(r"(\S+) depth=(\d+) page=(\d+) sel=(\d+) top=(\d+) eff=(\d+),(\d+),(\d+)", l).groups()) for l in o[1:]]
with tempfile.TemporaryDirectory() as td:
    n = mbuild(td, 0); d = mbuild(td, 1)
    r = mrun(n, "U A")
    check("normal build: the last row of Home is ABOUT (ACT_ABOUT 8)", r[1][5:] == (2, 8, 0), r)
    r = mrun(n, "D D D A")
    check("normal build: the fourth row of Home is CONTROLS (ACT_CONTROLS 6), there is no DIAGNOSTICS group", r[3][5:] == (2, 6, 0), r)
    r = mrun(d, "U A")
    check("Diagnostic Build: the last row is still INFO (ACT_INFO 1)", r[1][5:] == (2, 1, 0), r)
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
