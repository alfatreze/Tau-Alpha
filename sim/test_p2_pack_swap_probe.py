#!/usr/bin/env python3
"""The pack-swap probe (fw2/diagnostics/pack_swap_probe.c; external-meters plan P0): on the host, with the Pocket stubbed and the REAL loader and REAL packs (the bundle comes from tools/player2/build_packs.py),
  1. a normal bundle (no window packs) does nothing: no runs, error 1;
  2. a probe bundle (--window-probe) is scanned once (7 window packs found), the seven packs are loaded one after the other in the window slot in order, twice round (14 runs), every run installs byte for byte
     what the bundle holds, flushes the instruction cache once, and ends with the live meter re-activated; the second pass equals the first (14 ok, 0 bad);
  3. a pack that answers like the pack before it (what a stale instruction cache would do) is counted BAD; a watchdog cut is HANG and BAD;
  4. no meter row to take parameters from is error 3; a damaged window pack is refused by the loader (error 2, E26 the CRC) and the others still run.
Needs the RISC-V toolchain for the packs and a native C compiler (the macOS 27 SDK needs SDKROOT=.../MacOSX26.5.sdk for `cc`)."""
import re, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(info)))
    fails += 0 if ok else 1
def build_harness(d):
    (d / "fw2/diagnostics").mkdir(parents=True)
    for f in ("pack_swap_probe.c", "pack_swap_probe.h"): (d / "fw2/diagnostics" / f).write_text((ROOT / "fw2/diagnostics" / f).read_text())
    (d / "fw2/ui").mkdir(); (d / "fw2/cores").mkdir(); (d / "fw2/stream").mkdir(); (d / "fw2/platform").mkdir(); (d / "fw2/aout").mkdir(); (d / "fw2/transport").mkdir(); (d / "sim").mkdir()
    for f in ("meter.h", "meter_pack.h"): (d / "fw2/ui" / f).write_text((ROOT / "fw2/ui" / f).read_text())
    for f in ("meter_pack_core.h", "library_core.h"): (d / "fw2/cores" / f).write_text((ROOT / "fw2/cores" / f).read_text())
    (d / "fw2/ui/theme.h").write_text("")
    (d / "fw2/stream/packfile.h").write_text("")
    (d / "fw2/platform/mmio.h").write_text("#include <stdint.h>\n#define CLK_HZ 66666667u\n#define P2_COLD_TEXT\nextern uint32_t clk;\nstatic inline uint32_t cycles(void) { return clk += 1000u; }\n")
    (d / "fw2/platform/platform.h").write_text("#include <stdint.h>\nextern int wd_cut;\nstatic inline int wd_run(uint32_t limit, void (*fn)(const void *), const void *arg) { (void)limit; if (wd_cut) return 1; fn(arg); return 0; }\n")
    (d / "fw2/aout/aout.h").write_text("#include <stdint.h>\ntypedef struct { uint32_t cnt, fifo, ridx, wptr; } aout_snap_t;\ntypedef struct { uint32_t dry, under_ring; } aout_stats_t;\nextern aout_stats_t aout_stats;\naout_snap_t aout_snap(void);\nstatic inline uint32_t aout_fill(const aout_snap_t *s) { return s->wptr - s->ridx; }\n")
    (d / "fw2/transport/mp3play.h").write_text("void mp3play_io_quiesce(void);\n")
    (d / "sim/h.c").write_text((ROOT / "sim/p2_pack_swap_probe_harness.c").read_text())
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-o", str(d / "h"), str(d / "sim/h.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
def run(d, bundle, mode=0):
    out = subprocess.run([str(d / "h"), str(bundle), str(mode)], capture_output=True, text=True).stdout.strip().splitlines()
    runs = [dict(zip(l.split()[2::2], map(int, l.split()[3::2]))) for l in out if l.startswith("run ")]
    return out, runs
with tempfile.TemporaryDirectory() as td:
    d = Path(td); build_harness(d)
    sys.path.insert(0, str(ROOT / "tools/player2")); import build_packs as bp, pack_bundle as pb
    normal, probe = d / "normal.bin", d / "probe.bin"
    with tempfile.TemporaryDirectory() as w:
        np_ = bp.build_all(Path(w)); normal.write_bytes(pb.bundle([b for _, b in np_]))
        probe.write_bytes(pb.bundle([b for _, b in np_ + bp.build_window(Path(w))]))
    out, r = run(d, normal)
    check("a normal bundle (no window packs): nothing runs, error 1, 0 found", r[-1]["ok"] == 0 and r[-1]["err"] == 1 and r[-1]["found"] == 0, r[-1])
    out, r = run(d, probe)
    last = r[-1]
    check("a probe bundle: 7 window packs found, 14 runs, 14 ok, 0 bad, 0 hang, no error", (last["found"], last["ok"], last["bad"], last["hang"], last["err"]) == (7, 14, 0, 0, 0), last)
    check("the meters are loaded in order, twice round (k = 0..6, 0..6)", [x["k"] for x in r] == list(range(7)) * 2, [x["k"] for x in r])
    check("the instruction cache is flushed once per swap, and the live meter is re-activated after the scan and after every run (15)", last["flush"] == 14 and last["done"] == 15, last)
    sm = [l for l in out if l.startswith("slot_match")]
    check("the window slot holds the last pack's image byte for byte", sm and sm[0].startswith("slot_match 1"), sm)
    tail = [l for l in out if l.startswith("fill_min")]
    check("the audio ring fill at the start of a probe (5000 pairs) and the timings are recorded; the audio did not fall behind (dry 0)", tail and tail[0].startswith("fill_min 5000 dry 0 inst_cyc_nonzero 1 run_cyc_nonzero 1"), tail)
    out, r = run(d, probe, 1)
    check("stale code after a swap (the pack answers like the one before it) is counted BAD", r[-1]["bad"] == 1 and r[-1]["ok"] == 13, r[-1])
    out, r = run(d, probe, 6)
    check("stale code on the very first swap of a pack is caught too, because the reference comes from the pack running in its own slot", r[-1]["bad"] == 1 and r[-1]["ok"] == 13, r[-1])
    out, r = run(d, probe, 5)
    check("with no pack running from its own slot the first swapped run is the reference (14 ok, 0 bad)", r[-1]["ok"] == 14 and r[-1]["bad"] == 0, r[-1])
    out, r = run(d, probe, 2)
    check("a watchdog cut is HANG and BAD", (r[-1]["hang"], r[-1]["bad"], r[-1]["ok"]) == (1, 1, 13), r[-1])
    out, r = run(d, probe, 3)
    check("no meter row at all is error 3 and nothing runs", r[-1]["err"] == 3 and r[-1]["ok"] == 0 and r[-1]["bad"] == 0, r[-1])
    out, r = run(d, probe, 4)
    check("a damaged window pack is refused by the loader (error 2, E26 the CRC) and the other six still run", (r[-1]["err"], r[-1]["inst_err"], r[-1]["ok"], r[-1]["bad"]) == (2, 26, 12, 0), r[-1])
src = (ROOT / "fw2/diagnostics/pack_swap_probe.c").read_text()
check("the counters live in PSRAM (.psram_state), not in hot RAM", '".psram_state"' in src and src.count("PSP_STATE") >= 3)
check("the probe is registered as a diagnostic feature (source, symbols and Report rows)", all(x in (ROOT / "fw2/diag_features.json").read_text() for x in ("pack_swap_probe.c", "pack_swap_probe_run", "PS INSTALL")))
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
