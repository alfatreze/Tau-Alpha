#!/usr/bin/env python3
"""Pack watchdog, firmware side (docs/features/PACK_WATCHDOG_PLAN.md, W2): the pure rule in fw2/cores/pack_strike_core.h (the hardware window wd_limit) and the call site in
fw2/ui/meters.c (a tick the watchdog cut off strikes the pack out at once and forces a repaint). The hardware path (trap entry, recovery, the registers) is proven separately on the real CPU:
`make test-rtl-wdog-soc`. Mutants: no overflow guard in the window, the cut ignored at the call site."""
import ast, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/player2"))
import pack_meter as pm, pack_bundle as pb
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("  " + str(info) if info and not ok else ""))
    fails += 0 if ok else 1

CORE = r'''
#include <stdio.h>
#include <stdint.h>
#include "pack_strike_core.h"
int main(void)
{
    const uint32_t soft[] = {0u, 1u, 200000u, 500000u, 600000u, 5000000u, 0xFFFFFFFFu, 0x40000000u};
    for (unsigned i = 0; i < sizeof soft / sizeof *soft; i++) printf("%u\n", wd_limit(soft[i]));
    return 0;
}
'''
def core(d, flags=()):
    d.mkdir(parents=True, exist_ok=True)
    (d / "c.c").write_text(CORE)
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", *flags, "-I", str(ROOT / "fw2/cores"), "-o", str(d / "c"), str(d / "c.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return subprocess.run([str(d / "c")], capture_output=True, text=True).stdout.strip().splitlines()

# the strike-path build: the same host harness as test_p2_pack_strike.py (its build() is reused as written)
src_t = (ROOT / "sim/test_p2_pack_strike.py").read_text()
fn = [n for n in ast.parse(src_t).body if isinstance(n, ast.FunctionDef) and n.name == "build"][0]
ns = {"ROOT": ROOT, "subprocess": subprocess, "sys": sys, "Path": Path}
exec(compile(ast.Module([fn], []), "build", "exec"), ns)
build_orig = ns["build"]
def build(d, flags=(), patch=None):
    """build_orig(d) understands only its own mutants, so apply ours by editing the temp copies it writes: run it, then rebuild with the extra flags / source patch."""
    exe = build_orig(d)
    mp = d / "fw2/ui/meters.c"
    if patch: mp.write_text(patch(mp.read_text()))
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-DP2_HOST", *flags, "-include", str(ROOT / "sim/p2_host_regs.h"), "-I", str(ROOT / "fw"), "-o", str(d / "h2"), str(d / "sim/h.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return d / "h2"

with tempfile.TemporaryDirectory() as td:
    tmp = Path(td)
    o = core(tmp / "core")
    want = [20000000, 2000000, 2000000, 2000000, 2400000, 20000000, 20000000, 20000000]
    check("hardware window = 4 x the soft limit, floor 2 M, ceiling 20 M, 0 (no limit) = ceiling", [int(x) for x in o[:8]] == want, o[:8])
    check("mutant (no overflow guard in the window) is caught", [int(x) for x in core(tmp / "m4", ["-DBUG=4"])[:8]] != want)

    blob, _ = pm.build("winamp_bars", pm.SLOT_BASE + pm.SLOT["winamp_bars"] * pm.SLOT_SIZE, tmp / "bars.elf")
    bundle = tmp / "b.bin"; bundle.write_bytes(pb.bundle([blob]))
    def run(exe, costs): return subprocess.run([str(exe), str(bundle)] + [str(c) for c in costs], capture_output=True, text=True).stdout.strip().splitlines()
    good = build(tmp / "good")
    lim = int(run(good, [])[0].split()[1])
    o = run(good, ["F0", 0, 0])[1:]
    check("without a watchdog cut the pack keeps running (state 0, no strike)", all(l.startswith("pack state 0 struck 0") for l in o), o)
    cut = build(tmp / "cut", ["-DWD_HOST_CUT"])
    o = run(cut, ["F0", 0, 0, 0])[1:]
    check("a tick the watchdog cut off strikes the pack out on the first tick (state 40), even a forced one", o[0].endswith("state 40 struck 1 overruns 0"), o)
    check("from the next picture the built-in code draws (the pack is not called again)", o[1].startswith("built-in state 40 struck 1") and o[2].startswith("built-in state 40 struck 1"), o)
    nocut = build(tmp / "nocut", ["-DWD_HOST_CUT"], patch=lambda s: s.replace("        if (cut) { pack_slot", "        if (0) { pack_slot"))
    check("mutant (the cut is ignored at the call site) is caught", run(nocut, ["F0", 0, 0, 0]) != run(cut, ["F0", 0, 0, 0]))
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
