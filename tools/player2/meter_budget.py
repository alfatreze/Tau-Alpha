#!/usr/bin/env python3
"""Per-meter memory budget of the Player 2 firmware: the section sizes of each meter's object file against the ceilings of its manifest (meters/<key>/meter.json, p2.budget).

Every meter is one object (fw2/ui/meter_<key>.o) and link.ld places its code and read-only data in the PSRAM cold image BY FILE NAME, so an object's sections ARE its footprint:
  cold         .text .rodata .srodata       what the meter adds to the cold image (loaded from the card at boot)
  hot_ram      .data .sdata .bss .sbss      on-chip working state: the scarce resource (it comes out of the free RAM above the image)
  psram_state  .psram_state                 its PSRAM state region (Layered Wave's history ring)
The framework object (meters.o: registry, presets, METR, tuning) has its own ceilings below. A ceiling is the measured size plus a margin: a meter that grows past it fails `make test-host` and
the budget is raised deliberately, with the reason in the audit trail.

  python3 tools/player2/meter_budget.py [OBJDIR]        default work/heapcheck/player2/obj (the build `make test-host` makes); needs riscv-none-elf-objdump
  python3 tools/player2/meter_budget.py --table         print the measured sizes next to the ceilings
Exit status 1 if any meter is over a ceiling or its object is missing."""
import json, os, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CLASSES = {"cold": (".text", ".rodata", ".srodata"), "hot_ram": (".data", ".sdata", ".bss", ".sbss"), "psram_state": (".psram_state",)}
FRAMEWORK = {"meters": {"hot_ram": 704, "cold": 12000}}                      # fw2/ui/meters.o: the registry, the preset and METR bookkeeping, the tuning words, the draw-cost counters


def objdump():
    tb = os.environ.get("RISCV_TOOLCHAIN_BIN")
    return str(Path(tb) / "riscv-none-elf-objdump") if tb else str(ROOT / "toolchain/xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-objdump")


def sections(obj):
    out = subprocess.run([objdump(), "-h", str(obj)], capture_output=True, text=True, check=True).stdout.split("\n")
    sec = {}
    for ln in out:
        p = ln.split()
        if len(p) >= 4 and p[0].isdigit():
            sec[p[1]] = int(p[2], 16)
    return sec


def measure(obj):
    sec = sections(obj)
    got = {}
    for cls, prefixes in CLASSES.items():
        got[cls] = sum(size for name, size in sec.items() if any(name == p or name.startswith(p + ".") for p in prefixes))
    return got


def targets():
    t = []
    for p in sorted((ROOT / "meters").glob("*/meter.json")):
        m = json.loads(p.read_text())
        if "p2" in m and not m["p2"].get("pack_only"):                  # a pack-only meter has no object in the firmware (its descriptor and code are in its pack)
            t.append(("meter_" + m["key"], m["key"], m["p2"].get("budget")))
    for obj, b in FRAMEWORK.items(): t.append((obj, obj, b))
    return t


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    objdir = Path(args[0]) if args else ROOT / "work/heapcheck/player2/obj"
    bad, rows = [], []
    for obj, name, budget in targets():
        f = objdir / (obj + ".o")
        if not f.exists():
            bad.append("%s: %s is missing (build fw2 first)" % (name, f)); continue
        if not budget:
            bad.append("%s: no p2.budget in its manifest" % name); continue
        got = measure(f); rows.append((name, got, budget))
        for cls, ceil in budget.items():
            if cls not in got: bad.append("%s: unknown budget class %r" % (name, cls))
            elif got[cls] > ceil: bad.append("%s: %s is %d B, over its %d B ceiling" % (name, cls, got[cls], ceil))
        for cls in got:
            if got[cls] and cls not in budget: bad.append("%s: %s is %d B but has no ceiling" % (name, cls, got[cls]))
    if "--table" in sys.argv or bad:
        print("%-14s %-16s %-16s %s" % ("meter", "cold (B)", "hot ram (B)", "psram state (B)"))
        for name, got, b in rows:
            f = lambda c: ("%d/%d" % (got[c], b[c])) if c in b else "%d/-" % got[c]
            print("%-14s %-16s %-16s %s" % (name, f("cold"), f("hot_ram"), f("psram_state")))
        print("(measured/ceiling)")
    for b in bad: print("OVER BUDGET: " + b)
    if not bad: print("meter budgets OK: %d objects" % len(rows))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
