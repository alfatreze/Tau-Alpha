#!/usr/bin/env python3
"""The preset-read probe (fw2/diagnostics/preset_probe.c, B-963; the measurement build of docs/features/METER_PRESETS_EXTERNAL.md): on the host, with the slot driver stubbed, it
  1. builds the path of the probe file from the stamped version text ("0.6.1-dev.212+..." -> "/Assets/tau_dev/alfatreze.TAU DEV 212/preset-probe.bin", the folder the packager makes);
  2. accepts exactly the 200 bytes the packager writes (tools/package_dev_build.py --preset-probe: the pattern is read from that file's source, so the two cannot drift) and counts a flipped byte as bad;
  3. records an open failure (pp_err 2) and a read failure (pp_err 3) without counting them as runs that read, and keeps the ring fill and the timings.
Mutants: the pattern changed on the firmware side is caught by 2; a path without the core number is caught by 1."""
import re, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(info)))
    fails += 0 if ok else 1
def run(src_mut=None):
    with tempfile.TemporaryDirectory() as td:
        d = Path(td); (d / "fw2/diagnostics").mkdir(parents=True)
        src = (ROOT / "fw2/diagnostics/preset_probe.c").read_text()
        if src_mut: src = src_mut(src)
        (d / "fw2/diagnostics/preset_probe.c").write_text(src)
        for f in ("preset_probe.h",): (d / "fw2/diagnostics" / f).write_text((ROOT / "fw2/diagnostics" / f).read_text())
        for sub, hs in (("stream", ("slot.h",)), ("platform", ("mmio.h", "platform.h")), ("aout", ("aout.h",)), ("transport", ("mp3play.h",))):
            (d / "fw2" / sub).mkdir(parents=True, exist_ok=True)
            for h in hs: (d / "fw2" / sub / h).write_text("")          # replaced below by the harness's own declarations
        (d / "fw2/cores").mkdir(); (d / "sim").mkdir()
        # minimal declarations the module needs (the real headers pull in far more than a host test wants)
        (d / "fw2/stream/slot.h").write_text("#include <stdint.h>\nint slot_open_path(uint32_t, const char *); int slot_read_blocking(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);\n")
        (d / "fw2/platform/mmio.h").write_text("#include <stdint.h>\n#define CLK_HZ 66666667u\n#define P2_COLD_TEXT\nstatic inline uint32_t cycles(void) { extern uint32_t clk; return clk += 1000u; }\n")
        (d / "fw2/platform/platform.h").write_text("#include <stdint.h>\nextern const char p2_ver_field[48];\n")
        (d / "fw2/aout/aout.h").write_text("#include <stdint.h>\ntypedef struct { uint32_t cnt, fifo, ridx, wptr; } aout_snap_t;\ntypedef struct { uint32_t dry, under_ring; } aout_stats_t;\nextern aout_stats_t aout_stats;\naout_snap_t aout_snap(void);\n#define AOUT_RING_PAIRS 65536u\nstatic inline uint32_t aout_fill(const aout_snap_t *s) { return s->wptr >= s->ridx ? s->wptr - s->ridx : s->wptr + AOUT_RING_PAIRS - s->ridx; }\n")
        (d / "fw2/transport/mp3play.h").write_text("void mp3play_io_quiesce(void);\n")
        h = (ROOT / "sim/p2_preset_probe_harness.c").read_text().replace('#define REG_RD(a) (clk += 1000u)\n#define REG_WR(a, v) ((void)(a), (void)(v))\n', "").replace("static uint32_t clk;", "uint32_t clk;")
        (d / "sim/h.c").write_text(h)
        r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-o", str(d / "h"), str(d / "sim/h.c")], capture_output=True, text=True)
        if r.returncode: print(r.stderr); sys.exit(1)
        return subprocess.run([str(d / "h")], capture_output=True, text=True).stdout.strip().splitlines()
good = run()
check("the path is the packager's core folder, built from the version text (%s)" % (good[0].split(" ", 5)[-1] if good else ""), good and good[0].endswith("/Assets/tau_dev/alfatreze.TAU DEV 212/preset-probe.bin"), good[:1])
check("the 200 bytes the packager writes are accepted (1 run, 1 ok, 0 bad, no error)", good[0].startswith("good 1 1 0 0"), good[0])
check("a flipped byte is counted as bad, not ok", good[1].startswith("corrupt 2 1 1 0"), good[1])
check("a failed open is error 2 and a failed read is error 3 (neither counts as ok or bad)", good[2].startswith("openfail 3 1 1 2") and good[3].startswith("readfail 4 1 1 3"), good[2:4])
check("the ring fill at the start (5000 pairs) and the timings are recorded", good[4].startswith("fill 5000 open_max_nonzero 1"), good[4])
pk = (ROOT / "tools/package_dev_build.py").read_text()
m = re.search(r'b"TPRB" \+ bytes\(\(i \* (\d+) \+ (\d+)\) & 255 for i in range\(4, 200\)\)', pk)
fw = re.search(r"\(uint8_t\)\(i \* (\d+)u \+ (\d+)u\)", (ROOT / "fw2/diagnostics/preset_probe.c").read_text())
check("the firmware's byte pattern is the packager's (37 and 11 in both)", m and fw and m.groups() == fw.groups() == ("37", "11"), (m and m.groups(), fw and fw.groups()))
bad = run(lambda s: s.replace("(uint8_t)(i * 37u + 11u)", "(uint8_t)(i * 37u + 12u)"))
check("mutant (a different pattern in the firmware) is caught", bad[0].startswith("good 1 1 0 0") is False)
bad2 = run(lambda s: s.replace('"/Assets/tau_dev/alfatreze.TAU DEV "', '"/Assets/tau_dev/alfatreze.TAU DEV x"'))
check("mutant (a path without the core number) is caught", not bad2[0].endswith("/Assets/tau_dev/alfatreze.TAU DEV 212/preset-probe.bin"))
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
