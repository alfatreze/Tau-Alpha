#!/usr/bin/env python3
"""Regression guard for B-391 (docs/AUDIT_TRAIL.md): a function moving out of the cold image (losing
COLD_FN/COLD_TEXT placement, deliberately or by accident) grows the hot code and shrinks the heap gap
-- a real, silent RAM-budget regression. Last time, it was caught only because a human happened to
compare two printed "heap gap: N B" numbers before and after a refactor. This script automates that
comparison: builds each tracked firmware target, parses its own heap-gap line, and fails if the actual
gap drops more than TOLERANCE_B below the recorded baseline (tools/heap_gap_baseline.json). Free RAM
going UP, or moving by less than the tolerance, is never a failure -- only a real drop past it, so
ordinary feature growth from genuinely new code does not need constant re-baselining.

Not wired into `make test-host`: like tools/check_cold_calls.py, this is an informational tool run by
hand, deliberately -- it rebuilds every tracked target via fw/build.sh, which overwrites
dist/Assets/tau/common/tau.rom (the real shipped ROM), a side effect a plain host test should not have
without the project deciding to accept it.

Usage:
  check_heap_gap.py            # check every tracked target against the baseline
  check_heap_gap.py --update   # rebuild every tracked target and overwrite the baseline with today's numbers
"""
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BASELINE = ROOT / "tools" / "heap_gap_baseline.json"
TOLERANCE_B = 512   # a handful of bytes moving with ordinary code changes is not a regression to chase
DEFAULT_TARGETS = ["release", "player-library-diagnostic", "player-library-diagnostic-profile"]


def build_heap_gap(target):
    r = subprocess.run(["bash", "fw/build.sh", target], cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout)
        print(r.stderr, file=sys.stderr)
        sys.exit(f"fw/build.sh {target} failed")
    m = re.search(r"heap gap: (\d+) B", r.stdout)
    if not m:
        print(r.stdout)
        sys.exit(f"{target}: no 'heap gap' line in build output -- did fw/build.sh's own message change?")
    return int(m.group(1))


def main():
    update = "--update" in sys.argv
    if update:
        targets = DEFAULT_TARGETS
    elif BASELINE.exists():
        targets = sorted(json.loads(BASELINE.read_text()))
    else:
        sys.exit(f"{BASELINE} does not exist yet -- run with --update to create it")

    results = {}
    for t in targets:
        gap = build_heap_gap(t)
        results[t] = gap
        print(f"{t}: heap gap {gap} B")

    if update:
        BASELINE.write_text(json.dumps(results, indent=2, sort_keys=True) + "\n")
        print(f"baseline written to {BASELINE}")
        return

    baseline = json.loads(BASELINE.read_text())
    bad = []
    for t, gap in results.items():
        base = baseline.get(t)
        if base is None:
            print(f"{t}: no baseline recorded for this target (run --update to add it)")
            continue
        drop = base - gap
        if drop > TOLERANCE_B:
            bad.append((t, base, gap, drop))

    if bad:
        for t, base, gap, drop in bad:
            print(f"FAIL {t}: heap gap dropped {drop} B (baseline {base} B, now {gap} B) -- "
                  f"check whether something moved out of the cold image (docs/AUDIT_TRAIL.md B-391)")
        sys.exit(1)
    print("PASSED")


if __name__ == "__main__":
    main()
