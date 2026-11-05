#!/usr/bin/env python3
"""Writes mutated copies of fw2/platform/start.S for the pack watchdog test (Makefile test-rtl-wdog-soc): each removes one thing the trap path needs, and the CPU-in-the-loop test (sim/fw_wdog/main.c)
must fail on every one of them. Usage: wdog_start_mutants.py <start.S> <out dir>; prints the mutant names, one per line. Exits 1 if a mutation changed nothing (the pattern went stale)."""
import re, sys
from pathlib import Path

MUTANTS = {
    # the handler does not acknowledge the watchdog: the interrupt stays up and fires again at once
    "noack": lambda s: re.sub(r"\tli t3, 0x800001E4[^\n]*\n\tsw zero, 0\(t3\)\n", "", s),
    # the interrupt window is not closed on the way out (MPIE stays 1: MIE is 1 after the mret)
    "mie_on": lambda s: re.sub(r"\tli t3, 0x80\n\tcsrc mstatus, t3[^\n]*\n", "", s),
    # the callee-saved registers are not restored by wd_resume
    "no_restore": lambda s: re.sub(r"\tlw s[0-9]+, [0-9]+\(t0\)\n", "", s),
    # no window flag: a timer interrupt outside a guarded window is "recovered" into a stale frame
    "no_flag": lambda s: re.sub(r"\tla t3, wd_cut_flag[^\n]*\n\tlw t4, 0\(t3\)\n\tbeqz t4, 1f\n", "", s),
    # any other trap is not restarted (it spins): the unset-mtvec behaviour is lost
    "trap_spins": lambda s: s.replace("1:\tla t0, _start\n\tjr t0\n", "1:\tj 1b\n"),
}

def main():
    src = Path(sys.argv[1]).read_text()
    out = Path(sys.argv[2]); out.mkdir(parents=True, exist_ok=True)
    bad = 0
    for name, f in MUTANTS.items():
        m = f(src)
        if m == src:
            print(f"mutant {name} changed nothing", file=sys.stderr); bad = 1; continue
        (out / f"start_{name}.S").write_text(m)
        print(name)
    sys.exit(bad)

main()
