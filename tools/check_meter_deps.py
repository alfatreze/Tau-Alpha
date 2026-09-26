#!/usr/bin/env python3
"""Layer check for the meter code (docs/METER_MODULE_SPEC.md section 14): engine primitives <- meter_core <- meter modules <- host.
Fails if a lower layer reaches upward or a module reaches past its contract.

  fw/meter_core.h        portable: only <stdint.h>; no MMIO (REG(, R_*), no fb_*, no player.c state, no float
  fw/meters/*.inc        modules: no REG(, no raw framebuffer/SDRAM pointers, no other module's include, colours only via
                         the role table (no 0x____u colour literals); they draw through mtr_* only
"""
import re, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
bad = []


def strip(s):
    s = re.sub(r"/\*.*?\*/", "", s, flags=re.S)
    return re.sub(r"//[^\n]*", "", s)


core = ROOT / "fw" / "meter_core.h"
if core.exists():
    t = strip(core.read_text())
    for inc in re.findall(r'#include\s*[<"]([^>"]+)[>"]', t):
        if inc != "stdint.h":
            bad.append(f"meter_core.h includes {inc}: the core may only include <stdint.h>")
    for pat, why in [(r"\bREG\s*\(", "MMIO access"), (r"\bR_[A-Z0-9_]+\b", "a hardware register"), (r"\bfb_[a-z_]+\s*\(", "an engine call"),
                     (r"\bui_[a-z_]+\b", "player.c UI state"), (r"\b(float|double)\b", "floating point (rv32im has none)"),
                     (r"0xA0[0-9A-Fa-f]{6}", "a raw SDRAM address")]:
        if re.search(pat, t):
            bad.append(f"meter_core.h uses {why}")

mods = sorted((ROOT / "fw" / "meters").glob("*.inc")) if (ROOT / "fw" / "meters").is_dir() else []
for m in mods:
    t = strip(m.read_text())
    for pat, why in [(r"\bREG\s*\(", "MMIO access"), (r"\bfb_[a-z_]+\s*\(", "an engine call (use mtr_*)"),
                     (r"0x[0-9A-Fa-f]{4}u\b", "a colour literal (use the role table)"), (r"0xA0[0-9A-Fa-f]{6}", "a raw SDRAM address")]:
        if re.search(pat, t):
            bad.append(f"{m.name} uses {why}")
    for inc in re.findall(r'#include\s*"([^"]+)"', t):
        if inc.startswith("meters/") or (inc.endswith(".inc") and inc != "meter_core.h"):
            bad.append(f"{m.name} includes {inc}: a module may include only meter_core.h")

if bad:
    print("meter layer violations:\n  " + "\n  ".join(bad), file=sys.stderr)
    sys.exit(1)
print(f"meter layers OK (core {'present' if core.exists() else 'not yet'}, {len(mods)} module file(s))")
