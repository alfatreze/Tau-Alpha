#!/usr/bin/env python3
"""The gapless boundary control flow (G2b, B-750): sim/pcm_ring_gp_harness.c compiles the REAL fw/pcm_ring_fw.inc and fw/pcm_ring_state.h against stubs of the player and drives the sequence the
player runs around a natural end of track (end-of-file marker, gapless load, display swap, hand-off without a flush, the marker following the heard count, a flush bringing it forward, the fallbacks).
Ten mutants of the firmware text must each fail it."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
MUTANTS = [
    ("fw/pcm_ring_fw.inc", "    gp_save(&gp_new);\n    gp_load(&gp_old);\n    gp_pend = 1u;", "    gp_save(&gp_new);\n    gp_pend = 1u;", "the old display is not put back after a gapless load"),
    ("fw/pcm_ring_fw.inc", "    if (!prh_marker_reached(heard, gp_marker)) return;\n    gp_pend = 0u;", "    gp_pend = 0u;", "the boundary applies without waiting for the marker"),
    ("fw/pcm_ring_fw.inc", "    rg_playout();\n    pcm_flush();\n    paused = 0;", "    rg_playout();\n    paused = 0;", "the fallback does not flush"),
    ("fw/pcm_ring_fw.inc", "    refill_drain();\n    if (track_fmt == FMT_FLAC) {\n        if (!flac_restart())", "    pcm_flush(); refill_drain();\n    if (track_fmt == FMT_FLAC) {\n        if (!flac_restart())", "the gapless hand-off flushes"),
    ("fw/pcm_ring_state.h", "    if (gp_pend) gp_marker = rg_fed;", "", "a flush does not bring the marker forward"),
    ("fw/pcm_ring_fw.inc", "    gp_marker = rg_put_total;", "    gp_marker = 0u;", "the marker is not the put count"),
    ("fw/pcm_ring_fw.inc", "    if (FB_HELD()) return;\n    const uint32_t heard", "    const uint32_t heard", "the boundary applies under an overlay"),
    ("fw/pcm_ring_fw.inc", "    if (!opened) { gp_load(&gp_old); return 0; }", "    if (!opened) { return 0; }", "a failed gapless open leaves the new display"),
    ("fw/pcm_ring_fw.inc", "gp_aborted++;\n    rg_playout();", "gp_aborted++;", "the fallback does not let the old tail play out"),
    ("fw/pcm_ring_state.h", "    if ((uint32_t)(t - rg_ui_last) < (CLK_HZ / 50u)) return 0;", "", "the draw throttle never skips"),
]
def build_run(tmp, mutate=None):
    d = Path(tmp); (d / "fw").mkdir(exist_ok=True); (d / "sim").mkdir(exist_ok=True)
    for f in ("fw/pcm_ring_hw.h", "fw/pcm_ring_state.h", "fw/pcm_ring_fw.inc", "sim/pcm_ring_gp_harness.c"):
        (d / f).write_text((ROOT / f).read_text())
    if mutate:
        f, a, b = mutate; t = (d / f).read_text(); assert a in t, (f, a[:50]); (d / f).write_text(t.replace(a, b, 1))
    r = subprocess.run(["cc", "-O1", "-w", "-o", str(d / "h"), str(d / "sim/pcm_ring_gp_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return subprocess.run([str(d / "h")], capture_output=True, text=True, timeout=30)
def main():
    fails = 0
    with tempfile.TemporaryDirectory() as t:
        r = build_run(t); sys.stdout.write(r.stdout); fails += r.returncode != 0
    for f, a, b, name in MUTANTS:
        with tempfile.TemporaryDirectory() as t:
            r = build_run(t, (f, a.replace("\\n", "\n"), b.replace("\\n", "\n")))
            caught = r.returncode != 0
            print(("ok   mutant caught: " if caught else "FAIL mutant NOT caught: ") + name); fails += not caught
    print("PASSED" if not fails else f"{fails} FAILED"); sys.exit(1 if fails else 0)
if __name__ == "__main__": main()
