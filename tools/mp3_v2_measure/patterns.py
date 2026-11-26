#!/usr/bin/env python3
"""Instruction-count model for hypothetical custom instructions, over a profile written by rvprof (prof.txt) and the ELF's symbols.
Usage: patterns.py <harness.elf> <prof.txt> <frames>
For each hot function it counts, DYNAMICALLY (execution counts from the profile, patterns found in the static instruction stream):
  MACH   : a mul/mulh/mulhsu/mulhu whose result is added/subtracted within the next 3 static instructions (a fused multiply-high-accumulate would drop the add)
  CLZ    : calls to the software count-leading-zeros (__clz_tab users: a `call` into a function reading the table is counted by symbol)
  SHSAT  : a variable or constant right shift (srai/srli) followed within 3 instructions by a clamp pattern (slt/slti + branch) -- rounding-shift/saturate candidates
  SHIFTADD: slli/srli/srai immediately followed by add/sub/or/and of that register (a shift-and-add; bit-extract candidates when and/mask follows)
These are model counts, not measurements of a CPU; the saving per fused pattern is applied by the caller (see the scoping report section 5)."""
import bisect, re, subprocess, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
BIN = Path(subprocess.check_output([sys.executable, str(ROOT / "tools/toolchain.py"), "bin"], text=True).strip().splitlines()[-1])
elf, prof, frames = sys.argv[1], sys.argv[2], int(sys.argv[3])
nm = subprocess.run([str(BIN / "riscv-none-elf-nm"), "-S", "-n", elf], capture_output=True, text=True).stdout
syms = sorted((int(p[0], 16), int(p[1], 16), re.sub(r"\.(constprop|isra|part)\.\d+", "", p[3])) for p in (l.split() for l in nm.splitlines()) if len(p) == 4 and p[2] in "tT")
starts = [s[0] for s in syms]
ins, cnt = {}, {}
for l in Path(prof).read_text().splitlines():
    pc, w, n, c, t = l.split()[:5]; ins[int(pc, 16)] = int(w, 16); cnt[int(pc, 16)] = int(n)   # rvprof writes two more columns (I$ and D$ misses) since WP-05
def fn(pc):
    i = bisect.bisect_right(starts, pc) - 1
    return syms[i][2] if i >= 0 else "?"
def f(w): return (w & 0x7f, (w >> 7) & 31, (w >> 12) & 7, (w >> 15) & 31, (w >> 20) & 31, w >> 25)
res = {}
for pc in sorted(ins):
    w = ins[pc]; op, rd, f3, rs1, rs2, f7 = f(w); n = cnt[pc]; name = fn(pc)
    r = res.setdefault(name, {"MACH": 0, "SHSAT": 0, "SHIFTADD": 0, "instr": 0}); r["instr"] += n
    ismul = op == 0x33 and f7 == 1 and f3 < 4
    if ismul and rd:
        for k in range(1, 4):
            q = pc + 4 * k
            if q not in ins: break
            o2, d2, f32, a2, b2, g2 = f(ins[q])
            if o2 == 0x33 and f32 == 0 and g2 in (0, 0x20) and rd in (a2, b2): r["MACH"] += n; break
    if op == 0x13 and f3 in (1, 5) and rd:
        q = pc + 4
        if q in ins:
            o2, d2, f32, a2, b2, g2 = f(ins[q])
            if (o2 == 0x33 and f32 in (0, 6, 7) and rd in (a2, b2)) or (o2 == 0x13 and f32 in (7,) and a2 == rd): r["SHIFTADD"] += n
    if op == 0x13 and f3 == 5 and rd:   # shift right then a compare/branch soon: a saturate
        for k in range(1, 4):
            q = pc + 4 * k
            if q not in ins: break
            o2, d2, f32, a2, b2, g2 = f(ins[q])
            if (o2 == 0x63 and rd in (a2, b2)) or (o2 == 0x13 and f32 in (2, 3) and a2 == rd): r["SHSAT"] += n; break
print(f"{'function':32s} {'instr/fr':>9s} {'MACH/fr':>8s} {'SHIFTADD/fr':>11s} {'SHSAT/fr':>9s}")
for name, r in sorted(res.items(), key=lambda kv: -kv[1]["instr"])[:14]:
    print(f"{name:32s} {r['instr']/frames:9.0f} {r['MACH']/frames:8.0f} {r['SHIFTADD']/frames:11.0f} {r['SHSAT']/frames:9.0f}")
