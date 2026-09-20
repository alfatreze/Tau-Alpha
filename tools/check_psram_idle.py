#!/usr/bin/env python3
"""Static P0 check: with every TAU_PSRAM_* macro OFF, core_top.v must drive all
cram0_*/cram1_* outputs to the idle values (chips deselected, DQ high-Z, async
mode). This is a source check, not a simulation of the fitted netlist.

If a `ifdef TAU_PSRAM...` block is added later, only its `else` branch (the
macro-off path) is checked here."""
import re
import sys
from pathlib import Path

IDLE = {
    "a": "6'h0", "dq": "16'hZZZZ", "clk": "1'b0", "adv_n": "1'b1", "cre": "1'b0",
    "ce0_n": "1'b1", "ce1_n": "1'b1", "oe_n": "1'b1", "we_n": "1'b1",
    "ub_n": "1'b1", "lb_n": "1'b1",
}


def macro_off(text: str) -> str:
    out, stack = [], []          # stack of (is_psram_macro, in_else)
    for line in text.splitlines():
        s = line.strip()
        m = re.match(r"`ifn?def\s+(\w+)", s)
        if m:
            stack.append([m.group(1).startswith("TAU_PSRAM") and not s.startswith("`ifndef"), False])
            continue
        if s.startswith("`else") and stack:
            stack[-1][1] = True
            continue
        if s.startswith("`endif") and stack:
            stack.pop()
            continue
        if any(psram and not in_else for psram, in_else in stack):
            continue
        out.append(line)
    return "\n".join(out)


def main() -> int:
    path = Path(sys.argv[1] if len(sys.argv) > 1 else "src/fpga/core/core_top.v")
    text = macro_off(path.read_text())
    assigns = dict(re.findall(r"assign\s+(cram[01]_\w+)\s*=\s*([^;]+);", text))
    bad = 0
    for chip in (0, 1):
        for sig, want in IDLE.items():
            name = f"cram{chip}_{sig}"
            got = assigns.get(name)
            if got is None or got.strip() != want:
                print(f"FAIL: {name} = {got!r}, expected {want}")
                bad += 1
    print("PASS: all cram0_/cram1_ outputs idle with PSRAM macros off" if not bad
          else f"FAILED ({bad} pins)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
