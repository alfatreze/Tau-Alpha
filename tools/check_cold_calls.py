#!/usr/bin/env python3
"""Lists every direct call from hot code (on-chip RAM) into cold code (PSRAM alias 0x24800000..), from a linked fw.elf.
Each of these is an entry into cold code that must be gated on COLD_READY() or on state that implies it (library loaded, menu
open); review the list when it changes (Phase G4, docs/PHASE_G_SPEC.md section 4.4). Usage: check_cold_calls.py [fw.elf] [--callers]

Also lists Helios region redraw callbacks (fw/helios.inc, docs/AUDIT_TRAIL.md B-391): helios_flush() calls these through a
STORED FUNCTION POINTER, an indirect jalr with no static target for objdump to resolve -- invisible to the disassembly
scan above no matter how the regex is tuned, a real limit of static disassembly, not a fixable gap in this script's
pattern matching. Found instead by scanning source for helios_region_register*() call sites and cross-referencing each
callback's placement via nm. This is exactly the kind of change that regressed silently in B-391 (a redraw callback lost
its COLD_FN3 placement when it was extracted into its own function, caught only by the heap-gap number moving, not by
any tool) -- this section exists so a reviewer sees the placement directly next time instead of relying on that."""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BIN = ROOT / "toolchain/xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-"
elf = next((a for a in sys.argv[1:] if not a.startswith("--")), str(ROOT / "fw/fw.elf"))
dis = subprocess.run([str(BIN) + "objdump", "-d", "--no-show-raw-insn", elf], capture_output=True, text=True, check=True).stdout
cur, calls, hotfns = None, {}, set()
for ln in dis.splitlines():
    m = re.match(r"^([0-9a-f]+) <([^>]+)>:", ln)
    if m:
        cur = (int(m.group(1), 16), m.group(2))
        continue
    if cur is None or cur[0] >= 0x40000:
        continue                                  # only callers in hot RAM
    m = re.search(r"\b(?:jal|jalr|j|tail)\b.*?(?:#\s*)?([0-9a-f]{8})\s+<([^>]+)>", ln)
    if m and 0x24000000 <= int(m.group(1), 16) < 0x26000000:
        calls.setdefault(m.group(2), set()).add(cur[1])
if not calls:
    print("no direct hot -> cold calls found (or the disassembly does not annotate them)")
for callee in sorted(calls):
    print(f"{callee}: called from " + ", ".join(sorted(calls[callee])))
print(f"{len(calls)} cold functions entered from hot code")

FW = ROOT / "fw"
region_pat = re.compile(r"helios_region_register(_rows(?:_cold)?)?\(\s*([A-Za-z_][A-Za-z0-9_]*)")
region_calls = []
for src in sorted(FW.glob("*.inc")) + [FW / "player.c"]:
    if src.name == "helios.inc":            # the mechanism's own definitions and internal delegation
        continue                            # calls (helios_region_register() -> ..._rows(fn, ...)), not real call sites
    text = src.read_text()
    for m in region_pat.finditer(text):
        region_calls.append((src.name, m.group(2), "_cold" in (m.group(1) or "")))

if region_calls:
    sym_out = subprocess.run([str(BIN) + "nm", elf], capture_output=True, text=True, check=True).stdout
    addr_of = {}
    for ln in sym_out.splitlines():
        parts = ln.split()
        if len(parts) == 3:
            addr_of[parts[2]] = int(parts[0], 16)
    print()
    print("Helios region redraw callbacks (see this script's own docstring for why these are separate):")
    flagged = 0
    for src_name, fn, registered_cold in region_calls:
        addr = addr_of.get(fn)
        placement = "not found in symbol table" if addr is None else ("cold" if addr >= 0x24000000 else "hot")
        note = ""
        if registered_cold and placement == "hot":
            note = "  <-- registered _cold but the callback is placed HOT (harmless: the requires_cold gate just never fires)"
        elif not registered_cold and placement == "cold":
            note = "  <-- registered WITHOUT _cold but the callback is placed COLD: helios_flush() calls this with no gate at all -- verify every helios_mark_dirty() call site for this region is itself reached only after cold code is ready"
            flagged += 1
        print(f"  {fn} ({src_name}, registered {'cold-aware' if registered_cold else 'plain'}): {placement}{note}")
    if flagged:
        print(f"{flagged} region callback(s) worth double-checking by hand")
