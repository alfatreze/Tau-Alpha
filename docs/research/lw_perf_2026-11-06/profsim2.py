import sys, re, struct, subprocess, collections
R = "/Users/abel.santos/Downloads/DEV PROJECTS/Tau Alpha/tau-alpha"
src = open(R + "/tools/rv32sim.py").read()
src = src.replace("        while n < max_instr:\n            n += 1\n", "        H = self.H; T = self.T\n        while n < max_instr:\n            n += 1\n            H[pc] = H.get(pc, 0) + 1\n            if pc >= 0x400000:\n                ln = pc >> 5; ix = ln & 127\n                if T[ix] != ln:\n                    T[ix] = ln; self.miss += 1; self.missby[ln] = self.missby.get(ln, 0) + 1\n", 1)
ns = {"__name__": "rv32prof"}; exec(compile(src, "rv32prof", "exec"), ns)
m = ns["Machine"](); m.H = {}; m.T = [-1]*128; m.miss = 0; m.missby = {}
import importlib.util
sp = importlib.util.spec_from_file_location("devmodel", R + "/tools/p2_dev_model.py"); m.dev = importlib.util.module_from_spec(sp); sp.loader.exec_module(m.dev)
elf = sys.argv[1]; m.load_elf(elf)
if len(sys.argv) > 2: m.blob = open(sys.argv[2], "rb").read()
rc = m.run()
sys.stderr.write("[%d instructions]\n" % m.icount)
nm = subprocess.run([R + "/toolchain/xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-nm", "-n", "-S", elf], capture_output=True, text=True).stdout
syms = []
for ln in nm.splitlines():
    p = ln.split()
    if len(p) == 4 and p[2] in "tT": syms.append((int(p[0], 16), int(p[1], 16), p[3]))
if len(sys.argv) > 3:
    nm2 = subprocess.run([R + "/toolchain/xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-nm", "-n", "-S", sys.argv[3]], capture_output=True, text=True).stdout
    for ln in nm2.splitlines():
        p = ln.split()
        if len(p) == 4 and p[2] in "tT": syms.append((int(p[0], 16), int(p[1], 16), p[3]))
syms.sort()
import bisect
starts = [s[0] for s in syms]
byfn = collections.Counter(); cls = collections.Counter()
for pc, c in m.H.items():
    i = bisect.bisect_right(starts, pc) - 1
    byfn[syms[i][2] if i >= 0 and pc < syms[i][0] + max(syms[i][1], 4) else "?"] += c
    ins = m.mem[pc] | (m.mem[pc+1] << 8) | (m.mem[pc+2] << 16) | (m.mem[pc+3] << 24)
    op = ins & 0x7F; f3 = (ins >> 12) & 7; f7 = ins >> 25
    k = "alu"
    if op == 0x33 and f7 == 1: k = "div/rem" if f3 >= 4 else "mul"
    elif op == 0x03: k = "load"
    elif op == 0x23: k = "store"
    elif op == 0x63: k = "branch"
    elif op in (0x6F, 0x67): k = "jump"
    cls[k] += c
tot = sum(byfn.values())
print("TOTAL", tot)
print("CLASS", " ".join("%s=%d(%.1f%%)" % (k, v, 100.0 * v / tot) for k, v in cls.most_common()))
for k, v in byfn.most_common(14): print("FN %-28s %10d %5.1f%%" % (k, v, 100.0 * v / tot))

print("MISSES", m.miss, "icount", m.icount)
def fname(pc):
    i = bisect.bisect_right(starts, pc) - 1
    return syms[i][2] if i >= 0 and pc < syms[i][0] + max(syms[i][1], 4) else "?"
starts = [s[0] for s in syms]
print("--- top missing lines (misses, line addr, index, function)")
for ln, c in sorted(m.missby.items(), key=lambda kv: -kv[1])[:16]:
    print("%8d  %06x  idx %3d  %s" % (c, ln << 5, ln & 127, fname(ln << 5)))
hot = {}
for pc, c in m.H.items():
    if pc >= 0x400000: hot.setdefault(pc >> 5, 0); hot[pc >> 5] += c
print("distinct code lines executed", len(hot), "of 128 cache lines; lines with >1% of instructions:", sum(1 for v in hot.values() if v > 0.01 * m.icount))
byidx = {}
for ln, c in hot.items(): byidx.setdefault(ln & 127, []).append((c, ln))
conf = sum(1 for v in byidx.values() if len(v) > 1)
print("cache indices used", len(byidx), "indices shared by 2+ hot lines", conf)
# execution-weighted: lines sharing an index with another line each carrying >=0.1% of instructions
heavy = {i: [x for x in v if x[0] > 0.001 * m.icount] for i, v in byidx.items()}
pairs = [(i, v) for i, v in heavy.items() if len(v) > 1]
print("indices with 2+ lines each >=0.1% of instructions:", len(pairs))
for i, v in sorted(pairs, key=lambda iv: -sum(c for c, _ in iv[1]))[:10]:
    print("  idx %3d: " % i + ", ".join("%s@%x(%d)" % (fname(ln << 5), ln << 5, c) for c, ln in sorted(v, reverse=True)))
