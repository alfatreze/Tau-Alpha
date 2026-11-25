#!/usr/bin/env python3
"""B-653: every firmware target must link under the flags the shipped cores are built with (192 KB link, 66.667 MHz clock). tools/check_heap_gap.py builds the 256 KB default flags, which
hid a target that no longer fitted the real 192 KB link (the profile variant, once Halcyon became a default). Builds go to work/heapcheck (fw/build.sh TAU_BUILD_OUT), never into dist/.
Usage: check_targets_link.py [target ...]   (default: the three tracked targets)"""
import os, subprocess, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
TARGETS = ["release", "player-library-diagnostic", "player-library-diagnostic-profile"]
FLAGS = {"RAM_192K": "1", "CLK66": "1", "SDRAM_BUSY": "1", "LPC_FW": "1"}
bad = []
for t in (sys.argv[1:] or TARGETS):
    env = dict(os.environ, TAU_BUILD_OUT="work/heapcheck", **FLAGS)
    r = subprocess.run(["bash", str(ROOT / "fw/build.sh"), t], cwd=ROOT, env=env, capture_output=True, text=True)
    gap = [l for l in r.stdout.splitlines() if l.startswith("heap gap")]
    if r.returncode:
        bad.append(t); print("FAIL: %s does not link under the 192 KB flags" % t); print("\n".join((r.stdout + r.stderr).splitlines()[-6:]))
    else:
        print("ok   %s links (%s)" % (t, gap[0] if gap else "no gap line"))
sys.exit(1 if bad else 0)
