#!/usr/bin/env python3
"""settings_service() (fw2/settings/settings.c): a setting changed while the music streams is published after one second (it used to wait for the music to stop, so a setting changed during playback was lost when the core
was quit: the Appearance settings did not save); with nothing streaming it is published at once; nothing changed stores nothing. The last played track (lazy fields) is saved at once when nothing streams, after 8 s on the same track while it does (a new track restarts the wait), and rides along when a setting is published. Mutants: the old rule (never while streaming); a track saved like a setting (after 1 s)."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
def run(mutant=0):
    with tempfile.TemporaryDirectory() as td:
        d = Path(td); (d / "fw2/settings").mkdir(parents=True); (d / "fw2/platform").mkdir(); (d / "fw2/cores").mkdir(); (d / "sim").mkdir()
        src = (ROOT / "fw2/settings/settings.c").read_text()
        if mutant == 1:
            a = "if (!streaming || (int32_t)(cycles() - dirty_t0) >= (int32_t)CLK_HZ) { publish(vals); was_dirty = 0; }"; assert a in src
            src = src.replace(a, "if (!streaming) { publish(vals); was_dirty = 0; }")
        if mutant == 2: a = "(int32_t)(cycles() - lazy_t0) >= (int32_t)LAZY_STREAMING_CYC"; assert a in src; src = src.replace(a, "(int32_t)(cycles() - lazy_t0) >= (int32_t)CLK_HZ")
        (d / "fw2/settings/settings.c").write_text(src)
        for f in ("settings/settings.h", "platform/mmio.h", "cores/settings_core.h"): (d / "fw2" / f).write_text((ROOT / "fw2" / f).read_text())
        (d / "sim/h.c").write_text((ROOT / "sim/p2_settings_service_harness.c").read_text())
        r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-o", str(d / "h"), str(d / "sim/h.c")], capture_output=True, text=True)
        if r.returncode: print(r.stderr); sys.exit(1)
        return subprocess.run([str(d / "h")], capture_output=True, text=True).stdout.strip().splitlines()
GOOD = ["start stores 0", "streaming t+0 stores 0 reg1 0", "streaming t+0.5s stores 0 reg1 0", "streaming t+1s stores 1 reg1 7", "idle stores 2 reg1 9", "revert stores 2 reg1 9", "track streaming t+2s stores 0 reg19 0", "track streaming, new track, 5s later stores 0 reg20 0", "track streaming, 9s on the same track stores 1 reg20 43", "track not streaming stores 2 reg20 44", "a setting published with the track stores 3 reg1 13 reg20 45"]
fails = 0
out = run(0)
for g, o in zip(GOOD, out):
    ok = g == o; print(("ok   " if ok else "FAIL ") + g + ("" if ok else "   got: " + o)); fails += 0 if ok else 1
if len(out) != len(GOOD): print("FAIL line count"); fails += 1
bad = run(1)
ok = bad != out; print(("ok   " if ok else "FAIL ") + "mutant (never while streaming) is caught"); fails += 0 if ok else 1
bad2 = run(2); ok = bad2 != out; print(("ok   " if ok else "FAIL ") + "mutant (a track saved after 1 s like a setting) is caught"); fails += 0 if ok else 1
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
