#!/usr/bin/env python3
"""B-653: what a bitstream was BUILT WITH, recorded next to it, and the checks that use it.

* `macros_of(qsf_text)`: every VERILOG_MACRO in a Quartus settings text (name only, `NAME=value` and bare `NAME` both).
* `dead_macros(bundle_text)`: the macros a fit bundle defines that no RTL file under src/fpga ever reads. A macro nobody reads still builds, and quietly measures
  something other than what the bundle's name promises (tools/blit_g3_..._gain_eq24_... kept defining TAU_EQ_COEF24 after the RTL that read it was deleted).
* `write_manifest(path, ...)` / `load_manifest(rbf)`: `<rbf>.json` beside a collected RBF: its macros, seed, commit, raw SHA-256.
* `FEATURES`: which firmware feature (the TAUFWNEED list inside the ROM, fw/player.c) needs which bitstream macro.

CLI: `fit_manifest.py check-bundles [FILE ...]` (default: every live tools/*qsf_append*.txt; the archive folder is not checked)."""
import json, re, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FEATURES = {"HALCYON": "TAU_HALCYON", "LPC": "TAU_LPC", "POLY": "TAU_POLY", "SDRAM_BUSY": "TAU_SDRAM_BUSY"}


def macros_of(text):
    out = []
    for m in re.finditer(r'^\s*set_global_assignment\s+-name\s+VERILOG_MACRO\s+"?([A-Za-z_]\w*)(?:\s*=\s*([^"\s]+))?"?', text, re.M):
        if not (m.group(2) is not None and m.group(2) == "0"):       # NAME=0 is defined but off
            out.append(m.group(1))
    return sorted(set(out))


def used_macros(src=None):
    src = src or ROOT / "src/fpga"
    used = set()
    for f in list(src.rglob("*.v")) + list(src.rglob("*.sv")) + list(src.rglob("*.vh")):
        t = f.read_text(errors="ignore")
        used |= set(re.findall(r"`(\w+)", t)) | set(re.findall(r"`(?:ifdef|ifndef|elsif|undef)\s+(\w+)", t))
    return used


def dead_macros(bundle_text, used=None):
    used = used if used is not None else used_macros()
    return [m for m in macros_of(bundle_text) if m not in used]


def git_state():
    try:
        c = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
        d = bool(subprocess.run(["git", "status", "--porcelain", "src/fpga"], cwd=ROOT, capture_output=True, text=True).stdout.strip())
        return c, d
    except Exception:
        return "unknown", True


def write_manifest(path, name, seed, macros, rbf_sha256, append=None):
    c, d = git_state()
    Path(path).write_text(json.dumps({"name": name, "seed": seed, "macros": sorted(macros), "rbf_sha256": rbf_sha256, "append": append,
                                      "commit": c, "rtl_dirty": d}, indent=1) + "\n")


def load_manifest(rbf):
    p = Path(str(rbf) + ".json")
    return json.loads(p.read_text()) if p.is_file() else None


def live_bundles():
    return sorted((ROOT / "tools").glob("*qsf_append*.txt"))


def main(argv):
    if argv[:1] != ["check-bundles"]:
        sys.exit(__doc__)
    files = [Path(a) for a in argv[1:]] or live_bundles()
    used, bad = used_macros(), 0
    for f in files:
        dead = dead_macros(f.read_text(), used)
        if dead:
            bad += 1
            print(f"FAIL: {f.name} defines macros no RTL reads: {', '.join(dead)} (archive the bundle to tools/fit_bundles_archive/ or remove the line)")
    print(("FAIL" if bad else "ok  ") + f" {len(files)} live fit bundle(s) checked, {bad} with dead macros")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
