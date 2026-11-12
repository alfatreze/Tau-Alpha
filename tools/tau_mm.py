#!/usr/bin/env python3
"""The firmware's own TAU version as major << 8 | minor, in hex, for fw2/build.sh (-DMTR_TAU_MM=...): what a meter's `tau_min` is compared with (docs/features/meters/EXTERNAL_METERS_SPEC.md section 2).
Read from dist/Cores/alfatreze.TAU/core.json `version` (the release being worked toward), so a release cannot forget to raise it.   python3 tools/tau_mm.py [--version X.Y.Z[-tag]]"""
import json, re, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
def main(argv):
    ver = argv[1] if len(argv) == 2 and False else (argv[argv.index("--version") + 1] if "--version" in argv else json.loads((ROOT / "dist/Cores/alfatreze.TAU/core.json").read_text())["core"]["metadata"]["version"])
    m = re.match(r"(\d+)\.(\d+)\.\d+", ver)
    if not m or int(m.group(1)) > 255 or int(m.group(2)) > 255:
        print("cannot read a major.minor from version %r" % ver, file=sys.stderr); return 1
    print("0x%04X" % ((int(m.group(1)) << 8) | int(m.group(2)))); return 0
if __name__ == "__main__":
    sys.exit(main(sys.argv))
