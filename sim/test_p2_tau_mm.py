#!/usr/bin/env python3
"""MTR_TAU_MM (the firmware's own major.minor, what a meter's tau_min is compared with, fw2/ui/meters.c) is generated at build time from dist/Cores/alfatreze.TAU/core.json `version` by tools/tau_mm.py,
passed by fw2/build.sh, and has no hand-edited value in a firmware build: a release cannot forget it. Mutant: a build.sh without the define."""
import json, re, subprocess, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("  " + str(info) if info and not ok else "")); fails += 0 if ok else 1
ver = json.loads((ROOT / "dist/Cores/alfatreze.TAU/core.json").read_text())["core"]["metadata"]["version"]
m = re.match(r"(\d+)\.(\d+)\.\d+", ver); want = (int(m.group(1)) << 8) | int(m.group(2))
r = subprocess.run([sys.executable, str(ROOT / "tools/tau_mm.py")], capture_output=True, text=True)
check("tools/tau_mm.py prints the core.json version's major << 8 | minor (%s -> 0x%04X)" % (ver, want), r.returncode == 0 and int(r.stdout.strip(), 0) == want, r.stdout + r.stderr)
r = subprocess.run([sys.executable, str(ROOT / "tools/tau_mm.py"), "--version", "0.8.0-preview.2"], capture_output=True, text=True)
check("a pre-release version counts as its major.minor (0.8.0-preview.2 -> 0x0008)", r.returncode == 0 and int(r.stdout.strip(), 0) == 8, r.stdout + r.stderr)
r = subprocess.run([sys.executable, str(ROOT / "tools/tau_mm.py"), "--version", "garbage"], capture_output=True, text=True)
check("a version it cannot read stops the build (non-zero exit)", r.returncode != 0)
bs = (ROOT / "fw2/build.sh").read_text()
check("fw2/build.sh passes -DMTR_TAU_MM from tools/tau_mm.py", "tau_mm.py" in bs and "MTR_TAU_MM=" in bs)
src = (ROOT / "fw2/ui/meters.c").read_text()
check("meters.c has no hand-edited value for a firmware build (only the host-test default under P2_HOST)", re.search(r"#ifdef P2_HOST\s*\n#define MTR_TAU_MM", src) is not None and "#error \"MTR_TAU_MM" in src)
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
