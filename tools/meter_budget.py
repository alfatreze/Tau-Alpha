#!/usr/bin/env python3
"""Per-meter memory budget check: measured sizes from a firmware ELF against the ceilings in each meters/*/meter.json.

A manifest that declares `symbols` (symbol-name prefixes that belong to the meter) and `budget` (byte ceilings) is checked:
  cold         .cold_text + .cold_data        what the meter adds to the PSRAM cold image (and the SD read at boot)
  hot_rom      .text + .rodata + .data        what it adds to the on-chip image
  hot_ram      .bss + .data                   on-chip working state (the scarce resource: it comes out of the heap gap)
  psram_state  .psram_state                   its PSRAM state region
Sizes are summed from `objdump -t`, so they are attributed by symbol-name prefix: unnamed statics and compiler-generated tables are
not counted, and the eight older meters (inlined into ui_draw_dynamic_cold) are not listed. Treat the numbers as the meter's own
footprint, not the whole firmware's.

  python3 tools/meter_budget.py --elf fw/fw.elf       measure and check (needs riscv-none-elf-objdump: RISCV_TOOLCHAIN_BIN or PATH)
  python3 tools/meter_budget.py --sym dump.txt        check a saved `objdump -t` dump
  python3 tools/meter_budget.py ... --json            machine-readable
Exit status 1 if any meter is over a ceiling.
"""
import json, os, re, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CLASSES = {"cold": (".cold_text", ".cold_data"), "hot_rom": (".text", ".rodata", ".data"), "hot_ram": (".bss", ".data"), "psram_state": (".psram_state",)}


def manifests():
    out = []
    for p in sorted((ROOT / "meters").glob("*/meter.json")):
        m = json.loads(p.read_text())
        if m.get("budget") and m.get("symbols"):
            out.append(m)
    return out


def symbols(text):
    syms = []
    for ln in text.splitlines():
        p = ln.split()
        if len(p) >= 5 and p[3].startswith("."):
            try:
                size = int(p[4], 16)
            except ValueError:
                continue
            if size:
                syms.append((p[3], size, p[-1]))
    return syms


def measure(meter, syms):
    per_section = {}
    for sec, size, name in syms:
        if any(name.startswith(pre) for pre in meter["symbols"]):
            per_section[sec] = per_section.get(sec, 0) + size
    return {cls: sum(per_section.get(s, 0) for s in secs) for cls, secs in CLASSES.items()}


def check(text):
    syms = symbols(text)
    rows, bad = [], []
    for m in manifests():
        got = measure(m, syms)
        if not any(got.values()):
            bad.append("%s: no symbols matched %s (renamed? the budget would silently pass)" % (m["key"], m["symbols"]))
        row = {"meter": m["key"], "measured": got, "budget": m["budget"]}
        for cls, ceil in m["budget"].items():
            if got[cls] > ceil:
                bad.append("%s: %s is %d B, over its %d B ceiling" % (m["key"], cls, got[cls], ceil))
        rows.append(row)
    return rows, bad


def main():
    a = sys.argv[1:]
    if "--sym" in a:
        text = Path(a[a.index("--sym") + 1]).read_text()
    elif "--elf" in a:
        tool = Path(os.environ.get("RISCV_TOOLCHAIN_BIN", "")) / "riscv-none-elf-objdump" if os.environ.get("RISCV_TOOLCHAIN_BIN") else "riscv-none-elf-objdump"
        text = subprocess.run([str(tool), "-t", a[a.index("--elf") + 1]], capture_output=True, text=True, check=True).stdout
    else:
        print(__doc__); return 2
    rows, bad = check(text)
    if "--json" in a:
        print(json.dumps({"meters": rows, "problems": bad}, indent=1))
    else:
        print("%-14s %-22s %-22s %-22s %s" % ("meter", "cold (B)", "hot rom (B)", "hot ram (B)", "psram state (B)"))
        for r in rows:
            f = lambda c: "%d/%d" % (r["measured"][c], r["budget"][c]) if c in r["budget"] else "%d/-" % r["measured"][c]
            print("%-14s %-22s %-22s %-22s %s" % (r["meter"], f("cold"), f("hot_rom"), f("hot_ram"), f("psram_state")))
        print("(measured/ceiling)")
    for b in bad:
        print("OVER BUDGET: " + b)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
