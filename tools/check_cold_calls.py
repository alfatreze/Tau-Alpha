#!/usr/bin/env python3
"""Lists every direct call from hot code (on-chip RAM) into cold code (PSRAM alias 0x24800000..), from a linked fw.elf.
Each of these is an entry into cold code that must be gated on COLD_READY() or on state that implies it (library loaded, menu
open); review the list when it changes (Phase G4, docs/PHASE_G_SPEC.md section 4.4). Usage: check_cold_calls.py [fw.elf] [--callers]"""
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
