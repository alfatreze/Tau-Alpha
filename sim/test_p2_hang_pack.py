#!/usr/bin/env python3
"""The hang test pack for the pack watchdog (docs/features/PACK_WATCHDOG_PLAN.md, W4; fw2/ui/meter_pack_winamp_bars_hang.c). It is the card-side half of the Pocket test, so what it must be is checked here:
  1. it builds freestanding at the simulator slot and at the real slot, carries Winamp Bars' meter id and slot, and its default build (HANG_AFTER 200) is a valid pack file;
  2. until it hangs it IS Winamp Bars: with the hang pushed out of reach, the run on tools/rv32sim.py (the pack loader + the pack harness) gives exactly the output of the real Bars pack;
  3. both hang variants (a plain endless loop; an endless loop issuing engine commands) are accepted by the loader and then never return: the run does not finish within a time that the real Bars run needs many times over;
  4. the shipped pack is not affected: the Bars pack built from its own source has no hang code, and the builder's default source is unchanged;
  5. the hang pack is a card file only: no firmware source, bundle tool or packager lists it."""
import subprocess, sys, tempfile, re
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/player2")); sys.path.insert(0, str(ROOT / "tools"))
import pack_meter as pm
SIM_ORG, SIM_SCRATCH = 0x00400000, 0x00300000
SIMREGS = ["-include", str(ROOT / "sim/p2_pack_sim_regs.h")]
fails = 0
def check(name, cond, info=""):
    global fails
    print(("ok   " if cond else "FAIL ") + name + ("  " + info if info and not cond else ""))
    fails += 0 if cond else 1
def build_harness(tmp):
    elf = tmp / "harness_bars.elf"
    cmd = [pm.tool("gcc"), "-march=rv32im", "-mabi=ilp32", "-mno-relax", "-O2", "-ffreestanding", "-nostdlib", "-nostartfiles", "-Wall", "-Wno-unused-function", "-Wno-unused-variable", "-Wno-unused-const-variable", "-Wno-comment",
           "-DPACK_ORG=0x%X" % SIM_ORG, "-DSCRATCH_ORG=0x%X" % SIM_SCRATCH, "-DPACK_BARS", "-DPACK_METER=%d" % pm.METER_IDS["winamp_bars"], "-DPACK_SLOT=%d" % pm.SLOT["winamp_bars"], "-Wl,--no-warn-rwx-segments", "-T", str(ROOT / "tools/host/link.ld"),
           str(ROOT / "tools/host/start.S"), str(ROOT / "tools/host/p2_pack_harness.c"), "-I", str(ROOT / "fw2/ui"), "-I", str(ROOT / "fw2/cores"), "-o", str(elf), "-lgcc"]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return elf
def run(elf, blob, tmp, timeout):
    """-> (finished, output lines)"""
    f = tmp / "run.tmpk"; f.write_bytes(blob)
    try:
        r = subprocess.run([sys.executable, str(ROOT / "tools/rv32sim.py"), "--dev=" + str(ROOT / "tools/p2_dev_model.py"), str(elf), str(f)], capture_output=True, text=True, timeout=timeout)
        return True, [l for l in r.stdout.splitlines() if not l.startswith("[")]
    except subprocess.TimeoutExpired as e:
        out = e.stdout.decode() if isinstance(e.stdout, bytes) else (e.stdout or "")
        return False, [l for l in out.splitlines() if not l.startswith("[")]
def main():
    import time
    with tempfile.TemporaryDirectory() as d:
        tmp = Path(d)
        ref, _ = pm.build("winamp_bars", SIM_ORG, tmp / "ref.elf", SIMREGS, scratch=SIM_SCRATCH)
        elf = build_harness(tmp)
        t0 = time.time(); done, want = run(elf, ref, tmp, 600); took = time.time() - t0
        check("the real Bars pack runs the harness to the end (the reference for the checks below)", done and want[:1] == ["LOAD E0"] and len(want) > 4, str(want[:3]))
        wait = max(10.0, 4 * took)
        for mode, what in ((0, "a plain endless loop"), (1, "an endless loop that keeps issuing engine commands")):
            far, _ = pm.build("winamp_bars", SIM_ORG, tmp / "far.elf", SIMREGS + ["-DHANG_AFTER=100000000", "-DHANG_MODE=%d" % mode], scratch=SIM_SCRATCH, source="winamp_bars_hang")
            done, got = run(elf, far, tmp, 600)
            check("mode %d, hang out of reach: it is Winamp Bars (output equal to the real pack's, every scenario)" % mode, done and got == want, "\n got %s\n want %s" % (got[:4], want[:4]))
            hang, info = pm.build("winamp_bars", SIM_ORG, tmp / "hang.elf", SIMREGS + ["-DHANG_AFTER=3", "-DHANG_MODE=%d" % mode], scratch=SIM_SCRATCH, source="winamp_bars_hang")
            done, got = run(elf, hang, tmp, wait)
            check("mode %d (%s): the loader accepts it" % (mode, what), got[:1] == ["LOAD E0"], str(got[:2]))
            check("mode %d: after its third picture the tick never returns (no end within %.0f s; the real pack needs %.1f s)" % (mode, wait, took), not done and len(got) < len(want), "finished=%s lines=%d" % (done, len(got)))
        for hm in (0, 1):
            blob, info = pm.build("winamp_bars", pm.SLOT_BASE + pm.SLOT["winamp_bars"] * pm.SLOT_SIZE, tmp / "hw.elf", ["-DHANG_MODE=%d" % hm], source="winamp_bars_hang")
            import struct
            magic, abi, mid = struct.unpack_from("<IHH", blob, 0)
            check("mode %d: the default build (hang after 200 pictures) is a valid pack file for Winamp Bars at its real slot, no undefined symbols" % hm, magic == 0x4B504D54 and mid == pm.METER_IDS["winamp_bars"] and info["org"] == pm.SLOT_BASE + pm.SLOT["winamp_bars"] * pm.SLOT_SIZE, str(info))
        shipped, _ = pm.build("winamp_bars", SIM_ORG, tmp / "s.elf", SIMREGS, scratch=SIM_SCRATCH)
        syms = subprocess.run([pm.tool("nm"), str(tmp / "s.elf")], capture_output=True, text=True).stdout
        check("the shipped Bars pack (built from its own source) has no hang code", "hang_ticks" not in syms and "bars_entry" not in syms)
    listed = []
    for pat in ("fw2/**/*.c", "fw2/**/*.h", "fw2/build.sh", "tools/player2/pack_bundle.py", "tools/package_dev_build.py", "fw2/diag_features.json", "Makefile"):
        for p in ROOT.glob(pat):
            if "hang" in p.read_text(errors="ignore").lower() and "winamp_bars_hang" in p.read_text(errors="ignore") and p.name != "meter_pack_winamp_bars_hang.c": listed.append(str(p.relative_to(ROOT)))
    check("the hang pack is a card file only: no firmware source, bundle tool or packager names it", not listed, str(listed))
    print("PASSED" if not fails else "FAILED (%d)" % fails)
    sys.exit(1 if fails else 0)
main()
