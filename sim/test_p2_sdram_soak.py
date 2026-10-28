#!/usr/bin/env python3
"""fw2/cores/sdram_soak_core.h (the SDRAM soak of S0, D-T09) on the host against a memory model with injected faults: a clean memory gives no failure over several passes; a flipped bit, a stuck word, an
address-line alias (two addresses share a cell) and a dropped write are each caught, the first failing address is the right one; the passes advance. Four mutants of the core (no comparison, a pattern that
ignores the address, reads of the next pair, a seed that never changes) must each let a fault through or fail the clean run."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
C = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sdram_soak_core.h"
#define PAIRS 4096u
#define BASE 0x1000u
static uint32_t mem[2u * PAIRS + 64u]; static int fault, faddr, fval; static uint32_t stuck_pass_val;
static int wr(uint32_t a, uint32_t d)
{
    uint32_t k = a - BASE;
    if (fault == 4 && a == (uint32_t)faddr) return 0;                       /* a dropped write */
    if (fault == 3 && a == (uint32_t)faddr) k = (uint32_t)(fval - BASE);            /* an address-line fault: two addresses share one cell */
    mem[k] = d; return 0;
}
static int rd(uint32_t a, uint32_t *o)
{
    uint32_t k = a - BASE;
    if (fault == 3 && a == (uint32_t)faddr) k = (uint32_t)(fval - BASE);
    *o = mem[k];
    if (fault == 1 && a == (uint32_t)faddr) *o ^= 1u << 7;                  /* a flipped bit */
    if (fault == 2 && a == (uint32_t)faddr) *o = 0xDEADBEEFu;               /* a stuck word */
    return 0;
}
static void run(int f, int fa, int fv, int passes, const char *name)
{
    sk_t s; memset(mem, 0, sizeof mem); fault = f; faddr = fa; fval = fv;
    for (uint32_t i = 0; i < PAIRS; i++) mem[2u * i] = sk_pat(12345u, i);     /* what a previous run left behind: a dropped write is only visible once the seed has changed */
    sk_init(&s, BASE, PAIRS, 12345u);
    while (s.pass < (uint32_t)passes) sk_step(&s, wr, rd, 64u);
    printf("%s %u %u %08x %u\n", name, s.fails, s.pass, s.first_addr, s.tmo);
}
int main(void)
{
    run(0, 0, 0, 3, "clean");
    run(1, BASE + 2u * 100u, 0, 3, "flip");
    run(2, BASE + 2u * 2000u, 0, 3, "stuck");
    run(3, BASE + 2u * 10u, BASE + 2u * 11u, 3, "alias");
    run(4, BASE + 2u * 3000u, 0, 3, "drop");
    return 0;
}
'''
def run(bug):
    with tempfile.TemporaryDirectory() as td:
        d = Path(td); (d / "t.c").write_text(C)
        r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", f"-DBUG={bug}", "-I", str(ROOT / "fw2/cores"), "-o", str(d / "t"), str(d / "t.c")], capture_output=True, text=True)
        if r.returncode: print(r.stderr); sys.exit(1)
        out = subprocess.run([str(d / "t")], capture_output=True, text=True, timeout=60).stdout.strip().split("\n")
        return {l.split()[0]: l.split()[1:] for l in out}
fails = 0
def check(n, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + n); fails += 0 if ok else 1
r = run(0)
check("a clean memory: no failure over 3 passes, the passes advance", r["clean"][0] == "0" and int(r["clean"][1]) == 3 and r["clean"][3] == "0")
check("a flipped bit is caught, at the right address, once per pass", int(r["flip"][0]) == 3 and int(r["flip"][2], 16) == 0x1000 + 200)
check("a stuck word is caught at its address", int(r["stuck"][0]) == 3 and int(r["stuck"][2], 16) == 0x1000 + 4000)
check("an address alias is caught", int(r["alias"][0]) >= 3)
check("a dropped write is caught (the old pass's data stays: needs the seed to change)", int(r["drop"][0]) >= 2)
for bug, name, key in ((1, "no comparison", "flip"), (2, "a pattern that ignores the address", "alias"), (3, "reads of the next pair", "clean"), (4, "a seed that never changes", "drop")):
    m = run(bug)
    caught = (int(m[key][0]) == 0) if key != "clean" else (int(m["clean"][0]) != 0)
    check(f"mutant ({name}) is caught: {'the fault goes unseen' if key != 'clean' else 'the clean memory fails'}", caught)
print("PASSED" if not fails else f"FAILED ({fails})"); sys.exit(1 if fails else 0)
