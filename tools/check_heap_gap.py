#!/usr/bin/env python3
"""Heap-gap guard for Player 2 (rule 6 of docs/PROJECT_RULES.md: measure against the shipped link).

fw2/build.sh has ONE configuration, the shipped 192 KB / CLK66 link (there is no 256 KB variant to mistake for it), in two builds: DIAG=0 (the normal build) and
DIAG=1 (the Diagnostic Build). The heap gap of a build is the free RAM between the end of the image and the reserved zone under the tag buffer:
`_heap_limit - _image_end` from the linked ELF (fw2/platform/link.ld). The link itself already refuses an image that leaves less than `_min_free`; this script also
catches a slow shrink: it fails when a build's gap falls more than TOLERANCE_B below the recorded baseline (tools/heap_gap_baseline.json), so a function that
leaves the cold image, or a feature that eats the margin, shows up here and not on a Pocket. Free RAM going up, or moving by less than the tolerance, never fails.

(Until D-T20 this script built the old player's fw/build.sh targets with RAM_192K=1 CLK66=1 SDRAM_BUSY=1; that player and its baseline are archived in legacy-harpmudd/.)

Builds go to work/heapcheck/gap (TAU_BUILD_OUT), never into dist/.

Usage:
  check_heap_gap.py            # build DIAG=0 and DIAG=1, check against the baseline
  check_heap_gap.py --update   # rebuild and overwrite the baseline
  check_heap_gap.py --elf FILE # the gap of an existing ELF (no build)
"""
import json
import os
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import toolchain  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
BASELINE = ROOT / "tools" / "heap_gap_baseline.json"
TOLERANCE_B = 512   # a handful of bytes moving with ordinary code changes is not a regression to chase
BUILDS = {"fw2-diag0": "0", "fw2-diag1": "1"}


def gap_from_nm(text):
    """(heap gap, link floor) in bytes from `nm` output of fw2.elf."""
    sym = {}
    for line in text.splitlines():
        p = line.split()
        if len(p) == 3 and p[2] in ("_heap_limit", "_image_end", "_min_free"):
            sym[p[2]] = int(p[0], 16)
    for k in ("_heap_limit", "_image_end", "_min_free"):
        if k not in sym:
            sys.exit(f"nm output has no {k}: not a Player 2 ELF?")
    return sym["_heap_limit"] - sym["_image_end"], sym["_min_free"]


def verdict(name, gap, floor, baseline):
    """(ok, text) for one build."""
    if gap < floor:
        return False, f"FAIL {name}: heap gap {gap} B is under the link floor {floor} B"
    base = baseline.get(name)
    if base is None:
        return True, f"{name}: heap gap {gap} B (no baseline recorded: run --update)"
    if base - gap > TOLERANCE_B:
        return False, f"FAIL {name}: heap gap dropped {base - gap} B (baseline {base} B, now {gap} B): did something leave the cold image (docs/AUDIT_TRAIL.md B-391)?"
    return True, f"{name}: heap gap {gap} B (baseline {base} B)"


def elf_gap(elf):
    nm = toolchain.riscv_bin() / "riscv-none-elf-nm"
    r = subprocess.run([str(nm), str(elf)], capture_output=True, text=True, check=True)
    return gap_from_nm(r.stdout)


def build(diag):
    out = ROOT / "work" / "heapcheck" / "gap" / f"d{diag}"
    env = dict(os.environ, DIAG=diag, TAU_BUILD_OUT=str(out))   # never dist/
    r = subprocess.run(["bash", "fw2/build.sh"], cwd=ROOT, capture_output=True, text=True, env=env)
    if r.returncode:
        print(r.stdout)
        print(r.stderr, file=sys.stderr)
        sys.exit(f"fw2/build.sh DIAG={diag} failed")
    return out / "player2" / "fw2.elf"


def main():
    args = sys.argv[1:]
    if "--elf" in args:
        gap, floor = elf_gap(args[args.index("--elf") + 1])
        print(f"heap gap {gap} B (link floor {floor} B)")
        return
    results = {}
    for name, diag in BUILDS.items():
        gap, floor = elf_gap(build(diag))
        results[name] = (gap, floor)
    if "--update" in args:
        BASELINE.write_text(json.dumps({k: v[0] for k, v in results.items()}, indent=2, sort_keys=True) + "\n")
        print(f"baseline written to {BASELINE}: " + ", ".join(f"{k} {v[0]} B" for k, v in results.items()))
        return
    baseline = json.loads(BASELINE.read_text()) if BASELINE.exists() else {}
    ok = True
    for name, (gap, floor) in results.items():
        good, text = verdict(name, gap, floor, baseline)
        print(text)
        ok &= good
    if not ok:
        sys.exit(1)
    print("PASSED")


if __name__ == "__main__":
    main()
