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


def macro_on(text: str) -> str:
    """Keep the TAU_PSRAM_* branch, drop the `else` branch."""
    out, stack = [], []
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
        if any(psram and in_else for psram, in_else in stack):
            continue
        out.append(line)
    return "\n".join(out)


def check_macro_on(top: Path, game: Path) -> int:
    """With TAU_PSRAM_PROBE on: core_top must not also drive the CRAM pins, and
    the probe instance in core_game.vh must connect every CRAM signal of both chips."""
    bad = 0
    doubled = re.findall(r"assign\s+(cram[01]_\w+)\s*=", macro_on(top.read_text()))
    for n in doubled:
        print(f"FAIL: core_top still drives {n} with the probe enabled (double driver)")
        bad += 1
    g = macro_on(game.read_text())
    m = re.search(r"tau_psram_probe\s*(?:#\s*\(.*?\)\s*)?u_psram\s*\((.*?)\);\s*$", g, re.S | re.M)
    if not m:
        print("FAIL: no tau_psram_probe u_psram instance in the TAU_PSRAM_PROBE branch")
        return bad + 1
    conn = set(re.findall(r"\.(cram[01]_\w+)\s*\(", m.group(1)))
    for chip in (0, 1):
        for sig in list(IDLE) + ["wait"]:
            if f"cram{chip}_{sig}" not in conn:
                print(f"FAIL: probe instance does not connect cram{chip}_{sig}")
                bad += 1
    return bad


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
    if len(sys.argv) <= 1:
        on = check_macro_on(path, path.parent / "core_game.vh")
        print("PASS: probe branch has no double drivers and connects every CRAM pin" if not on
              else f"FAILED ({on} problems in the probe branch)")
        bad += on
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
